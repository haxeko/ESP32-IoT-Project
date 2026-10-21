import pcbnew, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_ses, stitch
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
for t in list(b.GetTracks()):
    if not t.IsLocked(): b.Remove(t)
print("imported:", import_ses.import_ses(b, "/tmp/route/board.ses"))
print("stitch vias:", len(stitch.stitch(b)))
pcbnew.SaveBoard(PCB, b)
