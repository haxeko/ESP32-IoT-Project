#!/usr/bin/env python3
"""Generate the KiCad project settings (.kicad_pro: design rules, net classes), the project library
lib/matter_button_chip.{kicad_sym,pretty} and the project library tables.

- ESP32-H2FH4 symbol: not in the KiCad 9 library. Pins of the datasheet v1.3 (QFN32 4 x 4 mm, pad 33 GND).
  Footprint: KiCad VQFN-32-1EP_4x4mm_P0.4mm_EP2.8x2.8mm_ThermalVias (EP 2.8 mm as the datasheet package drawing)
  with the 9 thermal vias drilled 0.3 mm (standard process; Espressif asks for at least 9 GND vias in the pad).
- Antenna: TI AN043 meandered inverted F (15.2 x 5.7 mm, dimensions of table 1), copper drawn in the footprint.
  The feed (pad 1) and the short to ground (pad 2) are one piece of copper: net tie footprint. A keep-out (no
  tracks, vias, pour on any layer) covers the antenna area and 1.5 mm left of it.
- XL-2121RGBC-WS2812B: KiCad LED:WS2812B symbol renumbered to the XINGLIGHT pinout (1 DI, 2 VDD, 3 DO, 4 GND),
  footprint of the recommended land pattern of the datasheet (4 pads 0.8 x 0.6 mm).
- VBAT: power symbol for the battery rail (KiCad has +BATT, not VBAT).
- Keystone 3034: the stock footprint has two pads "1" (the two tabs of the same metal clip). KiCad 9 would ask for
  a copper connection between them around the coin cell, so the copy here leaves the tab at x = +10.985
  unnumbered (soldered, mechanical only). The holder sits on the bottom side: that tab is on the left in the top
  view.
"""

import json
import os
import re

from design import GROUND_NETS, LIB, POWER_NETS, PROJECT, RF_NETS
from kicad_sexp import HW, KICAD_FP, effects, num, q, stock_symbol, uid, write

DATASHEET = "https://www.espressif.com/sites/default/files/documentation/esp32-h2_datasheet_en.pdf"
AN043 = "https://www.ti.com/lit/an/swra117d/swra117d.pdf"


# ------------------------------------------------------------------------------------------------- symbols
def pin(kind, x, y, angle, length, name, number, hide_name=False):
    nm = effects(hide=False) if not hide_name else effects(hide=True)
    return (f"\t\t\t(pin {kind} line (at {num(x)} {num(y)} {angle}) (length {num(length)})\n"
            f"\t\t\t\t(name {q(name)} {nm})\n"
            f"\t\t\t\t(number {q(number)} {effects()})\n"
            f"\t\t\t)\n")


def prop(name, value, x, y, hide=False, justify=None):
    return f"\t\t(property {q(name)} {q(value)} (at {num(x)} {num(y)} 0) {effects(hide=hide, justify=justify)})\n"


def chip_symbol():
    L, R = -22.86, 22.86
    left = [  # (number, name, type, y): supplies, enable, clocks, RF
        ("1", "VDD3P3", "power_in", 22.86), ("2", "VDD3P3", "power_in", 20.32), ("27", "VDD3P3", "power_in", 17.78),
        ("30", "VDD3P3", "power_in", 15.24), ("31", "VDD3P3", "power_in", 12.7), ("19", "VDDA_PMU", "power_in", 10.16),
        ("18", "VBAT", "power_in", 7.62), ("9", "VDDPST1", "power_in", 5.08), ("20", "VDDPST2", "power_in", 2.54),
        ("17", "CHIP_EN", "input", -2.54), ("29", "XTAL_P", "passive", -7.62), ("28", "XTAL_N", "passive", -10.16),
        ("15", "XTAL_32K_P", "bidirectional", -15.24), ("16", "XTAL_32K_N", "bidirectional", -17.78),
        ("32", "ANT", "bidirectional", -22.86),
    ]
    right = [
        ("3", "IO0", 22.86), ("4", "IO1/ADC1_CH0", 20.32), ("5", "MTMS/IO2", 17.78), ("6", "MTDO/IO3", 15.24),
        ("7", "MTCK/IO4", 12.7), ("8", "MTDI/IO5", 10.16), ("10", "IO8", 5.08), ("11", "IO9", 2.54),
        ("12", "IO10", 0), ("13", "IO11", -2.54), ("14", "IO12", -5.08), ("21", "IO22", -10.16),
        ("22", "U0RXD/IO23", -12.7), ("23", "U0TXD/IO24", -15.24), ("24", "IO25", -17.78),
        ("25", "IO26/USB_D-", -20.32), ("26", "IO27/USB_D+", -22.86),
    ]
    out = ["\t(symbol \"ESP32-H2FH4\"\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n",
           prop("Reference", "U", -17.78, 29.21, justify="left"),
           prop("Value", "ESP32-H2FH4", -17.78, 26.67, justify="left"),
           prop("Footprint", f"{LIB}:ESP32-H2_QFN-32-1EP_4x4mm_P0.4mm", 0, -38.1, hide=True),
           prop("Datasheet", DATASHEET, 0, -40.64, hide=True),
           prop("Description", "ESP32-H2 SoC, 802.15.4 (Thread, Zigbee) + Bluetooth LE 5.3, 4 MB in-package flash, "
                "QFN32 4 x 4 mm", 0, -43.18, hide=True),
           prop("ki_keywords", "ESP32-H2 Thread Zigbee Matter BLE RISC-V", 0, 0, hide=True),
           prop("ki_fp_filters", "*QFN*32*4x4mm*P0.4mm*", 0, 0, hide=True),
           "\t\t(symbol \"ESP32-H2FH4_0_1\"\n"
           "\t\t\t(rectangle (start -17.78 25.4) (end 17.78 -25.4) (stroke (width 0.254) (type default)) "
           "(fill (type background)))\n\t\t)\n",
           "\t\t(symbol \"ESP32-H2FH4_1_1\"\n"]
    for n, name, kind, y in left:
        out.append(pin(kind, L, y, 0, 5.08, name, n))
    for n, name, y in right:
        out.append(pin("bidirectional", R, y, 180, 5.08, name, n))
    out.append(pin("power_in", 0, -30.48, 90, 5.08, "GND", "33"))
    out.append("\t\t)\n\t\t(embedded_fonts no)\n\t)\n")
    return "".join(out)


def led_symbol():
    """KiCad LED:WS2812B with the pins renumbered to the XL-2121RGBC-WS2812B pinout."""
    numbers = {"DIN": "1", "VDD": "2", "DOUT": "3", "VSS": "4"}
    s = stock_symbol("LED:WS2812B").replace('(symbol "WS2812B', '(symbol "XL-2121RGBC-WS2812B')

    def renumber(m):
        name = re.search(r'\(name "([^"]+)"', m.group(0)).group(1)
        return re.sub(r'\(number "\d+"', f'(number "{numbers[name]}"', m.group(0))
    s = re.sub(r"\(pin \w+ \w+.*?\(number \"\d+\"", renumber, s, flags=re.S)
    s = re.sub(r'\(property "Footprint" "[^"]*"', f'(property "Footprint" "{LIB}:LED_XL-2121RGBC-WS2812B_2.1x2.1mm"', s)
    s = re.sub(r'\(property "Datasheet" "[^"]*"',
               '(property "Datasheet" "https://www.lcsc.com/datasheet/C5349957.pdf"', s)
    s = re.sub(r'\(property "ki_fp_filters" "[^"]*"', '(property "ki_fp_filters" "LED*XL?2121*"', s)
    return "\t" + s.replace("\n", "\n\t") + "\n"


def vbat_symbol():
    """Power symbol for the battery rail after the reverse polarity MOSFET (net VBAT), drawn like +3V3."""
    s = stock_symbol("power:+3V3").replace('"+3V3', '"VBAT')
    s = s.replace('(property "Description" "', '(property "Description" "Battery rail (CR2032 after Q1). ', 1)
    return "\t" + s.replace("\n", "\n\t") + "\n"


def write_symbols():
    text = ("(kicad_symbol_lib\n\t(version 20241209)\n\t(generator \"kicad_symbol_editor\")\n"
            "\t(generator_version \"9.0\")\n" + chip_symbol() + led_symbol() + vbat_symbol() + ")\n")
    write(os.path.join(HW, "lib", LIB + ".kicad_sym"), text)


# ---------------------------------------------------------------------------------------------- footprints
def fp_line(name, x1, y1, x2, y2, layer, w):
    return (f"\t(fp_line (start {num(x1)} {num(y1)}) (end {num(x2)} {num(y2)}) "
            f"(stroke (width {num(w)}) (type solid)) (layer {q(layer)}) (uuid {q(uid(name, layer, x1, y1, x2, y2))}))\n")


def fp_rect(name, x1, y1, x2, y2, layer, w):
    return (f"\t(fp_rect (start {num(x1)} {num(y1)}) (end {num(x2)} {num(y2)}) "
            f"(stroke (width {num(w)}) (type solid)) (fill no) (layer {q(layer)}) "
            f"(uuid {q(uid(name, 'rect', layer, x1, y1))}))\n")


def fp_text(name, text, x, y, layer, size=1.0, thick=0.15):
    return (f"\t(fp_text user {q(text)} (at {num(x)} {num(y)} 0) (layer {q(layer)}) "
            f"(uuid {q(uid(name, 'text', text, layer))}) {effects(size, thickness=thick)})\n")


def fp_prop(name, key, value, x, y, layer, hide=False):
    h = " (hide yes)" if hide else ""
    return (f"\t(property {q(key)} {q(value)} (at {num(x)} {num(y)} 0) (layer {q(layer)}){h} "
            f"(uuid {q(uid(name, 'prop', key))}) {effects(1.0, thickness=0.15)})\n")


def smd(name, number, x, y, w, h, paste=True, extra=""):
    layers = '"F.Cu" "F.Mask" "F.Paste"' if paste else '"F.Cu" "F.Mask"'
    return (f"\t(pad {q(number)} smd rect (at {num(x)} {num(y)}) (size {num(w)} {num(h)}) (layers {layers}){extra} "
            f"(uuid {q(uid(name, 'pad', number, x, y))}))\n")


def via_pad(name, number, x, y, d=0.6, drill=0.3):
    return (f"\t(pad {q(number)} thru_hole circle (at {num(x)} {num(y)}) (size {num(d)} {num(d)}) "
            f"(drill {num(drill)}) (layers \"*.Cu\") (remove_unused_layers no) (zone_connect 2) "
            f"(uuid {q(uid(name, 'via', number, x, y))}))\n")


def chip_footprint():
    """Copy of the KiCad VQFN 4 x 4 mm EP 2.8 mm thermal via footprint, vias drilled 0.3 mm."""
    with open(os.path.join(KICAD_FP, "Package_DFN_QFN.pretty",
                           "VQFN-32-1EP_4x4mm_P0.4mm_EP2.8x2.8mm_ThermalVias.kicad_mod")) as f:
        s = f.read()
    n = "ESP32-H2_QFN-32-1EP_4x4mm_P0.4mm"
    assert s.count("(drill 0.2)") == 9, "stock QFN footprint changed"
    s = s.replace("(drill 0.2)", "(drill 0.3)")
    s = s.replace('(footprint "VQFN-32-1EP_4x4mm_P0.4mm_EP2.8x2.8mm_ThermalVias"', f'(footprint "{n}"')
    s = s.replace('"VQFN-32-1EP_4x4mm_P0.4mm_EP2.8x2.8mm_ThermalVias"', f'"{n}"')
    s = re.sub(r'\(descr "[^"]*"', '(descr "ESP32-H2 QFN32 4 x 4 mm, EP 2.8 mm (datasheet v1.3 figure 7-1): KiCad '
               'VQFN-32-1EP_4x4mm_P0.4mm_EP2.8x2.8mm_ThermalVias with 0.3 mm thermal via drills. ' + DATASHEET + '"', s)
    return s


# TI AN043 table 1 (mm): legs and arms of the meandered inverted F
W1, W2, L1, L2, L3, L4, L5, L6 = 0.9, 0.5, 3.94, 2.7, 5.0, 2.64, 2.0, 4.9
D1, D2, D3, D4, D5, D6 = 0.5, 0.3, 0.3, 0.5, 1.4, 1.7
ANT_W, ANT_H = 15.2, D2 + W2 + L6 - D4  # area free of ground: 15.2 x 5.2 mm, the legs reach D4 into the ground
ANT_CLEAR = 1.5  # extra keep-out left of the antenna (ground of the board there, not in the reference design)


def antenna_rects():
    """Copper of the antenna, (x1, y1, x2, y2). Origin: left edge of the antenna area on the edge of the ground
    (y = 0), the antenna points up (-y), the open end on the right."""
    top = -ANT_H + D2                      # top edge of the top arms
    arm = top + W2                         # underside of the top arms
    bottom = arm + L4                      # outer edge of the meander bottoms
    x_short = D1                           # short leg (to ground)
    x_feed = x_short + W1 + D5             # feed leg
    x_m1 = x_feed + W2 + D6                # first leg of the meanders, under the end of the first arm
    rects = [(x_short, arm, x_short + W1, D4), (x_feed, arm, x_feed + W2, D4), (x_short, top, x_m1 + W2, arm)]
    x = x_m1
    for _ in range(2):  # two U shaped meanders, each followed by a top arm L2
        rects += [(x, arm, x + W2, bottom), (x, bottom - W2, x + 2 * W2 + L5, bottom),
                  (x + W2 + L5, arm, x + 2 * W2 + L5, bottom)]
        x2 = x + W2 + L5
        rects.append((x2, top, x2 + L2, arm))
        x = x2 + L2 - W2
    rects.append((x, arm, x + W2, arm + L1))  # open end
    assert abs(x + W2 + D3 - ANT_W) < 1e-6, "antenna width"
    return rects, x_short, x_feed


def antenna_footprint():
    n = "ANT_MIFA_TI-AN043"
    rects, x_short, x_feed = antenna_rects()
    o = [f"(footprint {q(n)}\n\t(version 20241229)\n\t(generator \"pcbnew\")\n\t(generator_version \"9.0\")\n"
         "\t(layer \"F.Cu\")\n"
         f"\t(descr {q('2.4 GHz meandered inverted F antenna, TI AN043 (CC2511 USB dongle), 15.2 x 5.7 mm. Exact copy '
                       'of table 1; the reference is a 1.0 mm board. No copper on any layer in the antenna area. ' + AN043)})\n"
         "\t(tags \"antenna 2.4GHz IFA MIFA PCB\")\n",
         fp_prop(n, "Reference", "AE**", ANT_W / 2, 1.3, "F.Fab"),
         fp_prop(n, "Value", n, ANT_W / 2, 2.3, "F.Fab"),
         fp_prop(n, "Datasheet", AN043, 0, 0, "F.Fab", hide=True),
         fp_prop(n, "Description", "TI AN043 meandered inverted F antenna", 0, 0, "F.Fab", hide=True),
         "\t(net_tie_pad_groups \"1, 2\")\n\t(attr smd exclude_from_pos_files exclude_from_bom)\n"]
    for i, (x1, y1, x2, y2) in enumerate(rects):
        o.append(f"\t(fp_rect (start {num(x1)} {num(y1)}) (end {num(x2)} {num(y2)}) (stroke (width 0) (type solid)) "
                 f"(fill yes) (layer \"F.Cu\") (uuid {q(uid(n, 'copper', i))}))\n")
    # pads at the ends of the legs: 1 feed, 2 short to ground (with a via to the bottom ground)
    o.append(smd(n, 1, x_feed + W2 / 2, D4 / 2, W2, D4, paste=False).replace('"F.Cu" "F.Mask"', '"F.Cu"'))
    o.append(smd(n, 2, x_short + W1 / 2, D4 / 2, W1, D4, paste=False).replace('"F.Cu" "F.Mask"', '"F.Cu"'))
    o.append(via_pad(n, 2, x_short + W1 / 2, D4 / 2))
    x0 = -ANT_CLEAR
    o.append(fp_rect(n, 0, -ANT_H, ANT_W, 0, "F.Fab", 0.1))
    o.append(fp_text(n, "ANTENNA", ANT_W / 2, -ANT_H - 0.8, "F.Fab", 0.8, 0.12))
    # courtyard: the antenna copper above the edge of the ground (the matching parts sit right below that edge)
    o.append(fp_rect(n, D1 - 0.25, -ANT_H - 0.25, ANT_W + 0.25, 0, "F.CrtYd", 0.05))
    o.append("\t(zone (net 0) (net_name \"\") (layers \"*.Cu\") "
             f"(uuid {q(uid(n, 'keepout'))}) (name \"antenna\") (hatch full 0.508)\n"
             "\t\t(connect_pads (clearance 0)) (min_thickness 0.254) (filled_areas_thickness no)\n"
             "\t\t(keepout (tracks not_allowed) (vias not_allowed) (pads allowed) (copperpour not_allowed) "
             "(footprints allowed))\n"
             "\t\t(placement (enabled no) (sheetname \"\"))\n"
             "\t\t(fill (thermal_gap 0.508) (thermal_bridge_width 0.508))\n"
             f"\t\t(polygon (pts (xy {num(x0)} {num(-ANT_H)}) (xy {num(ANT_W)} {num(-ANT_H)}) (xy {num(ANT_W)} 0) "
             f"(xy {num(x0)} 0)))\n\t)\n")
    o.append("\t(embedded_fonts no)\n)\n")
    return "".join(o)


def led_footprint():
    """XINGLIGHT XL-2121RGBC-WS2812B, recommended land pattern: pads 0.8 x 0.6 mm, 0.9 mm apart across, 0.4 mm
    along. Top view: 1 DI bottom right (the chamfered corner), 2 VDD top right, 3 DO top left, 4 GND bottom left."""
    n = "LED_XL-2121RGBC-WS2812B_2.1x2.1mm"
    o = [f"(footprint {q(n)}\n\t(version 20241229)\n\t(generator \"pcbnew\")\n\t(generator_version \"9.0\")\n"
         "\t(layer \"F.Cu\")\n"
         "\t(descr \"XINGLIGHT XL-2121RGBC-WS2812B RGB LED with WS2812B controller, 2.1 x 2.1 x 1.0 mm, LCSC C5349957\")\n"
         "\t(tags \"LED RGB WS2812B 2121\")\n",
         fp_prop(n, "Reference", "REF**", 0, -1.9, "F.SilkS"),
         fp_prop(n, "Value", n, 0, 1.9, "F.Fab"),
         "\t(attr smd)\n"]
    for number, x, y in (("1", 0.85, 0.5), ("2", 0.85, -0.5), ("3", -0.85, -0.5), ("4", -0.85, 0.5)):
        o.append(smd(n, number, x, y, 0.8, 0.6))
    o.append(fp_line(n, -1.05, -1.05, 1.05, -1.05, "F.Fab", 0.1))
    o.append(fp_line(n, 1.05, -1.05, 1.05, 0.6, "F.Fab", 0.1))
    o.append(fp_line(n, 1.05, 0.6, 0.6, 1.05, "F.Fab", 0.1))
    o.append(fp_line(n, 0.6, 1.05, -1.05, 1.05, "F.Fab", 0.1))
    o.append(fp_line(n, -1.05, 1.05, -1.05, -1.05, "F.Fab", 0.1))
    o.append(fp_line(n, -1.05, -1.17, 1.05, -1.17, "F.SilkS", 0.12))
    o.append(fp_line(n, -1.05, 1.17, 0.5, 1.17, "F.SilkS", 0.12))
    o.append(fp_line(n, 0.5, 1.17, 1.4, 1.17, "F.SilkS", 0.12))  # pin 1 side: the line runs on past the pad
    o.append(fp_rect(n, -1.5, -1.5, 1.5, 1.5, "F.CrtYd", 0.05))
    o.append(fp_text(n, "${REFERENCE}", 0, 0, "F.Fab", 0.5, 0.08))
    o.append("\t(embedded_fonts no)\n)\n")
    return "".join(o)


def battery_holder_footprint():
    with open(os.path.join(KICAD_FP, "Battery.pretty", "BatteryHolder_Keystone_3034_1x20mm.kicad_mod")) as f:
        s = f.read()
    s = s.replace('(footprint "BatteryHolder_Keystone_3034_1x20mm"', '(footprint "BatteryHolder_Keystone_3034_CR2032"')
    s = s.replace('(descr "', '(descr "Copy of Battery:BatteryHolder_Keystone_3034_1x20mm with the tab at x = +10.985 '
                  'unnumbered (same metal clip as pad 1, soldered for strength only). ', 1)
    old = '(pad "1" smd rect\n\t\t(at 10.985 0)'
    assert old in s, "stock Keystone 3034 footprint changed"
    s = s.replace(old, '(pad "" smd rect\n\t\t(at 10.985 0)')
    return s


def write_footprints():
    d = os.path.join(HW, "lib", LIB + ".pretty")
    write(os.path.join(d, "ESP32-H2_QFN-32-1EP_4x4mm_P0.4mm.kicad_mod"), chip_footprint())
    write(os.path.join(d, "ANT_MIFA_TI-AN043.kicad_mod"), antenna_footprint())
    write(os.path.join(d, "LED_XL-2121RGBC-WS2812B_2.1x2.1mm.kicad_mod"), led_footprint())
    write(os.path.join(d, "BatteryHolder_Keystone_3034_CR2032.kicad_mod"), battery_holder_footprint())


def write_tables():
    write(os.path.join(HW, "sym-lib-table"),
          f'(sym_lib_table\n\t(version 7)\n\t(lib (name "{LIB}") (type "KiCad") '
          f'(uri "${{KIPRJMOD}}/lib/{LIB}.kicad_sym") (options "") (descr "Project parts"))\n)\n')
    write(os.path.join(HW, "fp-lib-table"),
          f'(fp_lib_table\n\t(version 7)\n\t(lib (name "{LIB}") (type "KiCad") '
          f'(uri "${{KIPRJMOD}}/lib/{LIB}.pretty") (options "") (descr "Project footprints"))\n)\n')


# ------------------------------------------------------------------------------------------------- project
def netclass(name, track, priority, clearance=0.15):
    return {"bus_width": 12, "clearance": clearance, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
            "diff_pair_width": 0.2, "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "name": name,
            "pcb_color": "rgba(0, 0, 0, 0.000)", "priority": priority, "schematic_color": "rgba(0, 0, 0, 0.000)",
            "track_width": track, "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6}


def write_project():
    """Design rules for a 4-layer 1.0 mm board, JLCPCB stack-up JLC04101H-7628 (the default for 4 layers, 1.0 mm):
    L1 35 um / 7628 prepreg 0.21 mm (er 4.4) / L2 / 0.5 mm core / L3 / 0.21 mm / L4. Standard process: 0.127 mm track
    and space, 0.3 mm drill. Routing uses 0.15 mm clearance (the 0.4 mm pitch of the chip), 0.2 mm signal and 0.3 mm
    power tracks, 0.6/0.3 mm vias, 0.2 mm from the ground pours. RF: 0.38 mm track, the ground pour 0.2 mm away on both
    sides, over the L2 ground: grounded coplanar line of 50 ohm."""
    pro = {
        "board": {"design_settings": {
            "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2, "silk_line_width": 0.12,
                         "silk_text_size_h": 0.8, "silk_text_size_v": 0.8, "silk_text_thickness": 0.12,
                         "fab_line_width": 0.1, "courtyard_line_width": 0.05, "zones": {"min_clearance": 0.2}},
            "diff_pair_dimensions": [], "drc_exclusions": [], "meta": {"version": 2},
            "rules": {"max_error": 0.005, "min_clearance": 0.15, "min_connection": 0.0,
                      "min_copper_edge_clearance": 0.3, "min_groove_width": 0.0, "min_hole_clearance": 0.25,
                      "min_hole_to_hole": 0.25, "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1,
                      "min_resolved_spokes": 1, "min_silk_clearance": 0.0, "min_text_height": 0.6,
                      "min_text_thickness": 0.08, "min_through_hole_diameter": 0.3, "min_track_width": 0.15,
                      "min_via_annular_width": 0.1, "min_via_diameter": 0.5, "solder_mask_to_copper_clearance": 0.0,
                      "use_height_for_length_calcs": True},
            "track_widths": [0.0, 0.2, 0.3, 0.38, 0.5],
            "via_dimensions": [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}],
            "zones_allow_external_fillets": False}},
        "boards": [],
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": PROJECT + ".kicad_pro", "version": 3},
        "net_settings": {
            "classes": [netclass("Default", 0.2, 2147483647), netclass("Power", 0.3, 0),
                        netclass("Ground", 0.3, 1), netclass("RF", 0.38, 2)],
            "meta": {"version": 4}, "net_colors": None, "netclass_assignments": None,
            "netclass_patterns": [{"netclass": "Power", "pattern": n} for n in POWER_NETS] +
                                 [{"netclass": "Ground", "pattern": n} for n in GROUND_NETS] +
                                 [{"netclass": "RF", "pattern": n} for n in RF_NETS]},
        "pcbnew": {"page_layout_descr_file": ""},
        "sheets": [[uid(PROJECT, "root-sheet"), "Root"]],
        "text_variables": {},
    }
    write(os.path.join(HW, PROJECT + ".kicad_pro"), json.dumps(pro, indent=2) + "\n")


# footprints that may sit closer than their courtyards allow (pad to pad clearances still apply): the passives,
# crystals and transistors packed around the 0.4 mm pitch chip, and the antenna (copper, no body)
DENSE = ["*_0402_*", "*_0603_*", "*QFN-32*", "*Crystal_SMD*", "*SOT-23*", "*XL-2121*", "*ANT_MIFA*",
         "*SolderJumper*"]


def write_rules():
    """Custom design rules (<project>.kicad_dru): courtyard overlap allowed between the DENSE footprints."""
    def any_of(side):
        return " || ".join(f"{side}.Library_Link == '{p}'" for p in DENSE)
    write(os.path.join(HW, PROJECT + ".kicad_dru"),
          "(version 1)\n"
          "(rule \"dense small parts\"\n"
          "\t(constraint courtyard_clearance (min -1mm))\n"
          f"\t(condition \"({any_of('A')}) && ({any_of('B')})\"))\n")


if __name__ == "__main__":
    write_project()
    write_rules()
    write_symbols()
    write_footprints()
    write_tables()
    print("library written to", os.path.join(HW, "lib"))
