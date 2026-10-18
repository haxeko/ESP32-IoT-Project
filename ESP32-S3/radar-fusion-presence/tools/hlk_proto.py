"""Hi-Link HLK-LD2410 (B/C) and HLK-LD2450 serial protocol, for the PC tools.

Mirrors components/radar_proto (C++): the same framing rules, decoded values
and event text, checked against the same vectors (test/vectors/hlk_vectors.txt).
Standard library only.

Frames (little endian):
  LD2410 report  F4 F3 F2 F1 | LEN (2) | payload | F8 F7 F6 F5
  LD2450 report  AA FF 03 00 | 3 targets x 8 bytes | 55 CC
  ACK            FD FC FB FA | LEN (2) | COMMAND|0x0100 (2) STATUS (2) value | 04 03 02 01
Neither protocol has a checksum.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

MAX_PAYLOAD = 64
HEADER_LEN = 4
LD2450_FRAME_LEN = 30
LD2450_PAYLOAD_LEN = 24
ACK_FLAG = 0x0100

KIND_LD2410 = "ld2410"
KIND_LD2450 = "ld2450"
KIND_ACK = "ack"
KIND_COMMAND = "command"

# header, footer, has LEN field, kind
_FORMATS = (
    (bytes.fromhex("F4F3F2F1"), bytes.fromhex("F8F7F6F5"), True, KIND_LD2410),
    (bytes.fromhex("AAFF0300"), bytes.fromhex("55CC"), False, KIND_LD2450),
    (bytes.fromhex("FDFCFBFA"), bytes.fromhex("04030201"), True, KIND_ACK),
)
_START_BYTES = {f[0][0] for f in _FORMATS}

# Command words
CMD_ENABLE_CONFIG = 0x00FF
CMD_END_CONFIG = 0x00FE
CMD_READ_VERSION = 0x00A0
CMD_SET_BAUD = 0x00A1
CMD_FACTORY_RESET = 0x00A2
CMD_RESTART = 0x00A3
CMD_BLUETOOTH = 0x00A4
CMD_READ_MAC = 0x00A5
LD2410_CMD_SET_MAX_GATES = 0x0060
LD2410_CMD_READ_PARAMS = 0x0061
LD2410_CMD_ENGINEERING_ON = 0x0062
LD2410_CMD_ENGINEERING_OFF = 0x0063
LD2410_CMD_SET_SENSITIVITY = 0x0064
LD2410_CMD_SET_RESOLUTION = 0x00AA
LD2410_CMD_READ_RESOLUTION = 0x00AB
LD2410_CMD_SET_AUX_CONTROL = 0x00AD
LD2410_CMD_READ_AUX_CONTROL = 0x00AE
LD2410_CMD_NOISE_DETECTION = 0x000B
LD2410_CMD_NOISE_STATUS = 0x001B
LD2450_CMD_SINGLE_TARGET = 0x0080
LD2450_CMD_MULTI_TARGET = 0x0090
LD2450_CMD_READ_TRACKING = 0x0091
LD2450_CMD_READ_ZONES = 0x00C1
LD2450_CMD_SET_ZONES = 0x00C2

COMMAND_NAMES = {
    CMD_ENABLE_CONFIG: "enable_config", CMD_END_CONFIG: "end_config", CMD_READ_VERSION: "read_version",
    CMD_SET_BAUD: "set_baud", CMD_FACTORY_RESET: "factory_reset", CMD_RESTART: "restart",
    CMD_BLUETOOTH: "bluetooth", CMD_READ_MAC: "read_mac", LD2410_CMD_SET_MAX_GATES: "ld2410_set_max_gates",
    LD2410_CMD_READ_PARAMS: "ld2410_read_params", LD2410_CMD_ENGINEERING_ON: "ld2410_engineering_on",
    LD2410_CMD_ENGINEERING_OFF: "ld2410_engineering_off", LD2410_CMD_SET_SENSITIVITY: "ld2410_set_sensitivity",
    LD2410_CMD_SET_RESOLUTION: "ld2410_set_resolution", LD2410_CMD_READ_RESOLUTION: "ld2410_read_resolution",
    LD2410_CMD_SET_AUX_CONTROL: "ld2410_set_aux_control", LD2410_CMD_READ_AUX_CONTROL: "ld2410_read_aux_control",
    LD2410_CMD_NOISE_DETECTION: "ld2410_noise_detection", LD2410_CMD_NOISE_STATUS: "ld2410_noise_status",
    LD2450_CMD_SINGLE_TARGET: "ld2450_single_target", LD2450_CMD_MULTI_TARGET: "ld2450_multi_target",
    LD2450_CMD_READ_TRACKING: "ld2450_read_tracking", LD2450_CMD_READ_ZONES: "ld2450_read_zones",
    LD2450_CMD_SET_ZONES: "ld2450_set_zones",
}

BAUD_RATES = (9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800)

LD2410_GATES = 9
LD2450_TARGETS = 3
LD2450_MAX_ABS_X_MM = 8000
LD2450_MAX_Y_MM = 8000
LD2450_MAX_ABS_SPEED_CM_S = 800


def u16(b: bytes, pos: int = 0) -> int:
    return b[pos] | (b[pos + 1] << 8)


def s16(b: bytes, pos: int = 0) -> int:
    """Two's complement int16 (LD2450 zone coordinates)."""
    v = u16(b, pos)
    return v - 0x10000 if v & 0x8000 else v


def ld2450_signed(b: bytes, pos: int = 0) -> int:
    """Sign-magnitude value of an LD2450 report: bit 15 set = positive."""
    raw = u16(b, pos)
    magnitude = raw & 0x7FFF
    return magnitude if raw & 0x8000 else -magnitude


@dataclass
class Frame:
    kind: str
    frame: bytes
    payload: bytes
    input_offset: int


class Parser:
    """Splits a byte stream into frames, like hlk_parser_feed()."""

    def __init__(self):
        self.buf = bytearray()
        self.frames = 0
        self.garbage = 0
        self.bad_length = 0
        self.bad_footer = 0

    def counters(self) -> str:
        return (f"frames={self.frames} garbage={self.garbage} bad_length={self.bad_length} "
                f"bad_footer={self.bad_footer}")

    def _drop_first(self):
        del self.buf[0]
        self.garbage += 1

    def _extract(self, offset: int, out: List[Frame]):
        while self.buf:
            fmt = None
            n = min(len(self.buf), HEADER_LEN)
            for f in _FORMATS:
                if self.buf[:n] == f[0][:n]:
                    fmt = f
                    break
            if fmt is None:
                self._drop_first()
                continue
            if len(self.buf) < HEADER_LEN:
                return
            header, footer, has_length, kind = fmt
            payload_len, payload_pos = LD2450_PAYLOAD_LEN, HEADER_LEN
            if has_length:
                if len(self.buf) < HEADER_LEN + 2:
                    return
                payload_len = u16(self.buf, HEADER_LEN)
                payload_pos = HEADER_LEN + 2
                if payload_len > MAX_PAYLOAD:
                    self.bad_length += 1
                    self._drop_first()
                    continue
            frame_len = payload_pos + payload_len + len(footer)
            if len(self.buf) < frame_len:
                return
            if self.buf[frame_len - len(footer):frame_len] != footer:
                self.bad_footer += 1
                self._drop_first()
                continue
            payload = bytes(self.buf[payload_pos:payload_pos + payload_len])
            if kind == KIND_ACK and (payload_len < 2 or not (u16(payload) & ACK_FLAG)):
                kind = KIND_COMMAND
            self.frames += 1
            out.append(Frame(kind, bytes(self.buf[:frame_len]), payload, offset))
            del self.buf[:frame_len]

    def feed(self, data: bytes) -> List[Frame]:
        out: List[Frame] = []
        for i, b in enumerate(data):
            if not self.buf and b not in _START_BYTES:
                self.garbage += 1
                continue
            self.buf.append(b)
            self._extract(i + 1, out)
        return out


class DecodeError(ValueError):
    """The frame is well formed but its payload is not; str() is the error name."""


@dataclass
class Ld2410Report:
    type: int
    state: int
    moving_cm: int
    moving_energy: int
    still_cm: int
    still_energy: int
    detection_cm: int
    moving_gate_energy: List[int] = field(default_factory=list)  # engineering reports only
    still_gate_energy: List[int] = field(default_factory=list)
    extra_len: int = 0
    light: Optional[int] = None  # extra byte 0 (LD2410B V1.07 / LD2410C V1.06 on)
    out_pin: Optional[int] = None  # extra byte 1

    @property
    def engineering(self) -> bool:
        return self.type == 0x01


def decode_ld2410_report(p: bytes) -> Ld2410Report:
    if len(p) < 13:
        raise DecodeError("short")
    if p[0] not in (0x01, 0x02):
        raise DecodeError("type")
    if p[1] != 0xAA or p[-2] != 0x55 or p[-1] != 0x00:
        raise DecodeError("marker")
    r = Ld2410Report(type=p[0], state=p[2], moving_cm=u16(p, 3), moving_energy=p[5], still_cm=u16(p, 6),
                     still_energy=p[8], detection_cm=u16(p, 9))
    if r.type == 0x02:
        r.extra_len = len(p) - 13
        return r
    if len(p) < 15:
        raise DecodeError("short")
    nm, ns = p[11] + 1, p[12] + 1
    if nm > LD2410_GATES or ns > LD2410_GATES or 13 + nm + ns + 2 > len(p):
        raise DecodeError("gates")
    r.moving_gate_energy = list(p[13:13 + nm])
    r.still_gate_energy = list(p[13 + nm:13 + nm + ns])
    extra = 13 + nm + ns
    r.extra_len = len(p) - 2 - extra
    if r.extra_len >= 2:
        r.light, r.out_pin = p[extra], p[extra + 1]
    return r


@dataclass
class Ld2450Target:
    present: bool
    x_mm: int = 0
    y_mm: int = 0
    speed_cm_s: int = 0  # positive = moving away (ESPHome reading, not in the Hi-Link documents)
    resolution_mm: int = 0

    @property
    def plausible(self) -> bool:
        if not self.present:
            return True
        return (0 <= self.y_mm <= LD2450_MAX_Y_MM and abs(self.x_mm) <= LD2450_MAX_ABS_X_MM
                and abs(self.speed_cm_s) <= LD2450_MAX_ABS_SPEED_CM_S)


def decode_ld2450_report(p: bytes) -> List[Ld2450Target]:
    if len(p) < LD2450_PAYLOAD_LEN:
        raise DecodeError("short")
    targets = []
    for i in range(LD2450_TARGETS):
        t = p[i * 8:i * 8 + 8]
        if not any(t):
            targets.append(Ld2450Target(False))
            continue
        targets.append(Ld2450Target(True, ld2450_signed(t, 0), ld2450_signed(t, 2), ld2450_signed(t, 4), u16(t, 6)))
    return targets


@dataclass
class Ack:
    command: int
    status: int
    value: bytes


def decode_ack(p: bytes) -> Ack:
    if len(p) < 4:
        raise DecodeError("short")
    word = u16(p)
    if not word & ACK_FLAG:
        raise DecodeError("type")
    return Ack(word & ~ACK_FLAG, u16(p, 2), bytes(p[4:]))


def format_version(major: int, minor: int) -> str:
    return f"V{major >> 8}.{major & 0xFF:02X}.{minor:08X}"


def decode_version(ack: Ack) -> Tuple[int, str]:
    """Firmware type and version text, e.g. (1, 'V2.44.24073110')."""
    v = ack.value
    if len(v) < 8:
        raise DecodeError("short")
    minor = v[4] | (v[5] << 8) | (v[6] << 16) | (v[7] << 24)
    return u16(v), format_version(u16(v, 2), minor)


def decode_mac(ack: Ack) -> Tuple[str, bool]:
    """MAC text and whether the radar's Bluetooth is on (08:05:04:03:02:01 = off, per ESPHome)."""
    v = ack.value
    if len(v) < 6:
        raise DecodeError("short")
    mac = bytes(v[:6])
    on = mac not in (bytes.fromhex("080504030201"), bytes(6), b"\xff" * 6)
    return ":".join(f"{b:02X}" for b in mac), on


def decode_ld2410_params(ack: Ack) -> dict:
    v = ack.value
    if len(v) < 4:
        raise DecodeError("short")
    if v[0] != 0xAA:
        raise DecodeError("marker")
    gates = v[1] + 1
    if gates > LD2410_GATES:
        raise DecodeError("gates")
    if len(v) < 4 + 2 * gates + 2:
        raise DecodeError("short")
    return {
        "gates": gates, "max_moving_gate": v[2], "max_still_gate": v[3],
        "moving_threshold": list(v[4:4 + gates]), "still_threshold": list(v[4 + gates:4 + 2 * gates]),
        "no_one_duration_s": u16(v, 4 + 2 * gates),
    }


def decode_ld2450_zones(ack: Ack) -> dict:
    v = ack.value
    if len(v) < 26:
        raise DecodeError("short")
    zones = [[s16(v, 2 + i * 8 + j * 2) for j in range(4)] for i in range(3)]
    if u16(v) > 2:
        raise DecodeError("value")
    return {"type": u16(v), "zones": zones}


def describe_ack(ack: Ack) -> str:
    """Short decoded text of a reply, for the CSV and the report."""
    try:
        c, v = ack.command, ack.value
        if ack.status != 0:
            return "failed"
        if c == CMD_READ_VERSION:
            return "firmware %s (type %d)" % (decode_version(ack)[1], decode_version(ack)[0])
        if c == CMD_READ_MAC:
            mac, on = decode_mac(ack)
            return f"bluetooth {'on' if on else 'off'}" + (f" mac {mac}" if on else "")
        if c == CMD_ENABLE_CONFIG and len(v) >= 4:
            return f"protocol {u16(v)} buffer {u16(v, 2)}"
        if c == LD2410_CMD_READ_PARAMS:
            p = decode_ld2410_params(ack)
            return ("max gates moving %d still %d, no-one %d s, moving thresholds %s, still thresholds %s"
                    % (p["max_moving_gate"], p["max_still_gate"], p["no_one_duration_s"],
                       ",".join(map(str, p["moving_threshold"])), ",".join(map(str, p["still_threshold"]))))
        if c == LD2410_CMD_READ_RESOLUTION and len(v) >= 2:
            return {0: "gate 0.75 m", 1: "gate 0.2 m"}.get(u16(v), f"resolution {u16(v)}?")
        if c == LD2410_CMD_READ_AUX_CONTROL and len(v) >= 3:
            return f"light mode {v[0]} threshold {v[1]} OUT idle level {v[2]}"
        if c == LD2410_CMD_NOISE_STATUS and len(v) >= 2:
            return {0: "noise detection idle", 1: "noise detection running", 2: "noise detection done"}.get(
                u16(v), f"noise status {u16(v)}?")
        if c == LD2450_CMD_READ_TRACKING and len(v) >= 2:
            return {1: "single target", 2: "multi target"}.get(u16(v), f"tracking {u16(v)}?")
        if c == LD2450_CMD_READ_ZONES:
            z = decode_ld2450_zones(ack)
            return f"zone type {z['type']} zones {z['zones']}"
        return "ok"
    except DecodeError as e:
        return f"bad reply ({e})"


def event_text(f: Frame) -> str:
    """Same text as event_text() of test/host/test_radar_proto.cpp."""
    if f.kind == KIND_LD2410:
        try:
            r = decode_ld2410_report(f.payload)
        except DecodeError as e:
            return f"E2410 err={e}"
        s = (f"R2410 t={r.type} st={r.state} mcm={r.moving_cm} me={r.moving_energy} scm={r.still_cm} "
             f"se={r.still_energy} dcm={r.detection_cm}")
        if r.engineering:
            s += (f" nm={len(r.moving_gate_energy)} ns={len(r.still_gate_energy)} "
                  f"m={','.join(map(str, r.moving_gate_energy))} s={','.join(map(str, r.still_gate_energy))}")
        s += f" x={r.extra_len}"
        if r.extra_len >= 2:
            s += f" light={r.light} out={r.out_pin}"
        return s
    if f.kind == KIND_LD2450:
        try:
            targets = decode_ld2450_report(f.payload)
        except DecodeError as e:
            return f"E2450 err={e}"
        s = f"R2450 n={sum(t.present for t in targets)}"
        for i, t in enumerate(targets):
            if not t.present:
                s += f" t{i + 1}=-"
            else:
                s += (f" t{i + 1}={t.x_mm},{t.y_mm},{t.speed_cm_s},{t.resolution_mm}"
                      + ("" if t.plausible else "*"))
        return s
    if f.kind == KIND_ACK:
        try:
            a = decode_ack(f.payload)
        except DecodeError as e:
            return f"EACK err={e}"
        return f"ACK c={a.command:04X} st={a.status} v={a.value.hex().upper()}"
    return f"CMD c={u16(f.payload):04X} v={f.payload[2:].hex().upper()}"


def build_command(command: int, value: bytes = b"") -> bytes:
    data = command.to_bytes(2, "little") + value
    return bytes.fromhex("FDFCFBFA") + len(data).to_bytes(2, "little") + data + bytes.fromhex("04030201")
