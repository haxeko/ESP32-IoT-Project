# Shared design data for h2_contact: ESP32-H2-MINI-1-N4 Matter over Thread door/window contact sensor.
# 26 x 46 mm, 2 layers, every SMD part on the top side (single-side assembly).
# Board coordinates (u, v) in mm, origin at the board centre, v pointing UP (top view).
# Pins follow the firmware defaults (../../main/Kconfig.projbuild): reed switch GPIO4 (to GND, closed = LOW),
# factory reset / BOOT button GPIO9, status LED GPIO5 (active high), USB Serial/JTAG on GPIO26/27.
import os
import uuid

PROJECT = "h2_contact"
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "kicad"))
FAB_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "fab"))
IMG_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "docs", "images"))
WORK = os.environ.get("H2C_WORK", "/tmp/h2c_route")
STD_FP = "/usr/share/kicad/footprints"
LOCAL_FP = os.path.join(PROJ_DIR, PROJECT + ".pretty")

BOARD_W, BOARD_H, CORNER_R = 26.0, 46.0, 1.5
CENTER = (100.0, 100.0)    # KiCad page coordinates of the board centre
U1_V = 14.7                # module origin: module top edge (y = -8.3) at the top board edge v = 23
ANT_V = 17.6               # antenna area of the module starts here (y = -2.9): no copper above, both layers

_NS = uuid.UUID("3b8f9d52-6c1e-4f0a-9d27-5e4b1a7c2f61")


def suuid(tag):
    return str(uuid.uuid5(_NS, tag))


ROOT_UUID = suuid("root-sheet")


def kc(u, v):
    return CENTER[0] + u, CENTER[1] - v


L = PROJECT
# ESP32-H2-MINI-1 pads (Espressif KiCad library / datasheet v1.6 table 3)
U1_GND = ["1", "2", "11", "14"] + [str(n) for n in range(36, 54)]
U1_PADS = {n: "GND" for n in U1_GND}
U1_PADS.update({"3": "+3V3", "8": "EN", "18": "CONTACT", "19": "LED", "22": "IO8", "23": "IO9",
                "26": "USB_DN", "27": "USB_DP", "30": "RXD0", "31": "TXD0"})

PARTS = [
    # ---- ESP32-H2 module at the top edge, antenna to the edge ------------
    dict(ref="U1", value="ESP32-H2-MINI-1-N4", fp=f"{L}:ESP32-H2-MINI-1", u=0.0, v=U1_V, rot=0,
         pads=U1_PADS, sym=f"{L}:ESP32-H2-MINI-1",
         part="Espressif ESP32-H2-MINI-1-N4 (4 MB flash, PCB antenna)"),
    # 3V3 decoupling and EN RC next to the left pin column (pin 3 = 3V3, pin 8 = EN)
    dict(ref="C5", value="100nF", fp="Capacitor_SMD:C_0603_1608Metric", u=-8.6, v=14.4, rot=0,
         pads={"1": "GND", "2": "+3V3"}, sym="Device:C", part="MLCC X7R 16 V"),
    dict(ref="C4", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", u=-9.0, v=12.4, rot=0,
         pads={"1": "GND", "2": "+3V3"}, sym="Device:C", part="MLCC X5R/X7R 10 V+"),
    dict(ref="R3", value="10k", fp="Resistor_SMD:R_0603_1608Metric", u=-9.0, v=10.4, rot=0,
         pads={"1": "+3V3", "2": "EN"}, sym="Device:R", part="1% 0603"),
    dict(ref="C6", value="1uF", fp="Capacitor_SMD:C_0603_1608Metric", u=-9.0, v=8.6, rot=0,
         pads={"1": "GND", "2": "EN"}, sym="Device:C", part="MLCC X7R 16 V"),
    # USB D-/D+ series resistors next to pins 26/27 (right column)
    dict(ref="R9", value="22", fp="Resistor_SMD:R_0603_1608Metric", u=8.8, v=10.4, rot=0,
         pads={"1": "USB_DP_C", "2": "USB_DP"}, sym="Device:R", part="1% 0603"),
    dict(ref="R10", value="22", fp="Resistor_SMD:R_0603_1608Metric", u=8.8, v=8.0, rot=0,
         pads={"1": "USB_DN_C", "2": "USB_DN"}, sym="Device:R", part="1% 0603"),
    # ---- row under the module: pull-ups, LED, reed input filter ----------
    dict(ref="C7", value="100nF", fp="Capacitor_SMD:C_0603_1608Metric", u=-2.6, v=4.4, rot=90,
         pads={"1": "CONTACT", "2": "GND"}, sym="Device:C", part="MLCC X7R 16 V"),
    dict(ref="R8", value="1k", fp="Resistor_SMD:R_0603_1608Metric", u=-0.8, v=4.4, rot=90,
         pads={"1": "REED", "2": "CONTACT"}, sym="Device:R", part="1% 0603"),
    dict(ref="R6", value="1k", fp="Resistor_SMD:R_0603_1608Metric", u=1.1, v=4.4, rot=90,
         pads={"1": "LED", "2": "LED_A"}, sym="Device:R", part="1% 0603"),
    dict(ref="R5", value="10k", fp="Resistor_SMD:R_0603_1608Metric", u=3.0, v=4.4, rot=90,
         pads={"1": "+3V3", "2": "IO8"}, sym="Device:R", part="1% 0603"),
    dict(ref="R4", value="10k", fp="Resistor_SMD:R_0603_1608Metric", u=4.9, v=4.4, rot=90,
         pads={"1": "+3V3", "2": "IO9"}, sym="Device:R", part="1% 0603"),
    dict(ref="D3", value="LED green", fp="LED_SMD:LED_0603_1608Metric", u=1.1, v=1.6, rot=0,
         pads={"1": "GND", "2": "LED_A"}, sym="Device:LED", part="0603 green LED, Vf ~2 V"),
    # ---- buttons -------------------------------------------------------
    dict(ref="SW1", value="RESET", fp="Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", u=-8.6, v=-0.3, rot=0,
         pads={"1": "EN", "2": "GND"}, sym="Switch:SW_Push", part="XKB TS-1187A-B-A-B 5.1 x 5.1 mm SMD tact"),
    dict(ref="SW2", value="BOOT", fp="Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", u=8.6, v=-0.3, rot=0,
         pads={"1": "IO9", "2": "GND"}, sym="Switch:SW_Push", part="XKB TS-1187A-B-A-B 5.1 x 5.1 mm SMD tact"),
    # ---- power: USB-C VBUS -> PTC -> TVS -> AP2112K-3.3 ------------------
    dict(ref="F1", value="PTC 0.5A", fp="Fuse:Fuse_1206_3216Metric", u=-1.2, v=-12.4, rot=90,
         pads={"1": "VBUS", "2": "+5V"}, sym="Device:Polyfuse_Small",
         part="PTC resettable fuse 1206, Ihold 0.5 A, >= 6 V"),
    dict(ref="D1", value="SMF5.0A", fp="Diode_SMD:D_SOD-123F", u=-1.2, v=-7.6, rot=90,
         pads={"1": "+5V", "2": "GND"}, sym="Diode:SMF5V0A", part="SMF5.0A unidirectional TVS, SOD-123F"),
    dict(ref="C1", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", u=1.8, v=-8.0, rot=90,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C", part="MLCC X5R 16 V+"),
    dict(ref="U2", value="AP2112K-3.3", fp="Package_TO_SOT_SMD:SOT-23-5", u=-4.7, v=-8.5, rot=0,
         pads={"1": "+5V", "2": "GND", "3": "+5V", "5": "+3V3"}, sym="Regulator_Linear:AP2112K-3.3",
         part="Diodes AP2112K-3.3TRG1, 600 mA (alt: ME6211C33M5G, same pinout)"),
    dict(ref="C2", value="1uF", fp="Capacitor_SMD:C_0603_1608Metric", u=-4.7, v=-11.4, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C", part="MLCC X7R 16 V, LDO input"),
    dict(ref="C3", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", u=-4.7, v=-5.4, rot=0,
         pads={"1": "+3V3", "2": "GND"}, sym="Device:C", part="MLCC X5R/X7R 10 V+, LDO output"),
    # ---- USB-C (16P, USB 2.0) at the bottom edge -------------------------
    dict(ref="J1", value="USB-C", fp="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", u=5.5, v=-19.65, rot=0,
         pads={"A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
               "A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "S1": "GND",
               "A5": "CC1", "B5": "CC2", "A6": "USB_DP_C", "B6": "USB_DP_C", "A7": "USB_DN_C", "B7": "USB_DN_C"},
         sym="Connector:USB_C_Receptacle_USB2.0_16P", part="HRO TYPE-C-31-M-12 16P USB-C receptacle"),
    dict(ref="R1", value="5.1k", fp="Resistor_SMD:R_0603_1608Metric", u=1.5, v=-12.6, rot=90,
         pads={"1": "CC1", "2": "GND"}, sym="Device:R", part="1% 0603"),
    dict(ref="R2", value="5.1k", fp="Resistor_SMD:R_0603_1608Metric", u=9.6, v=-12.6, rot=90,
         pads={"1": "CC2", "2": "GND"}, sym="Device:R", part="1% 0603"),
    dict(ref="U3", value="USBLC6-2SC6", fp="Package_TO_SOT_SMD:SOT-23-6", u=5.6, v=-9.5, rot=0,
         pads={"1": "USB_DP_C", "6": "USB_DP_C", "3": "USB_DN_C", "4": "USB_DN_C", "2": "GND", "5": "+5V"},
         sym="Power_Protection:USBLC6-2SC6", part="ST USBLC6-2SC6 USB ESD array, SOT-23-6"),
    # ---- reed switch screw terminal (bottom-left, wires out of the bottom edge) --
    dict(ref="J2", value="REED", fp="TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-2-3.5-H_1x02_P3.50mm_Horizontal",
         u=-10.6, v=-18.45, rot=0, pads={"1": "REED", "2": "GND"}, sym="Connector:Screw_Terminal_01x02",
         part="2P screw terminal, 3.5 mm pitch (e.g. KF350-3.5-2P / Phoenix PT 1,5/2-3,5-H), hand soldered"),
    dict(ref="R7", value="10k", fp="Resistor_SMD:R_0603_1608Metric", u=-7.9, v=-12.8, rot=0,
         pads={"1": "+3V3", "2": "REED"}, sym="Device:R", part="1% 0603, wetting current for the reed switch"),
    dict(ref="D2", value="ESD 3.3V", fp="Diode_SMD:D_SOD-323", u=-11.0, v=-12.8, rot=0,
         pads={"1": "REED", "2": "GND"}, sym="Device:D_TVS", part="bidirectional ESD TVS 3.3 V, SOD-323 (e.g. PESD3V3L1BA)"),
] + [dict(ref=f"TP{i + 1}", value=val, fp="TestPoint:TestPoint_Pad_D1.5mm", u=-3.81 + 2.54 * i, v=12.0, rot=0,
          side="B", pads={"1": net}, sym="Connector:TestPoint", part="test pad (bottom side, no part)")
     for i, (val, net) in enumerate([("3V3", "+3V3"), ("GND", "GND"), ("TX0", "TXD0"), ("RX0", "RXD0")])]

# board-only M2 mounting holes (not in the schematic)
HOLES = [("H1", -10.5, -7.5), ("H2", 10.5, -7.5)]

POWER_NETS = ["VBUS", "+5V", "+3V3", "GND"]
for p in PARTS:
    p.setdefault("side", "F")
    p["uuid"] = suuid("sym-" + p["ref"])
