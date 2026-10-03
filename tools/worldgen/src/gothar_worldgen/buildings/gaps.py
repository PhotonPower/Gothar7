"""Slots between houses closed with collision bodies (W3 decision E4).

The walkthrough found gaps between houses narrower than the character (0.6 m): it cannot pass,
but it (and the camera) can get stuck in them. Like Gothic's closed rows of houses, such slots get
invisible ``COL_HULL_`` filler bodies; the gap stays visible. A slot is free space that a disc of
``maxWidthM`` / 2 does not reach (morphological opening), at least ``minAreaM2`` large and mostly
enclosed by houses (``enclosedShare`` of its outline). Each filler hangs on the house with the
longest contact, so it is written into that house's ``.glb`` (``batch.generate``).
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import Any

import numpy as np
import shapely
from shapely.geometry import MultiPoint, Polygon
from shapely.ops import unary_union

from gothar_worldgen.buildings.collision import convex_pieces
from gothar_worldgen.buildings.gltf import CollisionPart


@dataclass
class Filler:
    owner: str  # building id the body is written with
    piece: Polygon  # convex, world (x, z)
    y0: float
    y1: float


def footprint_of(parts: Sequence[CollisionPart], origin: Sequence[float]) -> Polygon | None:
    """Union of the (x, z) outlines of a house's collision bodies, world coordinates."""
    polys = []
    for p in parts:
        pts = np.asarray(p.positions, dtype=np.float64)
        hull = MultiPoint([(x + origin[0], z + origin[2]) for x, _, z in pts]).convex_hull
        if isinstance(hull, Polygon) and hull.area > 1e-3:
            polys.append(hull)
    if not polys:
        return None
    u = unary_union(polys)
    return u if isinstance(u, Polygon) else max(u.geoms, key=lambda g: g.area)


def find_fillers(houses: dict[str, Polygon], ground: Callable[[float, float], float],
                 spec: dict[str, Any]) -> list[Filler]:  # fmt: skip
    """Filler bodies for the narrow slots between ``houses`` (id -> footprint)."""
    if not houses:
        return []
    r = float(spec.get("maxWidthM", 0.7)) / 2
    min_area = float(spec.get("minAreaM2", 0.3))
    share = float(spec.get("enclosedShare", 0.5))
    above = float(spec.get("aboveGroundM", 3.0))
    ids = list(houses)
    polys = [houses[i] for i in ids]
    solid = unary_union(polys)
    frame = shapely.box(*solid.bounds).buffer(2.0)
    free = frame.difference(solid)
    passable = free.buffer(-r, join_style="mitre").buffer(r, join_style="mitre")
    narrow = free.difference(passable.buffer(0.01))
    tree = shapely.STRtree(polys)
    out: list[Filler] = []
    for g in getattr(narrow, "geoms", [narrow]):
        if not isinstance(g, Polygon) or g.area < min_area:
            continue
        rim = g.exterior
        touching = tree.query(g.buffer(0.05), predicate="intersects")
        contact = {ids[k]: rim.intersection(polys[k].buffer(0.05)).length for k in touching}
        if not contact or sum(contact.values()) < share * rim.length:
            continue
        owner = max(contact, key=lambda k: contact[k])
        piece_poly = g.simplify(0.05, preserve_topology=True)
        heights = [ground(x, z) for x, z in piece_poly.exterior.coords]
        y0, y1 = min(heights) - 0.5, max(heights) + above
        for piece in convex_pieces(piece_poly):
            if piece.area >= 0.05:
                out.append(Filler(owner, piece, y0, y1))
    return out


def prism_part(piece: Polygon, y0: float, y1: float, origin: Sequence[float],
               name: str) -> CollisionPart:  # fmt: skip
    """Closed, outward-wound prism over a convex piece, relative to ``origin``."""
    ring = list(shapely.orient_polygons(piece).exterior.coords)[:-1]
    n = len(ring)
    pts = [(x - origin[0], y0 - origin[1], z - origin[2]) for x, z in ring]
    pts += [(x - origin[0], y1 - origin[1], z - origin[2]) for x, z in ring]
    tris: list[tuple[int, int, int]] = []
    # orient_polygons: positive shoelace in (x, z), which faces -y (z points south)
    for k in range(1, n - 1):
        tris.append((0, k, k + 1))  # bottom, facing down
        tris.append((n, n + k + 1, n + k))  # top, facing up
    for i in range(n):
        j = (i + 1) % n
        tris += [(i, n + i, n + j), (i, n + j, j)]
    return CollisionPart(name, np.round(np.asarray(pts), 4).astype(np.float32),
                         np.asarray(tris, dtype=np.uint32).reshape(-1))  # fmt: skip
