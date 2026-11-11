# Project symbol library (KiCad 7 format): ESP32-H2-MINI-1.
# Pin numbers and names from the Espressif KiCad library (github.com/espressif/kicad-libraries, symbol
# ESP32-H2-MINI-1) and the ESP32-H2-MINI-1 datasheet v1.6, table 3. The NC pads (4, 7, 17, 28, 29, 32-35)
# have no pin. VBAT (15) is tied to 3V3 inside the module (R2 = 0 ohm), left unconnected here.
# The footprint h2_contact.pretty/ESP32-H2-MINI-1.kicad_mod is the Espressif one, unchanged
# (CC-BY-SA 4.0 with the KiCad library exception, see hardware/README.md).
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PROJ_DIR, PROJECT

F = '(effects (font (size 1.27 1.27)))'
FH = '(effects (font (size 1.27 1.27)) hide)'


def pin(typ, x, y, ang, name, num, length=2.54, hide=False):
    return (f'      (pin {typ} line (at {x} {y} {ang}) (length {length}){" hide" if hide else ""}\n'
            f'        (name "{name}" {F})\n        (number "{num}" {F})\n      )\n')


def prop(k, v, x, y, hide=False, justify=None):
    eff = '(effects (font (size 1.27 1.27))' + (f' (justify {justify})' if justify else '') + (' hide' if hide else '') + ')'
    return f'    (property "{k}" "{v}" (at {x} {y} 0)\n      {eff}\n    )\n'


# (type, name, number, y); left side x = -15.24, right side x = 15.24
left = [("input", "EN", 8, 22.86), ("bidirectional", "IO9", 23, 17.78), ("bidirectional", "IO8", 22, 15.24),
        ("bidirectional", "IO25", 25, 12.7), ("bidirectional", "IO2", 5, 10.16), ("bidirectional", "IO3", 6, 7.62),
        ("bidirectional", "RXD0/IO23", 30, 2.54), ("bidirectional", "TXD0/IO24", 31, 0),
        ("bidirectional", "IO26/USB_D-", 26, -5.08), ("bidirectional", "IO27/USB_D+", 27, -7.62),
        ("power_in", "VBAT", 15, -12.7)]
right = [("bidirectional", "IO0", 9, 22.86), ("bidirectional", "IO1", 10, 20.32), ("bidirectional", "IO4", 18, 17.78),
         ("bidirectional", "IO5", 19, 15.24), ("bidirectional", "IO10", 20, 12.7), ("bidirectional", "IO11", 21, 10.16),
         ("bidirectional", "IO12", 16, 7.62), ("bidirectional", "IO13", 12, 5.08), ("bidirectional", "IO14", 13, 2.54),
         ("bidirectional", "IO22", 24, 0)]
GND = ["1", "2", "11", "14"] + [str(n) for n in range(36, 54)]

s = '(kicad_symbol_lib (version 20220914) (generator kicad_symbol_editor)\n'
s += '  (symbol "ESP32-H2-MINI-1" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)\n'
s += prop("Reference", "U", -15.24, 29.21, justify="left")
s += prop("Value", "ESP32-H2-MINI-1", 2.54, 29.21, justify="left")
s += prop("Footprint", f"{PROJECT}:ESP32-H2-MINI-1", 0, -22.86, hide=True)
s += prop("Datasheet", "https://www.espressif.com/sites/default/files/documentation/esp32-h2-mini-1_mini-1u_datasheet_en.pdf",
          0, -25.4, hide=True)
s += prop("ki_keywords", "ESP32-H2 Thread Zigbee Matter BLE 802.15.4 module", 0, 0, hide=True)
s += prop("ki_description", "ESP32-H2 802.15.4 (Thread/Zigbee) + BLE 5 module, PCB antenna, 2/4 MB flash. "
          "Strapping: IO8, IO9, IO25. VBAT tied to 3V3 inside the module", 0, 0, hide=True)
s += ('    (symbol "ESP32-H2-MINI-1_0_1"\n      (rectangle (start -12.7 26.67) (end 12.7 -17.78)\n'
      '        (stroke (width 0.254) (type default))\n        (fill (type background))\n      )\n    )\n')
s += '    (symbol "ESP32-H2-MINI-1_1_1"\n'
s += pin("power_in", 0, 29.21, 270, "3V3", 3)
for t, n, num, y in left:
    s += pin(t, -15.24, y, 0, n, num)
for t, n, num, y in right:
    s += pin(t, 15.24, y, 180, n, num)
# every GND pad on one pin position: the first visible, the others stacked and hidden
for i, num in enumerate(GND):
    s += pin("power_in" if i == 0 else "passive", 0, -20.32, 90, "GND", num, hide=i > 0)
s += '    )\n  )\n)\n'

open(os.path.join(PROJ_DIR, f"{PROJECT}.kicad_sym"), "w").write(s)
open(os.path.join(PROJ_DIR, "sym-lib-table"), "w").write(
    f'(sym_lib_table\n  (version 7)\n  (lib (name "{PROJECT}")(type "KiCad")(uri "${{KIPRJMOD}}/{PROJECT}.kicad_sym")'
    f'(options "")(descr "Project symbols"))\n)\n')
open(os.path.join(PROJ_DIR, "fp-lib-table"), "w").write(
    f'(fp_lib_table\n  (version 7)\n  (lib (name "{PROJECT}")(type "KiCad")(uri "${{KIPRJMOD}}/{PROJECT}.pretty")'
    f'(options "")(descr "Project footprints (ESP32-H2-MINI-1 from the Espressif KiCad library)"))\n)\n')
print("ok", len(left) + len(right) + 1 + len(GND), "pins")
