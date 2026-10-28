"""Single source of the KiCad design: parts, nets and schematic placement.

Designators, values and part numbers are the ones of the README BOM (section 4). Every script of this folder
(gen_project.py, gen_sch.py, gen_pcb.py) reads this file, so the schematic and the board always carry the same nets.
lcsc: LCSC / JLCPCB part number for the JLCPCB assembly BOM, from the JLCPCB Basic library where one fits; empty for
the parts LCSC does not stock (hand soldered).
"""

PROJECT = "matter-button-cr2032"
LIB = "matter_button"  # project-local symbol and footprint library (lib/)

TITLE = "ESP32-H2 Matter over Thread CR2032 button"
COMPANY = "Haxeko"
REV = "1.0"
DATE = "2026-10-28"

# Net names. Power nets drawn with power symbols in the schematic: GND, +3V3, VBAT
GND = "GND"
V33 = "+3V3"
VBAT = "VBAT"
# Net classes (KiCad net names): "Power" 0.3 mm tracks; "Ground" 0.3 mm, poured on both layers, every GND pad
# has its own via (gen_pcb.py), so the autorouter leaves it alone
POWER_NETS = [V33, VBAT, "/BAT+", "/VBAT_Q", "/VOUT", "/LX1", "/LX2"]
GROUND_NETS = [GND]

# -------------------------------------------------------------------------------------------------------------------
# Parts. pins: pin number -> net name, "NC" (no-connect flag) or None (pin of type no_connect, left alone).
# sch: (x, y, rotation) on the A3 schematic sheet, mm, 1.27 mm grid.

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1210 = "Capacitor_SMD:C_1210_3225Metric"
TP = "TestPoint:TestPoint_Pad_D1.5mm"
SJ = "Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm"

PARTS = [
    # ---- battery, reverse polarity protection, bulk
    dict(ref="BT1", lib="Device:Battery_Cell", value="CR2032", fp=f"{LIB}:BatteryHolder_Keystone_3034_CR2032",
         mpn="Keystone 3034", lcsc="", desc="CR2032 holder, SMT", pins={"1": "BAT+", "2": GND}, sch=(40.64, 63.5, 0)),
    dict(ref="Q1", lib="Transistor_FET:Q_PMOS_GSD", value="AO3401A", fp="Package_TO_SOT_SMD:SOT-23",
         mpn="AO3401A", lcsc="C15127", desc="P-MOSFET -30 V, reverse polarity protection",
         pins={"1": GND, "2": "VBAT_Q", "3": "BAT+"}, sch=(66.04, 63.5, 0)),
    dict(ref="JP1", lib="Jumper:SolderJumper_2_Bridged", value="JP_IBAT", fp=SJ, mpn="",
         desc="Solder jumper, closed: cut to measure the battery current", pins={"1": "VBAT_Q", "2": VBAT},
         sch=(101.6, 63.5, 0)),
    dict(ref="C1", lib="Device:C", value="220uF", fp=C1210, mpn="GRM32ER60J227ME05L", lcsc="C469553",
         desc="6.3 V X5R bulk", pins={"1": VBAT, "2": GND}, sch=(116.84, 63.5, 0)),
    dict(ref="C2", lib="Device:C", value="220uF", fp=C1210, mpn="GRM32ER60J227ME05L", lcsc="C469553",
         desc="6.3 V X5R bulk", pins={"1": VBAT, "2": GND}, sch=(127.0, 63.5, 0)),
    # ---- TPS63900 buck-boost
    dict(ref="U2", lib="Regulator_Switching:TPS63900", value="TPS63900DSKR",
         fp="Package_SON:WSON-10-1EP_2.5x2.5mm_P0.5mm_EP1.2x2mm", mpn="TPS63900DSKR", lcsc="C1518762",
         desc="Buck-boost 1.8-5.5 V in, 75 nA Iq",
         pins={"10": VBAT, "1": VBAT, "2": VBAT, "9": "LX1", "7": "LX2", "8": GND, "11": GND, "6": "VOUT",
               "3": "CFG1", "4": "CFG2", "5": "CFG3"}, sch=(180.34, 66.04, 0)),
    dict(ref="C3", lib="Device:C", value="10uF", fp=C0603, mpn="CL10A106KP8NNNC", lcsc="C19702",
         desc="10 V X5R, at U2 VIN",
         pins={"1": VBAT, "2": GND}, sch=(147.32, 63.5, 0)),
    dict(ref="L1", lib="Device:L", value="2.2uH", fp="Inductor_SMD:L_1008_2520Metric", mpn="DFE252012P-2R2M=P2",
         lcsc="C391305", desc="Murata 2.5x2.0 mm, Isat 3 A", pins={"1": "LX1", "2": "LX2"}, sch=(160.02, 91.44, 90)),
    dict(ref="R1", lib="Device:R", value="36.5k", fp=R0402, mpn="FRC0402F3652TS", lcsc="C49010116",
         desc="1 %, CFG1: VO(2) 3.3 V",
         pins={"1": "CFG1", "2": GND}, sch=(203.2, 81.28, 0)),
    dict(ref="R2", lib="Device:R", value="0R", fp=R0402, mpn="0402WGF0000TCE", lcsc="C17168",
         desc="CFG2: no input current limit",
         pins={"1": "CFG2", "2": GND}, sch=(213.36, 81.28, 0)),
    dict(ref="R3", lib="Device:R", value="0R", fp=R0402, mpn="0402WGF0000TCE", lcsc="C17168",
         desc="CFG3: VO(1) 1.8 V, unused (SEL = VIN selects VO(2))",
         pins={"1": "CFG3", "2": GND}, sch=(223.52, 81.28, 0)),
    dict(ref="C4", lib="Device:C", value="22uF", fp=C0603, mpn="CL10A226MQ8NRNC", lcsc="C59461",
         desc="6.3 V X5R, at U2 VOUT",
         pins={"1": "VOUT", "2": GND}, sch=(213.36, 48.26, 0)),
    dict(ref="JP2", lib="Jumper:SolderJumper_2_Bridged", value="JP_I3V3", fp=SJ, mpn="",
         desc="Solder jumper, closed: cut to measure the 3.3 V current", pins={"1": "VOUT", "2": V33},
         sch=(233.68, 48.26, 0)),
    dict(ref="C5", lib="Device:C", value="10uF", fp=C0603, mpn="CL10A106KP8NNNC", lcsc="C19702",
         desc="10 V X5R, at U1 pin 1",
         pins={"1": V33, "2": GND}, sch=(251.46, 48.26, 0)),
    dict(ref="C6", lib="Device:C", value="100nF", fp=C0402, mpn="CL05B104KO5NNNC", lcsc="C1525",
         desc="16 V X7R, at U1 pin 1",
         pins={"1": V33, "2": GND}, sch=(261.62, 48.26, 0)),
    # ---- module
    dict(ref="U1", lib=f"{LIB}:ESP32-H2-WROOM-02C", value="ESP32-H2-WROOM-02C-H4S",
         fp=f"{LIB}:ESP32-H2-WROOM-02C", mpn="ESP32-H2-WROOM-02C-H4S", lcsc="", desc="Thread/BLE module, 4 MB flash",
         pins={"1": V33, "14": "NC", "2": "EN", "7": "STRAP8", "8": "BOOT", "5": "BTN", "6": "NC", "10": "NC",
               "11": "NC", "12": "NC", "16": None, "17": None, "18": None, "19": None,
               "28": "NC", "27": "VBAT_SENSE", "26": "NC", "25": "NC", "3": "NC", "4": "NC", "15": "LED",
               "20": "NC", "21": "RXD0", "22": "TXD0", "23": "USB_DN", "24": "USB_DP",
               "9": GND, "13": GND, "29": GND}, sch=(208.28, 177.8, 0)),
    dict(ref="R4", lib="Device:R", value="10k", fp=R0402, mpn="0402WGF1002TCE", lcsc="C25744", desc="EN pull-up",
         pins={"1": V33, "2": "EN"}, sch=(142.24, 154.94, 0)),
    dict(ref="C7", lib="Device:C", value="1uF", fp=C0402, mpn="CL05A105KA5NQNC", lcsc="C52923",
         desc="EN delay, not fitted",
         pins={"1": "EN", "2": GND}, sch=(154.94, 154.94, 0), dnp=True),
    dict(ref="R5", lib="Device:R", value="10k", fp=R0402, mpn="0402WGF1002TCE", lcsc="C25744",
         desc="GPIO8 strapping pull-up",
         pins={"1": V33, "2": "STRAP8"}, sch=(129.54, 154.94, 0)),
    # ---- button (GPIO10, LP IO)
    dict(ref="R6", lib="Device:R", value="1M", fp=R0402, mpn="0402WGF1004TCE", lcsc="C26083", desc="button pull-up",
         pins={"1": V33, "2": "BTN_SW"}, sch=(289.56, 152.4, 0)),
    dict(ref="R7", lib="Device:R", value="1k", fp=R0402, mpn="0402WGF1001TCE", lcsc="C11702", desc="button series",
         pins={"1": "BTN_SW", "2": "BTN"}, sch=(304.8, 152.4, 0)),
    dict(ref="C8", lib="Device:C", value="1nF", fp=C0402, mpn="CL05B102KB5NNNC", lcsc="C1523",
         desc="50 V X7R, button filter",
         pins={"1": "BTN", "2": GND}, sch=(320.04, 152.4, 0)),
    dict(ref="SW1", lib="Switch:SW_Push", value="PTS810", fp="Button_Switch_SMD:SW_SPST_PTS810",
         mpn="PTS810 SJM 250 SMTR LFS", lcsc="C116501",
         desc="C&K 4.2x3.2 mm tact switch, 160 gf", pins={"1": "BTN_SW", "2": GND},
         sch=(304.8, 170.18, 0)),
    # ---- LED (GPIO22)
    dict(ref="R8", lib="Device:R", value="1k", fp=R0402, mpn="0402WGF1001TCE", lcsc="C11702", desc="LED series",
         pins={"1": "LED", "2": "LED_A"}, sch=(289.56, 195.58, 0)),
    dict(ref="D1", lib="Device:LED", value="red", fp="LED_SMD:LED_0603_1608Metric", mpn="KT-0603R",
         lcsc="C2286", desc="Hubei KENTO 0603 red", pins={"1": GND, "2": "LED_A"}, sch=(307.34, 195.58, 180)),
    # ---- battery sense (GPIO1)
    dict(ref="R9", lib="Device:R", value="1M", fp=R0402, mpn="0402WGF1004TCE", lcsc="C26083",
         desc="VBAT sense, no divider",
         pins={"1": VBAT, "2": "VBAT_SENSE"}, sch=(289.56, 233.68, 0)),
    dict(ref="C9", lib="Device:C", value="100nF", fp=C0402, mpn="CL05B104KO5NNNC", lcsc="C1525",
         desc="16 V X7R, VBAT sense filter",
         pins={"1": "VBAT_SENSE", "2": GND}, sch=(304.8, 233.68, 0)),
    # ---- test pads (bottom side)
    dict(ref="TP1", lib="Connector:TestPoint", value="3V3", fp=TP, mpn="", desc="test pad", pins={"1": V33},
         sch=(40.64, 152.4, 0)),
    dict(ref="TP2", lib="Connector:TestPoint", value="GND", fp=TP, mpn="", desc="test pad", pins={"1": GND},
         sch=(53.34, 152.4, 0)),
    dict(ref="TP3", lib="Connector:TestPoint", value="TXD0", fp=TP, mpn="", desc="test pad", pins={"1": "TXD0"},
         sch=(66.04, 152.4, 0)),
    dict(ref="TP4", lib="Connector:TestPoint", value="RXD0", fp=TP, mpn="", desc="test pad", pins={"1": "RXD0"},
         sch=(78.74, 152.4, 0)),
    dict(ref="TP5", lib="Connector:TestPoint", value="EN", fp=TP, mpn="", desc="test pad", pins={"1": "EN"},
         sch=(40.64, 177.8, 0)),
    dict(ref="TP6", lib="Connector:TestPoint", value="BOOT", fp=TP, mpn="", desc="test pad", pins={"1": "BOOT"},
         sch=(53.34, 177.8, 0)),
    dict(ref="TP7", lib="Connector:TestPoint", value="USB_D-", fp=TP, mpn="", desc="test pad",
         pins={"1": "USB_DN"}, sch=(66.04, 177.8, 0)),
    dict(ref="TP8", lib="Connector:TestPoint", value="USB_D+", fp=TP, mpn="", desc="test pad",
         pins={"1": "USB_DP"}, sch=(78.74, 177.8, 0)),
    dict(ref="TP9", lib="Connector:TestPoint", value="VBAT", fp=TP, mpn="", desc="test pad", pins={"1": VBAT},
         sch=(40.64, 203.2, 0)),
    dict(ref="TP10", lib="Connector:TestPoint", value="VBAT_SENSE", fp=TP, mpn="", desc="test pad",
         pins={"1": "VBAT_SENSE"}, sch=(320.04, 231.14, 0)),
]

# Test pads and solder jumpers are copper only: not in the bill of materials
for _p in PARTS:
    _p.setdefault("lcsc", "")
    _p.setdefault("bom", not _p["ref"].startswith(("TP", "JP")))

# Power flags (ERC: these nets are driven by passive parts only): (net, x, y)
PWR_FLAGS = [(VBAT, 101.6, 81.28), (GND, 114.3, 81.28), (V33, 271.78, 35.56)]

# Schematic section titles and notes: (x, y, text, size)
TEXTS = [
    (30.48, 35.56, "1  Battery CR2032, reverse polarity protection, bulk capacitors", 2.0),
    (30.48, 40.64, "Q1: drain to the cell, source to the load, gate to GND (an upside-down cell turns it off)", 1.27),
    (137.16, 35.56, "2  TPS63900 buck-boost, 3.3 V out, 75 nA quiescent", 2.0),
    (137.16, 106.68, "CFG1 36.5k + CFG2 0R: VO(2) = 3.3 V, no input current limit; CFG3 0R: VO(1) = 1.8 V, "
                     "unused: EN, SEL to VIN", 1.27),
    (30.48, 137.16, "4  Test pads (bottom side, pogo pins)", 2.0),
    (116.84, 137.16, "5  EN pull-up, strapping", 2.0),
    (116.84, 142.24, "C7: not fitted (the module has its own 10k/1uF RC)", 1.27),
    (180.34, 137.16, "3  ESP32-H2-WROOM-02C", 2.0),
    (180.34, 142.24, "Pin 14 VBAT: leave open, tied to 3V3 inside the module", 1.27),
    (279.4, 137.16, "6  Button GPIO10 (LP IO, EXT1 wake-up)", 2.0),
    (279.4, 185.42, "7  LED GPIO22", 2.0),
    (279.4, 213.36, "8  Battery sense GPIO1 (ADC1_CH0), no divider", 2.0),
]


def nets():
    """net name -> list of (ref, pin)"""
    out = {}
    for p in PARTS:
        for pin, net in p["pins"].items():
            if net and net != "NC":
                out.setdefault(net, []).append((p["ref"], pin))
    return out
