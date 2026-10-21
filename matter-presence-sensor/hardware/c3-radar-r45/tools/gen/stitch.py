import pcbnew, math, sys
from design import kc, BOARD_R, ANT_V
MM = pcbnew.FromMM

def stitch(board, spacing=3.0, max_vias=40):
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    gnd = board.GetNetsByName()["GND"]
    zones = {z.GetLayer(): z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == "GND"}
    polys = {L: zones[L].GetFilledPolysList(L) for L in (pcbnew.F_Cu, pcbnew.B_Cu)}
    existing = [v.GetPosition() for v in board.GetTracks() if v.GetClass() == "PCB_VIA"]
    ring = [(0.55 * math.cos(a * math.pi / 4), 0.55 * math.sin(a * math.pi / 4)) for a in range(8)] + [(0, 0)]

    def ok(u, v):
        if v > ANT_V - 0.4 or math.hypot(u, v) > BOARD_R - 1.0:
            return False
        for (du, dv) in ring:
            x, y = kc(u + du, v + dv)
            p = pcbnew.VECTOR2I_MM(x, y)
            for L in polys:
                if not polys[L].Contains(p):
                    return False
        return True

    cands = []
    for i in range(-45, 46):
        for j in range(-45, 46):
            u, v = i * 0.5, j * 0.5
            if ok(u, v):
                r = math.hypot(u, v)
                # prefer the board rim and the ESP module flanks
                score = -abs(r - (BOARD_R - 1.6)) - 0.6 * min(abs(abs(u) - 7.3), 3)
                cands.append((score, u, v))
    cands.sort(reverse=True)
    placed = []
    for s, u, v in cands:
        x, y = kc(u, v)
        P = pcbnew.VECTOR2I_MM(x, y)
        if any(math.hypot(u - a, v - b) < spacing for a, b in placed):
            continue
        if any(math.hypot(pcbnew.ToMM(P.x - e.x), pcbnew.ToMM(P.y - e.y)) < 1.2 for e in existing):
            continue
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(P); via.SetWidth(MM(0.6)); via.SetDrill(MM(0.3))
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); via.SetNet(gnd)
        board.Add(via)
        placed.append((u, v))
        if len(placed) >= max_vias:
            break
    filler.Fill(board.Zones())
    return placed
