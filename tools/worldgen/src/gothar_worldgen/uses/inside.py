"""Inside the enterable houses (W7 C2): mobs, table with benches, hearth and light, freepoints,
the room's waypoint and its private area.

The room comes from the buildings index (``interior``: floor, ceiling, ring, door). What stands
inside: per use (``uses.json`` ``uses.<use>.inside``) beds and chests against the walls, a table
with a bench on both long sides in the open floor, an open hearth against a wall with a warm
light over it, freepoints on free floor, then household props (``props``: barrels, shelves,
crates, sacks, a counter with a shelf behind it, the smithy's tools beside the hearth, sausages
and herbs hanging from the ceiling); the way from the door to the middle of the room, the way to
the hearth and the straight walks to every place inside stay clear. A waypoint just inside the
door is linked to the house's routine waypoint outside; a box trigger over the room marks it
private once ``uses.json`` names its ``owner``.
"""

from __future__ import annotations

import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from typing import Any

import numpy as np
from shapely import affinity
from shapely.geometry import LineString, Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

from gothar_worldgen.uses.places import FP_TYPES, MOB_TYPES, House, UsesError, door_hinge
from gothar_worldgen.uses.suggest import short_id
from gothar_worldgen.waynet.generate import name_part

# footprint (along the wall or the table axis, depth) of what stands in a room, and the slot
SIZE = {"bed": (2.0, 0.9), "chest": (0.9, 0.6), "bench": (1.5, 0.35), "table": (1.6, 0.8),
        "anvil": (0.8, 0.5), "hearth": (1.2, 0.9)}  # fmt: skip
SLOT = {"bed": 0.75, "chest": 0.65, "bench": 0.38, "anvil": 0.6, "hearth": 0.6,
        "barrel_rack": 0.6}  # fmt: skip
# household props (W7 B, assets/source/props): footprint along the wall, depth; how they stand
PROP_SIZE = {"barrel": (0.62, 0.62), "barrel_rack": (1.62, 0.95), "shelf": (1.22, 0.37),
             "crate": (0.64, 0.54), "crate_stack": (1.26, 0.58), "sacks": (0.9, 0.58),
             "workbench": (1.82, 0.67), "quench_trough": (1.02, 0.52), "bellows": (1.2, 0.46),
             "tool_board": (1.2, 0.1), "weapon_board": (1.2, 0.1),
             "sausages": (1.0, 0.1), "herbs": (0.9, 0.1),
             # denser rooms (W7 step 4)
             "stool": (0.36, 0.36), "bucket": (0.34, 0.34), "basket": (0.5, 0.5),
             "broom": (0.3, 0.25), "wall_hanging": (1.4, 0.1), "firewood": (0.82, 0.44),
             "fur": (1.5, 0.96)}  # fmt: skip
WALL_BOARDS = {"tool_board", "weapon_board", "wall_hanging"}  # on the wall, nothing below
HANGING = {"sausages": 0.55, "herbs": 0.45}  # from the ceiling: how far they hang down
HANG_OUT_M = 0.45  # hanging things this far from the wall
HEAD_M = 2.0  # hanging things end above this (over the floor)
BY_HEARTH = {"bellows", "quench_trough", "firewood"}  # beside the hearth, on either side
PROP_GAP_M = 0.12  # props stand this close to each other (furniture keeps MOVE_M)
# the counter group (a trader's): shelf at the wall, an aisle, the counter; the open end of the
# aisle gets a waypoint, the trader stands behind the counter
GROUP_L, GROUP_D = 3.0, 1.9  # along the wall, depth
AISLE_M = 0.9
COUNTER_ALONG_M = 0.6  # counter and shelf off the middle: the aisle opens at the other end
STAND_BEHIND_M = 0.45  # the trader's place in front of the shelf
BENCH_OFF_M = 0.62  # bench middles beside the table axis (engine's table slots, #197)
WALL_GAP_M = 0.05
PATH_W_M = 1.3  # the way from the door to the middle of the room stays this wide
DOOR_ZONE_M = 1.4  # nothing within this distance inside the door
DOOR_SWEEP_EXTRA_M = 0.3  # the door's sweep stays free and this much round it (engine #232)
MOVE_M = 0.45  # room to walk past furniture
STEP_M = 0.25  # placement search step
INSIDE_WP_M = 1.2  # the room's waypoint this far inside the door
REACH_R_M = 0.4  # straight walks keep this clear (engine: 0.3 m spheres, ai.md; models overhang)
DOOR_WP_IN_M = 0.15  # the door's waypoint in the middle of the opening (half the wall)
OUTSIDE_WP_M = 0.9  # the waypoint in front of the door, outside
LIGHT = {
    "hearth": {"color": [1.0, 0.62, 0.32], "range": 6.5, "intensity": 2.6, "flicker": 0.3},
    "candle": {"color": [1.0, 0.75, 0.45], "range": 4.0, "intensity": 1.4, "flicker": 0.15},
    "lantern": {"color": [1.0, 0.72, 0.4], "range": 5.0, "intensity": 1.6, "flicker": 0.12},
    # the day falling in at a window: the engine takes colour and brightness from the sky
    # (``daylight``, world.md), so it is dark at night; no flicker
    "window": {"color": [1.0, 1.0, 1.0], "range": 4.5, "intensity": 1.4, "daylight": True},
}
WINDOW_LIGHTS = 2  # per room at most (the engine's 8 lights per object: hearth, candles, ...)
ROOM_LIGHTS = 6  # lights of a room at most: two of the engine's 8 stay for the neighbours'
NIGHT_M2 = 15.0  # a light burning at night per this much floor (hearth, candles, lanterns)
LANTERN_APART_M = 2.5  # lanterns this far from other lights at least
LANTERN_CLEAR_M = 1.2  # and this far from doors, passages and windows
WINDOW_LIGHT_IN_M = 0.8  # the window's light this far inside
WINDOW_FREE_M = 0.8  # tall things keep this far from a window (into the room), and 0.3 beside it
TALL = {"hearth", "shelf", "barrel_rack", "tool_board", "weapon_board", "crate_stack",
        "wall_hanging"}  # fmt: skip
# what is set out on every table: (item, along the table, across it, height over the top, rot)
TABLE_ITEMS = (("items/it_mug.glb", 0.45, 0.2, 0.05, None),
               ("items/it_mug.glb", -0.45, -0.2, 0.05, None),
               ("items/it_bread.glb", -0.2, -0.28, 0.068, [0.0, 0.0, 0.70711, 0.70711]),
               ("items/it_apple.glb", 0.25, 0.26, 0.0, None))  # fmt: skip
BROOM_GRIP_M = 1.2  # the broom hangs bristles down from its grip (it_broom: grip at the origin)
BIG_ROOM_M2 = 60.0  # a second light in rooms bigger than this
Pt = tuple[float, float]
# divided ground storeys (index ``interior.rooms``): what goes into the chambers, round the rooms
CHAMBER_MOBS = {"bed", "chest"}
CHAMBER_PROPS = {"barrel", "crate", "crate_stack", "sacks", "basket", "fur", "wall_hanging"}


@dataclass
class InsideSpec:
    mobs: list[tuple[str, str]] = field(default_factory=list)  # (type, count or "R" residents)
    freepoints: list[tuple[str, int]] = field(default_factory=list)
    hearth: bool = False
    props: list[tuple[str, int]] = field(default_factory=list)  # (prop or "counter", count)


def inside_spec(spec: dict[str, Any], where: str) -> InsideSpec:
    """``uses.<use>.inside`` checked: mobs ``type:N`` (``N`` may be ``R``: the residents, at most
    3), freepoints ``TYPE:N``, ``hearth``, props ``prop:N`` (``PROP_SIZE`` or ``counter``)."""
    out = InsideSpec(hearth=bool(spec.get("hearth", False)))
    for item in spec.get("mobs", []):
        kind, _, n = str(item).partition(":")
        if kind not in MOB_TYPES | {"table"} or not (n.isdigit() or n == "R"):
            raise UsesError(f"{where}: {item!r} must be type:N or type:R")
        out.mobs.append((kind, n))
    for item in spec.get("freepoints", []):
        kind, _, n = str(item).partition(":")
        if kind.upper() not in FP_TYPES or not n.isdigit():
            raise UsesError(f"{where}: {item!r} must be TYPE:N")
        out.freepoints.append((kind.upper(), int(n)))
    for item in spec.get("props", []):
        kind, _, n = str(item).partition(":")
        if kind not in {*PROP_SIZE, "counter"} or not n.isdigit():
            raise UsesError(
                f"{where}: {item!r} must be prop:N (one of {sorted(PROP_SIZE)}, counter)"
            )
        out.props.append((kind, int(n)))
    return out


def _rect(cx: float, cz: float, ax: float, az: float, length: float, depth: float) -> Polygon:
    """Rectangle centred at (cx, cz), ``length`` along (ax, az), ``depth`` across."""
    r = box(-length / 2, -depth / 2, length / 2, depth / 2)
    return affinity.translate(affinity.rotate(r, math.atan2(az, ax), use_radians=True), cx, cz)


def _matrix_quat(m: tuple[tuple[float, float, float], ...]) -> list[float]:
    """Unit quaternion (x, y, z, w) of a rotation matrix given by rows."""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s4 = math.sqrt(tr + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / s4, (m[0][2] - m[2][0]) / s4, (m[1][0] - m[0][1]) / s4, s4 / 4)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s4 = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (s4 / 4, (m[0][1] + m[1][0]) / s4, (m[0][2] + m[2][0]) / s4, (m[2][1] - m[1][2]) / s4)
    elif m[1][1] > m[2][2]:
        s4 = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s4, s4 / 4, (m[1][2] + m[2][1]) / s4, (m[0][2] - m[2][0]) / s4)
    else:
        s4 = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s4, (m[1][2] + m[2][1]) / s4, s4 / 4, (m[1][0] - m[0][1]) / s4)
    if q[3] < 0:
        q = (-q[0], -q[1], -q[2], -q[3])
    return [round(c, 5) + 0.0 for c in q]


def _quat(fx: float, fz: float) -> list[float]:
    """Rotation about +Y turning the model's front (+Z) into (fx, fz)."""
    a = math.atan2(fx, fz)
    return [0.0, round(math.sin(a / 2), 5) + 0.0, 0.0, round(math.cos(a / 2), 5) + 0.0]


@dataclass
class _Room:
    poly: Polygon
    floor: float
    ceiling: float
    keep: Polygon  # the way from the door and the door zone
    door_mid: tuple[float, float]
    inward: tuple[float, float]
    taken: list[Polygon] = field(default_factory=list)
    stands: list[tuple[tuple[float, float], tuple[float, float]]] = field(default_factory=list)
    spots: list[Polygon] = field(default_factory=list)  # freepoints: kept apart, walked through
    windows: list[dict[str, Any]] = field(default_factory=list)  # index ``interior.windows``
    exits: list[tuple[float, float]] = field(default_factory=list)  # passages to the chambers
    tall: list[Polygon] = field(default_factory=list)  # shelves, boards, ...: no lantern above
    props: list[Polygon] = field(default_factory=list)  # household props: close to each other
    hanging: list[Polygon] = field(default_factory=list)  # under the ceiling

    @property
    def entry(self) -> tuple[float, float]:
        """The room's waypoint just inside the door."""
        return (self.door_mid[0] + self.inward[0] * INSIDE_WP_M,
                self.door_mid[1] + self.inward[1] * INSIDE_WP_M)  # fmt: skip

    def reachable(self, p: tuple[float, float]) -> bool:
        """A straight walk from the room's waypoint to ``p`` past all furniture."""
        lane = LineString([self.entry, p]).buffer(REACH_R_M)
        return not any(lane.intersects(t) for t in [*self.taken, *self.props])

    def on_wall_free(self, shape: Polygon) -> bool:
        """A wall-mounted ``shape`` (boards, hangings): on the wall, off the door and away from
        other tall or wall-mounted things."""
        if not self.poly.buffer(-0.02).contains(shape):
            return False
        if shape.distance(Point(self.door_mid)) < DOOR_ZONE_M:
            return False
        if any(shape.distance(q) < 0.45 for q in self.spots):  # a figure leaning there
            return False
        return not any(shape.distance(q) < 0.15 for q in self.tall)

    def before_window(self, shape: Polygon) -> bool:
        """``shape`` stands in front of one of the room's windows (keeping out the day)."""
        for w in self.windows:
            (fx, fz), (tx, tz) = w["from"], w["to"]
            nx, nz = w["normal"]
            d = WINDOW_FREE_M
            inside = [(fx, fz), (tx, tz), (tx - nx * d, tz - nz * d), (fx - nx * d, fz - nz * d)]
            zone = Polygon(inside).buffer(0.3)  # in front of it and 0.3 m beside it
            if shape.intersects(zone):
                return True
        return False

    def reachable_from(self, a: tuple[float, float], p: tuple[float, float]) -> bool:
        lane = LineString([a, p]).buffer(REACH_R_M)
        return not any(lane.intersects(t) for t in [*self.taken, *self.props])

    def fits(self, shape: Polygon, margin: float = MOVE_M) -> bool:
        if not self.poly.buffer(-0.02).contains(shape) or shape.intersects(self.keep):
            return False
        if any(shape.distance(t) < PROP_GAP_M for t in self.props):
            return False
        return not any(shape.distance(t) < margin for t in [*self.taken, *self.spots])


def _clip(poly: Polygon, obstacles: Sequence[Polygon], near: Point) -> Polygon:
    """``poly`` without other bodies reaching into it (their face is the room's wall there); of
    several parts the one nearest ``near``."""
    inside = [o for o in obstacles if o.intersects(poly)]
    if not inside:
        return poly
    rest = poly.difference(unary_union(inside).buffer(WALL_GAP_M))
    parts = [g for g in getattr(rest, "geoms", [rest]) if isinstance(g, Polygon)]
    return orient(min(parts, key=lambda g: g.distance(near)), sign=1.0) if parts else poly


def _chamber(r: dict[str, Any], ring: Sequence[Sequence[float]], mid: tuple[float, float],
             axis: tuple[float, float], obstacles: Sequence[Polygon] = ()) -> _Room:  # fmt: skip
    """A chamber of a divided ground storey, entered through the open passage at ``mid`` in a
    partition across ``axis``: the passage zone and the way to the middle stay clear."""
    poly = Polygon(ring)
    c = poly.centroid
    sign = 1.0 if (c.x - mid[0]) * axis[0] + (c.y - mid[1]) * axis[1] > 0 else -1.0
    inward = (axis[0] * sign, axis[1] * sign)
    poly = _clip(poly, obstacles, Point(mid[0] + inward[0], mid[1] + inward[1]))
    c = poly.centroid
    path = LineString([mid, (c.x, c.y)]).buffer(PATH_W_M / 2, cap_style="flat")
    keep = path.union(Point(mid).buffer(DOOR_ZONE_M))
    room = _Room(poly, float(r["floor"]), float(r["ceiling"]), keep, mid, inward)
    room.windows = _windows_of(r, poly)
    return room


def _room(e: dict[str, Any], obstacles: Sequence[Polygon] = (),
          ring: Sequence[Sequence[float]] | None = None) -> _Room:  # fmt: skip
    """The room of an index entry (or of its ``ring``: the room with the door of a divided
    storey); ``obstacles`` (other bodies reaching into it, such as a town wall the house leans
    against) are cut off: their face is the room's wall there."""
    r = e["interior"]
    poly = Polygon(r["ring"] if ring is None else ring)
    d = r["door"]
    mid = ((d["from"][0] + d["to"][0]) / 2, (d["from"][1] + d["to"][1]) / 2)
    inward = (-d["normal"][0], -d["normal"][1])
    poly = _clip(poly, obstacles, Point(mid[0] + inward[0] * INSIDE_WP_M,
                                        mid[1] + inward[1] * INSIDE_WP_M))  # fmt: skip
    c = poly.centroid
    path = LineString([mid, (c.x, c.y)]).buffer(PATH_W_M / 2, cap_style="flat")
    zone = Point(mid).buffer(DOOR_ZONE_M)
    # the door swings into the room: its sweep stays free, the open blade is in the way
    hx, hz = door_hinge(d)
    w = math.dist(d["from"], d["to"])
    ux, uz = (mid[0] - hx) / (w / 2), (mid[1] - hz) / (w / 2)
    quarter = Polygon([(hx, hz), (hx + ux * w, hz + uz * w),
                       (hx + (ux + inward[0]) * w, hz + (uz + inward[1]) * w),
                       (hx + inward[0] * w, hz + inward[1] * w)])  # fmt: skip
    sweep = (
        Point(hx, hz)
        .buffer(w + DOOR_SWEEP_EXTRA_M)
        .intersection(quarter.buffer(DOOR_SWEEP_EXTRA_M))
    )
    blade = LineString([(hx, hz), (hx + inward[0] * w, hz + inward[1] * w)]).buffer(0.06)
    keep = path.union(zone).union(sweep)
    room = _Room(poly, float(r["floor"]), float(r["ceiling"]), keep, mid, inward, [blade])
    room.windows = _windows_of(r, poly)
    return room


def _windows_of(r: dict[str, Any], poly: Polygon) -> list[dict[str, Any]]:
    """The windows (index ``interior.windows``) on the walls of the room ``poly``."""
    edge = poly.exterior.buffer(0.1)
    return [w for w in r.get("windows", [])
            if edge.contains(Point((w["from"][0] + w["to"][0]) / 2,
                                   (w["from"][1] + w["to"][1]) / 2))]  # fmt: skip


def _walls(room: _Room) -> list[tuple[tuple[float, float], tuple[float, float], float]]:
    """Room edges, longest first: (start, unit along, length); the ring is counter-clockwise."""
    ring = list(room.poly.exterior.coords)[:-1]
    out = []
    for k, a in enumerate(ring):
        b = ring[(k + 1) % len(ring)]
        length = math.dist(a, b)
        if length > 1.0:
            out.append((a, ((b[0] - a[0]) / length, (b[1] - a[1]) / length), length))
    return sorted(out, key=lambda w: -w[2])


def _against_wall(
    room: _Room,
    kind: str,
    facing: tuple[float, float] | None = None,
    size: tuple[float, float] | None = None,
    accept: Callable[[Polygon, tuple[float, float], tuple[float, float]], bool] | None = None,
) -> tuple[Polygon, tuple[float, float], tuple[float, float]] | None:
    """A spot with the back to a wall: (footprint, centre, front direction into the room); the
    first along the longest walls, or with ``facing`` the one turned most towards that point
    (the hearth towards the door: seen on coming in) and reached from the room's waypoint;
    ``size`` overrides the footprint, ``accept`` may turn a spot down."""
    length, depth = size or SIZE.get(kind) or PROP_SIZE[kind]
    best: tuple[float, Polygon, tuple[float, float], tuple[float, float]] | None = None
    for a, (ux, uz), wall_len in _walls(room):
        nx, nz = -uz, ux  # into the room (counter-clockwise ring)
        t = length / 2 + 0.1
        while t <= wall_len - length / 2 - 0.1:
            cx = a[0] + ux * t + nx * (WALL_GAP_M + depth / 2)
            cz = a[1] + uz * t + nz * (WALL_GAP_M + depth / 2)
            t += STEP_M
            shape = _rect(cx, cz, ux, uz, length, depth)
            if kind in WALL_BOARDS:  # above heads: only the wall must be free (no floor room)
                if room.on_wall_free(shape) and not room.before_window(shape):
                    return shape, (cx, cz), (nx, nz)
                continue
            reach = depth / 2 + SLOT.get(kind, 0.6)
            slot = (cx + nx * reach, cz + nz * reach)
            if not (room.fits(shape) and room.poly.contains(Point(slot))):
                continue
            if room.keep.contains(Point(slot)):
                continue
            if accept is not None and not accept(shape, (cx, cz), (nx, nz)):
                continue
            if kind in TALL and room.before_window(shape):
                continue
            if facing is None:
                return shape, (cx, cz), (nx, nz)
            if not room.reachable(slot):
                continue
            dx, dz = facing[0] - cx, facing[1] - cz
            score = (nx * dx + nz * dz) / max(math.hypot(dx, dz), 1e-6)
            if best is None or score > best[0] + 1e-9:
                best = (score, shape, (cx, cz), (nx, nz))
    return None if best is None else best[1:]


def _table_spot(room: _Room) -> tuple[Polygon, tuple[float, float], tuple[float, float]] | None:
    """The table with its benches in the open floor, along the room's long side, nearest the
    middle: (block, centre, table axis)."""
    (_, (ux, uz), _) = _walls(room)[0]
    length = SIZE["table"][0] + 0.1
    depth = 2 * (BENCH_OFF_M + SIZE["bench"][1] / 2)
    c = room.poly.centroid
    minx, minz, maxx, maxz = room.poly.bounds
    best = None
    x = minx
    while x <= maxx:
        z = minz
        while z <= maxz:
            block = _rect(x, z, ux, uz, length, depth)
            if room.fits(block, MOVE_M + 0.2):
                d = math.dist((x, z), (c.x, c.y))
                if best is None or d < best[0]:
                    best = (d, block, (x, z))
            z += STEP_M
        x += STEP_M
    return None if best is None else (best[1], best[2], (ux, uz))


@dataclass
class InsidePlan:
    vobs: list[dict[str, Any]] = field(default_factory=list)  # specs for assemble
    places: list[dict[str, Any]] = field(default_factory=list)  # waypoints, freepoints
    failed: list[dict[str, Any]] = field(default_factory=list)


def room_tag(h: House) -> str:
    """The room's name in places, mobs and its indoor zone: ``LEO_<USE>_<SHORT>_INNEN``."""
    return f"LEO_{h.token}_{name_part(short_id(h.id))}_INNEN"


class _House:
    """Places in the room of one house."""

    def __init__(self, plan: InsidePlan, h: House, room: _Room, tag: str | None = None) -> None:
        self.plan, self.h, self.room = plan, h, room
        self.tag = tag or room_tag(h)
        self.counts: dict[str, int] = {}
        self.tables: list[tuple[float, float]] = []
        self.table_axes: list[tuple[float, float]] = []
        self.beds: list[tuple[tuple[float, float], tuple[float, float]]] = []

    def vob(self, kind: str, name: str, pos: tuple[float, float], front: tuple[float, float],
            height: float | None = None, **comp: object) -> None:  # fmt: skip
        y = self.room.floor if height is None else height
        spec = {
            "key": f"use:{name}",
            "name": name,
            "type": kind,
            "pos": [pos[0], y, pos[1]],
            "rot": _quat(*front),
        }
        self.plan.vobs.append({**spec, **comp})

    def mob(self, kind: str, pos: tuple[float, float], front: tuple[float, float]) -> None:
        self.counts[kind] = self.counts.get(kind, 0) + 1
        name = f"MOB_{self.tag}_{name_part(kind)}_{self.counts[kind]}"
        self.vob("mob", name, pos, front, mesh=f"mobs/{kind}.glb",
                 components={"mob": {"definition": kind}})  # fmt: skip

    def fp(self, kind: str, pos: tuple[float, float], d: tuple[float, float]) -> None:
        key = f"FP_{kind}"
        self.counts[key] = self.counts.get(key, 0) + 1
        name = f"FP_{kind}_{self.tag}_{self.counts[key]:02d}"
        self.plan.places.append({"kind": "fp", "name": name,
                                 "house": self.h.id, "pos": [pos[0], pos[1]], "dir": [d[0], d[1]],
                                 "y": self.room.floor})  # fmt: skip
        self.room.spots.append(Point(pos).buffer(0.3))

    def fail(self, what: str) -> None:
        self.plan.failed.append({"house": self.h.id, "what": what, "reason": "no room"})

    def taps_reached(self, _shape: Polygon, c: tuple[float, float], n: tuple[float, float]) -> bool:
        """The innkeeper's place at the taps of a rack is reached from the room's waypoint."""
        reach = PROP_SIZE["barrel_rack"][1] / 2 + SLOT["barrel_rack"]
        return self.room.reachable((c[0] + n[0] * reach, c[1] + n[1] * reach))

    def lights_so_far(self) -> list[tuple[float, float]]:
        prefix = f"LIGHT_{self.tag}_"
        return [(v["pos"][0], v["pos"][2]) for v in self.plan.vobs
                if v["type"] == "light" and v["name"].startswith(prefix)]  # fmt: skip

    def candles(self) -> None:
        """A candlestick on every table; lanterns on the walls until the room has a light at
        night per ``NIGHT_M2`` (the hearth counts), within ``ROOM_LIGHTS`` and spread out."""
        from gothar_worldgen.mobs import CANDLE_TOP, LANTERN_Y

        room = self.room
        for k, (x, z) in enumerate(self.tables):
            self.prop("candlestick", (x, z), (0.0, 1.0), None, height=room.floor + 0.75)
            self.vob("light", f"LIGHT_{self.tag}_KERZE_{k + 1}", (x, z), (0.0, 1.0),
                     height=room.floor + 0.75 + CANDLE_TOP + 0.1,
                     components={"light": dict(LIGHT["candle"])})  # fmt: skip
        lights = self.lights_so_far()
        windows = sum(
            1 for v in self.plan.vobs if v["name"].startswith(f"LIGHT_{self.tag}_FENSTER")
        )
        night = len(lights) - windows
        want = max(1, round(room.poly.area / NIGHT_M2))
        n = max(0, min(want - night, ROOM_LIGHTS - len(lights)))
        clear = [room.door_mid, *room.exits] + [
            ((w["from"][0] + w["to"][0]) / 2, (w["from"][1] + w["to"][1]) / 2) for w in room.windows
        ]
        spots = []
        for a, (ux, uz), wall_len in _walls(room):
            nx, nz = -uz, ux
            t = 0.4
            while t <= wall_len - 0.4:
                p = (a[0] + ux * t, a[1] + uz * t)
                t += STEP_M
                if any(math.dist(p, q) < LANTERN_CLEAR_M for q in clear):
                    continue
                if any(Point(p).distance(q) < 0.4 for q in room.tall):
                    continue
                spots.append((p, (nx, nz)))
        for k in range(n):  # the spot farthest from every light so far
            best = max(spots, key=lambda sp: min((math.dist(sp[0], q) for q in lights),
                                                 default=1e9), default=None)  # fmt: skip
            if best is None or (lights and min(math.dist(best[0], q) for q in lights)
                                < LANTERN_APART_M):  # fmt: skip
                break
            (px, pz), (nx, nz) = best
            self.prop("lantern", (px, pz), (nx, nz), None, height=room.floor)
            at = (px + nx * 0.2, pz + nz * 0.2)
            self.vob(
                "light",
                f"LIGHT_{self.tag}_LATERNE_{k + 1}",
                at,
                (nx, nz),
                height=room.floor + LANTERN_Y,
                components={"light": dict(LIGHT["lantern"])},
            )
            lights.append(at)

    def window_lights(self) -> None:
        """Daylight in at up to ``WINDOW_LIGHTS`` windows, on different walls where it can."""
        chosen: list[dict[str, Any]] = []
        for w in sorted(self.room.windows, key=lambda w: (w["normal"], w["from"])):
            if len(chosen) < WINDOW_LIGHTS and all(c["normal"] != w["normal"] for c in chosen):
                chosen.append(w)
        for w in self.room.windows:
            if len(chosen) < WINDOW_LIGHTS and w not in chosen:
                chosen.append(w)
        for k, w in enumerate(chosen):
            nx, nz = w["normal"]
            mx = (w["from"][0] + w["to"][0]) / 2 - nx * WINDOW_LIGHT_IN_M
            mz = (w["from"][1] + w["to"][1]) / 2 - nz * WINDOW_LIGHT_IN_M
            self.vob("light", f"LIGHT_{self.tag}_FENSTER_{k + 1}", (mx, mz), (-nx, -nz),
                     height=(w["sill"] + w["top"]) / 2,
                     components={"light": dict(LIGHT["window"])})  # fmt: skip

    def keep_way(self, p: tuple[float, float]) -> None:
        """The straight walk from the room's waypoint to ``p`` (and a figure at it) stays free."""
        room = self.room
        lane = LineString([room.entry, p]).buffer(REACH_R_M).union(Point(p).buffer(0.35))
        room.keep = room.keep.union(lane)

    def prop(self, kind: str, centre: tuple[float, float], front: tuple[float, float],
             shape: Polygon | None, height: float | None = None) -> str:  # fmt: skip
        """A household prop (a plain mesh vob, ``props/<kind>.glb``)."""
        key = f"PROP_{kind}"
        self.counts[key] = self.counts.get(key, 0) + 1
        name = f"PROP_{self.tag}_{name_part(kind)}_{self.counts[key]}"
        self.vob("mesh", name, centre, front, height=height, mesh=f"props/{kind}.glb")
        if shape is not None:
            self.room.props.append(shape)
            if kind in TALL or kind in WALL_BOARDS:
                self.room.tall.append(shape)
        return name

    def set_tables(self, stools: int) -> int:
        """A rug under every table, plates, mugs, bread and an apple on it, stools at its ends;
        returns the stools that found no place there."""
        room = self.room
        top = room.floor + 0.75
        for t, ((cx, cz), (ax, az)) in enumerate(zip(self.tables, self.table_axes, strict=True)):
            nx, nz = -az, ax
            self.prop("rug", (cx, cz), (nx, nz), None)
            self.prop("tableware", (cx, cz), (nx, nz), None, height=top)
            for k, (item, along, across, up, rot) in enumerate(TABLE_ITEMS):
                x, z = cx + ax * along + nx * across, cz + az * along + nz * across
                name = f"PROP_{self.tag}_TISCH_{t + 1}_{k + 1}"
                spec = {"key": f"use:{name}", "name": name, "type": "mesh",
                        "pos": [x, top + up, z], "rot": rot or _quat(nx, nz),
                        "mesh": item}  # fmt: skip
                self.plan.vobs.append(spec)
            for sign in (1.0, -1.0):  # at the table's ends
                if stools <= 0:
                    break
                reach = SIZE["table"][0] / 2 + 0.35
                x, z = cx + ax * reach * sign, cz + az * reach * sign
                spot = Point(x, z).buffer(0.2)
                free = room.poly.buffer(-0.05).contains(spot) and not spot.intersects(room.keep)
                if free and not any(spot.intersects(t) for t in [*room.props, *room.spots]):
                    self.prop("stool", (x, z), (ax * sign, az * sign), spot)
                    stools -= 1
        return stools

    def free_floor_rect(self, length: float, depth: float) -> tuple[Pt, Pt] | None:
        """Free floor for something flat (a fur): nearest the room's middle, on nothing."""
        room = self.room
        (_, (ux, uz), _) = _walls(room)[0]
        c = room.poly.centroid
        x0, z0, x1, z1 = room.poly.bounds
        best = None
        for x in np.arange(x0, x1, STEP_M):
            for z in np.arange(z0, z1, STEP_M):
                shape = _rect(float(x), float(z), ux, uz, length, depth)
                if not room.poly.buffer(-0.1).contains(shape):
                    continue
                if any(shape.intersects(q) for q in [*room.taken, *room.props]):
                    continue
                d = math.dist((x, z), (c.x, c.y))
                if best is None or d < best[0]:
                    best = (d, (float(x), float(z)))
        return None if best is None else (best[1], (-uz, ux))

    def place_prop(self, kind: str, hearth_at: Any) -> None:  # noqa: ANN401
        room = self.room
        if kind == "fur":  # before a bed, else on free floor: flat, walked over
            if self.beds:
                (bx, bz), (fx, fz) = self.beds.pop(0)
                reach = SIZE["bed"][1] / 2 + PROP_SIZE["fur"][1] / 2 + 0.05
                self.prop("fur", (bx + fx * reach, bz + fz * reach), (fx, fz), None)
                return
            spot = self.free_floor_rect(*PROP_SIZE["fur"])
            if spot is None:
                self.fail(f"prop {kind}")
                return
            self.prop("fur", spot[0], spot[1], None)
            return
        if kind == "broom":  # figuren's broom, leaning on a wall bristles down
            spot = _against_wall(room, "broom")
            if spot is None:
                self.fail(f"prop {kind}")
                return
            shape, (x, z), _ = spot
            self.counts["BESEN"] = self.counts.get("BESEN", 0) + 1
            name = f"PROP_{self.tag}_BESEN_{self.counts['BESEN']}"
            self.plan.vobs.append({"key": f"use:{name}", "name": name, "type": "mesh",
                                   "pos": [x, room.floor + BROOM_GRIP_M, z],
                                   "rot": [0.0, 0.0, 1.0, 0.0],
                                   "mesh": "items/it_broom.glb"})  # fmt: skip
            room.props.append(shape)
            return
        if kind in HANGING:
            self.hang(kind, hearth_at)
            return
        if kind in BY_HEARTH and hearth_at is not None and self.by_hearth(kind, hearth_at):
            return
        if kind == "bellows":  # only where it can blow into the fire
            self.fail(f"prop {kind}")
            return
        spot = _against_wall(room, kind)
        if spot is None:
            self.fail(f"prop {kind}")
            return
        shape, centre, front = spot
        self.prop(kind, centre, front, shape)
        if kind == "weapon_board":
            self.weapons(centre, front)

    def beside_hearth(self, c: tuple[float, float], n: tuple[float, float]) -> bool:
        """Room for the bellows on one side of a hearth at ``c`` facing ``n``."""
        length, depth = PROP_SIZE["bellows"]
        back = SIZE["hearth"][1] / 2 - depth / 2
        ux, uz = n[1], -n[0]
        for side in (1.0, -1.0):
            off = side * (SIZE["hearth"][0] / 2 + length / 2 + 0.2)
            spot = (c[0] + ux * off - n[0] * back, c[1] + uz * off - n[1] * back)
            if self.room.fits(_rect(spot[0], spot[1], ux, uz, length, depth), PROP_GAP_M):
                return True
        return False

    def by_hearth(self, kind: str, hearth_at: Any) -> bool:  # noqa: ANN401
        """Bellows (the nozzle towards the fire) or trough beside the hearth, on either side."""
        room = self.room
        (hx, hz), (nx, nz) = hearth_at
        ux, uz = nz, -nx  # along the wall: the model's +X when its front is (nx, nz)
        length, depth = PROP_SIZE[kind]
        back = SIZE["hearth"][1] / 2 - depth / 2  # both against the same wall
        # the bellows' nozzle (its -X) points at the fire: on the -u side the bellows is turned
        # round (its front, +Z, to the wall; it looks the same from both sides)
        for side in (1.0, -1.0) if kind == "bellows" else (-1.0, 1.0):
            off = side * (SIZE["hearth"][0] / 2 + length / 2 + 0.2)
            c = (hx + ux * off - nx * back, hz + uz * off - nz * back)
            shape = _rect(c[0], c[1], ux, uz, length, depth)
            if room.fits(shape, PROP_GAP_M):
                turn = kind == "bellows" and side < 0
                self.prop(kind, c, (-nx, -nz) if turn else (nx, nz), shape)
                return True
        return False

    def hang(self, kind: str, hearth_at: Any) -> None:  # noqa: ANN401
        """Sausages or herbs from the ceiling near a wall, above head height, off the hearth's
        hood and the door."""
        room = self.room
        if room.ceiling - HANGING[kind] - room.floor < HEAD_M:
            self.fail(f"prop {kind}")
            return
        length = PROP_SIZE[kind][0]
        hood = None
        if hearth_at is not None:
            (hx, hz), (nx, nz) = hearth_at
            hood = _rect(hx, hz, nz, -nx, SIZE["hearth"][0], SIZE["hearth"][1]).buffer(0.4)
        door = Point(room.door_mid).buffer(DOOR_ZONE_M)
        for a, (ux, uz), wall_len in _walls(room):
            nx, nz = -uz, ux
            t = length / 2 + 0.3
            while t <= wall_len - length / 2 - 0.3:
                c = (a[0] + ux * t + nx * HANG_OUT_M, a[1] + uz * t + nz * HANG_OUT_M)
                t += STEP_M
                shape = _rect(c[0], c[1], ux, uz, length, 0.3)
                if not room.poly.buffer(-0.05).contains(shape) or shape.intersects(door):
                    continue
                if hood is not None and shape.intersects(hood):
                    continue
                if any(shape.distance(q) < 0.6 for q in room.hanging):
                    continue
                room.hanging.append(shape)
                self.prop(kind, c, (nx, nz), None, height=room.ceiling - 0.02)
                return
        self.fail(f"prop {kind}")

    def weapons(self, centre: tuple[float, float], front: tuple[float, float]) -> None:
        """The blades on the weapon board: the items' own models (F6), point down, flat against
        the wall (item +Y down, its flat side +X towards the room, its edge +Z along the wall)."""
        from gothar_worldgen.mobs import WEAPON_ITEMS, WEAPON_PEGS

        nx, nz = front
        ux, uz = nz, -nx
        # rotation with columns item X -> (nx, 0, nz), item Y -> (0, -1, 0), item Z -> (ux, 0, uz)
        m = ((nx, 0.0, ux), (0.0, -1.0, 0.0), (nz, 0.0, uz))
        rot = _matrix_quat(m)
        for k, ((px, py), item) in enumerate(zip(WEAPON_PEGS, WEAPON_ITEMS, strict=True)):
            pos = (centre[0] + ux * px + nx * 0.03, centre[1] + uz * px + nz * 0.03)
            name = f"PROP_{self.tag}_WAFFE_{k + 1}"
            self.plan.vobs.append({"key": f"use:{name}", "name": name, "type": "mesh",
                                   "pos": [pos[0], self.room.floor + py, pos[1]], "rot": rot,
                                   "mesh": item})  # fmt: skip

    def counter_group(self) -> None:
        """A trader's counter: a shelf at the wall, an aisle, the counter facing the room; the
        aisle opens at one end (``WP_…_THEKE``), the trader stands behind the counter."""
        room = self.room

        def parts(
            c: tuple[float, float], n: tuple[float, float]
        ) -> tuple[tuple[float, float], float]:
            """The aisle's open end (the waypoint) and the aisle's offset behind the middle."""
            ux, uz = n[1], -n[0]
            aisle = GROUP_D / 2 - PROP_SIZE["shelf"][1] - AISLE_M / 2
            wp = (c[0] - ux * (GROUP_L / 2 - 0.45) - n[0] * aisle,
                  c[1] - uz * (GROUP_L / 2 - 0.45) - n[1] * aisle)  # fmt: skip
            return wp, aisle

        def shelf_at(c: Pt, n: Pt) -> Polygon:  # the tall part: it alone keeps off the windows
            ux, uz = n[1], -n[0]
            back = GROUP_D / 2 - PROP_SIZE["shelf"][1] / 2
            x = c[0] + ux * COUNTER_ALONG_M - n[0] * back
            z = c[1] + uz * COUNTER_ALONG_M - n[1] * back
            return _rect(x, z, ux, uz, *PROP_SIZE["shelf"])

        def ok(_s: Polygon, c: Pt, n: Pt) -> bool:
            return room.reachable(parts(c, n)[0]) and not room.before_window(shelf_at(c, n))

        spot = _against_wall(room, "counter_group", facing=room.door_mid,
                             size=(GROUP_L, GROUP_D), accept=ok)  # fmt: skip
        if spot is None:
            self.fail("prop counter")
            return
        shape, c, (nx, nz) = spot
        wp, aisle = parts(c, (nx, nz))
        ux, uz = nz, -nx
        along = (c[0] + ux * COUNTER_ALONG_M, c[1] + uz * COUNTER_ALONG_M)
        sd = PROP_SIZE["shelf"][1]
        shelf_c = (along[0] - nx * (GROUP_D / 2 - sd / 2), along[1] - nz * (GROUP_D / 2 - sd / 2))
        counter_c = (along[0] + nx * (GROUP_D / 2 - 0.32), along[1] + nz * (GROUP_D / 2 - 0.32))
        self.prop("shelf", shelf_c, (nx, nz), _rect(*shelf_c, ux, uz, *PROP_SIZE["shelf"]))
        self.prop("counter", counter_c, (nx, nz), _rect(*counter_c, ux, uz, 1.82, 0.66))
        stand = (along[0] - nx * aisle, along[1] - nz * aisle)
        room.stands.insert(0, (stand, (nx, nz)))
        self.keep_way(wp)  # the way to the aisle's open end stays free of table and beds
        room.keep = room.keep.union(LineString([wp, stand]).buffer(AISLE_M / 2 - 0.05))
        name = f"WP_{self.tag}_THEKE"
        self.plan.places.append({"kind": "wp", "name": name, "house": self.h.id,
                                 "pos": [wp[0], wp[1]], "dir": [ux, uz], "y": room.floor,
                                 "link": f"WP_{self.tag}"})  # fmt: skip

    def build(self, spec: InsideSpec, routine_wp: str,
              through: tuple[str, tuple[float, float]] | None = None) -> None:  # fmt: skip
        """Furnish the room; its waypoints: through the house door (linked to ``routine_wp``) or,
        for a chamber, ``through`` the passage (the waypoint it is linked to, the passage)."""
        room, h = self.room, self.h
        hearth_at = None
        # the hearth first: the furniture keeps out of its way and out of the view on it
        if spec.hearth:
            room_beside = (
                (lambda _s, c, n: self.beside_hearth(c, n))
                if any(k == "bellows" for k, _ in spec.props)
                else None
            )  # a smithy: bellows beside it
            hearth_spot = _against_wall(room, "hearth", facing=room.door_mid, accept=room_beside)
            if hearth_spot is None and room_beside is not None:
                hearth_spot = _against_wall(room, "hearth", facing=room.door_mid)
            if hearth_spot is None:
                self.fail("hearth")
            else:
                shape, centre, front = hearth_spot
                self.vob("mesh", f"PROP_{self.tag}_HERD", centre, front, mesh="props/hearth.glb")
                light = {"light": dict(LIGHT["hearth"])}
                self.vob("light", f"LIGHT_{self.tag}_HERD", centre, front,
                         height=room.floor + 0.9, components=light)  # fmt: skip
                room.taken.append(shape)
                room.tall.append(shape)
                hearth_at = (centre, front)
                # keep the way to the fire and the view on it from the door free of furniture
                reach = SIZE["hearth"][1] / 2 + SLOT["hearth"]
                fire = (centre[0] + front[0] * reach, centre[1] + front[1] * reach)
                room.keep = room.keep.union(LineString([room.entry, fire]).buffer(0.6))
        props = dict(spec.props)
        for kind in sorted(BY_HEARTH):  # the smithy's bellows and trough belong to the hearth
            for _ in range(props.pop(kind, 0)):
                self.place_prop(kind, hearth_at)
        if hearth_at is not None:  # every hearth: a pot on the fire, firewood beside it
            from gothar_worldgen.mobs import HEARTH_H

            (hx, hz), front = hearth_at
            self.prop("pot", (hx, hz), front, None, height=room.floor + HEARTH_H + 0.05)
            self.by_hearth("firewood", hearth_at)
        # the counter and the taps next: the shop's or tavern's heart, before table and beds
        if props.pop("counter", 0):
            self.counter_group()
        reach = PROP_SIZE["barrel_rack"][1] / 2 + SLOT["barrel_rack"]
        for _ in range(props.pop("barrel_rack", 0)):  # the innkeeper stands at the taps
            spot = _against_wall(room, "barrel_rack", accept=self.taps_reached)
            if spot is None:
                self.fail("prop barrel_rack")
                continue
            shape, centre, front = spot
            self.prop("barrel_rack", centre, front, shape)
            room.stands.insert(0, ((centre[0] + front[0] * reach, centre[1] + front[1] * reach),
                                   (-front[0], -front[1])))  # fmt: skip
            self.keep_way((centre[0] + front[0] * reach, centre[1] + front[1] * reach))
        for kind, n in spec.mobs:
            for _ in range(min(3, max(1, h.residents)) if n == "R" else int(n)):
                if kind == "table":
                    spot = _table_spot(room)
                    if spot is None:
                        self.fail("table")
                        continue
                    block, (cx, cz), (ax, az) = spot
                    nx, nz = -az, ax  # across the table
                    self.mob("table", (cx, cz), (nx, nz))
                    self.mob("bench", (cx + nx * BENCH_OFF_M, cz + nz * BENCH_OFF_M), (nx, nz))
                    self.mob("bench", (cx - nx * BENCH_OFF_M, cz - nz * BENCH_OFF_M), (-nx, -nz))
                    room.taken.append(block)
                    self.tables.append((cx, cz))
                    self.table_axes.append((ax, az))
                    continue
                wall_spot = _against_wall(room, kind)
                if wall_spot is None:
                    self.fail(kind)
                    continue
                shape, centre, front = wall_spot
                self.mob(kind, centre, front)
                room.taken.append(shape)
                if kind == "bed":
                    self.beds.append((centre, front))
                reach = (SIZE.get(kind) or (0.0, 0.6))[1] / 2 + SLOT.get(kind, 0.6)
                self.keep_way((centre[0] + front[0] * reach, centre[1] + front[1] * reach))
                if kind == "chest":  # a counter: the trader stands at its slot
                    reach = SIZE["chest"][1] / 2 + SLOT["chest"]
                    slot = (centre[0] + front[0] * reach, centre[1] + front[1] * reach)
                    room.stands.append((slot, (-front[0], -front[1])))
        if hearth_at:
            (hx, hz), (fx, fz) = hearth_at
            reach = SIZE["hearth"][1] / 2 + SLOT["hearth"]
            self.fp("CAMPFIRE", (hx + fx * reach, hz + fz * reach), (-fx, -fz))
        for kind, n in spec.freepoints:
            for _ in range(n):
                if kind == "STAND" and room.stands:
                    pos, d = room.stands.pop(0)
                    self.fp(kind, pos, d)
                    continue
                spots = _free_floor(room, kind)
                if spots is None:
                    self.fail(f"FP_{kind}")
                    continue
                for pos, d in spots:
                    self.fp(kind, pos, d)
        for place in [p for p in self.plan.places if p["house"] == h.id and p["kind"] == "fp"]:
            self.keep_way((place["pos"][0], place["pos"][1]))
        props["stool"] = self.set_tables(props.get("stool", 0))
        for kind, n in props.items():
            for _ in range(n):
                self.place_prop(kind, hearth_at)
        # the lights last: windows, candles on the tables, lanterns where nothing tall stands
        self.window_lights()
        self.candles()
        if through is not None:
            self.chamber_waypoints(*through)
            return
        # waypoints: in the middle of the door (linked to the routine waypoint outside), then
        # just inside it (linked to the door's): a figure lines up before the narrow passage
        # in front of it first (on the ground outside): through the opening straight, not across
        # a jamb towards a routine waypoint off to one side
        base = f"WP_{self.tag[: -len('_INNEN')]}"
        door_name = f"{base}_TUER"
        vx = room.door_mid[0] - room.inward[0] * OUTSIDE_WP_M
        vz = room.door_mid[1] - room.inward[1] * OUTSIDE_WP_M
        self.plan.places.append({"kind": "wp", "name": f"{base}_VOR", "house": h.id,
                                 "pos": [vx, vz], "dir": [room.inward[0], room.inward[1]],
                                 "link": routine_wp})  # fmt: skip
        dx = room.door_mid[0] - room.inward[0] * DOOR_WP_IN_M
        dz = room.door_mid[1] - room.inward[1] * DOOR_WP_IN_M
        self.plan.places.append({"kind": "wp", "name": door_name, "house": h.id,
                                 "pos": [dx, dz], "dir": [room.inward[0], room.inward[1]],
                                 "y": room.floor, "link": f"{base}_VOR"})  # fmt: skip
        wx, wz = room.entry
        self.plan.places.append({"kind": "wp", "name": f"WP_{self.tag}", "house": h.id,
                                 "pos": [wx, wz], "dir": [room.inward[0], room.inward[1]],
                                 "y": room.floor, "link": door_name})  # fmt: skip
        if h.owner:
            self.private_area()

    def chamber_waypoints(self, link: str, mid: tuple[float, float]) -> None:
        """In the passage (linked to the room before it), then just inside the chamber."""
        room, h = self.room, self.h
        name = f"WP_{self.tag}_DURCHGANG"
        self.plan.places.append({"kind": "wp", "name": name, "house": h.id,
                                 "pos": [mid[0], mid[1]], "dir": [room.inward[0], room.inward[1]],
                                 "y": room.floor, "link": link})  # fmt: skip
        wx, wz = room.entry
        self.plan.places.append({"kind": "wp", "name": f"WP_{self.tag}", "house": h.id,
                                 "pos": [wx, wz], "dir": [room.inward[0], room.inward[1]],
                                 "y": room.floor, "link": name})  # fmt: skip

    def private_area(self) -> None:
        """A box trigger over the room naming its owner (world.md ``trigger.owner``)."""
        room = self.room
        rect = room.poly.minimum_rotated_rectangle
        rc = list(rect.exterior.coords)[:4]
        e1 = (rc[1][0] - rc[0][0], rc[1][1] - rc[0][1])
        e2 = (rc[2][0] - rc[1][0], rc[2][1] - rc[1][1])
        ln1, ln2 = math.hypot(*e1), math.hypot(*e2)
        half_h = (room.ceiling - room.floor) / 2
        c = rect.centroid
        size = [round(ln1 / 2, 3), round(half_h, 3), round(ln2 / 2, 3)]  # local x along e1
        self.vob("trigger", f"TRIGGER_{self.tag}", (c.x, c.y), (e2[0] / ln2, e2[1] / ln2),
                 height=room.floor + half_h,
                 components={"trigger": {"shape": "box", "halfExtents": size,
                                         "owner": self.h.owner}})  # fmt: skip


def plan_inside(houses: Sequence[House], specs: dict[str, InsideSpec],
                index: dict[str, Any], routine_wps: dict[str, str],
                bodies: Sequence[tuple[str, Polygon]] = ()) -> InsidePlan:  # fmt: skip
    """Everything inside the enterable houses (those with ``interior`` in the index); ``bodies``
    (vob name, section) are the world's collision bodies: those of other vobs reaching into a room
    (a town wall, a neighbour) are cut off it, the house's own and the mobs' are not."""
    plan = InsidePlan()
    rooms = {e["id"]: e for e in index.get("entries", []) if e.get("interior")}
    for h in houses:
        if h.inside and h.id in rooms:
            own = f"BLD_{h.id}"
            others = [
                poly
                for owner, poly in bodies
                if owner != own and not owner.startswith(("MOB_", "PROP_"))
            ]
            spec = specs.get(h.use, InsideSpec())
            parts = rooms[h.id]["interior"].get("rooms")
            if not parts:
                house = _House(plan, h, _room(rooms[h.id], others))
                house.build(spec, routine_wps.get(h.id, ""))
                continue
            _divided(plan, h, rooms[h.id], spec, routine_wps.get(h.id, ""), others)
    return plan


def _share(spec: InsideSpec, residents: int, chambers: int) -> list[InsideSpec]:
    """The spec of the room with the door, then of each chamber: beds and chests and the stores
    (barrels, crates, sacks) go round the chambers, everything else stays in the first room."""
    first = InsideSpec(freepoints=list(spec.freepoints), hearth=spec.hearth)
    rest = [InsideSpec() for _ in range(chambers)]
    k = 0
    for kind, n in spec.mobs:
        count = min(3, max(1, residents)) if n == "R" else int(n)
        if kind not in CHAMBER_MOBS or not chambers:
            first.mobs.append((kind, n))
            continue
        for _ in range(count):
            rest[k % chambers].mobs.append((kind, "1"))
            k += 1
    for kind, n in spec.props:
        if kind not in CHAMBER_PROPS or not chambers:
            first.props.append((kind, n))
            continue
        for _ in range(n):
            rest[k % chambers].props.append((kind, 1))
            k += 1
    return [first, *rest]


def _divided(plan: InsidePlan, h: House, e: dict[str, Any], spec: InsideSpec, routine_wp: str,
             others: Sequence[Polygon]) -> None:  # fmt: skip
    """A divided ground storey (index ``interior.rooms``, the first with the house door): the
    passages are kept clear on both sides, each chamber is entered from the room before it."""
    r = e["interior"]
    parts = r["rooms"]
    order = [p["name"] for p in parts]
    base = room_tag(h)[: -len("_INNEN")]
    built: dict[str, _Room] = {parts[0]["name"]: _room(e, others, parts[0]["ring"])}
    entered: dict[str, tuple[str, tuple[float, float]]] = {}
    for p in sorted(r.get("passages", []), key=lambda q: min(order.index(n) for n in q["rooms"])):
        a, b = sorted(p["rooms"], key=order.index)  # from the room nearer the door
        mid = (float(p["mid"][0]), float(p["mid"][1]))
        ring = next(q["ring"] for q in parts if q["name"] == b)
        built[b] = _chamber(r, ring, mid, (float(p["axis"][0]), float(p["axis"][1])), others)
        before = built[a]
        before.exits.append(mid)
        lane = LineString([before.entry, mid]).buffer(REACH_R_M)
        before.keep = before.keep.union(lane).union(Point(mid).buffer(DOOR_ZONE_M))
        entered[b] = (f"WP_{base}_{a}", mid)
    shares = _share(spec, h.residents, len(order) - 1)
    for name, share in zip(order, shares, strict=True):
        if name not in built:
            continue
        house = _House(plan, h, built[name], f"{base}_{name}")
        house.build(share, routine_wp, entered.get(name))


def _free_floor(
    room: _Room, kind: str
) -> list[tuple[tuple[float, float], tuple[float, float]]] | None:
    """Where a freepoint of ``kind`` goes: LEAN at a wall facing in, SMALLTALK as a pair facing
    each other, others on free floor facing the middle of the room."""
    c = room.poly.centroid
    if kind == "LEAN":
        for a, (ux, uz), wall_len in _walls(room):
            nx, nz = -uz, ux
            t = 0.6
            while t <= wall_len - 0.6:
                p = (a[0] + ux * t + nx * 0.4, a[1] + uz * t + nz * 0.4)
                if room.fits(Point(p).buffer(0.25)) and room.reachable(p):
                    return [(p, (nx, nz))]
                t += STEP_M
        return None
    minx, minz, maxx, maxz = room.poly.bounds
    best = None
    x = minx + 0.5
    while x <= maxx - 0.5:
        z = minz + 0.5
        while z <= maxz - 0.5:
            if kind == "SMALLTALK":  # a pair facing each other, side by side along x or z
                for dx, dz in ((0.6, 0.0), (0.0, 0.6)):
                    p, q = (x - dx, z - dz), (x + dx, z + dz)
                    if (room.fits(Point(p).buffer(0.3)) and room.fits(Point(q).buffer(0.3))
                            and room.reachable(p) and room.reachable(q)):  # fmt: skip
                        d = math.dist((x, z), (c.x, c.y))
                        if best is None or d < best[0]:
                            f = (dx / 0.6, dz / 0.6)
                            best = (d, [(p, f), (q, (-f[0], -f[1]))])
            elif room.fits(Point(x, z).buffer(0.3)) and room.reachable((x, z)):
                d = math.dist((x, z), (c.x, c.y))
                if best is None or d > best[0]:  # out of the way: away from the middle
                    dx, dz = c.x - x, c.y - z
                    ln = math.hypot(dx, dz) or 1.0
                    best = (d, [((x, z), (dx / ln, dz / ln))])
            z += STEP_M
        x += STEP_M
    return None if best is None else best[1]
