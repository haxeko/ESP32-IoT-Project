#!/usr/bin/env python3
"""Generate, route and check the PCB (KiCad 9 pcbnew API + FreeRouting).

  python3 gen_pcb.py place     board from the schematic netlist: outline, placement, nets, rule areas, texts
  python3 gen_pcb.py route     FreeRouting (Specctra DSN/SES), GND pours both layers, stitching vias
  python3 gen_pcb.py pour      GND pours and stitching vias again (after a manual change)
  python3 gen_pcb.py all       place + route
  python3 gen_pcb.py sync      values and part numbers from the schematic onto the routed board (no placement)

The footprints, values, nets and symbol links come from the netlist that kicad-cli exports from the schematic,
as "Update PCB from Schematic" would do, so DRC with --schematic-parity checks the board against the schematic.

Board: 25 x 27 mm, 2 layers, 1.6 mm. The module antenna (6 mm) overhangs the top edge, nothing under it.
The CR2032 holder is on the bottom side under the module and the circuit, the cell 6.5 mm from the antenna edge.
Every other part is on the top side; the test pads are on the bottom side under the module (pogo pins).
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

X0, Y0, W, H, R_CORNER = 100.0, 100.0, 25.0, 27.0, 1.0  # board outline, mm
MODULE = (112.5, 104.0)  # module centre: antenna area (y -10 .. -4 of the footprint) outside the top edge
HOLDER = (112.5, 116.46)  # CR2032 holder centre, bottom side: the cell edge 0.5 mm from the bottom edge
# bottom side, ring from the edge of the contact pad (8.9 mm) to just outside the cell (10 mm): the crimped edge of the
# cell is its + terminal, so no copper at all (tracks, vias, pour) where the cell slides in and sits
CELL_RING = (8.95, 10.3)

# ref: (x, y, rotation, side). Rotation counter-clockwise: 0 pad 1 left, 90 pad 1 down, 180 right, 270 up.
PLACE = {
    "U1": (*MODULE, 0, "F"),
    "BT1": (*HOLDER, 0, "B"),
    # left of the module: decoupling at pin 1 (3V3), EN pull-up (and RC option) at pin 2, GPIO8 strap at pin 7
    "C6": (102.2, 101.62, 270, "F"),
    "R4": (101.72, 103.4, 0, "F"),
    "C7": (101.72, 104.5, 180, "F"),
    "R5": (101.72, 110.1, 0, "F"),
    # right of the module: battery sense at IO1 (pin 27)
    "R9": (123.3, 102.6, 180, "F"),
    "C9": (123.3, 101.35, 180, "F"),
    # below the module, left: 3V3 out (JP2, C5), button pull-up and RC filter
    "JP2": (101.6, 122.62, 90, "F"),
    "C5": (101.6, 119.2, 270, "F"),
    "R7": (103.6, 115.32, 180, "F"),
    "C8": (102.2, 116.4, 0, "F"),
    "R6": (105.3, 116.0, 90, "F"),
    # TPS63900 (VOUT left, VIN right): CFG resistors above, inductor below
    "U2": (106.8, 122.0, 270, "F"),
    "R1": (106.8, 118.5, 90, "F"),
    "R2": (105.8, 118.5, 90, "F"),
    "R3": (104.8, 118.5, 90, "F"),
    "L1": (106.8, 125.35, 180, "F"),
    "C4": (103.7, 122.8, 90, "F"),
    "C3": (109.9, 122.8, 90, "F"),
    # button in the middle, LED (IO22, pin 15) on its right
    "SW1": (112.0, 116.8, 0, "F"),
    "R8": (116.3, 115.35, 0, "F"),
    "D1": (119.0, 115.6, 180, "F"),
    # battery input, right: + tab of the holder -> Q1 -> JP1 -> C1, C2 (VBAT)
    "C2": (112.6, 122.6, 90, "F"),
    "C1": (116.0, 122.6, 90, "F"),
    "Q1": (120.6, 121.0, 0, "F"),
    "JP1": (120.6, 124.3, 180, "F"),
    # test pads, bottom side, under the module (pogo pins, 2.54 x 2.7 mm grid)
    "TP1": (107.42, 101.3, 0, "B"), "TP2": (109.96, 101.3, 0, "B"), "TP7": (112.5, 101.3, 0, "B"),
    "TP8": (115.04, 101.3, 0, "B"), "TP10": (117.58, 101.3, 0, "B"),
    "TP5": (107.42, 104.0, 0, "B"), "TP6": (109.96, 104.0, 0, "B"), "TP4": (112.5, 104.0, 0, "B"),
    "TP3": (115.04, 104.0, 0, "B"), "TP9": (117.58, 104.0, 0, "B"),
}

# Hand-routed tracks (top layer unless "B"): the 0.5 mm pitch TPS63900 and the short local connections.
# FreeRouting keeps them (locked) and routes the rest. (net, width, [points], layer)
F, B = "F", "B"
ROUTES = [
    # TPS63900 (U2 rotated: bottom row VOUT LX2 GND LX1 VIN, top row CFG3 CFG2 CFG1 SEL EN, from left)
    ("GND", 0.25, [(106.8, 123.2125), (106.8, 122.0)], F),                    # pin 8 to exposed pad
    ("GND", 0.3, [(106.8, 122.0), (103.7, 122.025)], F),                      # EP to C4
    ("GND", 0.3, [(106.8, 122.0), (108.6, 122.0), (109.9, 122.025)], F),      # EP, via, C3
    ("/VOUT", 0.25, [(105.8, 123.2125), (105.8, 123.5), (103.7, 123.5)], F),  # pin 6 to C4
    ("/VOUT", 0.3, [(103.7, 123.575), (102.9, 123.27), (101.6, 123.27)], F),  # C4 to JP2
    ("VBAT", 0.25, [(107.8, 123.2125), (107.8, 123.5), (109.9, 123.5)], F),   # VIN to C3
    ("/LX1", 0.3, [(107.3, 123.2125), (107.3, 124.6)], F),
    ("/LX2", 0.3, [(106.3, 123.2125), (106.3, 124.6)], F),
    ("/CFG1", 0.2, [(106.8, 120.7875), (106.8, 119.01)], F),
    ("/CFG2", 0.2, [(106.3, 120.7875), (106.3, 120.1), (105.8, 119.6), (105.8, 119.01)], F),
    ("/CFG3", 0.2, [(105.8, 120.7875), (105.8, 120.4), (104.8, 119.4), (104.8, 119.01)], F),
    ("VBAT", 0.25, [(107.3, 120.5), (107.8, 120.5)], F),                      # SEL and EN to VIN
    ("VBAT", 0.25, [(107.8, 120.5), (107.8, 120.0), (108.1, 119.7), (110.9, 119.7), (110.9, 123.6),
                    (110.375, 123.6)], F),
    ("GND", 0.3, [(104.8, 117.99), (107.7, 117.99)], F),                      # R1-R3 to GND
    # battery input: + tab (bottom side) -> Q1 -> JP1 -> C1, C2 -> C3 / VIN; VBAT up the right edge to R9, TP9
    ("/BAT+", 0.4, [(123.3, 120.2), (123.45, 118.7)], B),
    ("/BAT+", 0.4, [(121.537, 121.0), (122.6, 121.0), (123.3, 120.3)], F),
    ("GND", 0.3, [(119.662, 120.05), (118.4, 120.05)], F),                    # Q1 gate
    ("/VBAT_Q", 0.4, [(119.662, 121.95), (120.6, 122.9), (121.25, 123.6)], F),
    ("VBAT", 0.4, [(110.375, 123.6), (111.6, 124.075), (117.35, 124.075), (119.95, 124.3)], F),
    ("VBAT", 0.3, [(119.95, 124.3), (119.95, 125.6), (124.1, 125.6), (124.1, 103.0), (123.81, 102.6)], F),
    ("GND", 0.3, [(112.6, 121.125), (112.6, 120.15)], F),                     # C2
    ("GND", 0.3, [(116.0, 121.125), (116.0, 120.15)], F),                     # C1
    # button: both pads of each contact, pull-up and RC filter on the left; LED right of the button
    ("/BTN_SW", 0.25, [(109.925, 115.725), (114.075, 115.725)], F),
    ("GND", 0.3, [(109.925, 117.875), (114.075, 117.875)], F),
    ("/BTN_SW", 0.25, [(104.11, 115.32), (105.3, 115.49), (106.0, 115.7), (109.925, 115.725)], F),
    ("/BTN", 0.2, [(101.9, 113.4), (101.9, 116.2)], F),
    ("/BTN", 0.2, [(101.9, 115.32), (103.09, 115.32)], F),
    ("GND", 0.25, [(102.68, 116.4), (104.1, 116.6)], F),                      # C8
    ("+3V3", 0.25, [(101.6, 118.425), (102.4, 117.3), (105.3, 117.3), (105.3, 116.51)], F),  # R6
    ("GND", 0.3, [(101.6, 119.975), (102.5, 120.75)], F),                     # C5
    ("/LED", 0.2, [(113.25, 114.4), (113.25, 114.95), (115.4, 114.95), (115.79, 115.35)], F),
    ("/LED_A", 0.2, [(116.81, 115.35), (117.4, 115.6), (118.213, 115.6)], F),
    ("GND", 0.3, [(119.787, 115.6), (121.0, 115.6)], F),                      # D1 cathode
    # left of the module: 3V3 trunk from JP2 to pin 1 with its pull-ups; GPIO10 crosses the strap and BOOT
    # stubs on the bottom side, next to the edge
    ("+3V3", 0.3, [(101.0, 121.97), (100.55, 121.97), (100.55, 101.14), (103.2, 101.14)], F),
    ("+3V3", 0.3, [(100.55, 103.4), (101.21, 103.4)], F),                     # R4
    ("+3V3", 0.3, [(100.55, 110.1), (101.21, 110.1)], F),                     # R5
    ("+3V3", 0.3, [(100.55, 118.425), (101.6, 118.425)], F),                  # C5
    ("+3V3", 0.25, [(104.4, 101.1), (104.9, 101.1)], F),                      # to TP1
    ("/EN", 0.2, [(103.1, 102.9), (102.23, 103.4), (102.2, 104.5)], F),
    ("/EN", 0.2, [(104.4, 102.6), (104.9, 102.6)], F),                        # to TP5
    ("GND", 0.25, [(102.2, 102.1), (101.3, 102.3)], F),                       # C6
    ("GND", 0.25, [(101.24, 104.5), (101.24, 105.4)], F),                     # C7
    ("/BTN", 0.2, [(101.9, 107.1), (103.1, 107.1)], F),
    ("/BTN", 0.2, [(101.9, 113.4), (101.3, 112.8), (101.3, 107.7), (101.9, 107.1)], B),
    ("/STRAP8", 0.2, [(102.23, 110.1), (103.1, 110.1)], F),
    ("/BOOT", 0.2, [(102.25, 111.6), (103.1, 111.6)], F),
    # right of the module: battery sense; UART down to the test pads (FreeRouting takes USB D+/D-)
    ("/VBAT_SENSE", 0.2, [(121.6, 102.6), (122.79, 102.6), (123.78, 101.35)], F),
    ("/VBAT_SENSE", 0.2, [(120.6, 102.6), (120.1, 102.6)], F),
    ("GND", 0.2, [(122.82, 101.35), (122.3, 101.85), (120.0, 101.85)], F),   # C9, between pins 28 and 27
    ("/TXD0", 0.2, [(121.6, 110.1), (122.75, 110.1)], F),
    ("/RXD0", 0.2, [(121.6, 111.6), (122.75, 111.6)], F),
    ("GND", 0.3, [(109.96, 101.3), (111.23, 102.65)], B),                     # TP2
]
# GND vias: inside the contact pad of the cell or outside the cell, never in CELL_RING
VIAS = [("GND", 108.6, 122.0), ("GND", 107.7, 117.99), ("GND", 118.4, 120.05), ("GND", 112.6, 120.15),
        ("GND", 116.0, 120.15), ("GND", 112.0, 117.875), ("GND", 104.1, 116.6), ("GND", 102.5, 120.75),
        ("GND", 121.0, 115.6), ("GND", 101.3, 102.3), ("GND", 101.24, 105.4),
        ("GND", 111.23, 102.65), ("/BAT+", 123.3, 120.2), ("+3V3", 104.9, 101.1), ("/EN", 104.9, 102.6),
        ("/BTN", 101.9, 107.1), ("/BTN", 101.9, 113.4), ("/BOOT", 102.25, 111.6), ("/VBAT_SENSE", 120.1, 102.6),
        ("/TXD0", 122.75, 110.1), ("/RXD0", 122.75, 111.6), ("VBAT", 124.1, 105.6)]

# reference designators shown on the silkscreen: ref -> (x, y) or None (footprint default)
SILK_REFS = {"U1": (112.5, 111.2), "JP1": (122.3, 126.35), "JP2": (101.7, 125.4)}

# silkscreen labels of the test pads (bottom side), below each pad
TP_LABEL = {"TP1": "3V3", "TP2": "GND", "TP3": "TX", "TP4": "RX", "TP5": "EN", "TP6": "BOOT", "TP7": "D-",
            "TP8": "D+", "TP9": "VBAT", "TP10": "VSNS"}

# top layer under the module body: no tracks (ground under the module, Espressif guideline); the 1 mm next to
# the pads stays free for the fan-out vias
UNDER_MODULE = [(105.4, 100.4), (119.6, 100.4), (119.6, 112.6), (105.4, 112.6)]

# silkscreen texts: (text, x, y, layer, size, thickness)
TEXTS = [("MATTER H2 BUTTON", 114.0, 126.3, F, 0.7, 0.1), (f"rev {design.REV}", 121.6, 104.4, B, 0.6, 0.1),
         ("+", 124.1, 122.6, B, 1.0, 0.15)]


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


def save(board):
    """SaveBoard also writes every default setting into the project file: keep it as gen_project.py wrote it."""
    pro = PCB.replace(".kicad_pcb", ".kicad_pro")
    settings = open(pro).read() if os.path.exists(pro) else None
    pcbnew.SaveBoard(PCB, board)
    if settings is not None:
        with open(pro, "w") as f:
            f.write(settings)


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


def set_fields(fp, c, side):
    """Schematic fields kept on the footprint (hidden, on the fab layer): part numbers for the BOM and the CPL."""
    for k in ("MPN", "LCSC", "Description"):
        if c["fields"].get(k):
            fp.SetField(k, c["fields"][k])
            f = fp.GetFieldByName(k)
            f.SetVisible(False)
            f.SetLayer(pcbnew.B_Fab if side == "B" else pcbnew.F_Fab)
        elif fp.HasField(k):
            fp.RemoveField(k)
    fp.SetDNP(c["dnp"])


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
        set_fields(fp, c, side)
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
    add_rule_area(board, "under-module", [pcbnew.F_Cu], UNDER_MODULE, tracks=True)
    r1, r2 = CELL_RING
    for i in range(24):  # ring as 24 sectors (plain polygons for the Specctra export)
        a = [math.radians(15 * (i + k)) for k in (0, 0.5, 1)]
        pts = [(HOLDER[0] + r * math.cos(t), HOLDER[1] + r * math.sin(t)) for r, ts in ((r1, a), (r2, a[::-1]))
               for t in ts]
        add_rule_area(board, f"coin-cell-{i}", [pcbnew.B_Cu], pts, tracks=True, vias=True, pour=True)

    # silkscreen
    for text, x, y, layer, size, thick in TEXTS:
        add_text(board, text, x, y, pcbnew.F_SilkS if layer == F else pcbnew.B_SilkS, size, thick)
    for ref, label in TP_LABEL.items():
        x, y, _, _ = PLACE[ref]
        add_text(board, label, x, y + 1.35, pcbnew.B_SilkS, 0.6, 0.1)

    save(board)
    print("placed:", PCB)
    return board


def sync():
    """Values and part numbers changed in design.py (same footprints and nets): update the placed and routed board
    in place, no new placement or routing."""
    comps, _ = read_netlist()
    board = pcbnew.LoadBoard(PCB)
    board.GetTitleBlock().SetDate(design.DATE)
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    missing = set(comps) ^ set(fps)
    assert not missing, f"board and schematic differ: {missing}, run the place and route stages"
    for ref, c in sorted(comps.items()):
        fp = fps[ref]
        if fp.GetFPIDAsString() != c["footprint"]:
            raise SystemExit(f"{ref}: footprint changed to {c['footprint']}, run the place and route stages")
        fp.SetValue(c["value"])
        set_fields(fp, c, "B" if fp.IsFlipped() else "F")
    save(board)
    print("synced:", PCB)


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
    """One pass: remove the dangling stubs and duplicate segments FreeRouting leaves behind and the vias connected
    on one layer only (GND vias stay: the pours connect them). An end is connected when a pad, a via or another
    track of the same net covers it on the same layer."""
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    items = list(board.GetTracks())
    tracks = [t for t in items if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in items if t.GetClass() == "PCB_VIA"]
    seen, dups = set(), []  # FreeRouting sometimes leaves the same segment twice
    for t in tracks:
        ends = tuple(sorted(((t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y))))
        key = (t.GetNetCode(), t.GetLayer(), ends)
        if key in seen and not t.IsLocked():
            dups.append(t)
        seen.add(key)
    for t in dups:
        board.Remove(t)
    tracks = [t for t in tracks if t not in dups]

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
    return len(dups) + len([t for t in bad if not t.IsLocked()])


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
    contact_r = CELL_RING[1] + 0.3  # no stitching via under the coin cell (contact pad and ring)
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
    save(board)


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
    save(board)
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
    save(board)
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
    if stage == "sync":
        sync()
    sys.stdout.flush()
    os._exit(0)
