"""City wall with flank towers, gate towers and posterns (W6, ``gothar-worldgen citywall``).

Course: a closed ring from OSM ``city_wall`` pieces and hand-set points, plus open Zwinger walls
(``data/<site>/city_wall.json``, freely interpreted). Where houses stand on the line today, the wall
is left out and its ends reach ``stossM`` into the houses. Rubble stone, an open wall-walk behind a
crenellated parapet, square flank towers (the walk passes through them), gate towers with a
pointed-arch passage, posterns with a lintel, stone stairs up to the walk next to the gate towers.

The crown follows the smoothed terrain (``smoothM``), the foot lies ``baseSinkM`` below the lowest
ground. Collision (``COL_HULL_``, contract in docs/modules/asset.md): wall body up to the walk,
parapet up to its top (climbable, merlons without collision), towers around their passage, gate
towers around the passage, stairs as a ramp through the nosings.

Coordinates: local metres, +X east, +Y up, +Z south; the ring is counter-clockwise in (x, z)
maths sense, so the right-hand side of the walking direction is outside.
"""

from __future__ import annotations

import hashlib
import json
import math
import random
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from shapely.geometry import LineString, Point, Polygon
from shapely.strtree import STRtree

from gothar_worldgen.buildings.gltf import CollisionPart, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import _Builder
from gothar_worldgen.buildings.medieval import (
    Opening,
    Rules,
    WallContext,
    _reveal,
    _wall,
    make_frame,
)

Height = Callable[[float, float], float]
ROLES = ("wall", "roof", "frame", "timber")
INDEX_FORMAT = "gothar-citywall-index"
INDEX_VERSION = 1
COURSE_FORMAT = "gothar-city-wall"
SAMPLE_M = 0.25  # occupancy sampling along the ring
GRID_M = 0.5  # terrain sampling along the ring


class CourseError(Exception):
    """Invalid course annotation; the message is meant for the user."""


# --- course ----------------------------------------------------------------------------------


@dataclass(frozen=True)
class Gate:
    key: str
    kind: str  # tower | pforte
    name: str
    at: tuple[float, float]
    street: str = ""


@dataclass
class Course:
    ring: list[tuple[float, float]]  # closed (no repeated end), counter-clockwise in (x, z)
    zwinger: list[list[tuple[float, float]]]
    gates: list[Gate]


def _shoelace(pts: Sequence[tuple[float, float]]) -> float:
    return sum(pts[i - 1][0] * pts[i][1] - pts[i][0] * pts[i - 1][1] for i in range(len(pts))) / 2


def load_course(doc: dict[str, Any], features: Sequence[dict[str, Any]]) -> Course:
    if doc.get("format") != COURSE_FORMAT or doc.get("version") != 1:
        raise CourseError(f"not a {COURSE_FORMAT} v1 document")
    osm = {f["osmId"]: [(float(x), float(z)) for x, z in f["points"]]
           for f in features if f.get("geometry") == "line" and f.get("osmId")}  # fmt: skip

    def points(piece: dict[str, Any]) -> list[tuple[float, float]]:
        if "osm" in piece:
            if piece["osm"] not in osm:
                raise CourseError(f"OSM way {piece['osm']} not in features.json")
            return list(osm[piece["osm"]])
        pts = piece.get("points")
        if not isinstance(pts, list) or not pts:
            raise CourseError("a ring piece needs 'osm' or 'points'")
        return [(float(p[0]), float(p[1])) for p in pts]

    pieces = [(points(p), "osm" in p) for p in doc.get("ring", [])]
    if not pieces:
        raise CourseError("empty ring")
    if len(pieces) > 1 and pieces[0][1]:  # orient the first OSM piece towards the second
        first, nxt = pieces[0][0], pieces[1]
        ends = [nxt[0][0], nxt[0][-1]] if nxt[1] else [nxt[0][0]]
        if min(math.dist(first[0], e) for e in ends) < min(math.dist(first[-1], e) for e in ends):
            pieces[0] = (first[::-1], True)
    ring: list[tuple[float, float]] = []
    for pts, is_osm in pieces:
        if ring and is_osm and math.dist(ring[-1], pts[-1]) < math.dist(ring[-1], pts[0]):
            pts = pts[::-1]
        for p in pts:
            if not ring or math.dist(ring[-1], p) > 0.5:
                ring.append(p)
    if len(ring) > 2 and math.dist(ring[0], ring[-1]) <= 0.5:
        ring.pop()
    if len(ring) < 3 or not Polygon(ring).is_valid:
        raise CourseError("the ring does not form a simple closed outline")
    if _shoelace(ring) < 0:
        ring.reverse()
    zwinger = [points(p) for p in doc.get("zwinger", [])]
    gates = []
    for g in doc.get("gates", []):
        if g.get("kind") not in ("tower", "pforte") or not g.get("key"):
            raise CourseError(f"gate {g.get('key')}: kind must be tower or pforte")
        at = (float(g["at"][0]), float(g["at"][1]))
        name = str(g.get("name") or g["key"])
        gates.append(Gate(str(g["key"]), g["kind"], name, at, str(g.get("street", ""))))
    return Course(ring, zwinger, gates)


# --- ring geometry ---------------------------------------------------------------------------


class Path2:
    """Polyline parametrised by arc length ``s`` (closed: s wraps, starting at ``start``)."""

    def __init__(
        self, pts: Sequence[tuple[float, float]], closed: bool, start: float = 0.0
    ) -> None:
        p = np.asarray(pts, dtype=np.float64)
        self.closed = closed
        self.pts = np.vstack([p, p[:1]]) if closed else p
        seg = np.diff(self.pts, axis=0)
        self.len = np.hypot(seg[:, 0], seg[:, 1])
        keep = self.len > 1e-9
        self.pts = np.vstack([self.pts[:1], self.pts[1:][keep]])
        seg = np.diff(self.pts, axis=0)
        self.len = np.hypot(seg[:, 0], seg[:, 1])
        self.dirs = seg / self.len[:, None]
        self.cum = np.concatenate([[0.0], np.cumsum(self.len)])
        self.length = float(self.cum[-1])
        self.start = start % self.length if closed else 0.0

    def _raw(self, s: float) -> float:
        return (s + self.start) % self.length if self.closed else min(max(s, 0.0), self.length)

    def _seg(self, raw: float, before: bool = False) -> int:
        i = int(np.searchsorted(self.cum, raw, side="left" if before else "right")) - 1
        n = len(self.dirs)
        if self.closed:
            return i % n
        return min(max(i, 0), n - 1)

    def point(self, s: float) -> np.ndarray:
        raw = self._raw(s)
        i = self._seg(raw)
        return self.pts[i] + self.dirs[i] * (raw - self.cum[i])

    def direction(self, s: float, before: bool = False) -> np.ndarray:
        return self.dirs[self._seg(self._raw(s), before)]

    @staticmethod
    def outward(d: np.ndarray) -> np.ndarray:
        return np.array([d[1], -d[0]])

    def vertices_between(self, s0: float, s1: float) -> list[float]:
        """Arc lengths (in this path's s) of the polyline vertices strictly inside (s0, s1)."""
        out = []
        for c in self.cum[:-1] if self.closed else self.cum[1:-1]:
            s = (c - self.start) % self.length if self.closed else c
            for k in (-1, 0, 1):
                t = s + k * self.length if self.closed else s
                if s0 + 1e-6 < t < s1 - 1e-6:
                    out.append(float(t))
        return sorted(out)

    def miter(self, s: float) -> np.ndarray:
        """Offset vector for +1 m outwards at s (mitred at vertices, clamped)."""
        a, b = self.outward(self.direction(s, before=True)), self.outward(self.direction(s))
        if np.allclose(a, b):
            return b
        m = (a + b) / max(1.0 + float(np.dot(a, b)), 1e-3)
        n = float(np.linalg.norm(m))
        return m * (3.0 / n) if n > 3.0 else m

    def bend_deg(self, s: float) -> float:
        a, b = self.direction(s, before=True), self.direction(s)
        return math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(a, b))))))


# --- terrain along a path --------------------------------------------------------------------


class Profile:
    """Ground along a path: centre, lowest and highest across the wall, smoothed crown."""

    def __init__(self, path: Path2, height: Height, half: float, smooth: float) -> None:
        self.path = path
        self.height = height
        n = max(2, int(math.ceil(path.length / GRID_M)) + 1)
        self.s = np.linspace(0.0, path.length, n)
        centre, lo, hi = [], [], []
        for s in self.s:
            p, m = path.point(s), path.outward(path.direction(s))
            hs = [height(*(p + m * o)) for o in (-half, 0.0, half)]
            centre.append(hs[1])
            lo.append(min(hs))
            hi.append(max(hs))
        self.centre, self.lo, self.hi = np.array(centre), np.array(lo), np.array(hi)
        k = max(1, int(round(smooth / GRID_M / 2)))
        if path.closed:
            ext = np.concatenate([self.centre[-k - 1 : -1], self.centre, self.centre[1 : k + 1]])
            self.smooth = np.convolve(ext, np.ones(2 * k + 1) / (2 * k + 1), mode="valid")
        else:
            pad = np.pad(self.centre, k, mode="edge")
            self.smooth = np.convolve(pad, np.ones(2 * k + 1) / (2 * k + 1), mode="valid")

    def _at(self, arr: np.ndarray, s: float) -> float:
        if self.path.closed:
            s %= self.path.length
        return float(np.interp(s, self.s, arr))

    def ground(self, s: float) -> float:
        return self._at(self.centre, s)

    def crown(self, s: float) -> float:
        return self._at(self.smooth, s)

    def highest(self, s: float) -> float:
        return self._at(self.hi, s)

    def min_ground(self, points: Sequence[np.ndarray]) -> float:
        return min(self.height(float(p[0]), float(p[1])) for p in points)

    def lowest(self, s0: float, s1: float) -> float:
        ss = np.linspace(s0, s1, max(2, int(abs(s1 - s0) / GRID_M) + 2))
        return min(self._at(self.lo, s) for s in ss)


# --- output pieces ---------------------------------------------------------------------------


@dataclass
class Piece:
    """One output file: mesh roles plus collision bodies, relative to ``origin``."""

    key: str
    kind: str  # wall | gate | zwinger
    origin: tuple[float, float, float]
    builders: dict[str, _Builder] = field(default_factory=dict)
    collision: list[CollisionPart] = field(default_factory=list)
    length: float = 0.0
    towers: int = 0
    merlons: int = 0
    notes: list[str] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.builders = {r: _Builder(self.origin) for r in ROLES}

    def primitives(self, rules: Rules) -> list[Primitive]:
        mats = rules.get("cityWall", "materials")
        return [Primitive(mats[r], rules.color(mats[r]), self.builders[r].mesh())
                for r in ROLES if self.builders[r].idx]  # fmt: skip

    def body(self, bottom: Sequence[Sequence[float]], top: Sequence[Sequence[float]]) -> None:
        """Closed prism collision body from a bottom and a top ring of 3D points (same order)."""
        n = len(bottom)
        ring = [(p[0], p[2]) for p in bottom]
        if _shoelace(ring) < 0:
            bottom, top = list(bottom)[::-1], list(top)[::-1]
        pts = [tuple(p) for p in bottom] + [tuple(p) for p in top]
        tris: list[tuple[int, int, int]] = [(0, k, k + 1) for k in range(1, n - 1)]
        for i in range(n):
            j = (i + 1) % n
            tris += [(i, n + i, n + j), (i, n + j, j)]
        tris += [(n, n + k + 1, n + k) for k in range(1, n - 1)]
        pos = np.round(np.asarray(pts) - np.asarray(self.origin), 4).astype(np.float32)
        idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
        self.collision.append(CollisionPart(f"COL_HULL_{len(self.collision)}", pos, idx))

    @property
    def collision_triangles(self) -> int:
        return sum(c.triangle_count for c in self.collision)


def _p3(p2: np.ndarray, y: float) -> tuple[float, float, float]:
    return (float(p2[0]), float(y), float(p2[1]))


def _quad(b: _Builder, pts: Sequence[tuple[float, float, float]], want: Sequence[float]) -> None:
    b.polygon(list(pts), [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)][: len(pts)],
              (float(want[0]), float(want[1]), float(want[2])))  # fmt: skip


# --- wall strips -----------------------------------------------------------------------------


Row = tuple[float, np.ndarray, np.ndarray, float, float]  # s, point, miter, base, walk


@dataclass
class WallSpec:
    """Cross-section of a strip: offsets (outwards positive) and heights above the crown."""

    inner: float
    outer: float
    parapet: float | None  # offset of the parapet's inner face; None = no walk, flat top
    walk_above: float  # walk (or flat top) above the smoothed ground
    parapet_high: float = 0.0
    min_above: float = 2.5
    sink: float = 1.0


def _samples(path: Path2, s0: float, s1: float, piece: float) -> list[float]:
    marks = [s0, *path.vertices_between(s0, s1), s1]
    out = [s0]
    for a, b in zip(marks, marks[1:], strict=False):
        n = max(1, int(math.ceil((b - a) / piece - 1e-9)))
        out += [a + (b - a) * k / n for k in range(1, n + 1)]
    return out


def walk_height(prof: Profile, spec: WallSpec, s: float) -> float:
    return max(prof.crown(s) + spec.walk_above, prof.highest(s) + spec.min_above)


def wall_strip(piece: Piece, path: Path2, prof: Profile, spec: WallSpec, s0: float, s1: float,
               step: float, caps: tuple[bool, bool] = (True, True),
               lintel: float | None = None) -> list[Row]:  # fmt: skip
    """Faces of the wall between s0 and s1; returns samples (s, point, miter, base, walk).

    ``lintel``: a postern's lintel block: the strip starts at this height and gets a bottom face.
    """
    b = piece.builders["wall"]
    ss = _samples(path, s0, s1, step)
    rows = []
    for i, s in enumerate(ss):
        p = path.point(s)
        if i == 0:
            m = path.outward(path.direction(s))
        elif i == len(ss) - 1:
            m = path.outward(path.direction(s, before=True))
        else:
            m = path.miter(s)
        base = (
            lintel if lintel is not None else (prof.lowest(s - step / 2, s + step / 2) - spec.sink)
        )
        rows.append((s, p, m, base, walk_height(prof, spec, s)))
    top_off = spec.parapet_high if spec.parapet is not None else 0.0
    for (sa, pa, ma, ba, wa), (sb, pb, mb, bb, wb) in zip(rows, rows[1:], strict=False):
        n = path.outward(path.direction((sa + sb) / 2))
        out, inn = (n[0], 0.0, n[1]), (-n[0], 0.0, -n[1])

        def at(p: np.ndarray, m: np.ndarray, o: float, y: float) -> tuple[float, float, float]:
            return _p3(p + m * o, y)

        o, i = spec.outer, spec.inner
        _quad(b, [at(pa, ma, o, ba), at(pb, mb, o, bb), at(pb, mb, o, wb + top_off),
                  at(pa, ma, o, wa + top_off)], out)  # fmt: skip
        _quad(b, [at(pa, ma, i, ba), at(pb, mb, i, bb), at(pb, mb, i, wb), at(pa, ma, i, wa)], inn)
        if spec.parapet is None:
            _quad(b, [at(pa, ma, i, wa), at(pb, mb, i, wb), at(pb, mb, o, wb), at(pa, ma, o, wa)],
                  (0.0, 1.0, 0.0))  # fmt: skip
        else:
            q = spec.parapet
            ht = spec.parapet_high
            _quad(b, [at(pa, ma, i, wa), at(pb, mb, i, wb), at(pb, mb, q, wb), at(pa, ma, q, wa)],
                  (0.0, 1.0, 0.0))  # fmt: skip
            _quad(b, [at(pa, ma, q, wa), at(pb, mb, q, wb), at(pb, mb, q, wb + ht),
                      at(pa, ma, q, wa + ht)], inn)  # fmt: skip
            _quad(b, [at(pa, ma, q, wa + ht), at(pb, mb, q, wb + ht), at(pb, mb, o, wb + ht),
                      at(pa, ma, o, wa + ht)], (0.0, 1.0, 0.0))  # fmt: skip
        if lintel is not None:
            _quad(b, [at(pa, ma, i, ba), at(pb, mb, i, bb), at(pb, mb, o, bb), at(pa, ma, o, ba)],
                  (0.0, -1.0, 0.0))  # fmt: skip
    for k, (enabled, row, sign) in enumerate(((caps[0], rows[0], -1.0), (caps[1], rows[-1], 1.0))):
        if not enabled:
            continue
        s, p, m, base, walk = row
        d = path.direction(s, before=k == 1) * sign
        want = (d[0], 0.0, d[1])
        _quad(b, [_p3(p + m * spec.inner, base), _p3(p + m * spec.outer, base),
                  _p3(p + m * spec.outer, walk), _p3(p + m * spec.inner, walk)], want)  # fmt: skip
        if spec.parapet is not None:
            _quad(b, [_p3(p + m * spec.parapet, walk), _p3(p + m * spec.outer, walk),
                      _p3(p + m * spec.outer, walk + spec.parapet_high),
                      _p3(p + m * spec.parapet, walk + spec.parapet_high)], want)  # fmt: skip
    return rows


def strip_collision(piece: Piece, path: Path2, spec: WallSpec, rows: Sequence[tuple],
                    max_len: float = 12.0, max_bend: float = 5.0) -> None:  # fmt: skip
    """Boxes per nearly straight run of samples: wall body up to the walk, parapet up to its
    top (no merlons: the parapet stays climbable)."""
    start = 0
    for k in range(1, len(rows)):
        s0 = rows[start][0]
        last = k == len(rows) - 1
        bend = path.bend_deg(rows[k][0]) if not last else 0.0
        if not last and rows[k + 1][0] - s0 <= max_len and bend <= max_bend:
            continue
        a, b = rows[start], rows[k]
        n = path.outward(path.direction((a[0] + b[0]) / 2))
        base = min(r[3] for r in rows[start : k + 1])

        body = (spec.inner, spec.outer)
        piece.body(_band(a[1], b[1], n, body, base, base), _band(a[1], b[1], n, body, a[4], b[4]))
        if spec.parapet is not None:
            par, ht = (spec.parapet, spec.outer), spec.parapet_high
            piece.body(_band(a[1], b[1], n, par, a[4] - 0.01, b[4] - 0.01),
                       _band(a[1], b[1], n, par, a[4] + ht, b[4] + ht))  # fmt: skip
        start = k


def _band(pa: np.ndarray, pb: np.ndarray, n: np.ndarray, offs: tuple[float, float], ya: float,
          yb: float) -> list[tuple[float, float, float]]:  # fmt: skip
    """Four corners of a straight wall band between two ring points at heights ya / yb."""
    lo, hi = offs
    return [_p3(pa + n * lo, ya), _p3(pb + n * lo, yb), _p3(pb + n * hi, yb), _p3(pa + n * hi, ya)]


def merlons(piece: Piece, path: Path2, prof: Profile, spec: WallSpec, rules: Rules, s0: float,
            s1: float, skip: Sequence[tuple[float, float]], seed_key: str) -> int:  # fmt: skip
    """Merlons on the parapet; about ``missingMerlons`` of them are gone (aging)."""
    m = rules.get("cityWall", "merlon")
    w, gap, h = float(m["w"]), float(m["gap"]), float(m["h"])
    missing = float(rules.get("cityWall", "missingMerlons"))
    b = piece.builders["wall"]
    count = 0
    s = s0 + 0.4 + w / 2
    while s + w / 2 <= s1 - 0.4:
        sa, sb = s - w / 2, s + w / 2
        rng = random.Random(
            int(hashlib.sha256(f"{seed_key}:{s:.2f}".encode()).hexdigest()[:12], 16)
        )
        lost = rng.random() < missing
        bent = any(path.bend_deg(v) > 10.0 for v in path.vertices_between(sa, sb))
        blocked = any(lo - 0.1 < s < hi + 0.1 or lo < sa < hi or lo < sb < hi for lo, hi in skip)
        if not (lost or bent or blocked):
            n = path.outward(path.direction(s))
            d = path.direction(s)
            pa, pb = path.point(sa), path.point(sb)
            low = min(walk_height(prof, spec, sa), walk_height(prof, spec, sb))
            y0 = low + spec.parapet_high - 0.05
            y1 = walk_height(prof, spec, s) + spec.parapet_high + h
            q, o = float(spec.parapet or 0.0), spec.outer
            corners = [pa + n * q, pb + n * q, pb + n * o, pa + n * o]
            outn, inn = (n[0], 0.0, n[1]), (-n[0], 0.0, -n[1])
            _quad(b, [_p3(corners[3], y0), _p3(corners[2], y0), _p3(corners[2], y1),
                      _p3(corners[3], y1)], outn)  # fmt: skip
            _quad(b, [_p3(corners[0], y0), _p3(corners[1], y0), _p3(corners[1], y1),
                      _p3(corners[0], y1)], inn)  # fmt: skip
            _quad(b, [_p3(c, y1) for c in corners], (0.0, 1.0, 0.0))
            _quad(b, [_p3(corners[0], y0), _p3(corners[3], y0), _p3(corners[3], y1),
                      _p3(corners[0], y1)], (-d[0], 0.0, -d[1]))  # fmt: skip
            _quad(b, [_p3(corners[1], y0), _p3(corners[2], y0), _p3(corners[2], y1),
                      _p3(corners[1], y1)], (d[0], 0.0, d[1]))  # fmt: skip
            count += 1
        s += w + gap
    return count


# --- towers ----------------------------------------------------------------------------------


class _Frame3:
    """Local frame at a ring position: u along the wall, n outwards, plus a ground height."""

    def __init__(self, path: Path2, s: float) -> None:
        self.c = path.point(s)
        self.u = path.direction(s)
        self.n = path.outward(self.u)

    def p(self, a: float, o: float) -> np.ndarray:
        return self.c + self.u * a + self.n * o


def _face(piece: Piece, role: str, a: np.ndarray, b: np.ndarray, normal: np.ndarray, y0: float,
          outline: Sequence[tuple[float, float]] | Polygon,
          holes: Sequence[Opening] = ()) -> None:  # fmt: skip
    """Vertical wall polygon on the line a-b (facade coordinates via medieval.make_frame)."""
    f = make_frame(a, b, normal, y0)
    _wall(piece.builders[role], f, outline, list(holes))
    for op in holes:
        _reveal(piece.builders["frame"], f, op, 0.15)


def _pyramid(piece: Piece, corners: Sequence[np.ndarray], eave: float, rise: float) -> None:
    c = sum(corners) / len(corners)
    apex = _p3(c, eave + rise)
    for i, a in enumerate(corners):
        b = corners[(i + 1) % len(corners)]
        piece.builders["roof"].polygon([_p3(a, eave), _p3(b, eave), apex],
                                       [(0, 0), (1, 0), (0.5, 1)], (0.0, 1.0, 0.0))  # fmt: skip


def flank_tower(piece: Piece, path: Path2, prof: Profile, spec: WallSpec, rules: Rules,
                s: float) -> None:  # fmt: skip
    """Square tower projecting outwards; the walk passes through it in a vaulted passage."""
    t = rules.get("cityWall", "tower")
    al, out = float(t["alongM"]), float(t["outM"])
    f = _Frame3(path, s)
    lo_off, hi_off = spec.inner, spec.outer + out
    corners = [f.p(-al / 2, lo_off), f.p(al / 2, lo_off), f.p(al / 2, hi_off), f.p(-al / 2, hi_off)]
    base = min(prof.lowest(s - al / 2, s + al / 2), prof.min_ground([*corners, f.c])) - spec.sink
    walks = [walk_height(prof, spec, x) for x in (s - al / 2, s, s + al / 2)]
    floor, ceil = min(walks) - 0.05, max(walks) + float(t["passageH"])
    eave = prof.crown(s) + float(t["heightM"])
    slit = t["slit"]
    sw, sh = float(slit["w"]), float(slit["h"])
    h = eave - base
    mid = (prof.crown(s) - base) + 4.0
    # Outer face with two slits.
    _face(piece, "wall", corners[3], corners[2], f.n, base, [(0, 0), (al, 0), (al, h), (0, h)],
          [Opening("window", al / 2 - sw / 2, mid, sw, sh),
           Opening("window", al / 2 - sw / 2, min(h - 1.5, mid + 3.0), sw, sh)])  # fmt: skip
    # Inner face (town side), solid.
    _face(piece, "wall", corners[0], corners[1], -f.n, base, [(0, 0), (al, 0), (al, h), (0, h)])
    # Flanks: a slit on the outer part and the doorway of the walk passage.
    depth = hi_off - lo_off
    door_lo, door_hi = 0.0, (spec.parapet or spec.outer) - 0.05 - lo_off
    for sign, a, b in ((-1.0, corners[0], corners[3]), (1.0, corners[1], corners[2])):
        normal = f.u * sign
        frame = make_frame(a, b, normal, base)
        outline = Polygon([(0, 0), (depth, 0), (depth, h), (0, h)])
        # Door: facade u runs from left to right seen from outside; map the walk offsets.
        left_is_inner = math.dist(frame.left, a) < 1e-6
        u0, u1 = (door_lo, door_hi) if left_is_inner else (depth - door_hi, depth - door_lo)
        hole = Polygon([(u0, floor - base), (u1, floor - base), (u1, ceil - base),
                        (u0, ceil - base)])  # fmt: skip
        su = depth - 1.5 if left_is_inner else 1.0
        slit_op = Opening("window", su, mid, sw, sh)
        _wall(piece.builders["wall"], frame, outline.difference(hole), [slit_op])
        _reveal(piece.builders["frame"], frame, slit_op, 0.15)
    # Passage: ceiling and two side faces.
    q = (spec.parapet or spec.outer) - 0.05
    b = piece.builders["wall"]
    pa0, pa1 = f.p(-al / 2, lo_off), f.p(al / 2, lo_off)
    pq0, pq1 = f.p(-al / 2, q), f.p(al / 2, q)
    _quad(b, [_p3(pa0, ceil), _p3(pa1, ceil), _p3(pq1, ceil), _p3(pq0, ceil)], (0.0, -1.0, 0.0))
    _quad(b, [_p3(pa0, floor), _p3(pa1, floor), _p3(pa1, ceil), _p3(pa0, ceil)],
          (f.n[0], 0.0, f.n[1]))  # fmt: skip
    _quad(b, [_p3(pq0, floor), _p3(pq1, floor), _p3(pq1, ceil), _p3(pq0, ceil)],
          (-f.n[0], 0.0, -f.n[1]))  # fmt: skip
    _pyramid(piece, corners, eave, float(t["roofM"]))
    # Collision: the part outside the wall, and the block above the passage.
    o = spec.outer
    outside = [f.p(-al / 2, o), f.p(al / 2, o), f.p(al / 2, hi_off), f.p(-al / 2, hi_off)]
    piece.body([_p3(p, base) for p in outside], [_p3(p, eave) for p in outside])
    above = [f.p(-al / 2, lo_off), f.p(al / 2, lo_off), f.p(al / 2, o), f.p(-al / 2, o)]
    piece.body([_p3(p, ceil) for p in above], [_p3(p, eave) for p in above])
    piece.towers += 1


def gate_tower(piece: Piece, path: Path2, prof: Profile, rules: Rules, s: float) -> None:
    """Gate tower across the wall with a pointed-arch passage and opened wooden leaves."""
    g = rules.get("cityWall", "gate")
    al, dep = float(g["alongM"]), float(g["depthM"])
    pw, spring, apex = float(g["passageW"]), float(g["springH"]), float(g["apexH"])
    f = _Frame3(path, s)
    corners = [f.p(-al / 2, -dep / 2), f.p(al / 2, -dep / 2), f.p(al / 2, dep / 2),
               f.p(-al / 2, dep / 2)]  # fmt: skip
    ground = min(prof.ground(s), *(prof.ground(s + d) for d in (-pw / 2, pw / 2)))
    base = min(prof.min_ground([*corners, f.c]), ground) - 1.0
    eave = prof.crown(s) + float(g["heightM"])
    h = eave - base
    gy = ground - base  # street level in facade coordinates
    k = 4

    def arch(width: float, left: float) -> list[tuple[float, float]]:
        """Passage outline (facade coordinates): from below ground up to a pointed arch of two
        arcs with their centres on the spring line (needs a rise above half the width)."""
        rise = apex - spring
        r = (width**2 / 4 + rise**2) / width
        top = math.acos(min(1.0, (r - width / 2) / r))
        cx_l, cx_r = left + r, left + width - r
        left_arc = [(cx_l - r * math.cos(top * i / k), gy + spring + r * math.sin(top * i / k))
                    for i in range(k + 1)]  # fmt: skip
        right_arc = [(cx_r + r * math.cos(top * i / k), gy + spring + r * math.sin(top * i / k))
                     for i in range(k - 1, -1, -1)]  # fmt: skip
        return [(left, -1.0), *left_arc, *right_arc, (left + width, -1.0)]

    passage = arch(pw, al / 2 - pw / 2)
    outline = Polygon([(0, 0), (al, 0), (al, h), (0, h)]).difference(Polygon(passage))
    slit = rules.get("cityWall", "tower", "slit")
    sw, sh = float(slit["w"]), float(slit["h"])
    upper = [Opening("window", al / 2 - sw / 2 - 1.5, gy + 8.0, sw, sh),
             Opening("window", al / 2 - sw / 2 + 1.5, gy + 8.0, sw, sh),
             Opening("window", al / 2 - 0.4, gy + 12.0, 0.8, 1.0)]  # fmt: skip
    _face(piece, "wall", corners[3], corners[2], f.n, base, outline, upper)
    _face(piece, "wall", corners[1], corners[0], -f.n, base, outline, upper[2:])
    side = [(0, 0), (dep, 0), (dep, h), (0, h)]
    _face(piece, "wall", corners[0], corners[3], -f.u, base, side,
          [Opening("window", dep / 2 - sw / 2, gy + 10.0, sw, sh)])  # fmt: skip
    _face(piece, "wall", corners[2], corners[1], f.u, base, side,
          [Opening("window", dep / 2 - sw / 2, gy + 10.0, sw, sh)])  # fmt: skip
    # Passage walls and vault, facing inwards; facade x maps to along a = x - al / 2.
    b = piece.builders["wall"]
    ring_pts = passage[1:-1]
    for (xa, ya), (xb, yb) in zip(ring_pts, ring_pts[1:], strict=False):
        pa, pb = xa - al / 2, xb - al / 2
        to_axis, to_spring = -(pa + pb) / 2, gy + spring - (ya + yb) / 2  # towards the centre
        want = (f.u[0] * to_axis, to_spring, f.u[1] * to_axis)
        quad = [_p3(f.p(pa, -dep / 2), base + ya), _p3(f.p(pb, -dep / 2), base + yb),
                _p3(f.p(pb, dep / 2), base + yb), _p3(f.p(pa, dep / 2), base + ya)]  # fmt: skip
        _quad(b, quad, want)
    for sign in (-1.0, 1.0):  # straight side walls from below ground up to the spring line
        a = sign * pw / 2
        quad = [
            _p3(f.p(a, -dep / 2), base - 0.0),
            _p3(f.p(a, dep / 2), base),
            _p3(f.p(a, dep / 2), base + gy + spring),
            _p3(f.p(a, -dep / 2), base + gy + spring),
        ]
        _quad(b, quad, (-f.u[0] * sign, 0.0, -f.u[1] * sign))
    # Opened gate leaves against the passage walls at the town side.
    for sign in (-1.0, 1.0):
        a = sign * (pw / 2 - 0.08)
        leaf = [f.p(a, -dep / 2 + 0.3), f.p(a, -dep / 2 + 0.3 + pw / 2 - 0.1)]
        lb = piece.builders["timber"]
        _quad(lb, [_p3(leaf[0], base + gy), _p3(leaf[1], base + gy),
                   _p3(leaf[1], base + gy + spring), _p3(leaf[0], base + gy + spring)],
              (-f.u[0] * sign, 0.0, -f.u[1] * sign))  # fmt: skip
    _pyramid(piece, corners, eave, float(g["roofM"]))
    # Collision: two piers and the block above the passage.
    for lo, hi in ((-al / 2, -pw / 2), (pw / 2, al / 2)):
        ring = [f.p(lo, -dep / 2), f.p(hi, -dep / 2), f.p(hi, dep / 2), f.p(lo, dep / 2)]
        piece.body([_p3(p, base) for p in ring], [_p3(p, eave) for p in ring])
    ring = [f.p(-pw / 2, -dep / 2), f.p(pw / 2, -dep / 2), f.p(pw / 2, dep / 2),
            f.p(-pw / 2, dep / 2)]  # fmt: skip
    piece.body([_p3(p, base + gy + spring) for p in ring], [_p3(p, eave) for p in ring])


# --- stairs ----------------------------------------------------------------------------------


@dataclass
class Stair:
    s_top: float
    direction: int  # +1: the stair runs towards larger s from its top end
    steps: int
    rise: float  # per step (<= the rule's rise)
    run: float
    top: float  # walk height at the top
    bottom: float  # ground at the foot


def plan_stair(path: Path2, prof: Profile, spec: WallSpec, rules: Rules, s_top: float,
               direction: int) -> Stair:  # fmt: skip
    st = rules.get("cityWall", "stairs")
    rise, run = float(st["rise"]), float(st["run"])
    top = walk_height(prof, spec, s_top)
    bottom = prof.ground(s_top)
    n = 1
    for _ in range(4):  # the foot moves with the length: iterate
        n = max(1, math.ceil((top - bottom) / rise - 1e-9))
        bottom = prof.ground(s_top + direction * n * run)
    n = max(1, math.ceil((top - bottom) / rise - 1e-9))
    return Stair(s_top, direction, n, (top - bottom) / n, run, top, bottom)


def stair_footprint(path: Path2, spec: WallSpec, stair: Stair, width: float) -> Polygon:
    f = _Frame3(path, stair.s_top)
    length = stair.steps * stair.run
    a0, a1 = sorted((0.0, stair.direction * length))
    return Polygon([f.p(a0, spec.inner), f.p(a1, spec.inner), f.p(a1, spec.inner - width),
                    f.p(a0, spec.inner - width)])  # fmt: skip


def build_stair(piece: Piece, path: Path2, prof: Profile, spec: WallSpec, rules: Rules,
                stair: Stair) -> None:  # fmt: skip
    """Solid stone stair along the inner face; collision is a ramp through the nosings."""
    width = float(rules.get("cityWall", "stairs", "width"))
    f = _Frame3(path, stair.s_top)
    d = stair.direction
    length = stair.steps * stair.run
    foot = stair_footprint(path, spec, stair, width)
    base = prof.min_ground([np.asarray(c) for c in foot.exterior.coords[:-1]]) - spec.sink
    b = piece.builders["wall"]
    io, wo = spec.inner, spec.inner - width
    axis = f.u * d
    down = (axis[0], 0.0, axis[1])  # the direction in which the stair descends

    def p(a: float, o: float, y: float) -> tuple[float, float, float]:
        return _p3(f.p(d * a, o), y)

    for j in range(stair.steps):  # j = 0 is the top tread
        y = stair.top - j * stair.rise
        a0, a1 = j * stair.run, (j + 1) * stair.run
        _quad(b, [p(a0, io, y), p(a1, io, y), p(a1, wo, y), p(a0, wo, y)], (0.0, 1.0, 0.0))
        below = stair.top - (j + 1) * stair.rise if j < stair.steps - 1 else base
        _quad(b, [p(a1, io, below), p(a1, wo, below), p(a1, wo, y), p(a1, io, y)], down)
    profile = [(0.0, 0.0), (length, 0.0)]
    for j in range(stair.steps - 1, -1, -1):
        y = stair.top - j * stair.rise - base
        profile += [((j + 1) * stair.run, y), (j * stair.run, y)]
    side = Polygon(profile).buffer(0)
    a_start, a_end = f.p(0.0, wo), f.p(d * length, wo)
    frame = make_frame(a_start, a_end, -f.n, base)
    left_is_top = math.dist(frame.left, a_start) < 1e-6
    if not left_is_top:
        side = Polygon([(length - x, y) for x, y in side.exterior.coords]).buffer(0)
    _wall(b, frame, side, [])
    _quad(b, [p(0.0, io, base), p(0.0, wo, base), p(0.0, wo, stair.top), p(0.0, io, stair.top)],
          (-down[0], 0.0, -down[2]))  # fmt: skip
    # Ramp through the nosings: flat over the top tread (flush with the walk), then down to one
    # rise above the foot (a step the controller takes).
    low = stair.bottom + stair.rise
    along = [(0.0, stair.top), (stair.run, stair.top), (length, low)]
    ring = [(a, wo, y) for a, y in along] + [(a, io, y) for a, y in reversed(along)]
    piece.body([p(a, o, base) for a, o, _ in ring], [p(a, o, y) for a, o, y in ring])


# --- planning --------------------------------------------------------------------------------


@dataclass
class Plan:
    path: Path2
    free: list[tuple[float, float]]  # wall intervals (s) of the ring
    towers: list[float]
    gates: list[tuple[Gate, float]]  # gate, s
    pfortes: list[tuple[Gate, float]]
    stairs: list[tuple[Gate, Stair]]
    notes: list[str]


def _runs(flags: np.ndarray) -> list[tuple[int, int]]:
    """[start, end) index runs of True."""
    runs, start = [], None
    for i, f in enumerate(flags):
        if f and start is None:
            start = i
        elif not f and start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(flags)))
    return runs


def plan_wall(course: Course, footprints: Sequence[Polygon], height: Height,
              rules: Rules) -> tuple[Plan, Profile, WallSpec]:  # fmt: skip
    cw = rules.get("cityWall")
    t, walk, pm = float(cw["thicknessM"]), float(cw["walkM"]), float(cw["parapetM"])
    gt = cw["gate"]
    if float(gt["apexH"]) - float(gt["springH"]) <= float(gt["passageW"]) / 2:
        raise CourseError("cityWall.gate: a pointed arch needs apexH - springH > passageW / 2")
    if abs(walk + pm - t) > 1e-6:
        raise CourseError("cityWall: walkM + parapetM must equal thicknessM")
    base_path = Path2(course.ring, closed=True)
    line = LineString([*course.ring, course.ring[0]])
    towers_g = [g for g in course.gates if g.kind == "tower"]
    start = line.project(Point(towers_g[0].at)) if towers_g else 0.0
    path = Path2(course.ring, closed=True, start=start)
    del base_path
    spec = WallSpec(-t / 2, t / 2, t / 2 - pm, float(cw["heightM"]) - float(cw["parapetHighM"]),
                    float(cw["parapetHighM"]), float(cw["minAboveGroundM"]),
                    float(cw["baseSinkM"]))  # fmt: skip
    prof = Profile(path, height, t / 2, float(cw["smoothM"]))
    notes: list[str] = []

    def s_of(p: tuple[float, float]) -> float:
        return (line.project(Point(p)) - start) % path.length

    gate_al = float(cw["gate"]["alongM"])
    stoss = float(cw["stossM"])
    gates = [(g, s_of(g.at)) for g in towers_g]
    n = int(math.ceil(path.length / SAMPLE_M))
    ss = np.arange(n) * (path.length / n)
    polys = [p for p in footprints if p.is_valid and not p.is_empty]
    # Left out only where the centre line runs through a house; houses that only touch the line
    # stand against the wall (its back is hidden in them).
    tree = STRtree(polys) if polys else None
    pts = [Point(*path.point(s)) for s in ss]
    house = np.zeros(n, dtype=bool)
    if tree is not None:
        hit = tree.query(pts, predicate="intersects")
        house[np.unique(hit[0])] = True
    gate_block = np.zeros(n, dtype=bool)
    for _, sg in gates:
        d = (ss - sg + path.length / 2) % path.length - path.length / 2
        gate_block |= np.abs(d) < gate_al / 2 - stoss
    free = ~(house | gate_block)
    if free.all():
        intervals = [(0.0, path.length)]
    else:
        intervals = []
        for i0, i1 in _runs(free):
            s0, s1 = ss[i0], ss[i1 - 1] + path.length / n
            if i0 > 0 and house[i0 - 1]:
                s0 -= t / 2 + stoss  # ends reach into the house
            if i1 < n and house[i1 % n] or i1 == n and house[0]:
                s1 += t / 2 + stoss
            if s1 - s0 >= 0.6:
                intervals.append((s0, s1))
    intervals = _bridge_open_gaps(path, intervals, _solids(path, polys, gates, cw), t)
    pfortes = []
    for g in (g for g in course.gates if g.kind == "pforte"):
        s = s_of(g.at)
        w = float(cw["pforte"]["w"])
        inside = [iv for iv in intervals if iv[0] + 0.5 < s - w / 2 and s + w / 2 < iv[1] - 0.5]
        if inside:
            pfortes.append((g, s))
        else:
            notes.append(f"pforte {g.key}: the wall line is built over here, left out")
    # Flank towers: at bends, then so that towers stand about every everyM.
    tw = cw["tower"]
    al, every = float(tw["alongM"]), float(tw["everyM"])
    reserved = [(sg - gate_al / 2 - 1.0, sg + gate_al / 2 + 1.0) for _, sg in gates]
    reserved += [(s - float(cw["pforte"]["w"]) / 2 - 1.5, s + float(cw["pforte"]["w"]) / 2 + 1.5)
                 for _, s in pfortes]  # fmt: skip
    out_tree = STRtree([p.buffer(0.3) for p in polys]) if polys else None

    def fits(s: float) -> bool:
        if not any(a + 0.5 <= s - al / 2 and s + al / 2 <= b - 0.5 for a, b in intervals):
            return False
        if any(lo < s + al / 2 and s - al / 2 < hi for lo, hi in reserved):
            return False
        if abs(prof.crown(s + al / 2) - prof.crown(s - al / 2)) > 0.15 * al:
            return False
        f = _Frame3(path, s)
        box = Polygon([f.p(-al / 2, spec.outer), f.p(al / 2, spec.outer),
                       f.p(al / 2, spec.outer + float(tw["outM"])),
                       f.p(-al / 2, spec.outer + float(tw["outM"]))])  # fmt: skip
        return out_tree is None or len(out_tree.query(box, predicate="intersects")) == 0

    towers: list[float] = []
    for v in path.vertices_between(0.0, path.length):
        if path.bend_deg(v) >= float(tw["bendDeg"]):
            for shift in (0.0, al / 2 + 0.3, -(al / 2 + 0.3)):
                if fits(v + shift) and all(abs(v + shift - x) > al + 2 for x in towers):
                    towers.append(v + shift)
                    break
    anchors = sorted([sg for _, sg in gates] + towers)
    for a, b in intervals:
        s = a + al / 2 + 0.5
        while s + al / 2 <= b - 0.5:
            near = min((abs(s - x) for x in anchors + towers), default=math.inf)
            if near >= every and fits(s):
                towers.append(s)
                s += every
            else:
                s += 1.0
    towers.sort()
    # Stairs next to each gate tower (on a free stretch, leaning on the wall).
    stairs = []
    width = float(cw["stairs"]["width"])
    stair_tree = STRtree([p.buffer(0.2) for p in polys]) if polys else None
    for g, sg in gates:
        placed = None
        for k in range(int(float(cw["stairs"]["searchM"]) / 2) + 1):
            for d in (1, -1):
                s_top = sg + d * (gate_al / 2 + 0.3 + 2.0 * k)
                st = plan_stair(path, prof, spec, rules, s_top, d)
                a0, a1 = sorted((s_top, s_top + d * st.steps * st.run))
                straight = all(path.bend_deg(v) < 10.0 for v in path.vertices_between(a0, a1))
                on_wall = any(iv[0] <= a0 and a1 <= iv[1] for iv in intervals)
                clear_tw = all(not (x - al / 2 - 0.5 < a1 and a0 < x + al / 2 + 0.5)
                               for x in towers)  # fmt: skip
                clear_pf = all(not (s - 2.5 < a1 and a0 < s + 2.5) for _, s in pfortes)
                fp = stair_footprint(path, spec, st, width)
                free_fp = (
                    stair_tree is None or len(stair_tree.query(fp, predicate="intersects")) == 0
                )
                if straight and on_wall and clear_tw and clear_pf and free_fp:
                    placed = st
                    break
            if placed:
                break
        if placed:
            stairs.append((g, placed))
        else:
            notes.append(f"stair at {g.key}: no free stretch within {cw['stairs']['searchM']} m")
    return Plan(path, intervals, towers, gates, pfortes, stairs, notes), prof, spec


# --- whole wall ------------------------------------------------------------------------------


def _origin(path: Path2, prof: Profile, s: float, spec: WallSpec) -> tuple[float, float, float]:
    p = path.point(s)
    return (round(float(p[0]), 3), round(prof.lowest(s - 1, s + 1) - spec.sink, 3),
            round(float(p[1]), 3))  # fmt: skip


def build_wall(course: Course, footprints: Sequence[Polygon], height: Height,
               rules: Rules) -> tuple[list[Piece], Plan]:  # fmt: skip
    """All output pieces: ring wall chunks, gate towers with their stairs, Zwinger walls."""
    plan, prof, spec = plan_wall(course, footprints, height, rules)
    cw = rules.get("cityWall")
    path = plan.path
    step = float(cw["pieceM"])
    chunk = float(cw["chunkM"])
    pw = float(cw["pforte"]["w"])
    tower_al = float(cw["tower"]["alongM"])
    pieces: list[Piece] = []
    for k, (a, b) in enumerate(plan.free):
        cuts = [a]
        x = a
        while b - x > chunk * 1.25:
            y = x + chunk
            busy = [(s - tower_al / 2 - 0.5, s + tower_al / 2 + 0.5) for s in plan.towers]
            busy += [(s - pw / 2 - 0.5, s + pw / 2 + 0.5) for _, s in plan.pfortes]
            while any(lo < y < hi for lo, hi in busy):
                y += 0.5
            if b - y < 2.0:
                break
            cuts.append(y)
            x = y
        cuts.append(b)
        for j, (c0, c1) in enumerate(zip(cuts, cuts[1:], strict=False)):
            piece = Piece(f"wall_{k:02d}_{j:02d}", "wall", _origin(path, prof, c0, spec))
            piece.length = c1 - c0
            posts = sorted(s for _, s in plan.pfortes if c0 < s < c1)
            marks = [c0]
            for s in posts:
                marks += [s - pw / 2, s + pw / 2]
            marks.append(c1)
            for m in range(0, len(marks), 2):
                s0, s1 = marks[m], marks[m + 1]
                caps = (s0 == a or m > 0, s1 == b or m + 2 < len(marks))
                rows = wall_strip(piece, path, prof, spec, s0, s1, step, caps)
                strip_collision(piece, path, spec, rows)
            for s in posts:
                lintel = prof.ground(s) + float(cw["pforte"]["h"])
                rows = wall_strip(piece, path, prof, spec, s - pw / 2, s + pw / 2, pw,
                                  (False, False), lintel)  # fmt: skip
                strip_collision(piece, path, spec, rows)
            towers = [s for s in plan.towers if c0 <= s < c1]
            skip = [(s - tower_al / 2, s + tower_al / 2) for s in towers]
            piece.merlons = merlons(piece, path, prof, spec, rules, c0, c1, skip, "merlon")
            for s in towers:
                flank_tower(piece, path, prof, spec, rules, s)
            pieces.append(piece)
    for g, s in plan.gates:
        piece = Piece(f"gate_{g.key}", "gate", _origin(path, prof, s, spec))
        gate_tower(piece, path, prof, rules, s)
        for gg, stair in plan.stairs:
            if gg.key == g.key:
                build_stair(piece, path, prof, spec, rules, stair)
        pieces.append(piece)
    pieces += _zwinger(course, footprints, height, rules)
    return pieces, plan


def _zwinger(course: Course, footprints: Sequence[Polygon], height: Height,
             rules: Rules) -> list[Piece]:  # fmt: skip
    z = rules.get("cityWall", "zwinger")
    cw = rules.get("cityWall")
    t, h = float(z["thicknessM"]), float(z["heightM"])
    spec = WallSpec(-t / 2, t / 2, None, h, 0.0, 1.0, float(cw["baseSinkM"]))
    polys = [p for p in footprints if p.is_valid and not p.is_empty]
    tree = STRtree([p.buffer(t / 2) for p in polys]) if polys else None
    pieces = []
    for k, pts in enumerate(course.zwinger):
        path = Path2(pts, closed=False)
        prof = Profile(path, height, t / 2, float(cw["smoothM"]))
        n = max(2, int(math.ceil(path.length / SAMPLE_M)))
        ss = np.linspace(0.0, path.length, n)
        hit = np.zeros(n, dtype=bool)
        if tree is not None:
            q = tree.query([Point(*path.point(s)) for s in ss], predicate="intersects")
            hit[np.unique(q[0])] = True
        for j, (i0, i1) in enumerate(_runs(~hit)):
            s0, s1 = ss[i0], ss[i1 - 1]
            if i0 > 0:
                s0 -= t / 2 + float(cw["stossM"])
            if i1 < n:
                s1 += t / 2 + float(cw["stossM"])
            s0, s1 = max(s0, 0.0), min(s1, path.length)
            if s1 - s0 < 0.6:
                continue
            p0 = path.point(s0)
            piece = Piece(f"zwinger_{k:02d}_{j:02d}", "zwinger",
                          (round(float(p0[0]), 3), round(prof.lowest(s0, s0 + 1) - 1.0, 3),
                           round(float(p0[1]), 3)))  # fmt: skip
            piece.length = s1 - s0
            rows = wall_strip(piece, path, prof, spec, s0, s1, float(cw["pieceM"]))
            strip_collision(piece, path, spec, rows)
            pieces.append(piece)
    return pieces


def _solids(path: Path2, polys: Sequence[Polygon], gates: Sequence[tuple[Gate, float]],
            cw: dict[str, Any]) -> STRtree | None:  # fmt: skip
    """Houses and gate towers: where a wall end may stop."""
    al, dep = float(cw["gate"]["alongM"]), float(cw["gate"]["depthM"])
    towers = []
    for _, s in gates:
        f = _Frame3(path, s)
        towers.append(Polygon([f.p(-al / 2, -dep / 2), f.p(al / 2, -dep / 2), f.p(al / 2, dep / 2),
                               f.p(-al / 2, dep / 2)]))  # fmt: skip
    solid = [p.buffer(1e-3) for p in [*polys, *towers]]
    return STRtree(solid) if solid else None


def _end_open(path: Path2, s: float, inwards: float, solids: STRtree | None, t: float) -> bool:
    """True if the end face at s (wall continuing towards ``inwards``) is not inside a solid."""
    p = path.point(s + inwards * 0.05)
    n = path.outward(path.direction(s + inwards * 0.05))
    for o in (-t / 2 + 0.05, 0.0, t / 2 - 0.05):
        if solids is None or len(solids.query(Point(*(p + n * o)), predicate="intersects")) == 0:
            return True
    return False


def _bridge_open_gaps(path: Path2, intervals: list[tuple[float, float]],
                      solids: STRtree | None, t: float) -> list[tuple[float, float]]:  # fmt: skip
    """Joins neighbouring wall intervals whose ends would stand in the open (a building on the
    line narrower than the wall, e.g. a small wall remnant): the wall runs through."""
    out: list[tuple[float, float]] = []
    for a, b in sorted(intervals):
        if out and (_end_open(path, out[-1][1], -1.0, solids, t)
                    or _end_open(path, a, 1.0, solids, t)) and a - out[-1][1] < 12.0:  # fmt: skip
            out[-1] = (out[-1][0], max(b, out[-1][1]))
        else:
            out.append((a, b))
    # Ends still open: reach further into the building (up to 6 m) until the end face is in it.
    fixed = []
    for a, b in out:
        for _ in range(24):
            if not _end_open(path, a, 1.0, solids, t):
                break
            a -= 0.25
        for _ in range(24):
            if not _end_open(path, b, -1.0, solids, t):
                break
            b += 0.25
        fixed.append((a, b))
    return fixed


def open_wall_ends(pieces_plan: Plan, footprints: Sequence[Polygon], rules: Rules) -> list[str]:
    """Wall ends that stop in the open (neither in a house nor in a gate tower): should be none."""
    cw = rules.get("cityWall")
    path = pieces_plan.path
    t = float(cw["thicknessM"])
    polys = [p for p in footprints if p.is_valid and not p.is_empty]
    towers = []
    for _, s in pieces_plan.gates:
        f = _Frame3(path, s)
        al, dep = float(cw["gate"]["alongM"]), float(cw["gate"]["depthM"])
        towers.append(Polygon([f.p(-al / 2, -dep / 2), f.p(al / 2, -dep / 2), f.p(al / 2, dep / 2),
                               f.p(-al / 2, dep / 2)]))  # fmt: skip
    solid = [p.buffer(1e-3) for p in polys + towers]
    tree = STRtree(solid) if solid else None
    out = []
    if len(pieces_plan.free) == 1 and pieces_plan.free[0] == (0.0, path.length):
        return out
    for a, b in pieces_plan.free:
        for s, d in ((a, 1.0), (b, -1.0)):
            p = path.point(s + d * 0.05)
            n = path.outward(path.direction(s + d * 0.05))
            ok = True
            for o in (-t / 2 + 0.05, 0.0, t / 2 - 0.05):
                q = Point(*(p + n * o))
                if tree is None or len(tree.query(q, predicate="intersects")) == 0:
                    ok = False
            if not ok:
                out.append(f"s={s:.1f} at {tuple(round(float(v), 1) for v in path.point(s))}")
    return out


def generate_citywall(course: Course, footprints: Sequence[Polygon], height: Height, rules: Rules,
                      out_dir: Path, vfs_dir: str) -> dict[str, Any]:  # fmt: skip
    """Writes one ``.glb`` per piece (with COL_ bodies) and returns the index."""
    pieces, plan = build_wall(course, footprints, height, rules)
    cw = rules.get("cityWall")
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for piece in pieces:
        prims = piece.primitives(rules)
        if not prims:
            continue
        data = glb_bytes_multi(prims, piece.key, piece.collision)
        path = out_dir / f"{piece.key}.glb"
        if not path.is_file() or path.read_bytes() != data:
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_bytes(data)
            tmp.replace(path)
        entries.append({"id": piece.key, "kind": piece.kind, "mesh": f"{vfs_dir}/{piece.key}.glb",
                        "pos": list(piece.origin),
                        "triangles": sum(p.mesh.triangle_count for p in prims),
                        "collisionTriangles": piece.collision_triangles,
                        "lengthM": round(piece.length, 1)})  # fmt: skip
    referenced = {f"{e['id']}.glb" for e in entries}
    for stale in out_dir.glob("*.glb"):
        if stale.name not in referenced:
            stale.unlink()
    ring_walls = sum(b - a for a, b in plan.free)
    tris = sum(e["triangles"] for e in entries)
    col = [e["collisionTriangles"] for e in entries]
    budget = int(cw["collisionPerFile"])
    stats = {
        "ringM": round(plan.path.length, 1),
        "wallM": round(ring_walls, 1),
        "onHousesM": round(plan.path.length - ring_walls, 1),
        "zwingerM": round(sum(p.length for p in pieces if p.kind == "zwinger"), 1),
        "towers": len(plan.towers),
        "gateTowers": len(plan.gates),
        "pfortes": len(plan.pfortes),
        "stairs": len(plan.stairs),
        "merlons": sum(p.merlons for p in pieces),
        "files": len(entries),
        "triangles": tris,
        "budgetTriangles": int(cw["budgetTriangles"]),
        "collisionMax": max(col, default=0),
        "collisionOver": sum(c > budget for c in col),
        "openEnds": open_wall_ends(plan, footprints, rules),
        "notes": plan.notes,
    }
    return {"format": INDEX_FORMAT, "version": INDEX_VERSION, "entries": entries, "stats": stats}


def write_index(path: Path, index: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    text = json.dumps(index, indent=1, ensure_ascii=False) + "\n"
    tmp.write_text(text, encoding="utf-8", newline="\n")
    tmp.replace(path)


def footprints_of(buildings: Sequence[dict[str, Any]]) -> list[tuple[str, Polygon]]:
    """(id, footprint) of every building or LoD2 part."""
    out = []
    for b in buildings:
        for part in b.get("parts") or [b]:
            fp = part.get("footprint") or []
            if len(fp) >= 3:
                poly = Polygon(fp).buffer(0)
                if isinstance(poly, Polygon) and not poly.is_empty:
                    out.append((str(b["id"]), poly))
    return out


def wall_houses(course: Course, buildings: Sequence[dict[str, Any]], rules: Rules) -> set[str]:
    """Houses the wall line runs through (shared by ``buildings`` and ``citywall``)."""
    line = LineString([*course.ring, course.ring[0]])
    least = float(rules.get("cityWall", "wallHouse", "onLineM"))
    return {
        bid for bid, poly in footprints_of(buildings) if line.intersection(poly).length >= least
    }


def wall_context(course: Course, buildings: Sequence[dict[str, Any]], height: Height,
                 rules: Rules) -> WallContext:  # fmt: skip
    """The wall as the house generator needs it: town outline, crown height, wall houses."""
    footprints = [poly for _, poly in footprints_of(buildings)]
    plan, prof, spec = plan_wall(course, footprints, height, rules)
    line = LineString([*course.ring, course.ring[0]])
    path = plan.path

    def crown(x: float, z: float) -> float:
        s = (line.project(Point(x, z)) - path.start) % path.length
        return walk_height(prof, spec, s) + spec.parapet_high

    cw = rules.get("cityWall")
    m = cw["merlon"]
    houses = frozenset(wall_houses(course, buildings, rules))
    merlon = (float(m["w"]), float(m["gap"]), float(m["h"]))
    return WallContext(Polygon(course.ring), crown, houses, merlon, float(cw["parapetM"]))
