"""Rule-based half-timbered houses (W5 groundwork, ``buildings --mode medieval``).

Mechanics only: storeys, jetties on the street sides, openings (exact from the facade annotation
or procedural), timber frames from pattern data, roof with overhang. All numbers and the meaning
of style words come from ``data/building_rules.json`` (a draft until the owner decides). The output
has five material roles shared by every house (equal values, so the engine can batch by material):
``wall_ground``, ``infill``, ``timber``, ``roof``, ``frame``.

Facade coordinates: ``u`` metres from the left end of an edge as seen from outside, ``v`` metres
above the storey floor (the convention of the overrides, leonberg-pipeline.md section 4).
"""

from __future__ import annotations

import hashlib
import json
import math
import random
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Polygon
from shapely.ops import split
from shapely.strtree import STRtree

from gothar_worldgen.buildings.gltf import Primitive
from gothar_worldgen.buildings.massing import (
    Mass,
    _Builder,
    _Roof,
    _valid_polygon,
    masses_for_building,
)

ROLES = ("wall_ground", "infill", "timber", "roof", "frame")
# Front faces of beams lie at slightly different depths: no coplanar overlaps where they cross.
DEPTH_FACTOR = {"sill": 1.0, "post": 0.9, "rail": 0.8, "brace": 0.7}


# --- rules -----------------------------------------------------------------------------------


@dataclass(frozen=True)
class Rules:
    data: dict[str, Any]

    def get(self, *keys: str) -> Any:  # noqa: ANN401
        v: Any = self.data
        for k in keys:
            v = v[k]
        return v

    def color(self, role: str) -> tuple[float, float, float, float]:
        c = self.data["materials"][role]
        return (float(c[0]), float(c[1]), float(c[2]), float(c[3]))

    def pattern(self, name: str | None = None) -> list[tuple[float, float, float, float]]:
        t = self.data["timber"]
        key = name if name and name in t["patterns"] else t["pattern"]
        return [tuple(map(float, s)) for s in t["patterns"][key]["segments"]]  # type: ignore[misc]


def load_rules(path: Path) -> Rules:
    data = json.loads(path.read_text(encoding="utf-8"))
    for role in ROLES:
        if role not in data.get("materials", {}):
            raise ValueError(f"{path.name}: material role '{role}' missing")
    return Rules(data)


# --- helpers ---------------------------------------------------------------------------------


def _rng(building_id: str, seed: int | None) -> random.Random:
    digest = hashlib.sha256(f"{building_id}:{seed or 0}".encode()).hexdigest()
    return random.Random(int(digest[:16], 16))


def _outward_normals(ring: Sequence[tuple[float, float]]) -> list[tuple[float, float]]:
    area2 = sum(ring[i - 1][0] * ring[i][1] - ring[i][0] * ring[i - 1][1] for i in range(len(ring)))
    sign = 1.0 if area2 > 0 else -1.0  # > 0: counter-clockwise in (x, z) maths axes
    normals = []
    for i, a in enumerate(ring):
        b = ring[(i + 1) % len(ring)]
        dx, dz = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dz) or 1.0
        normals.append((sign * dz / length, -sign * dx / length))
    return normals


def offset_ring(
    ring: Sequence[tuple[float, float]], distances: Sequence[float]
) -> list[tuple[tuple[float, float], int]]:
    """Moves edge i outwards by ``distances[i]``; returns (vertex, index of the edge from there).

    Where neighbouring edges are (nearly) parallel or the mitre would be too long, two vertices are
    used; the short connecting edge gets index -1 (plain wall, no openings or timber).
    """
    normals = _outward_normals(ring)
    n = len(ring)
    out: list[tuple[tuple[float, float], int]] = []
    for i in range(n):
        j = (i - 1) % n
        pi = np.asarray(ring[i])
        ni, nj = np.asarray(normals[i]), np.asarray(normals[j])
        di, dj = distances[i], distances[j]
        ei = np.asarray(ring[(i + 1) % n]) - pi
        ej = pi - np.asarray(ring[j])
        cross = ej[0] * ei[1] - ej[1] * ei[0]
        a_j = pi + nj * dj  # point on offset line j
        a_i = pi + ni * di
        if abs(di - dj) < 1e-9 and di == 0:
            out.append(((float(pi[0]), float(pi[1])), i))
            continue
        if abs(cross) > 1e-9 * np.linalg.norm(ei) * np.linalg.norm(ej):
            t = ((a_i - a_j)[0] * ei[1] - (a_i - a_j)[1] * ei[0]) / cross
            v = a_j + ej * t
            if np.linalg.norm(v - pi) <= 3 * max(abs(di), abs(dj), 1e-9) + 1e-9:
                out.append(((float(v[0]), float(v[1])), i))
                continue
        out.append(((float(a_j[0]), float(a_j[1])), -1))
        out.append(((float(a_i[0]), float(a_i[1])), i))
    return out


@dataclass
class Opening:
    kind: str  # window | door | gate
    u: float
    v: float
    w: float
    h: float


@dataclass
class FacadePlan:
    """Openings per storey for one footprint edge."""

    openings: dict[int, list[Opening]] = field(default_factory=dict)


# --- storeys ---------------------------------------------------------------------------------


def storey_heights(
    wall: float, rules: Rules, override: Sequence[float] | None, rng: random.Random
) -> list[float]:
    """Storey heights (bottom up) that add up to ``wall`` (ground to eave)."""
    if wall <= 0:
        return []
    if override:
        hs = [float(h) for h in override]
        total = sum(hs)
        if total < wall:
            hs[-1] += wall - total
        elif total > wall:
            hs = [h * wall / total for h in hs]
        return hs
    s = rules.get("storeys")
    g, u = float(s["groundM"]), float(s["upperM"])
    n = 1 if wall < g + 0.6 * u else 1 + round((wall - g) / u)
    n = max(int(s["min"]), min(int(s["max"]), n))
    if n == 1:
        return [wall]
    var = float(rules.get("variation", "storeyM"))
    ground = min(g + rng.uniform(-var, var), wall * 0.6)
    upper = (wall - ground) / (n - 1)
    return [ground] + [upper] * (n - 1)


# --- openings --------------------------------------------------------------------------------


def procedural_openings(
    width: float, heights: Sequence[float], rules: Rules, rng: random.Random, door: bool,
    usable: Sequence[float],
) -> dict[int, list[Opening]]:  # fmt: skip
    """Evenly spaced windows per storey, a door in the ground storey if ``door``.

    ``usable[s]`` is the height of storey s below the gable (windows stay below it).
    """
    o = rules.get("openings")
    win, beam = o["window"], float(rules.get("timber", "beamM"))
    result: dict[int, list[Opening]] = {}
    if width < float(o["minFacadeM"]):
        return result
    shift = rng.uniform(-1, 1) * float(rules.get("variation", "windowShiftM"))
    for s, _h in enumerate(heights):
        room = usable[s]
        w, sill = float(win["w"]), float(win["sill"])
        h = min(float(win["h"]), room - sill - 1.5 * beam)
        if h < 0.4:
            continue
        span = width - 2 * float(win["cornerM"])
        if span < w:
            continue
        n = int((span - w) // float(win["spacingM"])) + 1
        centres = [width / 2 + shift + (i - (n - 1) / 2) * float(win["spacingM"]) for i in range(n)]
        row = [Opening("window", c - w / 2, sill, w, h) for c in centres]
        if s == 0 and door:
            d = o["door"]
            dh = min(float(d["h"]), room - 1.5 * beam)
            k = min(range(len(row)), key=lambda i: abs(centres[i] - width / 2)) if row else -1
            c = centres[k] if row else width / 2
            if row:
                row.pop(k)
            row.append(Opening("door", c - float(d["w"]) / 2, 0.0, float(d["w"]), dh))
        result[s] = [op for op in row if op.u >= 0.2 and op.u + op.w <= width - 0.2]
    return result


def override_openings(front: Any, heights: Sequence[float]) -> dict[int, list[Opening]]:  # noqa: ANN401
    result: dict[int, list[Opening]] = {}
    for op in front.openings:
        if op.storey >= len(heights):
            continue
        y = op.y if op.y is not None else 0.0
        result.setdefault(op.storey, []).append(Opening(op.type, op.x, y, op.w, op.h))
    return result


# --- timber ----------------------------------------------------------------------------------


def _clip_out(
    p0: np.ndarray, p1: np.ndarray, rects: Sequence[Opening]
) -> list[tuple[np.ndarray, np.ndarray]]:
    """Parts of segment p0-p1 outside all rectangles (Liang-Barsky per rectangle)."""
    keep = [(0.0, 1.0)]
    d = p1 - p0
    for r in rects:
        t0, t1 = 0.0, 1.0
        inside = True
        for p, q in ((-d[0], p0[0] - r.u), (d[0], r.u + r.w - p0[0]),
                     (-d[1], p0[1] - r.v), (d[1], r.v + r.h - p0[1])):  # fmt: skip
            if abs(p) < 1e-12:
                if q < 0:
                    inside = False
                    break
            else:
                t = q / p
                if p < 0:
                    t0 = max(t0, t)
                else:
                    t1 = min(t1, t)
        if not inside or t0 >= t1:
            continue
        nxt = []
        for a, b in keep:
            if t1 <= a or t0 >= b:
                nxt.append((a, b))
                continue
            if t0 > a:
                nxt.append((a, t0))
            if t1 < b:
                nxt.append((t1, b))
        keep = nxt
    return [(p0 + d * a, p0 + d * b) for a, b in keep if (b - a) * float(np.linalg.norm(d)) > 0.05]


def timber_segments(width: float, height: float, openings: Sequence[Opening], rules: Rules,
                    pattern: Sequence[tuple[float, float, float, float]],
                    bays: bool = True) -> list[tuple[str, np.ndarray, np.ndarray]]:  # fmt: skip
    """Beam centre lines (kind, start, end) in facade coordinates for one storey."""
    b = float(rules.get("timber", "beamM"))
    segs: list[tuple[str, np.ndarray, np.ndarray]] = []
    lo, hi = b / 2, height - b / 2
    if hi - lo < 2 * b or width < 2 * b:
        return segs
    for v in (lo, hi):  # sill and plate over the whole width, cut at doors
        for a, c in _clip_out(np.array([0.0, v]), np.array([width, v]), openings):
            segs.append(("sill", a, c))
    # Corner and opening posts first; bay posts only where they do not crowd those.
    fixed = sorted(
        {b / 2, width - b / 2} | {x for op in openings for x in (op.u - b / 2, op.u + op.w + b / 2)}
    )
    fixed = [p for p in fixed if b / 2 - 1e-6 <= p <= width - b / 2 + 1e-6]
    merged: list[float] = []
    for p in fixed:  # two posts closer than a beam width become one
        if merged and p - merged[-1] <= b * 1.2:
            merged[-1] = (merged[-1] + p) / 2
        else:
            merged.append(p)
    if bays:
        n_bays = max(1, round(width / float(rules.get("timber", "bayM"))))
        for i in range(1, n_bays):
            p = width * i / n_bays
            if all(abs(p - q) > b * 1.2 for q in merged):
                merged.append(p)
        merged.sort()
    for p in merged:
        for a, c in _clip_out(np.array([p, lo + b / 2]), np.array([p, hi - b / 2]), openings):
            segs.append(("post", a, c))
    for left, right in zip(merged, merged[1:], strict=False):
        x0, x1 = left + b / 2, right - b / 2
        if x1 - x0 < b:
            continue
        for u0, v0, u1, v1 in pattern:
            p0 = np.array([x0 + (x1 - x0) * u0, lo + b / 2 + (hi - lo - b) * v0])
            p1 = np.array([x0 + (x1 - x0) * u1, lo + b / 2 + (hi - lo - b) * v1])
            kind = "rail" if abs(v1 - v0) < 1e-9 else "brace"
            for a, c in _clip_out(p0, p1, openings):
                segs.append((kind, a, c))
    return segs


# --- facade frame ----------------------------------------------------------------------------


@dataclass(frozen=True)
class Frame:
    """Facade plane of an edge: origin at its left end (seen from outside), storey floor height."""

    lx: float
    lz: float
    ax: float  # unit axis from left to right
    az: float
    nx: float  # unit outward normal
    nz: float
    y0: float
    width: float

    @property
    def left(self) -> np.ndarray:
        return np.array([self.lx, self.lz])

    @property
    def axis(self) -> np.ndarray:
        return np.array([self.ax, self.az])

    def point(self, u: float, v: float, depth: float = 0.0) -> tuple[float, float, float]:
        return (self.lx + self.ax * u + self.nx * depth, self.y0 + v,
                self.lz + self.az * u + self.nz * depth)  # fmt: skip

    def n3(self) -> tuple[float, float, float]:
        return (self.nx, 0.0, self.nz)


def make_frame(a: Sequence[float], b: Sequence[float], normal: Sequence[float], y0: float) -> Frame:
    nx, nz = float(normal[0]), float(normal[1])
    rx, rz = nz, -nx  # right, seen from outside (looking along -normal)
    dx, dz = b[0] - a[0], b[1] - a[1]
    left, other = (a, b) if dx * rx + dz * rz > 0 else (b, a)
    width = math.dist(left, other)
    w = width or 1.0
    return Frame(float(left[0]), float(left[1]), (other[0] - left[0]) / w, (other[1] - left[1]) / w,
                 nx, nz, y0, width)  # fmt: skip


def _wall(
    builder: _Builder, f: Frame, outline: Sequence[tuple[float, float]], holes: Sequence[Opening]
) -> None:
    """Planar wall polygon (facade coordinates) minus rectangular holes."""
    poly = Polygon(outline)
    if not poly.is_valid or poly.area < 1e-4:
        return
    for op in holes:
        poly = poly.difference(shapely.box(op.u, op.v, op.u + op.w, op.v + op.h))
    for tri in shapely.constrained_delaunay_triangles(poly).geoms:
        pts = list(tri.exterior.coords)[:3]
        builder.polygon([f.point(u, v) for u, v in pts], [(u, v) for u, v in pts], f.n3())


def _reveal(frame_b: _Builder, f: Frame, op: Opening, depth: float) -> None:
    """Recessed panel (window/door) with the four reveal faces."""
    u0, u1, v0, v1 = op.u, op.u + op.w, op.v, op.v + op.h
    n = f.n3()
    panel = [
        f.point(u0, v0, -depth),
        f.point(u1, v0, -depth),
        f.point(u1, v1, -depth),
        f.point(u0, v1, -depth),
    ]
    frame_b.polygon(panel, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], n)
    ax = (float(f.axis[0]), 0.0, float(f.axis[1]))
    sides = [
        ((u0, v1), (u1, v1), (0.0, -1.0, 0.0)),  # lintel faces down
        ((u0, v0), (u1, v0), (0.0, 1.0, 0.0)),  # sill faces up
        ((u0, v0), (u0, v1), ax),  # left jamb faces right
        ((u1, v0), (u1, v1), (-ax[0], 0.0, -ax[2])),
    ]
    for (ua, va), (ub, vb), want in sides:
        quad = [f.point(ua, va), f.point(ub, vb), f.point(ub, vb, -depth), f.point(ua, va, -depth)]
        frame_b.polygon(quad, [(0, 0), (1, 0), (1, 1), (0, 1)], want)


def _beam(
    builder: _Builder, f: Frame, p0: np.ndarray, p1: np.ndarray, width: float, depth: float
) -> None:
    """Front face and the two long side faces; back and end faces touch wall or other beams."""
    u0, v0, u1, v1 = float(p0[0]), float(p0[1]), float(p1[0]), float(p1[1])
    du, dv = u1 - u0, v1 - v0
    length = math.hypot(du, dv)
    if length < 1e-4:
        return
    su, sv = -dv / length * width / 2, du / length * width / 2
    c = [(u0 - su, v0 - sv), (u1 - su, v1 - sv), (u1 + su, v1 + sv), (u0 + su, v0 + sv)]
    back = [f.point(u, v) for u, v in c]
    front = [f.point(u, v, depth) for u, v in c]
    builder.polygon(front, [(0, 0), (length, 0), (length, width), (0, width)], f.n3())
    for i, j, sign in ((0, 1, -1.0), (2, 3, 1.0)):
        want = (f.ax * su * sign, sv * sign, f.az * su * sign)  # away from the beam axis
        builder.polygon([back[i], back[j], front[j], front[i]],
                        [(0, 0), (length, 0), (length, depth), (0, depth)], want)  # fmt: skip


# --- street sides ----------------------------------------------------------------------------


class StreetIndex:
    def __init__(self, streets: Sequence[dict[str, Any]]) -> None:
        self.lines = [LineString(s["points"]) for s in streets if len(s.get("points") or []) >= 2]
        self.tree = STRtree(self.lines) if self.lines else None

    def faces_street(
        self, a: Sequence[float], b: Sequence[float], normal: Sequence[float], reach: float
    ) -> bool:
        if self.tree is None:
            return False
        mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ray = LineString([(mx + normal[0] * 0.3, mz + normal[1] * 0.3),
                          (mx + normal[0] * reach, mz + normal[1] * reach)])  # fmt: skip
        return len(self.tree.query(ray, predicate="intersects")) > 0


# --- house -----------------------------------------------------------------------------------


@dataclass
class HouseResult:
    primitives: list[Primitive]
    triangles: int
    notes: list[str] = field(default_factory=list)
    timber_level: int = 0  # 0 full, 1 no pattern, 2 no bay posts, 3 no timber (budget)


def _roof_mass(mass: Mass, ring: Sequence[tuple[float, float]]) -> Mass:
    return Mass(tuple(ring), mass.eave_y, mass.ridge_y, mass.roof, mass.ridge_dir)


def _extrapolated_height(roof: _Roof, x: float, z: float) -> float:
    m = roof.mass
    if m.roof == "flat" or roof.half < 1e-6:
        return m.ridge_y
    v = x * roof.v[0] + z * roof.v[1]
    if m.roof == "saddle":
        return m.ridge_y - (m.ridge_y - m.eave_y) * abs(v - roof.mid) / roof.half
    return m.eave_y + (m.ridge_y - m.eave_y) * (v - roof.vmin) / (2 * roof.half)


@dataclass
class _Context:
    rules: Rules
    rng: random.Random
    front: Any  # FrontFacade or None
    pattern: list[tuple[float, float, float, float]]
    level: int
    builders: dict[str, _Builder]
    base_y: float


def _top_outline(f: Frame, roof: _Roof, crease: LineString | None, y: float,
                 below: float) -> tuple[list[tuple[float, float]], float]:  # fmt: skip
    """Wall outline of the top storey up to the roof; returns (outline, height below the gable)."""
    us = [0.0, f.width]
    if crease is not None:
        vx, vz = roof.v
        la = f.lx * vx + f.lz * vz - roof.mid
        ra = (f.lx + f.ax * f.width) * vx + (f.lz + f.az * f.width) * vz - roof.mid
        if la * ra < 0:
            us.insert(1, f.width * la / (la - ra))
    tops = [roof.height(f.lx + f.ax * u, f.lz + f.az * u) - y for u in us]
    outline = [(0.0, -below), (f.width, -below)]
    outline += list(zip(reversed(us), reversed(tops), strict=True))
    return outline, min(tops[0], tops[-1])


def _facade(ctx: _Context, f: Frame, edge: int, s: int, heights: Sequence[float],
            outline: Sequence[tuple[float, float]], usable: float, door: bool) -> None:  # fmt: skip
    rules = ctx.rules
    plan: list[Opening] = []
    if edge >= 0:
        if ctx.front is not None and ctx.front.edge == edge:
            plan = override_openings(ctx.front, heights).get(s, [])
        else:
            room = [*heights[:-1], usable] if s == len(heights) - 1 else list(heights)
            plan = procedural_openings(f.width, heights, rules, ctx.rng, door, room).get(s, [])
    plan = [op for op in plan
            if op.u >= 0 and op.u + op.w <= f.width + 1e-6
            and op.v + op.h <= usable + 1e-6]  # fmt: skip
    role = "wall_ground" if s == 0 else "infill"
    _wall(ctx.builders[role], f, outline, plan)
    for op in plan:
        _reveal(ctx.builders["frame"], f, op, float(rules.get("openings", "revealM")))
    if s == 0 or edge < 0 or ctx.level >= 3:
        return
    beam, depth = float(rules.get("timber", "beamM")), float(rules.get("timber", "depthM"))
    pattern = ctx.pattern if ctx.level == 0 else []
    for kind, p0, p1 in timber_segments(f.width, usable, plan, rules, pattern, bays=ctx.level < 2):
        _beam(ctx.builders["timber"], f, p0, p1, beam, depth * DEPTH_FACTOR[kind])


def _mass(ctx: _Context, mass: Mass, ground: float, override: Any,  # noqa: ANN401
          streets: StreetIndex | None, notes: list[str]) -> None:  # fmt: skip
    rules = ctx.rules
    if _valid_polygon(mass.footprint) is None:
        return
    ring = [(float(x), float(z)) for x, z in mass.footprint]
    storeys = getattr(override, "storeys", None) or None
    heights = storey_heights(mass.eave_y - ground, rules, storeys, ctx.rng)
    if not heights:
        return
    normals = _outward_normals(ring)
    n = len(ring)
    reach = float(rules.get("jetty", "streetReachM"))
    street = []
    for i in range(n):
        faces = bool(
            streets and streets.faces_street(ring[i], ring[(i + 1) % n], normals[i], reach)
        )
        street.append(faces or (ctx.front is not None and ctx.front.edge == i))
    sides = rules.get("jetty", "sides")
    jetty_edges = [sides == "all" or (sides == "street" and street[i]) for i in range(n)]
    jetty = getattr(override, "jetty_m", None)
    jetty = float(rules.get("jetty", "defaultM")) if jetty is None else float(jetty)
    lengths = [math.dist(ring[i], ring[(i + 1) % n]) for i in range(n)]
    candidates = [i for i in range(n) if street[i]] or list(range(n))
    door_edge = max(candidates, key=lambda i: lengths[i])

    outlines = [offset_ring(ring, [jetty * s if jetty_edges[i] else 0.0 for i in range(n)])
                for s in range(len(heights))]  # fmt: skip
    if any(not Polygon([p for p, _ in o]).is_valid for o in outlines):
        notes.append("jetty skipped (invalid offset)")
        outlines = [offset_ring(ring, [0.0] * n) for _ in heights]
    top_ring = [p for p, _ in outlines[-1]]
    top_poly = Polygon(top_ring)
    roof = _Roof(_roof_mass(mass, top_ring), top_poly)
    crease = roof.crease(top_poly)

    y = ground
    for s, h in enumerate(heights):
        ring_s = outlines[s]
        ns = _outward_normals([p for p, _ in ring_s])
        below = y - ctx.base_y if s == 0 else 0.0  # the ground storey reaches down to the base
        for k, (a, edge) in enumerate(ring_s):
            f = make_frame(a, ring_s[(k + 1) % len(ring_s)][0], ns[k], y)
            if f.width < 1e-3:
                continue
            if s == len(heights) - 1:
                outline, usable = _top_outline(f, roof, crease, y, below)
            else:
                outline, usable = [(0.0, -below), (f.width, -below), (f.width, h), (0.0, h)], h
            _facade(ctx, f, edge, s, heights, outline, usable, edge == door_edge)
        if s > 0:
            _jetty_underside(ctx, [p for p, _ in ring_s], [p for p, _ in outlines[s - 1]], y)
        y += h
    _add_roof(ctx.builders["roof"], roof, top_poly, crease, rules)


def _jetty_underside(ctx: _Context, upper: list, lower: list, y: float) -> None:
    try:
        gap = Polygon(upper).difference(Polygon(lower))
    except shapely.errors.GEOSException:
        return
    if gap.area <= 1e-3:
        return
    for tri in shapely.constrained_delaunay_triangles(gap).geoms:
        pts = [(x, y, z) for x, z in list(tri.exterior.coords)[:3]]
        ctx.builders["timber"].polygon(pts, [(x, z) for x, _, z in pts], (0.0, -1.0, 0.0))


def build_house(
    building: dict[str, Any],
    base_y: float,
    origin_xz: tuple[float, float],
    rules: Rules,
    streets: StreetIndex | None = None,
    override: Any = None,  # noqa: ANN401  BuildingOverride or None
) -> HouseResult:
    """Half-timbered house of a ``buildings.json`` entry; vertices relative to (origin, base_y).

    Over the triangle budget the timber is reduced step by step: no pattern, no bay posts, none.
    """
    budget = int(rules.get("budget", "trianglesPerBuilding"))
    front = getattr(override, "front_facade", None)
    notes: list[str] = []
    if front is not None and building.get("parts"):
        notes.append("frontFacade ignored (building has LoD2 parts)")
        front = None
    massing = masses_for_building(building)
    sources = building.get("parts") or [building]
    pattern = rules.pattern(getattr(front, "timber", None))
    result = None
    for level in range(4):
        builders = {role: _Builder((origin_xz[0], base_y, origin_xz[1])) for role in ROLES}
        rng = _rng(building["id"], getattr(override, "seed", None))
        ctx = _Context(rules, rng, front, pattern, level, builders, base_y)
        level_notes: list[str] = []
        for mass, src in zip(massing.masses, sources, strict=False):
            ground = float(src.get("groundY", building.get("groundY", base_y)))
            try:
                _mass(ctx, mass, ground, override, streets, level_notes)
            except shapely.errors.GEOSException:
                level_notes.append("part skipped (invalid geometry)")
        prims = [Primitive(r, rules.color(r), builders[r].mesh()) for r in ROLES if builders[r].idx]
        tris = sum(p.mesh.triangle_count for p in prims)
        result = HouseResult(prims, tris, [*notes, *massing.notes, *level_notes], level)
        if tris <= budget:
            break
    assert result is not None
    if result.timber_level:
        result.notes.append(f"timber reduced to level {result.timber_level} (budget {budget})")
    return result


def _add_roof(
    b: _Builder, roof: _Roof, top: Polygon, crease: LineString | None, rules: Rules
) -> None:
    over = float(rules.get("roof", "overhangM"))
    thick = float(rules.get("roof", "thicknessM"))
    outer = top.buffer(over, join_style="mitre", mitre_limit=3.0)
    if not isinstance(outer, Polygon):
        outer = top
    pieces = list(split(outer, crease).geoms) if crease is not None else [outer]
    for piece in pieces:
        for tri in shapely.constrained_delaunay_triangles(piece).geoms:
            pts = [
                (x, _extrapolated_height(roof, x, z) + thick, z)
                for x, z in list(tri.exterior.coords)[:3]
            ]
            b.polygon(pts, [(x - b.ox, z - b.oz) for x, _, z in pts], (0.0, 1.0, 0.0))
    eaves = outer.difference(top)
    under = (
        list(split(eaves, crease).geoms) if crease is not None and not eaves.is_empty else [eaves]
    )
    for piece in under:
        for poly in getattr(piece, "geoms", [piece]):
            if poly.is_empty or poly.area < 1e-4:
                continue
            for tri in shapely.constrained_delaunay_triangles(poly).geoms:
                pts = [
                    (x, _extrapolated_height(roof, x, z), z)
                    for x, z in list(tri.exterior.coords)[:3]
                ]
                b.polygon(pts, [(x - b.ox, z - b.oz) for x, _, z in pts], (0.0, -1.0, 0.0))
    # Fascia around the overhang, split where the ridge crosses.
    ring = list(outer.exterior.coords)[:-1]
    normals = _outward_normals(ring)
    for i, a in enumerate(ring):
        c = ring[(i + 1) % len(ring)]
        pts = [a]
        if crease is not None:
            va = a[0] * roof.v[0] + a[1] * roof.v[1] - roof.mid
            vc = c[0] * roof.v[0] + c[1] * roof.v[1] - roof.mid
            if va * vc < 0:
                t = va / (va - vc)
                pts.append((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t))
        pts.append(c)
        for p, q in zip(pts, pts[1:], strict=False):
            hp, hq = _extrapolated_height(roof, *p), _extrapolated_height(roof, *q)
            quad = [
                (p[0], hp, p[1]),
                (q[0], hq, q[1]),
                (q[0], hq + thick, q[1]),
                (p[0], hp + thick, p[1]),
            ]
            b.polygon(
                quad, [(0, 0), (1, 0), (1, thick), (0, thick)], (normals[i][0], 0.0, normals[i][1])
            )
