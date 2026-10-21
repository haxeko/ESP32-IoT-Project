# hand-routed USB-C fan-out (mouth at the bottom edge), locked; the autorouter does the rest
import pcbnew
from design import kc
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
MM = pcbnew.FromMM
P = lambda u, v: pcbnew.VECTOR2I_MM(*kc(u, v))
uv = lambda p: (pcbnew.ToMM(p.x) - 100, 100 - pcbnew.ToMM(p.y))

def track(b, pts, net, w, layer=pcbnew.B_Cu):
    n = b.GetNetsByName()[net]
    for a, c in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b); t.SetStart(P(*a)); t.SetEnd(P(*c)); t.SetWidth(MM(w))
        t.SetLayer(layer); t.SetNet(n); t.SetLocked(True); b.Add(t)

def via(b, u, v, net):
    t = pcbnew.PCB_VIA(b); t.SetPosition(P(u, v)); t.SetWidth(MM(0.6)); t.SetDrill(MM(0.3))
    t.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); t.SetNet(b.GetNetsByName()[net]); t.SetLocked(True); b.Add(t)

b = pcbnew.LoadBoard(PCB)
fps = {f.GetReference(): f for f in b.GetFootprints()}
pad = lambda ref, num: [uv(p.GetPosition()) for p in fps[ref].Pads() if p.GetNumber() == num]
J = {n: pad("J1", n)[0] for n in ("A5", "B5", "A9", "B9", "A12", "B12")}
tabs = pad("J1", "S1"); rear = sorted(tabs, key=lambda q: -q[1])[:2]          # rear tabs = closer to the centre
# CC resistors straight above their CC contacts, pad 1 facing the connector
for r, cc in (("R1", "A5"), ("R2", "B5")):
    fps[r].SetPosition(P(J[cc][0], -13.9))
    if pad(r, "1")[0][1] > pad(r, "2")[0][1]:
        fps[r].SetOrientationDegrees(fps[r].GetOrientationDegrees() + 180)
if pad("F1", "1")[0][1] > pad("F1", "2")[0][1]:
    fps["F1"].SetOrientationDegrees(fps["F1"].GetOrientationDegrees() + 180)
# GND contacts -> rear shell tabs on the same side
for g in ("A12", "B12"):
    t = min(rear, key=lambda q: abs(q[0] - J[g][0]))
    track(b, [J[g], (t[0], J[g][1])], "GND", 0.4)
# CC1/CC2 are left to the autorouter (R1/R2 already sit right above their contacts)
# VBUS: A9 straight up into F1 pad 1; B9 joins A9 through a short top-layer jumper (no closed loop on B.Cu)
f1 = pad("F1", "1")[0]
a9, b9 = J["A9"], J["B9"]
track(b, [a9, (a9[0], -11.5), (f1[0], -11.5), f1], "VBUS", 0.4)
yj = -14.9
via(b, b9[0], yj, "VBUS"); via(b, a9[0], yj, "VBUS")
track(b, [b9, (b9[0], yj)], "VBUS", 0.4)
track(b, [(b9[0], yj), (a9[0], yj)], "VBUS", 0.4, pcbnew.F_Cu)
pcbnew.SaveBoard(PCB, b)
print("J1", {k: (round(v[0], 2), round(v[1], 2)) for k, v in J.items()}, "rear tabs", rear, "F1.1", f1)
