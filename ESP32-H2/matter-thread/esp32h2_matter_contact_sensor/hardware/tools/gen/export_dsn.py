# Exports the board to Specctra DSN for Freerouting. Run in a fresh process so the project net classes
# are read from the .kicad_pro.
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import BOARD_W, BOARD_H, CENTER, PROJ_DIR, PROJECT, WORK

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
os.makedirs(WORK, exist_ok=True)
DSN = f"{WORK}/board.dsn"
b = pcbnew.LoadBoard(PCB)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
assert pcbnew.ExportSpecctraDSN(b, DSN)
d = open(DSN).read()


def drop_blocks(text, head):
    """remove every balanced (head ...) s-expression"""
    out, i = [], 0
    while True:
        j = text.find(head, i)
        if j < 0:
            return "".join(out) + text[i:]
        out.append(text[i:j])
        depth = 0
        for k in range(j, len(text)):
            depth += {"(": 1, ")": -1}.get(text[k], 0)
            if depth == 0:
                break
        i = k + 1


# GND stays a plane on both layers: the module GND pads (EPAD, the row next to the antenna) can only be
# reached by the top pour, so GND is connected by the pours and stitching vias (stitch.py), checked by DRC.
if os.environ.get("H2C_ROUTE_GND"):
    d = drop_blocks(d, "(plane ")


# shrink the routing boundary 0.15 mm so tracks and vias keep >= 0.35 mm from the board edge
def shrink(m):
    nums = m.group(2).split()
    pts = [(float(nums[i]), float(nums[i + 1])) for i in range(0, len(nums), 2)]
    cx, cy = CENTER[0] * 1000, -CENTER[1] * 1000
    kx = (BOARD_W / 2 - 0.15) / (BOARD_W / 2)
    ky = (BOARD_H / 2 - 0.15) / (BOARD_H / 2)
    out = " ".join("%.1f %.1f" % (cx + (x - cx) * kx, cy + (y - cy) * ky) for x, y in pts)
    return m.group(1) + out + ")"


d = re.sub(r"(\(boundary\s*\(path pcb 0\s+)([-\d\.\s]+)\)", shrink, d, count=1)
open(DSN, "w").write(d)
print("planes left:", d.count("(plane"), "| keepouts:", d.count("keepout"), "| nets:", d.count("(net "))
print("classes:", [l.strip()[:100] for l in d.splitlines() if l.strip().startswith("(class")])
print("exported", DSN)
