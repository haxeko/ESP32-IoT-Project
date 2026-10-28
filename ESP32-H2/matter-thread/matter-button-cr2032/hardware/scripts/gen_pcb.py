#!/usr/bin/env python3
"""Generate, route and check the PCB (KiCad 9 pcbnew API + FreeRouting).

  python3 gen_pcb.py place     board from the schematic netlist: outline, placement, nets, rule areas, texts
  python3 gen_pcb.py route     FreeRouting (Specctra DSN/SES) for what the hand routes leave open, GND pours both
                               layers, stitching vias
  python3 gen_pcb.py pour      GND pours and stitching vias again (after a manual change)
  python3 gen_pcb.py all       place + route
  python3 gen_pcb.py sync      values and part numbers from the schematic onto the routed board (no placement)

The footprints, values, nets and symbol links come from the netlist that kicad-cli exports from the schematic,
as "Update PCB from Schematic" would do, so DRC with --schematic-parity checks the board against the schematic.

Board: 29.8 x 23.9 mm, 2 layers, 1.6 mm; with the module antenna (6 mm, overhanging the top edge) 29.8 x 29.9 mm.
The module sits at the right edge, the TPS63900 on its left. The CR2032 holder is on the bottom side under the module
and the circuit, the cell edge 3.4 mm from the antenna (top board edge). Every other part is on the top side; the
test pads are on the bottom side (pogo pins).
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
PRO = os.path.join(HW, design.PROJECT + ".kicad_pro")
# the project file as gen_project.py wrote it, read before pcbnew touches it: NewBoard and SaveBoard write every
# default setting into it
PRO_TEXT = open(PRO).read() if os.path.exists(PRO) else None
BUILD = os.path.join(HW, "build")  # intermediate files (git-ignored)
FREEROUTING = os.environ.get("FREEROUTING_JAR", os.path.expanduser("~/freerouting-2.1.0.jar"))
JOB_TIMEOUT = os.environ.get("FREEROUTING_TIMEOUT", "00:03:00")

X0, Y0, W, H, R_CORNER = 100.0, 100.0, 29.8, 23.9, 0.5  # board outline, mm; with the antenna 29.8 x 29.9 mm
MODULE = (120.0, 104.0)  # module at the right edge, antenna area (y -10 .. -4 of the footprint) outside the top edge
HOLDER = (114.9, 113.36)  # CR2032 holder centre, bottom side: the cell edge 0.54 mm from the bottom edge
# bottom side, ring from the edge of the contact pad (8.9 mm) to just outside the cell (10 mm): the crimped edge of the
# cell is its + terminal, so no copper at all (tracks, vias, pour) where the cell slides in and sits
CELL_RING = (8.95, 10.3)

# ref: (x, y, rotation, side). Rotation counter-clockwise: 0 pad 1 left, 90 pad 1 down, 180 right, 270 up.
PLACE = {
    "U1": (*MODULE, 0, "F"),
    "BT1": (*HOLDER, 0, "B"),
    # left of the module, next to its pins: decoupling at pin 1 (3V3), EN pull-up (and RC option) at pin 2,
    # button RC filter and pull-up at pin 5 (GPIO10), GPIO8 strap at pin 7
    "C6": (109.6, 101.62, 270, "F"),
    "R4": (108.6, 103.1, 0, "F"),
    "C7": (108.6, 104.2, 180, "F"),
    "C8": (108.1, 106.2, 180, "F"),
    "R7": (109.6, 107.6, 90, "F"),
    "R6": (108.1, 109.1, 0, "F"),
    "R5": (108.6, 110.25, 0, "F"),
    # left, lower half: 3V3 out (C5, JP2) and the TPS63900 (VOUT left, VIN right, CFG resistors above, L1 below)
    "C5": (101.65, 115.2, 90, "F"),
    "JP2": (101.65, 118.62, 90, "F"),
    "U2": (106.85, 118.0, 270, "F"),
    "R1": (106.85, 114.5, 90, "F"),
    "R2": (105.85, 114.5, 90, "F"),
    "R3": (104.85, 114.5, 90, "F"),
    "L1": (106.85, 121.35, 180, "F"),
    "C4": (103.75, 118.8, 90, "F"),
    "C3": (109.95, 118.8, 90, "F"),
    # below the module: button in the middle of the board, C2 under it; LED (IO22, pin 15) on its right
    "SW1": (114.9, 117.4, 0, "F"),
    "C2": (114.9, 121.5, 0, "F"),
    "R8": (119.7, 115.35, 180, "F"),
    "D1": (119.4, 116.8, 180, "F"),
    # battery input, right: + tab of the holder -> Q1 -> JP1 -> C1 -> C2, C3; battery sense R9, C9 in the corner
    "Q1": (127.3, 117.3, 180, "F"),
    "JP1": (124.0, 117.2, 270, "F"),
    "C1": (122.6, 121.5, 180, "F"),
    "R9": (127.0, 121.0, 0, "F"),
    "C9": (127.0, 122.4, 180, "F"),
    # test pads, bottom side (pogo pins): the two top corners next to the holder (left: GND, 3V3, EN, BOOT; right:
    # TX, VBAT_SENSE, USB), RX and VBAT right of the holder, above the corner with R9
    "TP2": (102.0, 101.6, 0, "B"), "TP1": (104.54, 101.6, 0, "B"),
    "TP6": (102.0, 104.3, 0, "B"), "TP5": (104.54, 104.3, 0, "B"),
    "TP3": (125.44, 101.6, 0, "B"), "TP10": (127.98, 101.6, 0, "B"),
    "TP7": (125.44, 104.3, 0, "B"), "TP8": (127.98, 104.3, 0, "B"),
    "TP4": (126.9, 118.5, 0, "B"), "TP9": (127.2, 121.2, 0, "B"),
}

# Hand-routed tracks (top layer unless "B"): every connection but GND (the pours connect it). FreeRouting keeps them
# (locked) and routes whatever is left, nothing in this layout. (net, width, [points], layer)
F, B = "F", "B"
ROUTES = [
    # TPS63900 (U2 rotated: bottom row VOUT LX2 GND LX1 VIN, top row CFG3 CFG2 CFG1 SEL EN, from left)
    ("GND", 0.25, [(106.85, 119.2125), (106.85, 118.0)], F),                  # pin 8 to exposed pad
    ("GND", 0.3, [(106.85, 118.0), (105.05, 118.0), (103.75, 118.025)], F),   # EP, via, C4
    ("GND", 0.3, [(106.85, 118.0), (108.65, 118.0), (109.95, 118.025)], F),   # EP, via, C3
    ("/VOUT", 0.25, [(105.85, 119.2125), (105.85, 119.5), (103.75, 119.5)], F),  # pin 6 to C4
    ("/VOUT", 0.3, [(103.75, 119.575), (102.95, 119.27), (101.65, 119.27)], F),  # C4 to JP2
    ("VBAT", 0.25, [(107.85, 119.2125), (107.85, 119.5), (109.95, 119.5)], F),   # VIN to C3
    ("/LX1", 0.3, [(107.35, 119.2125), (107.35, 120.6)], F),
    ("/LX2", 0.3, [(106.35, 119.2125), (106.35, 120.6)], F),
    ("/CFG1", 0.2, [(106.85, 116.7875), (106.85, 115.01)], F),
    ("/CFG2", 0.2, [(106.35, 116.7875), (106.35, 116.1), (105.85, 115.6), (105.85, 115.01)], F),
    ("/CFG3", 0.2, [(105.85, 116.7875), (105.85, 116.4), (104.85, 115.4), (104.85, 115.01)], F),
    ("VBAT", 0.25, [(107.35, 116.5), (107.85, 116.5)], F),                    # SEL and EN to VIN
    ("VBAT", 0.25, [(107.85, 116.5), (107.85, 116.0), (108.15, 115.7), (110.95, 115.7), (110.95, 119.6),
                    (110.425, 119.6)], F),
    ("GND", 0.3, [(104.85, 113.99), (107.75, 113.99)], F),                    # R1-R3 to GND
    # 3V3: JP2 -> C5 -> trunk along the left edge to pin 1; branch at x 106.5 for the pull-ups R4, R6, R5
    ("+3V3", 0.3, [(101.65, 115.975), (101.65, 117.97)], F),
    ("+3V3", 0.3, [(101.65, 116.6), (100.55, 116.6), (100.55, 101.12), (110.8, 101.12)], F),
    ("+3V3", 0.3, [(106.5, 101.12), (106.5, 110.25), (108.09, 110.25)], F),
    ("+3V3", 0.3, [(106.5, 103.1), (108.09, 103.1)], F),                      # R4
    ("+3V3", 0.3, [(106.5, 109.1), (107.59, 109.1)], F),                      # R6
    ("GND", 0.3, [(101.65, 114.425), (102.6, 113.6)], F),                     # C5
    ("GND", 0.2, [(109.6, 102.1), (110.0, 101.85), (112.85, 101.85)], F),     # C6, between pins 1 and 2
    ("/EN", 0.2, [(110.6, 102.9), (109.11, 103.1), (109.08, 104.2)], F),
    ("/EN", 0.2, [(111.9, 102.6), (112.6, 102.6)], F),                        # to TP5
    ("GND", 0.25, [(108.12, 104.2), (107.3, 105.25)], F),                     # C7
    ("GND", 0.25, [(107.62, 106.2), (107.3, 105.25)], F),                     # C8
    # button: GPIO10 RC at pin 5; BTN_SW enters the module margin between pins 5 and 6, runs down inside the
    # pads and leaves at the corner between pins 9 and 10 to SW1
    ("/BTN", 0.2, [(110.6, 107.1), (109.6, 107.09), (108.58, 106.2)], F),
    ("/BTN_SW", 0.2, [(109.6, 108.11), (108.61, 109.1)], F),
    ("/BTN_SW", 0.2, [(109.6, 108.11), (109.9, 107.85), (112.4, 107.85), (112.4, 115.3), (112.825, 116.0)], F),
    ("/BTN_SW", 0.25, [(112.825, 116.325), (116.975, 116.325)], F),          # both pads of contact 1
    ("GND", 0.3, [(112.825, 118.475), (116.975, 118.475)], F),               # both pads of contact 2
    ("/STRAP8", 0.2, [(109.11, 110.25), (110.6, 110.1)], F),
    ("/BOOT", 0.2, [(110.6, 111.6), (104.6, 111.6), (104.6, 109.5)], F),
    # LED right of the button
    ("/LED", 0.2, [(120.75, 114.4), (120.75, 114.9), (120.21, 115.35)], F),
    ("/LED_A", 0.2, [(119.19, 115.35), (118.613, 116.8)], F),
    ("GND", 0.3, [(120.187, 116.8), (121.2, 116.0)], F),                      # D1 cathode
    # battery input: + tab (bottom side) -> Q1 -> JP1 -> C1 -> bus along the bottom edge -> C2, C3 / VIN
    ("/BAT+", 0.4, [(125.9, 116.5), (125.885, 115.6)], B),
    ("/BAT+", 0.4, [(125.9, 116.5), (126.362, 117.3)], F),
    ("/VBAT_Q", 0.4, [(128.238, 116.35), (128.238, 115.75), (124.6, 115.75), (124.0, 116.35)], F),
    ("GND", 0.3, [(128.238, 118.25), (128.45, 119.4)], F),                    # Q1 gate
    ("VBAT", 0.4, [(124.0, 117.85), (124.0, 120.4)], F),                      # JP1 to C1
    ("VBAT", 0.4, [(124.075, 122.85), (124.075, 123.35), (110.95, 123.35), (110.95, 119.6)], F),
    ("VBAT", 0.4, [(113.425, 122.85), (113.425, 123.35)], F),                 # C2
    ("VBAT", 0.3, [(124.65, 121.0), (126.49, 121.0)], F),                     # R9, via to TP9
    ("GND", 0.3, [(116.375, 121.5), (117.4, 120.3)], F),                      # C2
    ("GND", 0.3, [(121.125, 121.5), (119.6, 120.0)], F),                      # C1
    # battery sense in the corner; the sense line goes to pin 27 on the bottom side, along the right edge
    ("/VBAT_SENSE", 0.2, [(127.51, 121.0), (127.48, 122.4)], F),
    ("/VBAT_SENSE", 0.2, [(127.51, 121.0), (128.6, 121.0)], F),
    ("/VBAT_SENSE", 0.2, [(128.6, 121.0), (129.2, 120.4), (129.2, 103.0), (128.3, 102.1)], B),  # to TP10
    ("GND", 0.25, [(126.52, 122.4), (125.6, 122.9)], F),                      # C9
    ("VBAT", 0.3, [(125.6, 121.0), (127.2, 121.2)], B),                       # TP9
    # right of the module: inward stubs to vias next to the pads, to the test pads on the bottom side
    ("/VBAT_SENSE", 0.2, [(128.1, 102.6), (127.4, 102.6)], F),
    ("/USB_DP", 0.2, [(128.1, 107.1), (127.4, 107.1)], F),
    ("/USB_DN", 0.2, [(128.1, 108.6), (127.4, 108.6)], F),
    ("/TXD0", 0.2, [(128.1, 110.1), (127.4, 110.1)], F),
    ("/RXD0", 0.2, [(128.1, 111.6), (127.4, 111.6)], F),
    ("/VBAT_SENSE", 0.2, [(127.4, 102.6), (127.98, 101.6)], B),
    ("/USB_DP", 0.2, [(127.4, 107.1), (127.4, 104.88), (127.98, 104.3)], B),
    ("/USB_DN", 0.2, [(127.4, 108.6), (126.6, 108.6), (126.6, 105.4), (125.5, 104.3)], B),
    ("/TXD0", 0.2, [(127.4, 110.1), (126.2, 110.1), (124.3, 108.2), (124.3, 102.54), (124.9, 101.94)], B),
    ("/RXD0", 0.2, [(127.4, 111.6), (127.4, 117.8), (127.2, 118.0)], B),    # past the + tab of the holder
    # left: test pads in the top left corner
    ("+3V3", 0.3, [(106.1, 101.12), (105.0, 101.12)], B),                    # from the trunk on top
    ("/EN", 0.2, [(112.6, 102.6), (112.25, 102.95), (106.4, 102.95), (105.2, 104.15)], B),
    ("/BOOT", 0.2, [(104.6, 109.5), (102.6, 107.5), (102.6, 105.1), (102.2, 104.7)], B),
    ("GND", 0.3, [(102.0, 101.6), (101.6, 103.0)], B),                        # TP2
]
# GND vias: inside the contact pad of the cell or outside the cell, never in CELL_RING
VIAS = [("GND", 108.65, 118.0), ("GND", 105.05, 118.0), ("GND", 107.75, 113.99), ("GND", 102.6, 113.6),
        ("GND", 107.3, 105.25), ("GND", 114.9, 118.475), ("GND", 121.2, 116.0), ("GND", 128.45, 119.4),
        ("GND", 117.4, 120.3), ("GND", 119.6, 120.0), ("GND", 125.6, 122.9), ("GND", 101.6, 103.0),
        ("/BAT+", 125.9, 116.5), ("+3V3", 106.1, 101.12), ("/EN", 112.6, 102.6), ("/BOOT", 104.6, 109.5),
        ("/VBAT_SENSE", 127.4, 102.6), ("/VBAT_SENSE", 128.6, 121.0), ("/USB_DP", 127.4, 107.1),
        ("/USB_DN", 127.4, 108.6), ("/TXD0", 127.4, 110.1), ("/RXD0", 127.4, 111.6), ("VBAT", 125.6, 121.0)]

# reference designators shown on the silkscreen: ref -> (x, y) or None (footprint default)
SILK_REFS = {"U1": (120.0, 111.0), "JP1": (121.7, 118.9), "JP2": (101.65, 121.1)}

# silkscreen labels of the test pads (bottom side), below each pad
TP_LABEL = {"TP1": "3V3", "TP2": "GND", "TP3": "TX", "TP4": "RX", "TP5": "EN", "TP6": "BOOT", "TP7": "D-",
            "TP8": "D+", "TP9": "VBAT", "TP10": "VSNS"}

# top layer under the module body: no tracks (ground under the module, Espressif guideline); the margin next to the
# pads stays free: button line and test pad vias on the left, test pad vias on the right
UNDER_MODULE = [(113.0, 100.4), (127.0, 100.4), (127.0, 112.6), (113.0, 112.6)]

# silkscreen texts: (text, x, y, layer, size, thickness, angle)
TEXTS = [("Thread Button", 103.1, 106.4, F, 0.8, 0.12, 90), ("Made by Haxeko", 115.0, 101.35, B, 0.9, 0.13, 0),
         (f"rev {design.REV}", 115.0, 102.5, B, 0.6, 0.1, 0), ("+", 128.7, 114.6, B, 1.0, 0.15, 0)]


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
    """SaveBoard, then the project file back as gen_project.py wrote it (PRO_TEXT)."""
    pcbnew.SaveBoard(PCB, board)
    if PRO_TEXT is not None:
        with open(PRO, "w") as f:
            f.write(PRO_TEXT)


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
    for text, x, y, layer, size, thick, angle in TEXTS:
        add_text(board, text, x, y, pcbnew.F_SilkS if layer == F else pcbnew.B_SilkS, size, thick, angle)
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


def drc_unrouted():
    """Nets other than GND with a missing connection, from KiCad's DRC."""
    nets = set()
    for blk in drc(PCB).split("\n[")[1:]:
        if blk.startswith("unconnected_items"):
            nets.update(re.findall(r"@\([^)]*\): [^\[\n]*\[([^\]]*)\]", blk))
    return nets - {design.GND}


def route():
    if drc_unrouted():
        print(run_step("freeroute"))
    else:
        print("FreeRouting skipped: every connection but GND (pours) is routed")
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
