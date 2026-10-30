#!/usr/bin/env python3
"""Small two-layer grid router for this board (pcbnew + a C A* core).

The board from build_pcb.py already has its power zones (VIN, VLED, VOUTx on F.Cu,
GND on both layers). This script routes every remaining connection with exact
clearances computed from the KiCad pad/zone polygons, then refills the zones.
GND pads on the top layer are tied to the bottom plane with a via next to the pad.

usage: autoroute.py <board.kicad_pcb>
"""
import ctypes
import math
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import layout  # noqa: E402

G = 0.05                    # grid pitch, mm
MARGIN = 0.012              # polygon approximation + rounding allowance, mm
EDGE_CLEAR = 0.4
HW_CLASSES = (0.125, 0.2, 0.25, 0.3, 0.4, 0.5)   # track / via half widths
POWER = {"VIN", "VLED", "VOUT1", "VOUT2", "VOUT3"}

# net: (track width, via diameter, via drill)
WIDTH = {
    "SW_NODE": (0.8, 0.6, 0.3), "V_SYS": (0.6, 0.6, 0.3), "+3V3": (0.5, 0.6, 0.3),
    "VBUS": (0.5, 0.6, 0.3), "+5V": (0.4, 0.6, 0.3), "BST": (0.4, 0.6, 0.3),
    "VLED": (0.6, 0.8, 0.4), "GND": (0.5, 0.6, 0.3), "Q1_G": (0.4, 0.6, 0.3),
}
DEFAULT_WIDTH = (0.25, 0.6, 0.3)
ORDER = ["GND", "SW_NODE", "BST", "V_SYS", "STATUS_LED", "BOOT", "EN", "DATA_IN1", "DATA_IN2",
         "DATA_IN3", "+3V3", "USB_DP", "USB_DN", "SW_P1", "SW_P2", "SW_P3", "SW_P4", "CC1", "CC2",
         "VBUS", "VLED", "+5V", "Q1_G", "DATA_BUF1", "DATA_BUF2", "DATA_BUF3", "DATA_OUT1",
         "DATA_OUT2", "DATA_OUT3"]

def base(net):
    return net.lstrip("/")


MM = pcbnew.ToMM
IU = pcbnew.FromMM


# ----------------------------------------------------------------------------- geometry
def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    ll = dx * dx + dy * dy
    if ll == 0:
        return np.hypot(px - ax, py - ay)
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / ll, 0.0, 1.0)
    return np.hypot(px - (ax + t * dx), py - (ay + t * dy))


def poly_dist(px, py, pts):
    """Distance from points to a closed polygon region (0 inside)."""
    n = len(pts)
    d = np.full(px.shape, np.inf)
    inside = np.zeros(px.shape, dtype=bool)
    for k in range(n):
        ax, ay = pts[k]
        bx, by = pts[(k + 1) % n]
        d = np.minimum(d, seg_dist(px, py, ax, ay, bx, by))
        cond = (ay > py) != (by > py)
        with np.errstate(divide="ignore", invalid="ignore"):
            xint = ax + (py - ay) * (bx - ax) / (by - ay)
        inside ^= cond & (px < xint)
    d[inside] = 0.0
    return d


def outline_pts(chain):
    return [(MM(chain.CPoint(k).x), MM(chain.CPoint(k).y)) for k in range(chain.PointCount())]


def polyset_outlines(ps):
    return [outline_pts(ps.Outline(k)) for k in range(ps.OutlineCount())]


# ----------------------------------------------------------------------------- router
class Router:
    def __init__(self, board):
        self.b = board
        self.x0, self.y0 = layout.ORIGIN
        self.nx = int(round(layout.W / G)) + 1
        self.ny = int(round(layout.H / G)) + 1
        self.n = self.nx * self.ny
        names = sorted({p.GetNetname() for p in board.GetPads() if p.GetNetname()} |
                       {z.GetNetname() for z in board.Zones() if z.GetNetname()})
        self.bit = {nm: k + 1 for k, nm in enumerate(names)}
        assert len(self.bit) < 63
        self.occ = {hw: np.zeros((2, self.ny, self.nx), dtype=np.uint64) for hw in HW_CLASSES}
        self.novia = np.zeros((self.ny, self.nx), dtype=bool)
        self.cost = np.ones((2, self.ny, self.nx), dtype=np.float32)
        self.cost[1] = 1.6
        self.copper = {}            # net -> list of (layer, mask bbox slice, bool mask)
        self.new_tracks = []
        self.failed = set()
        self.lib = self._load_lib()
        n9 = 2 * self.n * 9
        self.g = np.empty(n9, dtype=np.float32)
        self.frm = np.empty(n9, dtype=np.int32)

    # ---------------------------------------------------------------- helpers
    def _load_lib(self):
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "astar.c")
        out = os.path.join(tempfile.gettempdir(), "c3ws_astar_%d.so" % int(os.path.getmtime(src)))
        if not os.path.exists(out):
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-o", out, src, "-lm"], check=True)
        lib = ctypes.CDLL(out)
        P = ctypes.c_void_p
        lib.astar.argtypes = [ctypes.c_int, ctypes.c_int, P, P, P, P, P, P, P, P, ctypes.c_int,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_float, ctypes.c_float, ctypes.c_float, P, P, P, ctypes.c_int,
                              ctypes.c_long]
        lib.astar.restype = ctypes.c_int
        return lib

    def cell(self, x, y):
        return int(round((x - self.x0) / G)), int(round((y - self.y0) / G))

    def window(self, xmin, ymin, xmax, ymax, pad):
        i0 = max(0, int(math.floor((xmin - pad - self.x0) / G)))
        j0 = max(0, int(math.floor((ymin - pad - self.y0) / G)))
        i1 = min(self.nx - 1, int(math.ceil((xmax + pad - self.x0) / G)))
        j1 = min(self.ny - 1, int(math.ceil((ymax + pad - self.y0) / G)))
        if i1 < i0 or j1 < j0:
            return None
        ii, jj = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1))
        return (slice(j0, j1 + 1), slice(i0, i1 + 1)), self.x0 + ii * G, self.y0 + jj * G

    def stamp(self, layers, bit, dist, sl, clear):
        b = np.uint64(1 << bit)
        for hw in HW_CLASSES:
            m = dist < hw + clear + MARGIN
            for L in layers:
                self.occ[hw][L][sl][m] |= b

    def add_copper(self, net, layers, sl, mask):
        if net:
            self.copper.setdefault(net, []).append((tuple(layers), sl, mask))

    # ---------------------------------------------------------------- obstacles
    def load(self):
        b = self.b
        R = max(HW_CLASSES) + 0.3 + MARGIN
        for fp in b.GetFootprints():
            for pad in fp.Pads():
                net = pad.GetNetname()
                layers = [L for L, lid in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu)) if pad.IsOnLayer(lid)]
                attr = pad.GetAttribute()
                if attr == pcbnew.PAD_ATTRIB_NPTH or not layers:
                    pos = pad.GetPosition()
                    r = MM(max(pad.GetDrillSize().x, pad.GetDrillSize().y)) / 2
                    win = self.window(MM(pos.x) - r, MM(pos.y) - r, MM(pos.x) + r, MM(pos.y) + r, R)
                    if win:
                        sl, px, py = win
                        d = np.hypot(px - MM(pos.x), py - MM(pos.y)) - r
                        self.stamp((0, 1), 0, d, sl, 0.25)
                        self.novia[sl] |= d < 0.4 + 0.25
                    continue
                lid = pcbnew.F_Cu if layers[0] == 0 else pcbnew.B_Cu
                ps = pad.GetEffectivePolygon(lid, pcbnew.ERROR_INSIDE)
                for pts in polyset_outlines(ps):
                    xs, ys = zip(*pts)
                    win = self.window(min(xs), min(ys), max(xs), max(ys), R)
                    if not win:
                        continue
                    sl, px, py = win
                    d = poly_dist(px, py, pts)
                    self.stamp(layers, self.bit.get(net, 0), d, sl, 0.3 if base(net) in POWER else 0.2)
                    self.add_copper(net, layers, sl, d <= 0.0)
                    if attr == pcbnew.PAD_ATTRIB_SMD:
                        self.novia[sl] |= d < 0.45
            if fp.GetReference() == "U1":
                # no top-layer routing under the module body, keep the bottom under it for GND
                c = fp.GetPosition()
                cx, cy = MM(c.x), MM(c.y)
                win = self.window(cx - 7.6, cy - 12.9, cx + 7.6, cy + 6.6, 0)
                sl, px, py = win
                self.occ_hard((0,), sl, np.ones(px.shape, dtype=bool))
                self.cost[1][sl] = 4.0
        zones = list(b.Zones()) + [z for fp in b.GetFootprints() for z in fp.Zones()]
        for z in zones:
            pts_list = polyset_outlines(z.Outline())
            if z.GetIsRuleArea():
                if not z.GetDoNotAllowTracks():
                    continue
                layers = [L for L, lid in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu)) if z.IsOnLayer(lid)]
                for pts in pts_list:
                    xs, ys = zip(*pts)
                    win = self.window(min(xs), min(ys), max(xs), max(ys), R)
                    if win:
                        sl, px, py = win
                        d = poly_dist(px, py, pts)
                        self.stamp(layers, 0, d, sl, 0.0)
                        self.novia[sl] |= d < 0.4
                continue
            net = z.GetNetname()
            if base(net) in POWER:
                for pts in pts_list:
                    xs, ys = zip(*pts)
                    win = self.window(min(xs), min(ys), max(xs), max(ys), R)
                    sl, px, py = win
                    d = poly_dist(px, py, pts)
                    self.stamp((0,), self.bit[net], d, sl, 0.3)
                    self.add_copper(net, (0,), sl, d <= 0.0)
            elif net == "GND" and z.IsOnLayer(pcbnew.B_Cu):
                self.gnd_plane = True
        # board edge
        ii, jj = np.meshgrid(np.arange(self.nx), np.arange(self.ny))
        x, y = ii * G, jj * G
        r = layout.CORNER_R
        qx = np.abs(x - layout.W / 2) - (layout.W / 2 - r)
        qy = np.abs(y - layout.H / 2) - (layout.H / 2 - r)
        sdf = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
        for hw in HW_CLASSES:
            m = sdf > -(EDGE_CLEAR + hw + MARGIN)
            self.occ[hw][0][m] |= np.uint64(1)
            self.occ[hw][1][m] |= np.uint64(1)
        self.novia |= sdf > -(EDGE_CLEAR + 0.45)
        # keep the bottom plane whole under the high current part of the board
        self.cost[1][jj * G > 49.0] = 3.0

    def occ_hard(self, layers, sl, mask):
        for hw in HW_CLASSES:
            for L in layers:
                self.occ[hw][L][sl][mask] |= np.uint64(1)

    # ---------------------------------------------------------------- routing
    def free_maps(self, net, hw):
        nb = ~np.uint64(1 << self.bit[net])
        f = [np.ascontiguousarray(((self.occ[hw][L] & nb) == 0).astype(np.uint8)) for L in (0, 1)]
        return f

    def components(self, net, free):
        """Connected copper groups of a net, as boolean full-size masks per layer."""
        items = []
        for layers, sl, m in self.copper.get(net, []):
            full = np.zeros((2, self.ny, self.nx), dtype=bool)
            for L in layers:
                full[L][sl] = m
            items.append(full)
        if net == "GND":
            plane = np.zeros((2, self.ny, self.nx), dtype=bool)
            plane[1] = free[1].astype(bool)
            items.append(plane)
        groups = []
        for it in items:
            keep = []
            for g in groups:
                if (g & it).any():
                    it = it | g
                else:
                    keep.append(g)
            groups = keep + [it]
        return groups

    def route_net(self, net):
        tw, vd, vdr = WIDTH.get(base(net), DEFAULT_WIDTH)
        hw = tw / 2
        hv = vd / 2
        if base(net) in POWER:
            # power class clearance is 0.3 mm: use the next wider occupancy class
            hw = min(c for c in HW_CLASSES if c >= hw + 0.1 - 1e-9)
            hv = min(c for c in HW_CLASSES if c >= hv + 0.1 - 1e-9)
        free = self.free_maps(net, hw)
        fv = self.free_maps(net, hv)
        viaok = np.ascontiguousarray((fv[0] & fv[1] & ~self.novia).astype(np.uint8))
        groups = self.components(net, free)
        if len(groups) <= 1:
            return 0
        groups.sort(key=lambda g: -int(g.sum()))
        tree = groups.pop(0)
        if net == "GND":
            # every pad group connects straight to the plane
            pass
        routed = 0
        while groups:
            # nearest group to the tree
            tb = self._bbox(tree)
            groups.sort(key=lambda g: self._bbox_dist(self._bbox(g), tb))
            src = groups.pop(0)
            path = self._search(src & np.stack([free[0], free[1]]).astype(bool),
                                tree & np.stack([free[0], free[1]]).astype(bool),
                                free, viaok, net)
            if path is None:
                print("   !! unrouted connection on", net)
                self.failed.add(net)
                continue
            self._commit(net, path, tw, vd, vdr)
            for L, i, j in path:
                tree[L, j, i] = True
            tree |= src
            if net != "GND":
                free = self.free_maps(net, hw)
            routed += 1
        return routed

    def _bbox(self, m):
        idx = np.argwhere(m.any(axis=0))
        (j0, i0), (j1, i1) = idx.min(axis=0), idx.max(axis=0)
        return i0, j0, i1, j1

    @staticmethod
    def _bbox_dist(a, b):
        dx = max(0, a[0] - b[2], b[0] - a[2])
        dy = max(0, a[1] - b[3], b[1] - a[3])
        return math.hypot(dx, dy)

    def _search(self, src_mask, goal_mask, free, viaok, net):
        if not src_mask.any() or not goal_mask.any():
            print("   !! no free copper cells for", net)
            return None
        src_idx = np.flatnonzero(src_mask.reshape(-1)).astype(np.int32)
        if len(src_idx) > 20000:
            src_idx = src_idx[:: max(1, len(src_idx) // 20000)]
        gx0, gy0, gx1, gy1 = self._bbox(goal_mask)
        g0 = np.ascontiguousarray(goal_mask[0].astype(np.uint8))
        g1 = np.ascontiguousarray(goal_mask[1].astype(np.uint8))
        c0 = np.ascontiguousarray(self.cost[0])
        c1 = np.ascontiguousarray(self.cost[1])
        maxpath = 200000
        path = np.zeros(maxpath, dtype=np.int32)
        via_cost = 40.0 if base(net) != "GND" else 6.0
        r = self.lib.astar(self.nx, self.ny, free[0].ctypes.data, free[1].ctypes.data, viaok.ctypes.data,
                           c0.ctypes.data, c1.ctypes.data, g0.ctypes.data, g1.ctypes.data,
                           src_idx.ctypes.data, len(src_idx), int(gx0), int(gy0), int(gx1), int(gy1),
                           ctypes.c_float(via_cost), ctypes.c_float(4.0), ctypes.c_float(10.0),
                           self.g.ctypes.data, self.frm.ctypes.data, path.ctypes.data, maxpath,
                           ctypes.c_long(60_000_000))
        if r < 0:
            return None
        cells = []
        for c in path[:r][::-1]:
            L, rem = divmod(int(c), self.n)
            j, i = divmod(rem, self.nx)
            cells.append((L, i, j))
        return cells

    def _commit(self, net, cells, tw, vd, vdr):
        """Turn a cell path into straight track segments and vias; stamp them as obstacles."""
        pts = []
        for k, (L, i, j) in enumerate(cells):
            x, y = self.x0 + i * G, self.y0 + j * G
            if pts and pts[-1][0] != L:
                self.new_tracks.append(("via", net, (x, y), vd, vdr))
                self._stamp_via(net, x, y, vd / 2)
            pts.append((L, x, y))
        # split into same-layer runs and keep only direction changes
        run = [pts[0]]
        for p in pts[1:] + [None]:
            if p is not None and p[0] == run[-1][0]:
                run.append(p)
                continue
            if len(run) >= 2:
                verts = [run[0]]
                for a, b2, c in zip(run, run[1:], run[2:]):
                    d1 = (round((b2[1] - a[1]) / G), round((b2[2] - a[2]) / G))
                    d2 = (round((c[1] - b2[1]) / G), round((c[2] - b2[2]) / G))
                    if d1 != d2:
                        verts.append(b2)
                verts.append(run[-1])
                for a, b2 in zip(verts, verts[1:]):
                    self.new_tracks.append(("seg", net, a[0], (a[1], a[2]), (b2[1], b2[2]), tw))
                    self._stamp_seg(net, a[0], a[1], a[2], b2[1], b2[2], tw / 2)
            run = [p] if p is not None else []

    def _stamp_seg(self, net, L, ax, ay, bx, by, hw_t):
        R = hw_t + max(HW_CLASSES) + 0.3 + MARGIN
        win = self.window(min(ax, bx), min(ay, by), max(ax, bx), max(ay, by), R)
        sl, px, py = win
        d = seg_dist(px, py, ax, ay, bx, by) - hw_t
        self.stamp((L,), self.bit[net], d, sl, 0.3 if base(net) in POWER else 0.2)
        self.add_copper(net, (L,), sl, d <= 0.0)

    def _stamp_via(self, net, x, y, r):
        R = r + max(HW_CLASSES) + 0.3 + MARGIN
        win = self.window(x, y, x, y, R)
        sl, px, py = win
        d = np.hypot(px - x, py - y) - r
        self.stamp((0, 1), self.bit[net], d, sl, 0.3 if base(net) in POWER else 0.2)
        self.add_copper(net, (0, 1), sl, d <= 0.0)
        self.novia[sl] |= d < r + 0.25

    # ---------------------------------------------------------------- output
    def apply(self):
        b = self.b
        for t in self.new_tracks:
            if t[0] == "seg":
                _, net, L, (ax, ay), (bx, by), w = t
                if (ax, ay) == (bx, by):
                    continue
                tr = pcbnew.PCB_TRACK(b)
                tr.SetStart(pcbnew.VECTOR2I(IU(ax), IU(ay)))
                tr.SetEnd(pcbnew.VECTOR2I(IU(bx), IU(by)))
                tr.SetWidth(IU(w))
                tr.SetLayer(pcbnew.F_Cu if L == 0 else pcbnew.B_Cu)
                tr.SetNet(b.FindNet(net))
                b.Add(tr)
            else:
                _, net, (x, y), vd, vdr = t
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pcbnew.VECTOR2I(IU(x), IU(y)))
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                v.SetWidth(IU(vd))
                v.SetDrill(IU(vdr))
                v.SetNet(b.FindNet(net))
                b.Add(v)


def route_once(path, order):
    board = pcbnew.LoadBoard(path)
    for t in list(board.GetTracks()):
        board.Remove(t)
    r = Router(board)
    r.load()
    for net in order:
        if net in r.bit:
            r.route_net(net)
    return board, r


def main():
    path = sys.argv[1]
    passes = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    t0 = time.time()
    probe = Router(pcbnew.LoadBoard(path))
    names = {base(n): n for n in probe.bit}
    order = [names[n] for n in ORDER if n in names]
    order += sorted(n for n in probe.bit if n not in order and base(n) not in POWER
                    and not n.startswith("unconnected-"))
    best = None
    for k in range(passes):
        board, r = route_once(path, order)
        print("pass %d: %d failed nets %s (%.0fs)" % (k + 1, len(r.failed), sorted(r.failed), time.time() - t0))
        if best is None or len(r.failed) < len(best[1].failed):
            best = (board, r)
        if not r.failed:
            break
        # failed nets go first next time
        order = [n for n in order if n in r.failed] + [n for n in order if n not in r.failed]
    board, r = best
    r.apply()
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    pcbnew.SaveBoard(path, board)
    print("saved", path, "tracks:", len(board.GetTracks()), "failed:", sorted(r.failed),
          "in %.0fs" % (time.time() - t0))
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
