#!/usr/bin/env python3
"""Phase 1 measurements from a radar probe capture (.rlog).

  pip install numpy
  python3 report.py session.rlog [-o report.md] [--compare ld2450_off ld2450_on]

Reports, for the whole capture and for each segment marked during it ("mark seg
<name>" ... "mark seg end", repeatable, blocks of the same name are pooled):
  - radars, firmware versions and settings found in the replies;
  - stream quality: report rate, interval jitter, gaps, frames lost or rejected;
  - LD2410 per gate: median, MAD, P95, P99 of the moving and still energy, share of
    reports with a target, dominant period (autocorrelation 2-60 s) and the share
    of still-energy power in the 0.1-0.5 Hz band (breathing range);
  - LD2450: number of targets, how long each target slot stays filled, where
    targets appear and disappear, how often a present target has speed 0;
  - --compare A B: per gate shift of the energy distributions between two
    segments (median, P95, Kolmogorov-Smirnov D). The p-value uses an effective
    sample size from the lag-1 autocorrelation, because 10 Hz energies are far
    from independent; read it as a screening value, not a test result.
"""

import argparse
import math
import os
import sys
from typing import Dict, List, Optional, Tuple

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode  # noqa: E402
import hlk_proto as hp  # noqa: E402

GATES = hp.LD2410_GATES
GAP_S = 0.3  # a pause longer than three report periods counts as a gap
RUN_GAP_S = 0.3  # an LD2450 slot empty this long ends a "run" (target presence)


# ---- segments ----

def find_segments(events: List[dict], boot_end: Dict[int, float]) -> Dict[str, List[Tuple[int, float, float]]]:
    """name -> [(boot, start_s, end_s)] from "seg <name>" / "seg end" markers. A segment
    still open at a new marker, a reboot or the end of the capture ends there."""
    segs: Dict[str, List[Tuple[int, float, float]]] = {}
    open_seg: Optional[Tuple[str, int, float]] = None

    def close(at_boot: int, at_t: float):
        name, boot, t0 = open_seg
        segs.setdefault(name, []).append((boot, t0, at_t if boot == at_boot else boot_end.get(boot, t0)))

    for e in sorted((e for e in events if e["source"] == "mark"), key=lambda e: (e["boot"], e["t_s"])):
        words = e["text"].split(None, 1)
        if not words or words[0] != "seg":
            continue
        name = words[1].strip() if len(words) > 1 else "end"
        if open_seg:
            close(e["boot"], e["t_s"])
            open_seg = None
        if name != "end":
            open_seg = (name, e["boot"], e["t_s"])
    if open_seg:
        close(-1, 0.0)
    return segs


def in_segment(rows: List[dict], spans: Optional[List[Tuple[int, float, float]]]) -> List[dict]:
    if spans is None:
        return rows
    return [r for r in rows if any(r["boot"] == b and t0 <= r["t_s"] < t1 for b, t0, t1 in spans)]


# ---- statistics ----

def robust(x: np.ndarray) -> dict:
    if x.size == 0:
        return {}
    med = float(np.median(x))
    return {"n": int(x.size), "mean": float(np.mean(x)), "std": float(np.std(x)), "median": med,
            "mad": float(np.median(np.abs(x - med))), "p95": float(np.percentile(x, 95)),
            "p99": float(np.percentile(x, 99)), "max": float(np.max(x))}


def effective_n(x: np.ndarray) -> float:
    """Sample size corrected for lag-1 autocorrelation (AR(1) approximation)."""
    if x.size < 3:
        return float(x.size)
    d = x - x.mean()
    denom = float(np.dot(d, d))
    if denom == 0:
        return float(x.size)
    rho = float(np.dot(d[:-1], d[1:]) / denom)
    rho = min(max(rho, -0.99), 0.99)
    return max(2.0, x.size * (1 - rho) / (1 + rho))


def ks_statistic(a: np.ndarray, b: np.ndarray) -> float:
    a, b = np.sort(a), np.sort(b)
    v = np.concatenate([a, b])
    return float(np.max(np.abs(np.searchsorted(a, v, "right") / a.size - np.searchsorted(b, v, "right") / b.size)))


def kolmogorov_q(lam: float) -> float:
    if lam < 0.2:
        return 1.0
    k = np.arange(1, 101)
    return float(min(1.0, max(0.0, 2 * np.sum((-1.0) ** (k - 1) * np.exp(-2 * k * k * lam * lam)))))


def ks_test(a: np.ndarray, b: np.ndarray) -> Tuple[float, float]:
    """D and a p-value with effective sample sizes."""
    d = ks_statistic(a, b)
    na, nb = effective_n(a), effective_n(b)
    ne = na * nb / (na + nb)
    return d, kolmogorov_q((math.sqrt(ne) + 0.12 + 0.11 / math.sqrt(ne)) * d)


def blocks(t: np.ndarray, max_gap_s: float = 1.0):
    """Slices of continuous data: a reboot (time going back) or a pause above max_gap_s starts a new one."""
    if t.size == 0:
        return []
    cuts = np.where((np.diff(t) <= 0) | (np.diff(t) > max_gap_s))[0] + 1
    edges = [0] + cuts.tolist() + [t.size]
    return [slice(a, b) for a, b in zip(edges[:-1], edges[1:]) if b > a]


def longest_block(t: np.ndarray, x: np.ndarray):
    if t.size == 0:
        return t, x
    sl = max(blocks(t), key=lambda b: b.stop - b.start)
    return t[sl], x[sl]


def window_medians(t: np.ndarray, x: np.ndarray, window_s: float = 10.0) -> np.ndarray:
    """Medians of consecutive non-overlapping windows (nearly independent samples), per continuous block."""
    out = []
    for sl in blocks(t):
        tb, xb = t[sl], x[sl]
        idx = np.floor((tb - tb[0]) / window_s).astype(int)
        out += [np.median(xb[idx == i]) for i in np.unique(idx) if np.sum(idx == i) >= window_s * 5]
    return np.array(out)


def auc(neg: np.ndarray, pos: np.ndarray) -> Optional[float]:
    """P(pos > neg) + 0.5 P(tie) (Mann-Whitney): 0.5 = no separation, 1 = pos always higher."""
    if neg.size == 0 or pos.size == 0:
        return None
    greater = np.sum(pos[:, None] > neg[None, :])
    ties = np.sum(pos[:, None] == neg[None, :])
    return float((greater + 0.5 * ties) / (neg.size * pos.size))


def uniform(t: np.ndarray, x: np.ndarray, rate: float = 10.0) -> np.ndarray:
    """x resampled at a constant rate (reports arrive with jitter)."""
    if t.size < 2:
        return x
    grid = np.arange(t[0], t[-1], 1.0 / rate)
    return np.interp(grid, t, x)


def dominant_period(x: np.ndarray, rate: float = 10.0, lo_s: float = 2.0, hi_s: float = 60.0):
    """(period s, autocorrelation) of the strongest repetition, None if weak. Periods up to
    half the length of x are searched (60 s needs 2 minutes)."""
    n = x.size
    hi_s = min(hi_s, n / (2 * rate))
    if hi_s < 2 * lo_s or np.std(x) < 1.0:
        return None
    d = x - x.mean()
    f = np.fft.rfft(d, 2 * n)
    acf = np.fft.irfft(f * np.conj(f))[:n]
    acf /= acf[0]
    lo, hi = int(lo_s * rate), int(hi_s * rate)
    lag = lo + int(np.argmax(acf[lo:hi]))
    return (lag / rate, float(acf[lag])) if acf[lag] >= 0.3 else None


def band_share(x: np.ndarray, rate: float = 10.0, band=(0.1, 0.5), total=(0.02, 2.0), window_s: float = 64.0):
    """Share of the power in band among total (Welch, 50 % overlap)."""
    n = int(window_s * rate)
    if x.size < n:
        return None
    win = np.hanning(n)
    psd = np.zeros(n // 2 + 1)
    for start in range(0, x.size - n + 1, n // 2):
        seg = x[start:start + n]
        seg = seg - seg.mean()
        psd += np.abs(np.fft.rfft(seg * win)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    tot = psd[(freqs >= total[0]) & (freqs <= total[1])].sum()
    if tot <= 0:
        return None
    return float(psd[(freqs >= band[0]) & (freqs <= band[1])].sum() / tot)


# ---- sections ----

def fmt(v, digits=1) -> str:
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return "-"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def table(header: List[str], rows: List[List[str]]) -> str:
    out = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def stream_quality(rows: List[dict], name: str) -> List[str]:
    if not rows:
        return [f"- {name}: no reports"]
    lines = []
    by_ch: Dict[Tuple[int, str], List[float]] = {}
    for r in rows:
        by_ch.setdefault((r["boot"], r["ch"]), []).append(r["t_s"])
    for (boot, ch), ts in sorted(by_ch.items()):
        t = np.array(ts)
        if t.size < 2:
            continue
        dt = np.diff(t)
        span = t[-1] - t[0]
        gaps = int(np.sum(dt > GAP_S))
        lines.append(f"- {name} (UART{ch}, boot {boot}): {t.size} reports in {span:.0f} s = "
                     f"{(t.size - 1) / span:.2f} Hz; interval P5/P50/P95/max = {np.percentile(dt, 5) * 1000:.0f}/"
                     f"{np.percentile(dt, 50) * 1000:.0f}/{np.percentile(dt, 95) * 1000:.0f}/{dt.max() * 1000:.0f} ms; "
                     f"{gaps} gap(s) > {GAP_S * 1000:.0f} ms")
    return lines


def ld2410_arrays(rows: List[dict]):
    eng = [r for r in rows if r["type"] == 1 and r["moving_gates"] == GATES and r["still_gates"] == GATES]
    eng.sort(key=lambda r: (r["boot"], r["t_s"]))
    if not eng:
        return None
    t = np.array([r["t_s"] for r in eng])
    m = np.array([[r[f"m{g}"] for g in range(GATES)] for r in eng], dtype=float)
    s = np.array([[r[f"s{g}"] for g in range(GATES)] for r in eng], dtype=float)
    state = np.array([r["state"] for r in eng])
    return t, m, s, state


def ld2410_section(rows: List[dict], gate_cm: Optional[int]) -> List[str]:
    arr = ld2410_arrays(rows)
    if arr is None:
        n_basic = sum(1 for r in rows if r["type"] == 2)
        return [f"No engineering reports ({n_basic} basic reports): turn engineering mode on (eng on)."]
    t, m, s, state = arr
    out = [f"{t.size} engineering reports. Share with a target in the radar's own decision: "
           f"moving {np.mean((state & 1) > 0) * 100:.1f} %, still {np.mean((state & 2) > 0) * 100:.1f} %."]
    rows_out = []
    for g in range(GATES):
        mm, ss = robust(m[:, g]), robust(s[:, g])
        span = f"{g * gate_cm / 100:.2f}-{(g + 1) * gate_cm / 100:.2f}" if gate_cm else "?"
        su = uniform(*longest_block(t, s[:, g]))
        mu = uniform(*longest_block(t, m[:, g]))
        per_m, per_s = dominant_period(mu), dominant_period(su)
        share = band_share(su)
        rows_out.append([str(g), span, fmt(mm["median"]), fmt(mm["mad"]), fmt(mm["p95"]), fmt(mm["p99"]),
                         fmt(ss["median"]), fmt(ss["mad"]), fmt(ss["p95"]), fmt(ss["p99"]),
                         f"{per_m[0]:.1f} s ({per_m[1]:.2f})" if per_m else "-",
                         f"{per_s[0]:.1f} s ({per_s[1]:.2f})" if per_s else "-",
                         fmt(share * 100 if share is not None else None, 0)])
    out.append(table(["gate", "m", "mov med", "mov MAD", "mov P95", "mov P99", "still med", "still MAD",
                      "still P95", "still P99", "mov period (acf)", "still period (acf)", "still 0.1-0.5 Hz %"],
                     rows_out))
    return out


def ld2450_runs(rows: List[dict]):
    """Per slot: list of (start, end, first (x, y), last (x, y), speed-0 share)."""
    runs = {1: [], 2: [], 3: []}
    for k in (1, 2, 3):
        cur = None
        last_t = None
        for r in rows:
            present = r[f"t{k}_present"] == 1
            if cur and (not present and r["t_s"] - last_t > RUN_GAP_S or r["boot"] != cur["boot"]):
                runs[k].append(cur)
                cur = None
            if present:
                pos = (r[f"t{k}_x_mm"], r[f"t{k}_y_mm"])
                if cur is None:
                    cur = {"boot": r["boot"], "start": r["t_s"], "first": pos, "n": 0, "still": 0}
                cur["end"], cur["last"] = r["t_s"], pos
                cur["n"] += 1
                cur["still"] += r[f"t{k}_speed_cm_s"] == 0
                last_t = r["t_s"]
        if cur:
            runs[k].append(cur)
    return runs


def ld2450_section(rows: List[dict]) -> List[str]:
    if not rows:
        return ["No LD2450 reports."]
    counts = np.array([r["count"] for r in rows])
    out = ["Targets per report: " + ", ".join(f"{c}: {np.mean(counts == c) * 100:.1f} %" for c in range(4))]
    implausible = sum(1 for r in rows for k in (1, 2, 3) if r[f"t{k}_present"] == 1 and r[f"t{k}_plausible"] == 0)
    out.append(f"Implausible targets (behind the radar, too far or too fast): {implausible}")
    present = [(r[f"t{k}_x_mm"], r[f"t{k}_y_mm"], r[f"t{k}_speed_cm_s"], r[f"t{k}_resolution_mm"])
               for r in rows for k in (1, 2, 3) if r[f"t{k}_present"] == 1]
    if present:
        p = np.array(present, dtype=float)
        out.append(f"Present targets: x {p[:, 0].min():.0f}..{p[:, 0].max():.0f} mm, y {p[:, 1].min():.0f}.."
                   f"{p[:, 1].max():.0f} mm, |speed| median {np.median(np.abs(p[:, 2])):.0f} cm/s, speed 0 in "
                   f"{np.mean(p[:, 2] == 0) * 100:.1f} %, resolution field {sorted({int(v) for v in p[:, 3]})}")
    runs = ld2450_runs(rows)
    table_rows = []
    for k, rs in runs.items():
        if not rs:
            continue
        d = np.array([r["end"] - r["start"] for r in rs])
        table_rows.append([str(k), str(len(rs)), fmt(float(np.median(d))), fmt(float(d.max())),
                           fmt(float(np.mean([r["still"] / r["n"] for r in rs]) * 100), 0)])
    if table_rows:
        out.append(table(["slot", "runs", "median s", "longest s", "speed-0 share %"], table_rows))
        appear = [r for rs in runs.values() for r in rs]
        appear.sort(key=lambda r: r["start"])
        out.append("Where runs start and end (first 20; a run starting in the middle of the room means a target "
                   "the radar had lost, or a ghost):")
        out.append(table(["start s", "duration s", "first x,y mm", "last x,y mm"],
                         [[fmt(r["start"]), fmt(r["end"] - r["start"]), f"{r['first'][0]},{r['first'][1]}",
                           f"{r['last'][0]},{r['last'][1]}"] for r in appear[:20]]))
    return out


def lag_section(ld2410_rows: List[dict], ld2450_rows: List[dict]) -> List[str]:
    """Delay of the LD2410 moving distance behind the LD2450 range of target 1 (E10)."""
    moving = [r["boot"] for r in ld2450_rows if r["t1_present"] == 1 and r["t1_speed_cm_s"]]
    if not moving:
        return ["No moving target on the LD2450."]
    boot = max(set(moving), key=moving.count)  # times restart at each boot: use one
    a = [(r["t_s"], r["moving_cm"] * 10.0) for r in ld2410_rows
         if r["boot"] == boot and r["state"] in (1, 3) and r["moving_cm"] > 0]
    b = [(r["t_s"], math.hypot(r["t1_x_mm"], r["t1_y_mm"])) for r in ld2450_rows
         if r["boot"] == boot and r["t1_present"] == 1 and r["t1_plausible"] == 1 and r["t1_speed_cm_s"]]
    if len(a) < 300 or len(b) < 300:
        return ["Not enough moving data on both radars (needs 30 s of walking)."]
    ta, ra = np.array(a).T
    tb, rb = np.array(b).T
    t0, t1 = max(ta[0], tb[0]), min(ta[-1], tb[-1])
    if t1 - t0 < 30:
        return ["The two radars saw movement at different times only."]
    grid = np.arange(t0, t1, 0.01)
    xa = np.interp(grid, ta, ra)
    xb = np.interp(grid, tb, rb)
    xa, xb = xa - xa.mean(), xb - xb.mean()
    best = None
    for shift in range(-150, 151):  # +-1.5 s in 10 ms steps
        if shift >= 0:
            c = np.corrcoef(xa[shift:], xb[:xb.size - shift])[0, 1]
        else:
            c = np.corrcoef(xa[:shift], xb[-shift:])[0, 1]
        if best is None or c > best[1]:
            best = (shift * 0.01, c)
    return [f"LD2410 moving distance lags the LD2450 range of target 1 by {best[0] * 1000:.0f} ms "
            f"(correlation {best[1]:.2f}; negative = LD2410 first). Valid only for one person walking "
            f"along the boresight (E10)."]


def compare_section(a_rows, b_rows, a_name, b_name) -> List[str]:
    a, b = ld2410_arrays(a_rows), ld2410_arrays(b_rows)
    out = [f"### {b_name} vs {a_name}"]
    if a is None or b is None:
        return out + ["LD2410 engineering reports missing in one of the segments."]
    rows_out = []
    for ch_name, ai, bi in (("moving", a[1], b[1]), ("still", a[2], b[2])):
        for g in range(GATES):
            x, y = ai[:, g], bi[:, g]
            d, p = ks_test(x, y)
            area = auc(window_medians(a[0], x), window_medians(b[0], y))
            rows_out.append([ch_name, str(g), fmt(float(np.median(x))), fmt(float(np.median(y))),
                             fmt(float(np.percentile(x, 95))), fmt(float(np.percentile(y, 95))),
                             fmt(float(np.percentile(y, 95) - np.percentile(x, 95))), fmt(d, 2),
                             f"{p:.2g}", fmt(effective_n(x), 0) + "/" + fmt(effective_n(y), 0),
                             fmt(area, 2)])
    out.append(table(["energy", "gate", f"med {a_name}", f"med {b_name}", f"P95 {a_name}", f"P95 {b_name}",
                      "dP95", "KS D", "p (n_eff)", "n_eff", f"AUC 10 s ({b_name} > {a_name})"], rows_out))
    sa, sb = a[3], b[3]
    out.append(f"Radar's own target decision: {a_name} {np.mean(sa > 0) * 100:.1f} % of reports, "
               f"{b_name} {np.mean(sb > 0) * 100:.1f} %.")
    return out


def report(path: str, compare: Optional[List[str]] = None) -> str:
    d = decode.decode_rlog(path)
    s = d.summary
    boot_end: Dict[int, float] = {}
    for r in d.ld2410 + d.ld2450 + d.events:
        boot_end[r["boot"]] = max(boot_end.get(r["boot"], 0.0), r["t_s"])
    segs = find_segments(d.events, boot_end)
    out = [f"# Phase 1 report: {s['file']}", ""]
    for note in s["notes"]:
        out.append(f"> {note}")
    out += ["", "## Capture", ""]
    out.append(f"- records {s['records']}, checksum errors {s['checksum_errors']}, bad records {s['bad_records']}, "
               f"other lines {s['other_lines']}, decode errors {s['decode_errors']}")
    out.append(f"- boots: {len(s['boots'])}; channels: "
               + ", ".join(f"UART{ch} {s['radars'].get(ch, 'no reports')}" for ch in sorted(s["channels"])))
    if s["sequence_gaps"]:
        out.append(f"- records lost on the probe (sequence gaps): {s['sequence_gaps']}")
    drops = [r for r in d.stats if r["ch"] == "0"]
    if drops:
        out.append(f"- probe output drops at the end: {drops[-1].get('out_drops')}, lowest free heap "
                   f"{drops[-1].get('min_heap')} bytes")
    for ch, replies in sorted(s["replies"].items()):
        out.append(f"- UART{ch} replies: " + "; ".join(f"{k}: {v}" for k, v in replies.items()))
    last_stats = {}
    for r in d.stats:
        if r["ch"] != "0":
            last_stats[r["ch"]] = r
    for ch, r in sorted(last_stats.items()):
        out.append(f"- UART{ch} counters: " + ", ".join(f"{k}={v}" for k, v in r.items()
                                                       if k not in ("boot", "t_s", "host_ms", "ch")))
    if segs:
        out.append("- segments: " + ", ".join(f"{n} {sum(t1 - t0 for _, t0, t1 in v):.0f} s in {len(v)} block(s)"
                                              for n, v in segs.items()))

    gate_cm = None
    for replies in s["replies"].values():
        text = replies.get("ld2410_read_resolution", "")
        if "0.2 m" in text:
            gate_cm = 20
        elif "0.75 m" in text:
            gate_cm = 75
    gate_cm = gate_cm or 75

    out += ["", "## Stream quality", ""]
    out += stream_quality(d.ld2410, "LD2410")
    out += stream_quality(d.ld2450, "LD2450")

    out += ["", "## Delay between the radars", ""]
    out += lag_section(d.ld2410, d.ld2450)

    parts = [("whole capture", None)] + [(n, v) for n, v in segs.items()]
    for name, spans in parts:
        out += ["", f"## LD2410 energy: {name}", ""]
        out += ld2410_section(in_segment(d.ld2410, spans), gate_cm)
        out += ["", f"## LD2450 targets: {name}", ""]
        out += ld2450_section(in_segment(d.ld2450, spans))

    if compare:
        a_name, b_name = compare
        if a_name not in segs or b_name not in segs:
            out += ["", f"Cannot compare: segments found: {', '.join(segs) or 'none'}"]
        else:
            out += ["", "## Segment comparison", ""]
            out += compare_section(in_segment(d.ld2410, segs[a_name]), in_segment(d.ld2410, segs[b_name]),
                                   a_name, b_name)
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rlog")
    ap.add_argument("-o", "--out", help="write the Markdown report here (default: print)")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"), help="compare segment B against segment A")
    args = ap.parse_args(argv)
    text = report(args.rlog, args.compare)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
