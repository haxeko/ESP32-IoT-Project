import pcbnew, sys, re

PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"


def sexp(text):
    tok = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    stack = [[]]
    for t in tok:
        if t == "(":
            stack.append([])
        elif t == ")":
            x = stack.pop()
            stack[-1].append(x)
        else:
            stack[-1].append(t.strip('"'))
    return stack[0][0]


def find(node, name):
    for c in node:
        if isinstance(c, list) and c and c[0] == name:
            yield c


def import_ses(board, path):
    root = sexp(open(path).read())
    routes = next(find(root, "routes"))
    res = next(find(routes, "resolution"))
    scale = 1000.0 / float(res[2])          # SES units (um/res) -> nm
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    nets = board.GetNetsByName()
    n_tr = n_via = 0
    for net in find(next(find(routes, "network_out")), "net"):
        ni = nets[net[1]]
        for w in find(net, "wire"):
            p = next(find(w, "path"))
            layer, width = layers[p[1]], int(float(p[2]) * scale)
            pts = [(int(float(p[i]) * scale), int(-float(p[i + 1]) * scale)) for i in range(3, len(p), 2)]
            for a, b in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(*a)); t.SetEnd(pcbnew.VECTOR2I(*b))
                t.SetWidth(width); t.SetLayer(layer); t.SetNet(ni)
                board.Add(t); n_tr += 1
        for v in find(net, "via"):
            m = re.search(r"_(\d+):(\d+)_um", v[1])
            dia, drill = (int(m.group(1)), int(m.group(2))) if m else (600, 300)
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(int(float(v[2]) * scale), int(-float(v[3]) * scale)))
            via.SetWidth(dia * 1000); via.SetDrill(drill * 1000)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); via.SetNet(ni)
            board.Add(via); n_via += 1
    return n_tr, n_via


if __name__ == "__main__":
    b = pcbnew.LoadBoard(PCB)
    for t in list(b.GetTracks()):
        b.Remove(t)
    print("imported tracks/vias:", import_ses(b, sys.argv[1] if len(sys.argv) > 1 else "/tmp/route/board.ses"))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
