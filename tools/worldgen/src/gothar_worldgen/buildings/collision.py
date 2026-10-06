"""Collision geometry of the generated buildings (``COL_`` nodes, contract with engine, M5 part B).

Per mass, the ground storey footprint (no jetties) is cut into convex pieces; each piece becomes a
closed convex body from the base up to the roof surface (``COL_HULL_<i>``): walls, the two roof
planes of a saddle roof (ridge inserted at the edges), no overhang, no sag. Dormers, chimneys,
timber and windows are left out. If a building would need too many pieces or triangles, it gets
one triangle mesh of its plain masses instead (``COL_0``).
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np
import shapely
from shapely.geometry import LineString, Polygon
from shapely.ops import split

from gothar_worldgen.buildings.gltf import CollisionPart
from gothar_worldgen.buildings.massing import Mass, _Roof, _valid_polygon, build_mesh

CONVEX_TOLERANCE = 0.02  # a footprint within 2 % of its convex hull counts as convex
MERGE_TOLERANCE = 0.01  # two pieces merge if their union is within 1 % of its convex hull
SLIVER_M2 = 0.5  # smaller pieces join a neighbour
MAX_PIECES = 8  # per building, beyond: triangle mesh fallback
BUDGET = 200  # collision triangles per building (engine warns above, no error)


@dataclass
class CollisionResult:
    parts: list[CollisionPart]
    fallback: bool = False  # triangle mesh instead of hulls
    decomposed: int = 0  # masses cut into several convex pieces

    @property
    def triangles(self) -> int:
        return sum(p.triangle_count for p in self.parts)


def _convex_enough(poly: Polygon, tolerance: float) -> bool:
    hull = poly.convex_hull
    return hull.area <= poly.area * (1.0 + tolerance) + 1e-9


def _reflex(poly: Polygon) -> list[tuple[np.ndarray, np.ndarray, np.ndarray]]:
    """(previous, vertex, next) of every reflex corner of the counter-clockwise outline."""
    ring = np.asarray(shapely.orient_polygons(poly).exterior.coords)[:-1]
    out = []
    for i in range(len(ring)):
        a, v, c = ring[i - 1], ring[i], ring[(i + 1) % len(ring)]
        e1, e2 = v - a, c - v
        cross = e1[0] * e2[1] - e1[1] * e2[0]
        if cross < -1e-9 * (np.linalg.norm(e1) * np.linalg.norm(e2) + 1e-12):
            out.append((a, v, c))
    return out


def _cut_pieces(poly: Polygon, depth: int = 0) -> list[Polygon]:
    """Cuts along the walls at reflex corners (an L becomes two rectangles), greedily."""
    if _convex_enough(poly, MERGE_TOLERANCE) or depth > MAX_PIECES:
        return [poly]
    minx, minz, maxx, maxz = poly.bounds
    reach = 2 * math.hypot(maxx - minx, maxz - minz) + 1.0
    best = None
    for a, v, c in _reflex(poly):
        for d in (v - a, c - v):
            n = float(np.linalg.norm(d))
            if n < 1e-9:
                continue
            d = d / n
            line = LineString([tuple(v - d * reach), tuple(v + d * reach)])
            parts = [g for g in split(poly, line).geoms if isinstance(g, Polygon) and g.area > 1e-6]
            if len(parts) < 2:
                continue
            score = (sum(len(_reflex(g)) for g in parts), len(parts), -max(g.area for g in parts))
            if best is None or score < best[0]:
                best = (score, parts)
    if best is None:
        return [poly]
    return [q for g in best[1] for q in _cut_pieces(g, depth + 1)]


def convex_pieces(poly: Polygon, slivers: bool = True) -> list[Polygon]:
    """Convex pieces that cover ``poly``: its hull if nearly convex, else the better of cuts at
    reflex corners and merged triangles (fewer pieces). ``slivers=False`` keeps small pieces as
    they are instead of joining them to a neighbour: for the thin walls left round a carved room
    (W7), whose small pieces would otherwise grow a hull far into the room."""
    if _convex_enough(poly, CONVEX_TOLERANCE):
        return [poly.convex_hull]
    keep = _merge_slivers if slivers else (lambda pieces: pieces)
    cut = keep(_cut_pieces(poly))
    merged = keep(_merged_triangles(poly))
    best = (
        cut
        if len(cut) <= len(merged) and all(_convex_enough(p, MERGE_TOLERANCE) for p in cut)
        else merged
    )
    return [p.convex_hull for p in best]


def _shared(a: Polygon, b: Polygon) -> float:
    return a.boundary.intersection(b.boundary).length


def _merged_triangles(poly: Polygon) -> list[Polygon]:
    pieces = [t for t in shapely.constrained_delaunay_triangles(poly).geoms if t.area > 1e-9]

    merged = True
    while merged:  # greedy: merge the neighbouring pair with the largest union first
        merged = False
        best = None
        for i, a in enumerate(pieces):
            for j in range(i + 1, len(pieces)):
                b = pieces[j]
                if _shared(a, b) < 1e-6:
                    continue
                union = a.union(b)
                if (
                    isinstance(union, Polygon)
                    and _convex_enough(union, MERGE_TOLERANCE)
                    and (best is None or union.area > best[2].area)
                ):
                    best = (i, j, union)
        if best is not None:
            i, j, union = best
            pieces = [p for k, p in enumerate(pieces) if k not in (i, j)] + [union]
            merged = True
    return pieces


def _merge_slivers(pieces: list[Polygon]) -> list[Polygon]:
    """Slivers join the neighbour whose hull grows least."""
    pieces = sorted(pieces, key=lambda p: p.area)
    while len(pieces) > 1 and pieces[0].area < SLIVER_M2:
        small, rest = pieces[0], pieces[1:]
        options = [k for k, p in enumerate(rest) if _shared(small, p) > 1e-6] or list(
            range(len(rest))
        )
        k = min(options, key=lambda k: small.union(rest[k]).convex_hull.area - rest[k].area)
        rest[k] = small.union(rest[k])
        pieces = sorted(rest, key=lambda p: p.area)
    return pieces


def _body(points: list[tuple[float, float, float]], tris: list[tuple[int, int, int]],
          origin: tuple[float, float, float], name: str) -> CollisionPart:  # fmt: skip
    pos = np.round(np.asarray(points, dtype=np.float64) - np.asarray(origin), 4).astype(np.float32)
    return CollisionPart(name, pos, np.asarray(tris, dtype=np.uint32).reshape(-1))


def hull_body(piece: Polygon, roof: _Roof, base_y: float, origin: tuple[float, float, float],
              name: str) -> CollisionPart:  # fmt: skip
    """Closed convex body over a convex piece: bottom, walls, roof planes (split at the ridge)."""
    ring = list(shapely.orient_polygons(piece).exterior.coords)[:-1]  # counter-clockwise (x, z)
    pts: list[tuple[float, float]] = []
    ridge: list[int] = []  # indices in pts that lie on the ridge line
    saddle = roof.mass.roof == "saddle" and roof.half > 1e-6
    for i, a in enumerate(ring):
        c = ring[(i + 1) % len(ring)]
        pts.append(a)
        if saddle:
            va = a[0] * roof.v[0] + a[1] * roof.v[1] - roof.mid
            vc = c[0] * roof.v[0] + c[1] * roof.v[1] - roof.mid
            if abs(va) < 1e-9:
                ridge.append(len(pts) - 1)
            elif va * vc < 0:
                t = va / (va - vc)
                pts.append((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t))
                ridge.append(len(pts) - 1)
    n = len(pts)
    tops = [roof.height(x, z) for x, z in pts]
    points = [(x, base_y, z) for x, z in pts]
    points += [(x, y, z) for (x, z), y in zip(pts, tops, strict=True)]
    tris: list[tuple[int, int, int]] = []
    # In (x, z) with +z south, "counter-clockwise" seen from above (+y) is clockwise in the maths
    # sense: orient_polygons gives a positive shoelace in (x, z), whose normal points to -y.
    for k in range(1, n - 1):  # bottom faces down: keep the ring order
        tris.append((0, k, k + 1))
    for i in range(n):  # walls: bottom i, i+1 and top i+1, i
        j = (i + 1) % n
        tris += [(i, n + i, n + j), (i, n + j, j)]
    if len(ridge) == 2:  # two roof planes, each a convex polygon from ridge point to ridge point
        a, b = ridge
        sides = [list(range(a, b + 1)), list(range(b, n)) + list(range(0, a + 1))]
    else:
        sides = [list(range(n))]
    for side in sides:
        for k in range(1, len(side) - 1):  # top faces up: reversed order
            tris.append((n + side[0], n + side[k + 1], n + side[k]))
    return _body(points, tris, origin, name)


def prism_body(piece: Polygon, y0: float, y1: float, origin: tuple[float, float, float],
               name: str) -> CollisionPart:  # fmt: skip
    """Closed convex prism over a convex piece from y0 to y1."""
    ring = list(shapely.orient_polygons(piece).exterior.coords)[:-1]
    n = len(ring)
    pts = [(x, y0, z) for x, z in ring] + [(x, y1, z) for x, z in ring]
    tris: list[tuple[int, int, int]] = [(0, k, k + 1) for k in range(1, n - 1)]
    for i in range(n):
        j = (i + 1) % n
        tris += [(i, n + i, n + j), (i, n + j, j)]
    tris += [(n, n + k + 1, n + k) for k in range(1, n - 1)]
    pos = np.asarray(pts, dtype=np.float64) - np.asarray(origin)
    centre = pos.mean(axis=0)
    fixed = []
    for a, b, d in tris:  # outward winding regardless of the ring's orientation
        pa, pb, pd = pos[a], pos[b], pos[d]
        if np.dot(np.cross(pb - pa, pd - pa), (pa + pb + pd) / 3 - centre) < 0:
            b, d = d, b
        fixed.append((a, b, d))
    return CollisionPart(name, np.round(pos, 4).astype(np.float32),
                         np.asarray(fixed, dtype=np.uint32).reshape(-1))  # fmt: skip


def collision_for(masses: Sequence[Mass], base_y: float, origin_xz: tuple[float, float],
                  carve: Sequence[tuple[Polygon, float]] = ()) -> CollisionResult:  # fmt: skip
    """``COL_HULL_`` bodies for all masses (ground footprints); triangle mesh as fallback.

    ``carve``: passages (corridor polygon, clear top): a piece they cross becomes the body above
    the top plus prisms beside the corridor below it."""
    origin = (origin_xz[0], base_y, origin_xz[1])
    parts: list[CollisionPart] = []
    decomposed = 0
    pieces_total = 0
    for mass in masses:
        poly = _valid_polygon(mass.footprint)
        if poly is None:
            continue
        roof = _Roof(mass, poly)
        pieces = convex_pieces(poly)
        decomposed += len(pieces) > 1
        pieces_total += len(pieces)
        for piece in pieces:
            cut = next(((cor, top) for cor, top in carve if cor.intersects(piece)), None)
            if cut is None:
                parts.append(hull_body(piece, roof, base_y, origin, f"COL_HULL_{len(parts)}"))
                continue
            corridor, top = cut
            parts.append(hull_body(piece, roof, top, origin, f"COL_HULL_{len(parts)}"))
            rest = piece.difference(corridor)
            for g in getattr(rest, "geoms", [rest]):
                if isinstance(g, Polygon) and g.area > 0.05:
                    for sub in convex_pieces(g, slivers=False):
                        parts.append(prism_body(sub, base_y, top, origin,
                                                f"COL_HULL_{len(parts)}"))  # fmt: skip
    result = CollisionResult(parts, False, decomposed)
    # a passage needs the hulls (the triangle mesh would close it); over budget only warns
    if not carve and (pieces_total > MAX_PIECES or result.triangles > BUDGET):
        try:
            mesh = build_mesh(masses, base_y, origin_xz)
        except ValueError:
            return result
        return CollisionResult([CollisionPart("COL_0", mesh.positions, mesh.indices)], True,
                               decomposed)  # fmt: skip
    return result


def merge_collision(parts: Sequence[tuple[Sequence[CollisionPart], tuple[float, float, float]]],
                    origin: tuple[float, float, float]) -> list[CollisionPart]:  # fmt: skip
    """Collision parts of several buildings around a common ``origin`` (cells), renumbered."""
    out: list[CollisionPart] = []
    for cols, (ox, oy, oz) in parts:
        shift = np.asarray([ox - origin[0], oy - origin[1], oz - origin[2]], np.float32)
        for c in cols:
            kind = "COL_HULL_" if c.name.startswith("COL_HULL_") else "COL_"
            out.append(CollisionPart(f"{kind}{len(out)}", c.positions + shift, c.indices))
    return out


def body_is_closed(part: CollisionPart) -> bool:
    """Every edge (by position) is used by exactly two triangles."""
    keys = [tuple(np.round(p, 3)) for p in part.positions]
    edges: dict[tuple, int] = {}
    for t in part.indices.reshape(-1, 3):
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            e = tuple(sorted((keys[a], keys[b])))
            edges[e] = edges.get(e, 0) + 1
    return all(v == 2 for v in edges.values())
