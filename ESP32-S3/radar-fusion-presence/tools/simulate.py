#!/usr/bin/env python3
"""Writes a SYNTHETIC radar probe capture (.rlog), to try and test the tools without hardware.

  python3 simulate.py -o demo.rlog --scenario empty:300,fan:300,person_still:600

The numbers are invented (Gaussian background per gate, a sinusoidal fan, a
breathing-like still energy). They are NOT measurements and must never be used
as evidence about the LD2410/LD2450 or as training data: only real captures
(capture.py) are. The records have the exact format of radar_probe, so
decode.py and report.py treat them like a real capture.

Scenarios (segment name:seconds, comma separated; each one is marked "seg <name>"):
  empty         background only
  fan           + moving energy around gate 4, oscillating with a 12 s period
  person_still  + still energy at gate 3 with a 0.25 Hz modulation; the LD2450
                tracks the person for 20 s, then loses them (seated, not moving)
  walk          + an LD2450 target walking back and forth, moving energy following it
  ld2450_off / ld2450_on   like empty, without / with LD2450 reports (interference tests)
"""

import argparse
import math
import random
import time
from typing import List, Optional, Tuple

GATES = 9


def checksum(body: str) -> int:
    c = 0
    for ch in body:
        c ^= ord(ch)
    return c


def record(body: str) -> str:
    return f"@{body}*{checksum(body):02X}"


def ld2410_frame(state: int, mcm: int, me: int, scm: int, se: int, dcm: int, moving: List[int], still: List[int],
                 light: int = 0x60, out: int = 0) -> bytes:
    payload = (bytes([0x01, 0xAA, state]) + mcm.to_bytes(2, "little") + bytes([me]) + scm.to_bytes(2, "little")
               + bytes([se]) + dcm.to_bytes(2, "little") + bytes([GATES - 1, GATES - 1]) + bytes(moving)
               + bytes(still) + bytes([light, out, 0x55, 0x00]))
    return bytes.fromhex("F4F3F2F1") + len(payload).to_bytes(2, "little") + payload + bytes.fromhex("F8F7F6F5")


def _sm(v: int) -> bytes:
    """Sign-magnitude int16 of the LD2450 reports (bit 15 set = positive)."""
    return ((0x8000 | v) if v >= 0 else min(-v, 0x7FFF)).to_bytes(2, "little")


def ld2450_frame(targets: List[Optional[Tuple[int, int, int]]]) -> bytes:
    body = b""
    for t in targets:
        body += bytes(8) if t is None else _sm(t[0]) + _sm(t[1]) + _sm(t[2]) + (360).to_bytes(2, "little")
    return bytes.fromhex("AAFF0300") + body + bytes.fromhex("55CC")


def clip(v: float) -> int:
    return max(0, min(100, int(round(v))))


def simulate(path: str, scenario: List[Tuple[str, float]], seed: int = 1):
    rng = random.Random(seed)
    host0 = int(time.time() * 1000)
    lines: List[Tuple[float, str]] = []

    def add(t: float, body: str):
        lines.append((t, record(body)))

    def us(t: float) -> int:
        return int(t * 1e6)

    t = 0.5
    add(t, f"B,{us(t)},probe=simulated,idf=none,chip=none,flash=0MB,reset=1")
    add(t, f"C,1,{us(t)},uart=1,rx=17,tx=18,baud=256000")
    add(t, f"C,2,{us(t)},uart=2,rx=4,tx=5,baud=256000")
    add(t, f"I,1,{us(t)},radar,LD2410")
    add(t, f"I,2,{us(t)},radar,LD2450")
    # Background of an empty room per gate (invented)
    bg_m = [30, 18, 8, 5, 4, 3, 3, 2, 2]
    bg_s = [45, 30, 10, 6, 5, 4, 3, 3, 3]
    seq = {1: 0, 2: 0}
    t = 1.0
    for name, seconds in scenario:
        add(t, f"E,{us(t)},mark,seg {name}")
        start = t
        walk_phase = rng.random() * math.pi
        while t < start + seconds:
            ts = t - start
            moving = [clip(rng.gauss(bg_m[g], 1.5 + 0.1 * bg_m[g])) for g in range(GATES)]
            still = [clip(rng.gauss(bg_s[g], 1.5 + 0.1 * bg_s[g])) for g in range(GATES)]
            targets: List[Optional[Tuple[int, int, int]]] = [None, None, None]
            if name == "fan":
                swing = 0.5 + 0.5 * math.sin(2 * math.pi * ts / 12.0)
                for g, w in ((3, 0.4), (4, 1.0), (5, 0.4)):
                    moving[g] = clip(moving[g] + w * (10 + 25 * swing + rng.gauss(0, 3)))
            if name == "person_still":
                breath = math.sin(2 * math.pi * 0.25 * ts)
                still[3] = clip(still[3] + 35 + 10 * breath + rng.gauss(0, 3))
                still[2] = clip(still[2] + 10 + 3 * breath + rng.gauss(0, 2))
                if ts < 20:
                    targets[0] = (-400 + int(rng.gauss(0, 30)), 2600 + int(rng.gauss(0, 40)), 0)
            if name == "walk":
                y = 3000 + 2000 * math.sin(0.4 * ts + walk_phase)
                v = int(0.4 * 2000 * math.cos(0.4 * ts + walk_phase) / 10)
                targets[0] = (int(600 * math.sin(0.1 * ts)), int(y), v)
                g = min(GATES - 1, int(y / 750))
                moving[g] = clip(moving[g] + 50 + rng.gauss(0, 8))
            moving_on = any(m > 40 for m in moving[2:])
            still_on = any(s > 40 for s in still[2:])
            state = (1 if moving_on else 0) | (2 if still_on else 0)
            mg = max(range(GATES), key=lambda i: moving[i])
            sg = max(range(GATES), key=lambda i: still[i])
            frame = ld2410_frame(state, mg * 75 if moving_on else 0, moving[mg] if moving_on else 0,
                                 sg * 75 if still_on else 0, still[sg] if still_on else 0, sg * 75, moving, still)
            tf = t + rng.uniform(-0.003, 0.003)
            add(tf, f"D,1,{seq[1]},{us(tf)},{frame.hex().upper()}")
            seq[1] += 1
            if name != "ld2450_off":
                tf2 = t + 0.037 + rng.uniform(-0.003, 0.003)
                add(tf2, f"D,2,{seq[2]},{us(tf2)},{ld2450_frame(targets).hex().upper()}")
                seq[2] += 1
            t += 0.1
            if int(t * 10) % 100 == 0:
                add(t, f"S,1,{us(t)},radar=LD2410,baud=256000,frames={seq[1]},reports={seq[1]},garbage=0,"
                       f"bad_len=0,bad_footer=0,decode_err=0")
        add(t, f"E,{us(t)},mark,seg end")
    lines.sort(key=lambda x: x[0])
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"# capture SYNTHETIC data from simulate.py (seed {seed}), not a measurement\n")
        for tl, line in lines:
            f.write(f"{host0 + int(tl * 1000)}\t{line}\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--scenario", default="empty:120,fan:120,person_still:180,walk:60")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args(argv)
    scenario = []
    for part in args.scenario.split(","):
        name, seconds = part.split(":")
        scenario.append((name.strip(), float(seconds)))
    simulate(args.out, scenario, args.seed)
    print(f"synthetic capture written to {args.out}")


if __name__ == "__main__":
    main()
