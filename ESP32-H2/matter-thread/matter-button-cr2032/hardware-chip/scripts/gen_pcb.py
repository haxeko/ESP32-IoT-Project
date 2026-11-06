#!/usr/bin/env python3
"""Generate and check the PCB (KiCad 9 pcbnew API).

  python3 gen_pcb.py place     board from the schematic netlist: outline, placement, nets, the hand-routed tracks and
                               vias, rule areas, texts
  python3 gen_pcb.py pour      GND pours on the four layers, the 3.3 V plane, stitching vias (again after a change)
  python3 gen_pcb.py all       place + pour
  python3 gen_pcb.py sync      values and part numbers from the schematic onto the board (no placement)

The footprints, values, nets and symbol links come from the netlist that kicad-cli exports from the schematic,
as "Update PCB from Schematic" would do, so DRC with --schematic-parity checks the board against the schematic.
Every connection but GND is routed in ROUTES and VIAS below (the GND pours make the rest).

Board: 29.8 x 29.8 mm, 4 layers, 1.0 mm (JLCPCB JLC04101H-7628): L1 parts and signals, L2 solid ground (reference of
the RF line, the crystal and the chip), L3 the 3.3 V plane under the chip, a few long tracks and ground, L4 the CR2032
holder, test pads and ground.
The printed antenna (TI AN043) takes the top right corner, the ESP32-H2 sits left of its feed; the holder is on the
bottom side, right aligned, so that the test pads fit in the strip on its left.
"""

import math
import os
import re
import subprocess
import sys

import pcbnew

import design
from kicad_sexp import HW, KICAD_FP

PCB = os.path.join(HW, design.PROJECT + ".kicad_pcb")
SCH = os.path.join(HW, design.PROJECT + ".kicad_sch")
PRO = os.path.join(HW, design.PROJECT + ".kicad_pro")
# the project file as gen_project.py wrote it, read before pcbnew touches it: NewBoard and SaveBoard write every
# default setting into it
PRO_TEXT = open(PRO).read() if os.path.exists(PRO) else None
BUILD = os.path.join(HW, "build")  # intermediate files (git-ignored)

X0, Y0, W, H, R_CORNER = 100.0, 100.0, 29.8, 29.8, 0.3  # board outline, mm
THICKNESS = 1.0
COPPER = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
ANTENNA = (114.6, 105.2)  # left end of the antenna area on the edge of the ground: area x .. 129.8, y 100 .. 105.2
HOLDER = (117.8, 119.2)  # CR2032 holder centre, bottom side, in the bottom right corner
# bottom side, ring from the edge of the contact pad (8.9 mm) to just outside the cell (10 mm): the crimped edge of the
# cell is its + terminal, so no copper at all (tracks, vias, pour) where the cell slides in and sits
CELL_RING = (8.95, 10.3)

U1_AT, U1_ROT = (106.3, 107.0), 270  # ESP32-H2: RF pin 32 at the top of the right side, facing the antenna


def u1_pin(n, out=0.0):
    """Board position of the centre of U1 pin n (QFN32, 0.4 mm pitch), moved `out` mm outwards along the pad (0.3875:
    the outer end of the pad). Sides at 270 degrees: right 32 (top) .. 25, top 1 (right) .. 8, left 9 (top) .. 16,
    bottom 17 (left) .. 24."""
    k, side = (n - 1) % 8, (n - 1) // 8
    t = -1.4 + 0.4 * k
    x, y, dx, dy = [(-1.9875, t, -1, 0), (t, 1.9875, 0, 1), (1.9875, -t, 1, 0), (-t, -1.9875, 0, -1)][side]
    x, y = x + dx * out, y + dy * out
    a = math.radians(U1_ROT)
    return (round(U1_AT[0] + x * math.cos(a) + y * math.sin(a), 4),
            round(U1_AT[1] - x * math.sin(a) + y * math.cos(a), 4))


# ref: (x, y, rotation, side). Rotation counter-clockwise: 0 pad 1 left, 90 pad 1 down, 180 right, 270 up.
PLACE = {
    "AE1": (*ANTENNA, 0, "F"),
    "BT1": (*HOLDER, 0, "B"),
    "U1": (*U1_AT, U1_ROT, "F"),
    # RF: CLC at the chip on the line y 105.35 (shunts to the ground above), the line drops to y 106.4 under the short
    # leg of the antenna, antenna tuning pi C24 R11 C25 at the feed
    "C22": (109.1, 104.87, 90, "F"), "L3": (110.49, 105.35, 0, "F"), "C23": (111.88, 104.87, 90, "F"),
    "C24": (114.87, 105.92, 90, "F"), "R11": (116.26, 106.4, 0, "F"), "C25": (117.65, 106.88, 270, "F"),
    # right side of the chip, under the RF line: 10 nF at pins 30-31, 24 nH at XTAL_P, 32 MHz crystal, pin 27 (C14,
    # C13)
    "C12": (110.1, 106.2, 0, "F"), "L2": (112.48, 106.8, 0, "F"), "Y1": (113.3, 108.9, 180, "F"),
    "C18": (116.0, 108.38, 270, "F"), "C19": (112.2, 111.13, 270, "F"),
    "C14": (110.55, 109.58, 270, "F"), "C13": (111.0, 112.3, 0, "F"),
    # top side of the chip: pins 1-2 (C11, C10, C6) and VBAT sense at pin 4 (C9)
    "C11": (107.5, 102.95, 90, "F"), "C10": (108.3, 102.6, 90, "F"), "C6": (101.0, 103.0, 90, "F"),
    "C9": (106.5, 102.95, 90, "F"),
    # left side: VDDPST1 (C17), GPIO8 strap (R5), 32.768 kHz crystal at pins 15-16
    "C17": (103.5, 104.4, 90, "F"), "R5": (102.4, 104.88, 180, "F"),
    "Y2": (103.0, 110.4, 90, "F"), "C20": (101.1, 109.15, 180, "F"), "C21": (101.1, 111.65, 180, "F"),
    # bottom side of the chip: 3V3 via under pins 18-20 and their capacitors, UART TX series resistor; EN, RXD, TXD
    # and USB drop to the bottom side right at the chip (to the test pads)
    "C15": (105.85, 111.0, 270, "F"), "C16": (105.05, 111.4, 270, "F"), "R10": (107.75, 110.55, 270, "F"),
    # left strip, left of the cell (signal vias allowed): button RC at the BTN via, EN RC at the EN via between the
    # test pads, VBAT sense resistor at the VBAT and VBAT_SENSE vias (the test pads are below them, bottom side)
    "R7": (103.2, 115.5, 180, "F"), "C8": (102.69, 116.75, 270, "F"),
    "R4": (101.25, 118.27, 0, "F"), "C7": (103.85, 118.27, 0, "F"), "R9": (102.0, 127.2, 270, "F"),
    # RGB LED right of the button (data and gate tracks over the button, 3.3 V from below), button pull-up at the
    # lower left of the button
    "R12": (119.0, 113.0, 0, "F"), "D1": (121.2, 113.5, 180, "F"), "Q2": (120.4, 116.5, 0, "F"),
    "R8": (118.3, 116.5, 90, "F"), "C26": (122.8, 116.0, 90, "F"), "R6": (111.1, 117.9, 90, "F"),
    "SW1": (114.9, 115.4, 0, "F"),
    # TPS63900 (VOUT left, VIN right, CFG resistors above, L1 below) on the cell, 3.3 V output C5 JP2 on its left
    "C5": (111.65, 121.1, 90, "F"), "JP2": (111.65, 124.52, 90, "F"),
    "U2": (116.85, 123.9, 270, "F"), "R1": (116.85, 120.4, 90, "F"), "R2": (115.85, 120.4, 90, "F"),
    "R3": (114.85, 120.4, 90, "F"), "L1": (116.85, 127.25, 180, "F"), "C4": (113.75, 124.7, 90, "F"),
    "C3": (119.95, 124.7, 90, "F"),
    # battery input, bottom right: + tab of the holder (via on it) -> Q1 -> JP1 -> C1, C2 -> C3, VIN
    "Q1": (127.4, 123.3, 0, "F"), "JP1": (124.1, 124.25, 180, "F"), "C1": (126.4, 127.9, 180, "F"),
    "C2": (121.6, 127.9, 180, "F"),
    # test pads, bottom side (pogo pins), 1.0 mm on a 2.54 mm grid in the bottom left corner, left of the holder
    "TP6": (101.3, 117.0, 0, "B"), "TP4": (103.84, 117.0, 0, "B"),
    "TP5": (101.3, 119.54, 0, "B"), "TP3": (103.84, 119.54, 0, "B"),
    "TP1": (101.3, 122.08, 0, "B"), "TP7": (103.84, 122.08, 0, "B"),
    "TP9": (101.3, 124.62, 0, "B"), "TP8": (103.84, 124.62, 0, "B"),
    "TP10": (101.3, 127.16, 0, "B"), "TP2": (103.84, 127.16, 0, "B"),
}

# Tracks: every connection but GND (the pours connect it). Layers: F top, In2 (L3: 3.3 V trunk, a few long tracks),
# B bottom; no tracks on In1 (L2, solid ground).
# (net, width, [points], layer)
F, B, IN2 = "F", "B", "2"
ROUTES = [
    # RF: narrow at the pin (0.2 mm from pin 31), then 0.38 mm (50 ohm over L2): CLC, line, pi, feed of the antenna
    ("/LNA_IN", 0.2, [(108.2875, 105.6), (108.675, 105.6), (108.95, 105.35)], F),
    ("/LNA_IN", 0.38, [(108.95, 105.35), (110.005, 105.35)], F),
    ("/RF_LINE", 0.38, [(110.975, 105.35), (112.85, 105.35), (113.9, 106.4), (115.75, 106.4)], F),
    ("/RF_ANT", 0.38, [(116.77, 106.4), (117.65, 106.4), (117.65, 105.45)], F),
    ("GND", 0.3, [(109.1, 104.39), (109.1, 103.5)], F),                   # C22
    ("GND", 0.3, [(111.88, 104.39), (111.88, 103.5)], F),                 # C23
    ("GND", 0.3, [(117.65, 107.36), (117.65, 108.0)], F),                 # C25
    # right side: 3V3 via at the ends of pins 30-31, 10 nF beyond it; XTAL_P under it to the 24 nH, over the top of
    # the crystal to its pad 1; XTAL_N down the left side of the crystal to its pad 3
    ("+3V3", 0.2, [(108.2875, 106.0), (108.85, 106.0)], F),
    ("+3V3", 0.2, [(108.2875, 106.4), (108.85, 106.4)], F),
    ("+3V3", 0.3, [(109.025, 106.2), (109.62, 106.2)], F),
    ("GND", 0.3, [(110.58, 106.2), (111.25, 106.2)], F),                  # C12
    ("/XTAL_P", 0.2, [(108.2875, 106.8), (111.995, 106.8)], F),
    ("/XTAL_PL", 0.2, [(112.965, 106.8), (113.2, 106.8), (113.75, 107.35), (114.4, 107.9), (116.0, 107.9)], F),
    ("/XTAL_N", 0.2, [(108.2875, 107.2), (111.2, 107.2), (111.2, 109.6), (112.2, 109.6), (112.2, 110.65)], F),
    ("GND", 0.3, [(116.0, 108.86), (116.6, 108.6)], F),                   # C18
    # pin 27 via to the 3V3 plane, C14 below; USB down to vias (bottom side to the test pads)
    ("+3V3", 0.2, [(108.2875, 107.6), (109.75, 107.6), (110.2, 108.05), (110.2, 108.4)], F),
    ("+3V3", 0.3, [(110.2, 108.4), (110.55, 108.75), (110.55, 109.1)], F),
    ("GND", 0.3, [(110.55, 110.06), (111.25, 110.75)], F),                # C14
    # C13 (1 uF at pin 27) below C14: 3V3 to C14 and a via to the L3 plane, GND with C19 to the C14 via
    ("+3V3", 0.2, [(110.52, 112.3), (110.1, 111.88), (110.1, 111.25), (109.9, 111.05), (109.9, 109.6),
                   (110.35, 109.15)], F),
    ("GND", 0.3, [(111.48, 112.3), (111.3, 111.6), (111.25, 110.75)], F),
    ("GND", 0.3, [(112.2, 111.61), (111.48, 112.3)], F),
    ("/USB_DP", 0.2, [(108.2875, 108.0), (109.35, 108.0), (109.35, 108.9)], F),
    ("/USB_DN", 0.2, [(108.2875, 108.4), (108.95, 108.4), (108.95, 109.95)], F),
    # top side: 3V3 via at the ends of pins 1-2, C11 and C10 above; VBAT sense at pin 4: C9, via to the bottom side
    ("+3V3", 0.2, [(107.7, 105.0125), (107.7, 104.4)], F),
    ("+3V3", 0.2, [(107.3, 105.0125), (107.3, 104.4)], F),
    ("+3V3", 0.3, [(107.5, 104.275), (107.5, 103.43)], F),
    ("+3V3", 0.3, [(107.5, 103.43), (108.0, 103.08), (108.3, 103.08)], F),
    ("/VBAT_SENSE", 0.2, [(106.5, 105.0125), (106.5, 103.43), (105.6, 103.6)], F),  # via: bottom side to TP10
    # bottom side: 3V3 via under pins 18-20, C15 C16 below it; EN, RXD and (after R10) TXD to vias
    ("+3V3", 0.2, [(105.3, 108.9875), (105.3, 109.4), (105.6, 109.7)], F),
    ("+3V3", 0.2, [(105.7, 108.9875), (105.7, 109.55)], F),
    ("+3V3", 0.2, [(106.1, 108.9875), (106.1, 109.55)], F),
    ("+3V3", 0.3, [(105.85, 109.8), (105.85, 110.52)], F),
    ("+3V3", 0.3, [(105.85, 110.52), (105.05, 110.92)], F),
    ("GND", 0.3, [(105.85, 111.48), (105.05, 111.88), (104.75, 112.6)], F),
    ("/EN", 0.2, [(104.9, 108.9875), (104.9, 109.8), (104.8, 110.1)], F),
    ("/RXD0", 0.2, [(106.9, 108.9875), (106.9, 111.6), (106.95, 111.65)], F),
    ("/TXD0_IC", 0.2, [(107.3, 108.9875), (107.3, 109.6), (107.6, 109.9), (107.75, 110.04)], F),
    ("/TXD0", 0.2, [(107.75, 111.06), (107.75, 111.6), (107.48, 111.87), (107.48, 112.18)], F),
    ("/USB_DN", 0.2, [(108.95, 109.95), (108.95, 111.77), (108.01, 112.71)], F),
    ("/USB_DP", 0.2, [(109.35, 108.9), (109.45, 109.0), (109.45, 112.33), (108.54, 113.24)], F),
    # TPS63900 (U2 rotated: bottom row VOUT LX2 GND LX1 VIN, top row CFG3 CFG2 CFG1 SEL EN, from left)
    ("GND", 0.25, [(116.85, 125.1125), (116.85, 123.9)], F),                  # pin 8 to exposed pad
    ("GND", 0.3, [(116.85, 123.9), (115.05, 123.9), (113.75, 123.925)], F),   # EP, via, C4
    ("GND", 0.3, [(116.85, 123.9), (118.65, 123.9), (119.95, 123.925)], F),   # EP, via, C3
    ("/VOUT", 0.25, [(115.85, 125.1125), (115.85, 125.4), (113.75, 125.4)], F),  # pin 6 to C4
    ("/VOUT", 0.3, [(113.75, 125.475), (112.95, 125.17), (111.65, 125.17)], F),  # C4 to JP2
    ("VBAT", 0.25, [(117.85, 125.1125), (117.85, 125.4), (119.95, 125.4)], F),   # VIN to C3
    ("/LX1", 0.3, [(117.35, 125.1125), (117.35, 126.5)], F),
    ("/LX2", 0.3, [(116.35, 125.1125), (116.35, 126.5)], F),
    ("/CFG1", 0.2, [(116.85, 122.6875), (116.85, 120.91)], F),
    ("/CFG2", 0.2, [(116.35, 122.6875), (116.35, 122.0), (115.85, 121.5), (115.85, 120.91)], F),
    ("/CFG3", 0.2, [(115.85, 122.6875), (115.85, 122.3), (114.85, 121.3), (114.85, 120.91)], F),
    ("VBAT", 0.25, [(117.35, 122.4), (117.85, 122.4)], F),                    # SEL and EN to VIN
    ("VBAT", 0.25, [(117.85, 122.4), (117.85, 121.9), (118.15, 121.6), (120.95, 121.6), (120.95, 125.5),
                    (120.425, 125.5)], F),
    ("GND", 0.3, [(114.85, 119.89), (117.75, 119.89)], F),                    # R1-R3 to GND
    ("+3V3", 0.3, [(111.65, 121.875), (111.65, 123.87)], F),                  # C5 to JP2
    ("GND", 0.3, [(111.65, 120.325), (112.6, 120.0)], F),                     # C5
    # battery input: + tab (bottom side, via on it) -> Q1 -> JP1 -> bus -> C1, C2, C3, VIN
    ("/BAT+", 0.4, [(128.8, 121.2), (128.3375, 121.7), (128.3375, 123.3)], F),
    ("/VBAT_Q", 0.4, [(126.4625, 124.25), (124.75, 124.25)], F),
    ("GND", 0.3, [(126.4625, 122.35), (125.6, 122.1)], F),                    # Q1 gate
    ("VBAT", 0.4, [(123.45, 124.25), (123.45, 126.0)], F),
    ("VBAT", 0.4, [(127.875, 126.55), (127.875, 126.0), (120.95, 126.0), (120.95, 125.5)], F),
    ("VBAT", 0.4, [(123.075, 126.55), (123.075, 126.0)], F),
    ("VBAT", 0.3, [(127.875, 127.9), (126.4, 128.6)], F),                     # to the VBAT via (L3 to TP9, R9)
    ("GND", 0.3, [(124.925, 127.3), (125.95, 127.3)], F),                      # C1
    # left side: pin 9 to C17, 3V3 via, C6; GPIO8 strap R5 above; 32.768 kHz crystal under the left side (load
    # capacitors C20 C21 left of it); BOOT, button, LED gate and data pins to vias (L3)
    ("+3V3", 0.2, [(104.3125, 105.6), (103.7, 105.6), (103.5, 105.4), (103.5, 104.88)], F),
    ("+3V3", 0.3, [(102.91, 104.88), (102.4, 104.3), (102.4, 104.0)], F),
    ("+3V3", 0.3, [(102.91, 104.88), (103.5, 104.88)], F),                       # R5 to C17
    ("+3V3", 0.3, [(101.0, 103.775), (101.0, 104.0), (102.4, 104.0)], F),
    ("/STRAP8", 0.2, [(104.3125, 106.0), (101.89, 106.0), (101.89, 104.88)], F),
    ("/XTAL32K_P", 0.2, [(104.3125, 108.0), (103.6, 108.0), (103.6, 108.8)], F),
    ("/XTAL32K_P", 0.2, [(103.0, 109.15), (101.58, 109.15)], F),                 # C20
    ("/XTAL32K_N", 0.2, [(104.3125, 108.4), (104.2, 108.5), (104.2, 111.65), (103.0, 111.65)], F),
    ("/XTAL32K_N", 0.2, [(103.0, 111.65), (101.58, 111.65)], F),                 # C21
    ("GND", 0.3, [(100.62, 109.15), (100.62, 110.4), (102.6, 110.4)], F),       # C20, C21 to a via under Y2
    ("GND", 0.3, [(100.62, 111.65), (100.62, 110.4)], F),
    ("/BOOT", 0.2, [(104.3125, 106.4), (101.0, 106.4)], F),
    ("/BTN", 0.2, [(104.3125, 106.8), (101.5, 106.8), (101.1, 107.2)], F),
    ("/LED_PWR", 0.2, [(104.3125, 107.2), (102.3, 107.2), (101.85, 107.65)], F),
    ("/LED_DIN", 0.2, [(104.3125, 107.6), (103.1, 107.6), (102.6, 108.1)], F),
    # L3, left edge, under the 32.768 kHz crystal: BOOT, button, LED gate and data down to the left strip
    ("/BOOT", 0.2, [(101.0, 106.4), (100.44, 106.96), (100.44, 115.95), (101.9, 115.95)], IN2),
    ("/BTN", 0.2, [(101.1, 107.2), (100.83, 107.47), (100.83, 115.2), (101.2, 115.2)], IN2),
    ("/LED_PWR", 0.2, [(101.85, 107.65), (101.22, 108.28), (101.22, 114.4), (102.6, 114.4)], IN2),
    ("/LED_DIN", 0.2, [(102.6, 108.1), (101.61, 109.09), (101.61, 113.6), (103.5, 113.6)], IN2),
    # bottom side: VBAT sense from pin 4 along the left edge to TP10; BOOT to TP6; EN to its via between the test pads
    # and TP5; RXD, TXD, USB D-, D+ from the vias at the chip down to the second column of test pads
    ("/VBAT_SENSE", 0.2, [(105.6, 103.6), (104.6, 104.6), (101.0, 104.6), (100.44, 105.16), (100.44, 127.16),
                          (101.3, 127.16), (101.3, 128.5)], B),
    ("/BOOT", 0.2, [(101.9, 115.95), (101.3, 116.55), (101.3, 117.0)], B),
    ("/EN", 0.2, [(104.8, 110.1), (104.1, 110.8), (104.1, 114.7), (102.57, 116.23), (102.57, 118.27), (101.3, 119.54)],
     B),
    ("/RXD0", 0.2, [(106.95, 111.65), (104.63, 113.97), (104.63, 117.0), (103.84, 117.0)], B),
    ("/TXD0", 0.2, [(107.48, 112.18), (105.02, 114.64), (105.02, 119.54), (103.84, 119.54)], B),
    ("/USB_DN", 0.2, [(108.01, 112.71), (105.41, 115.31), (105.41, 122.08), (103.84, 122.08)], B),
    ("/USB_DP", 0.2, [(108.54, 113.24), (105.8, 115.98), (105.8, 124.62), (103.84, 124.62)], B),
    ("+3V3", 0.3, [(102.57, 123.35), (101.3, 122.08)], B),                       # TP1
    ("VBAT", 0.3, [(102.57, 125.89), (101.3, 124.62)], B),                       # TP9
    # left strip, top side: button RC, EN RC, VBAT sense resistor
    ("/BTN", 0.2, [(101.2, 115.2), (102.69, 115.2), (102.69, 116.27)], F),
    ("/EN", 0.2, [(101.76, 118.27), (103.37, 118.27)], F),
    ("VBAT", 0.3, [(102.57, 125.89), (102.0, 126.46), (102.0, 126.69)], F),
    ("/VBAT_SENSE", 0.2, [(101.3, 128.5), (102.0, 127.8), (102.0, 127.71)], F),
    # 3.3 V: C5 -> via -> L3 trunk up to the plane under the chip; on the top side on to TP1 (via) and R4, and from C5
    # under the button to R6 and the LED switch
    ("+3V3", 0.4, [(111.65, 122.6), (106.8, 122.6)], F),
    ("+3V3", 0.4, [(106.8, 122.6), (106.8, 112.8)], IN2),
    ("+3V3", 0.3, [(106.8, 122.6), (103.32, 122.6), (102.57, 123.35), (100.74, 121.52), (100.74, 118.27)], F),
    ("+3V3", 0.3, [(111.65, 121.875), (110.65, 121.875), (110.65, 118.85), (118.3, 118.85), (118.3, 117.01)], F),
    ("+3V3", 0.3, [(118.3, 117.45), (119.4625, 117.45)], F),                     # Q2 source
    ("+3V3", 0.3, [(111.1, 118.85), (111.1, 118.41)], F),                        # R6
    # VBAT on L3 from the C1 via along the bottom edge to TP9 and R9
    ("VBAT", 0.3, [(126.4, 128.6), (125.9, 129.1), (103.64, 129.1), (102.57, 128.03), (102.57, 125.89)], IN2),
    # LED data and gate from the left strip vias, under the chip vias, over the button to the LED; button: R7 to the
    # left contact of SW1 and to R6 (pull-up)
    ("/LED_DIN", 0.2, [(103.5, 113.6), (103.75, 113.85), (109.2, 113.85), (110.05, 113.0), (118.49, 113.0)], F),
    ("/LED_PWR", 0.2, [(102.6, 114.4), (109.5, 114.4), (110.3, 113.6), (117.95, 113.6), (117.95, 115.55),
                       (119.4625, 115.55)], F),
    ("/LED_PWR", 0.2, [(118.3, 115.55), (118.3, 115.99)], F),                    # R8
    ("/LED_DI", 0.2, [(119.51, 113.0), (120.35, 113.0)], F),
    ("/LED_VDD", 0.3, [(121.0, 116.5), (121.0, 114.6), (120.6, 114.2)], F),     # Q2 drain to D1 VDD
    ("/LED_VDD", 0.3, [(122.0, 116.5), (122.8, 116.5)], F),                      # C26
    ("GND", 0.3, [(122.05, 113.0), (122.9, 113.0), (123.3, 113.4), (123.3, 114.4)], F),
    ("GND", 0.3, [(122.8, 115.52), (123.3, 115.02), (123.3, 114.4)], F),
    ("/BTN_SW", 0.2, [(103.71, 115.5), (111.0, 115.5), (112.0, 114.5), (112.825, 114.5)], F),
    ("/BTN_SW", 0.2, [(111.0, 115.5), (111.1, 115.6), (111.1, 117.39)], F),
    ("/BTN_SW", 0.25, [(112.825, 114.325), (116.975, 114.325)], F),            # both pads of contact 1
    ("GND", 0.3, [(112.825, 116.475), (112.825, 117.6)], F),                     # SW1
    ("GND", 0.3, [(116.975, 116.475), (116.975, 117.6)], F),
]
# vias: (net, x, y); GND vias inside the contact pad of the cell or outside the cell, never in CELL_RING
VIAS = [
    # RF shunts, decoupling, crystals
    ("GND", 109.1, 103.5), ("GND", 111.88, 103.5), ("GND", 117.65, 108.0), ("GND", 111.25, 106.2),
    ("GND", 116.6, 108.6), ("GND", 111.25, 110.75), ("GND", 103.5, 103.2), ("GND", 102.6, 110.4),
    ("GND", 104.75, 112.6),
    ("GND", 107.5, 101.75), ("GND", 108.3, 101.4), ("GND", 106.5, 101.75), ("GND", 101.0, 101.4),
    ("+3V3", 109.025, 106.2), ("+3V3", 110.2, 108.4), ("+3V3", 107.5, 104.275), ("+3V3", 105.85, 109.8),
    ("+3V3", 102.4, 104.0), ("+3V3", 110.1, 111.25),
    # chip signals: BOOT, button, LED gate and data to L3 at the left side; EN, RXD, TXD, USB to the bottom side at
    # the bottom side of the chip, VBAT sense at the top side
    ("/BOOT", 101.0, 106.4), ("/BTN", 101.1, 107.2), ("/LED_PWR", 101.85, 107.65), ("/LED_DIN", 102.6, 108.1),
    ("/EN", 104.8, 110.1), ("/RXD0", 106.95, 111.65), ("/TXD0", 107.48, 112.18), ("/USB_DN", 108.01, 112.71),
    ("/USB_DP", 108.54, 113.24), ("/VBAT_SENSE", 105.6, 103.6),
    # left strip: L3 to the top side (LED data and gate, button) and to the bottom side (BOOT), EN to its RC between
    # the test pads, 3V3, VBAT and VBAT_SENSE at their test pads; 3.3 V from the regulator down to the L3 trunk
    ("/LED_DIN", 103.5, 113.6), ("/LED_PWR", 102.6, 114.4), ("/BTN", 101.2, 115.2), ("/BOOT", 101.9, 115.95),
    ("/EN", 102.57, 118.27), ("+3V3", 102.57, 123.35), ("VBAT", 102.57, 125.89), ("/VBAT_SENSE", 101.3, 128.5),
    ("+3V3", 106.8, 122.6),
    # LED, button
    ("GND", 123.3, 114.4), ("GND", 112.825, 117.6), ("GND", 116.975, 117.6),
    # TPS63900, battery input (GND vias on the contact pad of the cell)
    ("GND", 115.05, 123.9), ("GND", 118.65, 123.9), ("GND", 117.75, 119.89), ("GND", 112.6, 120.0),
    ("/BAT+", 128.8, 121.2), ("GND", 125.6, 122.1), ("VBAT", 126.4, 128.6), ("GND", 125.95, 127.3),
]

# L3 (In2.Cu): 3.3 V plane under the chip, every 3V3 via of the decoupling drops into it (ground elsewhere on L3)
ISLAND_3V3 = [(102.0, 102.4), (111.6, 102.4), (111.6, 113.0), (103.1, 113.0), (103.1, 105.6), (102.0, 105.6)]

# reference designators shown on the silkscreen: ref -> (x, y) or None (footprint default)
SILK_REFS = {}

# silkscreen labels of the test pads (bottom side), below each pad
TP_LABEL = {"TP1": "3V3", "TP2": "GND", "TP3": "TX", "TP4": "RX", "TP5": "EN", "TP6": "BOOT", "TP7": "D-",
            "TP8": "D+", "TP9": "VBAT", "TP10": "VSNS"}

# silkscreen texts: (text, x, y, layer, size, thickness, angle)
# (top: bottom left corner; bottom: under the antenna, where the bottom side has no copper)
TEXTS = [("Thread Button", 106.6, 128.75, F, 0.8, 0.12, 0), ("Made by Haxeko", 121.4, 101.9, B, 0.9, 0.13, 0),
         (f"rev {design.REV}", 121.4, 103.5, B, 0.6, 0.1, 0)]


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# ------------------------------------------------------------------------------------------------- netlist
def read_netlist():
    os.makedirs(BUILD, exist_ok=True)
    path = os.path.join(BUILD, design.PROJECT + ".net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", path, SCH],
                   check=True, capture_output=True)
    text = open(path).read()
    comps = {}
    for blk in text.split("(comp ")[1:]:
        blk = blk.split("(libparts")[0]
        ref = re.search(r'\(ref "([^"]+)"\)', blk).group(1)
        comps[ref] = dict(
            value=re.search(r'\(value "([^"]*)"\)', blk).group(1),
            footprint=re.search(r'\(footprint "([^"]*)"\)', blk).group(1),
            tstamp=re.findall(r'\(tstamps "([^"]+)"\)', blk)[-1],
            fields=dict(re.findall(r'\(field \(name "([^"]+)"\)\s*"([^"]*)"\)', blk)),
            dnp='(property (name "dnp")' in blk)
    nets = {}
    for blk in text.split("(net (code")[1:]:
        name = re.search(r'\(name "([^"]*)"\)', blk).group(1)
        nets[name] = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk)
    return comps, nets


def load_footprint(lib_id):
    nick, name = lib_id.split(":")
    path = os.path.join(HW, "lib", nick + ".pretty") if nick == design.LIB else \
        os.path.join(KICAD_FP, nick + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint {lib_id} not found in {path}")
    fp.SetFPID(pcbnew.LIB_ID(nick, name))
    return fp


def save(board):
    """SaveBoard, then the project file back as gen_project.py wrote it (PRO_TEXT)."""
    pcbnew.SaveBoard(PCB, board)
    if PRO_TEXT is not None:
        with open(PRO, "w") as f:
            f.write(PRO_TEXT)


# --------------------------------------------------------------------------------------------------- board
def add_outline(board):
    """Rectangle with rounded corners on Edge.Cuts."""
    x1, y1, x2, y2, r = X0, Y0, X0 + W, Y0 + H, R_CORNER

    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*a))
        s.SetEnd(pt(*b))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    seg((x1 + r, y1), (x2 - r, y1))
    seg((x2, y1 + r), (x2, y2 - r))
    seg((x2 - r, y2), (x1 + r, y2))
    seg((x1, y2 - r), (x1, y1 + r))
    k = r * (1 - math.sqrt(0.5))
    for cx, cy, sx, sy, ex, ey, mx, my in [
            (x1 + r, y1 + r, x1, y1 + r, x1 + r, y1, x1 + k, y1 + k),
            (x2 - r, y1 + r, x2 - r, y1, x2, y1 + r, x2 - k, y1 + k),
            (x2 - r, y2 - r, x2, y2 - r, x2 - r, y2, x2 - k, y2 - k),
            (x1 + r, y2 - r, x1 + r, y2, x1, y2 - r, x1 + k, y2 - k)]:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(pt(sx, sy), pt(mx, my), pt(ex, ey))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)


def polygon(zone, pts):
    ol = zone.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(mm(x), mm(y))


def add_rule_area(board, name, layers, pts, tracks=False, vias=False, pour=False):
    """Rule area: what is NOT allowed inside."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowCopperPour(pour)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    polygon(z, pts)
    board.Add(z)


def add_track(board, net, width, pts, layer):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a))
        t.SetEnd(pt(*b))
        t.SetWidth(mm(width))
        t.SetLayer(layer)
        t.SetNet(net)
        t.SetLocked(True)
        board.Add(t)


def add_via(board, net, x, y, locked=True):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetWidth(mm(0.6))
    v.SetDrill(mm(0.3))
    v.SetNet(net)
    v.SetLocked(locked)
    board.Add(v)


def add_text(board, text, x, y, layer, size=0.8, thick=0.12, angle=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(thick))
    t.SetTextAngleDegrees(angle)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu):
        t.SetMirrored(True)
    board.Add(t)


def set_fields(fp, c, side):
    """Schematic fields kept on the footprint (hidden, on the fab layer): part numbers for the BOM and the CPL."""
    for k in ("MPN", "LCSC", "Description"):
        if c["fields"].get(k):
            fp.SetField(k, c["fields"][k])
            f = fp.GetFieldByName(k)
            f.SetVisible(False)
            f.SetLayer(pcbnew.B_Fab if side == "B" else pcbnew.F_Fab)
        elif fp.HasField(k):
            fp.RemoveField(k)
    fp.SetDNP(c["dnp"])


def place():
    comps, nets = read_netlist()
    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(4)
    board.GetDesignSettings().SetBoardThickness(mm(THICKNESS))
    tb = board.GetTitleBlock()
    tb.SetTitle(design.TITLE)
    tb.SetRevision(design.REV)
    tb.SetCompany(design.COMPANY)
    tb.SetDate(design.DATE)

    netinfo = {}
    for name in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni
    pin_net = {(ref, pin): name for name, nodes in nets.items() for ref, pin in nodes}

    missing = set(comps) ^ set(PLACE)
    assert not missing, f"placement table and schematic differ: {missing}"
    for ref, c in sorted(comps.items()):
        fp = load_footprint(c["footprint"])
        board.Add(fp)
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + c["tstamp"]))
        fp.SetSheetname("/")
        fp.SetSheetfile(os.path.basename(SCH))
        x, y, rot, side = PLACE[ref]
        set_fields(fp, c, side)
        fp.Value().SetVisible(False)  # values are in the BOM; the assembly drawing shows the designators
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        if side == "B":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        for pad in fp.Pads():
            net = pin_net.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(netinfo[net])
            if ref == "BT1" and pad.GetNumber() == "2":  # coin cell contact: solid, no thermal spokes
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        # reference designators on the silkscreen for the larger parts only; the 0402/0603 passives are too
        # dense, see the assembly drawing (F.Fab) instead; the test pads get their function as label
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Reference().SetVisible(ref in SILK_REFS)
        if ref in SILK_REFS and SILK_REFS[ref]:
            fp.Reference().SetPosition(pt(*SILK_REFS[ref]))
            fp.Reference().SetTextAngleDegrees(0)

    for net, width, pts, layer in ROUTES:
        add_track(board, netinfo[net], width, pts, {F: pcbnew.F_Cu, IN2: pcbnew.In2_Cu, B: pcbnew.B_Cu}[layer])
    for net, x, y in VIAS:
        add_via(board, netinfo[net], x, y)

    add_outline(board)
    # L2 solid ground: no tracks on In1.Cu (vias cross it); 3.3 V plane on L3
    add_rule_area(board, "L2 ground", [pcbnew.In1_Cu],
                  [(X0, Y0), (X0 + W, Y0), (X0 + W, Y0 + H), (X0, Y0 + H)], tracks=True)
    island = pcbnew.ZONE(board)
    island.SetLayer(pcbnew.In2_Cu)
    island.SetNet(netinfo[design.V33])
    island.SetZoneName("3V3 L3")
    island.SetLocalClearance(mm(0.2))
    island.SetMinThickness(mm(0.2))
    island.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    island.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    island.SetAssignedPriority(1)
    polygon(island, ISLAND_3V3)
    board.Add(island)
    r1, r2 = CELL_RING
    for i in range(24):  # ring as 24 sectors (plain polygons for the Specctra export)
        a = [math.radians(15 * (i + k)) for k in (0, 0.5, 1)]
        pts = [(HOLDER[0] + r * math.cos(t), HOLDER[1] + r * math.sin(t)) for r, ts in ((r1, a), (r2, a[::-1]))
               for t in ts]
        add_rule_area(board, f"coin-cell-{i}", [pcbnew.B_Cu], pts, tracks=True, vias=True, pour=True)

    # silkscreen
    for text, x, y, layer, size, thick, angle in TEXTS:
        add_text(board, text, x, y, pcbnew.F_SilkS if layer == F else pcbnew.B_SilkS, size, thick, angle)
    for ref, label in TP_LABEL.items():
        x, y, _, _ = PLACE[ref]
        dx = 0.3 if x < X0 + 2 else 0.0  # left column: labels away from the board edge
        add_text(board, label, x + dx, y + 1.35, pcbnew.B_SilkS, 0.6, 0.1)

    save(board)
    print("placed:", PCB)
    return board


def sync():
    """Values and part numbers changed in design.py (same footprints and nets): update the placed and routed board
    in place, no new placement or routing."""
    comps, _ = read_netlist()
    board = pcbnew.LoadBoard(PCB)
    board.GetTitleBlock().SetDate(design.DATE)
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    missing = set(comps) ^ set(fps)
    assert not missing, f"board and schematic differ: {missing}, run the place and pour stages"
    for ref, c in sorted(comps.items()):
        fp = fps[ref]
        if fp.GetFPIDAsString() != c["footprint"]:
            raise SystemExit(f"{ref}: footprint changed to {c['footprint']}, run the place and pour stages")
        fp.SetValue(c["value"])
        set_fields(fp, c, "B" if fp.IsFlipped() else "F")
    save(board)
    print("synced:", PCB)


# ----------------------------------------------------------------------------------------------- pours
def add_pour(board, net, layer, priority=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetZoneName("GND " + board.GetLayerName(layer))
    z.SetLocalClearance(mm(0.2))  # also the gap of the grounded coplanar RF line
    z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.25))
    z.SetThermalReliefSpokeWidth(mm(0.3))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetAssignedPriority(priority)
    e = 0.0
    polygon(z, [(X0 + e, Y0 + e), (X0 + W - e, Y0 + e), (X0 + W - e, Y0 + H - e), (X0 + e, Y0 + H - e)])
    board.Add(z)
    return z


def fill(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def stitch(board, gnd, pitch=2.0, keep=0.35):
    """GND vias wherever the pours of every layer have copper around the whole via (plus `keep` mm)."""
    pours = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd.GetNetCode()]
    r = 0.3 + keep
    ring = [(0, 0)] + [(r * math.cos(a), r * math.sin(a)) for a in [i * math.pi / 6 for i in range(12)]]
    holes = [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
             for fp in board.GetFootprints() for p in fp.Pads() if p.HasHole()]
    holes += [(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y))
              for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    contact_r = CELL_RING[1] + 0.3  # no stitching via under the coin cell (contact pad and ring)
    # nor on silkscreen text (labels of the test pads, designators, board name)
    texts = [t for t in board.GetDrawings() if t.GetClass() == "PCB_TEXT"]
    texts += [fp.Reference() for fp in board.GetFootprints() if fp.Reference().IsVisible()]
    boxes = []
    for t in texts:
        bb = t.GetBoundingBox()
        boxes.append((pcbnew.ToMM(bb.GetLeft()) - 0.5, pcbnew.ToMM(bb.GetTop()) - 0.5,
                      pcbnew.ToMM(bb.GetRight()) + 0.5, pcbnew.ToMM(bb.GetBottom()) + 0.5))
    added = []
    y = Y0 + 1.2
    while y < Y0 + H - 1.0:
        x = X0 + 1.2
        while x < X0 + W - 1.0:
            ok = all(any(z.HitTestFilledArea(layer, pt(x + dx, y + dy), 0) for z in pours if z.GetLayer() == layer)
                     for layer in COPPER for dx, dy in ring)
            ok = ok and all(math.hypot(x - hx, y - hy) > 1.2 for hx, hy in holes + added)
            ok = ok and math.hypot(x - HOLDER[0], y - HOLDER[1]) > contact_r
            ok = ok and not any(x1 <= x <= x2 and y1 <= y <= y2 for x1, y1, x2, y2 in boxes)
            if ok:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pt(x, y))
                v.SetWidth(mm(0.6))
                v.SetDrill(mm(0.3))
                v.SetNet(gnd)
                board.Add(v)
                added.append((x, y))
            x += pitch
        y += pitch
    return len(added)


def step_pour():
    """GND pours on the four layers and stitching vias; replaces earlier ones, so it can run again on its own."""
    board = pcbnew.LoadBoard(PCB)
    gnd = board.FindNet(design.GND)
    old = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd.GetNetCode()]
    old += [v for v in board.GetTracks() if v.GetClass() == "PCB_VIA" and v.GetNetCode() == gnd.GetNetCode()
            and not v.IsLocked()]
    for item in old:
        board.Remove(item)
    for layer in COPPER:
        add_pour(board, gnd, layer)
    fill(board)
    n = stitch(board, gnd)
    fill(board)
    save(board)
    print("stitching vias", n)


def run_step(name):
    """Each step in its own process (os._exit: no teardown of the SWIG objects)."""
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "_" + name], capture_output=True, text=True)
    out = "\n".join(line for line in (r.stdout + r.stderr).splitlines() if "memory leak" not in line)
    if r.returncode != 0:
        raise SystemExit(f"step {name} failed:\n{out}")
    return out


if __name__ == "__main__":
    stage = sys.argv[1] if len(sys.argv) > 1 else "all"
    if stage == "_pour":  # internal step, see run_step()
        step_pour()
        sys.stdout.flush()
        os._exit(0)
    if stage in ("place", "all"):
        place()
    if stage in ("pour", "all"):
        print(run_step("pour"))
    if stage == "sync":
        sync()
    sys.stdout.flush()
    os._exit(0)
