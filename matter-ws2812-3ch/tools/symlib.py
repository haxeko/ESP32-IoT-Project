"""Load KiCad symbol libraries, flatten derived symbols and list pins."""
import copy
import os

from sexp import Sym, find, find_all, parse

KICAD_SYM_DIR = os.environ.get("KICAD9_SYMBOL_DIR", "/usr/share/kicad/symbols")

_cache = {}


def _lib(path):
    if path not in _cache:
        with open(path, encoding="utf-8") as f:
            tree = parse(f.read())
        _cache[path] = {x[1]: x for x in find_all(tree, "symbol")}
    return _cache[path]


def lib_path(lib, local_libs):
    if lib in local_libs:
        return local_libs[lib]
    return os.path.join(KICAD_SYM_DIR, lib + ".kicad_sym")


def load_symbol(lib_id, local_libs=None):
    """Return a flattened copy of the symbol named lib_id ("Lib:Name").

    The top-level name becomes lib_id (as KiCad stores it inside a
    schematic); unit sub-symbols keep the bare name prefix.
    """
    local_libs = local_libs or {}
    lib, name = lib_id.split(":", 1)
    syms = _lib(lib_path(lib, local_libs))
    sym = copy.deepcopy(syms[name])
    ext = find(sym, "extends")
    if ext:
        parent = copy.deepcopy(syms[ext[1]])
        pname = ext[1]
        # child properties override parent's; graphics/units come from the parent
        child_props = {p[1]: p for p in find_all(sym, "property")}
        flat = [Sym("symbol"), name]
        for item in parent[2:]:
            if isinstance(item, list) and item[0] == "property":
                flat.append(child_props.pop(item[1], item))
            elif isinstance(item, list) and item[0] == "symbol":
                sub = copy.deepcopy(item)
                sub[1] = name + sub[1][len(pname):]
                flat.append(sub)
            else:
                flat.append(item)
        # remaining child-only properties go after the parent's ones
        insert_at = max(i for i, x in enumerate(flat)
                        if isinstance(x, list) and x[0] == "property") + 1
        for p in child_props.values():
            flat.insert(insert_at, p)
            insert_at += 1
        # keep child's own non-property flags (e.g. in_bom) when present
        sym = flat
    sym[1] = lib_id
    return sym


def pins(sym):
    """List pins: dict(number, name, type, x, y, angle, length, unit)."""
    out = []
    base = sym[1].split(":", 1)[-1]
    for sub in find_all(sym, "symbol"):
        suffix = sub[1][len(base) + 1:] if sub[1].startswith(base + "_") else sub[1].rsplit("_", 2)[-2] + "_" + sub[1].rsplit("_", 1)[-1]
        unit_s, style_s = suffix.split("_")
        unit, style = int(unit_s), int(style_s)
        if style not in (0, 1):
            continue
        for p in find_all(sub, "pin"):
            at = find(p, "at")
            length = find(p, "length")
            name = find(p, "name")
            number = find(p, "number")
            hidden = any(isinstance(x, Sym) and x == "hide" for x in p) or \
                (find(p, "hide") is not None and find(p, "hide")[1] == "yes")
            out.append(dict(number=str(number[1]), name=str(name[1]), type=str(p[1]),
                            shape=str(p[2]), x=float(at[1]), y=float(at[2]),
                            angle=float(at[3]) if len(at) > 3 else 0.0,
                            length=float(length[1]), unit=unit, hidden=hidden))
    return out


def units(sym):
    us = set()
    base = sym[1].split(":", 1)[-1]
    for sub in find_all(sym, "symbol"):
        u = int(sub[1][len(base) + 1:].split("_")[0])
        if u:
            us.add(u)
    return sorted(us) or [1]


def prop(sym, key):
    for p in find_all(sym, "property"):
        if p[1] == key:
            return p[2]
    return None
