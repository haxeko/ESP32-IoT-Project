# run in a fresh process so the project net classes are read from the .kicad_pro
import pcbnew, subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_ses, stitch
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
W = "/tmp/route"; os.makedirs(W, exist_ok=True)
DSN, SES = f"{W}/board.dsn", f"{W}/board.ses"
passes = sys.argv[1] if len(sys.argv) > 1 else "60"
b = pcbnew.LoadBoard(PCB)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
assert pcbnew.ExportSpecctraDSN(b, DSN)
d = open(DSN).read()
import re, math
# 1) route GND as a real net (zone fills get fragmented by tracks, planes would hide that)
# 2) shrink the routing boundary 0.15 mm so tracks/vias keep >= 0.35 mm from the round edge
def shrink(m):
    nums = m.group(2).split()
    pts = [(float(nums[i]), float(nums[i+1])) for i in range(0, len(nums), 2)]
    k = (22.5 - 0.15) / 22.5
    out = " ".join("%.1f %.1f" % (100000 + (x-100000)*k, -100000 + (y+100000)*k) for x, y in pts)
    return m.group(1) + out + ")"
d = re.sub(r"(\(boundary\s*\(path pcb 0\s+)([-\d\.\s]+)\)", shrink, d, count=1)
open(DSN, "w").write(d)
print("planes left:", d.count("(plane"))
print("classes:", [l.strip()[:90] for l in d.splitlines() if l.strip().startswith("(class")])
print('exported', DSN)
