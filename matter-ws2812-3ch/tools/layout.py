"""Board geometry: outline, footprint placement, copper zones (board coordinates in mm,
origin = top-left corner of the board, y down)."""

ORIGIN = (100.0, 100.0)          # board corner on the KiCad page
W, H = 84.0, 63.0
CORNER_R = 2.0

# ref: (x, y, rotation_deg)
PLACE = {
    # --- ESP32-C3 module: antenna on the top edge
    "U1": (42.0, 13.45, 0),
    "C9": (24.6, 8.6, 180),
    "C10": (29.8, 7.6, 180),
    "R4": (27.0, 10.6, 0),
    "C11": (27.0, 12.3, 0),
    "R6": (15.0, 18.5, 180),
    "R7": (18.0, 11.25, 0),
    "R5": (58.0, 24.5, 0),
    "J6": (42.0, 18.5, 0),     # bottom side, under the module
    # --- button + status LED (top left)
    "SW1": (7.5, 9.0, 0),
    "D5": (11.5, 18.5, 180),
    "R8": (8.0, 18.5, 0),
    # --- USB-C (right edge)
    "J5": (79.9, 15.0, 90),
    "U5": (68.5, 14.6, 0),
    "R2": (73.5, 22.0, 0),
    "R3": (73.5, 8.5, 0),
    "C8": (70.0, 19.5, 0),
    "D2": (64.0, 21.0, 0),
    # --- 3-position switch (right edge)
    "SW2": (79.0, 31.0, 90),
    "RN1": (71.5, 31.5, 0),
    # --- 3.3 V buck
    "U2": (14.0, 24.0, 0),
    "C4": (14.0, 26.9, 0),
    "C3": (14.0, 29.0, 0),
    "C5": (16.8, 21.7, 90),
    "L1": (20.0, 24.0, 0),
    "C6": (24.5, 22.3, 90),
    "C7": (24.5, 26.0, 90),
    "D1": (8.0, 33.0, 270),
    # --- VLED bulk / TVS
    "C1": (16.5, 36.0, 0),
    "D3": (27.5, 35.5, 90),
    "C2": (31.0, 38.2, 90),
    # --- level shifter + 5 V LDO
    "U4": (40.0, 30.0, 0),
    "C14": (45.5, 26.19, 0),
    "R9": (34.0, 27.46, 0),
    "R10": (34.0, 31.27, 0),
    "R11": (46.0, 32.54, 0),
    "R12": (34.0, 34.2, 0),
    "R13": (40.0, 36.6, 0),
    "R14": (46.0, 34.6, 0),
    "U3": (50.5, 31.0, 0),
    "C12": (50.5, 34.6, 0),
    "C13": (50.5, 27.6, 0),
    # --- input, reverse protection (tab = VIN faces J1, leads to the VLED bus)
    "J1": (10.5, 57.0, 0),
    "Q1": (10.0, 45.4, 180),
    "D4": (18.2, 46.6, 270),
    "R1": (20.3, 47.0, 90),
    # --- outputs, straight from the VLED bus
    "J2": (25.0, 57.0, 0),
    "J3": (43.0, 57.0, 0),
    "J4": (61.0, 57.0, 0),
    # --- spare GPIO header (right edge)
    "J7": (81.0, 41.6, 0),
    # --- mounting holes
    "H1": (3.5, 3.5, 0),
    "H2": (80.5, 3.5, 0),
    "H3": (3.5, 59.5, 0),
    "H4": (80.5, 59.5, 0),
}

# footprints mounted on the bottom side
BOTTOM = {"J6"}

# Bottom-layer routing costs more below this line: keep the GND return under the
# terminal row whole.
HC_Y = 46.0

# Copper zones: (net, layer, priority, [rectangles (x1, y1, x2, y2)])
POWER_ZONES = [
    ("VIN", "F.Cu", 2, [(6.0, 42.2, 13.0, 62.2)]),
    ("VLED", "F.Cu", 2, [(6.4, 39.0, 66.8, 41.9),      # bus, full width
                         (13.6, 41.9, 66.8, 45.0),     # bus right of the Q1 tab (Q1 source)
                         (6.4, 33.4, 14.6, 39.0),      # D1 anode, C1 +
                         (25.4, 36.2, 29.6, 39.0),     # D3 cathode
                         (22.8, 45.0, 27.2, 58.6),     # CH1 +
                         (40.8, 45.0, 45.2, 58.6),     # CH2 +
                         (58.8, 45.0, 63.2, 58.6)]),   # CH3 +
]

# Silkscreen text: (text, x, y, size, angle, layer)
SILK = [
    ("5-12V", 2.0, 45.4, 0.9, 90, "F.SilkS"),
    ("+", 10.5, 50.3, 1.2, 0, "F.SilkS"), ("VIN", 13.04, 50.3, 0.8, 0, "F.SilkS"),
    ("-", 15.58, 50.3, 1.2, 0, "F.SilkS"),
    ("+", 25.0, 50.3, 1.2, 0, "F.SilkS"), ("D", 30.08, 50.3, 1.0, 0, "F.SilkS"), ("-", 35.16, 50.3, 1.2, 0, "F.SilkS"),
    ("+", 43.0, 50.3, 1.2, 0, "F.SilkS"), ("D", 48.08, 50.3, 1.0, 0, "F.SilkS"), ("-", 53.16, 50.3, 1.2, 0, "F.SilkS"),
    ("+", 61.0, 50.3, 1.2, 0, "F.SilkS"), ("D", 66.08, 50.3, 1.0, 0, "F.SilkS"), ("-", 71.16, 50.3, 1.2, 0, "F.SilkS"),
    ("CH1", 32.6, 47.3, 1.2, 0, "F.SilkS"),
    ("CH2", 50.6, 47.3, 1.2, 0, "F.SilkS"),
    ("CH3", 68.6, 47.3, 1.2, 0, "F.SilkS"),
    ("LINK/RESET", 10.75, 16.25, 0.8, 0, "F.SilkS"),
    ("USB", 77.0, 22.0, 0.8, 0, "F.SilkS"),
    ("ENDPOINT", 83.0, 31.0, 0.8, 90, "F.SilkS"),
    ("3V3", 77.0, 41.6, 0.8, 0, "F.SilkS"), ("IO2", 77.0, 44.14, 0.8, 0, "F.SilkS"),
    ("IO7", 77.0, 46.68, 0.8, 0, "F.SilkS"), ("IO10", 77.0, 49.22, 0.8, 0, "F.SilkS"),
    ("GND", 77.0, 51.76, 0.8, 0, "F.SilkS"),
    ("GPIO", 83.2, 46.7, 0.8, 90, "F.SilkS"),
    ("c3_ws2812_3ch r1.1 - ESP32-C3 WS2812 3CH", 42.0, 52.5, 1.0, 0, "B.SilkS"),
]
