"""Small helpers to write KiCad S-expression files (symbols, footprints, schematic)."""

import os
import re
import uuid

HW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))  # hardware/ = KiCad project folder


def _stock(env, sub):
    """KiCad 9 stock library folder: environment variable, else the usual install locations."""
    for d in [os.environ.get(env, ""), f"/usr/share/kicad/{sub}",
              f"/Applications/KiCad/KiCad.app/Contents/SharedSupport/{sub}",
              f"C:/Program Files/KiCad/9.0/share/kicad/{sub}"]:
        if d and os.path.isdir(d):
            return d
    raise SystemExit(f"KiCad {sub} library not found, set {env}")


KICAD_SYM = _stock("KICAD9_SYMBOL_DIR", "symbols")
KICAD_FP = _stock("KICAD9_FOOTPRINT_DIR", "footprints")

_NS = uuid.UUID("6f1d3c52-5d2e-4c55-9a43-1f0c2b6d7e01")


def uid(*parts):
    """Deterministic UUID: regenerating the files keeps every UUID (and the board-to-schematic links) stable."""
    return str(uuid.uuid5(_NS, "/".join(str(p) for p in parts)))


def num(v):
    """KiCad number format: no trailing zeros, no -0."""
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def font(size=1.27, thickness=None):
    t = f" (thickness {num(thickness)})" if thickness else ""
    return f"(font (size {num(size)} {num(size)}){t})"


def effects(size=1.27, justify=None, hide=False, thickness=None):
    j = f" (justify {justify})" if justify else ""
    h = " (hide yes)" if hide else ""
    return f"(effects {font(size, thickness)}{j}{h})"


def block(text, name):
    """Return the balanced S-expression starting at '(symbol "<name>"' (or any '(<head> "<name>"') in text."""
    m = re.search(r'\((?:symbol|footprint) "' + re.escape(name) + '"', text)
    if not m:
        raise KeyError(name)
    depth = 0
    for j in range(m.start(), len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[m.start():j + 1]
    raise ValueError(name)


def stock_symbol(lib_id):
    """Symbol text of a KiCad stock library symbol, e.g. 'Device:R'."""
    lib, name = lib_id.split(":")
    with open(os.path.join(KICAD_SYM, lib + ".kicad_sym")) as f:
        return block(f.read(), name)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
