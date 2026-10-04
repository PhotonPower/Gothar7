"""Routine places at the houses of ``data/<site>/uses.json`` (W7, PR B).

For every house with a use: a waypoint in front of its door for routines (``WP_LEO_<USE>_<SHORT>``),
freepoints by type along the facade (``FP_<TYPE>_LEO_<USE>_<SHORT>_<NN>``) and the mobs of the use
(anvil, bench, chest) with their backs to the wall beside the door. Everything stands on free,
level ground in front of the house; what does not fit is reported, not forced. ``assemble``
writes the mobs as vobs, ``waynet`` the waypoints and freepoints (``uses_places.json``).
"""

from __future__ import annotations

import json
import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from gothar_worldgen.uses.suggest import USES, short_id
from gothar_worldgen.waynet.generate import name_part

MOB_TYPES = {"chest", "anvil", "bed", "bench", "door"}
FP_TYPES = {"SIT", "STAND", "SMALLTALK", "ROAM", "SLEEP", "CAMPFIRE", "LEAN", "SWEEP", "DRINK",
            "TRAIN", "CHOP", "HARVEST", "WATER", "REPAIR"}  # fmt: skip
# mob footprint (half width along the wall, depth from the wall) and the slot in front (mobs.toml)
MOB_SIZE = {"chest": (0.45, 0.6), "anvil": (0.4, 0.5), "bench": (0.75, 0.35), "bed": (1.0, 0.9)}
MOB_SLOT = {"chest": 0.65, "anvil": 0.6, "bench": 0.38, "bed": 0.75}
WALL_GAP_M = 0.1  # mobs stand this far off the wall
DOOR_KEEP_M = 1.3  # nothing within this distance of the door axis along the wall
LANE_M = 1.5  # in front of a mob's slot this much ground stays free (the lane past it)
WAY_KEEP_M = 1.2  # a mob keeps this much more than its half width from a walkable way's axis
ROUTINE_OUT_M = 1.8  # the routine waypoint lies this far in front of the wall
SPACING_M = 0.9  # places keep this distance from each other and from mobs
LEVEL_M = 0.6  # a place lies at most this much above or below the routine waypoint
# distance of a freepoint from the wall and whether the figure faces the house (else the street)
FP_ROW = {"LEAN": (0.45, False), "STAND": (1.6, False), "SWEEP": (1.4, False),
          "SIT": (2.4, True), "DRINK": (2.0, False), "SMALLTALK": (3.0, None), "CHOP": (2.2, False),
          "HARVEST": (3.0, False), "REPAIR": (1.6, True), "ROAM": (3.5, False),
          "WATER": (2.0, False), "TRAIN": (3.0, False), "SLEEP": (1.0, True),
          "CAMPFIRE": (3.0, True)}  # fmt: skip
OFFSETS_M = (1.6, -1.6, 2.8, -2.8, 4.0, -4.0, 5.2, -5.2, 6.4, -6.4)  # along the wall from the door


class UsesError(Exception):
    pass


@dataclass
class House:
    id: str
    use: str
    name: str = ""
    trade: str = ""
    residents: int = 0
    inside: bool = False
    owner: str = ""

    @property
    def token(self) -> str:
        """Name part of the use in place names: the trade where there is one (SCHUSTER)."""
        return name_part(self.trade or self.use)


@dataclass
class UsesDoc:
    houses: list[House]
    uses: dict[str, dict[str, list[tuple[str, int]]]]
    inside: dict[str, dict[str, Any]] = field(default_factory=dict)  # uses.<use>.inside (raw)


def _counts(
    items: Sequence[str], known: set[str], where: str, upper: bool
) -> list[tuple[str, int]]:
    out = []
    for item in items:
        kind, _, n = str(item).partition(":")
        kind = kind.upper() if upper else kind
        if kind not in known or not n.isdigit() or int(n) < 1:
            raise UsesError(f"{where}: {item!r} must be TYPE:N with TYPE one of {sorted(known)}")
        out.append((kind, int(n)))
    return out


def load_uses(path: Path) -> UsesDoc:
    """``uses.json`` checked: known uses, unique ids, ``TYPE:N`` freepoints and mobs."""
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise UsesError(f"{path.name}: {e}") from None
    if doc.get("version") != 1:
        raise UsesError(f"{path.name}: version must be 1")
    uses: dict[str, dict[str, list[tuple[str, int]]]] = {}
    inside: dict[str, dict[str, Any]] = {}
    for use, spec in (doc.get("uses") or {}).items():
        if "inside" in spec:
            inside[use] = dict(spec["inside"])
        if use not in USES:
            raise UsesError(f"{path.name}: uses.{use}: unknown use")
        uses[use] = {
            "freepoints": _counts(spec.get("freepoints", []), FP_TYPES, f"uses.{use}", True),
            "mobs": _counts(spec.get("mobs", []), MOB_TYPES, f"uses.{use}", False),
        }
    houses, seen = [], set()
    for k, h in enumerate(doc.get("houses") or []):
        where = f"{path.name}: houses[{k}]"
        if "id" not in h or "use" not in h:
            raise UsesError(f"{where}: needs id and use")
        if h["use"] not in USES:
            raise UsesError(f"{where}: unknown use {h['use']!r}")
        if h["id"] in seen:
            raise UsesError(f"{where}: {h['id']} is listed twice")
        seen.add(h["id"])
        houses.append(House(h["id"], h["use"], h.get("name", ""), h.get("trade", ""),
                            int(h.get("residents", 0)), bool(h.get("inside", False)),
                            h.get("owner", "")))  # fmt: skip
    return UsesDoc(houses, uses, inside)


@dataclass
class Place:
    kind: str  # "wp", "fp" or "mob"
    name: str
    house: str
    pos: tuple[float, float]
    dir: tuple[float, float]
    mob: str = ""  # mob type
    link: str = ""  # wp: the door point it hangs on (filled by the waynet)

    def json(self) -> dict[str, Any]:
        out: dict[str, Any] = {"kind": self.kind, "name": self.name, "house": self.house,
                               "pos": [round(self.pos[0], 3), round(self.pos[1], 3)],
                               "dir": [round(self.dir[0], 4), round(self.dir[1], 4)]}  # fmt: skip
        if self.mob:
            out["mob"] = self.mob
        return out


@dataclass
class Plan:
    places: list[Place] = field(default_factory=list)
    failed: list[dict[str, Any]] = field(default_factory=list)

    def json(self, doc: UsesDoc) -> dict[str, Any]:
        houses = []
        for h in doc.houses:
            mine = [p for p in self.places if p.house == h.id]
            houses.append({"id": h.id, "short": short_id(h.id), "use": h.use, "trade": h.trade,
                           "name": h.name, "residents": h.residents, "inside": h.inside,
                           "owner": h.owner,
                           "waypoint": next((p.name for p in mine if p.kind == "wp"), None),
                           "freepoints": [p.name for p in mine if p.kind == "fp"],
                           "mobs": [p.name for p in mine if p.kind == "mob"]})  # fmt: skip
        return {"version": 1, "places": [p.json() for p in self.places], "houses": houses,
                "failed": self.failed}  # fmt: skip


def _yaw_quat(dx: float, dz: float) -> list[float]:
    """Rotation about +Y turning the model's +Z into (dx, dz)."""
    a = math.atan2(dx, dz)
    return [0.0, round(math.sin(a / 2), 5) + 0.0, 0.0, round(math.cos(a / 2), 5) + 0.0]


class _Front:
    """The free ground in front of one house: what is taken, what still fits."""

    def __init__(self, free: Callable[[float, float, float], bool],
                 height: Callable[[float, float], float], rec: Sequence[Any],
                 way: Callable[[float, float], float] | None = None) -> None:  # fmt: skip
        self.free, self.height, self.way = free, height, way
        x, z = float(rec[0]), float(rec[1])
        self.n = (float(rec[4]), float(rec[5]))  # outward
        self.t = (-self.n[1], self.n[0])  # along the wall
        self.wall = (x - self.n[0] * 0.6, z - self.n[1] * 0.6)  # the door on the wall
        self.routine = self.at(0.0, ROUTINE_OUT_M)
        self.base_y = height(*self.routine)
        self.taken: list[tuple[float, float, float]] = [(*self.routine, 0.6)]

    def at(self, along: float, out: float) -> tuple[float, float]:
        return (self.wall[0] + self.t[0] * along + self.n[0] * out,
                self.wall[1] + self.t[1] * along + self.n[1] * out)  # fmt: skip

    def fits(self, p: tuple[float, float], r: float) -> bool:
        if not self.free(p[0], p[1], r) or abs(self.height(*p) - self.base_y) > LEVEL_M:
            return False
        return all(math.dist(p, (qx, qz)) >= max(SPACING_M, r + qr) for qx, qz, qr in self.taken)

    def take(self, p: tuple[float, float], r: float) -> None:
        self.taken.append((p[0], p[1], r))

    def mob_spot(self, mob: str) -> tuple[float, float] | None:
        """Beside the door, back to the wall, its corners and its slot free."""
        half, depth = MOB_SIZE.get(mob, (0.5, 0.5))
        for s in OFFSETS_M:
            if abs(s) - half < DOOR_KEEP_M:
                continue
            c = self.at(s, WALL_GAP_M + depth / 2)
            corners = [self.at(s + a, WALL_GAP_M + depth / 2 + b)
                       for a in (-half, half) for b in (-depth / 2, depth / 2)]  # fmt: skip
            slot = self.at(s, WALL_GAP_M + depth / 2 + MOB_SLOT.get(mob, 0.6))
            # the way past it stays open: free ground well beyond the slot (narrow lanes)
            lane = self.at(s, WALL_GAP_M + depth / 2 + MOB_SLOT.get(mob, 0.6) + LANE_M)
            if (all(self.free(qx, qz, 0.05) for qx, qz in corners) and self.fits(c, half)
                    and self.free(slot[0], slot[1], 0.35)
                    and self.free(lane[0], lane[1], 0.6)
                    and (self.way is None or self.way(*c) >= half + WAY_KEEP_M)):  # fmt: skip
                return c
        return None


def plan_places(
    doc: UsesDoc,
    doors: dict[str, Sequence[Any]],
    free: Callable[[float, float, float], bool],
    height: Callable[[float, float], float],
    way: Callable[[float, float], float] | None = None,
) -> Plan:
    """Places for every house. ``doors``: house id -> door record of the buildings index
    (x, z, floor, kind, nx, nz; the point lies 0.6 m in front of the wall); ``free(x, z, r)``:
    no collision body nearer than ``r``; ``way(x, z)``: distance to the nearest walkable way
    (mobs stay off the ways the waynet follows)."""
    plan = Plan()
    names: set[str] = set()

    def unique(name: str) -> str:
        base, k = name, 2
        while name in names:
            name = f"{base}_{k}"
            k += 1
        names.add(name)
        return name

    for h in doc.houses:
        rec = doors.get(h.id)
        if rec is None or (len(rec) > 3 and rec[3] == "blocked"):
            plan.failed.append({"house": h.id, "what": "house", "reason": "no usable door"})
            continue
        front = _Front(free, height, rec, way)
        if not free(*front.routine, 0.5):
            plan.failed.append({"house": h.id, "what": "waypoint", "reason": "blocked in front"})
            continue
        short = name_part(short_id(h.id))
        nx, nz = front.n
        plan.places.append(Place("wp", unique(f"WP_LEO_{h.token}_{short}"), h.id, front.routine,
                                 (-nx, -nz)))  # fmt: skip
        spec = doc.uses.get(h.use, {"freepoints": [], "mobs": []})
        anvils: list[Place] = []
        for mob, n in spec["mobs"]:
            for k in range(n):
                spot = front.mob_spot(mob)
                if spot is None:
                    plan.failed.append({"house": h.id, "what": f"mob {mob}", "reason": "no room"})
                    continue
                # the mob models are centred on their origin, front +Z (mobs.toml)
                name = unique(f"MOB_LEO_{h.token}_{short}_{name_part(mob)}_{k + 1}")
                place = Place("mob", name, h.id, spot, (nx, nz), mob)
                plan.places.append(place)
                if mob == "anvil":
                    anvils.append(place)
                front.take(spot, MOB_SIZE.get(mob, (0.5, 0.5))[0] + 0.2)
        counter: dict[str, int] = {}

        def add_fp(kind: str, spot: tuple[float, float], d: tuple[float, float],
                   house: str = h.id, sh: str = short, token: str = h.token,
                   fr: _Front = front, cnt: dict[str, int] = counter) -> None:  # fmt: skip
            cnt[kind] = cnt.get(kind, 0) + 1
            name = unique(f"FP_{kind}_LEO_{token}_{sh}_{cnt[kind]:02d}")
            plan.places.append(Place("fp", name, house, spot, d))
            fr.take(spot, 0.45)

        for kind, n in spec["freepoints"]:
            dist, faces_house = FP_ROW.get(kind, (2.0, False))
            for _ in range(n):
                if kind == "REPAIR" and anvils:  # at the anvil's slot, facing it
                    a = anvils[counter.get("REPAIR", 0) % len(anvils)]
                    slot = (a.pos[0] + nx * MOB_SLOT["anvil"], a.pos[1] + nz * MOB_SLOT["anvil"])
                    if free(slot[0], slot[1], 0.35):
                        add_fp(kind, slot, (-nx, -nz))
                        continue
                done = False
                for s in OFFSETS_M:
                    if abs(s) < DOOR_KEEP_M:
                        continue
                    p = front.at(s, dist)
                    if kind == "SMALLTALK":  # a pair facing each other along the wall
                        q = front.at(s + 1.2, dist)
                        if front.fits(p, 0.45) and front.fits(q, 0.45):
                            tx, tz = front.t
                            add_fp(kind, p, (tx, tz))
                            add_fp(kind, q, (-tx, -tz))
                            done = True
                            break
                    elif front.fits(p, 0.45):
                        add_fp(kind, p, (-nx, -nz) if faces_house else (nx, nz))
                        done = True
                        break
                if not done:
                    plan.failed.append({"house": h.id, "what": f"FP_{kind}", "reason": "no room"})
    return plan


def mob_vobs(plan: Plan, height: Callable[[float, float], float]) -> list[dict[str, Any]]:
    """The mobs as vob specs for ``assemble`` (key, name, pos, rot, mesh, definition)."""
    out = []
    for p in plan.places:
        if p.kind != "mob":
            continue
        out.append({"key": f"use:{p.name}", "name": p.name,
                    "pos": [p.pos[0], height(*p.pos), p.pos[1]], "rot": _yaw_quat(*p.dir),
                    "mesh": f"mobs/{p.mob}.glb", "definition": p.mob})  # fmt: skip
    return out


def routine_table_md(places: dict[str, Any]) -> str:
    """Routine places per house for writing the Leonberg routines (engine, figuren)."""
    rows = [
        "| Kürzel | Nutzung | Name | Bewohner | Routinen-Wegpunkt | Freepoints | Mobs | Innen |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for h in places.get("houses", []):
        use = USES[h["use"]][0] + (f" ({h['trade']})" if h.get("trade") else "")
        fps = ", ".join(f"`{n}`" for n in h["freepoints"]) or "–"
        mobs = ", ".join(f"`{n}`" for n in h["mobs"]) or "–"
        wp = f"`{h['waypoint']}`" if h.get("waypoint") else "– (kein Zugang)"
        inside = ", ".join(f"`{n}`" for n in h.get("insidePlaces", []) if not n.startswith("WP_")
                           or n.endswith("_INNEN"))  # fmt: skip
        rows.append(f"| {h['short']} | {use} | {h.get('name') or '–'} | {h['residents']} | {wp} | "
                    f"{fps} | {mobs} | {inside} |")  # fmt: skip
    return "\n".join(rows) + "\n"


DOOR_IN_REVEAL_M = 0.06  # the door blade stands this far inside the room's wall face


def door_mobs(index: dict[str, Any], opened: bool) -> list[dict[str, Any]]:
    """A door mob in the opening of every enterable house (index ``interior``): hinge at one
    jamb, the blade (``mobs/door.glb``, along +X) across the opening, its front (+Z) facing out.
    ``opened``: placed swung 90 degrees into the room, until NPCs open doors (W7, koordinator)."""
    out = []
    for e in index.get("entries", []):
        room = e.get("interior")
        if not room:
            continue
        d = room["door"]
        nx, nz = d["normal"]
        a = math.atan2(nx, nz)
        xdir = (math.cos(a), -math.sin(a))  # the model's +X in the world for that turn
        f, t = d["from"], d["to"]
        hinge = f if (t[0] - f[0]) * xdir[0] + (t[1] - f[1]) * xdir[1] > 0 else t
        hx, hz = hinge[0] + nx * DOOR_IN_REVEAL_M, hinge[1] + nz * DOOR_IN_REVEAL_M
        turn = a + math.pi / 2 if opened else a
        name = f"MOB_LEO_TUER_{name_part(short_id(e['id']))}"
        out.append({"key": f"use:door:{e['id']}", "name": name, "pos": [hx, d["floor"], hz],
                    "rot": [0.0, round(math.sin(turn / 2), 5) + 0.0, 0.0,
                            round(math.cos(turn / 2), 5) + 0.0],
                    "mesh": "mobs/door.glb", "definition": "door"})  # fmt: skip
    return out
