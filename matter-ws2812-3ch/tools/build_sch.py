#!/usr/bin/env python3
"""Generate hardware/<project>.kicad_sch from design.py (KiCad 9 format).

Every connected pin gets a short wire stub that ends in a net label or a power
symbol, so connectivity is carried by net names. The result is checked with
`kicad-cli sch erc` and by comparing the exported netlist with design.py
(check_netlist.py).
"""
import math
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
import libgen  # noqa: E402
import symlib  # noqa: E402
from sexp import dump, fmt, q  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, "..", "hardware"))
NS = uuid.UUID("6f1d2c3a-8b7e-4c55-9a1e-3c2b5d4e6f70")
STUB = 2.54


def uid(*key):
    return str(uuid.uuid5(NS, "/".join(str(k) for k in key)))


ROOT_UUID = uid("root", design.PROJECT)


def xform(px, py, rot):
    """Library offset (y up) -> schematic offset (y down) for a CCW rotation."""
    sx, sy = px, -py
    if rot == 0:
        return sx, sy
    if rot == 90:
        return sy, -sx
    if rot == 180:
        return -sx, -sy
    if rot == 270:
        return -sy, sx
    raise ValueError(rot)


def snap(v):
    return round(v / 0.635) * 0.635


def direction(dx, dy):
    if abs(dx) > abs(dy):
        return "right" if dx > 0 else "left"
    return "down" if dy > 0 else "up"


DIRV = {"right": (1, 0), "left": (-1, 0), "up": (0, -1), "down": (0, 1)}
LABEL_ANGLE = {"right": (0, "left bottom"), "up": (90, "left bottom"),
               "left": (180, "right bottom"), "down": (270, "right bottom")}
GND_ROT = {"down": 0, "right": 90, "up": 180, "left": 270}
VCC_ROT = {"up": 0, "left": 90, "down": 180, "right": 270}


class Sheet:
    def __init__(self):
        self.lib_symbols = {}
        self.items = []
        self.pwr_n = 0
        self.flg_n = 0
        self.local_libs = {design.LOCAL_SYM_LIB: os.path.join(HW, design.PROJECT + ".kicad_sym")}
        self.pinpos = {}
        self.wired = {(w[0], w[1]) for w in design.WIRES} | {(w[2], w[3]) for w in design.WIRES}

    # ------------------------------------------------------------ primitives
    def use_symbol(self, lib_id):
        if lib_id not in self.lib_symbols:
            self.lib_symbols[lib_id] = symlib.load_symbol(lib_id, self.local_libs)
        return self.lib_symbols[lib_id]

    def wire(self, x1, y1, x2, y2):
        self.items.append(
            '\t(wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid "%s"))'
            % (fmt(x1), fmt(y1), fmt(x2), fmt(y2), uid("w", x1, y1, x2, y2)))

    def label(self, net, x, y, d):
        ang, just = LABEL_ANGLE[d]
        self.items.append(
            '\t(label %s (at %s %s %d) (effects (font (size 1.27 1.27)) (justify %s)) (uuid "%s"))'
            % (q(net), fmt(x), fmt(y), ang, just, uid("l", net, x, y)))

    def no_connect(self, x, y):
        self.items.append('\t(no_connect (at %s %s) (uuid "%s"))' % (fmt(x), fmt(y), uid("nc", x, y)))

    def text(self, s, x, y, size=1.27, bold=False):
        self.items.append(
            '\t(text %s (exclude_from_sim no) (at %s %s 0) (effects (font (size %s %s)%s) (justify left top)) (uuid "%s"))'
            % (q(s), fmt(x), fmt(y), size, size, " (bold yes)" if bold else "", uid("t", s, x, y)))

    def rect(self, x1, y1, x2, y2):
        self.items.append(
            '\t(rectangle (start %s %s) (end %s %s) (stroke (width 0.254) (type dash)) (fill (type none)) (uuid "%s"))'
            % (fmt(x1), fmt(y1), fmt(x2), fmt(y2), uid("r", x1, y1, x2, y2)))

    # ------------------------------------------------------------ symbols
    def place(self, lib_id, ref, value, x, y, rot=0, unit=1, footprint="", fields=None,
              in_bom=True, on_board=True, dnp=False, key=None, ref_at=None, val_at=None,
              hide_ref=False, hide_val=False):
        sym = self.use_symbol(lib_id)
        key = key or (ref, unit)
        s_uuid = uid("sym", *key)
        pins = [p for p in symlib.pins(sym) if p["unit"] in (0, unit)]
        props = []

        text_angle = 90 if rot in (90, 270) else 0

        def prop(name, val, px, py, hide=False, just=None):
            props.append('\t\t(property %s %s (at %s %s %d) (effects (font (size 1.27 1.27))%s)%s)'
                         % (q(name), q(val), fmt(px), fmt(py), text_angle,
                            " (justify %s)" % just if just else "", " (hide yes)" if hide else ""))

        rx, ry, rj = ref_at if ref_at else (x, y - 2.54, None)
        vx, vy, vj = val_at if val_at else (x, y + 2.54, None)
        prop("Reference", ref, rx, ry, hide_ref, rj)
        prop("Value", value, vx, vy, hide_val, vj)
        prop("Footprint", footprint, x, y, True)
        prop("Datasheet", symlib.prop(sym, "Datasheet") or "", x, y, True)
        prop("Description", (fields or {}).pop("Description", None) or symlib.prop(sym, "Description") or "", x, y, True)
        for k, v in (fields or {}).items():
            prop(k, v, x, y, True)
        pin_txt = "\n".join('\t\t(pin %s (uuid "%s"))' % (q(p["number"]), uid("pin", ref, unit, p["number"]))
                            for p in pins)
        self.items.append(
            '\t(symbol (lib_id %s) (at %s %s %d) (unit %d) (exclude_from_sim no) (in_bom %s) (on_board %s) (dnp %s) (uuid "%s")\n'
            '%s\n%s\n\t\t(instances (project %s (path "/%s" (reference %s) (unit %d))))\n\t)'
            % (q(lib_id), fmt(x), fmt(y), rot, unit, "yes" if in_bom else "no", "yes" if on_board else "no",
               "yes" if dnp else "no", s_uuid, "\n".join(props), pin_txt, q(design.PROJECT), ROOT_UUID,
               q(ref), unit))
        return sym, pins, s_uuid

    def power(self, net, x, y, d):
        lib_id = design.POWER_NETS[net]
        rot = GND_ROT[d] if net == "GND" else VCC_ROT[d]
        self.pwr_n += 1
        ref = "#PWR%03d" % self.pwr_n
        # value text sits beyond the symbol body
        reach = 7.62 if d in ("left", "right") else (5.08 if net == "GND" else 3.81)
        vx, vy = xform(0, -reach if net == "GND" else reach, rot)
        self.place(lib_id, ref, net, x, y, rot, footprint="", in_bom=False, on_board=False,
                   key=("pwr", net, x, y), hide_ref=True, val_at=(x + vx, y + vy, None))

    def pwr_flag(self, net, x, y):
        self.flg_n += 1
        ref = "#FLG%02d" % self.flg_n
        if net == "GND":
            self.place("power:PWR_FLAG", ref, "PWR_FLAG", x, y, 0, in_bom=False, on_board=False,
                       key=("flg", net), hide_ref=True, val_at=(x, y - 3.81, None))
            self.power(net, x, y, "down")
        elif net in design.POWER_NETS:
            self.place("power:PWR_FLAG", ref, "PWR_FLAG", x, y, 180, in_bom=False, on_board=False,
                       key=("flg", net), hide_ref=True, val_at=(x, y + 3.81, None))
            self.power(net, x, y, "up")
        else:
            self.place("power:PWR_FLAG", ref, "PWR_FLAG", x, y, 0, in_bom=False, on_board=False,
                       key=("flg", net), hide_ref=True, val_at=(x, y - 3.81, None))
            self.wire(x, y, x, y + STUB)
            self.label(net, x, y + STUB, "down")

    def connect_pins(self, pins, conn, x, y, rot, ref):
        """Stub + label/power symbol for every pin position; NC flag when net is None."""
        seen = {}
        for p in pins:
            if p["number"] not in conn:
                continue
            px, py = xform(p["x"], p["y"], rot)
            ax, ay = round(x + px, 3), round(y + py, 3)
            net = conn[p["number"]]
            a = math.radians(p["angle"])
            ox, oy = xform(-math.cos(a), -math.sin(a), rot)
            self.pinpos[(ref, p["number"])] = (ax, ay, direction(ox, oy))
            if (ref, p["number"]) in self.wired:
                continue
            if (ax, ay) in seen:
                if seen[(ax, ay)] != net:
                    raise ValueError("%s pin %s stacked on another net" % (ref, p["number"]))
                continue
            seen[(ax, ay)] = net
            if net is None:
                self.no_connect(ax, ay)
                continue
            d = direction(ox, oy)
            dx, dy = DIRV[d]
            ex, ey = ax + dx * STUB, ay + dy * STUB
            self.wire(ax, ay, ex, ey)
            if net in design.POWER_NETS:
                self.power(net, ex, ey, d)
            else:
                self.label(net, ex, ey, d)
        missing = set(conn) - {p["number"] for p in pins}
        if missing:
            raise ValueError("%s: pins %s not in symbol unit" % (ref, sorted(missing)))

    def part(self, part):
        fields = {"MPN": part["mpn"]}
        if part["lcsc"]:
            fields["LCSC"] = part["lcsc"]
        if part["desc"]:
            fields["Description"] = part["desc"]
        units = [(part["unit"], part["at"], part["rot"], part["pins"])]
        for u, (at, rot, conn) in sorted(part["extra_units"].items()):
            units.append((u, at, rot, conn))
        for u, (x, y), rot, conn in units:
            sym = self.use_symbol(part["lib_id"])
            ref_at = val_at = None
            upins = [p for p in symlib.pins(sym) if p["unit"] in (0, u)]
            if len(upins) == 2:
                (x1, y1), (x2, y2) = [xform(p["x"], p["y"], rot) for p in upins]
                if abs(x1 - x2) < 0.01:      # vertical part: text on the right
                    ref_at = (x + 2.54, y - 1.27, "left")
                    val_at = (x + 2.54, y + 1.27, "left")
                else:                         # horizontal part: text above and below
                    ref_at = (x, y - 2.54, None)
                    val_at = (x, y + 2.54, None)
            else:
                for key in ("Reference", "Value"):
                    pr = [p for p in sym if isinstance(p, list) and p[0] == "property" and p[1] == key]
                    if not pr:
                        continue
                    at = [a for a in pr[0] if isinstance(a, list) and a[0] == "at"][0]
                    dx, dy = xform(float(at[1]), float(at[2]), rot)
                    just = None
                    if rot == 0:
                        eff = [a for a in pr[0] if isinstance(a, list) and a[0] == "effects"]
                        js = [a for a in (eff[0] if eff else []) if isinstance(a, list) and a[0] == "justify"]
                        if js:
                            just = " ".join(str(v) for v in js[0][1:])
                    pos = (x + dx, y + dy, just)
                    if key == "Reference":
                        ref_at = pos
                    else:
                        val_at = pos
            if u == part["unit"] and part.get("ref_at"):
                ref_at = (tuple(part["ref_at"]) + (None,))[:3]
            if u == part["unit"] and part.get("val_at"):
                val_at = (tuple(part["val_at"]) + (None,))[:3]
            _, pins, _ = self.place(part["lib_id"], part["ref"], part["value"], x, y, rot, u,
                                    part["footprint"], dict(fields), dnp=part["dnp"],
                                    in_bom=part.get("in_bom", True), ref_at=ref_at, val_at=val_at)
            self.connect_pins(pins, conn, x, y, rot, part["ref"])

    def direct_wire(self, r1, p1, r2, p2, net, mode):
        """Orthogonal wire pin->pin (horizontal first), labelled with the net name."""
        x1, y1, _ = self.pinpos[(r1, p1)]
        x2, y2, _ = self.pinpos[(r2, p2)]
        pts = [(x1, y1)]
        if abs(x1 - x2) > 1e-6:
            pts.append((x2, y1))
        if abs(y1 - y2) > 1e-6:
            pts.append((x2, y2))
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            self.wire(ax, ay, bx, by)
        if not net:
            return
        corner = pts[1] if len(pts) == 3 else None
        d0 = direction(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
        if net in design.POWER_NETS:
            # power symbol on the bend (or on the far pin for a straight wire)
            px, py = corner or (x2, y2)
            self.power(net, px, py, "down" if net == "GND" else "up")
        elif mode == "tail":
            if corner:
                bx, by, d = corner[0], corner[1], d0
                self.junction(bx, by)
            else:
                bx, by, d = x2, y2, d0
            dx, dy = DIRV[d]
            ex, ey = bx + dx * STUB, by + dy * STUB
            self.wire(bx, by, ex, ey)
            self.label(net, ex, ey, d)
        else:
            (ax, ay), (bx, by) = pts[0], pts[1]
            dx, dy = DIRV[d0]
            if d0 in ("right", "down"):
                self.label(net, ax + dx * 1.27, ay + dy * 1.27, d0)
            else:
                self.label(net, bx, by, "right" if d0 == "left" else "down")

    def junction(self, x, y):
        self.items.append('\t(junction (at %s %s) (diameter 0) (color 0 0 0 0) (uuid "%s"))'
                          % (fmt(x), fmt(y), uid("j", x, y)))

    # ------------------------------------------------------------ output
    def render(self):
        libs = "\n".join("\t\t" + dump(s, 2) for s in self.lib_symbols.values())
        tb = ('\t(title_block\n\t\t(title %s)\n\t\t(date %s)\n\t\t(rev %s)\n\t\t(company %s)\n'
              '\t\t(comment 1 %s)\n\t\t(comment 2 %s)\n\t)'
              % (q(design.TITLE), q(design.DATE), q(design.REV), q(design.COMPANY),
                 q("Input 5-12V, 3x WS2812/WS2811 outputs, 1 button, 3-position endpoint switch"),
                 q("Generated by tools/build_sch.py from tools/design.py")))
        return ('(kicad_sch\n\t(version 20250114)\n\t(generator "eeschema")\n\t(generator_version "9.0")\n'
                '\t(uuid "%s")\n\t(paper "A3")\n%s\n\t(lib_symbols\n%s\n\t)\n%s\n'
                '\t(sheet_instances (path "/" (page "1")))\n\t(embedded_fonts no)\n)\n'
                % (ROOT_UUID, tb, libs, "\n".join(self.items)))


def main():
    os.makedirs(HW, exist_ok=True)
    libgen.write(HW, design.PROJECT)
    sh = Sheet()
    for title, x1, y1, x2, y2 in design.BLOCKS:
        sh.rect(x1, y1, x2, y2)
        sh.text(title, x1 + 1.27, y1 + 1.27, size=1.778, bold=True)
    for (x, y), s in design.NOTES:
        sh.text(s, x, y)
    for part in design.PARTS:
        sh.part(part)
    for w in design.WIRES:
        sh.direct_wire(*w)
    for net, (x, y) in design.PWR_FLAGS:
        sh.pwr_flag(net, x, y)
    out = os.path.join(HW, design.PROJECT + ".kicad_sch")
    with open(out, "w", encoding="utf-8") as f:
        f.write(sh.render())
    print("wrote", out)


if __name__ == "__main__":
    main()
