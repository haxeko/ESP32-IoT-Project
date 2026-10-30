#!/usr/bin/env python3
"""Create hardware/<project>.kicad_pcb from design.py + layout.py (unrouted).

Footprints come from the KiCad 9 libraries (and the project .pretty), nets from
design.py, and each footprint is linked to its schematic symbol UUID, so
"Update PCB from Schematic" and DRC schematic parity find no differences.
Routing is done afterwards by route.py.
"""
import math
import os
import subprocess
import sys
import tempfile

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
import layout  # noqa: E402
from build_sch import uid  # noqa: E402
from sexp import find, find_all, parse  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, "..", "hardware"))
FP_DIR = os.environ.get("KICAD9_FOOTPRINT_DIR", "/usr/share/kicad/footprints")
MM = pcbnew.FromMM
TOMM = pcbnew.ToMM

NETCLASSES = {
    # name: (track, clearance, via_d, via_drill, nets)
    "Power": (1.5, 0.3, 0.8, 0.4, ["/VIN", "/VLED", "/VOUT1", "/VOUT2", "/VOUT3"]),
    "Supply": (0.5, 0.2, 0.8, 0.4, ["+3V3", "/V_SYS", "/VBUS", "+5V", "/SW_NODE", "GND"]),
}


LAYERS = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu, "F.SilkS": pcbnew.F_SilkS,
          "B.SilkS": pcbnew.B_SilkS, "F.Fab": pcbnew.F_Fab}


def pt(x, y):
    return pcbnew.VECTOR2I(MM(layout.ORIGIN[0] + x), MM(layout.ORIGIN[1] + y))


def load_fp(fpid):
    lib, name = fpid.split(":", 1)
    if lib == design.LOCAL_FP_LIB:
        path = os.path.join(HW, lib + ".pretty")
    else:
        path = os.path.join(FP_DIR, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def setup_rules(board):
    ds = board.GetDesignSettings()
    ds.m_MinClearance = MM(0.2)
    ds.m_TrackMinWidth = MM(0.2)
    ds.m_ViasMinSize = MM(0.5)
    ds.m_ViasMinAnnularWidth = MM(0.1)
    ds.m_MinThroughDrill = MM(0.3)
    ds.m_HoleClearance = MM(0.25)
    ds.m_HoleToHoleMin = MM(0.25)
    ds.m_CopperEdgeClearance = MM(0.4)
    ds.m_SolderMaskMinWidth = MM(0.0)
    ns = ds.m_NetSettings
    d = ns.GetDefaultNetclass()
    d.SetTrackWidth(MM(0.25))
    d.SetClearance(MM(0.2))
    d.SetViaDiameter(MM(0.6))
    d.SetViaDrill(MM(0.3))
    for name, (tw, cl, vd, vdr, nets) in NETCLASSES.items():
        nc = pcbnew.NETCLASS(name)
        nc.SetTrackWidth(MM(tw))
        nc.SetClearance(MM(cl))
        nc.SetViaDiameter(MM(vd))
        nc.SetViaDrill(MM(vdr))
        ns.SetNetclass(name, nc)
        for n in nets:
            ns.SetNetclassPatternAssignment(n, name)


def sch_netlist():
    """(ref, pin) -> net name exactly as the schematic names it (labels get a '/' prefix,
    no-connect pins get 'unconnected-(...)')."""
    sch = os.path.join(HW, design.PROJECT + ".kicad_sch")
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "n.net")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, sch],
                       check=True, capture_output=True)
        tree = parse(open(out, encoding="utf-8").read())
    m = {}
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1]
        for node in find_all(n, "node"):
            m[(find(node, "ref")[1], find(node, "pin")[1])] = name
    return m


def full_name(net):
    return net if net in design.POWER_NETS else "/" + net


def all_conns(part):
    conns = dict(part["pins"])
    for _u, (_at, _rot, c) in part["extra_units"].items():
        conns.update(c)
    return conns


def add_outline(board):
    w, h, r = layout.W, layout.H, layout.CORNER_R
    segs = [((r, 0), (w - r, 0)), ((w, r), (w, h - r)), ((w - r, h), (r, h)), ((0, h - r), (0, r))]
    for (x1, y1), (x2, y2) in segs:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x1, y1))
        s.SetEnd(pt(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)
    k = r * (1 - math.sqrt(0.5))
    corners = (((0, r), (k, k), (r, 0)), ((w - r, 0), (w - k, k), (w, r)),
               ((w, h - r), (w - k, h - k), (w - r, h)), ((r, h), (k, h - k), (0, h - r)))
    for (sx, sy), (mx, my), (ex, ey) in corners:
        a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        a.SetArcGeometry(pt(sx, sy), pt(mx, my), pt(ex, ey))
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetWidth(MM(0.1))
        board.Add(a)


def add_zone(board, net, layer, priority, rects, solid=True, name=""):
    for x1, y1, x2, y2 in rects:
        z = pcbnew.ZONE(board)
        z.SetLayer(LAYERS[layer])
        z.SetNet(board.FindNet(full_name(net)))
        z.SetAssignedPriority(priority)
        z.SetZoneName(name or net)
        poly = z.Outline()
        poly.NewOutline()
        for x, y in ((x1, y1), (x2, y1), (x2, y2), (x1, y2)):
            poly.Append(pt(x, y).x, pt(x, y).y)
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid else pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.4))
        z.SetThermalReliefSpokeWidth(MM(0.6))
        z.SetMinThickness(MM(0.25))
        z.SetLocalClearance(MM(0.3))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        board.Add(z)


def add_text(board, text, x, y, size, angle, layer):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pt(x, y))
    t.SetLayer(LAYERS[layer])
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(max(0.12, size * 0.15)))
    t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    if layer.startswith("B."):
        t.SetMirrored(True)
    board.Add(t)


def _bb(item, grow=0.0):
    r = item.GetBoundingBox()
    return (TOMM(r.GetLeft()) - grow, TOMM(r.GetTop()) - grow, TOMM(r.GetRight()) + grow, TOMM(r.GetBottom()) + grow)


def _hit(a, rects):
    return any(a[0] < r[2] and a[2] > r[0] and a[1] < r[3] and a[3] > r[1] for r in rects)


HIDE_REFS = {"J1", "J2", "J3", "J4", "H1", "H2", "H3", "H4", "SW2", "J6"}


def place_refs(board):
    """Put every reference designator where it does not touch pads, other silk or the edge."""
    obst = {pcbnew.F_SilkS: [], pcbnew.B_SilkS: []}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            for silk, cu in ((pcbnew.F_SilkS, pcbnew.F_Cu), (pcbnew.B_SilkS, pcbnew.B_Cu)):
                if pad.IsOnLayer(cu) or pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    obst[silk].append(_bb(pad, 0.2))
        for g in fp.GraphicalItems():
            if g.GetLayer() in obst:
                obst[g.GetLayer()].append(_bb(g, 0.15))
    for d in board.GetDrawings():
        if d.GetLayer() in obst:
            obst[d.GetLayer()].append(_bb(d, 0.15))
    x0, y0 = layout.ORIGIN
    inside = (x0 + 0.5, y0 + 0.5, x0 + layout.W - 0.5, y0 + layout.H - 0.5)
    fps = sorted(board.GetFootprints(), key=lambda f: -TOMM(f.GetBoundingBox().GetWidth()))
    for fp in fps:
        ref = fp.Reference()
        if fp.GetReference() in HIDE_REFS:
            ref.SetVisible(False)
            continue
        layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        ref.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
        ref.SetTextThickness(MM(0.12))
        ref.SetKeepUpright(True)
        crt = fp.GetCourtyard(pcbnew.F_CrtYd if not fp.IsFlipped() else pcbnew.B_CrtYd).BBox()
        l, t, r, bt = TOMM(crt.GetLeft()), TOMM(crt.GetTop()), TOMM(crt.GetRight()), TOMM(crt.GetBottom())
        cx, cy = (l + r) / 2, (t + bt) / 2
        cands = [(cx, t - 0.55, 0), (cx, bt + 0.55, 0), (l - 0.3, cy, 0), (r + 0.3, cy, 0),
                 (l - 0.55, cy, 90), (r + 0.55, cy, 90), (cx, t - 1.4, 0), (cx, bt + 1.4, 0)]
        placed = False
        for x, y, ang in cands:
            ref.SetTextAngle(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T))
            ref.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            box = _bb(ref, 0.05)
            # left/right candidates: shift so the text sits beside the courtyard
            if ang == 0 and x < l:
                ref.SetPosition(pcbnew.VECTOR2I(MM(x - (box[2] - box[0]) / 2), MM(y)))
            elif ang == 0 and x > r:
                ref.SetPosition(pcbnew.VECTOR2I(MM(x + (box[2] - box[0]) / 2), MM(y)))
            box = _bb(ref, 0.05)
            if box[0] < inside[0] or box[1] < inside[1] or box[2] > inside[2] or box[3] > inside[3]:
                continue
            if _hit(box, obst[layer]):
                continue
            obst[layer].append(box)
            placed = True
            break
        if not placed:
            ref.SetVisible(False)
            print("   reference hidden (no room):", fp.GetReference())


def main():
    out = os.path.join(HW, design.PROJECT + ".kicad_pcb")
    board = pcbnew.NewBoard(out)
    board.SetCopperLayerCount(2)
    setup_rules(board)
    # placement / drill origin at the lower left board corner (positive X/Y in the CPL file)
    board.GetDesignSettings().SetAuxOrigin(pt(0, layout.H))

    tb = board.GetTitleBlock()
    tb.SetTitle(design.TITLE)
    tb.SetRevision(design.REV)
    tb.SetDate(design.DATE)
    tb.SetCompany(design.COMPANY)

    pin_net = sch_netlist()
    nets = {}
    for name in sorted(set(pin_net.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni

    sch_file = design.PROJECT + ".kicad_sch"
    for part in design.PARTS:
        ref = part["ref"]
        fp = load_fp(part["footprint"])
        fp.SetReference(ref)
        fp.SetValue(part["value"])
        x, y, rot = layout.PLACE[ref]
        board.Add(fp)
        fp.SetPosition(pt(x, y))
        if ref in layout.BOTTOM:
            fp.Flip(pt(x, y), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        fp.SetOrientationDegrees(rot)
        fp.SetPath(pcbnew.KIID_PATH("/" + uid("sym", ref, part["unit"])))
        fp.SetSheetname("/")
        fp.SetSheetfile(sch_file)
        for k, v in (("MPN", part["mpn"]), ("LCSC", part["lcsc"])):
            if v:
                f = pcbnew.PCB_FIELD(fp, fp.GetNextFieldId(), k)
                f.SetText(v)
                f.SetVisible(False)
                f.SetLayer(pcbnew.F_Fab)
                fp.AddField(f)
        if not part.get("in_bom", True):
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM)
        if part["dnp"]:
            fp.SetDNP(True)
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_EXCLUDE_FROM_BOM)
        conns = all_conns(part)
        for pad in fp.Pads():
            name = pin_net.get((ref, pad.GetNumber()))
            if name:
                want = conns.get(pad.GetNumber())
                assert want is None or name == full_name(want), (ref, pad.GetNumber(), name, want)
                pad.SetNet(nets[name])

        # high current pins connect solidly to the planes
        if ref.startswith(("J1", "J2", "J3", "J4", "F")) or ref == "Q1":
            for pad in fp.Pads():
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        # the stacked USB-C ground pads are too narrow for two thermal spokes
        if ref == "J5":
            for pad in fp.Pads():
                if pad.GetNetname() == "GND":
                    pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

    add_outline(board)
    for net, layer, prio, rects in layout.POWER_ZONES:
        # overlapping pieces of one net need distinct priorities
        for k, rect in enumerate(rects):
            add_zone(board, net, layer, prio + k, [rect], solid=True)
    full = [(0.0, 0.0, layout.W, layout.H)]
    add_zone(board, "GND", "B.Cu", 1, full, solid=False, name="GND_B")
    add_zone(board, "GND", "F.Cu", 0, full, solid=False, name="GND_F")
    for args in layout.SILK:
        add_text(board, *args)

    place_refs(board)
    board.BuildConnectivity()
    pcbnew.SaveBoard(out, board)
    print("wrote", out, "footprints:", len(board.GetFootprints()))


if __name__ == "__main__":
    main()
