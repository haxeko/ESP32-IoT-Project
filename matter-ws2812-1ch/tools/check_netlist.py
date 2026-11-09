#!/usr/bin/env python3
"""Compare the netlist exported by KiCad (kicad-cli sch export netlist) with design.py.

Usage: check_netlist.py <netlist.net>
Every (ref, pin) must sit on the net design.py asks for, and every net must have
exactly the pins design.py lists (no extra pins, no merged nets).
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
from sexp import find, find_all, parse  # noqa: E402


def expected():
    nets = defaultdict(set)
    nc = set()
    for p in design.PARTS:
        conns = dict(p["pins"])
        for _u, (_at, _rot, c) in p["extra_units"].items():
            conns.update(c)
        for pin, net in conns.items():
            if net is None:
                nc.add((p["ref"], pin))
            else:
                nets[net].add((p["ref"], pin))
    return nets, nc


def actual(path):
    tree = parse(open(path, encoding="utf-8").read())
    nets = {}
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1].lstrip("/")
        nodes = {(find(x, "ref")[1], find(x, "pin")[1]) for x in find_all(n, "node")}
        nodes = {x for x in nodes if not x[0].startswith("#")}
        if nodes:
            nets[name] = nodes
    return nets


def main():
    (exp, nc), act = expected(), actual(sys.argv[1])
    errors = 0
    for net in [n for n in act if n.startswith("unconnected-")]:
        if act[net] <= nc:
            del act[net]
    pin_net = {pin: net for net, pins in act.items() for pin in pins}
    for net, pins in sorted(exp.items()):
        for pin in sorted(pins):
            got = pin_net.get(pin)
            if got != net:
                print("MISMATCH %s.%s: want %s, got %s" % (pin[0], pin[1], net, got))
                errors += 1
        if net in act and act[net] != pins:
            extra = act[net] - pins
            if extra:
                print("EXTRA on %s: %s" % (net, sorted(extra)))
                errors += 1
    for net, pins in act.items():
        if net not in exp and not net.startswith("unconnected-") and not net.startswith("Net-("):
            print("UNEXPECTED net %s: %s" % (net, sorted(pins)))
            errors += 1
        if net.startswith("Net-(") or net.startswith("unconnected-"):
            print("UNNAMED net %s: %s" % (net, sorted(pins)))
            errors += 1
    print("nets expected %d, found %d, errors %d" % (len(exp), len(act), errors))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
