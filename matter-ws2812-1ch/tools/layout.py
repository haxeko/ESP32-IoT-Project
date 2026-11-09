"""Board geometry: outline, footprint placement, copper zones (board coordinates in mm,
origin = top-left corner of the board, y down)."""

ORIGIN = (100.0, 100.0)          # board corner on the KiCad page
W, H = 39.0, 33.0
CORNER_R = 1.5

# ref: (x, y, rotation_deg)
PLACE = {
    # --- ESP32-C6-MINI-1: antenna sticks out over the top edge (edge at module y -5.75)
    "U1": (30.0, 5.75, 0),
    "J6": (30.0, 8.6, 0),          # bottom side, under the module (pad 1 at the left)
    "C10": (21.9, 2.6, 90),
    "R4": (22.2, 6.2, 90),
    "C11": (22.2, 9.5, 90),
    # --- USB-C in the top left corner
    "J5": (6.5, 3.55, 180),        # shell front 0.1 mm past the edge
    "U5": (6.5, 11.4, 90),
    "R3": (2.9, 11.0, 270),
    "R2": (10.1, 11.0, 270),
    "D2": (13.4, 11.6, 180),
    "C8": (17.4, 11.8, 90),
    # --- 3.3 V buck between USB-C and module
    "U2": (15.0, 3.8, 0),
    "C3": (15.0, 7.0, 0),
    "C4": (15.0, 8.9, 0),
    "C5": (17.1, 1.1, 0),
    "L1": (19.2, 3.8, 0),
    "C6": (19.2, 6.9, 0),
    "C7": (19.2, 9.0, 0),
    # --- input: reverse protection, VLED bulk / TVS
    "Q1": (4.2, 21.6, 270),
    "D4": (1.2, 18.8, 270),
    "R1": (0.95, 22.0, 270),
    "D1": (5.0, 16.4, 270),
    "C1": (9.25, 17.5, 90),
    "D3": (13.6, 17.0, 90),
    # --- 5 V LDO and level shifter above the output terminal
    "U3": (16.7, 15.6, 90),
    "C12": (17.0, 18.9, 0),
    "U4": (20.3, 15.6, 90),
    "R9": (20.3, 19.6, 0),
    "R10": (22.9, 17.2, 270),
    "C13": (17.6, 21.6, 0),
    # --- button, status LED
    "SW1": (31.0, 16.0, 0),
    "D5": (28.6, 20.6, 0),
    "R8": (31.8, 20.6, 0),
    "R6": (35.0, 20.6, 0),
    # --- bottom edge: terminals, GPIO header, endpoint switch (actuator over the edge)
    "J1": (3.5, 27.6, 0),
    "J2": (11.9, 27.6, 0),
    "J7": (23.2, 25.4, 0),
    "SW2": (31.15, 30.65, 0),
}

# reference designators placed by hand (x, y, angle) or hidden (None); the rest are
# placed by build_pcb.py
REF_AT = {"U1": (37.9, 2.6, 90), "U3": None}

# footprints mounted on the bottom side
BOTTOM = {"J6"}

# Bottom-layer routing costs more below this line: keep the GND return under the
# terminal row whole.
HC_Y = 24.0

# Copper zones: (net, layer, priority, [rectangles (x1, y1, x2, y2)])
POWER_ZONES = [
    ("VIN", "F.Cu", 2, [(2.2, 21.1, 6.2, 25.6),         # Q1 drain
                        (1.6, 25.6, 5.0, 32.6)]),       # J1 +
    ("VLED", "F.Cu", 2, [(3.6, 17.4, 13.1, 20.9),       # Q1 source, D1 anode, C1 +
                         (9.6, 20.9, 13.1, 27.6),       # down to J2 +
                         (12.4, 17.0, 16.1, 19.6)]),    # D3 cathode, U3 / C12 input
]

# Silkscreen text: (text, x, y, size, angle, layer)
SILK = [
    ("+", 11.9, 23.1, 0.9, 0, "F.SilkS"), ("D", 15.4, 23.1, 0.8, 0, "F.SilkS"), ("-", 18.9, 23.1, 0.9, 0, "F.SilkS"),
    ("+", 2.0, 23.4, 0.8, 0, "F.SilkS"), ("-", 7.0, 23.1, 0.9, 0, "F.SilkS"),
    ("1", 29.65, 27.2, 0.8, 0, "F.SilkS"), ("2", 31.15, 27.2, 0.8, 0, "F.SilkS"),
    ("3", 32.65, 27.2, 0.8, 0, "F.SilkS"),
    ("c6_ws2812_1ch r3.0", 19.0, 15.0, 1.0, 0, "B.SilkS"),
    ("ESP32-C6 WS2812 1CH", 19.0, 16.6, 1.0, 0, "B.SilkS"),
    ("VIN 5-12V", 5.25, 22.6, 0.8, 0, "B.SilkS"),
    ("LED V+ D GND", 15.4, 22.6, 0.8, 0, "B.SilkS"),
    ("J7: 3V3 IO18 GND", 23.2, 19.6, 0.8, 0, "B.SilkS"),
    ("SW1 LINK/RESET", 31.0, 21.0, 0.8, 0, "B.SilkS"),
    ("SW2 ENDPOINT 1/2/3", 31.15, 25.0, 0.8, 0, "B.SilkS"),
]
