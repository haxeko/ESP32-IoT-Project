"""Board geometry: outline, footprint placement, copper zones (board coordinates in mm,
origin = top-left corner of the board, y down)."""

ORIGIN = (100.0, 100.0)          # board corner on the KiCad page
W, H = 48.0, 40.0
CORNER_R = 2.0

# ref: (x, y, rotation_deg)
PLACE = {
    # --- ESP32-C6-MINI-1: antenna sticks out over the top edge (edge at module y -5.75)
    "U1": (34.0, 5.75, 0),
    "J6": (34.0, 8.6, 0),          # bottom side, under the module (pad 1 at the left)
    "C10": (26.15, 3.3, 90),
    "R4": (25.4, 9.55, 0),
    "C11": (25.4, 11.35, 0),
    # --- USB-C in the top left corner
    "J5": (6.5, 3.55, 180),        # shell front 0.1 mm past the edge
    "U5": (6.5, 11.3, 90),
    "R3": (2.9, 10.8, 270),
    "R2": (10.1, 10.8, 270),
    "C8": (12.9, 8.0, 90),
    "D2": (13.6, 11.6, 180),
    # --- 3.3 V buck between USB-C and module
    "U2": (16.5, 4.0, 0),
    "C3": (16.5, 7.3, 0),
    "C4": (16.5, 9.3, 0),
    "C5": (18.7, 1.3, 0),
    "L1": (20.7, 4.0, 0),
    "C6": (23.9, 2.6, 90),
    "C7": (23.9, 6.4, 270),
    # --- right edge: endpoint switch (actuator over the edge), GPIO header
    "SW2": (45.65, 7.3, 90),
    "J7": (46.3, 15.5, 0),
    # --- button, status LED
    "SW1": (40.0, 16.1, 0),
    "D5": (37.8, 20.0, 0),
    "R8": (41.3, 20.0, 0),
    "R6": (41.3, 21.9, 0),
    # --- level shifter (inputs on top, outputs at the bottom), 5 V LDO
    "U4": (32.0, 18.6, 270),
    "C13": (36.6, 23.2, 90),
    "R9": (26.8, 14.6, 0),
    "R10": (26.8, 16.4, 0),
    "R11": (26.8, 18.2, 0),
    "RN3": (33.0, 24.6, 90),
    "U3": (39.8, 25.6, 180),
    "C12": (43.0, 27.4, 90),
    # --- input: reverse protection, VLED bulk / TVS
    "Q1": (4.2, 28.5, 270),
    "D4": (1.2, 25.6, 270),
    "R1": (1.15, 29.1, 270),
    "D1": (5.0, 23.6, 270),
    "C1": (11.5, 23.2, 90),
    "D3": (17.6, 23.6, 90),
    "C2": (20.6, 28.2, 90),
    # --- terminals: wire entry at the bottom edge
    "J1": (3.5, 34.6, 0),
    "J2": (11.9, 34.6, 0),
    "J3": (23.7, 34.6, 0),
    "J4": (35.5, 34.6, 0),
}

# reference designators placed by hand (x, y, angle); the rest are placed by build_pcb.py
REF_AT = {"Q1": (7.3, 28.0, 0)}

# footprints mounted on the bottom side
BOTTOM = {"J6"}

# Bottom-layer routing costs more below this line: keep the GND return under the
# terminal row whole.
HC_Y = 28.4

# Copper zones: (net, layer, priority, [rectangles (x1, y1, x2, y2)])
POWER_ZONES = [
    ("VIN", "F.Cu", 2, [(2.2, 28.0, 6.2, 32.6),         # Q1 drain
                        (1.6, 32.6, 5.0, 39.6)]),       # J1 +
    ("VLED", "F.Cu", 2, [(6.4, 28.4, 44.4, 33.1),       # bus along the terminal row
                         (3.6, 24.4, 13.0, 27.8),       # Q1 source, D1 anode, C1 +
                         (6.4, 27.8, 13.0, 28.4),
                         (16.3, 24.2, 18.9, 28.4),      # D3 cathode
                         (39.9, 26.1, 41.6, 28.4),      # U3 input
                         (42.1, 27.6, 43.9, 28.4),      # C12 +
                         (10.7, 33.1, 13.1, 34.6),      # CH1 +
                         (22.5, 33.1, 24.9, 34.6),      # CH2 +
                         (34.3, 33.1, 36.7, 34.6)]),    # CH3 +
]

# Silkscreen text: (text, x, y, size, angle, layer)
SILK = [
    ("+", 11.9, 30.25, 0.9, 0, "F.SilkS"), ("D", 15.4, 30.25, 0.8, 0, "F.SilkS"), ("-", 18.9, 30.25, 0.9, 0, "F.SilkS"),
    ("+", 23.7, 30.25, 0.9, 0, "F.SilkS"), ("D", 27.2, 30.25, 0.8, 0, "F.SilkS"), ("-", 30.7, 30.25, 0.9, 0, "F.SilkS"),
    ("+", 35.5, 30.25, 0.9, 0, "F.SilkS"), ("D", 39.0, 30.25, 0.8, 0, "F.SilkS"), ("-", 42.5, 30.25, 0.9, 0, "F.SilkS"),
    ("CH1", 13.65, 29.1, 0.8, 0, "F.SilkS"), ("CH2", 25.45, 29.1, 0.8, 0, "F.SilkS"),
    ("CH3", 37.25, 29.1, 0.8, 0, "F.SilkS"),
    ("+", 1.3, 30.9, 0.8, 0, "F.SilkS"), ("-", 7.0, 30.25, 0.9, 0, "F.SilkS"),
    ("3", 42.3, 5.8, 0.8, 0, "F.SilkS"), ("2", 42.3, 7.3, 0.8, 0, "F.SilkS"), ("1", 42.3, 8.8, 0.8, 0, "F.SilkS"),
    ("c6_ws2812_3ch r2.0", 24.0, 20.0, 1.0, 0, "B.SilkS"),
    ("ESP32-C6 WS2812 3CH", 24.0, 21.6, 1.0, 0, "B.SilkS"),
    ("VIN 5-12V", 5.0, 30.0, 0.8, 0, "B.SilkS"),
    ("SW1 LINK/RESET  SW2 ENDPOINT 1/2/3", 24.0, 24.0, 0.8, 0, "B.SilkS"),
    ("J7: 3V3 IO20 IO19 IO18 GND", 24.0, 25.4, 0.8, 0, "B.SilkS"),
]
