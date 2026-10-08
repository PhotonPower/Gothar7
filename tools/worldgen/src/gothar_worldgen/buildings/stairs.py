"""Stairs to the upper storey of an enterable house (W7): where they go, their steps and rails,
the opening in the ceiling above and the ramp they collide as.

A straight flight along a wall of the room with the door, at most 35° steep (the engine's ground
probe allows about 38.7°; engine 2026-10-08): risers ``riseM`` at most, treads ``runM``. It
collides as one smooth wedge (no edges to catch on) from one tread before the first step up to
the upper floor, so the slope is exactly rise/run. Above the upper part of the flight the ceiling
is open far enough for ``headroomM`` over the ramp; a rail guards the opening upstairs on the
three sides that are not the way up.

The headroom counts the physics cylinder (1.8 m high, 0.3 m radius, Character.hpp): its front edge
reaches under the ceiling's edge before its middle does, so ``headroomM`` is 2.4 m, not 2.0.

Coordinates: world (x, z) in the plan, y up.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Any

import numpy as np
from shapely.geometry import LineString, Point, Polygon

from gothar_worldgen.buildings.gltf import CollisionPart

Pt = tuple[float, float]
Vec3 = tuple[float, float, float]

DEFAULTS = {"riseM": 0.18, "runM": 0.26, "widthM": 0.95, "headroomM": 2.4, "landingM": 0.8,
            "wallGapM": 0.02, "doorClearM": 1.3, "passageClearM": 1.2, "windowClearM": 0.15,
            "railM": 1.0, "stepM": 0.25, "upperMinHeightM": 2.1, "beamsFromM": 2.3}  # fmt: skip


@dataclass(frozen=True)
class Stair:
    foot: Pt  # wall-side corner at the bottom of the first step
    up: Pt  # unit vector the flight rises along
    side: Pt  # unit vector from the wall into the room
    width: float
    steps: int  # risers; the top one ends on the upper floor
    rise: float
    tread: float
    floor: float  # y of the lower floor
    top: float  # y of the upper floor
    side_entry: bool = False  # the landing beside the lowest steps (a short room), not before

    @property
    def run(self) -> float:
        """Horizontal length of the treads (the last riser rises onto the upper floor)."""
        return (self.steps - 1) * self.tread

    def at(self, t: float, s: float) -> Pt:
        """The point ``t`` along the flight from its foot, ``s`` from the wall into the room."""
        return (self.foot[0] + self.up[0] * t + self.side[0] * s,
                self.foot[1] + self.up[1] * t + self.side[1] * s)  # fmt: skip

    def rect(self, t0: float, t1: float, s0: float = 0.0, s1: float | None = None) -> Polygon:
        s1 = self.width if s1 is None else s1
        return Polygon([self.at(t0, s0), self.at(t1, s0), self.at(t1, s1), self.at(t0, s1)])

    @property
    def footprint(self) -> Polygon:
        return self.rect(-self.tread, self.run)  # with the ramp's toe

    @property
    def foot_landing(self) -> Polygon:
        land = DEFAULTS["landingM"]
        if self.side_entry:  # beside the first steps, on the room side
            return self.rect(-self.tread, -self.tread + land + 0.1, self.width, self.width + land)
        return self.rect(-self.tread - land, -self.tread)

    @property
    def head_landing(self) -> Polygon:
        return self.rect(self.run, self.run + DEFAULTS["landingM"])

    def opening_length(self, slab: float, headroom: float) -> float:
        """How far back from the head the ceiling must be open for ``headroom`` over the ramp."""
        slope = self.rise / self.tread
        return min(self.run, (headroom + slab) / slope + 0.1)

    def opening(self, slab: float, headroom: float) -> Polygon:
        """The hole in the ceiling over the upper flight; it reaches into the wall behind (no
        sliver of slab between wall and opening, which a convex piece would swallow)."""
        return self.rect(self.run - self.opening_length(slab, headroom), self.run, -0.25,
                         self.width + 0.05)  # fmt: skip

    @property
    def foot_point(self) -> Pt:
        """Where one stands to go up (the middle of the landing at the first step)."""
        c = self.foot_landing.centroid
        return (c.x, c.y)

    @property
    def head_point(self) -> Pt:
        return self.at(self.run + DEFAULTS["landingM"] / 2, self.width / 2)

    def json(self, slab: float, headroom: float) -> dict[str, Any]:
        r = 3
        return {
            "foot": [round(self.foot[0], r), round(self.foot[1], r)],
            "up": [round(self.up[0], 4), round(self.up[1], 4)],
            "side": [round(self.side[0], 4), round(self.side[1], 4)],
            "width": round(self.width, r), "steps": self.steps, "rise": round(self.rise, 4),
            "tread": round(self.tread, 4), "floor": round(self.floor, r),
            "sideEntry": self.side_entry,
            "top": round(self.top, r),
            "footPoint": [round(c, r) for c in self.foot_point],
            "headPoint": [round(c, r) for c in self.head_point],
            "footprint": _ring(self.footprint), "footLanding": _ring(self.foot_landing),
            "headLanding": _ring(self.head_landing), "opening": _ring(self.opening(slab, headroom)),
        }  # fmt: skip


def _ring(poly: Polygon) -> list[list[float]]:
    return [[round(x, 3), round(z, 3)] for x, z in list(poly.exterior.coords)[:-1]]


def stair_from_json(d: dict[str, Any]) -> Stair:
    return Stair(tuple(d["foot"]), tuple(d["up"]), tuple(d["side"]), d["width"], d["steps"],
                 d["rise"], d["tread"], d["floor"], d["top"],
                 bool(d.get("sideEntry", False)))  # type: ignore[arg-type]  # fmt: skip


def plan_stair(room: Polygon, upper: Polygon, floor: float, top: float,
               blocked: Sequence[Polygon], windows: Sequence[tuple[LineString, float]],
               away_from: Pt, spec: dict[str, Any] | None = None) -> Stair | None:  # fmt: skip
    """A straight flight along a wall of ``room`` (the room with the door, counter-clockwise):
    its steps and the landing before them inside the room, the landing at the head inside
    ``upper`` (the upper storey's floor), clear of ``blocked`` (the door's way in, passages); it
    may pass a window on its wall only below the sill (``windows``: inner span, sill height).
    Of the places that fit the one farthest from ``away_from`` (the door) wins; None if none."""
    sp = {**DEFAULTS, **(spec or {})}
    height = top - floor
    steps = max(2, math.ceil(height / float(sp["riseM"]) - 1e-9))
    rise = height / steps
    tread = float(sp["runM"])
    width = float(sp["widthM"])
    inside = room.buffer(0.01)
    above = upper.buffer(0.01)
    for side_entry in (False, True):  # the landing before the steps; in a short room beside them
        found = _best(room, upper, inside, above, floor, top, steps, rise, tread, width, blocked,
                      windows, away_from, sp, side_entry)  # fmt: skip
        if found is not None:
            return found
    return None


def _best(room: Polygon, upper: Polygon, inside: Any, above: Any, floor: float, top: float,  # noqa: ANN401
          steps: int, rise: float, tread: float, width: float, blocked: Sequence[Polygon],
          windows: Sequence[tuple[LineString, float]], away_from: Pt, sp: dict[str, Any],
          side_entry: bool) -> Stair | None:  # fmt: skip
    best: tuple[float, Stair] | None = None
    ring = list(room.exterior.coords)
    for a, b in zip(ring, ring[1:], strict=False):
        length = math.dist(a, b)
        u = ((b[0] - a[0]) / length, (b[1] - a[1]) / length) if length > 1e-6 else (1.0, 0.0)
        n = (-u[1], u[0])  # into a counter-clockwise room
        wall_windows = [(w, sill) for w, sill in windows if LineString([a, b]).distance(w) < 0.05]
        for sign in (1.0, -1.0):
            up = (u[0] * sign, u[1] * sign)
            start = a if sign > 0 else b
            t = 0.0
            while t <= length + 1e-9:
                base = (start[0] + up[0] * t + n[0] * float(sp["wallGapM"]),
                        start[1] + up[1] * t + n[1] * float(sp["wallGapM"]))  # fmt: skip
                t += float(sp["stepM"])
                st = Stair(base, up, n, width, steps, rise, tread, floor, top, side_entry)
                lower = st.footprint.union(st.foot_landing)
                if not inside.contains(lower) or not above.contains(st.head_landing):
                    continue
                if any(lower.intersects(q) or st.head_landing.intersects(q) for q in blocked):
                    continue
                if any(_blocks(st, w, sill, float(sp["windowClearM"])) for w, sill in wall_windows):
                    continue
                score = st.footprint.distance(_point(away_from))
                if best is None or score > best[0] + 1e-6:
                    best = (score, st)
    return None if best is None else best[1]


def _blocks(st: Stair, window: LineString, sill: float, clear: float) -> bool:
    """The steps would stand in front of the window (above its sill)."""
    ts = [(x - st.foot[0]) * st.up[0] + (z - st.foot[1]) * st.up[1] for x, z in window.coords]
    t0, t1 = min(ts) - clear, max(ts) + clear
    if t1 < -st.tread or t0 > st.run:
        return False  # beside the flight
    highest = st.floor + max(0.0, min(t1, st.run)) / st.tread * st.rise + st.rise
    return highest > sill - 0.05


def _point(p: Pt) -> Any:  # noqa: ANN401
    return Point(p)


# --- mesh -----------------------------------------------------------------------------------


def oriented_box(b: Any, corners: Sequence[Pt], y0: float, y1: float) -> None:  # noqa: ANN401
    """A box over a convex quad (x, z) from ``y0`` to ``y1``; ``b``: a ``_Builder``."""
    cx = sum(p[0] for p in corners) / 4
    cz = sum(p[1] for p in corners) / 4
    for k in range(4):
        (xa, za), (xb, zb) = corners[k], corners[(k + 1) % 4]
        w = math.dist((xa, za), (xb, zb))
        out = ((xa + xb) / 2 - cx, 0.0, (za + zb) / 2 - cz)
        b.polygon([(xa, y0, za), (xb, y0, zb), (xb, y1, zb), (xa, y1, za)],
                  [(0.0, y0), (w, y0), (w, y1), (0.0, y1)], out)  # fmt: skip
    top = [(x, y1, z) for x, z in corners]
    bottom = [(x, y0, z) for x, z in corners]
    b.polygon(top, [(x, z) for x, z in corners], (0.0, 1.0, 0.0))
    b.polygon(bottom, [(x, z) for x, z in corners], (0.0, -1.0, 0.0))


def bar(b: Any, p0: Vec3, p1: Vec3, size: float) -> None:  # noqa: ANN401
    """A square bar ``size`` thick from ``p0`` to ``p1`` (rails, slanted or level)."""
    d = np.subtract(p1, p0)
    length = float(np.linalg.norm(d))
    if length < 1e-6:
        return
    w = d / length
    helper = np.array([0.0, 1.0, 0.0]) if abs(w[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    s = np.cross(w, helper)
    s /= np.linalg.norm(s)
    t = np.cross(w, s)
    h = size / 2
    corners = [s * h + t * h, -s * h + t * h, -s * h - t * h, s * h - t * h]
    for k in range(4):
        ca, cb = corners[k], corners[(k + 1) % 4]
        mid = (ca + cb) / 2
        pts = [tuple(np.add(p0, ca)), tuple(np.add(p0, cb)), tuple(np.add(p1, cb)),
               tuple(np.add(p1, ca))]  # fmt: skip
        b.polygon(pts, [(0.0, 0.0), (size, 0.0), (size, length), (0.0, length)],
                  (float(mid[0]), float(mid[1]), float(mid[2])))  # fmt: skip


def stair_mesh(b: Any, st: Stair, rail: float) -> None:  # noqa: ANN401
    """Solid steps (each a block from the floor up to its tread) and a hand rail on the open
    side, posts at the foot, the middle and the head."""
    for i in range(st.steps - 1):
        q = st.rect(i * st.tread, (i + 1) * st.tread)
        oriented_box(b, list(q.exterior.coords)[:4], st.floor, st.floor + (i + 1) * st.rise)
    s = st.width - 0.04
    ends = [(-0.02, st.floor + st.rise), (st.run - 0.02, st.top)]
    for t, y in (*ends, (st.run / 2, st.floor + st.rise + (st.top - st.floor - st.rise) / 2)):
        x, z = st.at(t, s)
        bar(b, (x, y, z), (x, y + rail, z), 0.06)
    (x0, z0), (x1, z1) = st.at(ends[0][0], s), st.at(ends[1][0], s)
    bar(b, (x0, ends[0][1] + rail, z0), (x1, ends[1][1] + rail, z1), 0.05)


def opening_rail(b: Any, st: Stair, opening: Polygon, rail: float) -> list[Polygon]:  # noqa: ANN401
    """A rail round the opening upstairs on its three closed sides (not over the head of the
    flight); returns their footprints for the collision."""
    # corners as ``Stair.rect``: foot end at the wall, head at the wall, head in the room, foot
    # end in the room; the wall closes one long side, the head is the way up
    ring = list(opening.exterior.coords)[:4]
    feet = []
    sides = [(ring[0], ring[3]), (ring[3], ring[2])]  # the foot end, the side to the room
    y = st.top
    for (xa, za), (xb, zb) in sides:
        bar(b, (xa, y + rail, za), (xb, y + rail, zb), 0.05)
        for x, z in ((xa, za), (xb, zb), ((xa + xb) / 2, (za + zb) / 2)):
            bar(b, (x, y, z), (x, y + rail, z), 0.06)
        dx, dz = xb - xa, zb - za
        ln = math.hypot(dx, dz)
        nx, nz = -dz / ln * 0.04, dx / ln * 0.04
        feet.append(Polygon([(xa - nx, za - nz), (xb - nx, zb - nz), (xb + nx, zb + nz),
                             (xa + nx, za + nz)]))  # fmt: skip
    return feet


def opening_faces(b: Any, opening: Polygon, y0: float, y1: float,  # noqa: ANN401
                  room: Polygon | None = None) -> None:  # fmt: skip
    """The cut edges of the slab round the opening (facing into it); with ``room`` only those in
    the room (the opening reaches into the wall: there the wall's own face closes it)."""
    hole = Polygon(opening) if room is None else Polygon(opening).intersection(room)
    if hole.is_empty or not isinstance(hole, Polygon):
        return
    ring = list(hole.exterior.coords)
    c = hole.centroid
    for (xa, za), (xb, zb) in zip(ring, ring[1:], strict=False):
        if room is not None and room.exterior.distance(Point((xa + xb) / 2, (za + zb) / 2)) < 0.01:
            continue
        w = math.dist((xa, za), (xb, zb))
        inward = (c.x - (xa + xb) / 2, 0.0, c.y - (za + zb) / 2)
        b.polygon([(xa, y0, za), (xb, y0, zb), (xb, y1, zb), (xa, y1, za)],
                  [(0.0, y0), (w, y0), (w, y1), (0.0, y1)], inward)  # fmt: skip


# --- collision ------------------------------------------------------------------------------


def ramp_body(st: Stair, origin: Vec3, name: str) -> CollisionPart:
    """The flight as one smooth wedge: from one tread before the first step (on the floor) up to
    the upper floor at the head; slope exactly rise / tread."""
    pts = []
    for s in (0.0, st.width):
        (xa, za), (xb, zb) = st.at(-st.tread, s), st.at(st.run, s)
        pts += [(xa, st.floor, za), (xb, st.floor, zb), (xb, st.top, zb)]
    tris = [(0, 1, 2), (3, 5, 4), (0, 3, 4), (0, 4, 1), (1, 4, 5), (1, 5, 2), (2, 5, 3),
            (2, 3, 0)]  # fmt: skip
    pos = np.asarray(pts, dtype=np.float64) - np.asarray(origin)
    centre = pos.mean(axis=0)
    fixed = []
    for a, b, d in tris:
        pa, pb, pd = pos[a], pos[b], pos[d]
        if np.dot(np.cross(pb - pa, pd - pa), (pa + pb + pd) / 3 - centre) < 0:
            b, d = d, b
        fixed.append((a, b, d))
    return CollisionPart(name, np.round(pos, 4).astype(np.float32),
                         np.asarray(fixed, dtype=np.uint32).reshape(-1))  # fmt: skip
