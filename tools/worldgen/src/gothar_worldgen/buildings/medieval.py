"""Rule-based half-timbered houses (W5, ``buildings --mode medieval``).

Mechanics: storeys, jetties on the street sides, openings (exact from the facade annotation or
procedural), timber frames from pattern data (including ornamental parapet fields), stone socles or
massive ground storeys, roofs with overhang (flat and low roofs become steep saddle roofs), masonry
chimneys near the ridge and a few shed or gable dormers.
Style (decided 2026-10-03 by the koordinator on the owner's behalf, ``data/building_rules.json``):
each house gets a style from the override, OSM, ALKIS function or location, then a weighted,
seeded choice of timber pattern, infill, roof cover and timber colour.

Materials come from a fixed palette with equal values in every house (the engine batches by
material value). Internally a house is built in roles (``wall_ground``, ``infill``, ``timber``,
``roof``, ``roof_north``, ``frame``) that are mapped to palette entries at the end.

Facade coordinates: ``u`` metres from the left end of an edge as seen from outside, ``v`` metres
above the storey floor (the convention of the overrides, leonberg-pipeline.md section 4).
"""

from __future__ import annotations

import hashlib
import json
import math
import random
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import split
from shapely.strtree import STRtree

from gothar_worldgen.buildings.collision import (
    CollisionResult,
    collision_for,
    convex_pieces,
    prism_body,
)
from gothar_worldgen.buildings.gltf import CollisionPart, MeshData, Primitive
from gothar_worldgen.buildings.massing import (
    Mass,
    _add_mass,
    _Builder,
    _Roof,
    _valid_polygon,
    masses_for_building,
)

# *_low: the dirty foot band of a textured house (W5)
ROLES = ("wall_ground", "infill", "timber", "roof", "roof_north", "frame", "chimney",
         "wall_ground_low", "infill_low", "wall_ground_streak", "infill_streak",
         "room_wall", "room_floor", "room_ceiling", "room_beam")  # fmt: skip
ROOM_ROLES = ("room_wall", "room_floor", "room_ceiling", "room_beam")  # outside the house budget
LOW_SUFFIX = "~low"  # material name of the foot band (textures.apply.LOW)
MOSS_SUFFIX = "~moss"  # shady roof side of a textured house (textures.apply.MOSS)
STREAK_SUFFIX = "~streak"  # plaster under a window with a rain streak (textures.apply.STREAK)
STREAK_M = 0.6  # how far a streak runs down from the sill
STREAK_SHARE = 0.65  # share of windows with a streak (deterministic per window)
STREAK_VARIANTS = 4  # textures.procedural.STREAK_VARIANTS
# Front faces of beams lie at slightly different depths: no coplanar overlaps where they cross.
DEPTH_FACTOR = {"sill": 1.0, "post": 0.9, "rail": 0.5, "brace": 0.45}
# Rails and braces are flat boards (front face only): they sit only ~3 cm proud of the wall, their
# sides barely show, and figures (Mann, Andreaskreuz) stay within the triangle budget.
BOARD_KINDS = {"rail", "brace"}
BRACE_STAGGER = 0.06  # crossing braces: each further segment of a pattern a little flatter
DOOR_PROBE_M = 0.6  # the ground in front of a door / opening is read this far out
DOOR_TOLERANCE_M = 0.15  # floor this close to the ground at the door: no change
DOOR_FREE_PROBES_M = (1.2, 2.2)  # in front of a usable door nothing stands at these distances ...
DOOR_CLEAR_M = 0.5  # ... closer than this (character radius + margin, as the waynet asks)
DOOR_SIDE_MIN_M = 2.0  # a door moved to a side or back wall needs at least this much wall
DOOR_WALL_M = 1.3  # door probes this close to the city wall line are blocked (houses inside)
DOOR_REACH_M = 30.0  # a door "has a way" if a street axis this near is reachable in a line
SILL_CLEAR_M = 0.1  # ground-storey openings need their sill this far above the terrain
STAIR_EXTRA_M = 0.2  # steps reach this far beyond the door on both sides
STAIR_SINK_M = 0.2  # steps reach this far into the ground
NORTH_ROOF = -0.25  # roof faces whose normal z is below this face north (-Z): moss


# --- rules -----------------------------------------------------------------------------------


@dataclass(frozen=True)
class Rules:
    data: dict[str, Any]

    def get(self, *keys: str) -> Any:  # noqa: ANN401
        v: Any = self.data
        for k in keys:
            v = v[k]
        return v

    def color(self, material: str) -> tuple[float, float, float, float]:
        c = self.data["palette"][material]
        return (float(c[0]), float(c[1]), float(c[2]), float(c[3]))

    def pattern(self, name: str | None = None) -> list[tuple[float, float, float, float]]:
        t = self.data["timber"]
        key = name if name and name in t["patterns"] else t["pattern"]
        return [tuple(map(float, s)) for s in t["patterns"][key]["segments"]]  # type: ignore[misc]

    def is_brustung(self, name: str | None) -> bool:
        p = self.data["timber"]["patterns"].get(name or "")
        return bool(isinstance(p, dict) and p.get("brustung"))


def load_rules(path: Path) -> Rules:
    data = json.loads(path.read_text(encoding="utf-8"))
    palette = {k for k in data.get("palette", {}) if k != "note"}
    for name in ("stone", "frame", "roof_old", "roof_old_moss", "timber_dark"):
        if name not in palette:
            raise ValueError(f"{path.name}: palette entry '{name}' missing")
    for style, prof in data.get("styles", {}).items():
        for key in ("infill", "roof", "timberColor"):
            for material in prof.get(key, {}):
                if material not in palette:
                    raise ValueError(f"{path.name}: style {style}: '{material}' not in the palette")
        for pattern in prof.get("patterns", {}):
            if pattern not in data["timber"]["patterns"]:
                raise ValueError(f"{path.name}: style {style}: pattern '{pattern}' unknown")
    return Rules(data)


# --- helpers ---------------------------------------------------------------------------------


def _rng(building_id: str, seed: int | None, salt: str = "") -> random.Random:
    digest = hashlib.sha256(f"{building_id}:{seed or 0}{salt}".encode()).hexdigest()
    return random.Random(int(digest[:16], 16))


def _weighted(rng: random.Random, weights: dict[str, float]) -> str:
    items = sorted((k, float(v)) for k, v in weights.items() if float(v) > 0)
    total = sum(v for _, v in items)
    x = rng.random() * total
    for k, v in items:
        x -= v
        if x < 0:
            return k
    return items[-1][0]


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
    usable: Sequence[float], gate: bool = False,
) -> dict[int, list[Opening]]:  # fmt: skip
    """Evenly spaced windows per storey, a door (or a gate) in the ground storey if ``door``.

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
            d = o.get("gate", {"w": 2.6, "h": 2.6}) if gate else o["door"]
            dw = min(float(d["w"]), width - 1.0)
            dh = min(float(d["h"]), room - 1.5 * beam)
            k = min(range(len(row)), key=lambda i: abs(centres[i] - width / 2)) if row else -1
            c = centres[k] if row else width / 2
            if row:
                row.pop(k)
            if gate:  # the gate takes the neighbouring windows' room
                row = [
                    op for op in row if op.u + op.w < c - dw / 2 - 0.3 or op.u > c + dw / 2 + 0.3
                ]
            row.append(Opening("gate" if gate else "door", c - dw / 2, 0.0, dw, dh))
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
                    pattern: Sequence[tuple[float, float, float, float]], bays: bool = True,
                    brustung: Sequence[tuple[float, float, float, float]] = (),
                    plain: Sequence[tuple[float, float, float, float]] | None = None,
                    figure_every: int = 0,
                    ) -> list[tuple[str, int, np.ndarray, np.ndarray]]:  # fmt: skip
    """Beam centre lines (kind, index in its pattern, start, end) in facade coordinates.

    ``brustung``: ornament for the parapet field below a window (between sill beam and window).
    ``figure_every`` > 0: the ``pattern`` figure only in the first and last field and in every
    n-th field between, the others get ``plain`` (as carpenters placed Mann figures at corners).
    """
    b = float(rules.get("timber", "beamM"))
    segs: list[tuple[str, int, np.ndarray, np.ndarray]] = []
    lo, hi = b / 2, height - b / 2
    if hi - lo < 2 * b or width < 2 * b:
        return segs
    for v in (lo, hi):  # sill and plate over the whole width, cut at doors
        for a, c in _clip_out(np.array([0.0, v]), np.array([width, v]), openings):
            segs.append(("sill", 0, a, c))
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
            segs.append(("post", 0, a, c))
    fields = list(zip(merged, merged[1:], strict=False))
    for i, (left, right) in enumerate(fields):
        x0, x1 = left + b / 2, right - b / 2
        if x1 - x0 < b:
            continue
        window = next((op for op in openings if op.kind == "window" and op.u >= x0 - 1e-6
                       and op.u + op.w <= x1 + 1e-6 and op.v > 3 * b), None)  # fmt: skip
        if window is not None and brustung:
            # Parapet field: a rail under the window, the ornament below it.
            top = window.v - b / 2
            for a, c in _clip_out(np.array([x0, top]), np.array([x1, top]), openings):
                segs.append(("rail", 0, a, c))
            y0, y1 = lo + b / 2, top - b / 2
            if y1 - y0 > b:
                for k, (u0, v0, u1, v1) in enumerate(brustung):
                    p0 = np.array([x0 + (x1 - x0) * u0, y0 + (y1 - y0) * v0])
                    p1 = np.array([x0 + (x1 - x0) * u1, y0 + (y1 - y0) * v1])
                    segs.append(("brace", k + 1, p0, p1))
            continue
        figure = figure_every <= 0 or i in (0, len(fields) - 1) or i % figure_every == 0
        for k, (u0, v0, u1, v1) in enumerate(pattern if figure or plain is None else plain):
            p0 = np.array([x0 + (x1 - x0) * u0, lo + b / 2 + (hi - lo - b) * v0])
            p1 = np.array([x0 + (x1 - x0) * u1, lo + b / 2 + (hi - lo - b) * v1])
            kind = "rail" if abs(v1 - v0) < 1e-9 else "brace"
            for a, c in _clip_out(p0, p1, openings):
                segs.append((kind, k, a, c))
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

    def raised(self, dv: float) -> Frame:
        return Frame(self.lx, self.lz, self.ax, self.az, self.nx, self.nz, self.y0 + dv, self.width)


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
    builder: _Builder, f: Frame, outline: Sequence[tuple[float, float]] | Polygon,
    holes: Sequence[Opening],
) -> None:  # fmt: skip
    """Planar wall polygon (facade coordinates) minus rectangular holes."""
    poly = outline if isinstance(outline, Polygon) else Polygon(outline)
    if not poly.is_valid or poly.area < 1e-4:
        return
    for op in holes:
        poly = poly.difference(shapely.box(op.u, op.v, op.u + op.w, op.v + op.h))
    if poly.is_empty:
        return
    for tri in shapely.constrained_delaunay_triangles(poly).geoms:
        pts = list(tri.exterior.coords)[:3]
        builder.polygon([f.point(u, v) for u, v in pts], [(u, v) for u, v in pts], f.n3())


LOD_PANEL_M = 0.01  # lod 1: openings as flat panels this far in front of the wall


def _flat_opening(frame_b: _Builder, f: Frame, op: Opening) -> None:
    """lod 1: a window or door as one dark panel just in front of the closed wall."""
    u0, u1, v0, v1 = op.u, op.u + op.w, op.v, op.v + op.h
    d = LOD_PANEL_M
    pts = [f.point(u0, v0, d), f.point(u1, v0, d), f.point(u1, v1, d), f.point(u0, v1, d)]
    frame_b.polygon(pts, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], f.n3())


def _reveal(frame_b: _Builder, f: Frame, op: Opening, depth: float, panel: bool = True,
            start: float = 0.0) -> None:  # fmt: skip
    """Recessed panel (window/door) with the four reveal faces; ``panel=False``: open through;
    ``start``: the faces begin this deep in the wall (a niche continued)."""
    u0, u1, v0, v1 = op.u, op.u + op.w, op.v, op.v + op.h
    n = f.n3()
    if panel:
        quad = [f.point(u0, v0, -depth), f.point(u1, v0, -depth), f.point(u1, v1, -depth),
                f.point(u0, v1, -depth)]  # fmt: skip
        frame_b.polygon(quad, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], n)
    ax = (float(f.axis[0]), 0.0, float(f.axis[1]))
    sides = [
        ((u0, v1), (u1, v1), (0.0, -1.0, 0.0)),  # lintel faces down
        ((u0, v0), (u1, v0), (0.0, 1.0, 0.0)),  # sill faces up
        ((u0, v0), (u0, v1), ax),  # left jamb faces right
        ((u1, v0), (u1, v1), (-ax[0], 0.0, -ax[2])),
    ]
    for (ua, va), (ub, vb), want in sides:
        quad = [f.point(ua, va, -start), f.point(ub, vb, -start), f.point(ub, vb, -depth),
                f.point(ua, va, -depth)]  # fmt: skip
        frame_b.polygon(quad, [(0, 0), (1, 0), (1, 1), (0, 1)], want)


def _beam(
    builder: _Builder,
    f: Frame,
    p0: np.ndarray,
    p1: np.ndarray,
    width: float,
    depth: float,
    sides: bool = True,
) -> None:
    """Front face and (``sides``) the two long side faces; back and ends touch wall or beams."""
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
    if not sides:
        return
    for i, j, sign in ((0, 1, -1.0), (2, 3, 1.0)):
        want = (f.ax * su * sign, sv * sign, f.az * su * sign)  # away from the beam axis
        builder.polygon([back[i], back[j], front[j], front[i]],
                        [(0, 0), (length, 0), (length, depth), (0, depth)], want)  # fmt: skip


# --- site context ----------------------------------------------------------------------------


NO_WALK_HIGHWAYS = {"motorway", "motorway_link", "trunk", "trunk_link"}


class StreetIndex:
    """Streets (for street sides) plus what the style assignment needs about the surroundings.

    ``main`` highways and ``squares`` decide where Bürgerhäuser stand; ``features`` provide OSM
    churches and castles.
    """

    def __init__(self, streets: Sequence[dict[str, Any]], squares: Sequence[dict[str, Any]] = (),
                 features: Sequence[dict[str, Any]] = (),
                 main_highways: Sequence[str] = ("primary", "secondary", "tertiary", "pedestrian"),
                 representative: Sequence[str] = ("Marktplatz",),
                 ) -> None:  # fmt: skip
        self.lines = [LineString(s["points"]) for s in streets if len(s.get("points") or []) >= 2]
        self.tree = STRtree(self.lines) if self.lines else None
        # ways a character can walk (as the waynet takes them): no motorways, nothing underground
        self.walk_lines = [
            LineString(s["points"])
            for s in streets
            if len(s.get("points") or []) >= 2
            and s.get("highway") not in NO_WALK_HIGHWAYS
            and not (s.get("tunnel") and int(s.get("layer", 0) or 0) < 0)
        ]
        self.walk_tree = STRtree(self.walk_lines) if self.walk_lines else None
        main = [
            LineString(s["points"])
            for s in streets
            if s.get("highway") in main_highways and len(s.get("points") or []) >= 2
        ]
        main += [
            Polygon(q["polygon"]).exterior for q in squares if len(q.get("polygon") or []) >= 3
        ]
        self.main = STRtree(main) if main else None
        self.representative = [Polygon(q["polygon"]) for q in squares
                               if q.get("name") in representative
                               and len(q.get("polygon") or []) >= 3]  # fmt: skip
        self.landmarks: list[tuple[Polygon, str]] = []
        for f in features:
            if f.get("geometry") == "polygon" and f.get("kind") in ("place_of_worship", "castle"):
                self.landmarks.append((Polygon(f["polygon"]), f["kind"]))

    def faces_street(
        self, a: Sequence[float], b: Sequence[float], normal: Sequence[float], reach: float
    ) -> bool:
        if self.tree is None:
            return False
        mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ray = LineString([(mx + normal[0] * 0.3, mz + normal[1] * 0.3),
                          (mx + normal[0] * reach, mz + normal[1] * reach)])  # fmt: skip
        return len(self.tree.query(ray, predicate="intersects")) > 0

    def distance(self, x: float, z: float) -> float:
        """Distance to the nearest street axis (inf without streets)."""
        if self.tree is None:
            return math.inf
        p = Point(x, z)
        return float(self.lines[int(self.tree.nearest(p))].distance(p))

    def near_main(self, footprint: Sequence[Sequence[float]], reach: float) -> bool:
        if self.main is None:
            return False
        return len(self.main.query(Polygon(footprint).buffer(reach), predicate="intersects")) > 0

    def on_representative_square(self, footprint: Sequence[Sequence[float]], reach: float) -> bool:
        poly = Polygon(footprint)
        return any(poly.distance(sq) <= reach for sq in self.representative)

    def landmark(self, footprint: Sequence[Sequence[float]]) -> str | None:
        poly = Polygon(footprint)
        for geom, kind in self.landmarks:
            if poly.intersects(geom) and poly.intersection(geom).area > 0.5:
                return kind
        return None


# --- style -----------------------------------------------------------------------------------


@dataclass(frozen=True)
class HouseStyle:
    style: str
    massive_ground: bool
    timber: bool
    jetty: bool
    pattern: str | None
    brustung: str | None
    infill: str  # palette entries
    roof: str
    timber_color: str
    wall: str  # wall material of massive storeys
    openings: bool
    gate: bool
    age: float = 0.0  # 0 new .. 1 old (aging: ridge sag, leaning posts, irregular windows)
    chimney: str = "stone"  # palette entry of the chimneys
    wall_house: bool = False  # on the city wall line: the outward sides are the town wall (W6)


def assign_style(building: dict[str, Any], override: Any, site: StreetIndex | None,  # noqa: ANN401
                 rules: Rules) -> HouseStyle:  # fmt: skip
    """Style (override, OSM, ALKIS, location), then a seeded, weighted choice of the materials."""
    a = rules.get("assignment")
    styles = rules.get("styles")
    function = str(building.get("function") or "")
    footprint = building.get("footprint") or []
    style = getattr(override, "style", None)
    if style not in styles:
        style = None
    representative = False
    # Facing the market square: representative Bürgerhaus (massive ground storey, jetty).
    if (
        style is None
        and function not in a["officeFunctions"]
        and site
        and footprint
        and site.on_representative_square(footprint, a.get("representativeReachM", 6.0))
    ):
        style, representative = "buergerhaus", True  # fmt: skip
    if style is None and building.get("derivedFrom"):
        # Replacement houses (rueckbau): Bürgerhaus on main streets, otherwise Handwerkerhaus;
        # the function of the replaced modern building does not carry over.
        near = bool(site and footprint and site.near_main(footprint, a["mainReachM"]))
        style = "buergerhaus" if near else "handwerkerhaus"
    if style is None:
        landmark = site.landmark(footprint) if site and footprint else None
        if function.startswith(tuple(a["wallFunctions"])):
            style = "mauer"
        elif landmark == "place_of_worship" or function in a["churchFunctions"]:
            style = "kirche"
        elif landmark == "castle":
            style = "steinhaus"
        elif function in a["officeFunctions"]:
            style = "amtshaus"
        elif function.startswith(tuple(a["barnFunctions"])):
            style = "scheune"
        elif (site and footprint and site.near_main(footprint, a["mainReachM"])
              and Polygon(footprint).area >= a["buergerMinAreaM2"]):  # fmt: skip
            style = "buergerhaus"
        else:
            c = Polygon(footprint).centroid if footprint else Point(0, 0)
            style = (
                "ackerbuergerhaus"
                if math.hypot(c.x, c.y) > a["edgeDistanceM"]
                else "handwerkerhaus"
            )
    prof = styles[style]
    seed = getattr(override, "seed", None)
    bid = building["id"]
    vocab = rules.get("vocabulary")
    front = getattr(override, "front_facade", None)

    pattern = None
    if prof.get("timber"):
        pattern = _weighted(_rng(bid, seed, ":pattern"), prof["patterns"])
        wanted = getattr(front, "timber", None)
        if wanted in vocab["timber"]:
            pattern = vocab["timber"][wanted]
    brustung = prof.get("brustung")
    if pattern is not None and rules.is_brustung(
        pattern
    ):  # ornament only: plain frame, raute below
        pattern, brustung = "einfach", pattern
    infill = _weighted(_rng(bid, seed, ":infill"), prof["infill"]) if "infill" in prof else "stone"
    wanted = getattr(front, "infill", None)
    if wanted in vocab["infill"]:
        infill = vocab["infill"][wanted]
    roof = _weighted(_rng(bid, seed, ":roof"), prof["roof"])
    wanted = getattr(override, "roof_cover", None)
    if wanted in vocab["roofCover"]:
        roof = vocab["roofCover"][wanted]
    if roof == "roof_thatch" and a.get("noThatchInCore") and building.get("inCore"):
        roof = "roof_old"  # fire protection: no thatch inside the wall
    color = _weighted(_rng(bid, seed, ":color"), prof.get("timberColor", {"timber_dark": 1.0}))
    aging = rules.data.get("aging", {})
    lo, hi = aging.get("derivedAge" if building.get("derivedFrom") else "", None) or aging.get(
        "ageByStyle", {}
    ).get(style, [0.0, 0.0])
    age = getattr(override, "age", None)
    if age is None:
        age = round(_rng(bid, seed, ":age").uniform(float(lo), float(hi)), 3)
    massive = representative or _rng(bid, seed, ":ground").random() < float(
        prof.get("groundMassive", 0.0)
    )
    jetty = representative or _rng(bid, seed, ":jetty").random() < float(prof.get("jetty", 0.0))
    chimney = _weighted(
        _rng(bid, seed, ":chimney-material"),
        rules.data.get("chimneys", {}).get("material", {"stone": 1.0}),
    )
    return HouseStyle(style, massive, bool(prof.get("timber")) and pattern is not None, jetty,
                      pattern, brustung, infill, roof, color, prof.get("wall", "stone"),
                      bool(prof.get("openings", True)), bool(prof.get("gate", False)),
                      float(age), chimney)  # fmt: skip


# --- house -----------------------------------------------------------------------------------


@dataclass
class HouseResult:
    primitives: list[Primitive]
    triangles: int
    notes: list[str] = field(default_factory=list)
    # Budget levels: 0 full, 1 no pattern, 2 no bay posts, 3 no timber, 4 also no dormers.
    timber_level: int = 0
    style: HouseStyle | None = None
    steepened: int = 0  # roofs made steep (flat or below the minimum pitch)
    sag_m: float = 0.0  # largest ridge sag of the house (aging)
    dormers: int = 0
    chimneys: int = 0
    collision: CollisionResult | None = None  # COL_ bodies of the (steepened) ground footprints
    doors: list[list[Any]] = field(default_factory=list)  # x, z in front, floor, kind, nx, nz
    masses: list[Mass] = field(default_factory=list)  # the (steepened) masses, for lod 2
    room: dict[str, Any] | None = None  # W7: the enterable ground storey (door, floor, ceiling)
    room_prims: dict[str, list[Primitive]] = field(default_factory=dict)  # W7: per room
    room_cols: dict[str, list[CollisionPart]] = field(default_factory=dict)  # their COL_ box


class _SagRoof(_Roof):
    """Saddle roof whose ridge sags in the middle (aging); eaves and gable ends stay put."""

    def __init__(self, mass: Mass, poly: Polygon, sag: float) -> None:
        super().__init__(mass, poly)
        us = np.asarray(mass.footprint) @ np.asarray(self.u)
        self.umin, self.umax = float(us.min()), float(us.max())
        self.sag = sag if mass.roof == "saddle" and self.half > 1e-6 else 0.0

    @property
    def length(self) -> float:
        return self.umax - self.umin

    def sag_at(self, x: float, z: float) -> float:
        if self.sag <= 0 or self.length < 1e-6:
            return 0.0
        t = min(1.0, max(0.0, (x * self.u[0] + z * self.u[1] - self.umin) / self.length))
        v = x * self.v[0] + z * self.v[1]
        across = max(0.0, 1.0 - abs(v - self.mid) / self.half)  # 1 at the ridge, 0 at the eaves
        return self.sag * 4.0 * t * (1.0 - t) * across

    def height(self, x: float, z: float) -> float:
        return super().height(x, z) - self.sag_at(x, z)

    def bands(self, poly: Polygon, n: int) -> list[LineString]:
        """Cut lines across the ridge, so the sagging roof is built from n bands."""
        if self.sag <= 0 or n < 2:
            return []
        minx, minz, maxx, maxz = poly.bounds
        reach = 2 * math.hypot(maxx - minx, maxz - minz) + 10
        lines = []
        for k in range(1, n):
            uc = self.umin + self.length * k / n
            cx, cz = self.u[0] * uc + self.v[0] * self.mid, self.u[1] * uc + self.v[1] * self.mid
            vx, vz = self.v
            lines.append(
                LineString([(cx - vx * reach, cz - vz * reach), (cx + vx * reach, cz + vz * reach)])
            )
        return lines


def _roof_mass(mass: Mass, ring: Sequence[tuple[float, float]]) -> Mass:
    return Mass(tuple(ring), mass.eave_y, mass.ridge_y, mass.roof, mass.ridge_dir)


def _extrapolated_height(roof: _Roof, x: float, z: float) -> float:
    sag = roof.sag_at(x, z) if isinstance(roof, _SagRoof) else 0.0
    return _plain_extrapolated(roof, x, z) - sag


def _plain_extrapolated(roof: _Roof, x: float, z: float) -> float:
    m = roof.mass
    if m.roof == "flat" or roof.half < 1e-6:
        return m.ridge_y
    v = x * roof.v[0] + z * roof.v[1]
    if m.roof == "saddle":
        return m.ridge_y - (m.ridge_y - m.eave_y) * abs(v - roof.mid) / roof.half
    return m.eave_y + (m.ridge_y - m.eave_y) * (v - roof.vmin) / (2 * roof.half)


def cap_rise(mass: Mass, rules: Rules) -> Mass | None:
    """A saddle roof whose ridge lies more than ``roofPitch.maxRiseM`` above the eave gets a
    flatter pitch, down to ``minDeg`` (a very wide house keeps ``minDeg`` and a higher ridge)."""
    p = rules.get("roofPitch")
    max_rise = float(p.get("maxRiseM", 0.0))
    if max_rise <= 0 or mass.roof != "saddle" or mass.ridge_y - mass.eave_y <= max_rise:
        return None
    poly = _valid_polygon(mass.footprint)
    if poly is None:
        return None
    half = _Roof(mass, poly).half
    if half < 1e-6:
        return None
    rise = max(max_rise, math.tan(math.radians(float(p["minDeg"]))) * half)
    if rise >= mass.ridge_y - mass.eave_y:
        return None
    return replace(mass, ridge_y=mass.eave_y + rise)


def steepen(mass: Mass, rules: Rules, rng: random.Random) -> Mass | None:
    """Flat, shed or low roofs become saddle roofs of 50-55 degrees; eave kept, ridge grows."""
    p = rules.get("roofPitch")
    poly = _valid_polygon(mass.footprint)
    if poly is None:
        return None
    roof = _Roof(Mass(mass.footprint, mass.eave_y, mass.ridge_y, "saddle", mass.ridge_dir), poly)
    if roof.half < 0.5:
        return None
    pitch = math.degrees(math.atan2(mass.ridge_y - mass.eave_y, roof.half))
    if mass.roof == "saddle" and pitch >= float(p["minDeg"]):
        return None
    target = math.radians(rng.uniform(float(p["targetMinDeg"]), float(p["targetMaxDeg"])))
    ridge = mass.eave_y + math.tan(target) * roof.half
    return Mass(mass.footprint, mass.eave_y, ridge, "saddle", mass.ridge_dir or roof.u)


@dataclass(frozen=True)
class WallContext:
    """The city wall as the houses see it (W6, built by ``walls.citywall.wall_context``)."""

    ring: Polygon  # the town inside the wall
    crown: Callable[[float, float], float]  # absolute height of the parapet top near (x, z)
    houses: frozenset[str]  # ids of the houses on the wall line
    merlon: tuple[float, float, float] = (1.5, 0.9, 0.8)  # width, gap, height
    parapet: float = 0.6  # thickness of the screen wall above low eaves


@dataclass
class _PassagePlan:
    """A passage through the current mass: corridor and clear top (absolute y)."""

    axis: LineString
    corridor: Polygon  # the axis buffered by half the width, flat ends, beyond the facades
    w: float
    top: float


LINTEL_M = 0.3  # beam / stone lintel over a passage
PASSAGE_HEAD_M = 0.3  # the passage stays this far below the ground storey's ceiling


@dataclass
class _Context:
    rules: Rules
    rng: random.Random
    front: Any  # FrontFacade or None
    style: HouseStyle
    level: int
    builders: dict[str, _Builder]
    base_y: float
    max_sag: float = 0.0
    roofs: list[_RoofPart] = field(default_factory=list)
    wall: WallContext | None = None
    wall_edges: set[int] = field(default_factory=set)  # outward sides of the current mass
    screens: list[CollisionPart] = field(default_factory=list)
    ground_at: Callable[[float, float], float] | None = None  # terrain (x, z) -> y (E1)
    stair_ground: float | None = None  # terrain in front of the current mass's door if lower
    door_free: Callable[[float, float], bool] | None = None  # no other house there (W6 doors)
    door_reach: Callable[[float, float], bool] | None = None  # a street reachable from there
    streets: Any = None  # StreetIndex: a moved door turns towards the nearest street
    own_masses: list[Polygon] = field(default_factory=list)  # the building's masses (door check)
    door_blocked: bool = False  # the current mass has no free wall: no door geometry
    interior: dict[str, Any] | None = None  # W7: enterable ground storey (uses.json "inside")
    door_op: tuple[Any, Any] | None = None  # the door opening (frame, opening) of the current mass
    room: dict[str, Any] | None = None  # the room built (for the index: door, floor, ceiling)
    doors: list[list[Any]] = field(default_factory=list)  # per mass: x, z, floor, kind, nx, nz
    door_storey: int = 0  # storey of the current mass's door (hillside houses: above ground)
    passages: list[Any] = field(default_factory=list)  # override passages (BuildingOverride)
    passages_now: list[_PassagePlan] = field(default_factory=list)  # in the current mass
    carve: list[tuple[Polygon, float]] = field(default_factory=list)  # for the collision
    floors: list[tuple[Polygon, float]] = field(default_factory=list)  # solid room floors (W7)
    inner_walls: list[tuple[Polygon, float, float]] = field(default_factory=list)  # partitions
    room_storey: bool = False  # building the storey that gets the room: its windows open (W7)
    room_windows: list[tuple[Any, Any]] = field(default_factory=list)  # (frame, opening)
    room_parts: list[tuple[str, Polygon]] = field(default_factory=list)  # the rooms built
    room_levels: dict[str, tuple[float, float]] = field(default_factory=dict)  # y range per room
    upper_h: float | None = None  # height of the storey above the room (stairs, W7)
    stair: Any = None  # stairs.Stair up from the room (W7)
    room_inner: Polygon | None = None  # the room storey's inner outline (the upper room's too)
    upper_windows: list[tuple[Any, Any, float]] = field(default_factory=list)  # + reveal depth
    dirt_m: float = 0.0  # > 0: textured house, plaster walls get a dirty foot band this high
    lod: int = 0  # 1: simplified for the distance (flat openings, flat timber, no dormers)


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


def _room_spec(rules: Rules) -> dict[str, Any]:
    spec = {"wallM": 0.3, "ceilingM": 0.15, "beamM": 0.16, "beamEveryM": 1.0, "minAreaM2": 8.0,
            "minHeightM": 2.4, "stoneFloors": [], "doorsOpen": False, "maxRoomM2": 0.0,
            "maxRoomM2ByUse": {}, "minRoomWidthM": 3.0, "partitionM": 0.15,
            "passage": [0.9, 2.0]}  # fmt: skip
    spec.update(rules.data.get("interior", {}))
    from gothar_worldgen.buildings.stairs import DEFAULTS

    spec["stairs"] = {**DEFAULTS, **spec.get("stairs", {})}
    return spec


def _split_by_room(mesh: MeshData, rooms: Sequence[tuple[str, Polygon]],
                   origin: tuple[float, float, float],
                   levels: dict[str, tuple[float, float]] | None = None,
                   ) -> dict[str, MeshData]:  # fmt: skip
    """The triangles of ``mesh`` (positions relative to ``origin``) by the room nearest to each
    (walls, floors and partitions are already cut where rooms meet); ``levels``: y range per room
    (storeys), a triangle goes to a room of its height."""
    tri = mesh.indices.reshape(-1, 3)
    centre = mesh.positions[tri].mean(axis=1)
    pts = shapely.points(centre[:, 0] + origin[0], centre[:, 2] + origin[2])
    ys = centre[:, 1] + origin[1]
    dist = np.stack([shapely.distance(poly, pts) for _, poly in rooms])
    if levels:
        for k, (name, _) in enumerate(rooms):
            lo, hi = levels.get(name, (-math.inf, math.inf))
            dist[k][(ys < lo) | (ys >= hi)] = np.inf
    which = np.argmin(dist, axis=0)
    out = {}
    for k, (name, _) in enumerate(rooms):
        pick = tri[which == k]
        if not len(pick):
            continue
        used, inverse = np.unique(pick.ravel(), return_inverse=True)
        out[name] = MeshData(mesh.positions[used], mesh.normals[used], mesh.uvs[used],
                             inverse.astype(np.uint32))  # fmt: skip
    return out


def _room_fits(ctx: _Context, ring: Sequence[tuple[float, float]], storey: float) -> bool:
    """``_room`` will build a room in this storey (its checks, ahead of the facades)."""
    spec = _room_spec(ctx.rules)
    inner = Polygon(ring).buffer(-float(spec["wallM"]), join_style="mitre", mitre_limit=3.0)
    high = storey - float(spec["ceilingM"]) >= float(spec["minHeightM"])
    return isinstance(inner, Polygon) and inner.area >= float(spec["minAreaM2"]) and high


def _frame_box(b: _Builder, f: Frame, u0: float, u1: float, v0: float, v1: float, d0: float,
               d1: float, back: bool = True) -> None:  # fmt: skip
    """A box in the facade's frame (u along, v up, d out of the wall); ``back=False`` leaves out
    the face towards the wall (it lies on it)."""
    n = f.n3()
    ax = (float(f.axis[0]), 0.0, float(f.axis[1]))
    faces = [
        ([(u0, v0, d1), (u1, v0, d1), (u1, v1, d1), (u0, v1, d1)], n),
        ([(u0, v0, d0), (u1, v0, d0), (u1, v1, d0), (u0, v1, d0)], (-n[0], -n[1], -n[2])),
        ([(u0, v1, d0), (u1, v1, d0), (u1, v1, d1), (u0, v1, d1)], (0.0, 1.0, 0.0)),
        ([(u0, v0, d0), (u1, v0, d0), (u1, v0, d1), (u0, v0, d1)], (0.0, -1.0, 0.0)),
        ([(u1, v0, d0), (u1, v1, d0), (u1, v1, d1), (u1, v0, d1)], ax),
        ([(u0, v0, d0), (u0, v1, d0), (u0, v1, d1), (u0, v0, d1)], (-ax[0], 0.0, -ax[2])),
    ]
    for k, (corners, want) in enumerate(faces):
        if k == 1 and not back:
            continue
        b.polygon([f.point(u, v, d) for u, v, d in corners], [(u, v) for u, v, _ in corners],
                  want)  # fmt: skip


WINDOW_BAR_M = 0.05  # the window cross: oak bars this wide
OPEN_EVERY = 2  # in the room storey every second window is open (W7)


def _window_cross(b: _Builder, f: Frame, op: Any, depth: float) -> None:  # noqa: ANN401
    """A wooden cross in an open window, in the middle of the wall's depth."""
    h, um, vm = WINDOW_BAR_M / 2, op.u + op.w / 2, op.v + op.h * 0.6
    _frame_box(b, f, um - h, um + h, op.v, op.v + op.h, -depth - h, -depth + h)
    _frame_box(b, f, op.u, op.u + op.w, vm - h, vm + h, -depth - h, -depth + h)


SHUTTER_M = 0.03  # thickness of the open shutters


def _shutters(b: _Builder, f: Frame, op: Any) -> None:  # noqa: ANN401
    """Two shutters, open: folded back flat against the facade beside the window."""
    half = op.w / 2
    for u0, u1 in (
        (op.u - half - 0.02, op.u - 0.02),
        (op.u + op.w + 0.02, op.u + op.w + half + 0.02),
    ):
        _frame_box(b, f, u0, u1, op.v, op.v + op.h, 0.02, 0.02 + SHUTTER_M, back=False)


def _room(ctx: _Context, ring: Sequence[tuple[float, float]], floor: float, storey: float,
          notes: list[str]) -> None:  # fmt: skip
    """The ground storey as one room (W7 enterable houses): inner walls with the door hole,
    floor, ceiling with beams; the collision gets the room carved out (``ctx.carve``)."""
    spec = _room_spec(ctx.rules)
    wall, thick = float(spec["wallM"]), float(spec["ceilingM"])
    ceiling = floor + storey - thick
    inner = Polygon(ring).buffer(-wall, join_style="mitre", mitre_limit=3.0)
    if (
        not isinstance(inner, Polygon)
        or inner.area < float(spec["minAreaM2"])
        or (ceiling - floor < float(spec["minHeightM"]))
    ):
        notes.append("room skipped (too small or too low)")
        return
    inner = shapely.orient_polygons(inner)  # counter-clockwise (x, z)
    f, op = ctx.door_op  # type: ignore[misc]
    door_lo, door_hi = floor + op.v, floor + op.v + op.h
    hinge_side = (f.point(op.u, op.v, -wall), f.point(op.u + op.w, op.v, -wall))
    d0 = (hinge_side[0][0], hinge_side[0][2])
    d1 = (hinge_side[1][0], hinge_side[1][2])
    # big ground storeys are divided: the room with the door (its hearth, table) and chambers
    use = str((ctx.interior or {}).get("use", ""))
    rooms, cuts = _partition_plan(inner, ((d0[0] + d1[0]) / 2, (d0[1] + d1[1]) / 2), spec, use)
    ctx.room_parts = rooms  # (W7) each room becomes its own mesh vob (its own lights)
    holes = [(d0, d1, floor, door_hi)]  # the door, then the windows of this storey
    for fw, ow in ctx.room_windows:
        w0, w1 = fw.point(ow.u, ow.v, -wall), fw.point(ow.u + ow.w, ow.v, -wall)
        holes.append(((w0[0], w0[2]), (w1[0], w1[2]), float(w0[1]), float(w0[1]) + ow.h))
    _room_walls(ctx, inner, floor, ceiling, holes, cuts)
    ring_in = list(inner.exterior.coords)[:-1]
    # W7: stairs to the storey above (if it is high enough for a room)
    stair_cut = None
    if ctx.upper_h is None:
        notes.append("no stairs (a single storey)")
    elif ctx.upper_h - thick < float(spec["stairs"]["upperMinHeightM"]):  # upstairs is low
        notes.append("no stairs (the storey above is too low for a room)")
    else:
        from gothar_worldgen.buildings.stairs import plan_stair

        door_way = _door_corridor(f, op, wall).buffer(float(spec["stairs"]["doorClearM"]))
        door_mid0 = ((d0[0] + d1[0]) / 2, (d0[1] + d1[1]) / 2)
        clear = [door_way, *(Point(c["mid"]).buffer(float(spec["stairs"]["passageClearM"]))
                             for c in cuts)]  # fmt: skip
        if ctx.wall is not None and ctx.wall.ring.exterior.distance(Point(door_mid0)) < 40.0:
            clear.append(ctx.wall.ring.exterior.buffer(1.6))  # the town wall reaches into rooms
        win_lines = [(LineString([(w0[0], w0[1]), (w1[0], w1[1])]), y0)
                     for w0, w1, y0, _ in holes[1:]]  # fmt: skip
        door_mid = ((d0[0] + d1[0]) / 2, (d0[1] + d1[1]) / 2)
        # a chamber first (with the beds upstairs it keeps only stores), then the room with the
        # door (its hearth, tables, counter need the room); clear of the windows, then passing
        # them (stairs often run past a window)
        order = [poly for _, poly in rooms[1:]] + [rooms[0][1]]
        tries = [(poly, win_lines) for poly in order] + [(poly, []) for poly in order]
        for where, wins in tries:
            ctx.stair = plan_stair(where, inner, floor, floor + storey, clear, wins, door_mid,
                                   spec["stairs"])  # fmt: skip
            if ctx.stair is not None:
                break
        if ctx.stair is None:
            notes.append("no stairs (no wall for them in the room with the door)")
        else:
            stair_cut = ctx.stair.opening(
                storey - (ceiling - floor), float(spec["stairs"]["headroomM"])
            )
    _room_slabs(ctx, inner, rooms, floor, None, None, spec)
    _room_slabs(ctx, inner, rooms, None, ceiling, stair_cut, spec)
    _room_beams(ctx, inner, ceiling, spec, stair_cut)
    if ctx.stair is not None:
        from gothar_worldgen.buildings.stairs import stair_mesh

        stair_mesh(ctx.builders["room_beam"], ctx.stair, float(spec["stairs"]["railM"]))
    for cut in cuts:
        _partition(ctx, cut, floor, ceiling, spec)
    # the collision: walls beside the room and the door, a solid floor, the body above
    corridor = inner.union(_door_corridor(f, op, wall))
    ctx.carve.append((corridor, ceiling))
    if floor - ctx.base_y > 0.05:
        ctx.floors.append((inner, floor))
    nx, nz = float(f.nx), float(f.nz)
    ax, az = float(f.ax), float(f.az)
    ctx.room_inner = inner
    for name, _ in rooms:
        ctx.room_levels[name] = (-math.inf, floor + storey - 0.02)
    ctx.room = {
        "floor": round(floor, 3),
        "ceiling": round(ceiling, 3),
        "ring": [[round(x, 3), round(z, 3)] for x, z in ring_in],
        "door": {"from": [round(d0[0], 3), round(d0[1], 3)],
                 "to": [round(d1[0], 3), round(d1[1], 3)],
                 "axis": [round(ax, 4), round(az, 4)], "normal": [round(nx, 4), round(nz, 4)],
                 "floor": round(door_lo, 3), "w": round(op.w, 3), "h": round(op.h, 3)},
    }  # fmt: skip
    windows = []
    for fw, ow in ctx.room_windows:  # (W7) on the inner face: for the furnishing and the light
        w0, w1 = fw.point(ow.u, ow.v, -wall), fw.point(ow.u + ow.w, ow.v, -wall)
        windows.append({"from": [round(w0[0], 3), round(w0[2], 3)],
                        "to": [round(w1[0], 3), round(w1[2], 3)],
                        "sill": round(float(w0[1]), 3), "top": round(float(w0[1]) + ow.h, 3),
                        "normal": [round(float(fw.nx), 4), round(float(fw.nz), 4)]})  # fmt: skip
    if windows:
        ctx.room["windows"] = windows
    ctx.room_storey = False
    if len(rooms) > 1:  # (W7 rooms) the first is the one with the door
        ctx.room["rooms"] = [{"name": name, "ring": [[round(x, 3), round(z, 3)] for x, z in
                                                     list(poly.exterior.coords)[:-1]]}
                             for name, poly in rooms]  # fmt: skip
        ctx.room["passages"] = [{"rooms": list(c["rooms"]),
                                 "mid": [round(c["mid"][0], 3), round(c["mid"][1], 3)],
                                 "axis": [round(c["u"][0], 4), round(c["u"][1], 4)],
                                 "w": c["w"], "h": c["h"]} for c in cuts]  # fmt: skip


def _room_walls(ctx: _Context, inner: Polygon, floor: float, ceiling: float,
                holes: Sequence[tuple[tuple[float, float], tuple[float, float], float, float]],
                cuts: Sequence[dict[str, Any]]) -> None:  # fmt: skip
    """The inner faces of a room storey's outer walls with the door and window holes, split where
    partitions meet them (one piece per room)."""
    b = ctx.builders
    ring_in = list(inner.exterior.coords)[:-1]
    for k, a in enumerate(ring_in):
        c = ring_in[(k + 1) % len(ring_in)]
        ex, ez = c[0] - a[0], c[1] - a[1]
        length = math.hypot(ex, ez)
        if length < 1e-3:
            continue
        ux, uz = ex / length, ez / length
        want = (-uz, 0.0, ux)  # inward for a counter-clockwise ring (x, z)

        def along(q: tuple[float, float], ax: float = a[0], az: float = a[1], dx: float = ux,
                  dz: float = uz) -> float:  # fmt: skip
            return (q[0] - ax) * dx + (q[1] - az) * dz

        def off(q: tuple[float, float], ax: float = a[0], az: float = a[1], dx: float = ux,
                dz: float = uz) -> float:  # fmt: skip
            return abs((q[0] - ax) * dz - (q[1] - az) * dx)

        face = shapely.box(0.0, floor, length, ceiling)
        for h0, h1, y0, y1 in holes:
            if off(h0) < 0.05 and off(h1) < 0.05:  # on this wall
                t0, t1 = sorted((along(h0), along(h1)))
                if t1 > 0.0 and t0 < length:
                    face = face.difference(shapely.box(max(0.0, t0), y0, min(length, t1), y1))
        marks = [along(q) for c in cuts for q in (c["a"], c["b"]) if off(q) < 0.05]
        edges = sorted({0.0, length, *(t for t in marks if 0.0 < t < length)})
        for lo, hi in zip(edges, edges[1:], strict=False):  # one piece per room it bounds
            piece = face.intersection(shapely.box(lo, floor - 1.0, hi, ceiling + 1.0))
            for g in getattr(piece, "geoms", [piece]):
                if not isinstance(g, Polygon) or g.area < 1e-4:
                    continue
                for tri in shapely.constrained_delaunay_triangles(g).geoms:
                    pts = list(tri.exterior.coords)[:3]
                    b["room_wall"].polygon([(a[0] + ux * t, y, a[1] + uz * t) for t, y in pts],
                                           [(t, y) for t, y in pts], want)  # fmt: skip


def _room_slabs(ctx: _Context, inner: Polygon, rooms: Sequence[tuple[str, Polygon]],
                floor: float | None, ceiling: float | None, hole: Polygon | None,
                spec: dict[str, Any], boards: bool = False) -> None:  # fmt: skip
    """Floor and ceiling per room up to the middle of the partitions, without ``hole`` (the stair
    opening); ``None`` leaves that slab out. ``boards``: the floor of boards like the ceilings
    (upstairs: a stone floor below stays below)."""
    b = ctx.builders
    half = float(spec["partitionM"]) / 2
    floor_role = "room_ceiling" if boards else "room_floor"
    for _, part in rooms:
        whole = part if len(rooms) == 1 else inner.intersection(
            part.buffer(half, join_style="mitre", mitre_limit=3.0))  # fmt: skip
        for y, role, up in ((floor, floor_role, 1.0), (ceiling, "room_ceiling", -1.0)):
            if y is None:
                continue
            area = whole if hole is None else whole.difference(hole)
            for g in getattr(area, "geoms", [area]):
                if not isinstance(g, Polygon) or g.area < 1e-4:
                    continue
                for tri in shapely.constrained_delaunay_triangles(g).geoms:
                    pts = list(tri.exterior.coords)[:3]
                    b[role].polygon([(x, y, z) for x, z in pts], [(x, z) for x, z in pts],
                                    (0.0, up, 0.0))  # fmt: skip


def _room_beams(ctx: _Context, inner: Polygon, ceiling: float, spec: dict[str, Any],
                hole: Polygon | None = None) -> None:  # fmt: skip
    """Beams under the ceiling across the shorter side of the room (not over the stair hole)."""
    b = ctx.builders
    free = inner if hole is None else inner.difference(hole.buffer(0.1))
    rect = inner.minimum_rotated_rectangle
    rc = list(rect.exterior.coords)[:4]
    e1 = (rc[1][0] - rc[0][0], rc[1][1] - rc[0][1])
    e2 = (rc[2][0] - rc[1][0], rc[2][1] - rc[1][1])
    span, run = (e1, e2) if math.hypot(*e1) < math.hypot(*e2) else (e2, e1)
    run_len = math.hypot(*run)
    bw = float(spec["beamM"])
    n_beams = int(run_len // float(spec["beamEveryM"]))
    start = rc[0] if run is e1 else rc[1]
    for j in range(1, n_beams):
        t = j / n_beams
        o = (start[0] + run[0] * t, start[1] + run[1] * t)
        line = LineString([(o[0] - span[0] * 3, o[1] - span[1] * 3),
                           (o[0] + span[0] * 3, o[1] + span[1] * 3)])  # fmt: skip
        seg = line.intersection(free)
        for g in getattr(seg, "geoms", [seg]):
            if not isinstance(g, LineString) or g.length < 0.5:
                continue
            (x0, z0), (x1, z1) = g.coords[0], g.coords[-1]
            _room_beam(b["room_beam"], (x0, z0), (x1, z1), ceiling, bw)


def _slab_pieces(inner: Polygon, hole: Polygon) -> list[Polygon]:
    """The slab between the storeys without the stair opening, in convex pieces: first cut along
    the opening's four edges (a big nearly convex piece with the notch would pass as convex and
    its hull close the opening), then decomposed."""
    from gothar_worldgen.buildings.collision import convex_pieces

    pieces = [inner]
    ring = list(hole.exterior.coords)
    for (xa, za), (xb, zb) in zip(ring, ring[1:], strict=False):
        dx, dz = xb - xa, zb - za
        line = LineString([(xa - dx * 100, za - dz * 100), (xb + dx * 100, zb + dz * 100)])
        nxt = []
        for g in pieces:
            parts = split(g, line)
            nxt += [q for q in parts.geoms if isinstance(q, Polygon)]
        pieces = nxt
    out = []
    for g in pieces:
        if g.representative_point().within(hole) or g.area < 0.01:
            continue
        out += convex_pieces(g, slivers=False)
    return out


def _upper_room(ctx: _Context, floor: float, storey: float, notes: list[str]) -> None:
    """The storey above the room (W7): reached by the stairs, its own rooms (``OBEN``,
    ``OBEN_KAMMER`` …) with open windows, the rail round the stair opening; the collision gets
    the slab between the storeys and this storey carved out too."""
    from gothar_worldgen.buildings.stairs import opening_faces, opening_rail, ramp_body

    spec = _room_spec(ctx.rules)
    st = ctx.stair
    room = ctx.room
    inner = ctx.room_inner
    assert st is not None and room is not None and inner is not None
    thick = float(spec["ceilingM"])
    ceiling = floor + storey - thick
    below = float(room["ceiling"])
    hole = st.opening(floor - below, float(spec["stairs"]["headroomM"]))
    # the room upstairs lies round the opening and the head of the stairs (no partition there)
    centre = hole.union(st.head_landing).centroid
    plan, cuts = _partition_plan(inner, (centre.x, centre.y), spec, "oben")
    stairs_zone = hole.union(st.head_landing).buffer(0.3)
    if any(LineString([c["a"], c["b"]]).intersects(stairs_zone) for c in cuts):
        plan, cuts = [("INNEN", inner)], []  # a partition would cross the stairs: one room
        notes.append("upper storey not divided (the stair opening is in the way)")

    def upstairs(name: str) -> str:  # INNEN -> OBEN, KAMMER_2 -> OBEN_KAMMER_2
        return "OBEN" if name == "INNEN" else f"OBEN_{name}"

    rooms = [(upstairs(name), poly) for name, poly in plan]
    for c in cuts:
        c["rooms"] = tuple(upstairs(r) for r in c["rooms"])
    wall = float(spec["wallM"])
    holes = []
    for fw, ow, depth in ctx.upper_windows:
        w0, w1 = fw.point(ow.u, ow.v, -depth), fw.point(ow.u + ow.w, ow.v, -depth)
        holes.append(((w0[0], w0[2]), (w1[0], w1[2]), float(w0[1]), float(w0[1]) + ow.h))
    _room_walls(ctx, inner, floor, ceiling, holes, cuts)
    _room_slabs(ctx, inner, rooms, floor, ceiling, hole, spec, boards=True)
    if ceiling - floor >= float(spec["stairs"]["beamsFromM"]):  # low upstairs: no beams to duck
        _room_beams(ctx, inner, ceiling, spec)
    for cut in cuts:
        _partition(ctx, cut, floor, ceiling, spec)
    # the slab's height at the walls: the walls run on through it (the facade's inner side is not
    # drawn: through the opening one would look outside); the opening's cut edges in the room
    _room_walls(ctx, inner, below, floor, [], [])
    opening_faces(ctx.builders["room_ceiling"], hole, below, floor, inner)
    rail = float(spec["stairs"]["railM"])
    for foot in opening_rail(ctx.builders["room_beam"], st, hole, rail):
        ctx.inner_walls.append((foot, floor, floor + rail))
    # the collision: this storey carved out as well (the body above starts at its ceiling), the
    # slab between the storeys without the opening, the wall above the house door closed, the
    # stairs as one smooth ramp
    ctx.carve = [(cor, ceiling if abs(top - below) < 2e-3 else top) for cor, top in ctx.carve]
    for piece in _slab_pieces(inner, hole):
        ctx.inner_walls.append((piece, below, floor))
    f, op = ctx.door_op  # type: ignore[misc]
    above_door = _door_corridor(f, op, wall).difference(inner)
    for g in getattr(above_door, "geoms", [above_door]):
        if isinstance(g, Polygon) and g.area > 0.01:
            ctx.inner_walls.append((g, float(room["door"]["floor"]) + op.h, ceiling))
    wood = ctx.builders["room_beam"]
    ctx.screens.append(ramp_body(st, (wood.ox, wood.oy, wood.oz), "COL_HULL_RAMP"))
    ctx.room_parts += rooms
    for name, _ in rooms:
        ctx.room_levels[name] = (floor - 0.02, math.inf)
    windows = []
    for fw, ow, depth in ctx.upper_windows:
        w0, w1 = fw.point(ow.u, ow.v, -depth), fw.point(ow.u + ow.w, ow.v, -depth)
        windows.append({"from": [round(w0[0], 3), round(w0[2], 3)],
                        "to": [round(w1[0], 3), round(w1[2], 3)],
                        "sill": round(float(w0[1]), 3), "top": round(float(w0[1]) + ow.h, 3),
                        "normal": [round(float(fw.nx), 4), round(float(fw.nz), 4)]})  # fmt: skip
    room["stairs"] = st.json(floor - below, float(spec["stairs"]["headroomM"]))
    room["upper"] = {
        "floor": round(floor, 3), "ceiling": round(ceiling, 3), "windows": windows,
        "rooms": [{"name": name, "ring": [[round(x, 3), round(z, 3)]
                                          for x, z in list(poly.exterior.coords)[:-1]]}
                  for name, poly in rooms],
        "passages": [{"rooms": list(c["rooms"]), "mid": [round(c["mid"][0], 3),
                                                         round(c["mid"][1], 3)],
                      "axis": [round(c["u"][0], 4), round(c["u"][1], 4)], "w": c["w"],
                      "h": c["h"]} for c in cuts],
    }  # fmt: skip


_Rooms = tuple[list[tuple[str, Polygon]], list[dict[str, Any]]]


def _partition_plan(inner: Polygon, door: tuple[float, float], spec: dict[str, Any],
                    use: str) -> _Rooms:  # fmt: skip
    """Rooms of a ground storey (W7): above ``maxRoomM2`` (per use ``maxRoomM2ByUse`` for the
    room with the door) it is cut across its long axis. The room with the door (``INNEN``) lies
    around the door; the parts beside it become chambers (``KAMMER``, ``KAMMER_2`` …) of equal
    width, none narrower than ``minRoomWidthM``. Returns (name, polygon) per room and the cuts:
    position along the axis, the two rooms, the passage's middle, axis, width, height."""
    first_max = float(spec.get("maxRoomM2ByUse", {}).get(use, spec["maxRoomM2"]))
    other_max = float(spec["maxRoomM2"])
    if other_max <= 0 or inner.area <= first_max:
        return [("INNEN", inner)], []
    rect = list(inner.minimum_rotated_rectangle.exterior.coords)[:4]
    e1 = (rect[1][0] - rect[0][0], rect[1][1] - rect[0][1])
    e2 = (rect[2][0] - rect[1][0], rect[2][1] - rect[1][1])
    long = e1 if math.hypot(*e1) >= math.hypot(*e2) else e2
    ln = math.hypot(*long)
    ux, uz = long[0] / ln, long[1] / ln
    ts = [x * ux + z * uz for x, z in inner.exterior.coords]
    t0, t1 = min(ts), max(ts)
    length = t1 - t0
    depth = inner.area / length
    min_w = float(spec["minRoomWidthM"])
    td = door[0] * ux + door[1] * uz
    if length - min_w < min_w:  # no room for a chamber beside it
        return [("INNEN", inner)], []
    w_first = min(max(min_w, first_max / depth), length - min_w)
    # the room with the door around the door; where that leaves a strip too narrow for a chamber
    # it moves to the end of the storey (still holding the door), so one side is a chamber
    a = min(max(td - w_first / 2, t0), t1 - w_first)
    if 0 < a - t0 < min_w or 0 < t1 - (a + w_first) < min_w:
        ends = [x for x in (t0, t1 - w_first) if x <= td <= x + w_first]
        if ends:
            a = min(ends, key=lambda x: abs(td - (x + w_first / 2)))
    b = a + w_first
    if a - t0 < min_w:  # too narrow for a chamber: the room with the door takes it
        a = t0
    if t1 - b < min_w:
        b = t1
    rooms_t: list[tuple[float, float]] = [(a, b)]
    for lo, hi in ((t0, a), (b, t1)):
        if hi - lo < min_w:
            continue
        n = max(1, min(math.ceil((hi - lo) * depth / other_max), int((hi - lo) // min_w)))
        for k in range(n):
            rooms_t.append((lo + (hi - lo) * k / n, lo + (hi - lo) * (k + 1) / n))
    if len(rooms_t) == 1:
        return [("INNEN", inner)], []
    # order: the room with the door, then outwards on each side
    first = rooms_t[0]
    others = sorted(rooms_t[1:], key=lambda r: min(abs(r[0] - first[1]), abs(r[1] - first[0])))
    names = ["INNEN"] + ["KAMMER" if k == 0 else f"KAMMER_{k + 1}" for k in range(len(others))]
    ordered = [first, *others]
    half = float(spec["partitionM"]) / 2
    vx, vz = -uz, ux
    big = 1e3

    def strip(lo: float, hi: float) -> Polygon:
        return Polygon([(ux * lo + vx * -big, uz * lo + vz * -big),
                        (ux * hi + vx * -big, uz * hi + vz * -big),
                        (ux * hi + vx * big, uz * hi + vz * big),
                        (ux * lo + vx * big, uz * lo + vz * big)])  # fmt: skip

    rooms: list[tuple[str, Polygon]] = []
    for name, (lo, hi) in zip(names, ordered, strict=True):
        piece = inner.intersection(strip(lo + (half if lo > t0 + 1e-6 else 0.0),
                                         hi - (half if hi < t1 - 1e-6 else 0.0)))  # fmt: skip
        parts = [g for g in getattr(piece, "geoms", [piece]) if isinstance(g, Polygon)]
        rooms.append((name, shapely.orient_polygons(max(parts, key=lambda g: g.area))))
    pw, ph = (float(x) for x in spec["passage"])
    cuts = []
    for i, (_, hi_i) in enumerate(ordered):
        for j, (lo_j, _) in enumerate(ordered):
            if i != j and abs(hi_i - lo_j) < 1e-6:  # i on the low side of the cut at hi_i
                t = hi_i
                line = LineString([(ux * t - vx * big, uz * t - vz * big),
                                   (ux * t + vx * big, uz * t + vz * big)])  # fmt: skip
                chord = inner.intersection(line)
                segs = [g for g in getattr(chord, "geoms", [chord]) if isinstance(g, LineString)]
                seg = max(segs, key=lambda g: g.length)
                (ax, az), (bx, bz) = seg.coords[0], seg.coords[-1]
                cuts.append({"t": t, "rooms": (names[i], names[j]), "a": (ax, az), "b": (bx, bz),
                             "mid": ((ax + bx) / 2, (az + bz) / 2), "u": (ux, uz),
                             "w": min(pw, seg.length - 0.6), "h": ph})  # fmt: skip
    return rooms, cuts


def _partition(ctx: _Context, cut: dict[str, Any], floor: float, ceiling: float,
               spec: dict[str, Any]) -> None:  # fmt: skip
    """A partition wall across the room with an open passage in its middle: plastered on both
    sides, the passage's jambs and head, and its collision (``ctx.inner_walls``)."""
    b = ctx.builders["room_wall"]
    half = float(spec["partitionM"]) / 2
    ux, uz = cut["u"]
    (ax, az), (bx, bz) = cut["a"], cut["b"]
    length = math.dist((ax, az), (bx, bz))
    sx, sz = (bx - ax) / length, (bz - az) / length  # along the wall
    pw, ph = cut["w"], cut["h"]
    s0, s1 = (length - pw) / 2, (length + pw) / 2
    top = min(floor + ph, ceiling - 0.05)

    def at(s: float, y: float, side: float) -> tuple[float, float, float]:
        return (ax + sx * s + ux * half * side, y, az + sz * s + uz * half * side)

    face = shapely.box(0.0, floor, length, ceiling).difference(shapely.box(s0, floor, s1, top))
    for side in (-1.0, 1.0):
        for tri in shapely.constrained_delaunay_triangles(face).geoms:
            pts = list(tri.exterior.coords)[:3]
            b.polygon([at(su, y, side) for su, y in pts], [(su, y) for su, y in pts],
                      (ux * side, 0.0, uz * side))  # fmt: skip
    for s_, n in ((s0, 1.0), (s1, -1.0)):  # jambs, facing into the passage
        b.polygon([at(s_, floor, -1), at(s_, floor, 1), at(s_, top, 1), at(s_, top, -1)],
                  [(0.0, floor), (2 * half, floor), (2 * half, top), (0.0, top)],
                  (sx * n, 0.0, sz * n))  # fmt: skip
    b.polygon([at(s0, top, -1), at(s1, top, -1), at(s1, top, 1), at(s0, top, 1)],
              [(s0, 0.0), (s1, 0.0), (s1, 2 * half), (s0, 2 * half)], (0.0, -1.0, 0.0))  # fmt: skip

    def foot(sa: float, sb: float) -> Polygon:
        pts = [at(sa, 0, -1), at(sb, 0, -1), at(sb, 0, 1), at(sa, 0, 1)]
        return Polygon([(x, z) for x, _, z in pts])

    # the collision: the wall beside the passage; open to the ceiling above it, like the house
    # door (the engine probes the ground of a way from above)
    ctx.inner_walls.append((foot(0.0, s0), floor, ceiling))
    ctx.inner_walls.append((foot(s1, length), floor, ceiling))


def _door_corridor(f: Frame, op: Any, wall: float) -> Polygon:  # noqa: ANN401
    """The door opening through the wall (x, z), a little beyond both faces."""
    pts = [f.point(op.u, op.v, 0.3), f.point(op.u + op.w, op.v, 0.3),
           f.point(op.u + op.w, op.v, -wall - 0.3), f.point(op.u, op.v, -wall - 0.3)]  # fmt: skip
    return Polygon([(x, z) for x, _, z in pts])


def _room_beam(b: _Builder, p0: tuple[float, float], p1: tuple[float, float], top: float,
               width: float) -> None:  # fmt: skip
    """A beam under the ceiling from p0 to p1: two sides and the bottom (the top is hidden)."""
    dx, dz = p1[0] - p0[0], p1[1] - p0[1]
    length = math.hypot(dx, dz) or 1.0
    sx, sz = -dz / length * width / 2, dx / length * width / 2
    lo = top - width
    for side, want in ((1.0, (sx, 0.0, sz)), (-1.0, (-sx, 0.0, -sz))):
        q0 = (p0[0] + sx * side, p0[1] + sz * side)
        q1 = (p1[0] + sx * side, p1[1] + sz * side)
        side_quad = [(q0[0], lo, q0[1]), (q1[0], lo, q1[1]), (q1[0], top, q1[1]),
                     (q0[0], top, q0[1])]  # fmt: skip
        b.polygon(side_quad, [(0, 0), (length, 0), (length, width), (0, width)], want)
    b.polygon([(p0[0] - sx, lo, p0[1] - sz), (p1[0] - sx, lo, p1[1] - sz),
               (p1[0] + sx, lo, p1[1] + sz), (p0[0] + sx, lo, p0[1] + sz)],
              [(0, 0), (length, 0), (length, width), (0, width)], (0.0, -1.0, 0.0))  # fmt: skip


def _facade(ctx: _Context, f: Frame, edge: int, s: int, heights: Sequence[float],
            outline: Sequence[tuple[float, float]], usable: float, door: bool) -> None:  # fmt: skip
    rules, st = ctx.rules, ctx.style
    if edge >= 0 and edge in ctx.wall_edges:
        _wall_side(ctx, f, s, outline, usable)
        return
    plan: list[Opening] = []
    if edge >= 0 and st.openings:
        if ctx.front is not None and ctx.front.edge == edge:
            plan = override_openings(ctx.front, heights).get(s, [])
        else:
            room = [*heights[:-1], usable] if s == len(heights) - 1 else list(heights)
            plan = procedural_openings(f.width, heights, rules, ctx.rng, door, room, st.gate).get(
                s, []
            )
            plan = _age_windows(plan, rules, ctx.rng, st.age)
    plan = [op for op in plan
            if op.u >= 0 and op.u + op.w <= f.width + 1e-6
            and op.v + op.h <= usable + 1e-6]  # fmt: skip
    if door and s > 0 and st.openings:  # hillside house: the door in an upper storey (E1-C)
        plan = _with_door(plan, f.width, rules)
    if s <= ctx.door_storey and ctx.ground_at is not None:
        plan = _above_ground(ctx, f, plan)
        if door and ctx.stair_ground is not None:
            for op in plan:
                if op.kind in ("door", "gate"):
                    _door_stairs(ctx, f, op, ctx.stair_ground)
    lintels = []
    if s == 0 and ctx.passages_now:
        plan, lintels = _with_passages(ctx, f, plan)
    timbered = st.timber and ctx.level < 3 and edge >= 0
    massive = not st.timber or (s == 0 and st.massive_ground)
    poly = Polygon(outline)
    socle = 0.0
    if massive:
        _wall_banded(ctx, "wall_ground", f, poly, plan, s)
    elif s == 0:
        # Timber on a stone socle: lower band stone, the rest infill.
        socle = float(rules.get("socleM"))
        low, high = (
            poly.intersection(shapely.box(-1, -1e3, f.width + 1, socle)),
            poly.difference(shapely.box(-1, -1e3, f.width + 1, socle)),
        )
        for part, role in ((low, "wall_ground"), (high, "infill")):
            for g in getattr(part, "geoms", [part]):
                if isinstance(g, Polygon):
                    _wall_banded(ctx, role, f, g, plan, s)
    else:
        _wall_banded(ctx, "infill", f, poly, plan, s)
    # (W7) of the room storey's windows every second one is open (the first from the left): the
    # walls between keep room for shelves and counters; the others stay closed
    windows = sorted((op for op in plan if op.kind == "window"), key=lambda op: op.u)
    open_windows = windows[::OPEN_EVERY] if ctx.room_storey else []
    for op in plan:
        if op.kind == "passage":  # open: the tunnel walls are its jambs
            continue
        if door and ctx.interior is not None and not ctx.lod and op.kind in ("door", "gate"):
            ctx.door_op = (f, op)  # through the wall into the room (``_room``)
            _reveal(ctx.builders["frame"], f, op, _room_spec(rules)["wallM"], panel=False)
        elif ctx.room_storey and op in open_windows:  # W7: daylight into the room
            wall = float(_room_spec(rules)["wallM"])
            depth = wall
            if s > 0 and ctx.room_inner is not None:  # upstairs: through to the room's walls
                mid = f.point(op.u + op.w / 2, op.v + op.h / 2)
                depth = max(wall, float(ctx.room_inner.exterior.distance(Point(mid[0], mid[2]))))
            _reveal(ctx.builders["frame"], f, op, wall, panel=False)
            if depth > wall + 0.01:  # upstairs over a jetty: the rest of the niche plastered
                _reveal(ctx.builders["room_wall"], f, op, depth, panel=False, start=wall)
            _window_cross(ctx.builders["room_beam"], f, op, wall / 2)  # with the room's wood:
            _shutters(ctx.builders["room_beam"], f, op)  # outside the house budget
            if s == 0:
                ctx.room_windows.append((f, op))
            else:
                ctx.upper_windows.append((f, op, depth))
        elif ctx.lod:
            _flat_opening(ctx.builders["frame"], f, op)
        else:
            _reveal(ctx.builders["frame"], f, op, float(rules.get("openings", "revealM")))
    role = "wall_ground" if massive else "timber"  # lintel in the house's style
    for u0, u1, v in lintels:
        mid = v + LINTEL_M / 2
        depth = float(rules.get("timber", "depthM"))
        _beam(ctx.builders[role], f, np.array([u0, mid]), np.array([u1, mid]), LINTEL_M, depth)
    if massive or not timbered:
        return
    beam, depth = float(rules.get("timber", "beamM")), float(rules.get("timber", "depthM"))
    pattern = rules.pattern(st.pattern) if ctx.level == 0 else []
    brustung = rules.pattern(st.brustung) if ctx.level == 0 and st.brustung else []
    shifted = [Opening(o.kind, o.u, o.v - socle, o.w, o.h) for o in plan]
    plain = rules.pattern(rules.get("timber", "pattern")) if ctx.level == 0 else []
    every = int(rules.data["timber"].get("figureEvery", 0))
    segs = timber_segments(f.width, usable - socle, shifted, rules, pattern, bays=ctx.level < 2,
                           brustung=brustung, plain=plain, figure_every=every)  # fmt: skip
    g = f.raised(socle) if socle else f
    segs = _lean_posts(segs, f.width, shifted, rules, ctx.rng, st)
    for kind, k, p0, p1 in segs:
        factor = DEPTH_FACTOR[kind] - (BRACE_STAGGER * k if kind == "brace" else 0.0)
        _beam(
            ctx.builders["timber"],
            g,
            p0,
            p1,
            beam,
            depth * max(factor, 0.2),
            sides=kind not in BOARD_KINDS and not ctx.lod,
        )


def _plan_passages(ctx: _Context, ring: Sequence[tuple[float, float]], ground: float,
                   heights: Sequence[float], notes: list[str]) -> list[_PassagePlan]:  # fmt: skip
    """Passages of the override that cross this mass: corridor and clear top."""
    out = []
    poly = Polygon(ring)
    for ps in ctx.passages:
        axis = LineString(ps.axis)
        inside = axis.intersection(poly)
        if inside.is_empty or inside.length < 0.5:
            continue
        ends = [Point(q) for g in getattr(inside, "geoms", [inside]) for q in g.coords]
        terrain = [ctx.ground_at(e.x, e.y) if ctx.ground_at else ground for e in ends]
        top = max([ground, *terrain]) + float(ps.h)
        limit = ground + heights[0] - PASSAGE_HEAD_M - LINTEL_M
        if top > limit:
            notes.append(f"passage lowered to {limit - max([ground, *terrain]):.2f} m clear height")
            top = limit
        corridor = axis.buffer(float(ps.w) / 2, cap_style="flat")
        out.append(_PassagePlan(axis, corridor, float(ps.w), top))
        ctx.carve.append((corridor, top))
        # a door that the passage takes over is no door any more
        ctx.doors[:] = [
            d for d in ctx.doors if not corridor.buffer(0.5).contains(Point(d[0], d[1]))
        ]
    return out


def _with_passages(ctx: _Context, f: Frame, plan: list[Opening]
                   ) -> tuple[list[Opening], list[tuple[float, float, float]]]:  # fmt: skip
    """``plan`` with an opening where a passage crosses this facade (other openings there go);
    returns the lintels (u0, u1, v) too."""
    a = (f.lx, f.lz)
    b = (f.lx + f.ax * f.width, f.lz + f.az * f.width)
    edge = LineString([a, b])
    lintels = []
    for pp in ctx.passages_now:
        hit = edge.intersection(pp.axis)
        if hit.is_empty or hit.geom_type != "Point":
            continue
        u = math.dist(a, (hit.x, hit.y))
        d = np.asarray(pp.axis.coords[-1]) - np.asarray(pp.axis.coords[0])
        d = d / np.linalg.norm(d)
        sin = abs(f.ax * d[1] - f.az * d[0])  # the corridor meets the facade at an angle
        wd = pp.w / max(sin, 0.3)
        u0, u1 = max(u - wd / 2, 0.05), min(u + wd / 2, f.width - 0.05)
        v = pp.top - f.y0
        plan = [op for op in plan if op.u + op.w < u0 - 0.3 or op.u > u1 + 0.3]
        plan.append(Opening("passage", u0, -100.0, u1 - u0, v + 100.0))
        lintels.append((u0 - 0.2, u1 + 0.2, v))
    return plan, lintels


def _passage_tunnel(ctx: _Context, pp: _PassagePlan, ground_poly: Polygon) -> None:
    """Side walls and ceiling of a passage inside the ground storey."""
    role = "wall_ground"
    ceiling = "timber" if ctx.style.timber else "wall_ground"
    inner = pp.corridor.intersection(ground_poly)
    for g in getattr(inner, "geoms", [inner]):
        if not isinstance(g, Polygon) or g.area < 1e-3:
            continue
        for tri in shapely.constrained_delaunay_triangles(g).geoms:
            pts = [(x, pp.top, z) for x, z in list(tri.exterior.coords)[:3]]
            ctx.builders[ceiling].polygon(pts, [(x, z) for x, _, z in pts], (0.0, -1.0, 0.0))
    for side in (1.0, -1.0):
        line = pp.axis.offset_curve(side * pp.w / 2)
        part = line.intersection(ground_poly)
        for seg in getattr(part, "geoms", [part]):
            if seg.is_empty or seg.geom_type != "LineString" or seg.length < 0.05:
                continue
            (x0, z0), (x1, z1) = seg.coords[0], seg.coords[-1]
            cx, cz = pp.axis.interpolate(
                pp.axis.project(Point((x0 + x1) / 2, (z0 + z1) / 2))
            ).coords[0]
            mx, mz = (x0 + x1) / 2, (z0 + z1) / 2
            want = (cx - mx, 0.0, cz - mz)  # towards the axis
            y0 = ctx.base_y
            ctx.builders[role].polygon(
                [(x0, y0, z0), (x1, y0, z1), (x1, pp.top, z1), (x0, pp.top, z0)],
                [(0.0, 0.0), (seg.length, 0.0), (seg.length, pp.top - y0), (0.0, pp.top - y0)],
                want,
            )


def _wall_banded(ctx: _Context, role: str, f: Frame, poly: Polygon, holes: Sequence[Opening],
                 s: int) -> None:  # fmt: skip
    """``_wall``; on a textured house plaster gets its weathering as parts of the wall itself:
    under some windows a rain streak (``*_streak``: u across one of the texture's variants, v 0 at
    the sill to 1 at ``STREAK_M`` below), and in the ground storey the plaster up to ``dirt_m``
    above the terrain goes to the foot band (``*_low``, v 0..1 from the ground up)."""
    palette = ctx.style.wall if role == "wall_ground" else ctx.style.infill
    if ctx.lod:  # the distance: closed walls, the openings are flat panels in front of them
        _wall(ctx.builders[role], f, poly, [op for op in holes if op.kind == "passage"])
        return
    if ctx.dirt_m <= 0 or not palette.startswith(("plaster", "lehm")):
        _wall(ctx.builders[role], f, poly, holes)
        return
    for op in holes:
        poly = poly.difference(shapely.box(op.u, op.v, op.u + op.w, op.v + op.h))
    if poly.is_empty:
        return
    for op in holes:
        if op.kind != "window":
            continue
        # chosen by the window's place, not by the house's random stream (nothing else moves)
        pick = random.Random(hash((round(f.lx, 2), round(f.lz, 2), round(f.y0, 2), round(op.u, 2))))
        if pick.random() > STREAK_SHARE:
            continue
        k = pick.randrange(STREAK_VARIANTS)
        u0, u1 = op.u - 0.08, op.u + op.w + 0.08
        region = shapely.box(u0, op.v - STREAK_M, u1, op.v)
        part = poly.intersection(region)
        poly = poly.difference(region)
        for g in getattr(part, "geoms", [part]):
            if not isinstance(g, Polygon) or g.area < 1e-4:
                continue
            for tri in shapely.constrained_delaunay_triangles(g).geoms:
                pts = list(tri.exterior.coords)[:3]
                uvs = [((k + (u - u0) / (u1 - u0)) / STREAK_VARIANTS, (op.v - v) / STREAK_M)
                       for u, v in pts]  # fmt: skip
                ctx.builders[f"{role}_streak"].polygon([f.point(u, v) for u, v in pts], uvs,
                                                        f.n3())  # fmt: skip
    if s != 0:
        _wall(ctx.builders[role], f, poly, [])
        return
    terrain = []
    for u in (0.0, f.width):
        x, _, z = f.point(u, 0.0, 0.3)
        terrain.append((ctx.ground_at(x, z) if ctx.ground_at else f.y0) - f.y0)

    def ground(u: float) -> float:
        t = u / f.width if f.width > 1e-9 else 0.0
        return terrain[0] + (terrain[1] - terrain[0]) * t

    band = Polygon([(-1.0, -1e3), (f.width + 1.0, -1e3), (f.width + 1.0, ground(f.width + 1.0)
                    + ctx.dirt_m), (-1.0, ground(-1.0) + ctx.dirt_m)])  # fmt: skip
    _wall(ctx.builders[role], f, poly.difference(band), [])
    low = poly.intersection(band)
    for g in getattr(low, "geoms", [low]):
        if not isinstance(g, Polygon) or g.area < 1e-4:
            continue
        for tri in shapely.constrained_delaunay_triangles(g).geoms:
            pts = list(tri.exterior.coords)[:3]
            uvs = [(u, (v - ground(u)) / ctx.dirt_m) for u, v in pts]
            ctx.builders[f"{role}_low"].polygon([f.point(u, v) for u, v in pts], uvs, f.n3())


def _with_door(plan: list[Opening], width: float, rules: Rules) -> list[Opening]:
    """``plan`` with a door in the middle instead of the windows it would overlap."""
    d = rules.get("openings", "door")
    dw, dh = float(d["w"]), float(d["h"])
    u0 = (width - dw) / 2
    kept = [op for op in plan if op.u + op.w < u0 - 0.2 or op.u > u0 + dw + 0.2]
    return [*kept, Opening("door", u0, 0.0, dw, dh)]


def _above_ground(ctx: _Context, f: Frame, plan: list[Opening]) -> list[Opening]:
    """Openings whose sill is above the terrain in front (hillside, E1 B and C)."""
    keep = []
    for op in plan:
        x, _, z = f.point(op.u + op.w / 2, 0.0, DOOR_PROBE_M)
        terrain = ctx.ground_at(x, z)  # type: ignore[misc]
        if op.kind in ("door", "gate") or f.y0 + op.v >= terrain + SILL_CLEAR_M:
            keep.append(op)
    return keep


def _door_stairs(ctx: _Context, f: Frame, door: Opening, terrain: float) -> None:
    """Stone steps from the terrain up to the door sill, with one convex collision body."""
    rules = ctx.rules
    rise_max = float(rules.get("hillside", "stairRiseM"))
    run = float(rules.get("hillside", "stairRunM"))
    total = f.y0 - terrain
    count = max(1, math.ceil(total / rise_max))
    rise = total / count
    u0, u1 = door.u - STAIR_EXTRA_M, door.u + door.w + STAIR_EXTRA_M
    b = ctx.builders["wall_ground"]
    bottom = -total - STAIR_SINK_M
    for k in range(count):
        top = -rise * (k + 1)  # step k+1 below the sill, k * run in front of the wall
        d0, d1 = run * k, run * (k + 1)
        uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        b.polygon([f.point(u0, top, d0), f.point(u0, top, d1), f.point(u1, top, d1),
                   f.point(u1, top, d0)], uv, (0.0, 1.0, 0.0))  # fmt: skip
        b.polygon([f.point(u0, bottom, d1), f.point(u1, bottom, d1), f.point(u1, top, d1),
                   f.point(u0, top, d1)], uv, (f.nx, 0.0, f.nz))  # fmt: skip
        for u, sgn in ((u0, -1.0), (u1, 1.0)):
            side = [f.point(u, bottom, d0), f.point(u, top, d0), f.point(u, top, d1),
                    f.point(u, bottom, d1)]  # fmt: skip
            if sgn > 0:
                side.reverse()
            b.polygon(side, uv, (f.ax * sgn, 0.0, f.az * sgn))
    depth = run * count
    pts = np.asarray([f.point(u, v, d) for (v, d) in ((bottom, 0.0), (0.0, 0.0), (-total, depth),
                                                       (bottom, depth)) for u in (u0, u1)],
                     dtype=np.float64)  # fmt: skip
    origin = np.asarray([b.ox, b.oy, b.oz])
    ctx.screens.append(CollisionPart("COL_HULL_0", np.round(pts - origin, 4).astype(np.float32),
                                     _hull_indices(pts)))  # fmt: skip


def _hull_indices(pts: np.ndarray) -> np.ndarray:
    """Outward triangles of a convex hexahedron given as 4 pairs of points (u0, u1) around it."""
    quads = [(0, 2, 4, 6), (1, 7, 5, 3), (0, 1, 3, 2), (2, 3, 5, 4), (4, 5, 7, 6), (6, 7, 1, 0)]
    centre = pts.mean(axis=0)
    tris: list[int] = []
    for q in quads:
        for t in ((q[0], q[1], q[2]), (q[0], q[2], q[3])):
            a, b, c = pts[list(t)]
            if np.dot(np.cross(b - a, c - a), (a + b + c) / 3 - centre) < 0:
                t = (t[0], t[2], t[1])
            tris += t
    return np.asarray(tris, dtype=np.uint32)


def _wall_side(ctx: _Context, f: Frame, s: int, outline: Sequence[tuple[float, float]],
               usable: float) -> None:  # fmt: skip
    """Outward side of a wall house: rubble stone, small windows above the wall crown only, at
    most arrow slits below, no timber, no door."""
    rules = ctx.rules
    wh = rules.get("cityWall", "wallHouse")
    assert ctx.wall is not None
    mid = f.point(f.width / 2, 0.0)
    crown = ctx.wall.crown(mid[0], mid[2])
    plan: list[Opening] = []
    if ctx.style.openings and f.width >= 2.0:
        if f.y0 >= crown - 0.5:
            w, h, sill = (float(x) for x in wh["window"])
            step = float(rules.get("openings", "window", "spacingM"))
        elif s > 0:
            w, h = (float(x) for x in wh["slit"])
            sill = max(0.3, min(1.0, usable - h - 0.4))
            step = float(wh["slitEveryM"])
        else:
            w = 0.0
        if w > 0 and sill + h <= usable - 0.2:
            n = max(1, int((f.width - 1.6) // step) + 1)
            for i in range(n):
                c = f.width / 2 + (i - (n - 1) / 2) * step
                if c - w / 2 >= 0.8 and c + w / 2 <= f.width - 0.8:
                    plan.append(Opening("window", c - w / 2, sill, w, h))
    lintels = []
    if s == 0 and ctx.passages_now:  # a passage out through the town wall (postern)
        plan, lintels = _with_passages(ctx, f, plan)
    _wall(ctx.builders["wall_ground"], f, Polygon(outline), plan)
    for op in plan:
        if op.kind != "passage":
            _reveal(ctx.builders["frame"], f, op, float(rules.get("openings", "revealM")))
    depth = float(rules.get("timber", "depthM"))
    for u0, u1, v in lintels:
        mid_v = v + LINTEL_M / 2
        _beam(ctx.builders["wall_ground"], f, np.array([u0, mid_v]), np.array([u1, mid_v]),
              LINTEL_M, depth)  # fmt: skip


def _screen_wall(ctx: _Context, f: Frame, outline: Sequence[tuple[float, float]]) -> None:
    """Where a wall house is lower than the wall crown: its outward wall rises to the crown as
    a screen wall with parapet top and merlons, flush with the neighbouring wall."""
    assert ctx.wall is not None
    w = ctx.wall
    a, b = f.point(0.0, 0.0), f.point(f.width, 0.0)
    va, vb = w.crown(a[0], a[2]) - f.y0, w.crown(b[0], b[2]) - f.y0
    house = Polygon(outline)
    low = min(y for _, y in outline)
    area = Polygon([(0.0, low), (f.width, low), (f.width, vb), (0.0, va)])
    if not area.is_valid or area.area < 1e-3:
        return
    screen = area.difference(house.buffer(1e-6))
    parts = [
        g for g in getattr(screen, "geoms", [screen]) if isinstance(g, Polygon) and g.area > 0.05
    ]
    if not parts:
        return
    t = w.parapet
    bw = ctx.builders["wall_ground"]
    n = f.n3()
    back = (-n[0], 0.0, -n[2])
    for part in parts:
        for tri in shapely.constrained_delaunay_triangles(part).geoms:
            pts = list(tri.exterior.coords)[:3]
            bw.polygon([f.point(u, v) for u, v in pts], [(u, v) for u, v in pts], n)
            bw.polygon([f.point(u, v, -t) for u, v in pts], [(u, v) for u, v in pts], back)
        ring = list(part.exterior.coords)
        for (ua, wa), (ub, wb) in zip(ring, ring[1:], strict=False):
            edge = np.array([ub - ua, wb - wa])
            length = float(np.linalg.norm(edge))
            if length < 1e-6:
                continue
            # Outward in facade coordinates (the part is counter-clockwise after orient).
            nu, nv = edge[1] / length, -edge[0] / length
            if Polygon(part).exterior.is_ccw:
                nu, nv = -nu, -nv
            want = (f.ax * nu, nv, f.az * nu)
            bw.polygon([f.point(ua, wa), f.point(ub, wb), f.point(ub, wb, -t), f.point(ua, wa, -t)],
                       [(0, 0), (length, 0), (length, t), (0, t)], want)  # fmt: skip
        # Merlons on the crown line where the screen reaches it.
        mw, gap, mh = w.merlon
        u = 0.4 + mw / 2
        top_y = part.bounds[3]
        while u + mw / 2 <= f.width - 0.4:
            v = va + (vb - va) * u / f.width
            if v >= top_y - 0.05 and part.buffer(0.05).contains(Point(u, v - 0.02)):
                u0, u1 = u - mw / 2, u + mw / 2
                y0 = min(va + (vb - va) * u0 / f.width, va + (vb - va) * u1 / f.width) - 0.05
                y1 = v + mh
                box = [(u0, y0), (u1, y0), (u1, y1), (u0, y1)]
                bw.polygon([f.point(x, y) for x, y in box], box, n)
                bw.polygon([f.point(x, y, -t) for x, y in box], box, back)
                bw.polygon([f.point(u0, y1), f.point(u1, y1), f.point(u1, y1, -t),
                            f.point(u0, y1, -t)], box, (0.0, 1.0, 0.0))  # fmt: skip
                for x, sgn in ((u0, -1.0), (u1, 1.0)):
                    side = [f.point(x, y0), f.point(x, y1), f.point(x, y1, -t), f.point(x, y0, -t)]
                    bw.polygon(side, box, (f.ax * sgn, 0.0, f.az * sgn))
            u += mw + gap
        # Collision: a slab from the lowest screen point up to the parapet top.
        bot = part.bounds[1]
        corners = [f.point(0.0, bot), f.point(f.width, bot), f.point(f.width, bot, -t),
                   f.point(0.0, bot, -t)]  # fmt: skip
        tops = [f.point(0.0, va), f.point(f.width, vb), f.point(f.width, vb, -t),
                f.point(0.0, va, -t)]  # fmt: skip
        pts = np.asarray(corners + tops, dtype=np.float64)
        origin = np.asarray([bw.ox, bw.oy, bw.oz])
        ring2 = [(p[0], p[2]) for p in corners]
        area2 = sum(ring2[i - 1][0] * ring2[i][1] - ring2[i][0] * ring2[i - 1][1]
                    for i in range(4))  # fmt: skip
        order = [0, 1, 2, 3] if area2 > 0 else [3, 2, 1, 0]
        pts = pts[order + [4 + i for i in order]]
        tris = [(0, 1, 2), (0, 2, 3)]
        for i in range(4):
            j = (i + 1) % 4
            tris += [(i, 4 + i, 4 + j), (i, 4 + j, j)]
        tris += [(4, 6, 5), (4, 7, 6)]
        pos = np.round(pts - origin, 4).astype(np.float32)
        idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
        ctx.screens.append(CollisionPart("COL_HULL_0", pos, idx))


def _outer_edge(a: Sequence[float], b: Sequence[float], normal: Sequence[float],
                wall: WallContext, rules: Rules) -> bool:  # fmt: skip
    """A footprint edge that faces out of the town near the wall line."""
    wh = rules.get("cityWall", "wallHouse")
    mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    probe = Point(
        mx + normal[0] * float(wh["outerProbeM"]), mz + normal[1] * float(wh["outerProbeM"])
    )
    return (not wall.ring.contains(probe)) and wall.ring.exterior.distance(Point(mx, mz)) <= float(
        wh["nearRingM"]
    )


def _age_windows(
    plan: list[Opening], rules: Rules, rng: random.Random, age: float
) -> list[Opening]:
    """Old houses: window sills and widths a little irregular (procedural windows only)."""
    a = rules.data.get("aging", {})
    if age <= 0 or not a:
        return plan
    out = []
    for op in plan:
        if op.kind != "window":
            out.append(op)
            continue
        dv = rng.uniform(-1, 1) * float(a.get("windowSillJitterM", 0.0)) * age
        w = op.w * (1 + rng.uniform(-1, 1) * float(a.get("windowWidthJitter", 0.0)) * age)
        out.append(Opening(op.kind, op.u + (op.w - w) / 2, max(0.3, op.v + dv), w, op.h))
    return out


def _lean_posts(segs: list, width: float, openings: Sequence[Opening], rules: Rules,
                rng: random.Random, st: HouseStyle) -> list:  # fmt: skip
    """Old houses: inner posts lean a little about their foot; never into an opening or outside."""
    a = rules.data.get("aging", {})
    max_deg = float(a.get("postLeanMaxDeg", 0.0)) * st.age
    if max_deg <= 0 or st.style in a.get("stoneStyles", ()):
        return segs
    b = float(rules.get("timber", "beamM"))
    out = []
    for kind, k, p0, p1 in segs:
        if kind != "post" or p0[0] < b or p0[0] > width - b:  # corner posts stay upright
            out.append((kind, k, p0, p1))
            continue
        ang = math.radians(rng.uniform(-max_deg, max_deg))
        d = p1 - p0
        q1 = p0 + np.array([d[0] * math.cos(ang) - d[1] * math.sin(ang),
                            d[0] * math.sin(ang) + d[1] * math.cos(ang)])  # fmt: skip
        inside = b / 2 <= q1[0] <= width - b / 2
        grown = [Opening(o.kind, o.u - b / 2, o.v - b / 2, o.w + b, o.h + b) for o in openings]
        clear = len(_clip_out(p0, q1, grown)) == 1 and np.allclose(
            _clip_out(p0, q1, grown)[0][1], q1
        )
        out.append((kind, k, p0, q1) if inside and clear else (kind, k, p0, p1))
    return out


def _door_point(
    ring: Sequence[tuple[float, float]],
    normals: Sequence[tuple[float, float]],
    i: int,
    out: float = DOOR_PROBE_M,
) -> tuple[float, float]:
    a, b = ring[i], ring[(i + 1) % len(ring)]
    return ((a[0] + b[0]) / 2 + normals[i][0] * out, (a[1] + b[1]) / 2 + normals[i][1] * out)


def _door_is_free(ctx: _Context, ring: Sequence[tuple[float, float]],
                  normals: Sequence[tuple[float, float]], i: int) -> bool:  # fmt: skip
    """Nothing in front of the middle of edge ``i``: no other house (``door_free``), no other
    mass of this building, not the city wall (W6: a door on a shared wall is never reachable)."""
    if ctx.door_free is None:
        return True
    for d in DOOR_FREE_PROBES_M:
        x, z = _door_point(ring, normals, i, d)
        if not ctx.door_free(x, z):
            return False
        q = Point(x, z)
        # every mass of this building, the own one too (an L-shaped mass faces its other wing)
        if any(m.distance(q) < DOOR_CLEAR_M for m in ctx.own_masses):
            return False
        if ctx.wall is not None and ctx.wall.ring.exterior.distance(q) < DOOR_WALL_M:
            return False
    return True


def _door_edge(ctx: _Context, ring: Sequence[tuple[float, float]],
               normals: Sequence[tuple[float, float]], lengths: Sequence[float],
               candidates: Sequence[int], others: Sequence[int], ground: float,
               eave: float, rules: Rules,
               notes: list[str]) -> tuple[int, float, float | None]:  # fmt: skip
    """Door edge and floor height of a mass (decision E1 of the walkthrough).

    Without terrain: the longest candidate (street) edge. With terrain: among the candidates long
    enough for a facade, the one where the ground in front of the middle matches the floor best
    (A). If the ground there is still higher, the floor moves up onto it (at most so far that a
    ground storey still fits under the eave) and the part below becomes the socle of a hillside
    house (B); if it is much lower, the door gets stairs. Where no street edge allows that
    without burying the door (steep slopes), the door may go to another edge (``others``, not on
    the city wall) whose ground fits. If even that is too high, the third value is the ground at
    the door: ``_mass`` then puts the door into an upper storey (E1-C).

    With ``ctx.door_free`` (W6) only edges with nothing in front count; if no street edge is free,
    the door moves to a free side or back wall, the nearest to a street first; without any free
    edge the mass keeps its street edge for the floor but gets no door (``door_blocked``).
    """
    ctx.stair_ground = None
    ctx.door_storey = 0
    ctx.door_blocked = False
    moved = False
    if ctx.door_free is not None:
        free = [i for i in range(len(ring)) if _door_is_free(ctx, ring, normals, i)]
        reach = ctx.door_reach

        def reachable(i: int) -> bool:
            return reach is None or reach(*_door_point(ring, normals, i, DOOR_FREE_PROBES_M[0]))

        street_free = [i for i in candidates if i in free]
        side = [i for i in others if i in free and lengths[i] >= DOOR_SIDE_MIN_M]
        # a way to a street first, then the street side (decision W6 "Türen verlegen")
        street_way = [i for i in street_free if reachable(i)]
        side_way = [i for i in side if i not in street_free and reachable(i)]
        if street_way:
            candidates = street_way
        elif side_way:
            candidates, moved = side_way, True
            notes.append("door moved: street side blocked")
        elif street_free:
            candidates = street_free
        elif side:
            candidates, moved = side, True
            notes.append("door moved: street side blocked")
        else:
            ctx.door_blocked = True
            notes.append("door without access (no free wall)")
        if not ctx.door_blocked:
            others = [i for i in others if i in free]
    if ctx.ground_at is None:
        return max(candidates, key=lambda i: lengths[i]), ground, None
    min_facade = float(rules.get("openings", "minFacadeM"))
    long = [i for i in candidates if lengths[i] >= min_facade] or list(candidates)

    def key(i: int) -> tuple[float, ...]:
        t = ctx.ground_at(*_door_point(ring, normals, i))  # type: ignore[misc]
        fit = round(abs(t - ground) / DOOR_TOLERANCE_M)
        if moved and ctx.streets is not None:  # a moved door: towards the nearest street
            near = round(ctx.streets.distance(*_door_point(ring, normals, i)) / 4.0)
            return (fit, near, -lengths[i])
        return (fit, -lengths[i])

    edge = min(long, key=key)
    probe = _door_point(ring, normals, edge)
    t = ctx.ground_at(*probe)
    # never so high that not even a ground storey fits under the eave (steep slopes)
    cap = eave - float(rules.get("storeys")["groundM"])
    if t > cap + DOOR_TOLERANCE_M:
        spare = [i for i in others if lengths[i] >= min_facade and i not in long]
        if spare and key(min(spare, key=key)) < key(edge):
            edge = min(spare, key=key)
            notes.append("door moved off the street (slope)")
            probe = _door_point(ring, normals, edge)
            t = ctx.ground_at(*probe)
    nx, nz = normals[edge]
    kind = "blocked" if ctx.door_blocked else "ground"
    record = [round(probe[0], 2), round(probe[1], 2), 0.0, kind, round(nx, 4), round(nz, 4)]
    ctx.doors.append(record)
    if t > cap + DOOR_TOLERANCE_M:
        return edge, ground, t  # hillside house: door above the ground storey (``_mass``)
    if t > ground + DOOR_TOLERANCE_M:
        notes.append("floor raised to the ground at the door")
        ground = t
    elif t < ground - float(rules.get("hillside", "stairFromM")) and not ctx.door_blocked:
        notes.append("stairs in front of the door")
        ctx.stair_ground = t
        record[3] = "stairs"
    record[2] = round(ground, 3)
    return edge, ground, None


def _hillside_storeys(ground: float, eave: float, door_y: float, rules: Rules,
                      storeys: Sequence[float] | None,
                      rng: random.Random) -> tuple[list[float], int] | None:  # fmt: skip
    """Storey heights with a floor exactly at ``door_y`` and the storey number of that floor
    (E1-C A), or None if the storeys below or above the door would get too low."""
    hs = rules.get("hillside")
    low_min, low_max = float(hs["storeyMinM"]), float(hs["storeyMaxM"])
    below, above = door_y - ground, eave - door_y
    if above < float(hs["doorStoreyMinM"]):
        return None
    k = max(1, round(below / float(rules.get("storeys")["upperM"])))
    if not low_min <= below / k <= low_max:
        return None
    upper = storey_heights(above, rules, storeys, rng)
    return [below / k] * k + upper, k


def _mass(ctx: _Context, mass: Mass, ground: float, override: Any,  # noqa: ANN401
          streets: StreetIndex | None, notes: list[str]) -> None:  # fmt: skip
    rules = ctx.rules
    if _valid_polygon(mass.footprint) is None:
        return
    ring = [(float(x), float(z)) for x, z in mass.footprint]
    storeys = getattr(override, "storeys", None) or None
    normals = _outward_normals(ring)
    n = len(ring)
    ctx.wall_edges = set()
    if ctx.style.wall_house and ctx.wall is not None:
        wall = ctx.wall
        ctx.wall_edges = {
            i for i in range(n) if _outer_edge(ring[i], ring[(i + 1) % n], normals[i], wall, rules)
        }
    reach = float(rules.get("jetty", "streetReachM"))
    street = []
    for i in range(n):
        faces = bool(
            streets and streets.faces_street(ring[i], ring[(i + 1) % n], normals[i], reach)
        )
        own = faces or (ctx.front is not None and ctx.front.edge == i)
        street.append(own and i not in ctx.wall_edges)
    sides = rules.get("jetty", "sides")
    jetty = getattr(override, "jetty_m", None)
    use_jetty = ctx.style.jetty or jetty is not None
    jetty = float(rules.get("jetty", "defaultM")) if jetty is None else float(jetty)
    if not use_jetty or not ctx.style.timber:
        jetty = 0.0
    jetty_edges = [sides == "all" or (sides == "street" and street[i]) for i in range(n)]
    lengths = [math.dist(ring[i], ring[(i + 1) % n]) for i in range(n)]
    inner = [i for i in range(n) if i not in ctx.wall_edges]
    candidates = [i for i in range(n) if street[i]] or inner or list(range(n))
    door_edge, ground, door_y = _door_edge(
        ctx, ring, normals, lengths, candidates, inner, ground, mass.eave_y, rules, notes
    )
    heights: list[float] = []
    if door_y is not None:
        hill = _hillside_storeys(ground, mass.eave_y, door_y, rules, storeys, ctx.rng)
        if hill is not None:  # A: the door in the storey whose floor meets the ground there
            heights, ctx.door_storey = hill
            notes.append("door in an upper storey (hillside)")
            kind = "blocked" if ctx.door_blocked else "upper"
            ctx.doors[-1][2:4] = [round(door_y, 3), kind]
        else:  # B: floor as high as a ground storey allows; export-terrain digs a descent
            ground = max(ground, mass.eave_y - float(rules.get("storeys")["groundM"]))
            notes.append("door below the ground (short descent)")
            kind = "blocked" if ctx.door_blocked else "descent"  # no door: nothing to dig
            ctx.doors[-1][2:4] = [round(ground, 3), kind]
    if not heights:
        heights = storey_heights(mass.eave_y - ground, rules, storeys, ctx.rng)
    if not heights:
        return

    ctx.passages_now = _plan_passages(ctx, ring, ground, heights, notes)

    outlines = [offset_ring(ring, [jetty * s if jetty_edges[i] else 0.0 for i in range(n)])
                for s in range(len(heights))]  # fmt: skip
    if any(not Polygon([p for p, _ in o]).is_valid for o in outlines):
        notes.append("jetty skipped (invalid offset)")
        outlines = [offset_ring(ring, [0.0] * n) for _ in heights]
    top_ring = [p for p, _ in outlines[-1]]
    top_poly = Polygon(top_ring)
    aging = rules.data.get("aging", {})
    factor = (
        float(aging.get("stoneSagFactor", 0.0))
        if ctx.style.style in aging.get("stoneStyles", ())
        else 1.0
    )
    probe = _SagRoof(_roof_mass(mass, top_ring), top_poly, 0.0)
    sag = float(aging.get("ridgeSagMaxRatio", 0.0)) * probe.length * ctx.style.age * factor
    roof = _SagRoof(_roof_mass(mass, top_ring), top_poly, sag)
    ctx.max_sag = max(ctx.max_sag, roof.sag)
    crease = roof.crease(top_poly)

    y = ground
    ctx.upper_h = heights[1] if len(heights) > 1 else None
    for s, h in enumerate(heights):
        ring_s = outlines[s]
        ns = _outward_normals([p for p, _ in ring_s])
        below = y - ctx.base_y if s == 0 else 0.0  # the ground storey reaches down to the base
        ctx.room_storey = (s == 0 and ctx.interior is not None and not ctx.lod
                           and ctx.door_storey == 0 and not ctx.door_blocked and ctx.room is None
                           and door_edge is not None and _room_fits(ctx, ring, h)
                           ) or (s == 1 and ctx.stair is not None and ctx.room is not None
                                 and "upper" not in ctx.room)  # fmt: skip
        for k, (a, edge) in enumerate(ring_s):
            f = make_frame(a, ring_s[(k + 1) % len(ring_s)][0], ns[k], y)
            if f.width < 1e-3:
                continue
            if s == len(heights) - 1:
                outline, usable = _top_outline(f, roof, crease, y, below)
            else:
                outline, usable = [(0.0, -below), (f.width, -below), (f.width, h), (0.0, h)], h
            door_here = edge == door_edge and s == ctx.door_storey and not ctx.door_blocked
            _facade(ctx, f, edge, s, heights, outline, usable, door_here)
            if s == len(heights) - 1 and edge >= 0 and edge in ctx.wall_edges:
                _screen_wall(ctx, f, outline)
        if s > 0:
            _jetty_underside(ctx, [p for p, _ in ring_s], [p for p, _ in outlines[s - 1]], y)
        if s == 0:
            for pp in ctx.passages_now:
                _passage_tunnel(ctx, pp, Polygon([p for p, _ in ring_s]))
            if ctx.door_op is not None and ctx.door_storey == 0 and ctx.room is None:
                _room(ctx, ring, y, h, notes)
        if s == 1 and ctx.room_storey:
            _upper_room(ctx, y, h, notes)
        y += h
    cuts = []  # no roof overhang over the outward sides of a wall house
    top_n = _outward_normals(top_ring)
    for k, (a, edge) in enumerate(outlines[-1]):
        if edge >= 0 and edge in ctx.wall_edges:
            cuts.append((a, outlines[-1][(k + 1) % len(outlines[-1])][0], top_n[k]))
    _add_roof(ctx.builders["roof"], ctx.builders["roof_north"], roof, top_poly, crease, rules,
              cuts)  # fmt: skip
    ctx.roofs.append(_RoofPart(roof, top_poly, Polygon(ring)))


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


def _materials(st: HouseStyle) -> dict[str, str]:
    """Role -> palette entry for one house."""
    north = "roof_old_moss" if st.roof == "roof_old" else st.roof
    return {"wall_ground": st.wall, "infill": st.infill, "timber": st.timber_color,
            "roof": st.roof, "roof_north": north, "frame": "frame",
            "chimney": st.chimney, "wall_ground_low": st.wall,
            "infill_low": st.infill, "wall_ground_streak": st.wall,
            "infill_streak": st.infill, "room_wall": "plaster_white",
            "room_floor": "timber_dark", "room_ceiling": "timber_dark",
            "room_beam": st.timber_color}  # fmt: skip


def build_house(
    building: dict[str, Any],
    base_y: float,
    origin_xz: tuple[float, float],
    rules: Rules,
    streets: StreetIndex | None = None,
    override: Any = None,  # noqa: ANN401  BuildingOverride or None
    hearth: bool = False,
    wall: WallContext | None = None,
    ground_at: Callable[[float, float], float] | None = None,
    lod: int = 0,
    lod0_level: int = 0,
    door_free: Callable[[float, float], bool] | None = None,
    door_reach: Callable[[float, float], bool] | None = None,
    interior: dict[str, Any] | None = None,
) -> HouseResult:
    """Half-timbered house of a ``buildings.json`` entry; vertices relative to (origin, base_y).

    Over the triangle budget the timber is reduced step by step (no pattern, no bay posts, none),
    then the dormers are left out. ``hearth``: a barn that may have a chimney (``barn_hearths``).
    """
    budget = int(rules.get("budget", "trianglesPerBuilding"))
    front = getattr(override, "front_facade", None)
    notes: list[str] = []
    if front is not None and building.get("parts"):
        notes.append("frontFacade ignored (building has LoD2 parts)")
        front = None
    style = assign_style(building, override, streets, rules)
    wanted = getattr(override, "wall_house", None)
    on_line = wall is not None and building["id"] in wall.houses
    if wall is not None and (wanted if wanted is not None else on_line) and style.timber:
        style = replace(style, wall_house=True)
    massing = masses_for_building(building)
    sources = building.get("parts") or [building]
    masses, steepened = [], 0
    for i, mass in enumerate(massing.masses):
        steep = steepen(
            mass, rules, _rng(building["id"], getattr(override, "seed", None), f":roof{i}")
        )
        if steep is not None and style.style != "mauer":
            mass = steep
            steepened += 1
        masses.append(cap_rise(mass, rules) or mass)
    materials = _materials(style)
    if interior is not None and interior.get("use") in _room_spec(rules)["stoneFloors"]:
        materials["room_floor"] = "stone"
    passages = list(getattr(override, "passages", None) or [])
    tex = rules.data.get("textures", {})
    textured = tex.get("all", False) or building["id"] in tex.get("probe", ())
    dirt_m = float(tex.get("dirtM", 0.8)) if textured and not lod else 0.0
    result = None
    # lod 1: one pass with the plain timber (no figures) and without dormers
    # (never more timber than lod 0 kept: pass its ``timber_level`` as ``lod0_level``)
    for level in range(5) if not lod else (max(LOD1_LEVEL, lod0_level),):
        builders = {role: _Builder((origin_xz[0], base_y, origin_xz[1])) for role in ROLES}
        rng = _rng(building["id"], getattr(override, "seed", None))
        ctx = _Context(
            rules,
            rng,
            front,
            style,
            level,
            builders,
            base_y,
            wall=wall,
            ground_at=ground_at,
            passages=passages,
            dirt_m=dirt_m,
            lod=lod,
            door_free=door_free,
            door_reach=door_reach,
            streets=streets,
            interior=None if lod else interior,
            own_masses=[_valid_polygon(m.footprint) or Polygon() for m in masses],
        )
        level_notes: list[str] = []
        for mass, src in zip(masses, sources, strict=False):
            ground = float(src.get("groundY", building.get("groundY", base_y)))
            try:
                _mass(ctx, mass, ground, override, streets, level_notes)
            except shapely.errors.GEOSException:
                level_notes.append("part skipped (invalid geometry)")
        dormers, chimneys = _roof_features(
            ctx, building["id"], override, streets, hearth, level < 4 and not lod, level_notes
        )

        def name(role: str) -> str:
            if role.endswith("_low"):
                return materials[role] + LOW_SUFFIX
            if role == "roof_north" and textured:
                return materials[role] + MOSS_SUFFIX
            if role.endswith("_streak"):
                return materials[role] + STREAK_SUFFIX
            return materials[role]

        split = bool(ctx.room_parts) and not lod  # W7: each room its own mesh (its own lights)
        prims = [Primitive(name(r), rules.color(materials[r]), builders[r].mesh())
                 for r in ROLES if builders[r].idx and not (split and r in ROOM_ROLES)]  # fmt: skip
        room_prims: dict[str, list[Primitive]] = {}
        room_cols: dict[str, list[CollisionPart]] = {}
        if split:
            origin3 = (origin_xz[0], base_y, origin_xz[1])
            for r in ROOM_ROLES:
                if not builders[r].idx:
                    continue
                pieces = _split_by_room(
                    builders[r].mesh(), ctx.room_parts, origin3, ctx.room_levels
                )
                for room_name, mesh in pieces.items():
                    room_prims.setdefault(room_name, []).append(
                        Primitive(name(r), rules.color(materials[r]), mesh)
                    )
            # a model without COL_ collides with all its triangles (asset.md): each room gets one
            # small box just above its ceiling, inside the house's solid storey above
            top = float((ctx.room or {}).get("ceiling", base_y))
            upper = (ctx.room or {}).get("upper")
            hole = None
            if ctx.stair is not None and upper is not None:
                hole = Polygon((ctx.room or {})["stairs"]["opening"]).buffer(0.3)
            for room_name, poly in ctx.room_parts:
                over = float(upper["ceiling"]) if upper and room_name.startswith("OBEN") else top
                free = poly.difference(hole) if hole is not None else poly
                c = (free if not free.is_empty else poly).representative_point()
                square = Polygon([(c.x - 0.1, c.y - 0.1), (c.x + 0.1, c.y - 0.1),
                                  (c.x + 0.1, c.y + 0.1), (c.x - 0.1, c.y + 0.1)])  # fmt: skip
                room_cols[room_name] = [prism_body(square, over + 0.05, over + 0.12, origin3,
                                                   "COL_BOX_0")]  # fmt: skip
        # the weathering splits walls into a few more triangles and the room (W7) lies outside
        # the house budget: neither costs timber
        tris = sum(len(builders[r].idx) // 3 for r in ROLES
                   if r not in ROOM_ROLES and not r.endswith(("_low", "_streak")))  # fmt: skip
        collision = collision_for(masses, base_y, origin_xz, ctx.carve)
        col = collision
        origin3 = (origin_xz[0], base_y, origin_xz[1])
        floors = [prism_body(piece, base_y, top, origin3, "COL_HULL_F")
                  for poly, top in ctx.floors for piece in convex_pieces(poly)]  # fmt: skip
        floors += [prism_body(piece, y0, y1, origin3, "COL_HULL_W")
                   for poly, y0, y1 in ctx.inner_walls
                   for piece in convex_pieces(poly)]  # fmt: skip
        if ctx.screens or floors:
            parts = [*collision.parts, *ctx.screens, *floors]
            parts = [
                CollisionPart(f"COL_HULL_{i}", p.positions, p.indices)
                if p.name.startswith("COL_HULL_")
                else p
                for i, p in enumerate(parts)
            ]
            col = CollisionResult(parts, collision.fallback, collision.decomposed)
        result = HouseResult(prims, tris, [*notes, *massing.notes, *level_notes], level, style,
                             steepened, round(ctx.max_sag, 3), dormers, chimneys,
                             col, list(ctx.doors), list(masses), ctx.room, room_prims,
                             room_cols)  # fmt: skip
        if lod or tris <= budget or not style.timber:
            break
    assert result is not None
    if result.timber_level:
        what = "timber and dormers" if result.timber_level == 4 else "timber"
        result.notes.append(f"{what} reduced to level {result.timber_level} (budget {budget})")
    return result


LOD1_LEVEL = 1  # timber level of lod 1: posts and rails, no figures


def lod2_primitives(house: HouseResult, base_y: float, origin_xz: tuple[float, float],
                    rules: Rules) -> list[Primitive]:  # fmt: skip
    """lod 2: the masses only, walls in the house's wall colour (the infill of a timbered house)
    and the roof in its colour; no openings, timber or overhang."""
    if house.style is None or not house.masses:
        return []
    st = house.style
    wall_mat = st.infill if st.timber else st.wall
    walls = _Builder((origin_xz[0], base_y, origin_xz[1]))
    roofs = _Builder((origin_xz[0], base_y, origin_xz[1]))
    for mass in house.masses:
        _add_mass(walls, mass, base_y, roofs)
    out = []
    for mat, b in ((wall_mat, walls), (st.roof, roofs)):
        if b.idx:
            out.append(Primitive(mat, rules.color(mat), b.mesh()))
    return out


def _add_roof(
    b: _Builder, north: _Builder, roof: _Roof, top: Polygon, crease: LineString | None,
    rules: Rules, cuts: Sequence[tuple[Any, Any, Any]] = (),
) -> None:  # fmt: skip
    over = float(rules.get("roof", "overhangM"))
    thick = float(rules.get("roof", "thicknessM"))
    outer = top.buffer(over, join_style="mitre", mitre_limit=3.0)
    if not isinstance(outer, Polygon):
        outer = top
    for a, c, nrm in cuts:  # keep the overhang off these edges (half-plane on the inner side)
        d = np.asarray(c, dtype=float) - np.asarray(a, dtype=float)
        d /= float(np.linalg.norm(d)) or 1.0
        nn, big = np.asarray(nrm, dtype=float), 1e4
        pa, pc = np.asarray(a, dtype=float), np.asarray(c, dtype=float)
        half = Polygon(
            [pa - d * big, pc + d * big, pc + d * big - nn * big, pa - d * big - nn * big]
        )
        clipped = outer.intersection(half)
        if isinstance(clipped, Polygon) and not clipped.is_empty:
            outer = clipped
    pieces = list(split(outer, crease).geoms) if crease is not None else [outer]
    if isinstance(roof, _SagRoof):
        for line in roof.bands(outer, int(rules.data.get("aging", {}).get("roofBands", 4))):
            pieces = [g for piece in pieces for g in split(piece, line).geoms]
    for piece in pieces:
        tris = list(shapely.constrained_delaunay_triangles(piece).geoms)
        if not tris:
            continue
        pts0 = [(x, _extrapolated_height(roof, x, z) + thick, z)
                for x, z in list(tris[0].exterior.coords)[:3]]  # fmt: skip
        a, c, d = (np.asarray(p) for p in pts0)
        normal = np.cross(c - a, d - a)
        if normal[1] < 0:
            normal = -normal
        length = float(np.linalg.norm(normal)) or 1.0
        target = north if normal[2] / length < NORTH_ROOF else b  # a planar piece: one direction
        for tri in tris:
            pts = [
                (x, _extrapolated_height(roof, x, z) + thick, z)
                for x, z in list(tri.exterior.coords)[:3]
            ]
            target.polygon(pts, [(x - b.ox, z - b.oz) for x, _, z in pts], (0.0, 1.0, 0.0))
    eaves = outer.difference(top)
    under = (
        list(split(eaves, crease).geoms) if crease is not None and not eaves.is_empty else [eaves]
    )
    if isinstance(roof, _SagRoof) and not eaves.is_empty:
        for line in roof.bands(outer, int(rules.data.get("aging", {}).get("roofBands", 4))):
            under = [g for piece in under for g in split(piece, line).geoms]
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
        if isinstance(roof, _SagRoof) and roof.sag > 0:  # follow the sagging ridge along the edge
            fine = [pts[0]]
            for p, q in zip(pts, pts[1:], strict=False):
                fine += [
                    (p[0] + (q[0] - p[0]) * k / 4, p[1] + (q[1] - p[1]) * k / 4)
                    for k in range(1, 5)
                ]
            pts = fine
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


# --- chimneys and dormers --------------------------------------------------------------------


@dataclass(frozen=True)
class _RoofPart:
    roof: _SagRoof
    top: Polygon  # outline of the top storey: the roof's plan without overhang
    footprint: Polygon  # of the mass (clearance to the other LoD2 parts)


@dataclass(frozen=True)
class Dormer:
    """A dormer on one side of a saddle roof; roof coordinates u (along the ridge), v (across)."""

    side: int  # +1 / -1: the roof side towards +v / -v of the ridge
    u: float  # centre along the ridge
    w: float  # width of the front wall
    kind: str  # schlepp | giebel
    hatch: bool  # loading hatch instead of a window
    pitch: float  # degrees of the dormer roof
    d_front: float  # horizontal distance of the front wall from the ridge line
    depth: float = 0.0  # from the front wall to where the dormer roof ends under the main roof


def _plan(roof: _Roof, u: float, v: float) -> tuple[float, float]:
    return (roof.u[0] * u + roof.v[0] * v, roof.u[1] * u + roof.v[1] * v)


def _plan_rect(roof: _Roof, u0: float, u1: float, v0: float, v1: float) -> Polygon:
    return Polygon([_plan(roof, u0, v0), _plan(roof, u1, v0), _plan(roof, u1, v1),
                    _plan(roof, u0, v1)])  # fmt: skip


def _main_top(roof: _Roof, thick: float, u: float, v: float) -> float:
    return roof.height(*_plan(roof, u, v)) + thick


def barn_hearths(buildings: Sequence[dict[str, Any]], overrides: dict[str, Any] | None,
                 site: StreetIndex | None, rules: Rules) -> set[str]:  # fmt: skip
    """Ids of barns that may have a chimney: built onto a dwelling or with a hearth function.

    Free-standing barns get none (fire protection, hay), nor do the ``noHearthFunctions``
    (garages: sheds or stables without a hearth around 1700), even when built on.
    """
    c = rules.data.get("chimneys")
    if not c:
        return set()
    styles: dict[str, str] = {}
    polys: dict[str, Polygon] = {}
    for b in buildings:
        fp = b.get("footprint") or []
        poly = _valid_polygon([tuple(p) for p in fp]) if len(fp) >= 3 else None
        if poly is None:
            continue
        styles[b["id"]] = assign_style(b, (overrides or {}).get(b["id"]), site, rules).style
        polys[b["id"]] = poly
    ids = list(polys)
    tree = STRtree([polys[i] for i in ids]) if ids else None
    functions = {b["id"]: str(b.get("function") or "") for b in buildings}
    result = set()
    for bid, style in styles.items():
        if style not in c["barnStyles"] or functions.get(bid) in c.get("noHearthFunctions", ()):
            continue
        if functions.get(bid) in c["hearthFunctions"]:
            result.add(bid)
            continue
        assert tree is not None
        near = tree.query(polys[bid].buffer(float(c["attachedM"])), predicate="intersects")
        if any(ids[j] != bid and styles[ids[j]] in c["hearthStyles"] for j in near):
            result.add(bid)
    return result


def chimney_count(style: HouseStyle, ridge_m: float, rules: Rules, override: Any,  # noqa: ANN401
                  hearth: bool) -> int:  # fmt: skip
    wanted = getattr(override, "chimneys", None)
    if wanted is not None:
        return int(wanted)
    c = rules.data.get("chimneys")
    if not c or ridge_m < float(c["minRidgeM"]):
        return 0
    if style.style in c["barnStyles"] and not hearth:
        return 0
    n = int(c["countByStyle"].get(style.style, 0))
    if n == 1 and style.style in c["secondStyles"] and ridge_m >= float(c["secondFromRidgeM"]):
        n = 2
    return n


def chimney_rect(roof: _Roof, u: float, v: float, rules: Rules) -> Polygon:
    along, across = (float(x) for x in rules.get("chimneys", "sizeM"))
    return _plan_rect(roof, u - along / 2, u + along / 2, v - across / 2, v + across / 2)


def place_chimneys(part: _RoofPart, n: int, others: Sequence[Polygon], rules: Rules,
                   rng: random.Random) -> list[tuple[float, float]]:  # fmt: skip
    """Chimney centres (u, v) near the ridge, as above a central hearth.

    One chimney: t along the ridge in ``ridgeT``; several: one per section of ``ridgeSpanT``. Each
    stands up to ``offRidgeMaxM`` beside the ridge, keeps ``gableClearM`` to the gable ends and
    stays clear of the other parts of the building.
    """
    c = rules.get("chimneys")
    roof = part.roof
    along = float(c["sizeM"][0])
    lo = roof.umin + float(c["gableClearM"]) + along / 2
    hi = roof.umax - float(c["gableClearM"]) - along / 2
    if n <= 0 or hi < lo or roof.mass.roof != "saddle":
        return []
    if n == 1:
        ranges = [(float(c["ridgeT"][0]), float(c["ridgeT"][1]))]
    else:
        a, b = (float(x) for x in c["ridgeSpanT"])
        ranges = [(a + (b - a) * k / n, a + (b - a) * (k + 1) / n) for k in range(n)]
    inside = part.top.buffer(1e-6)
    out: list[tuple[float, float]] = []
    for t0, t1 in ranges:
        t = rng.uniform(t0, t1)
        off = rng.uniform(0.0, float(c["offRidgeMaxM"])) * rng.choice((-1.0, 1.0))
        for tt, oo in ((t, off), (t, 0.0), ((t0 + t1) / 2, 0.0)):
            u = min(hi, max(lo, roof.umin + roof.length * tt))
            v = roof.mid + oo
            rect = chimney_rect(roof, u, v, rules)
            if (inside.contains(rect)
                    and all(rect.distance(o) >= float(c["partsClearM"]) for o in others)
                    and all(abs(u - pu) >= along + 0.5 for pu, _ in out)):  # fmt: skip
                out.append((u, v))
                break
    return out


def _box(b: _Builder, corners: Sequence[tuple[float, float]], y0: float, y1: float,
         top: bool, bottom: bool) -> None:  # fmt: skip
    cx = sum(p[0] for p in corners) / len(corners)
    cz = sum(p[1] for p in corners) / len(corners)
    for i, p in enumerate(corners):
        q = corners[(i + 1) % len(corners)]
        want = ((p[0] + q[0]) / 2 - cx, 0.0, (p[1] + q[1]) / 2 - cz)
        b.polygon([(p[0], y0, p[1]), (q[0], y0, q[1]), (q[0], y1, q[1]), (p[0], y1, p[1])],
                  [(0, 0), (1, 0), (1, y1 - y0), (0, y1 - y0)], want)  # fmt: skip
    if top:
        b.polygon([(x, y1, z) for x, z in corners], list(corners), (0.0, 1.0, 0.0))
    if bottom:
        b.polygon([(x, y0, z) for x, z in corners], list(corners), (0.0, -1.0, 0.0))


def _chimney(b: _Builder, roof: _Roof, u: float, v: float, rules: Rules,
             rng: random.Random) -> None:  # fmt: skip
    """Masonry shaft from below the roof surface (no gap, also on a sagging roof), cap plate."""
    c = rules.get("chimneys")
    thick = float(rules.get("roof", "thicknessM"))
    along, across = (float(x) for x in c["sizeM"])

    def ring(ha: float, hc: float) -> list[tuple[float, float]]:
        return [_plan(roof, u + du, v + dv) for du, dv in ((-ha, -hc), (ha, -hc), (ha, hc),
                                                             (-ha, hc))]  # fmt: skip

    shaft = ring(along / 2, across / 2)
    foot = min(roof.height(x, z) for x, z in shaft) - 0.05
    lo, hi = (float(x) for x in c["riseM"])
    top = _main_top(roof, thick, u, roof.mid) + rng.uniform(lo, hi)
    _box(b, shaft, foot, top, top=False, bottom=False)
    co = float(c["capOverhangM"])
    _box(b, ring(along / 2 + co, across / 2 + co), top, top + float(c["capM"]), True, True)


def _dive(roof: _Roof, thick: float, u: float, side: int, d_front: float, h0: float,
          g: float) -> float:  # fmt: skip
    """Horizontal distance from the front line (towards the ridge) at which the line h0 + g * s
    meets the top of the main roof; at a fixed u the main roof is linear across."""
    m0 = _main_top(roof, thick, u, roof.mid + side * d_front)
    k = _main_top(roof, thick, u, roof.mid + side * (d_front - 1.0)) - m0
    if k - g <= 1e-6:
        return math.inf
    return max(0.0, (h0 - m0) / (k - g))


def _dormer_levels(roof: _Roof, dm: Dormer, rules: Rules) -> tuple[float, float]:
    """(foot, top) of the front wall: the foot at the lower roof surface of both front corners."""
    thick = float(rules.get("roof", "thicknessM"))
    v = roof.mid + dm.side * dm.d_front
    foot = min(_main_top(roof, thick, u, v) for u in (dm.u - dm.w / 2, dm.u + dm.w / 2))
    return foot, foot + float(rules.get("dormers", "frontM"))


def dormer_depth(roof: _Roof, dm: Dormer, rules: Rules) -> float:
    """Depth at which the top of the dormer roof has dived under the main roof, plus 0.1 m."""
    d = rules.get("dormers")
    thick = float(rules.get("roof", "thicknessM"))
    t, ov = float(d["roofThicknessM"]), float(d["overhangM"])
    _, yt = _dormer_levels(roof, dm, rules)
    tan = math.tan(math.radians(dm.pitch))
    h0, g = (yt + dm.w / 2 * tan + t, 0.0) if dm.kind == "giebel" else (yt + t, tan)
    us = (dm.u - dm.w / 2 - ov, dm.u, dm.u + dm.w / 2 + ov)
    return max(_dive(roof, thick, u, dm.side, dm.d_front, h0, g) for u in us) + 0.1


def preferred_side(roof: _Roof, streets: StreetIndex | None, reach: float) -> int:
    """The roof side towards the street (eaves to the street). If both or neither face one (gable
    towards the street), the sunnier side: south first, then west."""
    faces = {}
    for s in (1, -1):
        v = roof.mid + s * roof.half
        a, b = _plan(roof, roof.umin, v), _plan(roof, roof.umax, v)
        faces[s] = bool(streets and streets.faces_street(a, b, (s * roof.v[0], s * roof.v[1]),
                                                         reach))  # fmt: skip
    if faces[1] != faces[-1]:
        return 1 if faces[1] else -1

    def sunny(s: int) -> float:
        return s * roof.v[1] - 0.3 * s * roof.v[0]  # +z is south, -x is west

    return 1 if sunny(1) >= sunny(-1) else -1


def roof_takes_dormers(roof: _Roof, rules: Rules) -> bool:
    d = rules.data.get("dormers")
    m = roof.mass
    return (
        bool(d)
        and m.roof == "saddle"
        and roof.half >= float(d["minRoofDepthM"])
        and (m.ridge_y - m.eave_y >= float(d["minRoofRiseM"]))
    )


def place_dormers(part: _RoofPart, counts: dict[int, int], kind: str, w: float, pitch: float,
                  hatch: bool, chimneys: Sequence[Polygon], others: Sequence[Polygon],
                  rules: Rules) -> list[Dormer]:  # fmt: skip
    """Evenly spaced dormers per side that keep every clearance; the others are left out."""
    d = rules.get("dormers")
    roof = part.roof
    if not roof_takes_dormers(roof, rules):
        return []
    beta = math.atan2(roof.mass.ridge_y - roof.mass.eave_y, roof.half)
    d_front = roof.half - float(d["clearEaveM"]) * math.cos(beta)
    ridge_clear = float(d["clearRidgeM"]) * math.cos(beta)
    gable, gap, ov = float(d["clearGableM"]), float(d["clearEachOtherM"]), float(d["overhangM"])
    inside = part.top.buffer(1e-6)
    out: list[Dormer] = []
    for side, wanted in sorted(counts.items(), reverse=True):
        room = roof.length - 2 * gable
        n = min(wanted, int((room + gap) // (w + gap))) if room > 0 else 0
        lo, hi = roof.umin + gable + w / 2, roof.umax - gable - w / 2
        placed: list[float] = []
        for k in range(n):
            u = min(hi, max(lo, roof.umin + roof.length * (k + 0.5) / n))
            dm = Dormer(side, u, w, kind, hatch, pitch, d_front)
            depth = dormer_depth(roof, dm, rules)
            if d_front - depth < ridge_clear:
                continue
            front, back = roof.mid + side * (d_front + ov), roof.mid + side * (d_front - depth)
            body = _plan_rect(roof, u - w / 2 - ov, u + w / 2 + ov, min(front, back),
                              max(front, back))  # fmt: skip
            v0, v1 = sorted((roof.mid + side * d_front, back))
            clear = _plan_rect(roof, u - w / 2 - gable, u + w / 2 + gable, v0, v1)
            if (inside.contains(clear)
                    and all(body.distance(o) >= float(d["clearPartsM"]) for o in others)
                    and all(body.distance(c) >= float(d["clearChimneyM"]) for c in chimneys)
                    and all(abs(u - pu) - w >= gap - 1e-9 for pu in placed)):  # fmt: skip
                placed.append(u)
                out.append(Dormer(side, u, w, kind, hatch, pitch, d_front, depth))
    return out


@dataclass(frozen=True)
class DormerChoice:
    wanted: bool  # the house gets dormers (share of its style)
    kind: str
    w: float
    pitch: float
    hatch: bool
    one_side: bool  # only on the preferred side


def dormer_choice(bid: str, seed: int | None, style: str, rules: Rules) -> DormerChoice:
    """Seeded dormer decisions of a house; every draw is always made (stable across levels)."""
    d = rules.get("dormers")
    rng = _rng(bid, seed, ":dormers")
    wanted = rng.random() < float(d["shareByStyle"].get(style, 0.0))
    kind = _weighted(rng, d["types"])
    w = rng.uniform(*(float(x) for x in d["widthM"]))
    lo, hi = (float(x) for x in d["schleppPitchDeg"])
    pitch = rng.uniform(lo, hi) if kind == "schlepp" else float(d["giebelPitchDeg"])
    hatch = rng.random() < float(d["hatchShareByStyle"].get(style, 0.0))
    one_side = rng.random() < float(d["streetSideOnly"])
    return DormerChoice(wanted, kind, w, pitch, hatch, one_side)


def _roof_face(ctx: _Context, pts: list[tuple[float, float, float]], up: bool) -> None:
    a, c, e = (np.asarray(p) for p in pts[:3])
    normal = np.cross(c - a, e - a)
    if normal[1] < 0:
        normal = -normal
    length = float(np.linalg.norm(normal)) or 1.0
    north = up and normal[2] / length < NORTH_ROOF
    b = ctx.builders["roof_north" if north else "roof"]
    b.polygon(pts, [(x - b.ox, z - b.oz) for x, _, z in pts], (0.0, 1.0 if up else -1.0, 0.0))


def _dormer(ctx: _Context, roof: _Roof, dm: Dormer) -> None:
    """Front wall with window or hatch, cheeks down to the main roof, and a dormer roof that dives
    under the main roof at the back: no hole in the main roof, no coplanar faces."""
    rules = ctx.rules
    d = rules.get("dormers")
    thick = float(rules.get("roof", "thicknessM"))
    t, ov, height = float(d["roofThicknessM"]), float(d["overhangM"]), float(d["frontM"])
    s = dm.side
    u0, u1 = dm.u - dm.w / 2, dm.u + dm.w / 2
    foot, yt = _dormer_levels(roof, dm, rules)
    tan = math.tan(math.radians(dm.pitch))
    v_front = roof.mid + s * dm.d_front

    def p(u: float, ds: float, y: float) -> tuple[float, float, float]:
        x, z = _plan(roof, u, roof.mid + s * (dm.d_front - ds))
        return (x, y, z)

    def under_main(u: float, ds: float) -> float:
        return roof.height(*_plan(roof, u, roof.mid + s * (dm.d_front - ds)))

    wall = ctx.builders["infill" if ctx.style.timber else "wall_ground"]
    out_n = (s * roof.v[0], s * roof.v[1])
    f = make_frame(_plan(roof, u0, v_front), _plan(roof, u1, v_front), out_n, foot)
    below = thick + 0.05
    outline = [(0.0, -below), (dm.w, -below), (dm.w, height)]
    if dm.kind == "giebel":
        outline.append((dm.w / 2, height + dm.w / 2 * tan))
    outline.append((0.0, height))
    o = d["hatch" if dm.hatch else "window"]
    ow = min(float(o["w"]), dm.w - 0.3)
    op = Opening("gate" if dm.hatch else "window", (dm.w - ow) / 2, float(o["sill"]), ow,
                 float(o["h"]))  # fmt: skip
    _wall(wall, f, outline, [op])
    _reveal(ctx.builders["frame"], f, op, float(rules.get("openings", "revealM")))

    g = tan if dm.kind == "schlepp" else 0.0  # slope of the cheeks' top edge
    for u, sign in ((u0, -1.0), (u1, 1.0)):
        sc = min(dm.depth, _dive(roof, thick, u, s, dm.d_front, yt, g) + 0.05)
        quad = [p(u, 0.0, under_main(u, 0.0)), p(u, 0.0, yt), p(u, sc, yt + g * sc),
                p(u, sc, under_main(u, sc))]  # fmt: skip
        wall.polygon(quad, [(0, 0), (0, 1), (1, 1), (1, 0)],
                     (roof.u[0] * sign, 0.0, roof.u[1] * sign))  # fmt: skip

    s0, s1 = -ov, dm.depth
    # Roof planes as (underside quad, side edges (i, j, sign) that get a fascia).
    if dm.kind == "schlepp":
        a, b = u0 - ov, u1 + ov
        under = [p(a, s0, yt + s0 * tan), p(b, s0, yt + s0 * tan), p(b, s1, yt + s1 * tan),
                 p(a, s1, yt + s1 * tan)]  # fmt: skip
        planes = [(under, ((0, 3, -1.0), (1, 2, 1.0)))]
    else:
        ridge, eave = yt + dm.w / 2 * tan, yt - ov * tan
        planes = []
        for ue, sign in ((u0 - ov, -1.0), (u1 + ov, 1.0)):
            under = [p(ue, s0, eave), p(dm.u, s0, ridge), p(dm.u, s1, ridge), p(ue, s1, eave)]
            planes.append((under, ((0, 3, sign),)))
    fascia = ctx.builders["roof"]
    for under, edges in planes:
        top = [(x, y + t, z) for x, y, z in under]
        _roof_face(ctx, top, up=True)
        _roof_face(ctx, under, up=False)
        fascia.polygon([under[0], under[1], top[1], top[0]], [(0, 0), (1, 0), (1, t), (0, t)],
                       (out_n[0], 0.0, out_n[1]))  # fmt: skip
        for i, j, sign in edges:
            fascia.polygon([under[i], under[j], top[j], top[i]], [(0, 0), (1, 0), (1, t), (0, t)],
                           (roof.u[0] * sign, 0.0, roof.u[1] * sign))  # fmt: skip


def _roof_features(ctx: _Context, bid: str, override: Any,  # noqa: ANN401
                   streets: StreetIndex | None, hearth: bool, with_dormers: bool,
                   notes: list[str]) -> tuple[int, int]:  # fmt: skip
    """Chimneys and dormers on the largest saddle roof of the house; returns their numbers."""
    rules = ctx.rules
    parts = [r for r in ctx.roofs if r.roof.mass.roof == "saddle" and r.roof.half > 1e-6]
    if not parts:
        return 0, 0
    main = max(parts, key=lambda r: r.top.area)
    others = [r.footprint for r in ctx.roofs if r is not main]
    seed = getattr(override, "seed", None)
    rng = _rng(bid, seed, ":chimneys")
    n = chimney_count(ctx.style, main.roof.length, rules, override, hearth)
    spots = place_chimneys(main, n, others, rules, rng)
    for u, v in spots:
        _chimney(ctx.builders["chimney"], main.roof, u, v, rules, rng)
    d = rules.data.get("dormers")
    if not d:
        return 0, len(spots)
    ch = dormer_choice(bid, seed, ctx.style.style, rules)
    wanted = getattr(override, "dormers", None)
    if not with_dormers or (wanted is None and not ch.wanted) or wanted == 0:
        return 0, len(spots)
    if not roof_takes_dormers(main.roof, rules):
        if wanted:
            notes.append("dormers override ignored (roof too small or too flat)")
        return 0, len(spots)
    most = int(d["maxPerSide"])
    pref = preferred_side(main.roof, streets, float(rules.get("jetty", "streetReachM")))
    inside_only = False
    if ctx.style.wall_house and ctx.wall is not None:  # no dormers on the town wall side
        um = (main.roof.umin + main.roof.umax) / 2
        side_in = [s for s in (1, -1) if ctx.wall.ring.contains(
            Point(*_plan(main.roof, um, main.roof.mid + s * (main.roof.half + 3.0))))]  # fmt: skip
        if len(side_in) == 1:
            pref, inside_only = side_in[0], True
    if wanted is not None:
        counts = {pref: min(wanted, most), -pref: min(max(0, wanted - most), most)}
    else:
        per_side = min(most, math.ceil(main.roof.length / float(d["perEaveM"])))
        counts = {pref: per_side} if ch.one_side else {pref: per_side, -pref: per_side}
    if inside_only:
        counts = {pref: counts.get(pref, 0) + counts.get(-pref, 0)}
    rects = [chimney_rect(main.roof, u, v, rules) for u, v in spots]
    dormers = place_dormers(main, {k: v for k, v in counts.items() if v > 0}, ch.kind, ch.w,
                            ch.pitch, ch.hatch, rects, others, rules)  # fmt: skip
    for dm in dormers:
        _dormer(ctx, main.roof, dm)
    return len(dormers), len(spots)
