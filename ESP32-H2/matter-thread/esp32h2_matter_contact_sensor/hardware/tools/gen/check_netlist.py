# Cross-check: the schematic netlist (kicad-cli) against the nets of every PCB pad.
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import PROJ_DIR, PROJECT, WORK

os.makedirs(WORK, exist_ok=True)
net = f"{WORK}/sch.net"
subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", net,
                f"{PROJ_DIR}/{PROJECT}.kicad_sch"], check=True, capture_output=True)
s = open(net).read()
sch = {}
for m in re.finditer(r'\(net \(code "\d+"\) \(name "([^"]*)"\)((?:\s*\(node [^\n]*\))*)', s):
    for n in re.finditer(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)):
        sch[(n.group(1), n.group(2))] = m.group(1).lstrip("/")
b = pcbnew.LoadBoard(f"{PROJ_DIR}/{PROJECT}.kicad_pcb")
pcb = {(fp.GetReference(), p.GetNumber()): p.GetNetname() for fp in b.GetFootprints() for p in fp.Pads()
       if p.GetNetname()}
bad = [(k, v, sch.get(k)) for k, v in sorted(pcb.items()) if sch.get(k) != v]
extra = [(k, v) for k, v in sch.items() if k not in pcb and not v.startswith("unconnected")]
for x in bad:
    print("MISMATCH", x)
for x in extra:
    print("SCH ONLY", x)
fps = {f.GetReference() for f in b.GetFootprints()}
syms = {k[0] for k in sch}
print("pads compared: %d, mismatches: %d, schematic-only connections: %d" % (len(pcb), len(bad), len(extra)))
print("PCB-only footprints:", sorted(fps - syms), "| schematic-only symbols:", sorted(syms - fps))
sys.exit(1 if bad or extra or (syms - fps) else 0)
