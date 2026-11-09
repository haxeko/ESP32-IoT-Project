#!/usr/bin/env python3
"""Pass/fail matrix and minimum-capacitance sweep for the two devices (see capsim.py).

Usage: python3 tools/capsim_matrix.py tools [matrix|sweep|all]   (README 2.8)
"""
import sys

sys.path.insert(0, sys.argv[1] if len(sys.argv) > 1 else __import__("os").path.dirname(__file__))
from capsim import BTN, TH, Bank, run  # noqa: E402

UF = 1e-6
# Effective values at ~3 V, 25 degC. 220 uF 1210 X5R 6.3 V at 3 V: ~130 uF each assumed (README of the
# button: 2 x 220 uF ~ 260 uF), 100 uF 1206: ~37.5 uF (power_model.py), 10 uF 0603: ~5 uF.
# Aluminium 470 uF 6.3 V (KNSCHA RVT, tan d 0.22 @ 120 Hz -> ~0.6 Ohm), at 0/-10 degC: C x0.9, ESR x3.
# Tantalum MnO2 470 uF (AVX TAJD477K006, ESR <= 0.4 Ohm @ 100 kHz), at 0/-10 degC: C x0.97, ESR x1.5.
BANKS = [
    Bank("1x220 MLCC", c_cer=135 * UF),
    Bank("2x220 MLCC (nut)", c_cer=265 * UF),
    Bank("2x220 MLCC thap", c_cer=205 * UF),
    Bank("1x470 nhom", c_cer=5 * UF, c_el=470 * UF, esr_el=0.6, cold_c=0.9, cold_esr=3.0),
    Bank("1x470 tantal", c_cer=5 * UF, c_el=470 * UF, esr_el=0.4, cold_c=0.97, cold_esr=1.5),
    Bank("2x100+470 nhom (TH)", c_cer=85 * UF, c_el=470 * UF, esr_el=0.6, cold_c=0.9, cold_esr=3.0),
    Bank("2x220+470 nhom", c_cer=265 * UF, c_el=470 * UF, esr_el=0.6, cold_c=0.9, cold_esr=3.0),
    Bank("4x220 MLCC", c_cer=525 * UF),
]
STATES = [("fresh", "25C"), ("mid", "25C"), ("late", "25C"), ("eol", "25C"),
          ("fresh", "10C"), ("mid", "10C"), ("fresh", "0C"), ("mid", "0C"), ("fresh", "-10C"), ("mid", "-10C")]
CASES = [(BTN, "CR2032", ["poll x5", "press", "boot", "attach"]),
         (BTN, "CR2450", ["poll x5", "press", "boot", "attach"]),
         (TH, "CR2450", ["poll x5", "report", "boot", "attach"])]


def cellmark(r):
    return f"{r['v_min']:.2f}" if r["ok"] else "**RESET**"


def matrix():
    for dev, ctype, profs in CASES:
        print(f"\n### {dev.name} {ctype}\n")
        for prof in profs:
            print(f"\n{dev.name} {ctype}, {prof} (VIN min V, or RESET)\n")
            print("| cell | " + " | ".join(b.name for b in BANKS) + " |")
            print("|---|" + "---|" * len(BANKS))
            for st, tp in STATES:
                row = [cellmark(run(dev, ctype, st, tp, b, prof)) for b in BANKS]
                print(f"| {st} {tp} | " + " | ".join(row) + " |")
            sys.stdout.flush()


def sweep():
    caps = [50, 85, 135, 205, 265, 330, 470, 555, 735, 1000, 1500, 2200, 3300, 4700, 10000]
    print("\n### Minimum ideal (ceramic-like) capacitance to survive, uF (> 10000 = none)\n")
    for dev, ctype, profs in CASES:
        print(f"\n{dev.name} {ctype}\n")
        print("| cell | " + " | ".join(profs) + " |")
        print("|---|" + "---|" * len(profs))
        for st, tp in STATES:
            row = []
            for prof in profs:
                need = None
                for c in caps:
                    if run(dev, ctype, st, tp, Bank("x", c_cer=c * UF), prof)["ok"]:
                        need = c
                        break
                row.append(str(need) if need else "> 10000")
            print(f"| {st} {tp} | " + " | ".join(row) + " |")
            sys.stdout.flush()


if __name__ == "__main__":
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    if what in ("all", "matrix"):
        matrix()
    if what in ("all", "sweep"):
        sweep()
