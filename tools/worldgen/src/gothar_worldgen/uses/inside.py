"""Inside the enterable houses (W7 C2): mobs, table with benches, hearth and light, freepoints,
the room's waypoint and its private area.

The room comes from the buildings index (``interior``: floor, ceiling, ring, door). What stands
inside: per use (``uses.json`` ``uses.<use>.inside``) beds and chests against the walls, a table
with a bench on both long sides in the open floor, an open hearth against a wall with a warm
light over it, freepoints on free floor; the way from the door to the middle of the room stays
clear. A waypoint just inside the door is linked to the house's routine waypoint outside; a box
trigger over the room marks it private once ``uses.json`` names its ``owner``.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any

from shapely import affinity
from shapely.geometry import LineString, Point, Polygon, box

from gothar_worldgen.uses.places import FP_TYPES, MOB_TYPES, House, UsesError, door_hinge
from gothar_worldgen.uses.suggest import short_id
from gothar_worldgen.waynet.generate import name_part

# footprint (along the wall or the table axis, depth) of what stands in a room, and the slot
SIZE = {"bed": (2.0, 0.9), "chest": (0.9, 0.6), "bench": (1.5, 0.35), "table": (1.6, 0.8),
        "anvil": (0.8, 0.5), "hearth": (1.2, 0.9)}  # fmt: skip
SLOT = {"bed": 0.75, "chest": 0.65, "bench": 0.38, "anvil": 0.6, "hearth": 0.6}
BENCH_OFF_M = 0.62  # bench middles beside the table axis (engine's table slots, #197)
WALL_GAP_M = 0.05
PATH_W_M = 1.3  # the way from the door to the middle of the room stays this wide
DOOR_ZONE_M = 1.4  # nothing within this distance inside the door
MOVE_M = 0.45  # room to walk past furniture
STEP_M = 0.25  # placement search step
INSIDE_WP_M = 1.2  # the room's waypoint this far inside the door
DOOR_WP_IN_M = 0.15  # the door's waypoint in the middle of the opening (half the wall)
OUTSIDE_WP_M = 0.9  # the waypoint in front of the door, outside
LIGHT = {
    "hearth": {"color": [1.0, 0.62, 0.32], "range": 6.5, "intensity": 2.6, "flicker": 0.3},
    "candle": {"color": [1.0, 0.75, 0.45], "range": 5.0, "intensity": 1.8, "flicker": 0.1},
}
BIG_ROOM_M2 = 60.0  # a second light in rooms bigger than this


@dataclass
class InsideSpec:
    mobs: list[tuple[str, str]] = field(default_factory=list)  # (type, count or "R" residents)
    freepoints: list[tuple[str, int]] = field(default_factory=list)
    hearth: bool = False


def inside_spec(spec: dict[str, Any], where: str) -> InsideSpec:
    """``uses.<use>.inside`` checked: mobs ``type:N`` (``N`` may be ``R``: the residents, at most
    3), freepoints ``TYPE:N``, ``hearth``."""
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
    return out


def _rect(cx: float, cz: float, ax: float, az: float, length: float, depth: float) -> Polygon:
    """Rectangle centred at (cx, cz), ``length`` along (ax, az), ``depth`` across."""
    r = box(-length / 2, -depth / 2, length / 2, depth / 2)
    return affinity.translate(affinity.rotate(r, math.atan2(az, ax), use_radians=True), cx, cz)


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

    @property
    def entry(self) -> tuple[float, float]:
        """The room's waypoint just inside the door."""
        return (self.door_mid[0] + self.inward[0] * INSIDE_WP_M,
                self.door_mid[1] + self.inward[1] * INSIDE_WP_M)  # fmt: skip

    def reachable(self, p: tuple[float, float]) -> bool:
        """A straight walk from the room's waypoint to ``p`` past all furniture."""
        lane = LineString([self.entry, p]).buffer(0.3)
        return not any(lane.intersects(t) for t in self.taken)

    def fits(self, shape: Polygon, margin: float = MOVE_M) -> bool:
        if not self.poly.buffer(-0.02).contains(shape) or shape.intersects(self.keep):
            return False
        return not any(shape.distance(t) < margin for t in self.taken)


def _room(e: dict[str, Any]) -> _Room:
    r = e["interior"]
    poly = Polygon(r["ring"])
    d = r["door"]
    mid = ((d["from"][0] + d["to"][0]) / 2, (d["from"][1] + d["to"][1]) / 2)
    inward = (-d["normal"][0], -d["normal"][1])
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
    sweep = Point(hx, hz).buffer(w).intersection(quarter)
    blade = LineString([(hx, hz), (hx + inward[0] * w, hz + inward[1] * w)]).buffer(0.06)
    keep = path.union(zone).union(sweep)
    return _Room(poly, float(r["floor"]), float(r["ceiling"]), keep, mid, inward, [blade])


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
    room: _Room, kind: str, facing: tuple[float, float] | None = None
) -> tuple[Polygon, tuple[float, float], tuple[float, float]] | None:
    """A spot with the back to a wall: (footprint, centre, front direction into the room); the
    first along the longest walls, or with ``facing`` the one turned most towards that point
    (the hearth towards the door: seen on coming in) and reached from the room's waypoint."""
    length, depth = SIZE[kind]
    best: tuple[float, Polygon, tuple[float, float], tuple[float, float]] | None = None
    for a, (ux, uz), wall_len in _walls(room):
        nx, nz = -uz, ux  # into the room (counter-clockwise ring)
        t = length / 2 + 0.1
        while t <= wall_len - length / 2 - 0.1:
            cx = a[0] + ux * t + nx * (WALL_GAP_M + depth / 2)
            cz = a[1] + uz * t + nz * (WALL_GAP_M + depth / 2)
            t += STEP_M
            shape = _rect(cx, cz, ux, uz, length, depth)
            reach = depth / 2 + SLOT.get(kind, 0.6)
            slot = (cx + nx * reach, cz + nz * reach)
            if not (room.fits(shape) and room.poly.contains(Point(slot))):
                continue
            if room.keep.contains(Point(slot)):
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


class _House:
    """Places in the room of one house."""

    def __init__(self, plan: InsidePlan, h: House, room: _Room) -> None:
        self.plan, self.h, self.room = plan, h, room
        self.tag = f"LEO_{h.token}_{name_part(short_id(h.id))}_INNEN"
        self.counts: dict[str, int] = {}

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
        self.room.taken.append(Point(pos).buffer(0.3))

    def fail(self, what: str) -> None:
        self.plan.failed.append({"house": self.h.id, "what": what, "reason": "no room"})

    def build(self, spec: InsideSpec, routine_wp: str) -> None:
        room, h = self.room, self.h
        table_at, hearth_at = None, None
        # the hearth first: the furniture keeps out of its way and out of the view on it
        if spec.hearth:
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
                hearth_at = (centre, front)
                # keep the way to the fire and the view on it from the door free of furniture
                reach = SIZE["hearth"][1] / 2 + SLOT["hearth"]
                fire = (centre[0] + front[0] * reach, centre[1] + front[1] * reach)
                room.keep = room.keep.union(LineString([room.entry, fire]).buffer(0.6))
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
                    table_at = (cx, cz)
                    continue
                wall_spot = _against_wall(room, kind)
                if wall_spot is None:
                    self.fail(kind)
                    continue
                shape, centre, front = wall_spot
                self.mob(kind, centre, front)
                room.taken.append(shape)
                if kind == "chest":  # a counter: the trader stands at its slot
                    reach = SIZE["chest"][1] / 2 + SLOT["chest"]
                    slot = (centre[0] + front[0] * reach, centre[1] + front[1] * reach)
                    room.stands.append((slot, (-front[0], -front[1])))
        want = 2 if room.poly.area > BIG_ROOM_M2 else 1
        c = room.poly.centroid
        for k in range(want - (1 if hearth_at else 0)):
            where = table_at if (table_at and k == 0) else (c.x, c.y)
            light = {"light": dict(LIGHT["candle"])}
            self.vob("light", f"LIGHT_{self.tag}_{k + 1}", where, (0.0, 1.0),
                     height=room.ceiling - 0.6, components=light)  # fmt: skip
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
                index: dict[str, Any], routine_wps: dict[str, str]) -> InsidePlan:  # fmt: skip
    """Everything inside the enterable houses (those with ``interior`` in the index)."""
    plan = InsidePlan()
    rooms = {e["id"]: e for e in index.get("entries", []) if e.get("interior")}
    for h in houses:
        if h.inside and h.id in rooms:
            _House(plan, h, _room(rooms[h.id])).build(specs.get(h.use, InsideSpec()),
                                                      routine_wps.get(h.id, ""))  # fmt: skip
    return plan


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
            if kind == "SMALLTALK":
                p, q = (x - 0.6, z), (x + 0.6, z)
                if (room.fits(Point(p).buffer(0.3)) and room.fits(Point(q).buffer(0.3))
                        and room.reachable(p) and room.reachable(q)):  # fmt: skip
                    d = math.dist((x, z), (c.x, c.y))
                    if best is None or d < best[0]:
                        best = (d, [(p, (1.0, 0.0)), (q, (-1.0, 0.0))])
            elif room.fits(Point(x, z).buffer(0.3)) and room.reachable((x, z)):
                d = math.dist((x, z), (c.x, c.y))
                if best is None or d > best[0]:  # out of the way: away from the middle
                    dx, dz = c.x - x, c.y - z
                    ln = math.hypot(dx, dz) or 1.0
                    best = (d, [((x, z), (dx / ln, dz / ln))])
            z += STEP_M
        x += STEP_M
    return None if best is None else best[1]
