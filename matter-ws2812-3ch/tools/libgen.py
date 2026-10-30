"""Project-local symbol and footprint for the 3-position slide switch.

Same Sky SLW-156646-5A-N-D: SP3T, 4 PC pins in one row, 3.2 mm pitch,
body 15.6 x 6.0 mm (datasheet rev 1.01, 09/12/2024). The firmware scans all
four pins, so the board works whichever terminal a given SP3T switch uses as
its common.
"""
import os

SYM_NAME = "SW_Slide_SP3T"
FP_NAME = "SW_Slide_SP3T_SameSky_SLW-156646-5A-N-D"
DATASHEET = "https://www.sameskydevices.com/product/resource/slw-156646-5a-n-d.pdf"


def _font(hide=False):
    return "(effects (font (size 1.27 1.27))%s)" % (" (hide yes)" if hide else "")


def symbol_lib():
    pins = []
    # pin 1 on the left (pole of the Same Sky drawing), 2..4 on the right
    pins.append(("1", -7.62, 0, 0))
    for n, y in (("2", 2.54), ("3", 0), ("4", -2.54)):
        pins.append((n, 7.62, y, 180))
    pin_txt = "\n".join(
        '\t\t\t(pin passive line (at %s %s %d) (length 2.54) (name "~" %s) (number "%s" %s))'
        % (x, y, a, _font(), n, _font()) for n, x, y, a in pins)
    return """(kicad_symbol_lib
	(version 20241209)
	(generator "c3_ws2812_3ch_libgen")
	(generator_version "9.0")
	(symbol "%(name)s"
		(pin_names (offset 1.016) (hide yes))
		(exclude_from_sim no)
		(in_bom yes)
		(on_board yes)
		(property "Reference" "SW" (at 0 6.35 0) %(f)s)
		(property "Value" "%(name)s" (at 0 -6.35 0) %(f)s)
		(property "Footprint" "" (at 0 -8.89 0) %(fh)s)
		(property "Datasheet" "%(ds)s" (at 0 -11.43 0) %(fh)s)
		(property "Description" "3-position slide switch, SP3T, 4 pins in one row" (at 0 0 0) %(fh)s)
		(property "ki_keywords" "switch slide SP3T 3 position" (at 0 0 0) %(fh)s)
		(symbol "%(name)s_0_1"
			(rectangle (start -5.08 4.445) (end 5.08 -4.445) (stroke (width 0.254) (type default)) (fill (type background)))
			(polyline (pts (xy -5.08 0) (xy -2.794 0)) (stroke (width 0) (type default)) (fill (type none)))
			(circle (center -2.286 0) (radius 0.508) (stroke (width 0) (type default)) (fill (type none)))
			(polyline (pts (xy -1.905 0.381) (xy 1.524 2.286)) (stroke (width 0) (type default)) (fill (type none)))
			(circle (center 2.286 2.54) (radius 0.508) (stroke (width 0) (type default)) (fill (type none)))
			(circle (center 2.286 0) (radius 0.508) (stroke (width 0) (type default)) (fill (type none)))
			(circle (center 2.286 -2.54) (radius 0.508) (stroke (width 0) (type default)) (fill (type none)))
			(polyline (pts (xy 2.794 2.54) (xy 5.08 2.54)) (stroke (width 0) (type default)) (fill (type none)))
			(polyline (pts (xy 2.794 0) (xy 5.08 0)) (stroke (width 0) (type default)) (fill (type none)))
			(polyline (pts (xy 2.794 -2.54) (xy 5.08 -2.54)) (stroke (width 0) (type default)) (fill (type none)))
		)
		(symbol "%(name)s_1_1"
%(pins)s
		)
		(embedded_fonts no)
	)
)
""" % dict(name=SYM_NAME, f=_font(), fh=_font(True), ds=DATASHEET, pins=pin_txt)


def footprint():
    pitch = 3.2
    xs = [(-1.5 + i) * pitch for i in range(4)]
    pads = []
    for i, x in enumerate(xs):
        shape = "roundrect" if i == 0 else "circle"
        extra = " (roundrect_rratio 0.25)" if i == 0 else ""
        pads.append('\t(pad "%d" thru_hole %s (at %.2f 0) (size 1.9 1.9) (drill 1.1) '
                    '(layers "*.Cu" "*.Mask")%s (uuid "00000000-0000-4000-8000-0000000001%02d"))'
                    % (i + 1, shape, x, extra, i))
    bw, bh = 15.6 / 2, 6.0 / 2

    def rect(layer, w, h, width, uid):
        return ('\t(fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width %s) (type solid)) '
                '(fill no) (layer "%s") (uuid "00000000-0000-4000-8000-0000000002%02d"))'
                % (-w, -h, w, h, width, layer, uid))

    labels = []
    for i, x in enumerate((-pitch, 0.0, pitch)):
        labels.append('\t(fp_text user "%d" (at %.2f -4.3 0) (layer "F.SilkS") '
                      '(uuid "00000000-0000-4000-8000-0000000003%02d") '
                      '(effects (font (size 1 1) (thickness 0.15))))' % (i + 1, x, i))
    knob = ('\t(fp_rect (start -1.5 -1.55) (end 1.5 1.55) (stroke (width 0.1) (type solid)) '
            '(fill no) (layer "F.Fab") (uuid "00000000-0000-4000-8000-000000000210"))')
    return """(footprint "%(name)s"
	(version 20241229)
	(generator "c3_ws2812_3ch_libgen")
	(generator_version "9.0")
	(layer "F.Cu")
	(descr "Same Sky SLW-156646-5A-N-D slide switch, SP3T, 3 positions, 4 pins, pitch 3.2mm, %(ds)s")
	(tags "slide switch SP3T 3 position")
	(property "Reference" "REF**" (at 0 4.6 0) (layer "F.SilkS") (uuid "00000000-0000-4000-8000-000000000001")
		(effects (font (size 1 1) (thickness 0.15))))
	(property "Value" "%(name)s" (at 0 5.9 0) (layer "F.Fab") (uuid "00000000-0000-4000-8000-000000000002")
		(effects (font (size 1 1) (thickness 0.15))))
	(property "Datasheet" "%(ds)s" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "00000000-0000-4000-8000-000000000003")
		(effects (font (size 1 1) (thickness 0.15))))
	(property "Description" "3-position slide switch, SP3T" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "00000000-0000-4000-8000-000000000004")
		(effects (font (size 1 1) (thickness 0.15))))
	(attr through_hole)
%(silk)s
%(fab)s
%(knob)s
%(crt)s
%(labels)s
	(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (uuid "00000000-0000-4000-8000-000000000005")
		(effects (font (size 1 1) (thickness 0.15))))
%(pads)s
	(embedded_fonts no)
)
""" % dict(name=FP_NAME, ds=DATASHEET,
           silk=rect("F.SilkS", bw + 0.12, bh + 0.12, 0.12, 0),
           fab=rect("F.Fab", bw, bh, 0.1, 1),
           knob=knob,
           crt=rect("F.CrtYd", bw + 0.5, bh + 0.5, 0.05, 2),
           labels="\n".join(labels), pads="\n".join(pads))


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
	(generator "c3_ws2812_3ch_libgen")
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


ESP_FP_NAME = "ESP32-C3-WROOM-02_ThermalVia0.3"


def esp_module(kicad_fp_dir):
    """KiCad's ESP32-C3-WROOM-02 footprint with the 0.2 mm EPAD vias opened to 0.3 mm,
    the smallest hole of a standard 2-layer process."""
    src = os.path.join(kicad_fp_dir, "RF_Module.pretty", "ESP32-C3-WROOM-02.kicad_mod")
    text = open(src, encoding="utf-8").read()
    text = text.replace('(footprint "ESP32-C3-WROOM-02"', '(footprint "%s"' % ESP_FP_NAME, 1)
    n = text.count("(drill 0.2)")
    assert n > 0
    return text.replace("(drill 0.2)", "(drill 0.3)")


def write(hw_dir, project, kicad_fp_dir="/usr/share/kicad/footprints"):
    sym_path = os.path.join(hw_dir, project + ".kicad_sym")
    with open(sym_path, "w", encoding="utf-8") as f:
        f.write(symbol_lib())
    pretty = os.path.join(hw_dir, project + ".pretty")
    os.makedirs(pretty, exist_ok=True)
    with open(os.path.join(pretty, FP_NAME + ".kicad_mod"), "w", encoding="utf-8") as f:
        f.write(footprint())
    with open(os.path.join(pretty, PROG_FP_NAME + ".kicad_mod"), "w", encoding="utf-8") as f:
        f.write(prog_pads())
    with open(os.path.join(pretty, ESP_FP_NAME + ".kicad_mod"), "w", encoding="utf-8") as f:
        f.write(esp_module(kicad_fp_dir))
    with open(os.path.join(hw_dir, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n\t(version 7)\n\t(lib (name "%s")(type "KiCad")(uri "${KIPRJMOD}/%s.kicad_sym")'
                '(options "")(descr "Project symbols"))\n)\n' % (project, project))
    with open(os.path.join(hw_dir, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n\t(version 7)\n\t(lib (name "%s")(type "KiCad")(uri "${KIPRJMOD}/%s.pretty")'
                '(options "")(descr "Project footprints"))\n)\n' % (project, project))
    return sym_path
