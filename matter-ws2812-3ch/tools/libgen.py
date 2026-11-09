"""Project-local footprints.

* ProgPads_1x06: six SMD pads on the bottom side for UART0 programming (pogo pins / wires).
* ESP32-C6-MINI-1_ThermalVias: KiCad's ESP32-C6-MINI-1 footprint with four tented GND
  vias in the gaps of the centre pad (ties pad 49 to the bottom ground plane). The module
  is mounted with its antenna sticking out over the board edge, so courtyard and silk are
  trimmed to the part that sits on the board, and the antenna keep-out (kept from the
  original) starts just outside the board edge.
"""
import os

from sexp import Sym, dump, find, find_all, parse

PROG_FP_NAME = "ProgPads_1x06_P2.54mm_D1.5mm_SMD"


def prog_pads():
    pads = []
    for k in range(6):
        x = (k - 2.5) * 2.54
        shape = "roundrect" if k == 0 else "circle"
        extra = " (roundrect_rratio 0.25)" if k == 0 else ""
        pads.append('\t(pad "%d" smd %s (at %.2f 0) (size 1.5 1.5) (layers "F.Cu" "F.Mask")%s '
                    '(uuid "00000000-0000-4000-8000-0000000004%02d"))' % (k + 1, shape, x, extra, k))
    labels = []
    for k, name in enumerate(("3V3", "EN", "GND", "IO9", "RX0", "TX0")):
        labels.append('\t(fp_text user "%s" (at %.2f -1.2 90) (layer "F.SilkS") '
                      '(uuid "00000000-0000-4000-8000-0000000005%02d") '
                      '(effects (font (size 0.8 0.8) (thickness 0.12)) (justify right)))' % (name, (k - 2.5) * 2.54, k))
    return """(footprint "%(name)s"
	(version 20241229)
	(generator "c6_ws2812_3ch_libgen")
	(generator_version "9.0")
	(layer "F.Cu")
	(descr "Six 1.5 mm SMD pads, pitch 2.54 mm, for pogo pins or tacked wires (UART0 programming)")
	(tags "programming pads pogo")
	(property "Reference" "REF**" (at 0 -1.8 0) (layer "F.SilkS") (uuid "00000000-0000-4000-8000-000000000011")
		(effects (font (size 0.8 0.8) (thickness 0.12))))
	(property "Value" "%(name)s" (at 0 2.5 0) (layer "F.Fab") (uuid "00000000-0000-4000-8000-000000000012")
		(effects (font (size 1 1) (thickness 0.15))))
	(property "Datasheet" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "00000000-0000-4000-8000-000000000013")
		(effects (font (size 1 1) (thickness 0.15))))
	(property "Description" "UART0 programming pads" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "00000000-0000-4000-8000-000000000014")
		(effects (font (size 1 1) (thickness 0.15))))
	(attr smd exclude_from_pos_files exclude_from_bom)
	(fp_rect (start -7.6 -1.1) (end 7.6 1.1) (stroke (width 0.05) (type solid)) (fill no) (layer "F.CrtYd") (uuid "00000000-0000-4000-8000-000000000015"))
%(labels)s
%(pads)s
	(embedded_fonts no)
)
""" % dict(name=PROG_FP_NAME, pads="\n".join(pads), labels="\n".join(labels))


ESP_FP_NAME = "ESP32-C6-MINI-1_ThermalVias"
# board edge sits at this local y (module placed with the antenna over the edge)
ESP_EDGE_Y = -5.75
ESP_VIA = 0.9875          # centre of the gaps between the 3x3 centre pad squares


def _uuid(k):
    return "00000000-0000-4000-8000-0000000006%02d" % k


def _line(x1, y1, x2, y2, layer, width, k):
    return [Sym("fp_line"), [Sym("start"), x1, y1], [Sym("end"), x2, y2],
            [Sym("stroke"), [Sym("width"), width], [Sym("type"), Sym("solid")]],
            [Sym("layer"), layer], [Sym("uuid"), _uuid(k)]]


def esp_module(kicad_fp_dir, project):
    src = os.path.join(kicad_fp_dir, "RF_Module.pretty", "ESP32-C6-MINI-1.kicad_mod")
    tree = parse(open(src, encoding="utf-8").read())
    assert tree[0] == "footprint"
    tree[1] = ESP_FP_NAME
    descr = find(tree, "descr")
    if descr:
        descr[1] = ("ESP32-C6-MINI-1 with 4 GND vias in the centre pad; antenna over the board edge "
                    "(edge at local y %.2f). " % ESP_EDGE_Y) + descr[1]

    def layer_of(node):
        ly = find(node, "layer")
        return ly[1] if ly else None

    keep = []
    for node in tree:
        if isinstance(node, list) and node and node[0] in ("fp_line", "fp_rect", "fp_poly", "fp_arc"):
            ly = layer_of(node)
            if ly == "F.CrtYd":
                continue                       # replaced below
            if ly == "F.SilkS" and node[0] == "fp_line":
                continue                       # outline crosses the board edge: replaced below
        if isinstance(node, list) and node and node[0] == "fp_text" and layer_of(node) == "Cmts.User":
            continue                           # "Keepout Area" / "Antenna" notes lie off the board
        keep.append(node)
    tree[:] = keep

    # trimmed silk outline (sides stop 0.75 mm inside the board edge) and body courtyard
    x, yb, yt = 6.75, 5.75, ESP_EDGE_Y + 0.75
    new = [
        _line(-x, yt, -x, yb, "F.SilkS", 0.12, 1),
        _line(-x, yb, x, yb, "F.SilkS", 0.12, 2),
        _line(x, yb, x, yt, "F.SilkS", 0.12, 3),
        [Sym("fp_rect"), [Sym("start"), -6.9, -11.3], [Sym("end"), 6.9, 5.9],
         [Sym("stroke"), [Sym("width"), 0.05], [Sym("type"), Sym("solid")]], [Sym("fill"), Sym("no")],
         [Sym("layer"), "F.CrtYd"], [Sym("uuid"), _uuid(4)]],
    ]
    # tented GND vias in the centre pad
    k = 10
    for vx in (-ESP_VIA, ESP_VIA):
        for vy in (-ESP_VIA, ESP_VIA):
            new.append([Sym("pad"), "49", Sym("thru_hole"), Sym("circle"), [Sym("at"), vx, vy],
                        [Sym("size"), 0.6, 0.6], [Sym("drill"), 0.3], [Sym("layers"), "*.Cu"],
                        [Sym("zone_connect"), 2], [Sym("uuid"), _uuid(k)]])
            k += 1
    # antenna keep-out: from just outside the board edge outwards
    for z in find_all(tree, "zone"):
        pts = find(find(z, "polygon"), "pts")
        for xy in find_all(pts, "xy"):
            if abs(float(xy[2]) - (-5.6)) < 1e-6:
                xy[2] = ESP_EDGE_Y - 0.05
    # insert before the first pad so the file keeps KiCad's usual order
    first_pad = next(i for i, n in enumerate(tree) if isinstance(n, list) and n and n[0] == "pad")
    tree[first_pad:first_pad] = new
    set_model(tree, project, "ESP32-C6-MINI-1")
    return dump(tree) + "\n"


# Local copies of KiCad footprints: new name -> (library, footprint, silk y limit or None,
# project 3D model or None). Edge-mounted parts get the silkscreen that would hang over the
# board edge cut back (local y limit, footprint frame); parts without a model in the KiCad
# 3D library get a simple box model (MODELS below) so the board renders look complete.
LOCAL_COPIES = {
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12_EdgeSilk":
        ("Connector_USB", "USB_C_Receptacle_HRO_TYPE-C-31-M-12", 3.15, "USB_C_HRO_TYPE-C-31-M-12"),
    "SW_SP3T_PCM13_EdgeSilk": ("Button_Switch_SMD", "SW_SP3T_PCM13", 2.0, None),
    "SW_Push_1P1T_XKB_TS-1187A": ("Button_Switch_SMD", "SW_Push_1P1T_XKB_TS-1187A", None, "SW_XKB_TS-1187A"),
    "Vishay_PowerPAK_1212-8_Single": ("Package_SO", "Vishay_PowerPAK_1212-8_Single", None, "PowerPAK_1212-8"),
}

# Box models: name -> [(x, y, z, size_x, size_y, size_z, (r, g, b))], mm, model frame
# (x right, y up = -y of the footprint, z up from the board), centre of each box.
METAL, BLACK, GREEN, DARK = (0.78, 0.78, 0.80), (0.08, 0.08, 0.08), (0.10, 0.30, 0.18), (0.25, 0.25, 0.28)
MODELS = {
    "ESP32-C6-MINI-1": [(0, 2.7, 0.4, 13.2, 16.6, 0.8, GREEN),          # module PCB, antenna end at y +11
                        (0, -0.05, 1.6, 12.4, 10.6, 1.6, METAL)],       # shield can
    "USB_C_HRO_TYPE-C-31-M-12": [(0, 0, 1.63, 8.94, 7.3, 3.26, METAL),
                                 (0, -3.66, 1.63, 7.6, 0.05, 2.2, BLACK)],
    "SW_XKB_TS-1187A": [(0, 0, 0.5, 5.1, 5.1, 1.0, BLACK), (0, 0, 1.1, 5.1, 5.1, 0.2, METAL),
                        (0, 0, 1.35, 2.4, 2.4, 0.3, DARK)],
    "PowerPAK_1212-8": [(0, 0, 0.5, 3.3, 3.3, 1.0, BLACK)],
}


def vrml(boxes):
    """VRML 2.0 for KiCad (1 unit = 0.1 inch)."""
    k = 1 / 2.54
    out = ["#VRML V2.0 utf8"]
    for x, y, z, sx, sy, sz, (r, g, b) in boxes:
        out.append("Transform { translation %.4f %.4f %.4f children [ Shape { appearance Appearance { "
                   "material Material { diffuseColor %.3f %.3f %.3f specularColor 0.2 0.2 0.2 shininess 0.3 } } "
                   "geometry Box { size %.4f %.4f %.4f } } ] }" % (x * k, y * k, z * k, r, g, b, sx * k, sy * k, sz * k))
    return "\n".join(out) + "\n"


def set_model(tree, project, model):
    for m in find_all(tree, "model"):
        tree.remove(m)
    tree.insert(len(tree), [Sym("model"), "${KIPRJMOD}/%s.3dshapes/%s.wrl" % (project, model),
                            [Sym("offset"), [Sym("xyz"), 0, 0, 0]], [Sym("scale"), [Sym("xyz"), 1, 1, 1]],
                            [Sym("rotate"), [Sym("xyz"), 0, 0, 0]]])


def local_copy(kicad_fp_dir, project, lib, name, new_name, ymax, model):
    src = os.path.join(kicad_fp_dir, lib + ".pretty", name + ".kicad_mod")
    tree = parse(open(src, encoding="utf-8").read())
    tree[1] = new_name
    if model:
        set_model(tree, project, model)
    if ymax is None:
        return dump(tree) + "\n"
    descr = find(tree, "descr")
    if descr:
        descr[1] = "%s with silkscreen trimmed at y = %.2f (board edge side). %s" % (name, ymax, descr[1])
    keep = []
    for node in tree:
        ly = find(node, "layer") if isinstance(node, list) else None
        if ly and ly[1] == "F.SilkS" and node[0] == "fp_line":
            s, e = find(node, "start"), find(node, "end")
            x1, y1, x2, y2 = float(s[1]), float(s[2]), float(e[1]), float(e[2])
            if y1 > ymax and y2 > ymax:
                continue
            if y1 > ymax or y2 > ymax:          # clip the end that is past the limit
                t = (ymax - y1) / (y2 - y1)
                xc = x1 + t * (x2 - x1)
                if y1 > ymax:
                    s[1], s[2] = xc, ymax
                else:
                    e[1], e[2] = xc, ymax
        elif ly and ly[1] == "F.SilkS" and node[0] in ("fp_poly", "fp_arc", "fp_circle"):
            continue
        keep.append(node)
    tree[:] = keep
    return dump(tree) + "\n"


def write(hw_dir, project, kicad_fp_dir="/usr/share/kicad/footprints"):
    sym_path = os.path.join(hw_dir, project + ".kicad_sym")
    with open(sym_path, "w", encoding="utf-8") as f:
        f.write('(kicad_symbol_lib\n\t(version 20241209)\n\t(generator "c6_ws2812_3ch_libgen")\n'
                '\t(generator_version "9.0")\n)\n')
    pretty = os.path.join(hw_dir, project + ".pretty")
    os.makedirs(pretty, exist_ok=True)
    for name in os.listdir(pretty):
        if name.endswith(".kicad_mod") and name[:-len(".kicad_mod")] not in \
                (PROG_FP_NAME, ESP_FP_NAME) + tuple(LOCAL_COPIES):
            os.remove(os.path.join(pretty, name))
    with open(os.path.join(pretty, PROG_FP_NAME + ".kicad_mod"), "w", encoding="utf-8") as f:
        f.write(prog_pads())
    with open(os.path.join(pretty, ESP_FP_NAME + ".kicad_mod"), "w", encoding="utf-8") as f:
        f.write(esp_module(kicad_fp_dir, project))
    for new_name, (lib, name, ymax, model) in LOCAL_COPIES.items():
        with open(os.path.join(pretty, new_name + ".kicad_mod"), "w", encoding="utf-8") as f:
            f.write(local_copy(kicad_fp_dir, project, lib, name, new_name, ymax, model))
    shapes = os.path.join(hw_dir, project + ".3dshapes")
    os.makedirs(shapes, exist_ok=True)
    for name, boxes in MODELS.items():
        with open(os.path.join(shapes, name + ".wrl"), "w", encoding="utf-8") as f:
            f.write(vrml(boxes))
    with open(os.path.join(hw_dir, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n\t(version 7)\n\t(lib (name "%s")(type "KiCad")(uri "${KIPRJMOD}/%s.kicad_sym")'
                '(options "")(descr "Project symbols"))\n)\n' % (project, project))
    with open(os.path.join(hw_dir, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n\t(version 7)\n\t(lib (name "%s")(type "KiCad")(uri "${KIPRJMOD}/%s.pretty")'
                '(options "")(descr "Project footprints"))\n)\n' % (project, project))
    return sym_path
