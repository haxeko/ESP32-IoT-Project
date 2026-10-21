# Shared design data for c3_radar_r45 (ESP32-C3-WROOM-02 + HLK-LD2410C side by side, round 45 mm)
# Board coordinates (u, v) in mm, origin at board centre, v pointing UP (top view).
import uuid

PROJECT = "c3_radar_r45"
PROJ_DIR = "/home/claude/c3_radar_r45"
STD_FP = "/usr/share/kicad/footprints"
LOCAL_FP = PROJ_DIR + "/c3_radar_r45.pretty"

BOARD_R = 22.5            # 45 mm diameter
CENTER = (100.0, 100.0)   # KiCad page coordinates of the board centre
ANT_V = 14.5              # WiFi antenna keep-out starts here (whole width, both layers)
U1_V = 7.4                # U1 footprint origin -> module top edge at v = 20.5 (flush with the round edge)
RADAR_ROW_V = -2.965      # radar pin row -> radar body v = -17.5 .. -1.5 (16 mm below the antenna zone)

_NS = uuid.UUID("6f1c2e3a-0d5b-4c3e-9a1f-0b2d4e6f8a10")


def suuid(tag):
    return str(uuid.uuid5(_NS, tag))


ROOT_UUID = suuid("root-sheet")


def kc(u, v):
    return CENTER[0] + u, CENTER[1] - v


L = PROJECT
PARTS = [
    # ---- top side (room side) ------------------------------------------
    dict(ref="U1", value="ESP32-C3-WROOM-02-N4", fp="RF_Module:ESP32-C3-WROOM-02", side="F", u=0.0, v=U1_V, rot=0,
         pads={"1": "+3V3", "2": "EN", "7": "IO8", "8": "IO9", "9": "GND", "10": "RADAR_TX", "11": "RXD0",
               "12": "TXD0", "15": "RADAR_RX", "16": "IO2", "17": "RADAR_OUT", "19": "GND"},
         sym=f"{L}:ESP32-C3-WROOM-02"),
    dict(ref="RD1", value="HLK-LD2410C", fp=f"{L}:HLK-LD2410C_1x05_P2.54mm", side="F", u=0.0, v=RADAR_ROW_V, rot=180,
         pads={"1": "RADAR_TX", "2": "RADAR_RX", "3": "RADAR_OUT", "4": "GND", "5": "+5V"}, sym=f"{L}:HLK-LD2410C"),
    dict(ref="C4", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", side="F", u=-11.6, v=11.2, rot=0,
         pads={"1": "GND", "2": "+3V3"}, sym="Device:C"),
    dict(ref="C5", value="100nF", fp="Capacitor_SMD:C_0603_1608Metric", side="F", u=-11.3, v=13.35, rot=0,
         pads={"1": "GND", "2": "+3V3"}, sym="Device:C"),
    # ---- bottom side: USB-C power entry (bottom edge) --------------------
    dict(ref="J1", value="USB-C 6P (power only)", fp="Connector_USB:USB_C_Receptacle_GCT_USB4135-GF-A_6P_TopMnt_Horizontal",
         side="B", u=0.0, v=-19.1, rot=None,
         pads={"A9": "VBUS", "B9": "VBUS", "A12": "GND", "B12": "GND", "A5": "CC1", "B5": "CC2", "S1": "GND"},
         sym="Connector:USB_C_Receptacle_PowerOnly_6P"),
    dict(ref="R1", value="5.1k", fp="Resistor_SMD:R_0402_1005Metric", side="B", u=-0.5, v=-13.9, rot=90,
         pads={"1": "CC1", "2": "GND"}, sym="Device:R"),
    dict(ref="R2", value="5.1k", fp="Resistor_SMD:R_0402_1005Metric", side="B", u=0.5, v=-13.9, rot=90,
         pads={"1": "CC2", "2": "GND"}, sym="Device:R"),
    dict(ref="F1", value="PTC 0.5A", fp="Fuse:Fuse_0805_2012Metric", side="B", u=0.0, v=-10.0, rot=90,
         pads={"1": "VBUS", "2": "+5V"}, sym="Device:Polyfuse_Small"),
    dict(ref="D1", value="SMF5.0A", fp="Diode_SMD:D_SOD-123F", side="B", u=-3.6, v=-10.0, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Diode:SMF5V0A"),
    dict(ref="C1", value="22uF", fp="Capacitor_SMD:C_0805_2012Metric", side="B", u=3.4, v=-10.0, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C"),
    # ---- bottom side: LDO at the left edge, away from the radar ----------
    dict(ref="U2", value="AP2112K-3.3", fp="Package_TO_SOT_SMD:SOT-23-5", side="B", u=-15.0, v=5.5, rot=0,
         pads={"1": "+5V", "2": "GND", "3": "+5V", "5": "+3V3"}, sym="Regulator_Linear:AP2112K-3.3"),
    dict(ref="C2", value="4.7uF", fp="Capacitor_SMD:C_0603_1608Metric", side="B", u=-15.0, v=2.6, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C"),
    dict(ref="C3", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", side="B", u=-15.0, v=9.0, rot=0,
         pads={"1": "+3V3", "2": "GND"}, sym="Device:C"),
    # ---- bottom side: ESP support under the module -----------------------
    dict(ref="R3", value="10k", fp="Resistor_SMD:R_0603_1608Metric", side="B", u=-6.0, v=9.2, rot=0,
         pads={"1": "+3V3", "2": "EN"}, sym="Device:R"),
    dict(ref="C6", value="1uF", fp="Capacitor_SMD:C_0603_1608Metric", side="B", u=-6.0, v=7.2, rot=0,
         pads={"1": "EN", "2": "GND"}, sym="Device:C"),
    dict(ref="R4", value="10k", fp="Resistor_SMD:R_0603_1608Metric", side="B", u=-6.0, v=3.1, rot=0,
         pads={"1": "+3V3", "2": "IO9"}, sym="Device:R"),
    dict(ref="R5", value="10k", fp="Resistor_SMD:R_0603_1608Metric", side="B", u=-6.0, v=5.1, rot=0,
         pads={"1": "+3V3", "2": "IO8"}, sym="Device:R"),
    dict(ref="R6", value="10k", fp="Resistor_SMD:R_0603_1608Metric", side="B", u=5.8, v=8.9, rot=0,
         pads={"1": "+3V3", "2": "IO2"}, sym="Device:R"),
    dict(ref="SW1", value="RESET", fp="Button_Switch_SMD:SW_SPST_B3U-1000P", side="B", u=13.0, v=4.0, rot=90,
         pads={"1": "EN", "2": "GND"}, sym="Switch:SW_Push"),
    # ---- bottom side: radar support next to its pin row ------------------
    dict(ref="C7", value="10uF", fp="Capacitor_SMD:C_0805_2012Metric", side="B", u=5.3, v=-6.9, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C"),
    dict(ref="C8", value="100nF", fp="Capacitor_SMD:C_0603_1608Metric", side="B", u=5.08, v=-5.0, rot=0,
         pads={"1": "+5V", "2": "GND"}, sym="Device:C"),
    dict(ref="R7", value="100k", fp="Resistor_SMD:R_0603_1608Metric", side="B", u=0.0, v=-5.0, rot=0,
         pads={"1": "RADAR_OUT", "2": "GND"}, sym="Device:R"),
] + [dict(ref=f"TP{i + 1}", value=val, fp=f"{L}:TestPoint_Pad_D1.4mm_Tight", side="B", u=-6.35 + 2.54 * i, v=11.0, rot=0,
          pads={"1": net}, sym="Connector:TestPoint")
     for i, (val, net) in enumerate([("3V3", "+3V3"), ("EN", "EN"), ("GND", "GND"), ("IO9", "IO9"), ("RX0", "RXD0"), ("TX0", "TXD0")])]

# board-only mounting holes (not in the schematic): M2, lower half, >25 mm from the antenna
HOLES = [("H1", -16.5, -10.5), ("H2", 16.5, -10.5)]

POWER_NETS = ["VBUS", "+5V", "+3V3", "GND"]
for p in PARTS:
    p["uuid"] = suuid("sym-" + p["ref"])
