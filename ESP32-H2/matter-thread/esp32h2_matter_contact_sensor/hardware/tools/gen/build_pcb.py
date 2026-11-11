# Places every footprint of design.py on a new board, adds the outline, GND pours, keep-outs, silkscreen and the
# design rules. Routing is done afterwards (preroute.py, export_dsn.py + Freerouting, import_ses.py).
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from design import *

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
PRO = f"{PROJ_DIR}/{PROJECT}.kicad_pro"
MM = pcbnew.FromMM


def lib_path(lib):
    return LOCAL_FP if lib == PROJECT else f"{STD_FP}/{lib}.pretty"


def V(u, v):
    x, y = kc(u, v)
    return pcbnew.VECTOR2I_MM(x, y)


def outline_points(inset=0.0, steps=8):
    """rounded rectangle, counter-clockwise, as (u, v) points"""
    w, h, r = BOARD_W / 2 - inset, BOARD_H / 2 - inset, max(CORNER_R - inset, 0.1)
    pts = []
    for cu, cv, a0 in ((w - r, h - r, 0), (-w + r, h - r, 90), (-w + r, -h + r, 180), (w - r, -h + r, 270)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cu + r * math.cos(a), cv + r * math.sin(a)))
    return pts


def add_outline(board):
    w, h, r = BOARD_W / 2, BOARD_H / 2, CORNER_R

    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.05))
        s.SetStart(V(*a))
        s.SetEnd(V(*b))
        board.Add(s)

    def arc(c, a, b):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.05))
        s.SetCenter(V(*c))
        s.SetStart(V(*a))
        s.SetEnd(V(*b))
        board.Add(s)

    seg((-w + r, h), (w - r, h))
    seg((w, h - r), (w, -h + r))
    seg((w - r, -h), (-w + r, -h))
    seg((-w, -h + r), (-w, h - r))
    # arcs drawn clockwise on screen (KiCad y down): start -> end
    arc((w - r, h - r), (w - r, h), (w, h - r))
    arc((w - r, -h + r), (w, -h + r), (w - r, -h))
    arc((-w + r, -h + r), (-w + r, -h), (-w, -h + r))
    arc((-w + r, h - r), (-w, h - r), (-w + r, h))


def build():
    for f in (PCB, PRO, PCB.replace(".kicad_pcb", ".kicad_prl")):
        if os.path.exists(f):
            os.remove(f)
    board = pcbnew.NewBoard(PCB)
    add_outline(board)

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    for n in POWER_NETS:
        net(n)

    for p in PARTS:
        lib, name = p["fp"].split(":")
        fp = pcbnew.FootprintLoad(lib_path(lib), name)
        assert fp is not None, p["fp"]
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(p["ref"])
        fp.SetValue(p["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + p["uuid"]))
        fp.SetProperty("Sheetfile", PROJECT + ".kicad_sch")
        fp.SetProperty("Sheetname", "")
        board.Add(fp)
        if p["side"] == "B":
            fp.Flip(fp.GetPosition(), False)
        fp.SetOrientationDegrees(p["rot"] or 0)
        fp.SetPosition(V(p["u"], p["v"]))
        for pad in fp.Pads():
            num = pad.GetNumber()
            if num in p["pads"]:
                pad.SetNet(net(p["pads"][num]))
        # small board: references on the Fab layers, a few labels on the silkscreen (add_silk)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        if p["ref"].startswith("TP"):
            fp.SetExcludedFromBOM(True)
            fp.SetExcludedFromPosFiles(True)
    for ref, u, v in HOLES:
        fp = pcbnew.FootprintLoad(lib_path("MountingHole"), "MountingHole_2.2mm_M2")
        fp.SetFPID(pcbnew.LIB_ID("MountingHole", "MountingHole_2.2mm_M2"))
        fp.SetReference(ref)
        fp.SetValue("M2")
        fp.SetBoardOnly(True)
        fp.SetExcludedFromBOM(True)
        fp.SetExcludedFromPosFiles(True)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        board.Add(fp)
        fp.SetPosition(V(u, v))
    return board, nets


def rule_area(board, name, pts, layers, tracks=True, vias=True, pour=True):
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True)
    k.SetDoNotAllowTracks(tracks)
    k.SetDoNotAllowVias(vias)
    k.SetDoNotAllowCopperPour(pour)
    k.SetDoNotAllowPads(False)
    k.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    k.SetLayerSet(ls)
    k.SetZoneName(name)
    ol = k.Outline()
    ol.NewOutline()
    for (u, v) in pts:
        x, y = kc(u, v)
        ol.Append(MM(x), MM(y))
    board.Add(k)


def add_zones(board, gnd):
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        ol = z.Outline()
        ol.NewOutline()
        for (u, v) in outline_points(0.1):
            x, y = kc(u, v)
            ol.Append(MM(x), MM(y))
        z.SetLocalClearance(MM(0.25))
        z.SetMinThickness(MM(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetThermalReliefGap(MM(0.25))
        z.SetThermalReliefSpokeWidth(MM(0.3))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetZoneName("GND_" + ("TOP" if layer == pcbnew.F_Cu else "BOT"))
        board.Add(z)
    w, h = BOARD_W / 2 + 1, BOARD_H / 2 + 1
    both = (pcbnew.F_Cu, pcbnew.B_Cu)
    # module antenna: no copper, tracks or vias on either layer over the whole board width
    rule_area(board, "ANTENNA_KEEPOUT", [(-w, ANT_V), (w, ANT_V), (w, h), (-w, h)], both)
    # no top-layer tracks between the module pad rows: solid GND under the module (vias allowed)
    rule_area(board, "NO_TOP_TRACKS_UNDER_U1", [(-5.3, U1_V - 7.0), (5.3, U1_V - 7.0), (5.3, U1_V + 1.6),
                                                (-5.3, U1_V + 1.6)], (pcbnew.F_Cu,), vias=False, pour=False)


def text(board, s, u, v, layer, size=0.8, thick=0.15, mirror=False, angle=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(u, v))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I_MM(size, size))
    t.SetTextThickness(MM(thick))
    t.SetMirrored(mirror)
    if angle:
        t.SetTextAngleDegrees(angle)
    board.Add(t)


def add_silk(board):
    F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
    text(board, "RST", -8.6, 3.2, F)
    text(board, "BOOT", 8.6, 3.2, F)
    text(board, "LED", 1.1, 0.0, F, 0.7, 0.12)
    text(board, "REED", -10.6, -14.3, F, 0.7, 0.12)   # above the terminal body, which covers its pads
    text(board, "GND", -7.1, -14.3, F, 0.7, 0.12)
    text(board, "5V USB-C", 5.5, -16.0 + 2.6, F, 0.7, 0.12)
    text(board, "H2 CONTACT v1.0", 0, -3.8, F, 0.8, 0.15)
    text(board, "ESP32-H2 Matter/Thread contact sensor", 0, -1.5, B, 0.7, 0.12, mirror=True)
    for p in PARTS:   # test pad labels on the bottom side, under each pad
        if p["ref"].startswith("TP"):
            text(board, p["value"], p["u"], p["v"] - 1.8, B, 0.6, 0.12, mirror=True)


def write_project_rules():
    d = json.load(open(PRO))
    ds = d["board"]["design_settings"]
    r = ds.setdefault("rules", {})
    r.update({
        "min_clearance": 0.15,
        "min_track_width": 0.15,
        "min_via_diameter": 0.5,
        "min_via_annular_width": 0.1,
        "min_through_hole_diameter": 0.2,
        "min_hole_to_hole": 0.25,
        "min_hole_clearance": 0.2,
        "min_copper_edge_clearance": 0.3,
        "min_silk_clearance": 0.0,
        "min_text_height": 0.5,
        "min_text_thickness": 0.08,
        "solder_mask_to_copper_clearance": 0.0,
        "min_resolved_spokes": 1,
    })
    ds["track_widths"] = [0.0, 0.25, 0.3, 0.4, 0.5]
    ds["via_dimensions"] = [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}]
    sev = ds.setdefault("rule_severities", {})
    sev["silk_over_copper"] = "ignore"
    sev["silk_overlap"] = "ignore"
    sev["lib_footprint_issues"] = "ignore"
    sev["lib_footprint_mismatch"] = "ignore"
    sev["silk_edge_clearance"] = "ignore"   # module silkscreen ("Antenna Area") reaches the top edge by design
    base = d["net_settings"]["classes"][0]
    base.update({"clearance": 0.2, "track_width": 0.25, "via_diameter": 0.6, "via_drill": 0.3})
    pwr = dict(base)
    pwr.update({"name": "Power", "track_width": 0.4})
    d["net_settings"]["classes"] = [base, pwr]
    d["net_settings"]["netclass_patterns"] = [
        {"netclass": "Power", "pattern": n} for n in ("VBUS", "+5V", "+3V3", "GND")]
    json.dump(d, open(PRO, "w"), indent=2)


if __name__ == "__main__":
    board, nets = build()
    add_zones(board, nets["GND"])
    add_silk(board)
    pcbnew.SaveBoard(PCB, board)
    write_project_rules()
    print("saved", PCB)
