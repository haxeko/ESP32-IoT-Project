"""Single source of the KiCad design: parts, nets and schematic placement.

Chip-down variant of ../hardware: the ESP32-H2FH4 chip (QFN32, 4 MB in-package flash) instead of the
ESP32-H2-WROOM-02C module, with its 32 MHz crystal, 32.768 kHz crystal, RF matching and a printed antenna (TI AN043
meandered inverted F), and an addressable RGB LED (WS2812B type, power switched) instead of the red LED. The battery,
TPS63900, button, battery sense and test pads are the ones of the module variant (same designators and values).
Every script of this folder (gen_project.py, gen_sch.py, gen_pcb.py) reads this file, so the schematic and the board
always carry the same nets.
lcsc: LCSC / JLCPCB part number for the JLCPCB assembly BOM, from the JLCPCB Basic library where one fits; empty for
the parts LCSC does not stock (hand soldered) and for the parts not fitted.
"""

PROJECT = "matter-button-cr2032-chip"
LIB = "matter_button_chip"  # project-local symbol and footprint library (lib/)

TITLE = "ESP32-H2 Matter over Thread CR2032 button, chip and PCB antenna"
COMPANY = "Haxeko"
REV = "1.0"
DATE = "2026-10-29"

# Net names. Power nets drawn with power symbols in the schematic: GND, +3V3, VBAT
GND = "GND"
V33 = "+3V3"
VBAT = "VBAT"
# Net classes (KiCad net names): "Power" 0.3 mm tracks; "Ground" 0.3 mm, poured on the four layers, every GND pad
# has its own via (gen_pcb.py); "RF" 0.38 mm, 0.2 mm to the ground pour: 50 ohm grounded coplanar line over the L2
# ground of the 4-layer board (gen_project.py)
POWER_NETS = [V33, VBAT, "/BAT+", "/VBAT_Q", "/VOUT", "/LX1", "/LX2", "/LED_VDD"]
GROUND_NETS = [GND]
RF_NETS = ["/LNA_IN", "/RF_LINE", "/RF_ANT"]

# -------------------------------------------------------------------------------------------------------------------
# Parts. pins: pin number -> net name, "NC" (no-connect flag) or None (pin of type no_connect, left alone).
# sch: (x, y, rotation) on the A3 schematic sheet, mm, 1.27 mm grid.

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1210 = "Capacitor_SMD:C_1210_3225Metric"
L0402 = "Inductor_SMD:L_0402_1005Metric"
TP = "TestPoint:TestPoint_Pad_D1.0mm"
SJ = "Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm"


def cap(ref, value, net, sch, fp=C0402, mpn="CL05B104KO5NNNC", lcsc="C1525", desc="16 V X7R", **kw):
    """Capacitor from net to GND (decoupling, load capacitors)."""
    return dict(ref=ref, lib="Device:C", value=value, fp=fp, mpn=mpn, lcsc=lcsc, desc=desc,
                pins={"1": net, "2": GND}, sch=sch, **kw)


def tp(ref, value, net, sch):
    return dict(ref=ref, lib="Connector:TestPoint", value=value, fp=TP, mpn="", desc="test pad", pins={"1": net},
                sch=sch)


PARTS = [
    # ---- battery, reverse polarity protection, bulk (as the module variant)
    dict(ref="BT1", lib="Device:Battery_Cell", value="CR2032", fp=f"{LIB}:BatteryHolder_Keystone_3034_CR2032",
         mpn="Keystone 3034", lcsc="", desc="CR2032 holder, SMT", pins={"1": "BAT+", "2": GND}, sch=(40.64, 63.5, 0)),
    dict(ref="Q1", lib="Transistor_FET:Q_PMOS_GSD", value="AO3401A", fp="Package_TO_SOT_SMD:SOT-23",
         mpn="AO3401A", lcsc="C15127", desc="P-MOSFET -30 V, reverse polarity protection",
         pins={"1": GND, "2": "VBAT_Q", "3": "BAT+"}, sch=(66.04, 63.5, 0)),
    dict(ref="JP1", lib="Jumper:SolderJumper_2_Bridged", value="JP_IBAT", fp=SJ, mpn="",
         desc="Solder jumper, closed: cut to measure the battery current", pins={"1": "VBAT_Q", "2": VBAT},
         sch=(101.6, 63.5, 0)),
    cap("C1", "220uF", VBAT, (116.84, 63.5, 0), fp=C1210, mpn="GRM32ER60J227ME05L", lcsc="C469553",
        desc="6.3 V X5R bulk"),
    cap("C2", "220uF", VBAT, (127.0, 63.5, 0), fp=C1210, mpn="GRM32ER60J227ME05L", lcsc="C469553",
        desc="6.3 V X5R bulk"),
    # ---- TPS63900 buck-boost (as the module variant)
    dict(ref="U2", lib="Regulator_Switching:TPS63900", value="TPS63900DSKR",
         fp="Package_SON:WSON-10-1EP_2.5x2.5mm_P0.5mm_EP1.2x2mm", mpn="TPS63900DSKR", lcsc="C1518762",
         desc="Buck-boost 1.8-5.5 V in, 75 nA Iq",
         pins={"10": VBAT, "1": VBAT, "2": VBAT, "9": "LX1", "7": "LX2", "8": GND, "11": GND, "6": "VOUT",
               "3": "CFG1", "4": "CFG2", "5": "CFG3"}, sch=(180.34, 66.04, 0)),
    cap("C3", "10uF", VBAT, (147.32, 63.5, 0), fp=C0603, mpn="CL10A106KP8NNNC", lcsc="C19702",
        desc="10 V X5R, at U2 VIN"),
    dict(ref="L1", lib="Device:L", value="2.2uH", fp="Inductor_SMD:L_1008_2520Metric", mpn="DFE252012P-2R2M=P2",
         lcsc="C391305", desc="Murata 2.5x2.0 mm, Isat 3 A", pins={"1": "LX1", "2": "LX2"}, sch=(160.02, 91.44, 90)),
    dict(ref="R1", lib="Device:R", value="36.5k", fp=R0402, mpn="FRC0402F3652TS", lcsc="C49010116",
         desc="1 %, CFG1: VO(2) 3.3 V", pins={"1": "CFG1", "2": GND}, sch=(203.2, 81.28, 0)),
    dict(ref="R2", lib="Device:R", value="0R", fp=R0402, mpn="0402WGF0000TCE", lcsc="C17168",
         desc="CFG2: no input current limit", pins={"1": "CFG2", "2": GND}, sch=(213.36, 81.28, 0)),
    dict(ref="R3", lib="Device:R", value="0R", fp=R0402, mpn="0402WGF0000TCE", lcsc="C17168",
         desc="CFG3: VO(1) 1.8 V, unused (SEL = VIN selects VO(2))", pins={"1": "CFG3", "2": GND},
         sch=(223.52, 81.28, 0)),
    cap("C4", "22uF", "VOUT", (213.36, 48.26, 0), fp=C0603, mpn="CL10A226MQ8NRNC", lcsc="C59461",
        desc="6.3 V X5R, at U2 VOUT"),
    dict(ref="JP2", lib="Jumper:SolderJumper_2_Bridged", value="JP_I3V3", fp=SJ, mpn="",
         desc="Solder jumper, closed: cut to measure the 3.3 V current", pins={"1": "VOUT", "2": V33},
         sch=(233.68, 48.26, 0)),
    cap("C5", "10uF", V33, (251.46, 48.26, 0), fp=C0603, mpn="CL10A106KP8NNNC", lcsc="C19702",
        desc="10 V X5R, 3.3 V rail at JP2"),
    # ---- ESP32-H2FH4 chip
    dict(ref="U1", lib=f"{LIB}:ESP32-H2FH4", value="ESP32-H2FH4", fp=f"{LIB}:ESP32-H2_QFN-32-1EP_4x4mm_P0.4mm",
         mpn="ESP32-H2FH4", lcsc="C22470214", desc="Thread/Zigbee/BLE SoC, 4 MB in-package flash, QFN32 4x4",
         pins={"1": V33, "2": V33, "27": V33, "30": V33, "31": V33, "19": V33, "18": V33, "9": V33, "20": V33,
               "17": "EN", "29": "XTAL_P", "28": "XTAL_N", "15": "XTAL32K_P", "16": "XTAL32K_N", "32": "LNA_IN",
               "3": "NC", "4": "VBAT_SENSE", "5": "NC", "6": "NC", "7": "NC", "8": "NC", "10": "STRAP8",
               "11": "BOOT", "12": "BTN", "13": "LED_PWR", "14": "LED_DIN", "21": "NC", "22": "RXD0",
               "23": "TXD0_IC", "24": "NC", "25": "USB_DN", "26": "USB_DP", "33": GND},
         sch=(220.98, 190.5, 0)),
    # decoupling (ESP32-H2 reference schematic): 10u + 1u + 100n at VDD3P3 pins 1-2, 10n at the RF supply pins
    # 30-31, 1u + 100n at pin 27, 100n at VBAT/VDDA_PMU 18-19, VDDPST2 20, VDDPST1 9
    cap("C6", "10uF", V33, (114.3, 182.88, 0), fp=C0603, mpn="CL10A106KP8NNNC", lcsc="C19702",
        desc="10 V X5R, at U1 pins 1-2"),
    cap("C10", "1uF", V33, (124.46, 182.88, 0), mpn="CL05A105KA5NQNC", lcsc="C52923", desc="25 V X5R, at U1 pins 1-2"),
    cap("C11", "100nF", V33, (134.62, 182.88, 0), desc="16 V X7R, at U1 pins 1-2"),
    cap("C12", "10nF", V33, (144.78, 182.88, 0), mpn="CL05B103KB5NNNC", lcsc="C15195",
        desc="50 V X7R, at U1 pins 30-31"),
    cap("C13", "1uF", V33, (154.94, 182.88, 0), mpn="CL05A105KA5NQNC", lcsc="C52923", desc="25 V X5R, at U1 pin 27"),
    cap("C14", "100nF", V33, (165.1, 182.88, 0), desc="16 V X7R, at U1 pin 27"),
    cap("C15", "100nF", V33, (134.62, 205.74, 0), desc="16 V X7R, at U1 pins 18-19"),
    cap("C16", "100nF", V33, (144.78, 205.74, 0), desc="16 V X7R, at U1 pin 20"),
    cap("C17", "100nF", V33, (154.94, 205.74, 0), desc="16 V X7R, at U1 pin 9"),
    # EN delay (10k / 1 uF, Espressif recommendation), GPIO8 strapping pull-up, UART0 TX series resistor
    dict(ref="R4", lib="Device:R", value="10k", fp=R0402, mpn="0402WGF1002TCE", lcsc="C25744", desc="EN pull-up",
         pins={"1": V33, "2": "EN"}, sch=(114.3, 149.86, 0)),
    cap("C7", "1uF", "EN", (124.46, 149.86, 0), mpn="CL05A105KA5NQNC", lcsc="C52923", desc="25 V X5R, EN delay"),
    dict(ref="R5", lib="Device:R", value="10k", fp=R0402, mpn="0402WGF1002TCE", lcsc="C25744",
         desc="GPIO8 strapping pull-up", pins={"1": V33, "2": "STRAP8"}, sch=(139.7, 149.86, 0)),
    dict(ref="R10", lib="Device:R", value="510R", fp=R0402, mpn="0402WGF5100TCE", lcsc="C25123",
         desc="U0TXD series (harmonics), at U1 pin 23", pins={"1": "TXD0_IC", "2": "TXD0"},
         sch=(162.56, 149.86, 90)),
    # 32 MHz crystal, +-10 ppm: 24 nH in series at XTAL_P (chip side), load capacitors at the crystal (CL 12 pF)
    dict(ref="Y1", lib="Device:Crystal_GND24", value="32MHz", fp="Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
         mpn="7M32000035", lcsc="C90923", desc="TXC 32 MHz 3225, +-10 ppm, CL 12 pF",
         pins={"1": "XTAL_PL", "3": "XTAL_N", "2": GND, "4": GND}, sch=(127.0, 246.38, 0)),
    dict(ref="L2", lib="Device:L", value="24nH", fp=L0402, mpn="SDCL1005C24NJTDF", lcsc="C74301",
         desc="XTAL_P series, chip side", pins={"1": "XTAL_P", "2": "XTAL_PL"}, sch=(106.68, 236.22, 90)),
    cap("C18", "18pF", "XTAL_PL", (109.22, 256.54, 0), mpn="0402CG180J500NT", lcsc="C1549", desc="50 V C0G, Y1 load"),
    cap("C19", "18pF", "XTAL_N", (144.78, 256.54, 0), mpn="0402CG180J500NT", lcsc="C1549", desc="50 V C0G, Y1 load"),
    # 32.768 kHz crystal (RTC slow clock, sleep timing): ESR <= 70 kOhm, CL 12.5 pF
    dict(ref="Y2", lib="Device:Crystal", value="32.768kHz", fp="Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm",
         mpn="Q13FC13500004", lcsc="C32346", desc="Epson FC-135, CL 12.5 pF, ESR 70 kOhm",
         pins={"1": "XTAL32K_P", "2": "XTAL32K_N"}, sch=(172.72, 246.38, 0)),
    cap("C20", "18pF", "XTAL32K_P", (154.94, 256.54, 0), mpn="0402CG180J500NT", lcsc="C1549", desc="50 V C0G, Y2 load"),
    cap("C21", "18pF", "XTAL32K_N", (193.04, 256.54, 0), mpn="0402CG180J500NT", lcsc="C1549", desc="50 V C0G, Y2 load"),
    # ---- RF: chip matching (CLC low pass at the ANT pin, values in the range of the Espressif guidelines), 50 ohm
    # line, antenna tuning pi (series 0R, shunts not fitted until tuned with a network analyzer), printed antenna
    cap("C22", "1.5pF", "LNA_IN", (269.24, 147.32, 0), mpn="GRM1555C1H1R5BA01D", lcsc="C76957",
        desc="50 V C0G +-0.1 pF, shunt at U1 pin 32"),
    dict(ref="L3", lib="Device:L", value="2.4nH", fp=L0402, mpn="LQG15HS2N4S02D", lcsc="C97997",
         desc="Murata RF, series", pins={"1": "LNA_IN", "2": "RF_LINE"}, sch=(281.94, 139.7, 90)),
    cap("C23", "1.5pF", "RF_LINE", (294.64, 147.32, 0), mpn="GRM1555C1H1R5BA01D", lcsc="C76957",
        desc="50 V C0G +-0.1 pF, shunt"),
    cap("C24", "NC", "RF_LINE", (307.34, 147.32, 0), mpn="", lcsc="", desc="antenna tuning shunt, not fitted",
        dnp=True),
    dict(ref="R11", lib="Device:R", value="0R", fp=R0402, mpn="0402WGF0000TCE", lcsc="C17168",
         desc="antenna tuning series", pins={"1": "RF_LINE", "2": "RF_ANT"}, sch=(320.04, 139.7, 90)),
    cap("C25", "NC", "RF_ANT", (332.74, 147.32, 0), mpn="", lcsc="", desc="antenna tuning shunt, not fitted",
        dnp=True),
    dict(ref="AE1", lib="Device:Antenna_Shield", value="PCB antenna", fp=f"{LIB}:ANT_MIFA_TI-AN043", mpn="",
         desc="2.4 GHz meandered inverted F, TI AN043, copper on the board",
         pins={"1": "RF_ANT", "2": GND}, sch=(355.6, 134.62, 0)),
    # ---- button (GPIO10, LP IO), as the module variant
    dict(ref="R6", lib="Device:R", value="1M", fp=R0402, mpn="0402WGF1004TCE", lcsc="C26083", desc="button pull-up",
         pins={"1": V33, "2": "BTN_SW"}, sch=(289.56, 182.88, 0)),
    dict(ref="R7", lib="Device:R", value="1k", fp=R0402, mpn="0402WGF1001TCE", lcsc="C11702", desc="button series",
         pins={"1": "BTN_SW", "2": "BTN"}, sch=(304.8, 182.88, 0)),
    cap("C8", "1nF", "BTN", (320.04, 182.88, 0), mpn="0402B102K500NT", lcsc="C1523", desc="50 V X7R, button filter"),
    dict(ref="SW1", lib="Switch:SW_Push", value="PTS810", fp="Button_Switch_SMD:SW_SPST_PTS810",
         mpn="PTS810 SJM 250 SMTR LFS", lcsc="C116501",
         desc="C&K 4.2x3.2 mm tact switch, 160 gf", pins={"1": "BTN_SW", "2": GND}, sch=(304.8, 200.66, 0)),
    # ---- RGB status LED (WS2812B type) on GPIO12, supply switched by Q2 from GPIO11 (low = on; R8 keeps it off
    # through reset and sleep: the LED draws 0.7 mA even when dark)
    dict(ref="Q2", lib="Transistor_FET:Q_PMOS_GSD", value="AO3401A", fp="Package_TO_SOT_SMD:SOT-23",
         mpn="AO3401A", lcsc="C15127", desc="P-MOSFET, LED supply switch",
         pins={"1": "LED_PWR", "2": V33, "3": "LED_VDD"}, sch=(287.02, 228.6, 0)),
    dict(ref="R8", lib="Device:R", value="100k", fp=R0402, mpn="0402WGF1003TCE", lcsc="C25741",
         desc="Q2 gate pull-up: LED off", pins={"1": V33, "2": "LED_PWR"}, sch=(271.78, 223.52, 0)),
    dict(ref="R12", lib="Device:R", value="100R", fp=R0402, mpn="0402WGF1000TCE", lcsc="C25076",
         desc="LED data series", pins={"1": "LED_DIN", "2": "LED_DI"}, sch=(297.18, 248.92, 90)),
    cap("C26", "100nF", "LED_VDD", (304.8, 233.68, 0), desc="16 V X7R, at D1 VDD"),
    dict(ref="D1", lib=f"{LIB}:XL-2121RGBC-WS2812B", value="XL-2121RGBC-WS2812B",
         fp=f"{LIB}:LED_XL-2121RGBC-WS2812B_2.1x2.1mm", mpn="XL-2121RGBC-WS2812B", lcsc="C5349957",
         desc="XINGLIGHT 2.1x2.1 mm RGB LED, WS2812B protocol, 3.0-5.5 V",
         pins={"1": "LED_DI", "2": "LED_VDD", "3": "NC", "4": GND}, sch=(327.66, 238.76, 0)),
    # ---- battery sense (GPIO1)
    dict(ref="R9", lib="Device:R", value="1M", fp=R0402, mpn="0402WGF1004TCE", lcsc="C26083",
         desc="VBAT sense, no divider", pins={"1": VBAT, "2": "VBAT_SENSE"}, sch=(360.68, 195.58, 0)),
    cap("C9", "100nF", "VBAT_SENSE", (375.92, 195.58, 0), desc="16 V X7R, VBAT sense filter, at U1 pin 4"),
    # ---- test pads (bottom side)
    tp("TP1", "3V3", V33, (40.64, 152.4, 0)),
    tp("TP2", "GND", GND, (53.34, 152.4, 0)),
    tp("TP3", "TXD0", "TXD0", (66.04, 152.4, 0)),
    tp("TP4", "RXD0", "RXD0", (78.74, 152.4, 0)),
    tp("TP5", "EN", "EN", (40.64, 177.8, 0)),
    tp("TP6", "BOOT", "BOOT", (53.34, 177.8, 0)),
    tp("TP7", "USB_D-", "USB_DN", (66.04, 177.8, 0)),
    tp("TP8", "USB_D+", "USB_DP", (78.74, 177.8, 0)),
    tp("TP9", "VBAT", VBAT, (40.64, 203.2, 0)),
    tp("TP10", "VBAT_SENSE", "VBAT_SENSE", (53.34, 203.2, 0)),
]

# Test pads and solder jumpers are copper only, the antenna is part of the board: not in the bill of materials
for _p in PARTS:
    _p.setdefault("lcsc", "")
    _p.setdefault("bom", not _p["ref"].startswith(("TP", "JP", "AE")))

# Power flags (ERC: these nets are driven by passive parts only, LED_VDD by Q2): (net, x, y)
PWR_FLAGS = [(VBAT, 101.6, 81.28), (GND, 114.3, 81.28), (V33, 271.78, 35.56), ("LED_VDD", 345.44, 231.14)]

# Schematic section titles and notes: (x, y, text, size)
TEXTS = [
    (30.48, 35.56, "1  Battery CR2032, reverse polarity protection, bulk capacitors", 2.0),
    (30.48, 40.64, "Q1: drain to the cell, source to the load, gate to GND (an upside-down cell turns it off)", 1.27),
    (137.16, 35.56, "2  TPS63900 buck-boost, 3.3 V out, 75 nA quiescent", 2.0),
    (137.16, 106.68, "CFG1 36.5k + CFG2 0R: VO(2) = 3.3 V, no input current limit; CFG3 0R: VO(1) = 1.8 V, "
                     "unused: EN, SEL to VIN", 1.27),
    (30.48, 137.16, "4  Test pads (bottom side, pogo pins)", 2.0),
    (101.6, 137.16, "5  EN delay, strapping, UART0 TX", 2.0),
    (101.6, 165.1, "6  Decoupling at the pins", 2.0),
    (101.6, 170.18, "At the pins: C6 C10 C11 1-2, C12 30-31, C13 C14 27, C15 18-19, C16 20, C17 9", 1.27),
    (101.6, 223.52, "7  Crystals: 32 MHz +-10 ppm (24 nH series at XTAL_P), 32.768 kHz (RTC)", 2.0),
    (101.6, 228.6, "Load capacitors to be trimmed: TX tone frequency offset within +-10 ppm (Espressif guidelines)",
     1.27),
    (195.58, 137.16, "3  ESP32-H2FH4 (4 MB in-package flash)", 2.0),
    (195.58, 142.24, "VBAT (pin 18) is the chip RTC supply: tied to 3V3, not to the battery rail", 1.27),
    (256.54, 127.0, "8  RF: CLC at the chip (C22 L3 C23), 50 ohm line, antenna tuning pi (C24 R11 C25)", 2.0),
    (256.54, 160.02, "C24, C25 not fitted: R11 0R until the antenna is tuned (network analyzer, S11 at 2.44 GHz)",
     1.27),
    (279.4, 167.64, "9  Button GPIO10 (LP IO)", 2.0),
    (350.52, 175.26, "11  VBAT sense GPIO1", 2.0),
    (256.54, 213.36, "10  RGB LED GPIO12 (data), supply switched by Q2 from GPIO11 (low = on)", 2.0),
]


def nets():
    """net name -> list of (ref, pin)"""
    out = {}
    for p in PARTS:
        for pin, net in p["pins"].items():
            if net and net != "NC":
                out.setdefault(net, []).append((p["ref"], pin))
    return out
