# DRC with the project rules (pcbnew python API: kicad-cli 7 has no DRC). Prints counts per violation type;
# "python3 drc.py fill" refills the zones first, extra arguments list the details of those violation types.
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import PROJ_DIR, PROJECT, WORK

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
if len(sys.argv) > 1 and sys.argv[1] == "fill":
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
os.makedirs(WORK, exist_ok=True)
rep = f"{WORK}/drc.rpt"
pcbnew.WriteDRCReport(b, rep, pcbnew.EDA_UNITS_MILLIMETRES, True)
txt = open(rep).read()
items = re.findall(r'\[(\w+)\]: ([^\n]*)\n([^\[]*)', txt)
cnt = collections.Counter(i[0] for i in items)
print(dict(cnt) or "DRC clean")
print([l for l in txt.splitlines() if "Found" in l or "unconnected" in l.lower()][:6])
show = sys.argv[2:]
for code, msg, body in items:
    if show and code in show:
        print(code, "|", msg.strip(), "|", " ".join(body.split())[:260])
