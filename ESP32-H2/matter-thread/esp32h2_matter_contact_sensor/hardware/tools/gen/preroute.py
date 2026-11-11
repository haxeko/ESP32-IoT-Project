# Hand-routed USB-C fan-out (locked); Freerouting does the rest.
# The 16P receptacle has D-/D+/D-/D+ interleaved at 0.5 mm (B7 A6 A7 B6), so one net has to change layer:
#   D-: B7 and A7 joined on the top layer behind the pad row
#   D+: A6 drops to the bottom through V1, runs under D- and comes back up at V2 on the B6 track
#   CC1 escapes to the left, CC2 to the right behind the VBUS via (B6 swings over its pad column)
#   VBUS: both VBUS pads go down through vias and are bridged on the bottom layer under the connector body
# Coordinates are in the J1 footprint frame (mm, y negative = towards the board centre), J1 is not rotated.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import PARTS, PROJ_DIR, PROJECT, kc

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
MM = pcbnew.FromMM
J1 = next(p for p in PARTS if p["ref"] == "J1")
assert J1["rot"] == 0 and J1["side"] == "F"
OX, OY = kc(J1["u"], J1["v"])
Y_PAD = -4.77   # rear end of the signal pads


def P(x, y):
    return pcbnew.VECTOR2I_MM(OX + x, OY + y)


def track(b, pts, net, w=0.2, layer=pcbnew.F_Cu):
    n = b.GetNetsByName()[net]
    for a, c in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(P(*a))
        t.SetEnd(P(*c))
        t.SetWidth(MM(w))
        t.SetLayer(layer)
        t.SetNet(n)
        t.SetLocked(True)
        b.Add(t)


def via(b, x, y, net):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(P(x, y))
    v.SetWidth(MM(0.6))
    v.SetDrill(MM(0.3))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(b.GetNetsByName()[net])
    v.SetLocked(True)
    b.Add(v)


b = pcbnew.LoadBoard(PCB)
# D- : B7 (-0.75) and A7 (0.25), joined at (0.75, -7.0)
track(b, [(-0.75, Y_PAD), (-0.75, -5.45), (-1.15, -5.85), (-1.15, -7.0), (0.75, -7.0)], "USB_DN_C")
track(b, [(0.25, Y_PAD), (0.25, -5.3), (0.75, -5.8), (0.75, -7.6)], "USB_DN_C")
# D+ : A6 (-0.25) -> V1 -> bottom -> V2 on the B6 (0.75) track
track(b, [(-0.25, Y_PAD), (-0.25, -6.1)], "USB_DP_C")
via(b, -0.25, -6.1, "USB_DP_C")
track(b, [(-0.25, -6.1), (1.45, -7.3)], "USB_DP_C", layer=pcbnew.B_Cu)
track(b, [(0.75, Y_PAD), (0.75, -5.05), (1.45, -5.75), (1.45, -7.6)], "USB_DP_C")
via(b, 1.45, -7.3, "USB_DP_C")
# CC1 (A5, -1.25) to the left, CC2 (B5, 1.75) to the right
track(b, [(-1.25, Y_PAD), (-1.25, -5.15), (-1.9, -5.8), (-1.9, -6.6)], "CC1")
track(b, [(1.75, Y_PAD), (2.1, -6.0), (2.6, -6.5)], "CC2")
# VBUS (A4/B9 at -2.45, A9/B4 at 2.45): vias behind the pads, bottom-layer bridge under the connector body
for x in (-2.45, 2.45):
    vx = x + (0.15 if x > 0 else -0.15)
    track(b, [(x, Y_PAD), (vx, -5.3)], "VBUS", 0.4)
    via(b, vx, -5.3, "VBUS")
track(b, [(-2.6, -5.3), (-2.6, -4.5), (2.6, -4.5), (2.6, -5.3)], "VBUS", 0.4, pcbnew.B_Cu)
pcbnew.SaveBoard(PCB, b)
print("J1 fan-out pre-routed at", (OX, OY))
