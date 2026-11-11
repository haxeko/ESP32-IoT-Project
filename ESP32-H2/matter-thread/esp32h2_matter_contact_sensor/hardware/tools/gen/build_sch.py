"""Generate h2_contact.kicad_sch (KiCad 7 format) from design.py (same generator as c3-radar-r45).
Every symbol UUID equals the footprint path in the PCB, so 'Update PCB from Schematic' matches 1:1."""
import re, os, sys, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import *

G = 1.27  # schematic grid
SCH = f"{PROJ_DIR}/{PROJECT}.kicad_sch"
STD_SYM = "/usr/share/kicad/symbols"
FONT = "(effects (font (size 1.27 1.27)))"


def gid(tag):
    return suuid("sch-" + tag)


_cnt = [0]


def nid():
    _cnt[0] += 1
    return gid("auto-%d" % _cnt[0])


# ------------------------------------------------------------------ s-expressions
def tokens(text):
    return re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)


def parse(text):
    st = [[]]
    for t in tokens(text):
        if t == "(":
            st.append([])
        elif t == ")":
            x = st.pop()
            st[-1].append(x)
        else:
            st[-1].append(t)
    return st[0][0]


def ser(x, ind=0):
    if isinstance(x, str):
        return x
    flat = "(" + " ".join(ser(c, ind + 1) for c in x) + ")"
    if len(flat) < 110 and "\n" not in flat:
        return flat
    i = 0
    head = []
    while i < len(x) and isinstance(x[i], str):
        head.append(x[i])
        i += 1
    out = "(" + " ".join(head)
    for c in x[i:]:
        out += "\n" + "  " * (ind + 1) + ser(c, ind + 1)
    return out + ")"


def q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def uq(t):
    return t[1:-1] if t.startswith('"') else t


def kids(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


# ------------------------------------------------------------------ library symbols
_libs = {}


def lib_syms(lib):
    path = f"{PROJ_DIR}/{PROJECT}.kicad_sym" if lib == PROJECT else f"{STD_SYM}/{lib}.kicad_sym"
    if path not in _libs:
        tree = parse(open(path).read())
        _libs[path] = {uq(c[1]): c for c in kids(tree, "symbol")}
    return _libs[path]


def flat_symbol(lib_id):
    lib, name = lib_id.split(":")
    s = lib_syms(lib)[name]
    ext = kids(s, "extends")
    if not ext:
        out = list(s)
        out[1] = q(lib_id)
        return out
    pname = uq(ext[0][1])
    parent = flat_symbol(f"{lib}:{pname}")
    out = ["symbol", q(lib_id)]
    out += [c for c in parent[2:] if not (isinstance(c, list) and c[0] in ("property", "symbol"))]
    out += kids(s, "property")
    for sub in kids(parent, "symbol"):
        sub = list(sub)
        sub[1] = q(uq(sub[1]).replace(pname, name, 1))
        out.append(sub)
    return out


def pins_of(fsym):
    pins = {}
    for sub in kids(fsym, "symbol"):
        m = re.match(r".*_(\d+)_(\d+)$", uq(sub[1]))
        unit, style = int(m.group(1)), int(m.group(2))
        if unit > 1 or style > 1:
            continue
        for p in kids(sub, "pin"):
            at = kids(p, "at")[0]
            pins.setdefault(uq(kids(p, "number")[0][1]), []).append(
                (float(at[1]), float(at[2]), int(float(at[3])) if len(at) > 3 else 0, uq(kids(p, "name")[0][1])))
    return pins


def lib_prop(fsym, key):
    for p in kids(fsym, "property"):
        if uq(p[1]) == key:
            at = kids(p, "at")[0]
            return float(at[1]), float(at[2]), kids(p, "effects")[0]
    return None


# ------------------------------------------------------------------ geometry
def rot(px, py, r):
    return {0: (px, py), 90: (-py, px), 180: (-px, -py), 270: (py, -px)}[r % 360]


def to_sch(X, Y, r, px, py):
    x, y = rot(px, py, r)
    return round(X + x, 4), round(Y - y, 4)


def out_vec(pin_angle, r):
    a = (pin_angle + 180 + r) % 360
    return {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}[a]


# ------------------------------------------------------------------ schematic items
items, lib_ids = [], set()
SUPPLY = {"+5V", "+3V3", "VBUS"}
pwr_n = [0]


def wire(a, b):
    items.append(f'  (wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]}))\n    (stroke (width 0) (type default))\n    (uuid {nid()})\n  )')


def label(name, p, d):
    ang = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[d]
    just = "left bottom" if ang in (0, 90) else "right bottom"
    items.append(f'  (label {q(name)} (at {p[0]} {p[1]} {ang}) (fields_autoplaced)\n'
                 f'    (effects (font (size 1.27 1.27)) (justify {just}))\n    (uuid {nid()})\n  )')


def no_connect(p):
    items.append(f'  (no_connect (at {p[0]} {p[1]}) (uuid {nid()}))')


def text(s, x, y, size=1.27, bold=False):
    b = " bold" if bold else ""
    items.append(f'  (text {q(s)} (at {x} {y} 0)\n    (effects (font (size {size} {size}){b}) (justify left bottom))\n    (uuid {nid()})\n  )')


def place_symbol(lib_id, ref, value, fp, X, Y, r=0, uid=None, in_bom=True, on_board=True, fields=None):
    fsym = flat_symbol(lib_id)
    lib_ids.add(lib_id)
    uid = uid or nid()
    props = []
    for key, val in (("Reference", ref), ("Value", value), ("Footprint", fp), ("Datasheet", "~")):
        lp = lib_prop(fsym, key)
        hide = key in ("Footprint", "Datasheet") or (ref.startswith("#") and key == "Reference")
        if fields and key in fields:
            fx, fy, just = fields[key]
            jst = "" if r in (90, 270) else f" (justify {just})"
            eff = f"(effects (font (size 1.27 1.27)){jst}{' hide' if hide else ''})"
        else:
            px, py, eff_l = lp if lp else (0, 0, None)
            fx, fy = to_sch(X, Y, r, px, py)
            eff = ser(eff_l) if eff_l else FONT
            eff = eff.replace(" hide", "")
            if hide:
                eff = eff[:-1] + " hide)"
        fa = 90 if r in (90, 270) else 0
        props.append(f'    (property "{key}" {q(val)} (at {fx} {fy} {fa})\n      {eff}\n    )')
    pins = pins_of(fsym)
    pin_lines = "\n".join(f'    (pin "{n}" (uuid {gid(uid + "-pin-" + n)}))' for n in pins)
    items.append(
        f'  (symbol (lib_id {q(lib_id)}) (at {X} {Y} {r}) (unit 1)\n'
        f'    (in_bom {"yes" if in_bom else "no"}) (on_board {"yes" if on_board else "no"}) (dnp no)\n'
        f'    (uuid {uid})\n' + "\n".join(props) + "\n" + pin_lines + "\n"
        f'    (instances\n      (project "{PROJECT}"\n        (path "/{ROOT_UUID}" (reference {q(ref)}) (unit 1))\n      )\n    )\n  )')
    return fsym, pins


def power(net, p, d):
    """power port at point p, drawn in direction d (unit vector)"""
    pwr_n[0] += 1
    if net == "GND":
        r = {(0, 1): 0, (0, -1): 180, (1, 0): 90, (-1, 0): 270}[d]
    else:
        r = {(0, -1): 0, (0, 1): 180, (-1, 0): 90, (1, 0): 270}[d]
    place_symbol(f"power:{net}", "#PWR%03d" % pwr_n[0], net, "", p[0], p[1], r, in_bom=False, on_board=False)


def pwr_flag(net, x, y):
    power(net, (x, y), (0, 1) if net == "GND" else (0, -1))
    wire((x, y), (x + 8 * G, y))
    place_symbol("power:PWR_FLAG", "#FLG%02d" % (pwr_n[0]), "PWR_FLAG", "", x + 8 * G, y, 0, in_bom=False, on_board=False)


def part(ref, gx, gy, r=0, stub=2, fields=None):
    p = PART[ref]
    X, Y = gx * G, gy * G
    fsym, pins = place_symbol(p["sym"], ref, p["value"], p["fp"], X, Y, r, uid=p["uuid"], fields=fields)
    done = set()
    for num, plist in pins.items():
        for (px, py, pa, pname) in plist:
            end = to_sch(X, Y, r, px, py)
            if end in done:
                continue
            done.add(end)
            net = p["pads"].get(num)
            if net is None:
                no_connect(end)
                continue
            d = out_vec(pa, r)
            tip = (round(end[0] + d[0] * stub * G, 4), round(end[1] + d[1] * stub * G, 4))
            wire(end, tip)
            if net in SUPPLY or net == "GND":
                power(net, tip, d)
            else:
                label(net, tip, d)


def small_fields(gx, gy, horizontal=False):
    X, Y = gx * G, gy * G
    if horizontal:
        return {"Reference": (X, Y - 2.5 * G, "bottom"), "Value": (X, Y + 2.5 * G, "top")}
    return {"Reference": (X + 2 * G, Y - 1 * G, "left"), "Value": (X + 2 * G, Y + 1 * G, "left")}


PART = {p["ref"]: p for p in PARTS}
PART["D1"]["sym"] = "Device:D_Zener"   # unidirectional TVS drawn as a zener (pin 1 = K)

# ------------------------------------------------------------------ layout (grid units)
text("1. USB-C (USB 2.0 data + 5 V)", 12 * G, 18 * G, 1.8, True)
part("J1", 28, 50)
part("R1", 56, 66, fields=small_fields(56, 66))
part("R2", 64, 66, fields=small_fields(64, 66))
part("U3", 84, 44)
part("R9", 112, 44, 90, fields=small_fields(112, 44, True))
part("R10", 112, 54, 90, fields=small_fields(112, 54, True))
text("Power flags", 14 * G, 84 * G)
pwr_flag("VBUS", 16 * G, 89 * G)
pwr_flag("GND", 32 * G, 89 * G)
pwr_flag("+5V", 48 * G, 89 * G)

text("2. Protection + 3.3 V LDO", 128 * G, 18 * G, 1.8, True)
part("F1", 134, 36, 90, fields=small_fields(134, 36, True))
part("D1", 146, 44, 270, fields={"Reference": (149.5 * G, 43 * G, ""), "Value": (150.5 * G, 45 * G, "")})
part("C1", 156, 44, fields=small_fields(156, 44))
part("C2", 164, 44, fields=small_fields(164, 44))
part("U2", 180, 42)
part("C3", 196, 44, fields=small_fields(196, 44))

text("3. Reed switch input (GPIO4, closed = LOW)", 226 * G, 18 * G, 1.8, True)
part("J2", 228, 44, 180)
part("R7", 248, 34, fields=small_fields(248, 34))
part("D2", 248, 58, 90, fields={"Reference": (250.5 * G, 57 * G, ""), "Value": (250.5 * G, 59 * G, "")})
part("R8", 266, 46, 90, fields=small_fields(266, 46, True))
part("C7", 284, 52, fields=small_fields(284, 52))

text("4. ESP32-H2-MINI-1-N4", 112 * G, 82 * G, 1.8, True)
part("U1", 150, 124)
part("C4", 172, 94, fields=small_fields(172, 94))
part("C5", 182, 94, fields=small_fields(182, 94))

text("5. Reset, boot / factory reset, strapping", 12 * G, 104 * G, 1.8, True)
text("Status LED (GPIO5)", 208 * G, 104 * G, 1.8, True)
part("R3", 20, 120, fields=small_fields(20, 120))
part("C6", 30, 120, fields=small_fields(30, 120))
part("SW1", 48, 120)
part("R4", 20, 144, fields=small_fields(20, 144))
part("SW2", 48, 144)
part("R5", 30, 144, fields=small_fields(30, 144))
part("R6", 222, 116, 90, fields=small_fields(222, 116, True))
part("D3", 248, 116, 180, fields=small_fields(248, 116, True))

text("6. Test pads (bottom side): UART0 log / recovery", 208 * G, 132 * G, 1.8, True)
for i, ref in enumerate(["TP1", "TP2", "TP3", "TP4"]):
    part(ref, 214 + 14 * i, 144)

NOTES = (
    "NOTES\n"
    "1. PCB 26 x 46 mm, 2 layers, 1.6 mm. Every SMD part on the top side; J2 (screw terminal) hand soldered.\n"
    "2. U1 at the top edge, antenna to the edge: no copper, tracks or vias on either layer under the antenna area.\n"
    "3. Pins = firmware defaults (main/Kconfig.projbuild): reed switch GPIO4, BOOT / factory reset GPIO9 (hold 6 s),\n"
    "   status LED GPIO5 active high (enable CONFIG_STATUS_LED_ENABLE), USB Serial/JTAG on GPIO26/27.\n"
    "4. Reed input: R7 10k pull-up at the terminal (about 0.3 mA wetting current), R8 1k + C7 100nF filter, D2 ESD.\n"
    "   The firmware also enables the GPIO4 internal pull-up and debounces in software.\n"
    "5. Flashing and logs over USB-C (USB Serial/JTAG). Download mode by hand: hold BOOT (GPIO9 low, GPIO8 pulled\n"
    "   high by R5) and press RESET. USB-C: CC1/CC2 5.1k each (5 V sink), PTC + TVS on VBUS, USBLC6 on D+/D-.\n"
    "6. VBAT (pin 15) is tied to 3V3 inside the module (datasheet v1.6): not connected here.\n"
    "7. Unused GPIO: IO0-IO3, IO10-IO14, IO22, IO25 (IO25 strapping, floating = default). H1/H2: M2 holes, PCB only."
)
text(NOTES, 208 * G, 186 * G)

# ------------------------------------------------------------------ write file
libsyms = "\n".join("    " + ser(flat_symbol(l), 2) for l in sorted(lib_ids))
out = f"""(kicad_sch (version 20230121) (generator eeschema)

  (uuid {ROOT_UUID})

  (paper "A3")

  (title_block
    (title "h2-contact: ESP32-H2 Matter over Thread door/window contact sensor")
    (date "2026-11-11")
    (rev "1.0")
    (comment 1 "2-layer 1.6 mm, 26 x 46 mm, all SMD parts on the top side")
    (comment 2 "USB-C 5 V + USB Serial/JTAG, reed switch on GPIO4 (screw terminal)")
  )

  (lib_symbols
{libsyms}
  )

{chr(10).join(items)}

  (sheet_instances
    (path "/" (page "1"))
  )
)
"""
open(SCH, "w").write(out)
print("wrote", SCH, len(items), "items,", len(lib_ids), "lib symbols")
