#!/usr/bin/env python3
"""Schematic of the CR2032 Matter button (README section 3), drawn with schemdraw.

    pip install schemdraw matplotlib
    python3 tools/schematic.py [OUTDIR]   -> OUTDIR (default docs/)/schematic.svg and schematic.png

Reference designators and values are the ones of the README BOM (section 4).
"""

import os
import sys

import schemdraw
import schemdraw.elements as elm
from schemdraw.segments import Segment, SegmentArc

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "docs")

NOTE = "#555555"
BOX = "#9a9a9a"
DNP = "#9a9a9a"


class SolderJumper(elm.Element2Term):
    """Solder jumper, bridged (closed by default): two half-round pads joined by solder."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        r, gap = 0.22, 0.16
        w = 2 * r + gap
        # First segment = the element path between its leads (Element2Term); the line through the pads = bridged
        self.segments.append(Segment([(0, 0), (w, 0)]))
        self.segments.append(SegmentArc((r, 0), 2 * r, 2 * r, theta1=90, theta2=270))
        self.segments.append(Segment([(r, r), (r, -r)]))
        self.segments.append(SegmentArc((r + gap, 0), 2 * r, 2 * r, theta1=-90, theta2=90))
        self.segments.append(Segment([(r + gap, r), (r + gap, -r)]))
        self.segments.append(Segment([(r, 0), (r + gap, 0)], lw=4))


def gnd(d, at):
    d += elm.Ground().at(at)


def vcc(d, at, name="3V3"):
    d += elm.Vdd().at(at).label(name, fontsize=10)


def text(d, at, s, size=9.5, color="black", halign="left", valign="center"):
    d += elm.Label().at(at).label(s, fontsize=size, color=color, halign=halign, valign=valign)


def note(d, at, s, halign="left", valign="center", color=NOTE, size=8.5):
    text(d, at, s, size=size, color=color, halign=halign, valign=valign)


def pad(d, at, name, side="right"):
    """Test pad: open circle with its name beside it."""
    d += elm.Dot(open=True).at(at)
    dx = 0.35 if side == "right" else -0.35
    text(d, (at[0] + dx, at[1]), name, halign="left" if side == "right" else "right")


def vpart(d, cls, x, y, length, ref, val, gnd_end=True, **style):
    """Vertical two-terminal part going down from (x, y), name and value on its right."""
    e = cls().down().at((x, y)).length(length)
    for k, v in style.items():
        e = getattr(e, k)(v)
    d += e
    color = style.get("color", "black")
    text(d, (x + 0.42, y - length / 2), f"{ref}\n{val}", color=color)
    if gnd_end:
        gnd(d, e.end)
    return e


def hpart(d, cls, x, y, length, ref, val, below=False):
    """Horizontal two-terminal part going right from (x, y), name and value above (or below) it."""
    e = cls().right().at((x, y)).length(length)
    d += e
    text(d, (x + length / 2, y - 0.62 if below else y + 0.62), f"{ref} {val}".strip(), halign="center")
    return e


def section(d, x0, y0, x1, y1, title):
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        d += elm.Line().at(a).to(b).color(BOX).linestyle("--").linewidth(0.8)
    text(d, (x0 + 0.3, y0 - 0.5), title, size=11.5, color="#222222")


def draw(path):
    with schemdraw.Drawing(file=path, show=False, dpi=150) as d:
        d.config(unit=2.0, fontsize=10, lw=1.4)

        # ------------------------------------------------------------------ title
        text(d, (-1, 25.0), "Nút Matter over Thread CR2032 · ESP32-H2-WROOM-02C · sơ đồ nguyên lý v1.0", size=15)
        note(d, (-1, 24.1), "Ký hiệu và giá trị theo BOM (README mục 4). Mọi tụ là MLCC; R1–R3 ±1 %, TCR ≤ ±200 ppm; "
                            "JP1/JP2 là cầu hàn, mặc định nối; C7 không lắp (DNP). Mũi tên 3V3 / VBAT = cùng một net.",
             size=9.5)

        # ================================================================== ① battery, reverse polarity, bulk
        rail = 18.0
        section(d, -1, 22.8, 16.4, 10.2, "① Nguồn pin: CR2032 → chống ngược cực → tụ bulk")
        # Long plate (+) at the element start: drawn downwards from the rail
        bt = elm.Battery().down().at((0.8, rail)).toy(13.6)
        d += bt
        gnd(d, bt.end)
        text(d, (0.15, 16.75), "+", size=12, halign="center")
        text(d, (1.35, 15.6), "BT1\nCR2032\n3 V")
        d += elm.Line().at((0.8, rail)).to((3.0, rail))
        q1 = elm.PFet().theta(-90).anchor("drain").at((3.0, rail))
        d += q1
        text(d, (q1.gate[0], rail + 1.75), "Q1 DMG2305UX", halign="center")
        note(d, (q1.gate[0], rail + 1.15), "D ◄ pin    S ► tải", halign="center")
        d += elm.Line().at(q1.gate).toy(rail - 2.4)
        gnd(d, (q1.gate[0], rail - 2.4))
        note(d, (q1.gate[0] + 0.5, rail - 1.7), "lắp ngược pin:\nV_GS = 0 → FET khoá")
        jp1 = SolderJumper().right().at(q1.source).length(2.4)
        d += jp1
        text(d, (jp1.center[0], rail + 0.62), "JP1", halign="center")
        note(d, (jp1.center[0], rail - 0.62), "đo dòng pin", halign="center")
        d += elm.Line().at(jp1.end).to((16.4, rail))
        text(d, (12.9, rail + 0.6), "VBAT  1,8 … 3,2 V", size=10.5, halign="center")
        for x, ref in ((10.0, "C1"), (12.6, "C2")):
            d += elm.Dot().at((x, rail))
            vpart(d, elm.Capacitor, x, rail, 3.0, ref, "220 µF")
        note(d, (11.3, 13.9), "1210 X5R 6,3 V\n≈ 260 µF hiệu dụng ở 3 V", halign="center")
        note(d, (7.4, 11.2), "Tụ bulk phía pin: cấp xung TX/RX ngắn (khung ≤ 4,3 ms),\n"
                             "pin chỉ nạp lại tụ qua điện trở trong 10–25 Ω", halign="center")

        # ================================================================== ② TPS63900
        section(d, 16.4, 22.8, 39.0, 10.2, "② Buck-boost TPS63900 → 3,3 V (Iq 75 nA)")
        u2 = elm.Ic(
            pins=[
                elm.IcPin(name="VIN", pin="10", side="left", pos=0.9),
                elm.IcPin(name="EN", pin="1", side="left", pos=0.5),
                elm.IcPin(name="SEL", pin="2", side="left", pos=0.1),
                elm.IcPin(name="LX1", pin="9", side="top", pos=0.3),
                elm.IcPin(name="LX2", pin="7", side="top", pos=0.7),
                elm.IcPin(name="VOUT", pin="6", side="right", pos=0.9),
                elm.IcPin(name="CFG1", pin="3", side="bottom", pos=0.04),
                elm.IcPin(name="CFG2", pin="4", side="bottom", pos=0.35),
                elm.IcPin(name="CFG3", pin="5", side="bottom", pos=0.66),
                elm.IcPin(name="GND", pin="8", side="bottom", pos=0.97),
            ],
            size=(7.0, 4.0), pinspacing=0, edgepadW=0.5, edgepadH=0.5, leadlen=0.6, lofst=0.15, lsize=11,
        ).right().anchor("VIN").at((21.2, rail))
        d += u2
        text(d, (u2.center[0], u2.center[1] - 0.2), "U2\nTPS63900DSKR", halign="center")
        d += elm.Line().at((16.4, rail)).to((21.2, rail))
        d += elm.Dot().at((18.0, rail))
        vpart(d, elm.Capacitor, 18.0, rail, 4.0, "C3", "10 µF")
        # EN, SEL tied to VIN
        xt = 20.4
        d += elm.Line().at(u2.EN).tox(xt)
        d += elm.Line().at(u2.SEL).tox(xt)
        d += elm.Line().at((xt, u2.SEL[1])).to((xt, rail))
        d += elm.Dot().at((xt, rail))
        d += elm.Dot().at((xt, u2.EN[1]))
        # L1 between LX1 and LX2
        top = u2.LX1[1] + 0.9
        d += elm.Line().at(u2.LX1).toy(top)
        l1 = elm.Inductor2(loops=3).right().at((u2.LX1[0], top)).tox(u2.LX2[0])
        d += l1
        d += elm.Line().at((u2.LX2[0], top)).to(u2.LX2)
        text(d, (l1.center[0], top + 0.7), "L1 2,2 µH", halign="center")
        note(d, (l1.center[0], top + 1.25), "DFE252012F-2R2M", halign="center")
        # CFG resistors, GND
        for pin, ref, val in (("CFG1", "R1", "36,5 kΩ"), ("CFG2", "R2", "0 Ω"), ("CFG3", "R3", "16,2 kΩ")):
            pp = getattr(u2, pin)
            vpart(d, elm.Resistor, pp[0], pp[1], 1.6, ref, val)
        d += elm.Line().at(u2.GND).toy(u2.GND[1] - 1.6)
        gnd(d, (u2.GND[0], u2.GND[1] - 1.6))
        note(d, (u2.center[0], 11.1), "CFG1 36,5 kΩ + CFG2 0 Ω → VO(2) = 3,3 V, không giới hạn dòng vào\n"
                                      "CFG3 16,2 kΩ → VO(1) = 3,3 V · EN, SEL nối VIN", halign="center")
        # VOUT -> C4 -> JP2 -> 3V3 rail with C5, C6 (a single pin on a side is centred: jog up to the rail)
        xb = u2.VOUT[0] + 1.0
        xj = u2.VOUT[0] + 0.5
        d += elm.Line().at(u2.VOUT).tox(xj)
        d += elm.Line().at((xj, u2.VOUT[1])).toy(rail)
        d += elm.Line().at((xj, rail)).to((xb, rail))
        d += elm.Dot().at((xb, rail))
        vpart(d, elm.Capacitor, xb, rail, 3.0, "C4", "22 µF")
        jp2 = SolderJumper().right().at((xb, rail)).length(2.6)
        d += jp2
        text(d, (jp2.center[0], rail + 0.62), "JP2", halign="center")
        note(d, (jp2.center[0], rail - 0.62), "đo dòng 3V3", halign="center")
        x5 = jp2.end[0] + 1.0
        d += elm.Line().at(jp2.end).to((x5 + 4.0, rail))
        vpart(d, elm.Capacitor, x5, rail, 3.0, "C5", "10 µF")
        d += elm.Dot().at((x5, rail))
        vpart(d, elm.Capacitor, x5 + 2.4, rail, 3.0, "C6", "100 nF")
        d += elm.Dot().at((x5 + 2.4, rail))
        vcc(d, (x5 + 4.0, rail))
        note(d, (x5 + 1.6, 13.9), "C5, C6 sát chân 1 của U1", halign="center")

        # ================================================================== ③ module
        section(d, -1, 9.6, 39.0, -11.4, "③ Module ESP32-H2-WROOM-02C và mạch ngoại vi")
        u1 = elm.Ic(
            pins=[
                elm.IcPin(name="3V3", pin="1", side="top", pos=0.3),
                elm.IcPin(name="VBAT", pin="14", side="top", pos=0.75),
                elm.IcPin(name="EN", pin="2", side="left", pos=0.92),
                elm.IcPin(name="IO8", pin="7", side="left", pos=0.55),
                elm.IcPin(name="IO9", pin="8", side="left", pos=0.35),
                elm.IcPin(name="IO10", pin="5", side="right", pos=0.92),
                elm.IcPin(name="IO22", pin="15", side="right", pos=0.66),
                elm.IcPin(name="IO1", pin="27", side="right", pos=0.42),
                elm.IcPin(name="TXD0", pin="22", side="right", pos=0.18),
                elm.IcPin(name="RXD0", pin="21", side="right", pos=0.12),
                elm.IcPin(name="IO26", pin="23", side="right", pos=0.06),
                elm.IcPin(name="IO27", pin="24", side="right", pos=0.001),
                elm.IcPin(name="GND", pin="9, 13, 29", side="bottom", pos=0.5),
            ],
            size=(8.0, 14.0), pinspacing=0, edgepadW=0.5, edgepadH=0.5, leadlen=0.8, lofst=0.15, lsize=11,
        ).right().at((12.0, -8.0))
        d += u1
        text(d, (u1.center[0], u1.center[1] - 0.5),
             "U1\nESP32-H2-WROOM-02C-H4S\n4 MB flash, anten PCB", halign="center")
        # Top: 3V3, VBAT (NC)
        d += elm.Line().at(u1["3V3"]).toy(u1["3V3"][1] + 0.6)
        vcc(d, (u1["3V3"][0], u1["3V3"][1] + 0.6))
        d += elm.NoConnect().at(u1.VBAT)
        note(d, (u1.VBAT[0] + 0.55, u1.VBAT[1] + 0.25),
             "VBAT (chân 14): KHÔNG nối pin —\ntrong module đã nối 3V3 qua 0 Ω", color="#b00020")
        # Bottom: GND
        d += elm.Line().at(u1.GND).toy(u1.GND[1] - 0.6)
        gnd(d, (u1.GND[0], u1.GND[1] - 0.6))

        # Left: EN with R4 pull-up, C7 (DNP), TP5
        y = u1.EN[1]
        xe = 8.6
        d += elm.Line().at(u1.EN).tox(xe)
        d += elm.Dot().at((xe, y))
        r4 = elm.Resistor().up().at((xe, y)).length(2.2)
        d += r4
        text(d, (xe + 0.42, y + 1.1), "R4\n10 kΩ")
        vcc(d, r4.end)
        vpart(d, elm.Capacitor, xe, y, 2.0, "C7 1 µF", "(DNP)", color=DNP, linestyle="--")
        d += elm.Line().at((xe, y)).tox(6.6)
        pad(d, (6.6, y), "TP5 EN", side="left")
        note(d, (6.2, y - 0.62), "reset: chạm GND", halign="right")
        note(d, (xe - 0.45, y + 1.4), "module đã có RC\n10 kΩ / 1 µF bên trong", halign="right")

        # Left: IO8 pull-up R5 (strapping, must be 1 in download mode)
        y = u1.IO8[1]
        r5 = elm.Resistor().left().at(u1.IO8).length(2.4)
        d += r5
        text(d, (r5.center[0], y + 0.62), "R5 10 kΩ", halign="center")
        d += elm.Line().at(r5.end).tox(7.6)
        vcc(d, (7.6, y))
        note(d, (r5.center[0], y - 0.62), "strapping: = 1 khi nạp", halign="center")

        # Left: IO9 = BOOT pad
        y = u1.IO9[1]
        d += elm.Line().at(u1.IO9).tox(8.2)
        pad(d, (8.2, y), "TP6 BOOT", side="left")
        note(d, (9.7, y - 0.62), "nối GND khi reset = chế độ nạp", halign="center")

        # Right: IO10 button (LP IO, EXT1 wakeup)
        y = u1.IO10[1]
        xa = u1.IO10[0] + 1.4
        d += elm.Line().at(u1.IO10).tox(xa)
        d += elm.Dot().at((xa, y))
        vpart(d, elm.Capacitor, xa, y, 1.6, "C8", "1 nF")
        r7 = hpart(d, elm.Resistor, xa, y, 2.4, "R7", "1 kΩ")
        xsw = r7.end[0]
        d += elm.Dot().at((xsw, y))
        r6 = elm.Resistor().up().at((xsw, y)).length(2.0)
        d += r6
        text(d, (xsw + 0.42, y + 1.0), "R6\n470 kΩ")
        vcc(d, r6.end)
        sw = elm.Button().right().at((xsw, y)).length(2.6)
        d += sw
        text(d, (sw.center[0], y - 0.65), "SW1", halign="center")
        d += elm.Line().at(sw.end).tox(sw.end[0] + 0.7)
        d += elm.Line().at((sw.end[0] + 0.7, y)).toy(y - 1.0)
        gnd(d, (sw.end[0] + 0.7, y - 1.0))
        note(d, (sw.end[0] + 1.2, y + 0.75), "nút: GPIO10 (LP IO, đánh thức EXT1)\n7 µA khi đang nhấn, 0 khi nhả")

        # Right: IO22 LED
        y = u1.IO22[1]
        r8 = hpart(d, elm.Resistor, u1.IO22[0], y, 2.4, "R8", "1 kΩ", below=True)
        d1 = elm.LED().right().at(r8.end).length(2.4)
        d += d1
        text(d, (d1.center[0], y - 0.65), "D1 đỏ", halign="center")
        d += elm.Line().at(d1.end).tox(d1.end[0] + 0.7)
        d += elm.Line().at((d1.end[0] + 0.7, y)).toy(y - 1.0)
        gnd(d, (d1.end[0] + 0.7, y - 1.0))
        note(d, (d1.end[0] + 1.2, y + 0.4), "LED: GPIO22, ~1,5 mA, chỉ nháy ngắn")

        # Right: IO1 VBAT sense (ADC1_CH0)
        y = u1.IO1[1]
        xc = u1.IO1[0] + 1.6
        d += elm.Line().at(u1.IO1).tox(xc)
        d += elm.Dot().at((xc, y))
        vpart(d, elm.Capacitor, xc, y, 1.5, "C9", "100 nF")
        r9 = hpart(d, elm.Resistor, xc, y, 2.6, "R9", "1 MΩ")
        d += elm.Line().at(r9.end).tox(r9.end[0] + 0.8)
        vcc(d, (r9.end[0] + 0.8, y), "VBAT")
        d += elm.Line().at((xc, y)).toy(y + 1.3)
        pad(d, (xc, y + 1.3), "TP10 VBAT_SENSE")
        note(d, (r9.end[0] + 1.5, y - 0.05), "đo pin: không chia áp, ~0 µA\nADC1_CH0, 0–3,3 V")

        # Right: UART0 and USB pads
        for pin, name in (("TXD0", "TP3 TXD0 (GPIO24)"), ("RXD0", "TP4 RXD0 (GPIO23)"),
                          ("IO26", "TP7 USB_D− (tùy chọn)"), ("IO27", "TP8 USB_D+ (tùy chọn)")):
            pp = u1[pin]
            d += elm.Line().at(pp).tox(pp[0] + 2.2)
            pad(d, (pp[0] + 2.2, pp[1]), name)

        # Notes
        note(d, (-0.5, -5.2),
             "Pad khác: TP1 3V3 · TP2 GND · TP9 VBAT\n"
             "Nạp: USB-UART 3,3 V vào TP2 / TP3 / TP4;\n"
             "TP6 BOOT xuống GND rồi chạm TP5 EN.\n"
             "Cấp nguồn bằng pin; không nối 3V3 của bộ\nchuyển vào TP1 khi đang có pin.", valign="top")
        note(d, (-0.5, -9.4), "Chân không dùng (để trống): IO0, IO2–IO5,\nIO11–IO14, IO25, NC 16–19", valign="top")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    os.makedirs(out, exist_ok=True)
    for ext in ("svg", "png"):
        draw(os.path.join(out, f"schematic.{ext}"))
    print("written:", os.path.normpath(os.path.join(out, "schematic.svg")), "and .png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
