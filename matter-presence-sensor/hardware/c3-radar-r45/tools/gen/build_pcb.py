import sys, os, json, math
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


def build():
    for f in (PCB, PRO, PCB.replace(".kicad_pcb", ".kicad_prl")):
        if os.path.exists(f):
            os.remove(f)
    board = pcbnew.NewBoard(PCB)

    # ---------------- outline: circle D = 30 mm ----------------
    c = pcbnew.PCB_SHAPE(board)
    c.SetShape(pcbnew.SHAPE_T_CIRCLE)
    c.SetLayer(pcbnew.Edge_Cuts)
    c.SetWidth(MM(0.05))
    c.SetStart(V(0, 0))
    c.SetEnd(V(BOARD_R, 0))
    board.Add(c)

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    for n in POWER_NETS:
        net(n)

    # ---------------- footprints ----------------
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
        # keep silkscreen clean on a 30 mm board: references live on the Fab layers
        fp.Reference().SetVisible(p["ref"] in ("RD1",))
        fp.Value().SetVisible(False)
        if p["ref"] == "J1":
            orient_usb(fp)
    for ref, u, v in HOLES:   # board-only M2 mounting holes
        fp = pcbnew.FootprintLoad(lib_path("MountingHole"), "MountingHole_2.2mm_M2")
        fp.SetFPID(pcbnew.LIB_ID("MountingHole", "MountingHole_2.2mm_M2"))
        fp.SetReference(ref); fp.SetValue("M2")
        fp.SetBoardOnly(True); fp.SetExcludedFromBOM(True); fp.SetExcludedFromPosFiles(True)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
        board.Add(fp); fp.SetPosition(V(u, v))
    return board, nets


def orient_usb(fp):
    """rotate J1 (on the bottom side) until the receptacle mouth points to -u (left)."""
    for _ in range(4):
        sig = [pd for pd in fp.Pads() if pd.GetNumber() in ("A5", "B5", "A9", "B9", "A12", "B12")]
        sh = [pd for pd in fp.Pads() if pd.GetNumber() == "S1"]
        sx = sum(pd.GetPosition().x for pd in sig) / len(sig)
        sy = sum(pd.GetPosition().y for pd in sig) / len(sig)
        hx = sum(pd.GetPosition().x for pd in sh) / len(sh)
        hy = sum(pd.GetPosition().y for pd in sh) / len(sh)
        dx, dy = hx - sx, hy - sy
        if dy > 0 and abs(dy) > abs(dx):   # mouth towards the bottom edge (KiCad +y)
            return
        fp.SetOrientationDegrees(fp.GetOrientationDegrees() + 90)
    raise RuntimeError("could not orient J1")


def add_zones(board, gnd):
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        ol = z.Outline()
        ol.NewOutline()
        for i in range(120):
            a = 2 * math.pi * i / 120
            x, y = kc((BOARD_R - 0.1) * math.cos(a), (BOARD_R - 0.1) * math.sin(a))
            ol.Append(MM(x), MM(y))
        z.SetLocalClearance(MM(0.2))
        z.SetMinThickness(MM(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetThermalReliefGap(MM(0.25))
        z.SetThermalReliefSpokeWidth(MM(0.3))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetZoneName("GND_" + ("TOP" if layer == pcbnew.F_Cu else "BOT"))
        board.Add(z)
    # board-level antenna keep-out (duplicates the module rule so the autorouter sees it)
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True)
    k.SetDoNotAllowTracks(True)
    k.SetDoNotAllowVias(True)
    k.SetDoNotAllowCopperPour(True)
    k.SetDoNotAllowPads(False)
    k.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    k.SetLayerSet(ls)
    k.SetZoneName("ANTENNA_KEEPOUT")
    ol = k.Outline()
    ol.NewOutline()
    for (u, v) in [(-23.5, ANT_V), (23.5, ANT_V), (23.5, 24), (-23.5, 24)]:
        x, y = kc(u, v)
        ol.Append(MM(x), MM(y))
    board.Add(k)
    # no top-layer tracks under the ESP32 module body: keeps a solid GND under U1
    m = pcbnew.ZONE(board)
    m.SetIsRuleArea(True)
    m.SetDoNotAllowTracks(True)
    m.SetDoNotAllowVias(False)
    m.SetDoNotAllowCopperPour(False)
    m.SetDoNotAllowPads(False)
    m.SetDoNotAllowFootprints(False)
    m.SetLayer(pcbnew.F_Cu)
    m.SetZoneName("NO_TOP_TRACKS_UNDER_U1")
    ol = m.Outline()
    ol.NewOutline()
    for (u, v) in [(-3.4, 3.6), (4.6, 3.6), (4.6, 11.4), (-3.4, 11.4)]:
        x, y = kc(u, v)
        ol.Append(MM(x), MM(y))
    board.Add(m)


def text(board, s, u, v, layer, size=0.7, thick=0.12, mirror=False, angle=0):
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
    text(board, "ESP32-C3 + LD2410C  v2.0", 0, -19.6, F, 0.8, 0.13)
    for p in PARTS:   # programming pad labels, above each pad (bottom side)
        if p["ref"].startswith("TP"):
            text(board, p["value"], p["u"], 12.35, B, 0.5, 0.1, mirror=True)
    text(board, "RST", 13.0, 7.2, B, 0.6, 0.1, mirror=True)
    text(board, "5V IN", -6.8, -13.4, B, 0.6, 0.1, mirror=True)
    text(board, "c3-radar r45 v2.0", 0, -8.0, B, 0.7, 0.12, mirror=True)


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
    ds["track_widths"] = [0.0, 0.2, 0.3, 0.4, 0.5]
    ds["via_dimensions"] = [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}]
    sev = ds.setdefault("rule_severities", {})
    sev["silk_over_copper"] = "ignore"     # tiny board: Fab layers carry the references
    sev["silk_overlap"] = "ignore"
    sev["lib_footprint_issues"] = "ignore"
    sev["lib_footprint_mismatch"] = "ignore"
    sev["silk_edge_clearance"] = "ignore"   # U1 corners touch the round edge by design (antenna at the edge)
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
