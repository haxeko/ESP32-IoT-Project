"""Single source of truth for the c3_ws2812_3ch board: parts, nets, schematic placement.

ESP32-C3-WROOM-02 controller for three WS2812/WS2811 outputs, 5-12 V input.
build_sch.py turns this into the KiCad schematic, build_pcb.py into the board.
"""

PROJECT = "c3_ws2812_3ch"
TITLE = "ESP32-C3 WS2812 3-channel Matter LED controller"
REV = "1.0"
DATE = "2026-10-30"
COMPANY = "Haxeko"

LOCAL_SYM_LIB = PROJECT          # project symbol library nickname
LOCAL_FP_LIB = PROJECT           # project footprint library nickname

SW_SLIDE_FP = LOCAL_FP_LIB + ":SW_Slide_SP3T_SameSky_SLW-156646-5A-N-D"

# Power nets are drawn with KiCad power symbols, everything else with labels.
POWER_NETS = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V"}

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"


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


# JLCPCB "basic" parts for the common 0603 values
LCSC_R0603 = {"10k": "C25804", "1k": "C21190", "5.1k": "C23186", "100": "C22775"}
LCSC_C = {("100nF", C0603): "C14663", ("100nF 50V", C0603): "C14663"}


def R(ref, value, n1, n2, at, rot=0, fp=R0603, mpn=None, lcsc=""):
    return P(ref, "Device:R", value, fp, {"1": n1, "2": n2}, at, rot,
             mpn=mpn or ("0603 1% " + value), lcsc=lcsc or LCSC_R0603.get(value, ""))


def C(ref, value, n1, n2, at, rot=0, fp=C0603, mpn=None, lcsc=""):
    return P(ref, "Device:C", value, fp, {"1": n1, "2": n2}, at, rot,
             mpn=mpn or ("MLCC " + value), lcsc=lcsc or LCSC_C.get((value, fp), ""))


# Channel rows of the level shifter block
Y_CH = (104.14, 127.0, 149.86)

PARTS = [
    # ------------------------------------------------------------------ input + protection
    P("J1", "Connector:Screw_Terminal_01x02", "VIN 5-12V",
      "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
      {"1": "VIN", "2": "GND"}, (40.64, 48.26),
      mpn="Terminal block 2P 5.08mm (KF128-2P / MKDS 1,5/2-5,08)"),
    P("Q1", "Transistor_FET:SUD50P04-08", "SUD50P04-08",
      "Package_TO_SOT_SMD:TO-252-2",
      {"1": "Q1_G", "2": "VIN", "3": "VLED"}, (68.58, 45.72), rot=90,
      mpn="SUD50P04-08-GE3 (alt. AOD4185)",
      desc="P-MOSFET reverse polarity protection, -40V, 11.7mOhm @ -4.5V",
      ref_at=(66.04, 36.83), val_at=(66.04, 34.29)),
    P("D4", "Device:D_Zener", "BZT52C15", "Diode_SMD:D_SOD-123",
      {"1": "VLED", "2": "Q1_G"}, (83.82, 58.42), rot=270, mpn="BZT52C15",
      desc="Gate-source clamp, 15V"),
    R("R1", "10k", "Q1_G", "GND", (64.77, 64.77)),
    P("D3", "Diode:SMAJ15A", "SMBJ15A", "Diode_SMD:D_SMB",
      {"1": "VLED", "2": "GND"}, (99.06, 58.42), rot=270, mpn="SMBJ15A",
      desc="600W TVS, 15V standoff"),
    P("C1", "Device:C_Polarized", "470uF 25V", "Capacitor_SMD:CP_Elec_10x10.5",
      {"1": "VLED", "2": "GND"}, (111.76, 58.42), mpn="470uF 25V 10x10.5 SMD electrolytic"),
    C("C2", "100nF 50V", "VLED", "GND", (124.46, 58.42)),

    # ------------------------------------------------------------------ logic supply 3.3 V
    P("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
      {"1": "V_SYS", "2": "VLED"}, (160.02, 38.1), rot=180, mpn="B5819W", lcsc="C8598"),
    P("D2", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
      {"1": "V_SYS", "2": "VBUS"}, (160.02, 50.8), rot=180, mpn="B5819W", lcsc="C8598"),
    C("C3", "10uF 25V", "V_SYS", "GND", (182.88, 58.42), fp=C1206),
    C("C4", "100nF 50V", "V_SYS", "GND", (195.58, 58.42)),
    P("U2", "Regulator_Switching:AP63203WU", "AP63203WU-7", "Package_TO_SOT_SMD:TSOT-23-6",
      {"3": "V_SYS", "2": "V_SYS", "4": "GND", "5": "SW_NODE", "6": "BST", "1": "+3V3"},
      (220.98, 50.8), mpn="AP63203WU-7", lcsc="C780769",
      desc="3.3V 2A buck, 3.8-32V input"),
    C("C5", "100nF", "BST", "SW_NODE", (248.92, 38.1), rot=90),
    P("L1", "Device:L", "3.9uH", "Inductor_SMD:L_Sunlord_SWPA4030S",
      {"1": "SW_NODE", "2": "+3V3"}, (256.54, 48.26), rot=90, mpn="SWPA4030S3R9MT"),
    C("C6", "22uF 10V", "+3V3", "GND", (270.51, 58.42), fp=C0805),
    C("C7", "22uF 10V", "+3V3", "GND", (283.21, 58.42), fp=C0805),

    # ------------------------------------------------------------------ USB-C (flash / debug)
    P("J5", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
      "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
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

    # ------------------------------------------------------------------ ESP32-C3
    P("U1", "RF_Module:ESP32-C3-WROOM-02", "ESP32-C3-WROOM-02-N4",
      LOCAL_FP_LIB + ":ESP32-C3-WROOM-02_ThermalVia0.3",
      {"1": "+3V3", "9": "GND", "19": "GND", "2": "EN",
       "18": "SW_P1", "17": "SW_P2", "16": "SW_P3", "15": "SW_P4",
       "3": "DATA_IN1", "4": "DATA_IN2", "5": "DATA_IN3", "6": None,
       "7": "STATUS_LED", "8": "BOOT", "10": None,
       "13": "USB_DN", "14": "USB_DP", "11": "U0RXD", "12": "U0TXD"},
      (205.74, 140.97), mpn="ESP32-C3-WROOM-02-N4",
      ref_at=(210.82, 115.57), val_at=(229.87, 162.56)),
    C("C9", "10uF 10V", "+3V3", "GND", (157.48, 105.41), fp=C0805),
    C("C10", "100nF", "+3V3", "GND", (170.18, 105.41)),
    R("R4", "10k", "+3V3", "EN", (240.03, 105.41)),
    C("C11", "1uF 10V", "EN", "GND", (252.73, 105.41)),
    R("R5", "10k", "+3V3", "SW_P3", (262.89, 105.41)),
    R("R6", "10k", "+3V3", "STATUS_LED", (273.05, 105.41)),
    R("R7", "10k", "+3V3", "BOOT", (283.21, 105.41)),

    # ------------------------------------------------------------------ user I/O
    P("SW1", "Switch:SW_Push", "LINK / RESET", "Button_Switch_THT:SW_PUSH_6mm_H5mm",
      {"1": "BOOT", "2": "GND"}, (157.48, 203.2), rot=270, mpn="Tactile 6x6mm THT, H=5mm",
      desc="Commissioning / factory reset (GPIO9, also BOOT)"),
    R("R8", "1k", "+3V3", "LED_A", (200.66, 198.12)),
    P("D5", "Device:LED", "Green", "LED_SMD:LED_0603_1608Metric",
      {"1": "STATUS_LED", "2": "LED_A"}, (200.66, 213.36), rot=90, mpn="LED 0603 green", lcsc="C72043",
      ref_at=(205.74, 212.09, "left"), val_at=(205.74, 214.63, "left")),
    P("RN1", "Device:R_Pack04", "1k x4", "Resistor_SMD:R_Array_Convex_4x0603",
      {"1": "SW_P1", "2": "SW_P2", "3": "SW_P3", "4": "SW_P4",
       "8": "SWR_1", "7": "SWR_2", "6": "SWR_3", "5": "SWR_4"}, (226.06, 203.2), rot=270,
      mpn="4D03WGJ0102T5E (1k x4 0603 array)",
      ref_at=(226.06, 194.31), val_at=(226.06, 212.09)),
    P("SW2", LOCAL_SYM_LIB + ":SW_Slide_SP3T", "ENDPOINTS 1/2/3", SW_SLIDE_FP,
      {"1": "SWR_1", "2": "SWR_2", "3": "SWR_3", "4": "SWR_4"}, (271.78, 203.2),
      mpn="Same Sky SLW-156646-5A-N-D (SP3T, 4 pin, 3.2mm)",
      desc="3-position slide switch: number of Matter endpoints"),
    P("J6", "Connector_Generic:Conn_01x06", "PROG", LOCAL_FP_LIB + ":ProgPads_1x06_P2.54mm_D1.5mm_SMD",
      {"1": "+3V3", "2": "EN", "3": "GND", "4": "BOOT", "5": "U0RXD", "6": "U0TXD"},
      (182.88, 236.22), mpn="(pads on the bottom side, nothing to mount)", dnp=True, in_bom=False,
      desc="UART0 programming pads: 3V3 EN GND IO9 RX0 TX0"),

    # ------------------------------------------------------------------ 5 V for level shifter
    P("U3", "Regulator_Linear:AP7381-50SA-7", "AP7381-50SA-7", "Package_TO_SOT_SMD:SOT-23",
      {"1": "VLED", "2": "+5V", "3": "GND"}, (330.2, 48.26), mpn="AP7381-50SA-7",
      desc="5V LDO, 40V input, powers the 74AHCT125 only when LED supply is present",
      ref_at=(330.2, 40.64), val_at=(330.2, 38.1)),
    C("C12", "1uF 50V", "VLED", "GND", (312.42, 58.42), fp=C0805),
    C("C13", "2.2uF 10V", "+5V", "GND", (347.98, 58.42)),

    # ------------------------------------------------------------------ level shifter + outputs
    P("U4", "74xx:74AHCT125", "74AHCT125", "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
      {"2": "DATA_IN1", "1": "GND", "3": "DATA_BUF1"}, (335.28, Y_CH[0]), unit=1,
      mpn="SN74AHCT125DR",
      extra_units={
          2: ((335.28, Y_CH[1]), 0, {"5": "DATA_IN2", "4": "GND", "6": "DATA_BUF2"}),
          3: ((335.28, Y_CH[2]), 0, {"9": "DATA_IN3", "10": "GND", "8": "DATA_BUF3"}),
          4: ((335.28, 172.72), 0, {"12": "GND", "13": "GND", "11": None}),
          5: ((373.38, 52.07), 0, {"14": "+5V", "7": "GND"}),
      }),
    C("C14", "100nF", "+5V", "GND", (388.62, 52.07)),
    R("R9", "10k", "DATA_IN1", "GND", (317.5, Y_CH[0] + 6.35)),
    R("R10", "10k", "DATA_IN2", "GND", (317.5, Y_CH[1] + 6.35)),
    R("R11", "10k", "DATA_IN3", "GND", (317.5, Y_CH[2] + 6.35)),
    R("R12", "100", "DATA_BUF1", "DATA_OUT1", (360.68, Y_CH[0]), rot=90),
    R("R13", "100", "DATA_BUF2", "DATA_OUT2", (360.68, Y_CH[1]), rot=90),
    R("R14", "100", "DATA_BUF3", "DATA_OUT3", (360.68, Y_CH[2]), rot=90),
    P("J2", "Connector:Screw_Terminal_01x03", "CH1 V+ D GND",
      "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
      {"1": "VOUT1", "2": "DATA_OUT1", "3": "GND"}, (397.51, Y_CH[0]),
      mpn="Terminal block 3P 5.08mm (KF128-3P / MKDS 1,5/3-5,08)"),
    P("J3", "Connector:Screw_Terminal_01x03", "CH2 V+ D GND",
      "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
      {"1": "VOUT2", "2": "DATA_OUT2", "3": "GND"}, (397.51, Y_CH[1]),
      mpn="Terminal block 3P 5.08mm (KF128-3P / MKDS 1,5/3-5,08)"),
    P("J4", "Connector:Screw_Terminal_01x03", "CH3 V+ D GND",
      "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
      {"1": "VOUT3", "2": "DATA_OUT3", "3": "GND"}, (397.51, Y_CH[2]),
      mpn="Terminal block 3P 5.08mm (KF128-3P / MKDS 1,5/3-5,08)"),

    # ------------------------------------------------------------------ per-channel fuses
    P("F1", "Device:Fuse", "5A ATM", "Fuse:Fuseholder_Blade_Mini_Keystone_3568",
      {"1": "VLED", "2": "VOUT1"}, (76.2, 198.12), rot=90,
      mpn="Keystone 3568 holder + mini blade (ATM) fuse 5A"),
    P("F2", "Device:Fuse", "5A ATM", "Fuse:Fuseholder_Blade_Mini_Keystone_3568",
      {"1": "VLED", "2": "VOUT2"}, (76.2, 213.36), rot=90,
      mpn="Keystone 3568 holder + mini blade (ATM) fuse 5A"),
    P("F3", "Device:Fuse", "5A ATM", "Fuse:Fuseholder_Blade_Mini_Keystone_3568",
      {"1": "VLED", "2": "VOUT3"}, (76.2, 228.6), rot=90,
      mpn="Keystone 3568 holder + mini blade (ATM) fuse 5A"),

    # ------------------------------------------------------------------ mechanical
    P("H1", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, (40.64, 264.16), in_bom=False),
    P("H2", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, (60.96, 264.16), in_bom=False),
    P("H3", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, (81.28, 264.16), in_bom=False),
    P("H4", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, (101.6, 264.16), in_bom=False),
]

# Short direct wires drawn instead of two labels: (ref1, pin1, ref2, pin2, net label, label mode)
# mode "on": label sits on the wire next to the first pin; "tail": wire continues past the
# second pin and ends in the label.
WIRES = [
    ("U2", "5", "L1", "1", "SW_NODE", "on"),
    ("L1", "2", "C6", "1", "+3V3", "on"),
    ("U3", "2", "C13", "1", "+5V", "on"),
    ("R8", "2", "D5", "2", "LED_A", "on"),
    ("U4", "2", "R9", "1", "DATA_IN1", "tail"),
    ("U4", "5", "R10", "1", "DATA_IN2", "tail"),
    ("U4", "9", "R11", "1", "DATA_IN3", "tail"),
    ("U4", "3", "R12", "1", "DATA_BUF1", "on"),
    ("U4", "6", "R13", "1", "DATA_BUF2", "on"),
    ("U4", "8", "R14", "1", "DATA_BUF3", "on"),
    ("R12", "2", "J2", "2", "DATA_OUT1", "on"),
    ("R13", "2", "J3", "2", "DATA_OUT2", "on"),
    ("R14", "2", "J4", "2", "DATA_OUT3", "on"),
]

# PWR_FLAGs tell ERC which nets are supplied from outside the symbol pins
PWR_FLAGS = [("GND", (25.4, 64.77)), ("VIN", (25.4, 30.48)), ("VLED", (99.06, 30.48)),
             ("V_SYS", (182.88, 30.48)), ("+3V3", (283.21, 30.48)), ("VBUS", (127.0, 120.65))]

# Schematic frames: (title, x1, y1, x2, y2)
BLOCKS = [
    ("NGUON VAO 5-12V / CHONG CAM NGUOC / TVS", 15.24, 20.32, 137.16, 83.82),
    ("NGUON 3.3V (buck AP63203)", 142.24, 20.32, 292.1, 83.82),
    ("5V CHO IC DICH MUC (chi co khi co nguon LED)", 297.18, 20.32, 405.13, 83.82),
    ("USB-C: NAP CODE / DEBUG (USB Serial/JTAG)", 15.24, 91.44, 137.16, 177.8),
    ("ESP32-C3-WROOM-02", 142.24, 91.44, 292.1, 177.8),
    ("DICH MUC 3.3V -> 5V + 3 KENH WS2812", 297.18, 91.44, 405.13, 190.5),
    ("CAU CHI TUNG KENH", 15.24, 182.88, 137.16, 245.11),
    ("LO BAT VIT M3", 15.24, 250.19, 137.16, 276.86),
    ("NUT / LED / CONG TAC 3 NAC / PAD NAP", 142.24, 182.88, 292.1, 276.86),
]

NOTES = [
    ((147.32, 248.92),
     "SW1 (GPIO9 = BOOT): bam nhanh = bat/tat hoac vao che do ket noi; giu >5 s = factory reset.\n"
     "Giu SW1 trong luc cap nguon = vao che do nap ROM."),
    ((147.32, 259.08),
     "SW2 -> IO0..IO3 (IO2 la chan strap: R5 keo len, cong tac khong noi GND nen luon o muc 1 khi khoi dong).\n"
     "Firmware quet ca 4 chan (lan luot keo 1 chan xuong 0, doc 3 chan con lai, bat pull-up noi).\n"
     "Nac = so thu tu chan lon nhat trong cap dang noi - 1  ->  1, 2 hoac 3 endpoint Matter.\n"
     "Khong phu thuoc chan chung cua tung loai cong tac SP3T 4 chan."),
    ((20.32, 78.74),
     "Q1 chong cam nguoc, dong tong toi da 10 A. TVS D3 kep xung qua ap."),
    ((302.26, 184.15),
     "DATA 5V: WS2812B (5V) va WS2811 (12V) deu nhan muc 5V."),
    ((20.32, 238.76),
     "Moi kenh mot cau chi o to mini (ATM) rieng:\nchap 1 kenh khong lam mat cac kenh con lai."),
]
