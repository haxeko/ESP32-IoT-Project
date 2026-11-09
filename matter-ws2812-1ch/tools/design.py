"""Single source of truth for the c6_ws2812_1ch board: parts, nets, schematic placement.

ESP32-C6-MINI-1 controller for one WS2812/WS2811 output, 5-12 V input.
build_sch.py turns this into the KiCad schematic, build_pcb.py into the board.
"""

PROJECT = "c6_ws2812_1ch"
TITLE = "ESP32-C6 WS2812 1-channel Matter LED controller"
REV = "3.0"
DATE = "2026-11-09"
COMPANY = "Haxeko"

LOCAL_SYM_LIB = PROJECT          # project symbol library nickname
LOCAL_FP_LIB = PROJECT           # project footprint library nickname

# Power nets are drawn with KiCad power symbols, everything else with labels.
POWER_NETS = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V"}

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
TERM2 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-2-3.5-H_1x02_P3.50mm_Horizontal"
TERM3 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-3-3.5-H_1x03_P3.50mm_Horizontal"


def P(ref, lib_id, value, footprint, pins, at, rot=0, unit=None, mpn="", lcsc="",
      desc=None, dnp=False, extra_units=None, ref_at=None, val_at=None, in_bom=True):
    """One schematic symbol.

    pins: {pin_number: net or None(no-connect)}
    at:   (x, y) schematic position in mm, rot: 0/90/180/270
    extra_units: {unit: ((x, y), rot, {pin: net})} for multi-unit symbols
    """
    return dict(ref=ref, lib_id=lib_id, value=value, footprint=footprint, pins=pins,
                at=at, rot=rot, unit=unit or 1, mpn=mpn, lcsc=lcsc, desc=desc, dnp=dnp,
                extra_units=extra_units or {}, ref_at=ref_at, val_at=val_at, in_bom=in_bom)


# JLCPCB "basic" parts for the common values
LCSC_R0603 = {"10k": "C25804", "1k": "C21190", "5.1k": "C23186", "100": "C22775"}
LCSC_C = {("100nF", C0603): "C14663", ("100nF 50V", C0603): "C14663"}


def R(ref, value, n1, n2, at, rot=0, fp=R0603, mpn=None, lcsc=""):
    return P(ref, "Device:R", value, fp, {"1": n1, "2": n2}, at, rot,
             mpn=mpn or ("0603 1% " + value), lcsc=lcsc or LCSC_R0603.get(value, ""))


def C(ref, value, n1, n2, at, rot=0, fp=C0603, mpn=None, lcsc=""):
    return P(ref, "Device:C", value, fp, {"1": n1, "2": n2}, at, rot,
             mpn=mpn or ("MLCC " + value), lcsc=lcsc or LCSC_C.get((value, fp), ""))


# ESP32-C6-MINI-1 pin map (module pin numbers)
U1_GND = ["1", "2", "11", "14"] + [str(n) for n in range(36, 54)]
U1_NC = ["4", "7", "21", "32", "33", "34", "35"]             # NC pins of the module
# IO0 IO1 IO2 IO3 IO4 IO5 IO6 IO7 IO15 IO19 IO20
U1_UNUSED = ["12", "13", "5", "6", "9", "10", "15", "16", "20", "25", "26"]
U1_PINS = {"3": "+3V3", "8": "EN",
           "19": "DATA_IN",                                          # IO14
           "17": "USB_DN", "18": "USB_DP",                           # IO12 IO13
           "22": "STATUS_LED", "23": "BOOT",                         # IO8 IO9
           "24": "IO18",                                             # J7
           "27": "SW_POS1", "28": "SW_POS2", "29": "SW_POS3",        # IO21 IO22 IO23
           "30": "U0RXD", "31": "U0TXD"}
U1_PINS.update({p: "GND" for p in U1_GND})
U1_PINS.update({p: None for p in U1_NC + U1_UNUSED})

PARTS = [
    # ------------------------------------------------------------------ input + protection
    P("J1", "Connector:Screw_Terminal_01x02", "VIN 5-12V", TERM2,
      {"1": "VIN", "2": "GND"}, (40.64, 48.26),
      mpn="Terminal block 2P 3.5mm (Phoenix PT 1,5/2-3,5-H / KF350-3.5-2P)"),
    P("Q1", "Transistor_FET:SiSS27DN", "SiSS27DN", LOCAL_FP_LIB + ":Vishay_PowerPAK_1212-8_Single",
      {"4": "Q1_G", "5": "VIN", "1": "VLED", "2": "VLED", "3": "VLED"}, (68.58, 45.72), rot=90,
      mpn="SiSS27DN-T1-GE3",
      desc="P-MOSFET reverse polarity protection, -30V, 9mOhm max @ -4.5V",
      ref_at=(66.04, 36.83), val_at=(66.04, 34.29)),
    P("D4", "Device:D_Zener", "BZT52C15S", "Diode_SMD:D_SOD-323",
      {"1": "VLED", "2": "Q1_G"}, (81.28, 58.42), rot=270, mpn="BZT52C15S",
      desc="Gate-source clamp, 15V"),
    R("R1", "10k", "Q1_G", "GND", (64.77, 64.77)),
    P("D3", "Diode:SMF15A", "SMF15A", "Diode_SMD:D_SMF",
      {"1": "VLED", "2": "GND"}, (96.52, 58.42), rot=270, mpn="SMF15A",
      desc="200W TVS, 15V standoff"),
    P("C1", "Device:C_Polarized", "47uF 25V", "Capacitor_SMD:CP_Elec_5x5.4",
      {"1": "VLED", "2": "GND"}, (109.22, 58.42), mpn="47uF 25V 5x5.4 SMD electrolytic"),

    # ------------------------------------------------------------------ logic supply 3.3 V
    P("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
      {"1": "V_SYS", "2": "VLED"}, (160.02, 38.1), rot=180, mpn="B5819W", lcsc="C8598"),
    P("D2", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
      {"1": "V_SYS", "2": "VBUS"}, (160.02, 50.8), rot=180, mpn="B5819W", lcsc="C8598"),
    C("C3", "10uF 25V", "V_SYS", "GND", (182.88, 58.42), fp=C0805),
    C("C4", "100nF 50V", "V_SYS", "GND", (195.58, 58.42)),
    P("U2", "Regulator_Switching:AP63203WU", "AP63203WU-7", "Package_TO_SOT_SMD:TSOT-23-6",
      {"3": "V_SYS", "2": "V_SYS", "4": "GND", "5": "SW_NODE", "6": "BST", "1": "+3V3"},
      (220.98, 50.8), mpn="AP63203WU-7", lcsc="C780769",
      desc="3.3V 2A buck, 3.8-32V input"),
    C("C5", "100nF", "BST", "SW_NODE", (248.92, 38.1), rot=90),
    P("L1", "Device:L", "3.9uH", "Inductor_SMD:L_Sunlord_SWPA3015S",
      {"1": "SW_NODE", "2": "+3V3"}, (256.54, 48.26), rot=90, mpn="SWPA3015S3R9MT"),
    C("C6", "22uF 10V", "+3V3", "GND", (270.51, 58.42), fp=C0805),
    C("C7", "22uF 10V", "+3V3", "GND", (283.21, 58.42), fp=C0805),

    # ------------------------------------------------------------------ USB-C (flash / debug)
    P("J5", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
      LOCAL_FP_LIB + ":USB_C_Receptacle_HRO_TYPE-C-31-M-12_EdgeSilk",
      {"A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "S1": "GND",
       "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
       "A5": "CC1", "B5": "CC2", "A6": "USB_DP", "B6": "USB_DP",
       "A7": "USB_DN", "B7": "USB_DN", "A8": None, "B8": None},
      (40.64, 137.16), mpn="HRO TYPE-C-31-M-12", lcsc="C165948"),
    R("R2", "5.1k", "CC1", "GND", (72.39, 152.4)),
    R("R3", "5.1k", "CC2", "GND", (82.55, 152.4)),
    P("U5", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
      {"1": "USB_DP", "6": "USB_DP", "3": "USB_DN", "4": "USB_DN", "5": "VBUS", "2": "GND"},
      (106.68, 137.16), mpn="USBLC6-2SC6", lcsc="C7519",
      ref_at=(111.76, 128.27), val_at=(111.76, 148.59)),
    C("C8", "4.7uF 25V", "VBUS", "GND", (127.0, 152.4), fp=C0805),

    # ------------------------------------------------------------------ ESP32-C6
    P("U1", "RF_Module:ESP32-C6-MINI-1", "ESP32-C6-MINI-1-N4",
      LOCAL_FP_LIB + ":ESP32-C6-MINI-1_ThermalVias", U1_PINS,
      (205.74, 137.16), mpn="ESP32-C6-MINI-1-N4",
      desc="ESP32-C6 module, Wi-Fi 6 / BLE 5 / 802.15.4 (Thread, Zigbee), 4MB flash"),
    C("C10", "100nF", "+3V3", "GND", (170.18, 105.41)),
    R("R4", "10k", "+3V3", "EN", (240.03, 105.41)),
    C("C11", "1uF 10V", "EN", "GND", (252.73, 105.41)),
    R("R6", "10k", "+3V3", "STATUS_LED", (265.43, 105.41)),

    # ------------------------------------------------------------------ user I/O
    P("SW1", "Switch:SW_Push", "LINK / RESET", LOCAL_FP_LIB + ":SW_Push_1P1T_XKB_TS-1187A",
      {"1": "BOOT", "2": "GND"}, (157.48, 203.2), rot=270, mpn="XKB TS-1187A-B-A-B", lcsc="C318884",
      desc="Commissioning / factory reset (GPIO9, also BOOT)"),
    R("R8", "1k", "+3V3", "LED_A", (200.66, 198.12)),
    P("D5", "Device:LED", "Green", "LED_SMD:LED_0603_1608Metric",
      {"1": "STATUS_LED", "2": "LED_A"}, (200.66, 213.36), rot=90, mpn="LED 0603 green", lcsc="C72043",
      ref_at=(207.01, 212.09, "left"), val_at=(207.01, 214.63, "left")),
    P("SW2", "Switch:SW_SP3T", "ENDPOINTS 1/2/3", LOCAL_FP_LIB + ":SW_SP3T_PCM13_EdgeSilk",
      {"3": "GND", "1": "SW_POS1", "2": "SW_POS2", "4": "SW_POS3"}, (248.92, 203.2),
      mpn="C&K PCM13SMTR (SP3T, pos.1 = 1-3, pos.2 = 2-3, pos.3 = 3-4)", lcsc="C221842",
      desc="3-position slide switch, right angle: number of Matter endpoints (strip segments)"),
    P("J6", "Connector_Generic:Conn_01x06", "PAD NAP (mat duoi)", LOCAL_FP_LIB + ":ProgPads_1x06_P2.54mm_D1.5mm_SMD",
      {"1": "+3V3", "2": "EN", "3": "GND", "4": "BOOT", "5": "U0RXD", "6": "U0TXD"},
      (182.88, 236.22), mpn="(pads on the bottom side, nothing to mount)", in_bom=False,
      desc="UART0 programming pads: 3V3 EN GND IO9 RX0 TX0"),

    # ------------------------------------------------------------------ 5 V for level shifter
    P("U3", "Regulator_Linear:AP7381-50SA-7", "AP7381-50SA-7", "Package_TO_SOT_SMD:SOT-23",
      {"1": "VLED", "2": "+5V", "3": "GND"}, (330.2, 48.26), mpn="AP7381-50SA-7",
      desc="5V LDO, 40V input, powers the 74AHCT1G125 only when LED supply is present",
      ref_at=(330.2, 40.64), val_at=(330.2, 38.1)),
    C("C12", "1uF 50V", "VLED", "GND", (312.42, 58.42), fp=C0805),
    C("C13", "2.2uF 10V", "+5V", "GND", (347.98, 58.42)),

    # ------------------------------------------------------------------ level shifter + output
    P("U4", "74xGxx:74AHCT1G125", "74AHCT1G125", "Package_TO_SOT_SMD:SOT-23-5",
      {"2": "DATA_IN", "4": "DATA_BUF", "1": "GND", "5": "+5V", "3": "GND"},
      (342.9, 127.0), mpn="SN74AHCT1G125DBVR",
      ref_at=(349.25, 113.03, "left"), val_at=(349.25, 140.97, "left")),
    # 10k pull-down: GPIO14 floats after reset, keep the LED data low while booting
    R("R9", "10k", "DATA_IN", "GND", (322.58, 134.62)),
    R("R10", "100", "DATA_BUF", "DATA_OUT", (372.11, 127.0), rot=90),
    P("J2", "Connector:Screw_Terminal_01x03", "LED V+ D GND", TERM3,
      {"1": "VLED", "2": "DATA_OUT", "3": "GND"}, (397.51, 127.0),
      mpn="Terminal block 3P 3.5mm (Phoenix PT 1,5/3-3,5-H / KF350-3.5-3P)"),

    # ------------------------------------------------------------------ spare GPIO header
    P("J7", "Connector_Generic:Conn_01x03", "GPIO", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
      {"1": "+3V3", "2": "IO18", "3": "GND"}, (76.2, 210.82),
      mpn="Pin header 1x3 2.54mm",
      desc="Spare GPIO for custom control: 3V3 IO18 GND"),
]

# Short direct wires drawn instead of two labels: (ref1, pin1, ref2, pin2, net label, label mode)
# mode "on": label sits on the wire next to the first pin; "tail": wire continues past the
# second pin and ends in the label.
WIRES = [
    ("U2", "5", "L1", "1", "SW_NODE", "on"),
    ("L1", "2", "C6", "1", "+3V3", "on"),
    ("R8", "2", "D5", "2", "LED_A", "on"),
    ("U4", "2", "R9", "1", "DATA_IN", "tail"),
    ("U4", "4", "R10", "1", "DATA_BUF", "on"),
    ("R10", "2", "J2", "2", "DATA_OUT", "on"),
]

# PWR_FLAGs tell ERC which nets are supplied from outside the symbol pins
PWR_FLAGS = [("GND", (25.4, 64.77)), ("VIN", (25.4, 30.48)), ("VLED", (96.52, 30.48)),
             ("V_SYS", (182.88, 30.48)), ("+3V3", (283.21, 30.48)), ("VBUS", (127.0, 120.65))]

# Schematic frames: (title, x1, y1, x2, y2)
BLOCKS = [
    ("NGUON VAO 5-12V / CHONG CAM NGUOC / TVS", 15.24, 20.32, 137.16, 83.82),
    ("NGUON 3.3V (buck AP63203)", 142.24, 20.32, 292.1, 83.82),
    ("5V CHO IC DICH MUC (chi co khi co nguon LED)", 297.18, 20.32, 405.13, 83.82),
    ("USB-C: NAP CODE / DEBUG (USB Serial/JTAG)", 15.24, 91.44, 137.16, 177.8),
    ("ESP32-C6-MINI-1 (Wi-Fi 6 / Thread / Zigbee)", 142.24, 91.44, 292.1, 177.8),
    ("DICH MUC 3.3V -> 5V + NGO RA WS2812", 297.18, 91.44, 405.13, 177.8),
    ("MO RONG: 1 GPIO TU DO (J7)", 15.24, 182.88, 137.16, 245.11),
    ("NUT / LED / CONG TAC 3 NAC / PAD NAP", 142.24, 182.88, 292.1, 276.86),
]

NOTES = [
    ((147.32, 248.92),
     "SW1 (GPIO9 = BOOT): bam nhanh = bat/tat hoac vao che do ket noi; giu >5 s = factory reset.\n"
     "Giu SW1 trong luc cap nguon = vao che do nap ROM."),
    ((147.32, 259.08),
     "SW2 = C&K PCM13SMTR, chan 3 la chan chung (noi GND). Nac 1 noi 1-3, nac 2 noi 2-3, nac 3 noi 3-4.\n"
     "Firmware: IO21/IO22/IO23 la input, bat pull-up noi; chan nao doc 0 -> chia day LED thanh 1/2/3 doan,\n"
     "moi doan la mot endpoint Matter. Khong chan nao doc 0 (dang gat giua 2 nac) -> giu gia tri cu."),
    ((20.32, 78.74),
     "Q1 chong cam nguoc. Dong toi da 5 A. TVS D3 kep xung qua ap.\n"
     "Khong co cau chi: dung nguon co bao ve ngan mach."),
    ((302.26, 166.37),
     "DATA 5V: WS2812B (5V) va WS2811 (12V) deu nhan muc 5V."),
    ((20.32, 228.6),
     "IO18: GPIO thuong, pull-up yeu chi trong luc reset.\n"
     "Muc 3,3 V, khong chiu 5 V."),
    ((147.32, 152.4),
     "IO0-IO7, IO15, IO19, IO20:\nde trong. Anten module nho ra\nngoai mep board (khong de dong)."),
]
