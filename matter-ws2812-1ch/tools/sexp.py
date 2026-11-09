"""Minimal S-expression reader/writer for KiCad files."""
import re

_TOKEN = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')


class Sym(str):
    """Bare (unquoted) atom."""


def parse(text):
    pos = 0
    stack = [[]]
    n = len(text)
    while pos < n:
        m = _TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise ValueError("bad token at %d: %r" % (pos, text[pos:pos + 40]))
        pos = m.end()
        lp, rp, s, atom = m.groups()
        if lp:
            stack.append([])
        elif rp:
            node = stack.pop()
            stack[-1].append(node)
        elif s is not None:
            stack[-1].append(bytes(s[1:-1], "utf-8").decode("unicode_escape")
                             if "\\" in s else s[1:-1])
        elif atom is not None:
            stack[-1].append(Sym(atom))
    return stack[0][0] if len(stack[0]) == 1 else stack[0]


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def dump(node, indent=0):
    """Serialise a parsed tree back to KiCad-style text."""
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, str):
        return q(node)
    if isinstance(node, (int,)):
        return str(node)
    if isinstance(node, float):
        return fmt(node)
    # list
    if not node:
        return "()"
    simple = all(not isinstance(x, list) for x in node)
    if simple:
        return "(" + " ".join(dump(x) for x in node) + ")"
    pad = "\t" * (indent + 1)
    out = "(" + dump(node[0])
    for x in node[1:]:
        if isinstance(x, list):
            out += "\n" + pad + dump(x, indent + 1)
        else:
            out += " " + dump(x)
    out += "\n" + "\t" * indent + ")"
    return out


def fmt(v):
    v = round(float(v), 4)
    if v == int(v):
        return str(int(v))
    return ("%.4f" % v).rstrip("0").rstrip(".")


def find(node, key):
    for x in node:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def find_all(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]
