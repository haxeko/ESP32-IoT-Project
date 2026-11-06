#!/usr/bin/env python3
"""Generate the KiCad schematic (A3, one sheet) from design.py.

Every connected pin gets a 2.54 mm wire stub ending on a power symbol (GND, +3V3, VBAT) or a net label, unused
pins get a no-connect flag. Run gen_project.py first; check with:
  kicad-cli sch erc matter-button-cr2032.kicad_sch
"""

import math
import os
import re

import design
from design import LIB, PARTS, PROJECT, PWR_FLAGS, TEXTS
from kicad_sexp import HW, block, effects, num, q, stock_symbol, uid, write

SCH = os.path.join(HW, PROJECT + ".kicad_sch")
ROOT = uid(PROJECT, "root-sheet")
POWER = {design.GND: "power:GND", design.V33: "power:+3V3", design.VBAT: f"{LIB}:VBAT"}
STUB = 2.54


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    if lib == LIB:
        with open(os.path.join(HW, "lib", LIB + ".kicad_sym")) as f:
            text = block(f.read(), name)
    else:
        text = stock_symbol(lib_id)
    return text.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)


def sub_blocks(text, head):
    """Balanced '(head ...' blocks inside text."""
    out, i = [], 0
    while True:
        i = text.find("(" + head + " ", i)
        if i < 0:
            return out
        depth = 0
        for j in range(i, len(text)):
            depth += text[j] == "("
            depth -= text[j] == ")"
            if depth == 0:
                out.append(text[i:j + 1])
                i = j + 1
                break


def pins_of(sym_text):
    """number -> (x, y, angle) of the pin connection point, library coordinates (y up)."""
    pins = {}
    for b in sub_blocks(sym_text, "pin"):
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)", b)
        nb = re.search(r'\(number "([^"]*)"', b)
        pins[nb.group(1)] = (float(at.group(1)), float(at.group(2)), int(float(at.group(3))))
    return pins


def props_of(sym_text):
    """property name -> (x, y, justify) in library coordinates."""
    out = {}
    for b in sub_blocks(sym_text, "property"):
        m = re.match(r'\(property "((?:[^"\\]|\\.)*)" "(?:[^"\\]|\\.)*"\s*\(at ([-\d.]+) ([-\d.]+)', b)
        j = re.search(r"\(justify ([a-z ]+)\)", b)
        out[m.group(1)] = (float(m.group(2)), float(m.group(3)), j.group(1) if j else None)
    return out


def xform(x, y, X, Y, rot):
    """Library point -> schematic point for a symbol at (X, Y), rotation rot (counter-clockwise)."""
    a = math.radians(rot)
    xr = x * math.cos(a) - y * math.sin(a)
    yr = x * math.sin(a) + y * math.cos(a)
    return round(X + xr, 2), round(Y - yr, 2)


def stub_dir(angle, rot):
    """Unit vector (schematic coordinates) pointing away from the body for a pin with library angle."""
    a = math.radians(angle + rot + 180)  # pin points to the body; the stub goes the other way
    return round(math.cos(a)), -round(math.sin(a))


class Sheet:
    def __init__(self):
        self.items = []
        self.pwr = 0
        self.flg = 0

    def wire(self, x1, y1, x2, y2):
        self.items.append(f"\t(wire (pts (xy {num(x1)} {num(y1)}) (xy {num(x2)} {num(y2)})) "
                          f"(stroke (width 0) (type default)) (uuid {q(uid('wire', x1, y1, x2, y2))}))\n")

    def junction(self, x, y):
        self.items.append(f"\t(junction (at {num(x)} {num(y)}) (diameter 0) (color 0 0 0 0) "
                          f"(uuid {q(uid('junction', x, y))}))\n")

    def no_connect(self, x, y):
        self.items.append(f"\t(no_connect (at {num(x)} {num(y)}) (uuid {q(uid('nc', x, y))}))\n")

    def label(self, net, x, y, d):
        angle, just = {(1, 0): (0, "left bottom"), (-1, 0): (180, "right bottom"),
                       (0, -1): (90, "left bottom"), (0, 1): (270, "right bottom")}[d]
        self.items.append(f"\t(label {q(net)} (at {num(x)} {num(y)} {angle}) (fields_autoplaced yes) "
                          f"{effects(justify=just)} (uuid {q(uid('label', net, x, y))}))\n")

    def text(self, x, y, s, size):
        self.items.append(f"\t(text {q(s)} (exclude_from_sim no) (at {num(x)} {num(y)} 0) "
                          f"{effects(size, justify='left bottom')} (uuid {q(uid('text', x, y))}))\n")

    def symbol(self, lib_id, ref, value, x, y, rot, fields, pins, sym_text, dnp=False, power=False, bom=True):
        props = props_of(sym_text)
        key = uid("sym", ref) if not power else uid("pwr", ref, x, y)
        o = [f"\t(symbol (lib_id {q(lib_id)}) (at {num(x)} {num(y)} {rot}) (unit 1)\n"
             f"\t\t(exclude_from_sim no) (in_bom {'yes' if bom and not power else 'no'}) (on_board yes) "
             f"(dnp {'yes' if dnp else 'no'})\n\t\t(uuid {q(key)})\n"]
        for name, val in [("Reference", ref), ("Value", value)] + fields:
            px, py, just = props.get(name, (0, 0, None))
            sx, sy = xform(px, py, x, y, rot)
            hide = name not in ("Reference", "Value") or (power and name == "Reference")
            if name in ("Reference", "Value") and lib_id in ("Device:R", "Device:C", "Device:L"):
                if rot in (0, 180):
                    sx, sy, just = x + 2.54, y + (-1.27 if name == "Reference" else 1.27), "left"
                else:
                    sx, sy, just = x, y + (-3.81 if name == "Reference" else 2.54), None
            # field angles are relative to the symbol: keep the text horizontal on rotated symbols
            o.append(f"\t\t(property {q(name)} {q(val)} (at {num(sx)} {num(sy)} {rot % 180}) "
                     f"{effects(justify=just, hide=hide)})\n")
        for n in pins:
            o.append(f"\t\t(pin {q(n)} (uuid {q(uid(key, 'pin', n))}))\n")
        o.append(f"\t\t(instances (project {q(PROJECT)} (path {q('/' + ROOT)} (reference {q(ref)}) (unit 1))))\n\t)\n")
        self.items.append("".join(o))
        return key

    def power(self, net, x, y, d):
        lib_id = POWER[net]
        # GND hangs below its pin, +3V3 / VBAT stand above it: rotate them to point away from the wire
        if net == design.GND:
            rot = {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270}[d]
        else:
            rot = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[d]
        self.pwr += 1
        self.symbol(lib_id, f"#PWR{self.pwr:03d}", net, x, y, rot, [], ["1"], SYMS[lib_id], power=True)

    def pwr_flag(self, net, x, y):
        # power symbol and PWR_FLAG share the connection point, drawn on opposite sides
        self.power(net, x, y, (0, 1) if net == design.GND else (0, -1))
        self.flg += 1
        self.symbol("power:PWR_FLAG", f"#FLG{self.flg:02d}", "PWR_FLAG", x, y,
                    0 if net == design.GND else 180, [], ["1"], SYMS["power:PWR_FLAG"], power=True)


def power_rail(sh, net, d, ends):
    """Neighbouring pins of one part on the same power net (U1 GND 9/13/29, U2 VIN/EN/SEL) share one wire and
    one power symbol."""
    if len(ends) == 1:
        sh.power(net, *ends[0], d)
        return
    vertical = d[0] != 0  # stubs go sideways: the rail runs vertically
    ends = sorted(ends, key=lambda e: e[1] if vertical else e[0])
    for a, b in zip(ends, ends[1:]):
        sh.wire(*a, *b)
    for e in ends[1:-1]:
        sh.junction(*e)
    if vertical:  # GND hangs from the lowest end, supplies stand on the highest one
        e, up = (ends[-1], (0, 1)) if net == design.GND else (ends[0], (0, -1))
        sh.power(net, *e, up)
        if e != ends[0] and e != ends[-1]:
            sh.junction(*e)
    else:
        sh.power(net, *ends[len(ends) // 2], d)


LIB_IDS = sorted({p["lib"] for p in PARTS} | set(POWER.values()) | {"power:PWR_FLAG"})
SYMS = {lid: lib_symbol(lid) for lid in LIB_IDS}


def build():
    sh = Sheet()
    for p in PARTS:
        lid = p["lib"]
        X, Y, rot = p["sch"]
        pins = pins_of(SYMS[lid])
        missing = set(p["pins"]) - set(pins)
        assert not missing, f"{p['ref']}: pins {missing} not in {lid}"
        fields = [("Footprint", p["fp"]), ("Datasheet", "~"), ("Description", p["desc"]), ("MPN", p["mpn"]),
                  ("LCSC", p["lcsc"])]
        sh.symbol(lid, p["ref"], p["value"], X, Y, rot, fields, sorted(pins, key=lambda s: (len(s), s)),
                  SYMS[lid], dnp=p.get("dnp", False), bom=p["bom"])
        done = set()  # stacked pins (TPS63900 exposed pad 11 hidden under GND pin 8) get one stub
        rails = {}  # (net, stub direction) -> stub ends of this part on a power net
        for n, net in p["pins"].items():
            px, py, pa = pins[n]
            x0, y0 = xform(px, py, X, Y, rot)
            if net is None or (x0, y0) in done:
                continue
            done.add((x0, y0))
            if net == "NC":
                sh.no_connect(x0, y0)
                continue
            d = stub_dir(pa, rot)
            x1, y1 = round(x0 + d[0] * STUB, 2), round(y0 + d[1] * STUB, 2)
            sh.wire(x0, y0, x1, y1)
            if net in POWER:
                rails.setdefault((net, d), []).append((x1, y1))
            else:
                sh.label(net, x1, y1, d)
        for (net, d), ends in rails.items():
            power_rail(sh, net, d, ends)
    for net, x, y in PWR_FLAGS:
        sh.pwr_flag(net, x, y)
    for x, y, s, size in TEXTS:
        sh.text(x, y, s, size)
    return sh


def main():
    sh = build()
    libs = "".join("\t\t" + SYMS[lid].replace("\n", "\n\t\t") + "\n" for lid in LIB_IDS)
    text = (f"(kicad_sch\n\t(version 20250114)\n\t(generator \"eeschema\")\n\t(generator_version \"9.0\")\n"
            f"\t(uuid {q(ROOT)})\n\t(paper \"A3\")\n"
            f"\t(title_block (title {q(design.TITLE)}) (date {q(design.DATE)}) (rev {q(design.REV)}) "
            f"(company {q(design.COMPANY)}) (comment 1 \"CR2032 -> Q1 (reverse polarity) -> TPS63900 3.3 V -> "
            f"ESP32-H2-WROOM-02C\") (comment 2 \"Generated by scripts/gen_sch.py from scripts/design.py\"))\n"
            f"\t(lib_symbols\n{libs}\t)\n" + "".join(sh.items) +
            "\t(sheet_instances (path \"/\" (page \"1\")))\n\t(embedded_fonts no)\n)\n")
    write(SCH, text)
    print("schematic written:", SCH)


if __name__ == "__main__":
    main()
