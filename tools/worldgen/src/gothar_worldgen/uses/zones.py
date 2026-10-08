"""Indoor zones (world.md "Zonen", type ``indoor``, contract with engine 2026-10-05): every room of
the enterable houses as a few turned boxes, floor to ceiling at the inner faces of its walls.

The boxes cover the room greedily: on a grid of ``GRID_M`` along one of its walls, the rectangle
inside the room that covers the most of what is still uncovered, until every point more than
``SLANT_M`` from the walls lies in a box (boxes may overlap), at most ``MAX_BOXES``. The engine
counts 5 cm beyond a box as inside and fades out over 0.3 m: a slanted wall at 0.15 m keeps about
two thirds of the indoor ambient (0.3 m left the smithy's wall outdoors), and Leonberg's five
rooms need 24 boxes. Of the walls' directions, the one needing the fewest boxes sets the axis; a
rectangular room is one box. The written lines follow the engine's writer (one zone per line,
sorted by value, then box centre x, z), so a generator run and an editor save of the same world
give the same bytes."""

from __future__ import annotations

import functools
import json
import math
from collections.abc import Sequence
from typing import Any

import numpy as np
import shapely
from shapely.geometry import Polygon, box
from shapely.ops import unary_union

from gothar_worldgen.waynet.write import tidy

SLANT_M = 0.15  # everything farther than this from the walls lies in a box
GRID_M = 0.05  # cover grid (rectangles of whole cells inside the room)
MAX_BOXES = 8  # per room (the engine passes the 32 boxes nearest the camera)
AXES = 3  # wall directions tried
MIN_GAIN_M2 = 0.02  # a box covering less of what is left is not added


def is_room_zone(zone: dict[str, Any]) -> bool:
    """An indoor zone written by worldgen (``LEO_…_INNEN``, ``LEO_…_KAMMER…``, upstairs
    ``LEO_…_OBEN…`` and the stairs ``LEO_…_TREPPE``): assemble replaces those."""
    value = str(zone.get("value", ""))
    room = (value.endswith(("_INNEN", "_TREPPE")) or "_KAMMER" in value
            or "_OBEN" in value)  # fmt: skip
    return zone.get("type") == "indoor" and value.startswith("LEO_") and room


def _cover(local: Polygon) -> list[tuple[float, float, float, float]]:
    """Rectangles (a0, a1, b0, b1) of whole grid cells inside ``local`` (the room in the frame of
    one wall), added greedily until no cell farther than ``SLANT_M`` from the walls is left."""
    a_lo, b_lo, a_hi, b_hi = local.bounds
    na = math.ceil((a_hi - a_lo) / GRID_M - 1e-6)
    nb = math.ceil((b_hi - b_lo) / GRID_M - 1e-6)
    ai, bi = np.meshgrid(np.arange(na), np.arange(nb))  # rows: b, columns: a
    x0, y0 = a_lo + ai * GRID_M, b_lo + bi * GRID_M
    cells = shapely.box(x0.ravel(), y0.ravel(), (x0 + GRID_M).ravel(), (y0 + GRID_M).ravel())
    inside = shapely.contains(local.buffer(1e-6), cells).reshape(nb, na)
    deep = local.buffer(-SLANT_M)
    centres = shapely.points((x0 + GRID_M / 2).ravel(), (y0 + GRID_M / 2).ravel())
    todo = shapely.contains(deep, centres).reshape(nb, na) & inside
    out: list[tuple[float, float, float, float]] = []
    while todo.any() and len(out) < MAX_BOXES:
        acc = np.zeros((nb + 1, na + 1))
        acc[1:, 1:] = todo.cumsum(0).cumsum(1)
        best = (0, 0, (0, 0, 0, 0))  # gain, area, (row0, row1, col0, col1) inclusive
        height = np.zeros(na, dtype=int)
        for r in range(nb):  # maximal rectangles with their bottom on row r (histogram stack)
            height = np.where(inside[r], height + 1, 0)
            stack: list[int] = []
            for c in range(na + 1):
                h = int(height[c]) if c < na else 0
                while stack and int(height[stack[-1]]) >= h:
                    top = stack.pop()
                    hh = int(height[top])
                    if hh == 0:
                        continue
                    left = stack[-1] + 1 if stack else 0
                    r0, c1 = r - hh + 1, c - 1
                    gain = acc[r + 1, c1 + 1] - acc[r0, c1 + 1] - acc[r + 1, left] + acc[r0, left]
                    area = hh * (c1 - left + 1)
                    if (gain, area) > best[:2]:
                        best = (int(gain), area, (r0, r, left, c1))
                stack.append(c)
        if best[0] * GRID_M * GRID_M < MIN_GAIN_M2:
            break
        r0, r1, c0, c1 = best[2]
        todo[r0 : r1 + 1, c0 : c1 + 1] = False
        out.append((a_lo + c0 * GRID_M, a_lo + (c1 + 1) * GRID_M,
                    b_lo + r0 * GRID_M, b_lo + (r1 + 1) * GRID_M))  # fmt: skip
    return out


def room_boxes(
    value: str, ring: Sequence[Sequence[float]], floor: float, ceiling: float
) -> list[dict[str, Any]]:
    """The indoor zones of one room (index ``interior``: ring at the inner wall faces)."""
    pts = [(float(x), float(z)) for x, z in ring]
    n = len(pts)
    # the walls' directions (modulo 90 degrees), the longest walls first
    walls: dict[int, tuple[float, float, float]] = {}
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        length = math.dist(a, b)
        if length < 1e-3:
            continue
        ex, ez = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        key = round(math.degrees(math.atan2(ez, ex)) % 90.0 / 2.0) % 45  # 2-degree bins
        if key not in walls or length > walls[key][0]:
            walls[key] = (walls.get(key, (0.0, 0.0, 0.0))[0] + length, ex, ez)
    best: tuple[int, float, float, float, list[tuple[float, float, float, float]]] | None = None
    for _, ex, ez in sorted(walls.values(), reverse=True)[:AXES]:  # fewest boxes, most floor
        local = Polygon([(x * ex + z * ez, -x * ez + z * ex) for x, z in pts]).buffer(0)
        strips = _cover(local)
        area = unary_union([box(r[0], r[2], r[1], r[3]) for r in strips]).area
        if (
            best is None
            or len(strips) < best[0]
            or (len(strips) == best[0] and area > best[1] + 1e-6)
        ):
            best = (len(strips), area, ex, ez, strips)
    if best is None:
        return []
    _, _, ux, uz, strips = best
    vx, vz = -uz, ux  # the box's local +Z (world.md: local +X = (cos yaw, -sin yaw))
    yaw = math.degrees(math.atan2(-uz, ux))
    cy, hy = (floor + ceiling) / 2, (ceiling - floor) / 2
    out = []
    for a0, a1, b0, b1 in strips:
        ca, cb = (a0 + a1) / 2, (b0 + b1) / 2
        out.append({"type": "indoor", "value": value,
                    "box": {"center": [tidy(ca * ux + cb * vx), tidy(cy), tidy(ca * uz + cb * vz)],
                            "halfExtents": [tidy((a1 - a0) / 2), tidy(hy), tidy((b1 - b0) / 2)],
                            "yaw": tidy(yaw)}})  # fmt: skip
    return out


def _order(p: dict[str, Any], q: dict[str, Any]) -> int:
    """The engine's order: value, then (both with a box) centre x, then z; else as they are."""
    a, b = str(p.get("value", "")), str(q.get("value", ""))
    if a != b:
        return -1 if a < b else 1
    if "box" not in p or "box" not in q or p.get("type") != "indoor" or q.get("type") != "indoor":
        return 0
    (px, _, pz), (qx, _, qz) = p["box"]["center"], q["box"]["center"]
    if px != qx:
        return -1 if px < qx else 1
    return 0 if pz == qz else (-1 if pz < qz else 1)


def zones_text(zones: Sequence[dict[str, Any]]) -> str:
    """The ``zones`` list in the engine's layout: one zone per line, sorted."""
    lines = []
    for z in sorted(zones, key=functools.cmp_to_key(_order)):
        if z.get("type") == "indoor" and "box" in z:
            bx = z["box"]
            line = {"type": "indoor", "value": z["value"],
                    "box": {"center": [tidy(c) for c in bx["center"]],
                            "halfExtents": [tidy(c) for c in bx["halfExtents"]],
                            "yaw": tidy(bx.get("yaw", 0.0))}}  # fmt: skip
            lines.append(json.dumps(line, separators=(",", ":"), ensure_ascii=False))
        else:  # other types as the engine keeps them (a JSON object: keys sorted)
            lines.append(json.dumps(z, separators=(",", ":"), ensure_ascii=False, sort_keys=True))
    return "[\n    " + ",\n    ".join(lines) + "\n  ]"


def indoor_zones(houses: Sequence[Any], index: dict[str, Any]) -> list[dict[str, Any]]:
    """The indoor zones of every enterable house (``inside`` with ``interior`` in the index)."""
    from gothar_worldgen.uses.inside import room_tag

    rooms = {e["id"]: e["interior"] for e in index.get("entries", []) if e.get("interior")}
    out: list[dict[str, Any]] = []
    for h in houses:
        if h.inside and h.id in rooms:
            r = rooms[h.id]
            floor, ceiling = float(r["floor"]), float(r["ceiling"])
            if not r.get("rooms"):
                out += room_boxes(room_tag(h), r["ring"], floor, ceiling)
                continue
            base = room_tag(h)[: -len("_INNEN")]  # divided: a zone per room, as its waypoint
            for part in r["rooms"]:
                out += room_boxes(f"{base}_{part['name']}", part["ring"], floor, ceiling)
    for h in houses:  # upstairs (W7): its own zones at its height, the stairs over both storeys
        r = rooms.get(h.id) if h.inside else None
        if not r or "upper" not in r or "stairs" not in r:
            continue
        base = room_tag(h)[: -len("_INNEN")]
        up = r["upper"]
        for part in up["rooms"]:
            out += room_boxes(f"{base}_{part['name']}", part["ring"], float(up["floor"]),
                              float(up["ceiling"]))  # fmt: skip
        st = r["stairs"]
        well = Polygon(st["footprint"]).union(Polygon(st["opening"])).convex_hull
        out += room_boxes(f"{base}_TREPPE", list(well.exterior.coords)[:-1], float(r["floor"]),
                          float(up["ceiling"]))  # fmt: skip
    return out


def with_room_zones(world: dict[str, Any], zones: Sequence[dict[str, Any]]) -> None:
    """Replaces the worldgen room zones of ``world`` (others stay); no list without zones."""
    keep = [z for z in world.get("zones") or [] if not is_room_zone(z)]
    if keep or zones:
        world["zones"] = [*keep, *zones]
    else:
        world.pop("zones", None)
