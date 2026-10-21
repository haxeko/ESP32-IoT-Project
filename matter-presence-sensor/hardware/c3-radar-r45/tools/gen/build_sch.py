"""Generate c3_radar_r45.kicad_sch (KiCad 7 format) from design.py.
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
text("1. USB-C 5 V input (power only)", 16 * G, 20 * G, 1.8, True)
part("J1", 28, 44)
part("R1", 50, 54, fields=small_fields(50, 54))
part("R2", 56, 54, fields=small_fields(56, 54))
part("F1", 66, 38, 90, fields=small_fields(66, 38, True))
text("Power flags", 48 * G, 63 * G)
pwr_flag("VBUS", 50 * G, 68 * G)
pwr_flag("GND", 66 * G, 68 * G)
pwr_flag("+5V", 82 * G, 68 * G)

text("2. Protection + 3.3 V LDO", 96 * G, 20 * G, 1.8, True)
part("D1", 100, 46, 270, fields={"Reference": (103.5 * G, 45 * G, ""), "Value": (104.5 * G, 47 * G, "")})
part("C1", 110, 46, fields=small_fields(110, 46))
part("C2", 118, 46, fields=small_fields(118, 46))
part("U2", 134, 44)
part("C3", 148, 46, fields=small_fields(148, 46))

text("3. HLK-LD2410C radar (beside U1, antenna face up)", 190 * G, 20 * G, 1.8, True)
part("RD1", 214, 44)
part("C7", 226, 44, fields=small_fields(226, 44))
part("C8", 234, 44, fields=small_fields(234, 44))
part("R7", 244, 50, fields=small_fields(244, 50))

text("4. ESP32-C3-WROOM-02-N4", 96 * G, 86 * G, 1.8, True)
part("U1", 132, 122)
part("C4", 156, 96, 180, fields=small_fields(156, 96))
part("C5", 164, 96, 180, fields=small_fields(164, 96))

text("5. Reset / boot / strapping", 16 * G, 86 * G, 1.8, True)
part("R3", 24, 104, fields=small_fields(24, 104))
part("C6", 34, 104, fields=small_fields(34, 104))
part("SW1", 52, 104)
part("R4", 24, 126, fields=small_fields(24, 126))
part("R5", 34, 126, fields=small_fields(34, 126))
part("R6", 44, 126, fields=small_fields(44, 126))

text("6. Programming pads (pogo / solder, bottom side)", 190 * G, 86 * G, 1.8, True)
for i, ref in enumerate(["TP1", "TP2", "TP3", "TP4", "TP5", "TP6"]):
    part(ref, 196 + 12 * i, 100)

NOTES = (
    "NOTES\n"
    "1. Round PCB D45 mm, 2 layers, 1.6 mm. U1, RD1, C4, C5 on the top (room side); everything else on the bottom.\n"
    "2. RD1 (HLK-LD2410C) sits flat below U1, not stacked: header insulator on the PCB, antenna face up,\n"
    "   about 16 mm from the WiFi antenna zone. Solder the 5 pins from the bottom and trim them.\n"
    "3. LD2410C pin order seen on the antenna face with the pins at the top: TX RX OUT GND VCC.\n"
    "   Always check against the silkscreen of your module before soldering.\n"
    "4. Radar UART1: RADAR_TX -> IO10 (ESP RX), IO3 (ESP TX) -> RADAR_RX, RADAR_OUT -> IO1. 256000 baud 8N1.\n"
    "5. Flashing over UART0 pads RX0/TX0 (3.3 V USB-UART). Download mode: short IO9 to GND while tapping RST\n"
    "   (or EN to GND). Power from USB-C or the 3V3 pad, not both.\n"
    "6. USB-C is power only: CC1/CC2 each have their own 5.1k to GND. PTC + TVS on the 5 V input,\n"
    "   AP2112K-3.3 (600 mA) for the ESP32-C3; the radar runs from 5 V directly.\n"
    "7. Unused GPIO: IO0 IO4 IO5 IO6 IO7 IO18 IO19 (not routed on the PCB).\n"
    "8. H1/H2: M2 mounting holes, PCB only (no symbol). No copper or metal within 15 mm of the antenna."
)
text(NOTES, 190 * G, 136 * G)

# ------------------------------------------------------------------ write file
libsyms = "\n".join("    " + ser(flat_symbol(l), 2) for l in sorted(lib_ids))
out = f"""(kicad_sch (version 20230121) (generator eeschema)

  (uuid {ROOT_UUID})

  (paper "A3")

  (title_block
    (title "c3-radar r45: ESP32-C3 + HLK-LD2410C presence sensor, round 45 mm")
    (date "2026-10-19")
    (rev "1.0")
    (comment 1 "2-layer 1.6 mm, D45 mm. U1 + RD1 side by side on top, other parts bottom side")
    (comment 2 "USB-C power only, UART0 pads for flashing, OTA afterwards")
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
