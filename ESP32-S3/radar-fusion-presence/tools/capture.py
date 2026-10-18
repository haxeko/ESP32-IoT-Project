#!/usr/bin/env python3
"""Records the serial stream of the radar probe into an .rlog file.

  pip install pyserial
  python3 capture.py -p /dev/ttyACM0 --label "empty room, fan off" --duration 1800
  python3 capture.py -p COM5 --cmd "info all" --marks

Each line of the probe is stored as "<host unix ms><TAB><line>". Every 10 s the
PC clock is sent to the probe ("sync"), which answers with its own time (@T), so
both clocks can be aligned later. With --marks, each line typed in the terminal
is sent as "mark <text>" (an event record for labelling experiments, e.g. "seg
fan_on" ... "seg end"); a line starting with "!" is sent as a probe command
("!eng off"). With --schedule FILE, the probe commands of FILE are sent at their
times, one "<seconds from start> <command>" per line, e.g. for the interference
experiment (E8):
    0    mark seg ld2450_on
    300  pwr 2 off
    300  mark seg ld2450_off
    600  pwr 2 on
    600  mark seg ld2450_on
The 1 s summaries, events and replies are shown on the terminal. Decode the
file with decode.py, measure it with report.py.
"""

import argparse
import datetime
import os
import queue
import sys
import threading
import time

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None

SHOW = ("@H", "@E", "@K", "@I", "@B", "@C")


def now_ms() -> int:
    return int(time.time() * 1000)


def stdin_reader(commands: "queue.Queue[str]", stop: threading.Event):
    for line in sys.stdin:
        if stop.is_set():
            break
        line = line.strip()
        if not line:
            continue
        commands.put(line[1:].strip() if line.startswith("!") else f"mark {line}")


def read_schedule(path: str):
    """[(seconds, command)] sorted by time; '#' starts a comment."""
    items = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            when, _, cmd = line.partition(" ")
            try:
                items.append((float(when), cmd.strip()))
            except ValueError:
                sys.exit(f"{path}:{n}: expected '<seconds> <command>'")
    return sorted(items, key=lambda x: x[0])


def open_port(port: str, baud: int):
    while True:
        try:
            return serial.Serial(port, baud, timeout=0.2)
        except serial.SerialException as e:
            print(f"# cannot open {port} ({e}), retrying in 2 s", file=sys.stderr)
            time.sleep(2)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", required=True, help="serial port of the probe (/dev/ttyACM0, COM5...)")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="UART console rate (USB-Serial-JTAG ignores it)")
    ap.add_argument("-o", "--out", help="output file (default: capture_<date>_<time>.rlog)")
    ap.add_argument("--label", default="", help="free text stored at the top of the file")
    ap.add_argument("--duration", type=float, default=0, help="stop after this many seconds (0: Ctrl+C)")
    ap.add_argument("--cmd", action="append", default=[], help="probe command sent at the start (repeatable)")
    ap.add_argument("--sync-every", type=float, default=10, help="seconds between clock syncs (0: none)")
    ap.add_argument("--marks", action="store_true", help="send typed lines as markers / probe commands")
    ap.add_argument("--schedule", help="file of '<seconds> <command>' lines sent at those times")
    ap.add_argument("--quiet", action="store_true", help="do not echo summaries and events")
    args = ap.parse_args(argv)
    if serial is None:
        sys.exit("pyserial is missing: pip install pyserial")

    out = args.out or datetime.datetime.now().strftime("capture_%Y%m%d_%H%M%S.rlog")
    commands: "queue.Queue[str]" = queue.Queue()
    for c in args.cmd:
        commands.put(c)
    stop = threading.Event()
    if args.marks:
        threading.Thread(target=stdin_reader, args=(commands, stop), daemon=True).start()

    schedule = read_schedule(args.schedule) if args.schedule else []
    port = open_port(args.port, args.baud)
    start = time.time()
    next_sync = start
    lines = 0
    pending = b""
    with open(out, "w", encoding="utf-8") as f:
        f.write(f"# capture {datetime.datetime.now().astimezone().isoformat()} port={args.port}\n")
        if args.label:
            f.write(f"# label {args.label}\n")
        f.write(f"# tool capture.py, host {os.uname().nodename if hasattr(os, 'uname') else ''}\n")
        print(f"# recording to {out} (Ctrl+C to stop)", file=sys.stderr)
        try:
            while not args.duration or time.time() - start < args.duration:
                while schedule and time.time() - start >= schedule[0][0]:
                    commands.put(schedule.pop(0)[1])
                if args.sync_every and time.time() >= next_sync:
                    commands.put(f"sync {now_ms()}")
                    next_sync += args.sync_every
                while not commands.empty():
                    cmd = commands.get()
                    f.write(f"# sent {now_ms()} {cmd}\n")
                    try:
                        port.write((cmd + "\n").encode("ascii", "replace"))
                    except serial.SerialException:
                        pass
                try:
                    chunk = port.read(4096)
                except serial.SerialException as e:
                    # USB re-enumeration after a reset of the board
                    f.write(f"# port lost {now_ms()} {e}\n")
                    port.close()
                    port = open_port(args.port, args.baud)
                    f.write(f"# port back {now_ms()}\n")
                    continue
                if not chunk:
                    continue
                t = now_ms()
                pending += chunk
                *complete, pending = pending.split(b"\n")
                for raw in complete:
                    line = raw.decode("ascii", "replace").rstrip("\r")
                    if not line:
                        continue
                    f.write(f"{t}\t{line}\n")
                    lines += 1
                    if not args.quiet and line.startswith(SHOW):
                        print(line.rsplit("*", 1)[0])
                f.flush()
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            port.close()
    print(f"# {lines} lines in {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
