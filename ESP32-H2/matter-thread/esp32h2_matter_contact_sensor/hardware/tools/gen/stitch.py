# GND stitching vias between the two pours: a row along the antenna keep-out (Espressif: dense GND vias near
# the antenna), around the board edge and under the module, then wherever both pours overlap.
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import ANT_V, BOARD_H, BOARD_W, PROJ_DIR, PROJECT, kc

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
MM = pcbnew.FromMM


def stitch(board, spacing=2.4, max_vias=70):
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    gnd = board.GetNetsByName()["GND"]
    zones = {z.GetLayer(): z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == "GND"}
    polys = {L: zones[L].GetFilledPolysList(L) for L in (pcbnew.F_Cu, pcbnew.B_Cu)}
    existing = [(pcbnew.ToMM(v.GetPosition().x), pcbnew.ToMM(v.GetPosition().y))
                for v in board.GetTracks() if v.GetClass() == "PCB_VIA"]
    pads = [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in board.GetPads()
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]
    ring = [(0.6 * math.cos(a * math.pi / 4), 0.6 * math.sin(a * math.pi / 4)) for a in range(8)] + [(0, 0)]
    w, h = BOARD_W / 2, BOARD_H / 2

    def ok(u, v):
        if v > ANT_V - 0.6 or abs(u) > w - 1.0 or abs(v) > h - 1.0:
            return False
        for (du, dv) in ring:
            p = pcbnew.VECTOR2I_MM(*kc(u + du, v + dv))
            for L in polys:
                if not polys[L].Contains(p):
                    return False
        return True

    cands = []
    for i in range(-int(w * 2), int(w * 2) + 1):
        for j in range(-int(h * 2), int(h * 2) + 1):
            u, v = i * 0.5, j * 0.5
            if ok(u, v):
                edge = min(w - abs(u), h - abs(v))
                score = -2.0 * abs(v - (ANT_V - 1.0)) if v > ANT_V - 3 else -min(edge, 4)
                cands.append((score, u, v))
    cands.sort(reverse=True)
    placed = []
    for s, u, v in cands:
        x, y = kc(u, v)
        if any(math.hypot(u - a, v - b) < spacing for a, b in placed):
            continue
        if any(math.hypot(x - ex, y - ey) < 1.2 for ex, ey in existing):
            continue
        if any(math.hypot(x - px, y - py) < 1.5 for px, py in pads):
            continue
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pcbnew.VECTOR2I_MM(x, y))
        via.SetWidth(MM(0.6))
        via.SetDrill(MM(0.3))
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNet(gnd)
        board.Add(via)
        placed.append((u, v))
        if len(placed) >= max_vias:
            break
    filler.Fill(board.Zones())
    return placed


def connect_islands(board):
    """A top-pour fragment that is not the main one and has no via (e.g. around a GND pin boxed in by tracks)
    gets a via inside it, joining it to the bottom pour."""
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    gnd = board.GetNetsByName()["GND"].GetNetCode()
    zones = {z.GetLayer(): z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd}
    top = zones[pcbnew.F_Cu].GetFilledPolysList(pcbnew.F_Cu)
    bot = zones[pcbnew.B_Cu].GetFilledPolysList(pcbnew.B_Cu)
    vias = [t.GetPosition() for t in board.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetCode() == gnd]
    tht = [p.GetPosition() for p in board.GetPads() if p.GetNetCode() == gnd and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    sizes = [top.Outline(i).Area() for i in range(top.OutlineCount())]
    main = sizes.index(max(sizes))
    added = []
    ring = [(0.32 * math.cos(a * math.pi / 6), 0.32 * math.sin(a * math.pi / 6)) for a in range(12)] + [(0, 0)]
    for i in range(top.OutlineCount()):
        ol = top.Outline(i)
        if i == main or any(ol.PointInside(v) for v in vias + tht):
            continue
        bb = ol.BBox()
        best = None
        x0, y0, x1, y1 = (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetRight()),
                          pcbnew.ToMM(bb.GetBottom()))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        for ix in range(int((x1 - x0) / 0.05) + 1):
            for iy in range(int((y1 - y0) / 0.05) + 1):
                x, y = x0 + ix * 0.05, y0 + iy * 0.05
                pts = [pcbnew.VECTOR2I_MM(x + dx, y + dy) for dx, dy in ring]
                if all(ol.PointInside(q) for q in pts) and all(bot.Contains(q) for q in pts):
                    d = math.hypot(x - cx, y - cy)
                    if best is None or d < best[0]:
                        best = (d, x, y)
        if best is None:
            raise RuntimeError("GND island at %s without room for a via" % ((cx - 100, 100 - cy),))
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I_MM(best[1], best[2]))
        v.SetWidth(MM(0.6))
        v.SetDrill(MM(0.3))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(board.GetNetsByName()["GND"])
        board.Add(v)
        added.append((round(best[1] - 100, 2), round(100 - best[2], 2)))
    filler.Fill(board.Zones())
    return added


if __name__ == "__main__":
    b = pcbnew.LoadBoard(PCB)
    print("stitching vias:", len(stitch(b)))
    print("vias into isolated top GND islands at (u, v):", connect_islands(b))
    pcbnew.SaveBoard(PCB, b)
