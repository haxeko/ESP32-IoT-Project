#!/usr/bin/env python3
"""Decodes a capture of the radar probe (.rlog, written by capture.py) into CSV files.

  python3 decode.py session.rlog [-o session_csv/]

Writes ld2410.csv (one row per LD2410 report, with the energy of every gate in
engineering mode), ld2450.csv (one row per LD2450 report, three target slots),
replies.csv (ACKs and the commands sent), events.csv (markers, button, jobs,
console replies, decoded replies), stats.csv (the probe's counters), sync.csv
and summary.json. Standard library only.

.rlog: one line per line received from the probe, "<host unix ms><TAB><line>";
lines starting with '#' are notes of capture.py. A probe record is
"@<type>,<fields>*<XOR checksum>" (radar_probe/main/probe_out.h); other lines
(ESP-IDF log, boot ROM) are counted and kept apart.
"""

import argparse
import csv
import json
import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlk_proto as hp  # noqa: E402

LD2410_COLUMNS = (["boot", "t_s", "host_ms", "ch", "seq", "type", "state", "moving_cm", "moving_energy", "still_cm",
                   "still_energy", "detection_cm", "moving_gates", "still_gates"]
                  + [f"m{g}" for g in range(hp.LD2410_GATES)] + [f"s{g}" for g in range(hp.LD2410_GATES)]
                  + ["extra_len", "light", "out_pin"])
LD2450_COLUMNS = (["boot", "t_s", "host_ms", "ch", "seq", "count"]
                  + [f"t{k}_{f}" for k in range(1, 4) for f in ("present", "x_mm", "y_mm", "speed_cm_s",
                                                                  "resolution_mm", "plausible")])


class ChecksumError(ValueError):
    pass


def checksum(body: str) -> int:
    c = 0
    for ch in body:
        c ^= ord(ch)
    return c


def parse_record(line: str):
    """(type, fields) of a probe record, None for another line. Raises ChecksumError."""
    line = line.strip()
    if not line.startswith("@"):
        return None
    star = line.rfind("*")
    if star < 0 or len(line) - star != 3:
        raise ChecksumError("no checksum")
    body = line[1:star]
    try:
        expected = int(line[star + 1:], 16)
    except ValueError:
        raise ChecksumError("bad checksum digits")
    if checksum(body) != expected:
        raise ChecksumError("checksum mismatch")
    parts = body.split(",")
    return parts[0], parts[1:]


@dataclass
class Decoded:
    ld2410: List[dict] = field(default_factory=list)
    ld2450: List[dict] = field(default_factory=list)
    replies: List[dict] = field(default_factory=list)
    events: List[dict] = field(default_factory=list)
    stats: List[dict] = field(default_factory=list)
    sync: List[dict] = field(default_factory=list)
    raw: List[dict] = field(default_factory=list)
    other_lines: List[str] = field(default_factory=list)
    summary: dict = field(default_factory=dict)


def _t_s(t_us: str) -> float:
    return int(t_us) / 1e6


def decode_rlog(path: str) -> Decoded:
    d = Decoded()
    s = d.summary
    s.update({"file": os.path.basename(path), "lines": 0, "records": 0, "checksum_errors": 0, "bad_records": 0,
              "other_lines": 0, "boots": [], "notes": [], "channels": {}, "decode_errors": 0,
              "sequence_gaps": {}, "radars": {}, "replies": {}, "human_lines": 0})
    boot = 0
    last_seq: Dict[str, int] = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            s["lines"] += 1
            raw_line = raw_line.rstrip("\r\n")
            if raw_line.startswith("#"):
                s["notes"].append(raw_line[1:].strip())
                continue
            host_ms: Optional[int] = None
            text = raw_line
            if "\t" in raw_line:
                head, text = raw_line.split("\t", 1)
                host_ms = int(head) if head.isdigit() else None
            try:
                rec = parse_record(text)
            except ChecksumError:
                s["checksum_errors"] += 1
                continue
            if rec is None:
                if text.strip():
                    s["other_lines"] += 1
                    d.other_lines.append(text)
                continue
            s["records"] += 1
            kind, fl = rec
            try:
                boot = _decode_record(d, kind, fl, host_ms, boot, last_seq)
            except (ValueError, IndexError):
                s["bad_records"] += 1
    return d


def _decode_record(d: Decoded, kind: str, fl: List[str], host_ms, boot: int, last_seq: Dict[str, int]) -> int:
    s = d.summary
    if kind == "B":
        boot += 1
        info = {"boot": boot, "t_s": _t_s(fl[0])}
        info.update(kv.split("=", 1) for kv in fl[1:] if "=" in kv)
        s["boots"].append(info)
        last_seq.clear()
    elif kind == "C":
        info = dict(kv.split("=", 1) for kv in fl[2:] if "=" in kv)
        s["channels"][fl[0]] = info
    elif kind in ("D", "A"):
        ch, seq, t_us, hexdata = fl[0], int(fl[1]), fl[2], fl[3]
        key = f"{boot}:{ch}"
        if key in last_seq and seq != last_seq[key] + 1:
            gaps = s["sequence_gaps"]
            gaps[ch] = gaps.get(ch, 0) + max(seq - last_seq[key] - 1, 1)
        last_seq[key] = seq
        frames = hp.Parser().feed(bytes.fromhex(hexdata))
        if len(frames) != 1:
            s["decode_errors"] += 1
            return boot
        fr = frames[0]
        base = {"boot": boot, "t_s": _t_s(t_us), "host_ms": host_ms, "ch": ch, "seq": seq}
        if fr.kind == hp.KIND_LD2410:
            try:
                r = hp.decode_ld2410_report(fr.payload)
            except hp.DecodeError:
                s["decode_errors"] += 1
                return boot
            s["radars"][ch] = "LD2410"
            row = dict(base, type=r.type, state=r.state, moving_cm=r.moving_cm, moving_energy=r.moving_energy,
                       still_cm=r.still_cm, still_energy=r.still_energy, detection_cm=r.detection_cm,
                       moving_gates=len(r.moving_gate_energy), still_gates=len(r.still_gate_energy),
                       extra_len=r.extra_len, light=r.light, out_pin=r.out_pin)
            for g in range(hp.LD2410_GATES):
                row[f"m{g}"] = r.moving_gate_energy[g] if g < len(r.moving_gate_energy) else None
                row[f"s{g}"] = r.still_gate_energy[g] if g < len(r.still_gate_energy) else None
            d.ld2410.append(row)
        elif fr.kind == hp.KIND_LD2450:
            targets = hp.decode_ld2450_report(fr.payload)
            s["radars"][ch] = "LD2450"
            row = dict(base, count=sum(t.present for t in targets))
            for k, t in enumerate(targets, 1):
                row.update({f"t{k}_present": int(t.present), f"t{k}_x_mm": t.x_mm if t.present else None,
                            f"t{k}_y_mm": t.y_mm if t.present else None,
                            f"t{k}_speed_cm_s": t.speed_cm_s if t.present else None,
                            f"t{k}_resolution_mm": t.resolution_mm if t.present else None,
                            f"t{k}_plausible": int(t.plausible)})
            d.ld2450.append(row)
        elif fr.kind in (hp.KIND_ACK, hp.KIND_COMMAND):
            row = dict(base, direction="rx")
            if fr.kind == hp.KIND_COMMAND:
                row.update(command=f"0x{hp.u16(fr.payload):04X}", status="", value=fr.payload[2:].hex().upper(),
                           decoded="command looped back (TX wired to RX?)")
            else:
                try:
                    a = hp.decode_ack(fr.payload)
                except hp.DecodeError:
                    s["decode_errors"] += 1
                    return boot
                text = hp.describe_ack(a)
                row.update(command=f"0x{a.command:04X}", status=a.status, value=a.value.hex().upper(), decoded=text)
                name = hp.COMMAND_NAMES.get(a.command, f"0x{a.command:04X}")
                s["replies"].setdefault(ch, {})[name] = text
            d.replies.append(row)
    elif kind == "X":
        ch, t_us, hexdata = fl[0], fl[1], fl[2]
        data = bytes.fromhex(hexdata)
        command = hp.u16(data, 6) if len(data) >= 8 else None
        d.replies.append({"boot": boot, "t_s": _t_s(t_us), "host_ms": host_ms, "ch": ch, "seq": "",
                          "direction": "tx", "command": f"0x{command:04X}" if command is not None else "",
                          "status": "", "value": data[8:-4].hex().upper(),
                          "decoded": hp.COMMAND_NAMES.get(command, "")})
    elif kind == "R":
        d.raw.append({"boot": boot, "t_s": _t_s(fl[1]), "host_ms": host_ms, "ch": fl[0], "hex": fl[2]})
    elif kind == "I":
        d.events.append({"boot": boot, "t_s": _t_s(fl[1]), "host_ms": host_ms, "source": f"u{fl[0]}:{fl[2]}",
                         "text": ",".join(fl[3:])})
    elif kind == "E":
        d.events.append({"boot": boot, "t_s": _t_s(fl[0]), "host_ms": host_ms, "source": fl[1],
                         "text": ",".join(fl[2:])})
    elif kind == "K":
        d.events.append({"boot": boot, "t_s": _t_s(fl[0]), "host_ms": host_ms, "source": "console",
                         "text": ",".join(fl[1:])})
    elif kind == "S":
        row = {"boot": boot, "t_s": _t_s(fl[1]), "host_ms": host_ms, "ch": fl[0]}
        row.update(kv.split("=", 1) for kv in fl[2:] if "=" in kv)
        d.stats.append(row)
    elif kind == "T":
        d.sync.append({"boot": boot, "t_s": _t_s(fl[0]), "host_ms": host_ms, "pc_ms": int(fl[1])})
    elif kind == "H":
        s["human_lines"] += 1
    return boot


def _write(path: str, rows: List[dict], columns: Optional[List[str]] = None):
    if columns is None:
        columns = []
        for r in rows:
            for k in r:
                if k not in columns:
                    columns.append(k)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: ("" if r.get(k) is None else r.get(k)) for k in columns})


def write_outputs(d: Decoded, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    _write(os.path.join(out_dir, "ld2410.csv"), d.ld2410, LD2410_COLUMNS)
    _write(os.path.join(out_dir, "ld2450.csv"), d.ld2450, LD2450_COLUMNS)
    _write(os.path.join(out_dir, "replies.csv"), d.replies,
           ["boot", "t_s", "host_ms", "ch", "seq", "direction", "command", "status", "value", "decoded"])
    _write(os.path.join(out_dir, "events.csv"), d.events, ["boot", "t_s", "host_ms", "source", "text"])
    _write(os.path.join(out_dir, "stats.csv"), d.stats)
    _write(os.path.join(out_dir, "sync.csv"), d.sync, ["boot", "t_s", "host_ms", "pc_ms"])
    if d.raw:
        _write(os.path.join(out_dir, "raw.csv"), d.raw, ["boot", "t_s", "host_ms", "ch", "hex"])
    if d.other_lines:
        with open(os.path.join(out_dir, "console.log"), "w", encoding="utf-8") as f:
            f.write("\n".join(d.other_lines) + "\n")
    summary = dict(d.summary, ld2410_reports=len(d.ld2410), ld2450_reports=len(d.ld2450))
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rlog")
    ap.add_argument("-o", "--out", help="output directory (default: <rlog without extension>_csv)")
    args = ap.parse_args(argv)
    out = args.out or os.path.splitext(args.rlog)[0] + "_csv"
    d = decode_rlog(args.rlog)
    write_outputs(d, out)
    s = d.summary
    print(f"{s['records']} records: {len(d.ld2410)} LD2410 reports, {len(d.ld2450)} LD2450 reports, "
          f"{len(d.replies)} commands/replies, {len(d.events)} events; {s['checksum_errors']} checksum errors, "
          f"{s['decode_errors']} decode errors -> {out}")


if __name__ == "__main__":
    main()
