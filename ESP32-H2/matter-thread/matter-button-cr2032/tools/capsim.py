#!/usr/bin/env python3
"""
Bulk capacitor comparison for two ESP32-H2 coin-cell designs that share the same power path
(cell -> P-MOSFET -> TPS63900 3.3 V):
  BTN  matter-button-cr2032 / matter-door-sensor: TX 0 dBm (36 mA), RX 25 mA, no TPS63900 input
       current limit (CFG2 = 0 Ohm), ~15 uF effective at 3V3, reset = ESP32-H2 brownout ~2.94 V
  TH   matter-temperature-sensor Rev B / H2W: TX +5 dBm (50 mA), RX 29 mA, 100 mA input limit,
       100 uF effective at 3V3, reset = TPS3839K33 2.93 V

Same equations as github.com/haxeko/matter-temperature-sensor hardware/analysis/power_model.py (cell OCV - I*R - two
polarisations, converter with UVLO and average input current limit, backward Euler), re-implemented
here with one addition: the bulk capacitor bank is a ceramic part (no ESR) in parallel with an
optional electrolytic / tantalum branch with ESR. ESTIMATES ONLY, nothing here is a measurement.

Usage: python3 tools/capsim.py  (checks the model against the repo's Rev B table; README 2.8)
"""
import math
import sys
from dataclasses import dataclass, replace

# ------------------------------------------------------------------ cells (copied from power_model.py)
CELL_STATES = {
    "fresh": dict(ocv=3.10, r_ohm=12.0, r_p1=6.0, tau1=0.05, b2=0.12, tau2=20.0),
    "mid":   dict(ocv=3.00, r_ohm=18.0, r_p1=9.0, tau1=0.05, b2=0.18, tau2=20.0),
    "late":  dict(ocv=2.90, r_ohm=40.0, r_p1=20.0, tau1=0.05, b2=0.30, tau2=20.0),
    "eol":   dict(ocv=2.75, r_ohm=100.0, r_p1=50.0, tau1=0.05, b2=0.50, tau2=20.0),
}
TEMPS = {"25C": (1.0, 1.0, 0.0), "10C": (1.3, 1.25, 0.01), "0C": (1.7, 1.6, 0.03), "-10C": (2.5, 2.2, 0.05)}
CELL_TYPES = {"CR2032": (1.0, 1.0), "CR2450": ((17.0 / 22.0) ** 2, (22.0 / 17.0) ** 2)}


@dataclass(frozen=True)
class Cell:
    ocv: float
    r_ohm: float
    r_p1: float
    tau1: float
    b2: float
    tau2: float
    i_ref: float


def cell(state, temp="25C", ctype="CR2032"):
    k, area = CELL_TYPES[ctype]
    p = dict(CELL_STATES[state])
    rf, bf, docv = TEMPS[temp]
    return Cell(ocv=p["ocv"] - docv, r_ohm=p["r_ohm"] * rf * k, r_p1=p["r_p1"] * rf * k, tau1=p["tau1"],
                b2=p["b2"] * bf, tau2=p["tau2"], i_ref=1e-3 * area)


# ------------------------------------------------------------------ capacitor banks
@dataclass(frozen=True)
class Bank:
    name: str
    c_cer: float        # effective ceramic capacitance at ~3 V [F] (no ESR)
    c_el: float = 0.0   # electrolytic / tantalum capacitance [F]
    esr_el: float = 0.0 # its ESR [Ohm] at 25 degC
    cold_c: float = 1.0 # electrolytic capacitance factor at the cold temperatures
    cold_esr: float = 1.0


# ------------------------------------------------------------------ devices
@dataclass(frozen=True)
class Device:
    name: str
    tx: float
    rx: float
    i_lim: float
    c_out: float
    v_reset: float
    r_extra: float = 0.15
    uvlo_on: float = 1.75
    uvlo_off: float = 1.65
    eta: float = 0.90
    iq: float = 75e-9


BTN = Device("BTN", tx=36e-3, rx=25e-3, i_lim=math.inf, c_out=15e-6, v_reset=2.94)
TH = Device("TH", tx=50e-3, rx=29e-3, i_lim=0.100, c_out=100e-6, v_reset=2.93)
SLEEP_A = 27e-6


def poll(d):
    return [(0.006, 10e-3), (0.003, d.rx), (0.001, d.tx), (0.0015, d.rx), (0.001, 10e-3)]


def report(d):
    """power_model.py report(): crypto, 3 x (CCA + 4 ms frame + ACK), 1 s active mode with 2 polls"""
    seq = [(0.010, 10e-3)]
    for _ in range(3):
        seq += [(0.0015, d.rx), (0.004, d.tx), (0.001, d.rx)]
    seq += [(0.5 - 0.0315, SLEEP_A)] + poll(d) + [(0.5 - 0.0125, SLEEP_A)] + poll(d)
    return seq


def press(d):
    """Button press, SIT, 1 controller: 3 events (InitialPress, ShortRelease, MultiPressComplete), each a
    report + its status response, then ~1.5 s active mode with fast polls every 200 ms"""
    seq = []
    for _ in range(3):
        seq += [(0.010, 10e-3)]
        for _ in range(2):
            seq += [(0.0015, d.rx), (0.004, d.tx), (0.001, d.rx)]
        seq += [(0.1, SLEEP_A)]
    for _ in range(7):
        seq += poll(d) + [(0.2 - 0.0125, SLEEP_A)]
    return seq


def boot(d):
    """power_model.py boot(): ROM/bootloader/flash + app init, radio calibration (ASSUMED)"""
    return [(0.30, 22e-3), (0.10, 45e-3), (0.40, 15e-3)]


def attach(d):
    """Thread re-attach: Parent Request to routers then REEDs, ~2 s of receiving"""
    return [(0.02, 10e-3), (0.002, d.tx), (0.75, d.rx), (0.002, d.tx), (1.25, d.rx), (0.05, 10e-3)]


def repeat(seq, n, gap):
    out = []
    for _ in range(n):
        out += seq + [(gap, SLEEP_A)]
    return out


PROFILES = {
    "poll x5": lambda d: repeat(poll(d), 5, 0.5),
    "report": report,
    "press": press,
    "boot": boot,
    "attach": attach,
}


# ------------------------------------------------------------------ simulation
def simulate(d, c, bank, profile, cold=False, dt=50e-6):
    c_el = bank.c_el * (bank.cold_c if cold else 1.0)
    esr = bank.esr_el * (bank.cold_esr if cold else 1.0)
    g_el_base = c_el > 0
    r = c.r_ohm + d.r_extra
    vp1 = vp2 = 0.0
    v = c.ocv
    v_el = c.ocv
    v_out = 3.30
    on = v > d.uvlo_on
    v_min = v
    v_out_min = v_out
    uvlo = False
    reset_at = None
    t = 0.0
    for dur, i_load in profile:
        steps = max(1, int(math.ceil(dur / dt - 1e-9)))
        h = dur / steps
        gain = 0.5 * d.c_out / h
        for _ in range(steps):
            # converter (as power_model.py, single converter, no output diode)
            i_in = 0.0
            supplied = 0.0
            if on:
                i_max = d.i_lim * v * d.eta / max(v_out, 0.5)
                cmd = i_load + (3.30 - v_out) * gain
                supplied = min(max(cmd, 0.0), i_max)
                i_in = supplied * v_out / d.eta / max(v, 0.3) + d.iq
            v_out = max(0.0, v_out + (supplied - i_load) / d.c_out * h)
            # input node: ceramic C on the node, electrolytic through its ESR (both backward Euler)
            v_src = c.ocv - vp1 - vp2
            g_el = 1.0 / (esr + h / c_el) if g_el_base else 0.0
            a = bank.c_cer / h + 1.0 / r + g_el
            b = bank.c_cer * v / h + v_src / r + g_el * v_el - i_in
            v_new = max(0.0, b / a)
            if g_el_base:
                i_el = (v_el - v_new) * g_el
                v_el -= i_el * h / c_el
            v = v_new
            i_cell = (v_src - v) / r
            vp1 += (i_cell * c.r_p1 - vp1) * min(1.0, h / c.tau1)
            v_ss = c.b2 * math.log1p(i_cell / c.i_ref) if i_cell > 0 else 0.0
            vp2 += (v_ss - vp2) * min(1.0, h / c.tau2)
            if on and v < d.uvlo_off:
                on = False
                uvlo = True
            elif not on and v > d.uvlo_on:
                on = True
            v_min = min(v_min, v)
            v_out_min = min(v_out_min, v_out)
            t += h
            if v_out < d.v_reset and reset_at is None:
                reset_at = t
                return dict(ok=False, v_min=v_min, v_out_min=v_out_min, uvlo=uvlo, t=reset_at)
    return dict(ok=True, v_min=v_min, v_out_min=v_out_min, uvlo=uvlo, t=None)


def run(d, ctype, state, temp, bank, prof):
    seq = PROFILES[prof](d)
    dt = 2e-4 if prof in ("attach",) else 50e-6
    return simulate(d, cell(state, temp, ctype), bank, seq, cold=temp in ("0C", "-10C"), dt=dt)


if __name__ == "__main__":
    # Validation against matter-temperature-sensor docs/power_budget.md 4.1 (Rev B, ideal 555 uF)
    revb = Bank("Rev B ideal 555", c_cer=555e-6)
    expect = {("fresh", "25C", "report"): True, ("fresh", "25C", "boot"): True,
              ("mid", "25C", "report"): True, ("mid", "25C", "boot"): False,
              ("late", "25C", "report"): False, ("fresh", "-10C", "report"): True,
              ("fresh", "-10C", "boot"): False, ("mid", "-10C", "report"): False,
              ("fresh", "0C", "boot"): False}
    bad = 0
    for (st, tp, pr), want in expect.items():
        r = run(TH, "CR2450", st, tp, revb, pr)
        flag = "ok " if r["ok"] == want else "MISMATCH"
        bad += r["ok"] != want
        print(f"{flag} {st:5s} {tp:4s} {pr:6s} sim={'ok' if r['ok'] else 'RESET'} repo={'ok' if want else 'RESET'}"
              f"  Vin_min={r['v_min']:.2f}")
    sys.exit(1 if bad else 0)
