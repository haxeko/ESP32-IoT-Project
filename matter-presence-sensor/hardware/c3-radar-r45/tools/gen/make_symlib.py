# custom symbols: ESP32-C3-WROOM-02 and HLK-LD2410C (KiCad 7 symbol library format)
from design import PROJ_DIR, PROJECT
F = '(effects (font (size 1.27 1.27)))'
def pin(typ, x, y, ang, name, num, length=2.54):
    return (f'      (pin {typ} line (at {x} {y} {ang}) (length {length})\n'
            f'        (name "{name}" {F})\n        (number "{num}" {F})\n      )\n')
def prop(k, v, x, y, hide=False, justify=None):
    eff = '(effects (font (size 1.27 1.27))' + (f' (justify {justify})' if justify else '') + (' hide' if hide else '') + ')'
    return f'    (property "{k}" "{v}" (at {x} {y} 0)\n      {eff}\n    )\n'

esp_left = [("input", "EN", 2, 20.32), ("bidirectional", "IO9", 8, 15.24), ("bidirectional", "IO8", 7, 12.7),
            ("bidirectional", "IO2", 16, 10.16), ("bidirectional", "RXD0/IO20", 11, 5.08),
            ("bidirectional", "TXD0/IO21", 12, 2.54), ("bidirectional", "IO18/USB_D-", 13, -2.54),
            ("bidirectional", "IO19/USB_D+", 14, -5.08)]
esp_right = [("bidirectional", "IO0", 18, 20.32), ("bidirectional", "IO1", 17, 17.78), ("bidirectional", "IO3", 15, 15.24),
             ("bidirectional", "IO4", 3, 12.7), ("bidirectional", "IO5", 4, 10.16), ("bidirectional", "IO6", 5, 7.62),
             ("bidirectional", "IO7", 6, 5.08), ("bidirectional", "IO10", 10, 2.54)]
s = '(kicad_symbol_lib (version 20220914) (generator kicad_symbol_editor)\n'
s += '  (symbol "ESP32-C3-WROOM-02" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)\n'
s += prop("Reference", "U", -12.7, 29.21, justify="left")
s += prop("Value", "ESP32-C3-WROOM-02", 2.54, 29.21, justify="left")
s += prop("Footprint", "RF_Module:ESP32-C3-WROOM-02", 0, -33.02, hide=True)
s += prop("Datasheet", "https://www.espressif.com/sites/default/files/documentation/esp32-c3-wroom-02_datasheet_en.pdf", 0, -35.56, hide=True)
s += prop("ki_keywords", "ESP32-C3 WiFi BLE RISC-V module", 0, 0, hide=True)
s += prop("ki_description", "ESP32-C3 Wi-Fi 4 + BLE 5 module, PCB antenna, 4 MB flash (N4). Strapping: IO2, IO8, IO9", 0, 0, hide=True)
s += ('    (symbol "ESP32-C3-WROOM-02_0_1"\n      (rectangle (start -12.7 25.4) (end 12.7 -25.4)\n'
      '        (stroke (width 0.254) (type default))\n        (fill (type background))\n      )\n    )\n')
s += '    (symbol "ESP32-C3-WROOM-02_1_1"\n'
s += pin("power_in", 0, 27.94, 270, "3V3", 1)
for t, n, num, y in esp_left: s += pin(t, -15.24, y, 0, n, num)
for t, n, num, y in esp_right: s += pin(t, 15.24, y, 180, n, num)
s += pin("power_in", -2.54, -27.94, 90, "GND", 9)
s += pin("passive", 2.54, -27.94, 90, "GND", 19)
s += '    )\n  )\n'
s += '  (symbol "HLK-LD2410C" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)\n'
s += prop("Reference", "RD", -7.62, 11.43, justify="left")
s += prop("Value", "HLK-LD2410C", 1.27, 11.43, justify="left")
s += prop("Footprint", f"{PROJECT}:HLK-LD2410C_1x05_P2.54mm", 0, -13.97, hide=True)
s += prop("Datasheet", "https://www.hlktech.net", 0, -16.51, hide=True)
s += prop("ki_keywords", "radar mmwave 24GHz presence", 0, 0, hide=True)
s += prop("ki_description", "Hi-Link 24 GHz human presence radar, 5 V supply, 3.3 V UART 256000 8N1 + OUT pin", 0, 0, hide=True)
s += ('    (symbol "HLK-LD2410C_0_1"\n      (rectangle (start -7.62 7.62) (end 7.62 -7.62)\n'
      '        (stroke (width 0.254) (type default))\n        (fill (type background))\n      )\n    )\n')
s += '    (symbol "HLK-LD2410C_1_1"\n'
s += pin("output", -10.16, 2.54, 0, "TX", 1)
s += pin("input", -10.16, 0, 0, "RX", 2)
s += pin("output", -10.16, -2.54, 0, "OUT", 3)
s += pin("power_in", 0, -10.16, 90, "GND", 4)
s += pin("power_in", 0, 10.16, 270, "VCC", 5)
s += '    )\n  )\n)\n'
open(f"{PROJ_DIR}/{PROJECT}.kicad_sym", "w").write(s)
open(f"{PROJ_DIR}/sym-lib-table", "w").write(
    f'(sym_lib_table\n  (version 7)\n  (lib (name "{PROJECT}")(type "KiCad")(uri "${{KIPRJMOD}}/{PROJECT}.kicad_sym")(options "")(descr "Project symbols"))\n)\n')
open(f"{PROJ_DIR}/fp-lib-table", "w").write(
    f'(fp_lib_table\n  (version 7)\n  (lib (name "{PROJECT}")(type "KiCad")(uri "${{KIPRJMOD}}/{PROJECT}.pretty")(options "")(descr "Project footprints"))\n)\n')
print("ok")
