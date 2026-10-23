#!/usr/bin/env python3
"""Generate, route and check the PCB (KiCad 9 pcbnew API + FreeRouting).

  python3 gen_pcb.py place     board from the schematic netlist: outline, placement, nets, rule areas, texts
  python3 gen_pcb.py route     FreeRouting (Specctra DSN/SES), GND pours both layers, stitching vias
  python3 gen_pcb.py pour      GND pours and stitching vias again (after a manual change)
  python3 gen_pcb.py all       place + route

The footprints, values, nets and symbol links come from the netlist that kicad-cli exports from the schematic,
as "Update PCB from Schematic" would do, so DRC with --schematic-parity checks the board against the schematic.

Board: 30 x 38 mm, 2 layers, 1.6 mm. The module antenna (6 mm) overhangs the top edge, nothing under it.
The CR2032 holder is on the bottom side, its centre 26 mm from the antenna edge. Every other part is on the
top side; the test pads are on the bottom side under the module (pogo pins, 2.54 mm pitch).
"""

import math
import os
import re
import subprocess
import sys

import pcbnew

import design
from kicad_sexp import HW, KICAD_FP

PCB = os.path.join(HW, design.PROJECT + ".kicad_pcb")
SCH = os.path.join(HW, design.PROJECT + ".kicad_sch")
BUILD = os.path.join(HW, "build")  # intermediate files (git-ignored)
FREEROUTING = os.environ.get("FREEROUTING_JAR", os.path.expanduser("~/freerouting-2.1.0.jar"))
JOB_TIMEOUT = os.environ.get("FREEROUTING_TIMEOUT", "00:03:00")

X0, Y0, W, H, R_CORNER = 100.0, 100.0, 30.0, 38.0, 1.5  # board outline, mm
MODULE = (115.0, 104.0)  # module centre: antenna area (y -10 .. -4 of the footprint) outside the top edge
HOLDER = (115.0, 126.0)  # CR2032 holder centre, bottom side

# ref: (x, y, rotation, side). Rotation counter-clockwise: 0 pad 1 left, 90 pad 1 down, 180 right, 270 up.
PLACE = {
    "U1": (*MODULE, 0, "F"),
    "BT1": (*HOLDER, 0, "B"),
    # left of the module: 3V3 decoupling at pin 1, EN pull-up (and RC option), GPIO8 strap pull-up
    "C6": (104.55, 101.6, 270, "F"),
    "C5": (102.6, 101.95, 270, "F"),
    "R4": (103.7, 105.0, 0, "F"),
    "C7": (103.7, 106.2, 180, "F"),
    "R5": (103.7, 109.6, 0, "F"),
    # right of the module: battery sense at IO1 (pin 27)
    "R9": (126.4, 104.2, 90, "F"),
    "C9": (127.9, 104.2, 270, "F"),
    # below the module: LED at IO22 (pin 15), TPS63900 configuration resistors
    "R8": (117.0, 116.4, 0, "F"),
    "D1": (119.9, 116.4, 180, "F"),
    "R3": (108.0, 117.0, 90, "F"),
    "R2": (109.0, 117.0, 90, "F"),
    "R1": (110.0, 117.0, 90, "F"),
    # power chain, right to left: Q1 -> JP1 -> C2, C1 -> C3 -> U2 (L1 below) -> C4 -> JP2 -> 3V3
    "U2": (110.0, 120.5, 270, "F"),
    "L1": (110.0, 124.0, 180, "F"),
    "C3": (113.1, 121.3, 90, "F"),
    "C4": (106.9, 121.3, 90, "F"),
    "JP2": (104.7, 121.8, 90, "F"),
    "C1": (116.2, 121.2, 270, "F"),
    "C2": (119.5, 121.2, 270, "F"),
    "JP1": (122.4, 119.5, 270, "F"),
    "Q1": (125.5, 119.8, 270, "F"),
    # button at the centre of the lower half, its pull-up and RC filter on the left
    "SW1": (115.0, 131.0, 0, "F"),
    "R7": (110.6, 129.9, 180, "F"),
    "C8": (110.09, 128.4, 90, "F"),
    "R6": (110.6, 131.1, 0, "F"),
    # test pads, bottom side, under the module
    "TP1": (109.92, 103.5, 0, "B"), "TP5": (112.46, 103.5, 0, "B"), "TP2": (115.0, 103.5, 0, "B"),
    "TP7": (117.54, 103.5, 0, "B"), "TP8": (120.08, 103.5, 0, "B"),
    "TP6": (109.92, 109.5, 0, "B"), "TP9": (112.46, 109.5, 0, "B"), "TP10": (115.0, 109.5, 0, "B"),
    "TP3": (117.54, 109.5, 0, "B"), "TP4": (120.08, 109.5, 0, "B"),
}

# Hand-routed tracks (top layer unless "B"): the 0.5 mm pitch TPS63900 and the short local connections.
# FreeRouting keeps them (locked) and routes the rest. (net, width, [points], layer)
F, B = "F", "B"
ROUTES = [
    # TPS63900 (U2 rotated: bottom row VOUT LX2 GND LX1 VIN, top row CFG3 CFG2 CFG1 SEL EN, from left)
    ("GND", 0.25, [(110.0, 121.7125), (110.0, 120.5)], F),                    # pin 8 to exposed pad
    ("GND", 0.3, [(110.0, 120.5), (108.2, 120.5), (106.9, 120.525)], F),      # EP, via, C4
    ("GND", 0.3, [(110.0, 120.5), (111.8, 120.5), (113.1, 120.525)], F),      # EP, via, C3
    ("/VOUT", 0.25, [(109.0, 121.7125), (109.0, 122.0), (106.9, 122.0)], F),  # pin 6 to C4
    ("/VOUT", 0.3, [(106.9, 122.075), (104.7, 122.45)], F),                   # C4 to JP2
    ("VBAT", 0.25, [(111.0, 121.7125), (111.0, 122.0), (113.1, 122.0)], F),   # VIN to C3
    ("/LX1", 0.3, [(110.5, 121.7125), (110.5, 123.3)], F),
    ("/LX2", 0.3, [(109.5, 121.7125), (109.5, 123.3)], F),
    ("/CFG1", 0.2, [(110.0, 119.2875), (110.0, 117.51)], F),
    ("/CFG2", 0.2, [(109.5, 119.2875), (109.5, 118.6), (109.0, 118.1), (109.0, 117.51)], F),
    ("/CFG3", 0.2, [(109.0, 119.2875), (109.0, 118.9), (108.0, 117.9), (108.0, 117.51)], F),
    ("VBAT", 0.25, [(110.5, 119.0), (111.0, 119.0)], F),                      # SEL and EN to VIN
    ("VBAT", 0.3, [(111.0, 119.0), (111.0, 118.5), (111.3, 118.2), (114.3, 118.2), (114.3, 122.0),
                   (113.1, 122.075)], F),
    ("VBAT", 0.3, [(114.3, 119.725), (119.5, 119.725), (121.3, 119.9), (122.4, 120.15)], F),  # C1 C2 JP1
    ("GND", 0.3, [(108.0, 116.49), (110.0, 116.49)], F),                      # R1-R3 to GND
    ("GND", 0.3, [(108.0, 116.49), (106.9, 116.3)], F),
    ("GND", 0.3, [(116.2, 122.675), (116.2, 124.1)], F),                      # C1
    ("GND", 0.3, [(119.5, 122.675), (119.5, 124.1)], F),                      # C2
    # reverse polarity MOSFET: drain to the + tab of the holder (bottom side)
    ("/VBAT_Q", 0.3, [(122.4, 118.85), (124.55, 118.86)], F),
    ("GND", 0.3, [(126.45, 118.86), (127.45, 118.3)], F),                     # gate
    ("/BAT+", 0.4, [(125.5, 120.74), (125.5, 122.3)], F),
    ("/BAT+", 0.4, [(125.5, 122.3), (125.985, 123.9)], B),
    # LED, battery sense, EN, GPIO8 strap, decoupling at pin 1
    ("/LED", 0.2, [(115.75, 114.4), (115.75, 115.6), (116.49, 116.4)], F),
    ("/LED_A", 0.2, [(117.51, 116.4), (119.11, 116.4)], F),
    ("GND", 0.3, [(120.69, 116.4), (121.8, 116.4)], F),
    ("/VBAT_SENSE", 0.2, [(124.4, 102.6), (125.8, 102.6), (126.4, 103.2), (126.4, 103.69)], F),
    ("/VBAT_SENSE", 0.2, [(126.4, 103.69), (127.9, 103.69)], F),
    ("GND", 0.3, [(127.9, 104.71), (127.9, 105.8)], F),
    ("/EN", 0.2, [(105.6, 102.9), (104.21, 104.3), (104.21, 105.0), (104.21, 106.2)], F),
    ("/STRAP8", 0.2, [(104.21, 109.6), (105.0, 110.1), (106.25, 110.1)], F),
    ("+3V3", 0.3, [(102.6, 101.175), (104.55, 101.09), (106.25, 101.1)], F),
    ("GND", 0.3, [(104.55, 102.11), (103.6, 103.2)], F),
    ("GND", 0.3, [(102.6, 102.725), (103.6, 103.2)], F),
    ("GND", 0.3, [(112.75, 114.2), (112.75, 115.6)], F),                      # module pin 13
    ("GND", 0.3, [(105.6, 113.1), (104.6, 113.1)], F),                        # module pin 9
    # 3V3 trunk along the left edge: pin 1 decoupling, EN and GPIO8 pull-ups, button pull-up, test pad TP1
    ("+3V3", 0.3, [(104.7, 121.15), (104.7, 119.6), (101.0, 115.9), (101.0, 102.0), (101.7, 101.3),
                   (102.6, 101.175)], F),
    ("+3V3", 0.3, [(101.0, 105.0), (103.19, 105.0)], F),
    ("+3V3", 0.3, [(101.0, 109.6), (103.19, 109.6)], F),
    ("+3V3", 0.3, [(101.0, 115.9), (101.0, 131.1), (110.09, 131.1)], F),
    ("+3V3", 0.3, [(103.6, 101.13), (107.0, 101.13), (109.37, 103.5), (109.92, 103.5)], B),
    # button: GPIO10 under the left strip on the bottom side, RC filter, pull-up, both pads of each contact
    ("/BTN", 0.2, [(106.25, 107.1), (105.3, 107.1), (104.7, 107.75)], F),
    ("/BTN", 0.2, [(104.7, 107.75), (102.2, 110.25), (102.2, 128.6), (103.5, 129.9), (106.0, 129.9)], B),
    ("/BTN", 0.2, [(106.0, 129.9), (110.09, 129.9)], F),
    ("/BTN", 0.2, [(110.09, 128.91), (110.09, 129.9)], F),
    ("GND", 0.3, [(110.09, 127.89), (110.09, 126.9)], F),
    ("/BTN_SW", 0.2, [(111.11, 129.9), (111.11, 131.1)], F),
    ("/BTN_SW", 0.25, [(111.11, 129.9), (112.925, 129.925), (117.075, 129.925)], F),
    ("GND", 0.3, [(112.925, 132.075), (117.075, 132.075)], F),
    ("GND", 0.3, [(115.0, 132.075), (115.0, 133.3)], F),
    ("GND", 0.3, [(103.19, 106.2), (102.3, 106.2)], F),                       # C7 (not fitted)
]
VIAS = [("GND", 108.2, 120.5), ("GND", 111.8, 120.5), ("GND", 106.9, 116.3), ("GND", 116.2, 124.1),
        ("GND", 119.5, 124.1), ("GND", 127.45, 118.3), ("/BAT+", 125.5, 122.3), ("GND", 121.8, 116.4),
        ("GND", 127.9, 105.8), ("GND", 103.6, 103.2), ("GND", 112.75, 115.6), ("GND", 104.6, 113.1),
        ("GND", 115.0, 133.3), ("GND", 110.09, 126.9), ("GND", 102.3, 106.2), ("+3V3", 103.6, 101.13),
        ("/BTN", 104.7, 107.75), ("/BTN", 106.0, 129.9)]

# reference designators shown on the silkscreen: ref -> (x, y) or None (footprint default)
SILK_REFS = {"U1": (115.0, 111.6), "U2": (107.6, 123.5), "Q1": (127.9, 121.6), "SW1": (115.0, 128.5),
             "D1": (119.9, 115.2), "L1": (110.0, 126.0), "JP1": (122.4, 122.1), "JP2": (104.7, 124.3),
             "BT1": None}

# silkscreen labels of the test pads (bottom side)
TP_LABEL = {"TP1": "3V3", "TP2": "GND", "TP3": "TX", "TP4": "RX", "TP5": "EN", "TP6": "BOOT", "TP7": "D-",
            "TP8": "D+", "TP9": "VBAT", "TP10": "VSNS"}


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# ------------------------------------------------------------------------------------------------- netlist
def read_netlist():
    os.makedirs(BUILD, exist_ok=True)
    path = os.path.join(BUILD, design.PROJECT + ".net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", path, SCH],
                   check=True, capture_output=True)
    text = open(path).read()
    comps = {}
    for blk in text.split("(comp ")[1:]:
        blk = blk.split("(libparts")[0]
        ref = re.search(r'\(ref "([^"]+)"\)', blk).group(1)
        comps[ref] = dict(
            value=re.search(r'\(value "([^"]*)"\)', blk).group(1),
            footprint=re.search(r'\(footprint "([^"]*)"\)', blk).group(1),
            tstamp=re.findall(r'\(tstamps "([^"]+)"\)', blk)[-1],
            fields=dict(re.findall(r'\(field \(name "([^"]+)"\)\s*"([^"]*)"\)', blk)),
            dnp='(property (name "dnp")' in blk)
    nets = {}
    for blk in text.split("(net (code")[1:]:
        name = re.search(r'\(name "([^"]*)"\)', blk).group(1)
        nets[name] = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk)
    return comps, nets


def load_footprint(lib_id):
    nick, name = lib_id.split(":")
    path = os.path.join(HW, "lib", nick + ".pretty") if nick == design.LIB else \
        os.path.join(KICAD_FP, nick + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint {lib_id} not found in {path}")
    fp.SetFPID(pcbnew.LIB_ID(nick, name))
    return fp


# --------------------------------------------------------------------------------------------------- board
def add_outline(board):
    """Rectangle with rounded corners on Edge.Cuts."""
    x1, y1, x2, y2, r = X0, Y0, X0 + W, Y0 + H, R_CORNER

    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*a))
        s.SetEnd(pt(*b))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    seg((x1 + r, y1), (x2 - r, y1))
    seg((x2, y1 + r), (x2, y2 - r))
    seg((x2 - r, y2), (x1 + r, y2))
    seg((x1, y2 - r), (x1, y1 + r))
    k = r * (1 - math.sqrt(0.5))
    for cx, cy, sx, sy, ex, ey, mx, my in [
            (x1 + r, y1 + r, x1, y1 + r, x1 + r, y1, x1 + k, y1 + k),
            (x2 - r, y1 + r, x2 - r, y1, x2, y1 + r, x2 - k, y1 + k),
            (x2 - r, y2 - r, x2, y2 - r, x2 - r, y2, x2 - k, y2 - k),
            (x1 + r, y2 - r, x1 + r, y2, x1, y2 - r, x1 + k, y2 - k)]:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(pt(sx, sy), pt(mx, my), pt(ex, ey))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)


def polygon(zone, pts):
    ol = zone.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(mm(x), mm(y))


def add_rule_area(board, name, layers, pts, tracks=False, vias=False, pour=False):
    """Rule area: what is NOT allowed inside."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowCopperPour(pour)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    polygon(z, pts)
    board.Add(z)


def add_track(board, net, width, pts, layer):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a))
        t.SetEnd(pt(*b))
        t.SetWidth(mm(width))
        t.SetLayer(layer)
        t.SetNet(net)
        t.SetLocked(True)
        board.Add(t)


def add_via(board, net, x, y, locked=True):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetWidth(mm(0.6))
    v.SetDrill(mm(0.3))
    v.SetNet(net)
    v.SetLocked(locked)
    board.Add(v)


def add_text(board, text, x, y, layer, size=0.8, thick=0.12, angle=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(thick))
    t.SetTextAngleDegrees(angle)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu):
        t.SetMirrored(True)
    board.Add(t)


def place():
    comps, nets = read_netlist()
    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(2)
    tb = board.GetTitleBlock()
    tb.SetTitle(design.TITLE)
    tb.SetRevision(design.REV)
    tb.SetCompany(design.COMPANY)
    tb.SetDate(design.DATE)

    netinfo = {}
    for name in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni
    pin_net = {(ref, pin): name for name, nodes in nets.items() for ref, pin in nodes}

    missing = set(comps) ^ set(PLACE)
    assert not missing, f"placement table and schematic differ: {missing}"
    for ref, c in sorted(comps.items()):
        fp = load_footprint(c["footprint"])
        board.Add(fp)
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + c["tstamp"]))
        fp.SetSheetname("/")
        fp.SetSheetfile(os.path.basename(SCH))
        x, y, rot, side = PLACE[ref]
        for k in ("MPN", "Description"):
            if c["fields"].get(k):
                fp.SetField(k, c["fields"][k])
                f = fp.GetFieldByName(k)
                f.SetVisible(False)
                f.SetLayer(pcbnew.B_Fab if side == "B" else pcbnew.F_Fab)
        if c["dnp"]:
            fp.SetDNP(True)
        fp.Value().SetVisible(False)  # values are in the BOM; the assembly drawing shows the designators
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        if side == "B":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        for pad in fp.Pads():
            net = pin_net.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(netinfo[net])
            if ref == "BT1" and pad.GetNumber() == "2":  # coin cell contact: solid, no thermal spokes
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        # reference designators on the silkscreen for the larger parts only; the 0402/0603 passives are too
        # dense, see the assembly drawing (F.Fab) instead; the test pads get their function as label
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Reference().SetVisible(ref in SILK_REFS)
        if ref in SILK_REFS and SILK_REFS[ref]:
            fp.Reference().SetPosition(pt(*SILK_REFS[ref]))
            fp.Reference().SetTextAngleDegrees(0)

    for net, width, pts, layer in ROUTES:
        add_track(board, netinfo[net], width, pts, pcbnew.F_Cu if layer == F else pcbnew.B_Cu)
    for net, x, y in VIAS:
        add_via(board, netinfo[net], x, y)

    add_outline(board)
    # no tracks on the top layer under the module body (ground under the module, Espressif guideline)
    add_rule_area(board, "under-module", [pcbnew.F_Cu],
                  [(107.3, 100.4), (122.7, 100.4), (122.7, 112.7), (107.3, 112.7)], tracks=True)

    # silkscreen
    add_text(board, "MATTER H2 BUTTON", 115.0, 136.4, pcbnew.F_SilkS, 0.8, 0.12)
    add_text(board, f"rev {design.REV}", 125.6, 136.4, pcbnew.F_SilkS, 0.7, 0.1)
    add_text(board, "CR2032 +", 115.0, 136.3, pcbnew.B_SilkS, 0.8, 0.12)
    add_text(board, "+", 128.4, 126.0, pcbnew.B_SilkS, 1.2, 0.18)
    for ref, label in TP_LABEL.items():
        x, y, _, _ = PLACE[ref]
        add_text(board, label, x, y + 1.55, pcbnew.B_SilkS, 0.6, 0.1)

    pcbnew.SaveBoard(PCB, board)
    print("placed:", PCB)
    return board


# ------------------------------------------------------------------------------------------------- routing
def freeroute(board):
    os.makedirs(BUILD, exist_ok=True)
    dsn = os.path.join(BUILD, design.PROJECT + ".dsn")
    ses = os.path.join(BUILD, design.PROJECT + ".ses")
    if os.path.exists(ses):
        os.remove(ses)
    assert pcbnew.ExportSpecctraDSN(board, dsn), "DSN export failed"
    # FreeRouting 2.1 keeps passing while connections stay unrouted: the job timeout ends it and writes the SES
    cmd = ["java", "-jar", FREEROUTING, "-de", dsn, "-do", ses, "--gui.enabled=false", "-da", "-inc", "Ground",
           f"--router.job_timeout={JOB_TIMEOUT}"]
    print(" ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    log = os.path.join(BUILD, "freerouting.log")
    with open(log, "w") as f:
        f.write(r.stdout + r.stderr)
    if not os.path.exists(ses):
        raise SystemExit("FreeRouting produced no SES, see " + log)
    assert pcbnew.ImportSpecctraSES(board, ses), "SES import failed"


def cleanup_pass(board, keep_nets=(design.GND,)):
    """One pass: remove the dangling stubs FreeRouting leaves behind and the vias connected on one layer only
    (GND vias stay: the pours connect them). An end is connected when a pad, a via or another track of the same
    net covers it on the same layer."""
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    items = list(board.GetTracks())
    tracks = [t for t in items if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in items if t.GetClass() == "PCB_VIA"]

    def covered(pos, layer, net, skip):
        return (any(p.GetNetCode() == net and p.IsOnLayer(layer) and p.HitTest(pos) for p in pads) or
                any(v.GetNetCode() == net and v.HitTest(pos) for v in vias) or
                any(t is not skip and t.GetNetCode() == net and t.GetLayer() == layer and t.HitTest(pos)
                    for t in tracks))

    bad = [t for t in tracks
           if not (covered(t.GetStart(), t.GetLayer(), t.GetNetCode(), t) and
                   covered(t.GetEnd(), t.GetLayer(), t.GetNetCode(), t))]
    for v in vias:
        if v.GetNetname() in keep_nets:
            continue
        on = [any(t.GetNetCode() == v.GetNetCode() and t.GetLayer() == layer and
                  (v.HitTest(t.GetStart()) or v.HitTest(t.GetEnd())) for t in tracks) or
              any(p.GetNetCode() == v.GetNetCode() and p.IsOnLayer(layer) and p.HitTest(v.GetPosition())
                  for p in pads)
              for layer in (pcbnew.F_Cu, pcbnew.B_Cu)]
        if not all(on):
            bad.append(v)
    for t in bad:
        if not t.IsLocked():
            board.Remove(t)
    return len([t for t in bad if not t.IsLocked()])


def add_pour(board, net, layer, priority=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetZoneName("GND " + board.GetLayerName(layer))
    z.SetLocalClearance(mm(0.25))
    z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.25))
    z.SetThermalReliefSpokeWidth(mm(0.3))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetAssignedPriority(priority)
    e = 0.0
    polygon(z, [(X0 + e, Y0 + e), (X0 + W - e, Y0 + e), (X0 + W - e, Y0 + H - e), (X0 + e, Y0 + H - e)])
    board.Add(z)
    return z


def fill(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def stitch(board, gnd, pitch=2.0, keep=0.35):
    """GND vias wherever both pours have copper around the whole via (plus `keep` mm)."""
    pours = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd.GetNetCode()]
    r = 0.3 + keep
    ring = [(0, 0)] + [(r * math.cos(a), r * math.sin(a)) for a in [i * math.pi / 6 for i in range(12)]]
    holes = [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
             for fp in board.GetFootprints() for p in fp.Pads() if p.HasHole()]
    holes += [(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y))
              for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    contact_r = 9.4  # no stitching via in the contact pad of the coin cell (bottom side, 17.8 mm)
    # nor on silkscreen text (labels of the test pads, designators, board name)
    texts = [t for t in board.GetDrawings() if t.GetClass() == "PCB_TEXT"]
    texts += [fp.Reference() for fp in board.GetFootprints() if fp.Reference().IsVisible()]
    boxes = []
    for t in texts:
        bb = t.GetBoundingBox()
        boxes.append((pcbnew.ToMM(bb.GetLeft()) - 0.5, pcbnew.ToMM(bb.GetTop()) - 0.5,
                      pcbnew.ToMM(bb.GetRight()) + 0.5, pcbnew.ToMM(bb.GetBottom()) + 0.5))
    added = []
    y = Y0 + 1.2
    while y < Y0 + H - 1.0:
        x = X0 + 1.2
        while x < X0 + W - 1.0:
            ok = all(any(z.HitTestFilledArea(layer, pt(x + dx, y + dy), 0) for z in pours if z.GetLayer() == layer)
                     for layer in (pcbnew.F_Cu, pcbnew.B_Cu) for dx, dy in ring)
            ok = ok and all(math.hypot(x - hx, y - hy) > 1.2 for hx, hy in holes + added)
            ok = ok and math.hypot(x - HOLDER[0], y - HOLDER[1]) > contact_r
            ok = ok and not any(x1 <= x <= x2 and y1 <= y <= y2 for x1, y1, x2, y2 in boxes)
            if ok:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pt(x, y))
                v.SetWidth(mm(0.6))
                v.SetDrill(mm(0.3))
                v.SetNet(gnd)
                board.Add(v)
                added.append((x, y))
            x += pitch
        y += pitch
    return len(added)


def drc(path, *extra):
    rpt = os.path.join(BUILD, "drc.rpt")
    subprocess.run(["kicad-cli", "pcb", "drc", "--severity-all", *extra, "-o", rpt, path], check=True,
                   capture_output=True)
    return open(rpt).read()


def drc_dangling():
    """(x, y, net) of what KiCad's DRC calls dangling, GND excepted (the pours connect it)."""
    spots = set()
    for x, y, net in re.findall(r"\[(?:track|via)_dangling\].*?\n.*?\n\s+@\(([-\d.]+) mm, ([-\d.]+) mm\): "
                                r"(?:Track|Via) \[([^\]]*)\]", drc(PCB)):
        if net != design.GND:
            spots.add((round(float(x), 3), round(float(y), 3), net))
    return spots


def step_freeroute():
    board = pcbnew.LoadBoard(PCB)
    freeroute(board)
    pcbnew.SaveBoard(PCB, board)


def step_cleanup():
    """One pass: geometric dangling test, then KiCad's DRC dangling report. Prints the number removed."""
    board = pcbnew.LoadBoard(PCB)
    n = cleanup_pass(board)
    if n == 0:
        spots = drc_dangling()
        for t in list(board.GetTracks()):
            ends = [t.GetStart(), t.GetEnd()] if t.GetClass() == "PCB_TRACK" else [t.GetPosition()]
            keys = {(round(pcbnew.ToMM(p.x), 3), round(pcbnew.ToMM(p.y), 3), t.GetNetname()) for p in ends}
            if keys & spots and not t.IsLocked():
                board.Remove(t)
                n += 1
    pcbnew.SaveBoard(PCB, board)
    print("removed", n)


def step_pour():
    """GND pours on both layers and stitching vias; replaces earlier ones, so it can run again on its own."""
    board = pcbnew.LoadBoard(PCB)
    gnd = board.FindNet(design.GND)
    old = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd.GetNetCode()]
    old += [v for v in board.GetTracks() if v.GetClass() == "PCB_VIA" and v.GetNetCode() == gnd.GetNetCode()
            and not v.IsLocked()]
    for item in old:
        board.Remove(item)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        add_pour(board, gnd, layer)
    fill(board)
    n = stitch(board, gnd)
    fill(board)
    pcbnew.SaveBoard(PCB, board)
    print("stitching vias", n)


def run_step(name):
    """Each step in its own process (os._exit: no teardown of the SWIG objects)."""
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "_" + name], capture_output=True, text=True)
    out = "\n".join(line for line in (r.stdout + r.stderr).splitlines() if "memory leak" not in line)
    if r.returncode != 0:
        raise SystemExit(f"step {name} failed:\n{out}")
    return out


def route():
    print(run_step("freeroute"))
    for _ in range(8):
        out = run_step("cleanup")
        if out.strip().endswith("removed 0"):
            break
    print(run_step("pour"))
    print("routed:", PCB)


if __name__ == "__main__":
    stage = sys.argv[1] if len(sys.argv) > 1 else "all"
    if stage.startswith("_"):  # internal step, see run_step()
        {"_freeroute": step_freeroute, "_cleanup": step_cleanup, "_pour": step_pour}[stage]()
        sys.stdout.flush()
        os._exit(0)
    if stage in ("place", "all"):
        place()
    if stage in ("route", "all"):
        route()
    if stage == "pour":
        print(run_step("pour"))
    sys.stdout.flush()
    os._exit(0)
