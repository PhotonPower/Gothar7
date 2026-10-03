"""Geometry of the Leonberg castle model (W6), pure Python (runs in Blender and in the test venv).

The model is built from named parts in the worldgen frame (metres, +X east, +Y up, +Z south):
wings with stone socle, quoins, window rows with stone surrounds and saddle roofs (Renaissance
scroll gables at the marked ends), roof dormers, an octagonal stair tower with a bell roof and
lantern, a corner oriel, a portal and chimneys. Collision: one closed convex body per wing and
one for the tower (``COL_HULL_*``). Parameters: ``data/<site>/schloss.json``; colours: the
building palette. ``build_schloss.py`` turns the result into Blender objects and the ``.glb``.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

Vec3 = tuple[float, float, float]

SOCLE_M = 1.2
SINK_M = 1.0
WINDOW = (1.2, 1.8)  # width, height
WINDOW_SPACING = 3.3
REVEAL = 0.18
SURROUND = 0.16  # width of the stone surround
SURROUND_DEPTH = 0.06
OVERHANG = 0.35
ROOF_T = 0.22
GABLE_T = 0.45
NORTH_ROOF = -0.25
TERRAIN_F = (0.1, 0.3, 0.5, 0.7, 0.9)  # samples of each side's ground in schloss.json


@dataclass
class Model:
    """Faces per material (triangulated fans of convex polygons) plus collision bodies."""

    faces: dict[str, list[list[Vec3]]] = field(default_factory=dict)
    collision: list[tuple[str, list[Vec3], list[tuple[int, int, int]]]] = field(
        default_factory=list
    )

    def poly(self, material: str, pts: list[Vec3], want: Vec3) -> None:
        """Convex planar polygon facing roughly ``want``."""
        if len(pts) < 3:
            return
        n = _normal(pts)
        if _length(n) < 1e-10:
            return
        if _dot(n, want) < 0:
            pts = pts[::-1]
        self.faces.setdefault(material, []).append(list(pts))

    def quad(self, material: str, a: Vec3, b: Vec3, c: Vec3, d: Vec3, want: Vec3) -> None:
        self.poly(material, [a, b, c, d], want)

    def box(
        self, material: str, corners: list[Vec3], height: float, want_bottom: bool = False
    ) -> None:
        """Prism over four corners (counter-clockwise or not) with top, sides, optional bottom."""
        top = [(x, y + height, z) for x, y, z in corners]
        cx = sum(p[0] for p in corners) / 4
        cz = sum(p[2] for p in corners) / 4
        for i in range(4):
            a, b = corners[i], corners[(i + 1) % 4]
            mid = ((a[0] + b[0]) / 2 - cx, 0.0, (a[2] + b[2]) / 2 - cz)
            self.quad(material, a, b, top[(i + 1) % 4], top[i], mid)
        self.poly(material, top, (0.0, 1.0, 0.0))
        if want_bottom:
            self.poly(material, list(corners), (0.0, -1.0, 0.0))

    def body(self, name: str, bottom: list[Vec3], top: list[Vec3]) -> None:
        """Closed convex prism (same number of points, same order) for collision."""
        n = len(bottom)
        ring = [(p[0], p[2]) for p in bottom]
        if _shoelace(ring) < 0:
            bottom, top = bottom[::-1], top[::-1]
        tris = [(0, k, k + 1) for k in range(1, n - 1)]
        for i in range(n):
            j = (i + 1) % n
            tris += [(i, n + i, n + j), (i, n + j, j)]
        tris += [(n, n + k + 1, n + k) for k in range(1, n - 1)]
        self.collision.append((name, list(bottom) + list(top), tris))

    def triangles(self) -> int:
        return sum(len(f) - 2 for fs in self.faces.values() for f in fs)


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _length(a: Vec3) -> float:
    return math.sqrt(_dot(a, a))


def _normal(pts: list[Vec3]) -> Vec3:
    n = (0.0, 0.0, 0.0)
    for i in range(1, len(pts) - 1):
        c = _cross(_sub(pts[i], pts[0]), _sub(pts[i + 1], pts[0]))
        n = (n[0] + c[0], n[1] + c[1], n[2] + c[2])
    return n


def _shoelace(pts: list[tuple[float, float]]) -> float:
    return sum(pts[i - 1][0] * pts[i][1] - pts[i][0] * pts[i - 1][1] for i in range(len(pts)))


# --- wing frame ------------------------------------------------------------------------------


class Wing:
    """A rectangular wing between the gable-end midpoints a and b."""

    def __init__(self, spec: dict) -> None:
        self.key = spec["key"]
        ax, az = spec["a"]
        bx, bz = spec["b"]
        self.length = math.hypot(bx - ax, bz - az)
        self.u = ((bx - ax) / self.length, (bz - az) / self.length)
        self.n = (
            self.u[1],
            -self.u[0],
        )  # "court" side: right of a -> b (north-east for the main wing)
        self.a = (ax, az)
        self.width = float(spec["width"])
        self.ground = float(spec["ground"])
        self.eave = float(spec["eave"])
        self.ridge = float(spec["ridge"])
        self.base = self.ground - SINK_M
        self.gables = set(spec.get("gables", []))
        self.storeys = int(spec.get("storeys", 2))
        self.dormer_rows = int(spec.get("dormerRows", 0))
        self.skip: dict[int, list[tuple[float, float]]] = {1: [], -1: []}  # no windows here
        self.doors: dict[int, list[tuple[float, float, float]]] = {1: [], -1: []}  # u0, u1, top
        terr = spec.get("terrain") or {}
        self._terrain = {1: terr.get("court"), -1: terr.get("garden")}

    def terrain(self, side: int, s: float) -> float:
        """Ground in front of a long side at s (linear between the samples of schloss.json)."""
        vals = self._terrain.get(side)
        if not vals:
            return self.ground
        xs = [self.length * f for f in TERRAIN_F]
        if s <= xs[0]:
            return float(vals[0])
        for xa, va, xb, vb in zip(xs, vals, xs[1:], vals[1:], strict=False):
            if s <= xb:
                return float(va + (vb - va) * (s - xa) / (xb - xa))
        return float(vals[-1])

    def end_ground(self, s: float) -> float:
        return max(self.terrain(1, s), self.terrain(-1, s))

    def p(self, s: float, t: float, y: float) -> Vec3:
        """Point at s along the wing (0 at a), t across (+ towards the court side), height y."""
        x = self.a[0] + self.u[0] * s + self.n[0] * t
        z = self.a[1] + self.u[1] * s + self.n[1] * t
        return (x, y, z)

    def out(self, side: int) -> Vec3:
        return (self.n[0] * side, 0.0, self.n[1] * side)

    def along(self, sign: float) -> Vec3:
        return (self.u[0] * sign, 0.0, self.u[1] * sign)

    @property
    def half(self) -> float:
        return self.width / 2

    def roof_y(self, t: float) -> float:
        """Underside of the roof at t across (ridge in the middle)."""
        return self.ridge - (self.ridge - self.eave) * abs(t) / self.half


# --- facades ---------------------------------------------------------------------------------


def _window_us(length: float, margin: float, skip: list[tuple[float, float]]) -> list[float]:
    n = max(0, int((length - 2 * margin) // WINDOW_SPACING) + 1)
    if n <= 0:
        return []
    span = (n - 1) * WINDOW_SPACING
    us = [length / 2 - span / 2 + i * WINDOW_SPACING for i in range(n)]
    return [u for u in us if all(not (lo - WINDOW[0] < u < hi + WINDOW[0]) for lo, hi in skip)]


def _long_facade(m: Model, w: Wing, side: int) -> None:
    """Socle on the real ground, plaster wall in vertical strips around window columns, window
    rows from the ground of this side (a window only where it stays above the socle), string
    courses and a cornice."""
    t = side * w.half
    want = w.out(side)
    top = w.eave
    length = w.length
    xs = [0.0] + [length * f for f in TERRAIN_F] + [length]

    def soc(s: float) -> float:
        return w.terrain(side, s) + SOCLE_M

    doors = w.doors[side]

    def door_at(a: float, b: float) -> tuple[float, float, float] | None:
        return next((d for d in doors if d[0] - 1e-6 <= a and b <= d[1] + 1e-6), None)

    cuts = sorted({*xs, *(d[0] for d in doors), *(d[1] for d in doors)})
    for a, b in zip(cuts, cuts[1:], strict=False):
        top_a, top_b = soc(a), soc(b)
        if door_at(a, b) is not None:  # the doorway starts on the ground
            top_a, top_b = w.terrain(side, a), w.terrain(side, b)
        m.quad(
            "stone", w.p(a, t, w.base), w.p(b, t, w.base), w.p(b, t, top_b), w.p(a, t, top_a), want
        )
    g_ref = sum(w.terrain(side, length * f) for f in TERRAIN_F) / len(TERRAIN_F)
    n = max(1, round((top - g_ref - 0.5) / 3.6))
    h = (top - g_ref) / n
    rows = []
    for k in range(n):
        y0 = g_ref + k * h + (h - WINDOW[1]) / 2 + 0.2
        if k == 0:
            y0 = max(y0, g_ref + SOCLE_M + 0.3)
        if y0 + WINDOW[1] <= top - 0.4:
            rows.append((y0, y0 + WINDOW[1]))
    us = _window_us(length, 2.0, w.skip[side])
    half_w = WINDOW[0] / 2
    win = {u: [r for r in rows if r[0] >= max(soc(u - half_w), soc(u + half_w)) + 0.2] for u in us}
    edges = sorted({*cuts, *(u - half_w for u in us), *(u + half_w for u in us)})
    for e0, e1 in zip(edges, edges[1:], strict=False):
        if e1 - e0 < 1e-6:
            continue
        col = next((u for u in us if u - half_w - 1e-6 <= e0 and e1 <= u + half_w + 1e-6), None)
        ya, yb = soc(e0), soc(e1)
        door = door_at(e0, e1)
        if door is not None:
            ya = yb = door[2]
        for y0, y1 in win.get(col, []) if col is not None else []:
            m.quad(
                "plaster_white",
                w.p(e0, t, ya),
                w.p(e1, t, yb),
                w.p(e1, t, y0),
                w.p(e0, t, y0),
                want,
            )
            ya = yb = y1
        m.quad(
            "plaster_white", w.p(e0, t, ya), w.p(e1, t, yb), w.p(e1, t, top), w.p(e0, t, top), want
        )
    for u, rs in win.items():
        for y0, y1 in rs:
            _window(m, w, side, u, y0, y1)
    # String courses between the storeys and a cornice under the eave (Renaissance articulation).
    highest = max(soc(x) for x in xs)
    for k in range(1, n + 1):
        y = g_ref + k * h - (0.3 if k == n else 0.0)
        if y <= highest + 0.5:
            continue
        hb, d = (0.3, 0.14) if k == n else (0.16, 0.08)
        tb = t + side * d
        m.quad(
            "stone",
            w.p(0, tb, y - hb),
            w.p(length, tb, y - hb),
            w.p(length, tb, y),
            w.p(0, tb, y),
            want,
        )
        m.quad(
            "stone",
            w.p(0, t, y),
            w.p(length, t, y),
            w.p(length, tb, y),
            w.p(0, tb, y),
            (0.0, 1.0, 0.0),
        )
        m.quad(
            "stone",
            w.p(0, t, y - hb),
            w.p(length, t, y - hb),
            w.p(length, tb, y - hb),
            w.p(0, tb, y - hb),
            (0.0, -1.0, 0.0),
        )


def _window(m: Model, w: Wing, side: int, u: float, y0: float, y1: float) -> None:
    t = side * w.half
    want = w.out(side)
    u0, u1 = u - WINDOW[0] / 2, u + WINDOW[0] / 2
    tin = t - side * REVEAL
    m.quad("frame", w.p(u0, tin, y0), w.p(u1, tin, y0), w.p(u1, tin, y1), w.p(u0, tin, y1), want)
    m.quad(
        "plaster_white",
        w.p(u0, t, y1),
        w.p(u1, t, y1),
        w.p(u1, tin, y1),
        w.p(u0, tin, y1),
        (0.0, -1.0, 0.0),
    )
    m.quad(
        "stone", w.p(u0, t, y0), w.p(u1, t, y0), w.p(u1, tin, y0), w.p(u0, tin, y0), (0.0, 1.0, 0.0)
    )
    m.quad(
        "plaster_white",
        w.p(u0, t, y0),
        w.p(u0, t, y1),
        w.p(u0, tin, y1),
        w.p(u0, tin, y0),
        w.along(1.0),
    )
    m.quad(
        "plaster_white",
        w.p(u1, t, y0),
        w.p(u1, t, y1),
        w.p(u1, tin, y1),
        w.p(u1, tin, y0),
        w.along(-1.0),
    )
    # Stone surround: four flat bars standing a little proud of the wall (front faces only).
    s, d = SURROUND, t + side * SURROUND_DEPTH
    bars = [
        (u0 - s, u1 + s, y1, y1 + s),
        (u0 - s, u1 + s, y0 - s, y0),
        (u0 - s, u0, y0, y1),
        (u1, u1 + s, y0, y1),
    ]
    for a0, a1, b0, b1 in bars:
        m.quad("stone", w.p(a0, d, b0), w.p(a1, d, b0), w.p(a1, d, b1), w.p(a0, d, b1), want)
    # Sill and lintel edges of the surround (visible from above/below).
    m.quad(
        "stone",
        w.p(u0 - s, t, y1 + s),
        w.p(u1 + s, t, y1 + s),
        w.p(u1 + s, d, y1 + s),
        w.p(u0 - s, d, y1 + s),
        (0.0, 1.0, 0.0),
    )
    m.quad(
        "stone",
        w.p(u0 - s, t, y0 - s),
        w.p(u1 + s, t, y0 - s),
        w.p(u1 + s, d, y0 - s),
        w.p(u0 - s, d, y0 - s),
        (0.0, -1.0, 0.0),
    )


def _gable_end(m: Model, w: Wing, end: str) -> None:
    """End wall: socle, plaster up to the eave with a window column, then the gable."""
    s = 0.0 if end == "a" else w.length
    sign = -1.0 if end == "a" else 1.0
    want = w.along(sign)
    h = w.half
    socle = w.end_ground(s) + SOCLE_M
    m.quad(
        "stone", w.p(s, -h, w.base), w.p(s, h, w.base), w.p(s, h, socle), w.p(s, -h, socle), want
    )
    m.quad(
        "plaster_white",
        w.p(s, -h, socle),
        w.p(s, h, socle),
        w.p(s, h, w.eave),
        w.p(s, -h, w.eave),
        want,
    )
    if end in w.gables:
        _scroll_gable(m, w, s, sign)
    else:
        m.poly("plaster_white", [w.p(s, -h, w.eave), w.p(s, h, w.eave), w.p(s, 0.0, w.ridge)], want)


def _scroll_gable(m: Model, w: Wing, s: float, sign: float) -> None:
    """Renaissance scroll gable (own interpretation): stepped storeys with quarter-circle volutes,
    a stone coping and small obelisks, a slab that rises a little above the roof line."""
    want = w.along(sign)
    back = w.along(-sign)
    h = w.half
    rise = w.ridge - w.eave
    steps = 4
    s_back = s - sign * GABLE_T
    level = [w.eave + rise * k / steps for k in range(steps + 1)]
    for k in range(steps):
        y0, y1 = level[k], level[k + 1]
        half_w = h * (1 - k / steps) * 0.98  # step width follows the roof slope
        inner = h * (1 - (k + 1) / steps) * 0.98
        top = y1 + 0.35
        # Step block (front, back, top, sides).
        for t0, t1 in ((-half_w, half_w),):
            m.quad(
                "plaster_white",
                w.p(s, t0, y0),
                w.p(s, t1, y0),
                w.p(s, t1, top),
                w.p(s, t0, top),
                want,
            )
            m.quad(
                "plaster_white",
                w.p(s_back, t0, y0),
                w.p(s_back, t1, y0),
                w.p(s_back, t1, top),
                w.p(s_back, t0, top),
                back,
            )
            m.quad(
                "stone",
                w.p(s, t0, top),
                w.p(s, t1, top),
                w.p(s_back, t1, top),
                w.p(s_back, t0, top),
                (0.0, 1.0, 0.0),
            )
        for side in (-1, 1):
            t0 = side * half_w
            m.quad(
                "plaster_white",
                w.p(s, t0, y0),
                w.p(s_back, t0, y0),
                w.p(s_back, t0, top),
                w.p(s, t0, top),
                w.out(side),
            )
            # Volute: quarter circle leaning against the next step (convex fan in the front plane).
            if k < steps - 1:
                r = min(half_w - inner, (level[k + 1] - level[k]) * 0.9)
                cx = side * inner
                pts_front = [w.p(s, cx, top)]
                for i in range(5):
                    a = math.pi / 2 * i / 4
                    pts_front.append(w.p(s, cx + side * r * math.cos(a), top + r * math.sin(a)))
                m.poly("plaster_white", pts_front, want)
                pts_back = [
                    (x - w.u[0] * sign * GABLE_T, y, z - w.u[1] * sign * GABLE_T)
                    for x, y, z in pts_front
                ]
                m.poly("plaster_white", pts_back, back)
                for i in range(1, 5):
                    a0, a1 = pts_front[i], pts_front[i + 1] if i < 5 else pts_front[i]
                    if i == 4:
                        break
                    b0, b1 = pts_back[i], pts_back[i + 1]
                    out = _sub(a0, w.p(s, cx, top))
                    m.quad("stone", a0, a1, b1, b0, (out[0], out[1], out[2]))
            # Obelisk on the outer corner of each step.
            c = w.p(s - sign * GABLE_T / 2, t0, top)
            r = 0.18
            base4 = [
                (c[0] + dx, c[1], c[2] + dz)
                for dx, dz in (
                    (r * w.u[0] + r * w.n[0] * side, r * w.u[1] + r * w.n[1] * side),
                    (r * w.u[0] - r * w.n[0] * side, r * w.u[1] - r * w.n[1] * side),
                    (-r * w.u[0] - r * w.n[0] * side, -r * w.u[1] - r * w.n[1] * side),
                    (-r * w.u[0] + r * w.n[0] * side, -r * w.u[1] + r * w.n[1] * side),
                )
            ]
            apex = (c[0], c[1] + 1.1, c[2])
            for i in range(4):
                a, b = base4[i], base4[(i + 1) % 4]
                mid = ((a[0] + b[0]) / 2 - c[0], 0.3, (a[2] + b[2]) / 2 - c[2])
                m.poly("stone", [a, b, apex], mid)
    # Crowning: a small pediment on the top step.
    y = level[-1] + 0.35
    tw = h * 0.98 / steps
    m.poly("stone", [w.p(s, -tw, y), w.p(s, tw, y), w.p(s, 0.0, y + 1.2)], want)
    m.poly("stone", [w.p(s_back, -tw, y), w.p(s_back, tw, y), w.p(s_back, 0.0, y + 1.2)], back)
    for side in (-1, 1):
        m.quad(
            "stone",
            w.p(s, side * tw, y),
            w.p(s, 0.0, y + 1.2),
            w.p(s_back, 0.0, y + 1.2),
            w.p(s_back, side * tw, y),
            (w.n[0] * side, 0.6, w.n[1] * side),
        )
    # Window column in the gable (two small windows).
    for k in range(2):
        y0 = w.eave + 0.8 + k * rise / 2.4
        _gable_window(m, w, s, sign, y0)


def _gable_window(m: Model, w: Wing, s: float, sign: float, y0: float) -> None:
    ww, wh = 1.0, 1.4
    want = w.along(sign)
    s_in = s - sign * REVEAL
    t0, t1, y1 = -ww / 2, ww / 2, y0 + wh
    m.quad(
        "frame", w.p(s_in, t0, y0), w.p(s_in, t1, y0), w.p(s_in, t1, y1), w.p(s_in, t0, y1), want
    )
    d = s + sign * SURROUND_DEPTH
    sw = SURROUND
    for a0, a1, b0, b1 in (
        (t0 - sw, t1 + sw, y1, y1 + sw),
        (t0 - sw, t1 + sw, y0 - sw, y0),
        (t0 - sw, t0, y0, y1),
        (t1, t1 + sw, y0, y1),
    ):
        m.quad("stone", w.p(d, a0, b0), w.p(d, a1, b0), w.p(d, a1, b1), w.p(d, a0, b1), want)


def _quoins(m: Model, w: Wing) -> None:
    """Corner stones: alternating long and short blocks, a little proud of both faces."""
    for s, su in ((0.0, 1.0), (w.length, -1.0)):
        for side in (-1, 1):
            t = side * w.half
            y = max(w.terrain(side, s), w.end_ground(s)) + SOCLE_M
            k = 0
            while y + 0.45 <= w.eave - 0.2:
                long_u, long_t = (0.9, 0.45) if k % 2 == 0 else (0.45, 0.9)
                d = 0.05
                c = [
                    w.p(s - su * d, t + side * d, y),
                    w.p(s + su * long_u, t + side * d, y),
                    w.p(s + su * long_u, t - side * long_t, y),
                    w.p(s - su * d, t - side * long_t, y),
                ]
                m.box("stone", c, 0.42)
                y += 0.5
                k += 1


# --- roof ------------------------------------------------------------------------------------


def _roof(m: Model, w: Wing) -> None:
    """Saddle roof with eave overhang; no overhang at scroll-gable ends (they rise above it)."""
    h = w.half + OVERHANG
    s0 = 0.0 if "a" in w.gables else -OVERHANG
    s1 = w.length if "b" in w.gables else w.length + OVERHANG
    drop = (w.ridge - w.eave) / w.half * OVERHANG
    y_eave = w.eave - drop
    for side in (-1, 1):
        a, b = w.p(s0, side * h, y_eave + ROOF_T), w.p(s1, side * h, y_eave + ROOF_T)
        c, d = w.p(s1, 0.0, w.ridge + ROOF_T), w.p(s0, 0.0, w.ridge + ROOF_T)
        n = _normal([a, b, c, d])
        if n[1] < 0:
            n = (-n[0], -n[1], -n[2])
        mat = "roof_old"  # a well-kept roof: no moss on the castle (koordinator, 2026-10-03)
        m.quad(mat, a, b, c, d, (0.0, 1.0, 0.0))
        # Underside of the overhang and fascia.
        m.quad(
            "roof_old",
            w.p(s0, side * h, y_eave),
            w.p(s1, side * h, y_eave),
            w.p(s1, side * w.half, w.eave),
            w.p(s0, side * w.half, w.eave),
            (0.0, -1.0, 0.0),
        )
        m.quad(
            "roof_old",
            w.p(s0, side * h, y_eave),
            w.p(s1, side * h, y_eave),
            w.p(s1, side * h, y_eave + ROOF_T),
            w.p(s0, side * h, y_eave + ROOF_T),
            w.out(side),
        )
    for s, sign, key in ((s0, -1.0, "a"), (s1, 1.0, "b")):
        if key in w.gables:
            continue
        # Verge board along the gable overhang.
        for side in (-1, 1):
            m.quad(
                "roof_old",
                w.p(s, side * h, y_eave),
                w.p(s, 0.0, w.ridge),
                w.p(s, 0.0, w.ridge + ROOF_T),
                w.p(s, side * h, y_eave + ROOF_T),
                w.along(sign),
            )


def _dormers(m: Model, w: Wing, tower_s: float | None) -> None:
    """Rows of small gable dormers on both roof sides, the upper rows sparser."""
    rise = w.ridge - w.eave
    for row in range(w.dormer_rows):
        f = 0.22 + 0.5 * row / max(1, w.dormer_rows)  # depth from the eave, as a fraction of half
        t_front = w.half * (1 - f)
        y_foot = w.roof_y(t_front) + ROOF_T
        spacing = WINDOW_SPACING * (2 + row)
        n = max(1, int((w.length - 6.0) // spacing))
        span = (n - 1) * spacing
        for side in (-1, 1):
            for i in range(n):
                s = w.length / 2 - span / 2 + i * spacing
                if tower_s is not None and abs(s - tower_s) < 4.0 and side == -1:
                    continue
                _dormer(m, w, side, s, t_front, y_foot, 1.1 if row else 1.4, rise)


def _dormer(
    m: Model, w: Wing, side: int, s: float, t_front: float, y_foot: float, width: float, rise: float
) -> None:
    hw = width / 2
    front_h = 1.1 if width < 1.3 else 1.4
    yt = y_foot + front_h
    apex = yt + hw * 1.1
    slope = rise / w.half  # main roof rises 'slope' per metre towards the ridge
    depth = (apex + ROOF_T - y_foot) / slope + 0.2
    t_back = t_front - depth
    tf, tb = side * t_front, side * t_back
    want = w.out(side)
    under = y_foot - 0.3
    win_w, win_h = width * 0.5, front_h * 0.6
    y0 = y_foot + 0.25
    y1 = y0 + win_h
    a0, a1 = s - win_w / 2, s + win_w / 2
    # Front wall around the window: below, left, right, and the gable above (all convex).
    m.quad(
        "plaster_white",
        w.p(s - hw, tf, under),
        w.p(s + hw, tf, under),
        w.p(s + hw, tf, y0),
        w.p(s - hw, tf, y0),
        want,
    )
    m.quad(
        "plaster_white",
        w.p(s - hw, tf, y0),
        w.p(a0, tf, y0),
        w.p(a0, tf, y1),
        w.p(s - hw, tf, y1),
        want,
    )
    m.quad(
        "plaster_white",
        w.p(a1, tf, y0),
        w.p(s + hw, tf, y0),
        w.p(s + hw, tf, y1),
        w.p(a1, tf, y1),
        want,
    )
    m.poly(
        "plaster_white",
        [
            w.p(s - hw, tf, y1),
            w.p(s + hw, tf, y1),
            w.p(s + hw, tf, yt),
            w.p(s, tf, apex),
            w.p(s - hw, tf, yt),
        ],
        want,
    )
    tin = tf - side * 0.12
    m.quad("frame", w.p(a0, tin, y0), w.p(a1, tin, y0), w.p(a1, tin, y1), w.p(a0, tin, y1), want)
    for (pa, pb), wn in (
        ((w.p(a0, tf, y1), w.p(a1, tf, y1)), (0.0, -1.0, 0.0)),
        ((w.p(a0, tf, y0), w.p(a1, tf, y0)), (0.0, 1.0, 0.0)),
        ((w.p(a0, tf, y0), w.p(a0, tf, y1)), w.along(1.0)),
        ((w.p(a1, tf, y0), w.p(a1, tf, y1)), w.along(-1.0)),
    ):
        back_a = (pa[0] - w.n[0] * side * 0.12, pa[1], pa[2] - w.n[1] * side * 0.12)
        back_b = (pb[0] - w.n[0] * side * 0.12, pb[1], pb[2] - w.n[1] * side * 0.12)
        m.quad("plaster_white", pa, pb, back_b, back_a, wn)
    for sgn in (-1, 1):  # cheeks and roof planes
        x = s + sgn * hw
        m.quad(
            "plaster_white",
            w.p(x, tf, under),
            w.p(x, tf, yt),
            w.p(x, tb, yt),
            w.p(x, tb, under),
            w.along(sgn),
        )
        m.quad(
            "roof_old",
            w.p(x + sgn * 0.12, tf + side * 0.15, yt - 0.13),
            w.p(s, tf + side * 0.15, apex),
            w.p(s, tb, apex),
            w.p(x + sgn * 0.12, tb, yt - 0.13),
            (w.u[0] * sgn, 0.8, w.u[1] * sgn),
        )


# --- tower, oriel, portal, chimneys ----------------------------------------------------------


def _ring(c: tuple[float, float], r: float, y: float, n: int = 8, phase: float = 0.0) -> list[Vec3]:
    return [
        (
            c[0] + r * math.cos(phase + 2 * math.pi * i / n),
            y,
            c[1] + r * math.sin(phase + 2 * math.pi * i / n),
        )
        for i in range(n)
    ]


def _tower(m: Model, spec: dict, w: Wing, base: float, ground: float) -> None:
    """Octagonal stair tower with slit windows, bell roof (welsche Haube), lantern and spire."""
    c = (float(spec["at"][0]), float(spec["at"][1]))
    r = float(spec["radius"])
    top = w.eave + float(spec["aboveEave"])
    lo, hi = _ring(c, r, base), _ring(c, r, top)
    for i in range(8):
        j = (i + 1) % 8
        mid = ((lo[i][0] + lo[j][0]) / 2 - c[0], 0.0, (lo[i][2] + lo[j][2]) / 2 - c[1])
        y_s = ground + SOCLE_M
        a0, b0 = lo[i], lo[j]
        a1, b1 = (a0[0], y_s, a0[2]), (b0[0], y_s, b0[2])
        m.quad("stone", a0, b0, b1, a1, mid)
        m.quad("plaster_white", a1, b1, hi[j], hi[i], mid)
        if i % 2 == 0:  # slit windows climbing with the stair
            for k in range(3):
                y0 = w.ground + 2.5 + k * (top - w.ground - 4.0) / 3 + i * 0.4
                fx = [
                    (a0[0] * (1 - f) + b0[0] * f, a0[2] * (1 - f) + b0[2] * f) for f in (0.38, 0.62)
                ]
                out = (mid[0] * 0.03, 0.0, mid[2] * 0.03)
                pts = [
                    (fx[0][0] + out[0], y0, fx[0][1] + out[2]),
                    (fx[1][0] + out[0], y0, fx[1][1] + out[2]),
                    (fx[1][0] + out[0], y0 + 1.3, fx[1][1] + out[2]),
                    (fx[0][0] + out[0], y0 + 1.3, fx[0][1] + out[2]),
                ]
                m.poly("frame", pts, mid)
    # Cornice, bell roof, lantern, spire.
    profile = [
        (r + 0.25, 0.0),
        (r + 0.25, 0.25),
        (r * 0.95, 0.9),
        (r * 0.55, 1.9),
        (r * 0.62, 2.6),
        (r * 0.35, 3.3),
    ]
    rings = [_ring(c, rr, top + dy) for rr, dy in profile]
    m.poly("stone", rings[0][::-1], (0.0, -1.0, 0.0))
    for k in range(len(rings) - 1):
        mat = "stone" if k == 0 else "roof_old"
        for i in range(8):
            j = (i + 1) % 8
            a, b = rings[k][i], rings[k][j]
            mid = ((a[0] + b[0]) / 2 - c[0], 0.4, (a[2] + b[2]) / 2 - c[1])
            m.quad(mat, a, b, rings[k + 1][j], rings[k + 1][i], mid)
    lantern_y = top + profile[-1][1]
    lr = r * 0.35
    lan_lo, lan_hi = _ring(c, lr, lantern_y), _ring(c, lr, lantern_y + 1.2)
    for i in range(8):
        j = (i + 1) % 8
        mid = (
            (lan_lo[i][0] + lan_lo[j][0]) / 2 - c[0],
            0.0,
            (lan_lo[i][2] + lan_lo[j][2]) / 2 - c[1],
        )

        def at(f: float, lo: bool, i: int = i, j: int = j) -> Vec3:
            a, b = (lan_lo if lo else lan_hi)[i], (lan_lo if lo else lan_hi)[j]
            return (a[0] + (b[0] - a[0]) * f, a[1], a[2] + (b[2] - a[2]) * f)

        m.quad("plaster_white", at(0.0, True), at(0.25, True), at(0.25, False), at(0.0, False), mid)
        m.quad("plaster_white", at(0.75, True), at(1.0, True), at(1.0, False), at(0.75, False), mid)
        op = [at(0.25, True), at(0.75, True), at(0.75, False), at(0.25, False)]
        m.quad("frame", *[(p[0] - mid[0] * 0.08, p[1], p[2] - mid[2] * 0.08) for p in op], mid)
    apex = (c[0], lantern_y + 3.2, c[1])
    spire = _ring(c, lr + 0.1, lantern_y + 1.2)
    for i in range(8):
        j = (i + 1) % 8
        mid = ((spire[i][0] + spire[j][0]) / 2 - c[0], 0.3, (spire[i][2] + spire[j][2]) / 2 - c[1])
        m.poly("roof_old", [spire[i], spire[j], apex], mid)
    m.body("COL_HULL_tower", _ring(c, r, base), _ring(c, r, top))


def _oriel(m: Model, spec: dict, w: Wing) -> None:
    """Corner oriel on the upper storeys, on a stone corbel, with a small hipped roof."""
    side = 1 if spec.get("side", "court") == "court" else -1
    width, depth = float(spec["width"]), float(spec["depth"])
    s_c = w.length - width / 2 - 1.0 if spec.get("end", "b") == "b" else width / 2 + 1.0
    w.skip[side].append((s_c - width / 2, s_c + width / 2))
    t0, t1 = side * w.half, side * (w.half + depth)
    h_storey = (w.eave - w.ground) / w.storeys
    y0, y1 = w.ground + h_storey * 1.0 + 0.4, w.eave - 0.4
    a, b = s_c - width / 2, s_c + width / 2
    c = [w.p(a, t0, y0), w.p(b, t0, y0), w.p(b, t1, y0), w.p(a, t1, y0)]
    m.poly(
        "stone",
        [w.p(a, t0, y0 - 1.0), w.p(b, t0, y0 - 1.0), w.p(b, t1, y0), w.p(a, t1, y0)],
        (w.n[0] * side, -1.0, w.n[1] * side),
    )
    m.poly("stone", [w.p(a, t0, y0 - 1.0), w.p(a, t1, y0), w.p(a, t0, y0)], w.along(-1))
    m.poly("stone", [w.p(b, t0, y0 - 1.0), w.p(b, t0, y0), w.p(b, t1, y0)], w.along(1))
    del c
    for (pa, pb), want in (
        ((w.p(a, t1, y0), w.p(b, t1, y0)), w.out(side)),
        ((w.p(a, t0, y0), w.p(a, t1, y0)), w.along(-1)),
        ((w.p(b, t1, y0), w.p(b, t0, y0)), w.along(1)),
    ):
        m.quad("plaster_white", pa, pb, (pb[0], y1, pb[2]), (pa[0], y1, pa[2]), want)
    for k in range(2):  # windows on the front
        wy0 = y0 + 0.9 + k * (y1 - y0) / 2
        for f in (0.3, 0.7):
            s = a + width * f
            d = t1 + side * 0.03
            m.quad(
                "frame",
                w.p(s - 0.35, d, wy0),
                w.p(s + 0.35, d, wy0),
                w.p(s + 0.35, d, wy0 + 1.2),
                w.p(s - 0.35, d, wy0 + 1.2),
                w.out(side),
            )
    apex = w.p(s_c, t1 - side * depth * 0.2, y1 + 1.6)
    corners = [
        w.p(a - 0.15, t0, y1),
        w.p(a - 0.15, t1 + side * 0.15, y1),
        w.p(b + 0.15, t1 + side * 0.15, y1),
        w.p(b + 0.15, t0, y1),
    ]
    for i in range(3):
        p0, p1 = corners[i], corners[i + 1]
        mid = ((p0[0] + p1[0]) / 2 - apex[0], 0.5, (p0[2] + p1[2]) / 2 - apex[2])
        m.poly("roof_old", [p0, p1, apex], mid)
    m.poly("roof_old", corners[::-1], (0.0, -1.0, 0.0))


def _portal(m: Model, spec: dict, w: Wing) -> None:
    side = 1 if spec.get("side", "court") == "court" else -1
    s = w.length * float(spec["t"])
    pw, ph = float(spec["w"]), float(spec["h"])
    w.skip[side].append((s - pw / 2 - 0.6, s + pw / 2 + 0.6))
    t = side * w.half
    want = w.out(side)
    tin = t - side * 0.5
    y0 = w.terrain(side, s)
    w.doors[side].append((s - pw / 2, s + pw / 2, y0 + ph))
    m.quad(
        "timber_dark",
        w.p(s - pw / 2, tin, y0),
        w.p(s + pw / 2, tin, y0),
        w.p(s + pw / 2, tin, y0 + ph),
        w.p(s - pw / 2, tin, y0 + ph),
        want,
    )
    for sgn in (-1, 1):
        x = s + sgn * pw / 2
        m.quad(
            "stone",
            w.p(x, t, y0),
            w.p(x, t, y0 + ph),
            w.p(x, tin, y0 + ph),
            w.p(x, tin, y0),
            w.along(-sgn),
        )
    m.quad(
        "stone",
        w.p(s - pw / 2, t, y0 + ph),
        w.p(s + pw / 2, t, y0 + ph),
        w.p(s + pw / 2, tin, y0 + ph),
        w.p(s - pw / 2, tin, y0 + ph),
        (0.0, -1.0, 0.0),
    )
    d = t + side * 0.12
    fr = 0.45
    for a0, a1, b0, b1 in (
        (s - pw / 2 - fr, s - pw / 2, y0, y0 + ph),
        (s + pw / 2, s + pw / 2 + fr, y0, y0 + ph),
        (s - pw / 2 - fr, s + pw / 2 + fr, y0 + ph, y0 + ph + fr),
    ):
        m.quad("stone", w.p(a0, d, b0), w.p(a1, d, b0), w.p(a1, d, b1), w.p(a0, d, b1), want)
    # Pediment over the portal.
    m.poly(
        "stone",
        [
            w.p(s - pw / 2 - fr, d, y0 + ph + fr),
            w.p(s + pw / 2 + fr, d, y0 + ph + fr),
            w.p(s, d, y0 + ph + fr + 1.0),
        ],
        want,
    )


def _chimney(m: Model, w: Wing, t: float) -> None:
    s = w.length * t
    off = 0.8
    y0 = w.roof_y(off + 0.5) - 0.2
    y1 = w.ridge + ROOF_T + 1.3
    corners = [
        w.p(s - 0.4, off - 0.4, y0),
        w.p(s + 0.4, off - 0.4, y0),
        w.p(s + 0.4, off + 0.4, y0),
        w.p(s - 0.4, off + 0.4, y0),
    ]
    m.box("brick", corners, y1 - y0)
    cap = [
        w.p(s - 0.5, off - 0.5, y1),
        w.p(s + 0.5, off - 0.5, y1),
        w.p(s + 0.5, off + 0.5, y1),
        w.p(s - 0.5, off + 0.5, y1),
    ]
    m.box("stone", cap, 0.1, want_bottom=True)


# --- whole model -----------------------------------------------------------------------------


class GardenFrame:
    """The parterre's own frame: wing coordinates (s, t) turned by ``rotDeg`` about the garden
    centre (the real garden is not quite parallel to the main wing)."""

    def __init__(self, wing: Wing, centre: tuple[float, float], rot_deg: float) -> None:
        self.wing = wing
        self.centre = centre
        self.c, self.s = math.cos(math.radians(rot_deg)), math.sin(math.radians(rot_deg))

    def st(self, s: float, t: float) -> tuple[float, float]:
        ds, dt = s - self.centre[0], t - self.centre[1]
        return (
            self.centre[0] + ds * self.c - dt * self.s,
            self.centre[1] + ds * self.s + dt * self.c,
        )

    def p(self, s: float, t: float, y: float) -> Vec3:
        return self.wing.p(*self.st(s, t), y)

    def ground(self, g: dict, s: float, t: float) -> float:
        return garden_ground(g, *self.st(s, t))


def _plaza_cut(
    bed: list[tuple[float, float]], centre: tuple[float, float], r: float
) -> list[tuple[float, float]]:
    """``bed`` (counter-clockwise in (s, t)) with the corner nearest ``centre`` cut off along a
    45-degree line at distance ``r`` from it (a round plaza between four beds)."""
    k = max(range(len(bed)), key=lambda i: -((bed[i][0] - centre[0]) ** 2
                                             + (bed[i][1] - centre[1]) ** 2))  # fmt: skip
    cs, ct = bed[k]
    ds, dt = cs - centre[0], ct - centre[1]
    us, ut = (1.0 if ds >= 0 else -1.0), (1.0 if dt >= 0 else -1.0)
    reach = r * math.sqrt(2.0)  # the cut line: us*(s-cs0) + ut*(t-ct0) = reach, from the centre
    if abs(ds) + abs(dt) >= reach:
        return bed
    along_s = (centre[0] + us * (reach - abs(dt)), ct)  # on the edge with t = ct
    along_t = (cs, centre[1] + ut * (reach - abs(ds)))  # on the edge with s = cs
    prev = bed[k - 1]
    first, second = (along_s, along_t) if abs(prev[1] - ct) < 1e-9 else (along_t, along_s)
    return [*bed[:k], first, second, *bed[k + 1 :]]


def garden_layout(spec: dict) -> dict | None:
    """Parterre of the castle garden in world (x, z): beds (polygons in the wing frame (s, t),
    counter-clockwise), the garden areas and, with ``plazaR``, a round plaza in the middle of
    each half (for the garden fountains)."""
    g = spec.get("garden")
    if not g:
        return None
    w = next(Wing(ws) for ws in spec["wings"] if ws["key"] == g["wing"])
    t0, t1 = (float(v) for v in g["t"])
    nu, nv = (int(v) for v in g["beds"])
    parts = [(float(a), float(b)) for a, b in g["parts"]]
    centre = ((min(a for a, _ in parts) + max(b for _, b in parts)) / 2, (t0 + t1) / 2)
    w = GardenFrame(w, centre, float(g.get("rotDeg", 0.0)))
    path, border = float(g["pathM"]), float(g["borderM"])
    plaza = float(g.get("plazaR", 0.0))
    beds, areas, centres = [], [], []
    for s0, s1 in g["parts"]:
        s0, s1 = float(s0), float(s1)
        areas.append([w.p(s0, t0, 0), w.p(s1, t0, 0), w.p(s1, t1, 0), w.p(s0, t1, 0)])
        centre = ((s0 + s1) / 2, (t0 + t1) / 2)
        centres.append(centre)
        bu = (s1 - s0 - 2 * border - (nu - 1) * path) / nu
        bv = (t1 - t0 - 2 * border - (nv - 1) * path) / nv
        for i in range(nu):
            for k in range(nv):
                a = s0 + border + i * (bu + path)
                b = t0 + border + k * (bv + path)
                bed = [(a, b), (a + bu, b), (a + bu, b + bv), (a, b + bv)]
                beds.append(_plaza_cut(bed, centre, plaza) if plaza > 0 else bed)
    return {"wing": w, "beds": beds, "areas": areas, "centres": centres, "spec": g}


def garden_ground(g: dict, s: float, t: float) -> float:
    gr = g["ground"]
    return (
        float(gr["y"])
        + float(gr["perS"]) * (s - gr["at"][0])
        + float(gr["perT"]) * (t - gr["at"][1])
    )


def _garden(m: Model, spec: dict) -> None:
    lay = garden_layout(spec)
    if lay is None:
        return
    w, g = lay["wing"], lay["spec"]
    hh, hw = float(g["hedge"]["h"]), float(g["hedge"]["w"])
    for bed in lay["beds"]:
        # A low hedge along every edge of the bed, inside it; its foot follows the garden ground.
        n = len(bed)
        for i in range(n):
            (a0, b0), (a1, b1) = bed[i], bed[(i + 1) % n]
            length = math.hypot(a1 - a0, b1 - b0)
            if length < 1e-6:
                continue
            ns, nt = -(b1 - b0) / length * hw, (a1 - a0) / length * hw  # inward (bed is ccw)
            quad = [(a0, b0), (a1, b1), (a1 + ns, b1 + nt), (a0 + ns, b0 + nt)]
            y = min(w.ground(g, a, b) for a, b in quad) - 0.3
            top = max(w.ground(g, a, b) for a, b in quad) + hh
            m.box("hedge", [w.p(a, b, y) for a, b in quad], top - y)
    f = g.get("fountain")
    if f:
        cs, ct, r = float(f["s"]), float(f["t"]), float(f["r"])
        cx, _, cz = w.p(cs, ct, 0)
        y = garden_ground(g, cs, ct) - 0.3
        outer, inner = _ring((cx, cz), r, y), _ring((cx, cz), r - 0.35, y)
        top = garden_ground(g, cs, ct) + 0.55
        for i in range(8):
            j = (i + 1) % 8
            mid = ((outer[i][0] + outer[j][0]) / 2 - cx, 0.0, (outer[i][2] + outer[j][2]) / 2 - cz)
            m.quad(
                "stone",
                outer[i],
                outer[j],
                (outer[j][0], top, outer[j][2]),
                (outer[i][0], top, outer[i][2]),
                mid,
            )
            m.quad(
                "stone",
                inner[j],
                inner[i],
                (inner[i][0], top, inner[i][2]),
                (inner[j][0], top, inner[j][2]),
                (-mid[0], 0.0, -mid[2]),
            )
            m.quad(
                "stone",
                (outer[i][0], top, outer[i][2]),
                (outer[j][0], top, outer[j][2]),
                (inner[j][0], top, inner[j][2]),
                (inner[i][0], top, inner[i][2]),
                (0.0, 1.0, 0.0),
            )
        water = [(p[0], top - 0.2, p[2]) for p in inner]
        m.poly("water", water, (0.0, 1.0, 0.0))


def garden_splat(spec: dict) -> dict | None:
    """Splat areas for the terrain export: the garden (gravel paths) and its beds (lawn)."""
    lay = garden_layout(spec)
    if lay is None:
        return None
    w = lay["wing"]

    def xz(pts: list[Vec3]) -> list[list[float]]:
        return [[round(p[0], 2), round(p[2], 2)] for p in pts]

    beds = [xz([w.p(a, b, 0) for a, b in bed]) for bed in lay["beds"]]
    return {"gravel": [xz(a) for a in lay["areas"]], "lawn": beds}


def wing_body(m: Model, w: Wing) -> None:
    """Closed convex collision body: walls up to the eave and the roof prism (no overhang)."""
    h = w.half
    ring_b = [
        w.p(0, -h, w.base),
        w.p(w.length, -h, w.base),
        w.p(w.length, h, w.base),
        w.p(0, h, w.base),
    ]
    pts = ring_b + [
        w.p(0, -h, w.eave),
        w.p(w.length, -h, w.eave),
        w.p(w.length, h, w.eave),
        w.p(0, h, w.eave),
        w.p(0, 0, w.ridge),
        w.p(w.length, 0, w.ridge),
    ]
    if _shoelace([(p[0], p[2]) for p in ring_b]) < 0:  # make the bottom ring counter-clockwise
        pts = [pts[3], pts[2], pts[1], pts[0], pts[7], pts[6], pts[5], pts[4], pts[8], pts[9]]
    # 0-3 bottom, 4-7 eave corners (same order), 8 ridge over the 0/3 end, 9 ridge over 1/2.
    tris = [(0, 1, 2), (0, 2, 3)]
    for i in range(4):
        j = (i + 1) % 4
        tris += [(i, 4 + i, 4 + j), (i, 4 + j, j)]
    # Roof: two planes and two gable triangles.
    tris += [(4, 8, 9), (4, 9, 5), (6, 9, 8), (6, 8, 7), (5, 9, 6), (7, 8, 4)]
    m.collision.append((f"COL_HULL_{w.key}", pts, tris))


def build(spec: dict) -> Model:
    m = Model()
    wings = [Wing(s) for s in spec["wings"]]
    by_key = {w.key: w for w in wings}
    tower = spec.get("stairTower")
    tower_wing = tower_s = None
    if tower:
        # The tower stands against the main wing: no windows behind it, no dormer in front of it.
        tx, tz = tower["at"]
        best = None
        for w in wings:
            dx, dz = tx - w.a[0], tz - w.a[1]
            s = dx * w.u[0] + dz * w.u[1]
            t = dx * w.n[0] + dz * w.n[1]
            if -1 <= s <= w.length + 1:
                d = abs(abs(t) - w.half)
                if best is None or d < best[0]:
                    best = (d, w, s, 1 if t > 0 else -1)
        if best is not None:
            _, tower_wing, tower_s, side = best
            r = float(tower["radius"])
            tower_wing.skip[side].append((tower_s - r - 0.3, tower_s + r + 0.3))
    if spec.get("oriel"):
        _oriel(m, spec["oriel"], by_key[spec["oriel"]["wing"]])
    if spec.get("portal"):
        _portal(m, spec["portal"], by_key[spec["portal"]["wing"]])
    for w in wings:
        for side in (-1, 1):
            _long_facade(m, w, side)
        for end in ("a", "b"):
            _gable_end(m, w, end)
        _quoins(m, w)
        _roof(m, w)
        _dormers(m, w, tower_s if w is tower_wing else None)
        wing_body(m, w)
    if tower and tower_wing is not None:
        tside = 1 if spec.get("stairTowerSide", "garden") == "court" else -1
        ground = tower_wing.terrain(tside, tower_s) if tower_s is not None else tower_wing.ground
        _tower(m, tower, tower_wing, min(w.base for w in wings), ground)
    for c in spec.get("chimneys", []):
        _chimney(m, by_key[c["wing"]], float(c["t"]))
    _garden(m, spec)
    return m


def to_local(m: Model, origin: Vec3) -> Model:
    """Positions relative to the model origin (the vob position)."""
    ox, oy, oz = origin
    out = Model()
    for mat, fs in m.faces.items():
        out.faces[mat] = [[(x - ox, y - oy, z - oz) for x, y, z in f] for f in fs]
    out.collision = [
        (n, [(x - ox, y - oy, z - oz) for x, y, z in pts], tris) for n, pts, tris in m.collision
    ]
    return out


def origin_of(spec: dict) -> Vec3:
    """Vob position: the given (x, z) and the lowest foot of the model (wings and garden)."""
    ox, oz = spec["origin"]
    lowest = min(float(w["ground"]) for w in spec["wings"]) - SINK_M
    lay = garden_layout(spec)
    if lay:
        g, frame = lay["spec"], lay["wing"]
        corners = [(s, t) for part in g["parts"] for s in part for t in g["t"]]
        lowest = min(lowest, min(frame.ground(g, s, t) for s, t in corners) - 0.3)
    return (float(ox), round(lowest, 3), float(oz))
