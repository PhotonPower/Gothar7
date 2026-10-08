"""Music and ambient zones of a site (M13, world.md "Zonen"; ``data/<site>/sound_zones.json``).

Boxes like the indoor zones (centre, half extents, yaw); of the zones of one type holding the hero
the smallest box wins (engine), so the outdoor boxes all reach over the same height and the small
ones - a fountain, the wall walk, a room - win where they are:

- **music:** ``STADT`` over the old town (inside the town wall, its Zwinger strip and the castle
  garden); outside it silence (owner 2026-10-08; ``music.land`` would add a box over the world).
- **ambient:** ``feld`` over the whole world; ``stadt_gasse`` inside the town wall; ``stadt_markt``
  over its squares; ``schlossgarten`` over the formal garden; ``ufer`` along the rivers and lakes
  (their water boxes, widened); ``wald`` over the larger forests; ``brunnen`` round the fountains;
  ``stadtmauer`` on the wall walk; a room's own ambient (the smithy's forge, the tavern's
  parlour) in its indoor boxes. ``innen`` the engine derives from the indoor zones itself.

Large areas are covered by rectangles of whole grid cells inside them (``uses.zones``: the rooms'
cover, on a coarser grid): near their edge the next larger zone holds.
"""

from __future__ import annotations

import math
from collections.abc import Callable, Sequence
from typing import Any

from shapely.geometry import LineString, Polygon
from shapely.ops import unary_union

from gothar_worldgen.waynet.write import tidy

SOUND_TYPES = ("music", "ambient")


def box_zone(kind: str, value: str, centre: tuple[float, float, float],
             half: tuple[float, float, float], yaw_deg: float) -> dict[str, Any]:  # fmt: skip
    return {"type": kind, "value": value,
            "box": {"center": [tidy(c) for c in centre], "halfExtents": [tidy(h) for h in half],
                    "yaw": tidy(yaw_deg)}}  # fmt: skip


def _rect_zone(kind: str, value: str, rect: Polygon, cy: float, hy: float,
               margin: float = 0.0) -> dict[str, Any]:  # fmt: skip
    """A box over an (oriented) rectangle: its long side along the box's local +X."""
    c = list(rect.exterior.coords)[:4]
    e0 = (c[1][0] - c[0][0], c[1][1] - c[0][1])
    e1 = (c[2][0] - c[1][0], c[2][1] - c[1][1])
    if math.hypot(*e1) > math.hypot(*e0):
        e0, e1 = e1, e0
    ux, uz = e0[0] / (math.hypot(*e0) or 1.0), e0[1] / (math.hypot(*e0) or 1.0)
    yaw = math.degrees(math.atan2(-uz, ux))  # local +X = (cos yaw, -sin yaw) (world.md)
    m = rect.centroid
    half = (math.hypot(*e0) / 2 + margin, hy, math.hypot(*e1) / 2 + margin)
    return box_zone(kind, value, (m.x, cy, m.y), half, yaw)


def area_zones(kind: str, value: str, area: Polygon, cy: float, hy: float, cell: float,
               max_boxes: int = 40) -> list[dict[str, Any]]:  # fmt: skip
    """Boxes of whole ``cell``-sized cells inside ``area`` (along its longest direction)."""
    from gothar_worldgen.uses.zones import cover_boxes

    out = []
    for c, half, yaw in cover_boxes(area, cell, max_boxes):
        out.append(box_zone(kind, value, (c[0], cy, c[1]), (half[0], hy, half[1]), yaw))
    return out


def sound_zones(spec: dict[str, Any], half_extent: float, town_ring: Sequence[Sequence[float]],
                squares: Sequence[Sequence[Sequence[float]]], gardens: Sequence[Polygon],
                water: Sequence[dict[str, Any]], forests: Sequence[Polygon],
                fountains: Sequence[tuple[float, float, float]],
                wall_y: Callable[[float, float], float],
                rooms: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:  # fmt: skip
    """All music and ambient zones; ``wall_y``: the wall walk's height at a point of the ring,
    ``rooms``: the indoor zones (a room named in ``spec["rooms"]`` gets its own ambient)."""
    mus, amb = spec["music"], spec["ambient"]
    hy = float(spec["heightM"]) / 2
    out = [box_zone("ambient", amb["default"], (0.0, 0.0, 0.0), (half_extent, hy, half_extent),
                    0.0)]  # fmt: skip
    if mus.get("land"):  # music over the whole world: none, silence outside the town
        out.append(box_zone("music", mus["land"], (0.0, 0.0, 0.0),
                            (half_extent, hy, half_extent), 0.0))  # fmt: skip
    town = Polygon(town_ring).buffer(0)
    cell = float(spec["townCellM"])
    old_town = unary_union([town.buffer(float(spec["zwingerM"])), *gardens])
    for part in getattr(old_town, "geoms", [old_town]):
        out += area_zones("music", mus["town"], part, 0.0, hy, cell)
    out += area_zones("ambient", amb["town"], town, 0.0, hy, cell)
    for sq in squares:
        poly = Polygon(sq).buffer(0)
        if poly.area > 1.0 and town.intersects(poly):
            out.append(_rect_zone("ambient", amb["squares"], poly.minimum_rotated_rectangle, 0.0,
                                  hy, float(spec["squareMarginM"])))  # fmt: skip
    for g in gardens:
        out.append(_rect_zone("ambient", amb["garden"], g.minimum_rotated_rectangle, 0.0, hy))
    wm = float(spec["waterMarginM"])
    for w in water:
        qy, qw = float(w["rot"][1]), float(w["rot"][3])
        yaw = math.degrees(2 * math.atan2(qy, qw))
        hx, _, hz = w["halfExtents"]
        out.append(box_zone("ambient", amb["water"], (w["pos"][0], 0.0, w["pos"][2]),
                            (float(hx) + wm, hy, float(hz) + wm), yaw))  # fmt: skip
    for f in forests:
        if f.area >= float(spec["forestMinM2"]):
            out += area_zones("ambient", amb["forest"], f, 0.0, hy, float(spec["forestCellM"]), 12)
    fh, high = float(spec["fountainHalfM"]), float(spec["fountainHighM"])
    for x, y, z in fountains:
        out.append(box_zone("ambient", amb["fountain"], (x, y + high / 2, z), (fh, high, fh), 0.0))
    ring = LineString([*town_ring, town_ring[0]])
    piece = float(spec["wallPieceM"])
    n = max(1, round(ring.length / piece))
    for k in range(n):  # the wall walk in pieces: a band over the walk, no deeper than the wall
        a, b = ring.interpolate(ring.length * k / n), ring.interpolate(ring.length * (k + 1) / n)
        mx, mz = (a.x + b.x) / 2, (a.y + b.y) / 2
        yaw = math.degrees(math.atan2(-(b.y - a.y), b.x - a.x))
        band = float(spec["wallBandM"])
        out.append(box_zone("ambient", amb["wall"], (mx, wall_y(mx, mz) + band / 2, mz),
                            (math.dist((a.x, a.y), (b.x, b.y)) / 2, band / 2,
                             float(spec["wallHalfM"])), yaw))  # fmt: skip
    own = spec.get("rooms", {})
    for z in rooms:
        if z.get("type") == "indoor" and z.get("value") in own:
            out.append({"type": "ambient", "value": own[z["value"]], "box": dict(z["box"])})
    return out


def is_sound_zone(zone: dict[str, Any], values: set[str]) -> bool:
    """A music or ambient zone written by worldgen (its value one of the site's)."""
    return zone.get("type") in SOUND_TYPES and str(zone.get("value", "")) in values


def sound_values(spec: dict[str, Any]) -> set[str]:
    return {*spec["music"].values(), *spec["ambient"].values(), *spec.get("rooms", {}).values()}


def with_sound_zones(world: dict[str, Any], zones: Sequence[dict[str, Any]],
                     spec: dict[str, Any]) -> None:  # fmt: skip
    """Replaces worldgen's music and ambient zones of ``world`` (others stay)."""
    values = sound_values(spec)
    keep = [z for z in world.get("zones") or [] if not is_sound_zone(z, values)]
    if keep or zones:
        world["zones"] = [*keep, *zones]
    else:
        world.pop("zones", None)
