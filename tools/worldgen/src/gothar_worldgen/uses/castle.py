"""The castle's inside comes alive (W7 D1b, ``docs/design/schloss-dungeon.md``).

The walkable main wing (``schloss_interior.py``: cellar, ground and first floor, the rooms in a row,
the spiral stair in the tower) gets its furniture, light, waypoints and freepoints and the rooms'
indoor zones. What stands where is data (``data/<site>/castle_inside.json``), in the wing's frame:
``s`` along the wing from its end a, ``t`` across it (+ the court, - the garden). An entry stands
against a wall (``wall`` garden | court | a | b with the ``s`` or ``t`` of its middle) or free in
the room (``at`` [s, t], ``dir`` its front, or ``along`` its long axis). The kinds:

- ``mob:<type>`` (bed, chest, bench), ``table`` (a table with a bench on either side, set like the
  houses' tables, a candle unless ``candle`` false), ``prop:<kind>`` (``props/<kind>.glb``;
  ``hang`` from the ceiling), ``light:lantern`` / ``light:torch`` (the lamp on the wall and its
  light), ``fp:<TYPE>`` (a freepoint; ``fp`` on an entry puts one before it, facing it);
- the giant cask's front (``front``: a ``mover`` ``secret_wall`` swinging open about its hinge)
  and the ``lever`` (the crooked torch holder: mob ``lever`` with ``targets`` and ``action``),
  contract in schloss-dungeon.md §5 and engine's dungeon.md;
- ``lights`` (``max``: the room's light budget instead of ``roomLights``).

Every room gets daylight at one window (the court's first), the hearth its fire, the tower a lantern
at each landing. Waypoints: a lane along the doorways of every storey (``doorT``), a point in each
room's middle on it (``WP_<TAG>_<ROOM>``, the routine place), the doorways, the ways to the portal
and the tower, the spiral (a point every 30 degrees); places for freepoints and mobs farther than
``FP_NEAR_M`` from a waypoint get one of their own. The waypoints are tied as a tree (``link``),
from the court in front of the portal. ``check`` lists what is wrong with the data: things outside
their room, on top of each other, on the lane or the ways to the portal and the tower, in front of
windows, places without a free way, too many lights.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

from gothar_worldgen.uses.inside import (
    BENCH_OFF_M,
    HANG_OUT_M,
    LIGHT,
    PROP_SIZE,
    SIZE,
    SLOT,
    TABLE_ITEMS,
    WALL_BOARDS,
    WALL_GAP_M,
    WINDOW_LIGHT_IN_M,
    _quat,
)
from gothar_worldgen.waynet.write import tidy

FORMAT = "gothar-castle-inside"
VERSION = 1
HOUSE = "schloss"  # the ``house`` of the castle's places (uses_places.json, waynet report)
LIGHT_TORCH = {"color": [1.0, 0.6, 0.3], "range": 6.0, "intensity": 2.0, "flicker": 0.35}
# footprints (along the wall, depth) of what the houses do not have
SIZES = {"giant_barrel": (2.75, 2.8), "bookshelf": (1.2, 0.36), "lectern": (0.6, 0.5),
         "altar": (1.7, 0.9), "cell_bars": (1.35, 0.1), "barrel_stack": (1.9, 0.8),
         "candlestick": (0.12, 0.12), "oven": (1.3, 1.0)}  # fmt: skip
# how high things reach over the floor (the windows' niches start low on the ground floor)
HEIGHT = {"shelf": 1.7, "bookshelf": 2.0, "barrel_rack": 1.15, "crate_stack": 1.0,
          "barrel_stack": 1.5, "hearth": 2.5, "giant_barrel": 2.9, "altar": 1.8,
          "cell_bars": 2.2, "sacks": 0.6, "crate": 0.45, "barrel": 0.9, "bed": 0.7, "chest": 0.6,
          "bench": 0.45, "washstand": 0.9, "stool": 0.45, "basket": 0.5, "bucket": 0.4,
          "firewood": 0.6, "lectern": 1.25, "fur": 0.03}  # fmt: skip
ON_WALL = {"lantern": (1.6, 2.3), "wall_torch": (1.4, 2.4), "torch_lever": (1.4, 2.4)}  # y range
BOARD_Y = (1.0, 2.3)  # what hangs on the wall (WALL_BOARDS) covers this
HANGING = {"sausages", "herbs"}  # from the ceiling
FLAT = {"fur", "rug"}  # on the floor, nothing to bump into
ON_TOP = {"candlestick"}  # on a table or the altar
SLOTS = {**SLOT, "bed": 0.75, "chest": 0.65, "bench": 0.38, "lever": 0.6}
READ_M = 0.45  # a reader stands this far in front of a shelf or lectern
COOK_M = 0.6  # the cook this far in front of the hearth
FP_NEAR_M = 3.0  # a freepoint or mob has a waypoint this near in a free line (inside.FP_NEAR_M)
WAY_R_M = 0.3  # straight ways keep this clear of furniture
SPIRAL_R_M = 1.6  # the spiral's waypoints this far from the newel's axis
SPIRAL_STEP_DEG = 30.0
TOWER_LANTERN_DEG = -50.0  # the landing's lantern on the tower wall, from the doorway's side
DOOR_IN_M = 0.6  # the waypoint inside a doorway to the tower, past the wing's inner face
FRONT_MOVER = {"kind": "secret_wall", "open": False, "seconds": 2.5, "offset": [0, 0, 0],
               "axis": [0, 1, 0], "angle": -95, "crush": "stop"}  # fmt: skip
DIRS = {"+s": (1.0, 0.0), "-s": (-1.0, 0.0), "+t": (0.0, 1.0), "-t": (0.0, -1.0)}


class CastleError(Exception):
    """Inconsistent castle data; the message is meant for the user."""


def load_castle(path: Path) -> dict[str, Any]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if doc.get("format") != FORMAT or doc.get("version") != VERSION:
        raise CastleError(f"{path.name}: not a {FORMAT} v{VERSION} file")
    return doc


@dataclass
class CastlePlan:
    vobs: list[dict[str, Any]] = field(default_factory=list)  # specs for assemble
    places: list[dict[str, Any]] = field(default_factory=list)  # waypoints, freepoints
    zones: list[dict[str, Any]] = field(default_factory=list)  # indoor zones
    failed: list[str] = field(default_factory=list)  # what ``check`` found

    def table_md(self) -> str:
        """Routine places of the castle per room (for the residents' routines, engine)."""
        rows = ["| Raum | Routinen-Wegpunkt | Freepoints | Mobs |", "|---|---|---|---|"]
        rooms: dict[str, dict[str, list[str]]] = {}
        for p in self.places:
            if p["kind"] == "fp":
                rooms.setdefault(p["room"], {"wp": [], "fp": [], "mob": []})["fp"].append(p["name"])
            elif p.get("routine"):
                rooms.setdefault(p["room"], {"wp": [], "fp": [], "mob": []})["wp"].append(p["name"])
        for v in self.vobs:
            if v["type"] == "mob" and v.get("room"):
                rooms.setdefault(v["room"], {"wp": [], "fp": [], "mob": []})["mob"].append(
                    v["name"]
                )
        for room, r in rooms.items():
            cells = [", ".join(f"`{n}`" for n in r[k]) or "–" for k in ("wp", "fp", "mob")]
            rows.append(f"| {room} | " + " | ".join(cells) + " |")
        return "\n".join(rows) + "\n"


@dataclass
class _Thing:
    """Something placed in a room, in the wing's frame."""

    kind: str  # mob / prop / table / light / lever
    model: str
    centre: tuple[float, float]  # s, t
    front: tuple[float, float]  # in (s, t)
    length: float
    depth: float
    floor: bool  # takes floor room
    y0: float  # over the floor: what it covers on its wall
    y1: float
    wall: str | None
    entry: dict[str, Any]

    def shape(self) -> Polygon:
        fs, ft = self.front
        hl, hd = self.length / 2, self.depth / 2
        if abs(fs) > abs(ft):  # its long axis across the wing (t)
            return box(self.centre[0] - hd, self.centre[1] - hl, self.centre[0] + hd,
                       self.centre[1] + hl)  # fmt: skip
        return box(self.centre[0] - hl, self.centre[1] - hd, self.centre[0] + hl,
                   self.centre[1] + hd)  # fmt: skip


class _Castle:
    def __init__(self, built: dict[str, Any], data: dict[str, Any], geo: Any) -> None:  # noqa: ANN401
        self.data, self.geo = data, geo
        m = geo.build(built)
        self.w = m.inner
        inside = built["interior"]
        self.inside = inside
        self.tower = geo.interior.moved_tower(self.w, built["stairTower"])
        self.ins = geo.interior.Inside(geo.Model(), self.w, inside, self.tower, built["portal"])
        self.t_in = self.ins.t_in
        self.door_t = self.ins.door_t
        self.lane = float(data.get("laneHalfM", 1.2))
        self.tag = str(data.get("tag", "LEO_SCHLOSS"))
        self.plan = CastlePlan()
        self.counts: dict[str, int] = {}
        self.wps: dict[str, list[tuple[str, tuple[float, float]]]] = {}  # storey -> (name, st)
        self.storeys = {k: (y0, y1) for k, y0, y1 in geo.interior.storeys(inside)}
        self._rooms: list[tuple[str, str, float, float, list[_Thing], list[Any]]] = []

    # --- frame ------------------------------------------------------------------------------
    def xz(self, s: float, t: float) -> tuple[float, float]:
        x, _, z = self.w.p(s, t, 0.0)
        return (x, z)

    def vec(self, d: tuple[float, float]) -> tuple[float, float]:
        u, n = self.w.u, self.w.n
        return (d[0] * u[0] + d[1] * n[0], d[0] * u[1] + d[1] * n[1])

    def world_of(self, th: _Thing, local: tuple[float, float, float], floor: float,
                 ) -> list[float]:  # fmt: skip
        """A point of a thing's model (x right, y up, z front) in the world."""
        fx, fz = self.vec(th.front)
        rx, rz = fz, -fx  # the model's +X (the front turned about +Y, see inside._quat)
        cx, cz = self.xz(*th.centre)
        return [
            cx + rx * local[0] + fx * local[2],
            floor + local[1],
            cz + rz * local[0] + fz * local[2],
        ]

    def name(self, prefix: str, rm: str, kind: str) -> str:
        """``<prefix>_<rm>_<KIND>_<n>``, counted per room and kind."""
        key = f"{prefix}:{rm}:{kind}"
        self.counts[key] = self.counts.get(key, 0) + 1
        return f"{prefix}_{rm}_{kind.upper()}_{self.counts[key]}"

    def vob(self, kind: str, name: str, pos: list[float], front: tuple[float, float],
            room: str | None = None, **comp: Any) -> None:  # noqa: ANN401  # fmt: skip
        fx, fz = self.vec(front)
        spec: dict[str, Any] = {"key": f"castle:{name}", "name": name, "type": kind,
                                "pos": [tidy(c) for c in pos], "rot": _quat(fx, fz)}  # fmt: skip
        if room is not None:
            spec["room"] = room
        self.plan.vobs.append({**spec, **comp})

    # --- rooms ------------------------------------------------------------------------------
    def rooms(self) -> list[tuple[str, str, float, float]]:
        """(storey, room name, s from, s to) of every room."""
        return [(k, n, a, b) for k in self.storeys for n, a, b in self.ins.rooms(k)]

    def windows(self, key: str) -> list[tuple[int, float, float, float]]:
        """(side, s, sill, top) over the floor of the window niches of a storey."""
        y0, y1 = self.storeys[key]
        return [(side, u, wy0 - y0, wy1 - y0) for side, u, wy0, wy1 in self.w.windows
                if wy0 >= y0 + 0.2 and wy1 <= y1 - 0.1]  # fmt: skip

    def keep(self, key: str, a: float, b: float) -> list[Polygon]:
        """Floor kept free in a room: the lane, the ways to the portal and to the tower."""
        out = [box(a, self.door_t - self.lane, b, self.door_t + self.lane)]
        ts = self.ins.tower_s
        if ts is not None and a < ts < b:
            side = -1.0 if self.ins.tower_t < 0 else 1.0
            out.append(box(ts - 1.3, min(side * self.t_in, self.door_t), ts + 1.3,
                           max(side * self.t_in, self.door_t)))  # fmt: skip
        ps = self.ins.portal_s
        if key == "eg" and ps is not None and a < ps < b:
            out.append(box(ps - self.ins.portal_w / 2 - 0.1, self.door_t, ps + self.ins.portal_w / 2
                           + 0.1, self.t_in))  # fmt: skip
        return out

    def place(self, e: dict[str, Any], model: str, a: float, b: float) -> _Thing:
        size = SIZES.get(model) or SIZE.get(model) or PROP_SIZE.get(model) or (0.3, 0.25)
        length, depth = size
        mounted = model in ON_WALL
        if mounted:
            depth = 0.0
        y0, y1 = 0.0, HEIGHT.get(model, 1.0)
        if model in ON_WALL:
            y0, y1 = ON_WALL[model]
        elif model in WALL_BOARDS:
            y0, y1 = BOARD_Y
        floor = not (mounted or model in WALL_BOARDS | HANGING | FLAT | ON_TOP)
        wall = e.get("wall")
        off = 0.0 if mounted else WALL_GAP_M + depth / 2
        if model in HANGING:
            off = HANG_OUT_M
        if wall == "garden":
            centre, front = (float(e["s"]), -self.t_in + off), (0.0, 1.0)
        elif wall == "court":
            centre, front = (float(e["s"]), self.t_in - off), (0.0, -1.0)
        elif wall == "a":
            centre, front = (a + off, float(e["t"])), (1.0, 0.0)
        elif wall == "b":
            centre, front = (b - off, float(e["t"])), (-1.0, 0.0)
        elif "at" in e:
            centre = (float(e["at"][0]), float(e["at"][1]))
            if "dir" in e:
                front = DIRS[e["dir"]]
            else:  # its long axis along s: the front across
                front = (0.0, 1.0) if e.get("along", "s") == "s" else (1.0, 0.0)
        else:
            raise CastleError(f"{e}: needs wall or at")
        return _Thing("", model, centre, front, length, depth, floor, y0, y1, wall, e)

    # --- build ------------------------------------------------------------------------------
    def build(self) -> CastlePlan:
        geo = self.geo
        for key, room, a, b in self.rooms():
            part = geo.interior.part_name(key, room)
            entries = self.data["rooms"].get(part, [])
            self.room(key, room.upper(), a, b, entries)
        self.zones()
        self.tower_lights()
        self.waypoints()
        return self.plan

    def room(self, key: str, room: str, a: float, b: float, entries: list[dict[str, Any]]) -> None:
        floor_y = self.storeys[key][0]
        ceiling = self.storeys[key][1]
        things: list[_Thing] = []
        spots: list[tuple[str, tuple[float, float], tuple[float, float]]] = []  # fp/mob places
        lights = 0
        cap = int(self.data.get("roomLights", 4))
        rm = f"{self.tag}_{room}"
        for e in entries:
            what = str(e["what"])
            kind, _, model = what.partition(":")
            if kind == "lights":
                cap = int(e["max"])
                continue
            if kind == "fp":
                st = (float(e["at"][0]), float(e["at"][1]))
                spots.append((f"fp:{model}", st, DIRS[e["dir"]]))
                continue
            if kind == "table":
                th = self.place({**e, "along": e.get("along", "s")}, "table", a, b)
                (s, t), (fs, ft) = th.centre, th.front  # front: across the table
                ax = (ft, -fs) if abs(ft) > 0 else (0.0, 1.0)
                ax = (abs(ax[0]), abs(ax[1]))
                n = (fs, ft)
                block_l = SIZE["table"][0] + 0.1
                block_d = 2 * (BENCH_OFF_M + SIZE["bench"][1] / 2)
                things.append(_Thing("table", "table", (s, t), n, block_l, block_d, True, 0.0,
                                     0.75, None, e))  # fmt: skip
                self.mob("table", rm, th, floor_y, room)
                for sgn in (1.0, -1.0):
                    bc = (s + n[0] * BENCH_OFF_M * sgn, t + n[1] * BENCH_OFF_M * sgn)
                    bf = (n[0] * sgn, n[1] * sgn)
                    bench = _Thing("mob", "bench", bc, bf, *SIZE["bench"], True, 0.0, 0.45, None, e)
                    self.mob("bench", rm, bench, floor_y, room)
                    spots.append(("mob", (bc[0] + bf[0] * (SIZE["bench"][1] / 2 + SLOTS["bench"]),
                                          bc[1] + bf[1] * (SIZE["bench"][1] / 2 + SLOTS["bench"])),
                                  (-bf[0], -bf[1])))  # fmt: skip
                lights += self.set_table(rm, (s, t), ax, n, floor_y, bool(e.get("candle", True)))
                continue
            if kind == "light":
                prop = {"lantern": "lantern", "torch": "wall_torch"}[model]
                th = self.place(e, prop, a, b)
                things.append(th)
                self.prop(prop, rm, th, floor_y)
                self.light_at(th, model, rm, floor_y)
                lights += 1
                continue
            if kind == "lever":
                th = self.place(e, "torch_lever", a, b)
                th.kind = "lever"
                things.append(th)
                comp = {"definition": "lever", "targets": list(e["targets"]),
                        "action": str(e.get("action", "toggle"))}  # fmt: skip
                if e.get("hidden"):
                    comp["hidden"] = True
                self.vob("mob", str(e["name"]), self.world_of(th, (0.0, 0.0, 0.0), floor_y),
                         th.front, room, mesh="props/torch_lever.glb",
                         components={"mob": comp})  # fmt: skip
                self.light_at(th, "lever", rm, floor_y)
                lights += 1
                reach = SLOTS["lever"]
                spots.append(("mob", (th.centre[0] + th.front[0] * reach,
                                      th.centre[1] + th.front[1] * reach),
                              (-th.front[0], -th.front[1])))  # fmt: skip
                continue
            th = self.place(e, model, a, b)
            th.kind = kind
            things.append(th)
            if kind == "mob":
                self.mob(model, rm, th, floor_y, room)
                reach = th.depth / 2 + SLOTS.get(model, 0.6)
                spots.append(("mob", (th.centre[0] + th.front[0] * reach,
                                      th.centre[1] + th.front[1] * reach),
                              (-th.front[0], -th.front[1])))  # fmt: skip
            elif kind == "prop":
                height = None
                if model in HANGING:
                    height = ceiling
                elif "y" in e:
                    height = floor_y + float(e["y"])
                self.prop(model, rm, th, floor_y, height)
                if e.get("light"):
                    self.light_at(th, str(e["light"]), rm, floor_y if height is None else height)
                    lights += 1
                if model == "hearth":
                    self.light_at(th, "hearth", rm, floor_y)
                    lights += 1
                if model == "giant_barrel" and e.get("front"):
                    self.cask_front(th, str(e["front"]), floor_y, room)
            else:
                raise CastleError(f"{room}: unknown {what!r}")
            if e.get("fp"):
                gap = COOK_M if e["fp"] == "COOK" else READ_M
                reach = th.depth / 2 + gap
                spots.append((f"fp:{e['fp']}", (th.centre[0] + th.front[0] * reach,
                                                th.centre[1] + th.front[1] * reach),
                              (-th.front[0], -th.front[1])))  # fmt: skip
        lights += self.window_light(key, room, rm, a, b)
        self.check(key, room, a, b, things, spots, lights, cap)
        fps: dict[str, int] = {}
        for what, (s, t), d in spots:
            if what.startswith("fp:"):
                typ = what[3:]
                fps[typ] = fps.get(typ, 0) + 1
                x, z = self.xz(s, t)
                dx, dz = self.vec(d)
                self.plan.places.append({"kind": "fp", "name": f"FP_{typ}_{rm}_{fps[typ]:02d}",
                                         "house": HOUSE, "room": room,
                                         "pos": [tidy(x), tidy(z)], "dir": [tidy(dx), tidy(dz)],
                                         "y": tidy(floor_y)})  # fmt: skip
        self._rooms.append((key, room, a, b, things, spots))

    def mob(self, model: str, rm: str, th: _Thing, floor_y: float, room: str) -> None:
        self.vob("mob", self.name("MOB", rm, model),
                 self.world_of(th, (0.0, 0.0, 0.0), floor_y), th.front, room,
                 mesh=f"mobs/{model}.glb", components={"mob": {"definition": model}})  # fmt: skip

    def prop(self, model: str, rm: str, th: _Thing, floor_y: float,
             height: float | None = None) -> None:  # fmt: skip
        name = self.name("PROP", rm, model)
        pos = self.world_of(th, (0.0, 0.0, 0.0), floor_y)
        if height is not None:
            pos[1] = height
        self.vob("mesh", name, pos, th.front, mesh=f"props/{model}.glb")

    def light_at(self, th: _Thing, kind: str, rm: str, floor_y: float) -> None:
        from gothar_worldgen.mobs import CANDLE_TOP, LANTERN_Y, TORCH_TILT_DEG, torch_flame

        if kind == "lantern":
            at, comp, word = (0.0, LANTERN_Y, 0.2), LIGHT["lantern"], "LATERNE"
        elif kind == "torch":
            at, comp, word = torch_flame(), LIGHT_TORCH, "FACKEL"
        elif kind == "lever":
            at, comp, word = torch_flame(TORCH_TILT_DEG), LIGHT_TORCH, "FACKEL"
        elif kind == "candle":
            at, comp, word = (0.0, CANDLE_TOP + 0.1, 0.0), LIGHT["candle"], "KERZE"
        elif kind == "hearth":
            at, comp, word = (0.0, 0.9, 0.1), LIGHT["hearth"], "HERD"
        else:
            raise CastleError(f"unknown light {kind!r}")
        name = self.name("LIGHT", rm, word)
        self.vob("light", name, self.world_of(th, at, floor_y), th.front,
                 components={"light": dict(comp)})  # fmt: skip

    def set_table(self, rm: str, c: tuple[float, float], ax: tuple[float, float],
                  n: tuple[float, float], floor_y: float, candle: bool) -> int:  # fmt: skip
        """Rug, tableware, mugs, bread and an apple, a candle; returns the lights set."""
        top = floor_y + 0.75
        th = _Thing("prop", "rug", c, n, 2.6, 2.1, False, 0.0, 0.0, None, {})
        self.prop("rug", rm, th, floor_y)
        self.prop("tableware", rm, th, floor_y, top)
        k = self.counts.get(f"TISCH{rm}", 0) + 1
        self.counts[f"TISCH{rm}"] = k
        for j, (item, along, across, up, rot) in enumerate(TABLE_ITEMS):
            s, t = c[0] + ax[0] * along + n[0] * across, c[1] + ax[1] * along + n[1] * across
            x, z = self.xz(s, t)
            fx, fz = self.vec(n)
            name = f"PROP_{rm}_TISCH_{k}_{j + 1}"
            self.plan.vobs.append({"key": f"castle:{name}", "name": name, "type": "mesh",
                                   "pos": [tidy(x), tidy(top + up), tidy(z)],
                                   "rot": rot or _quat(fx, fz), "mesh": item})  # fmt: skip
        if not candle:
            return 0
        stick = _Thing("prop", "candlestick", (c[0] + ax[0] * 0.05, c[1] + ax[1] * 0.05), n,
                       0.1, 0.1, False, 0.0, 0.0, None, {})  # fmt: skip
        self.prop("candlestick", rm, stick, floor_y, top)
        self.light_at(stick, "candle", rm, top)
        return 1

    def cask_front(self, cask: _Thing, name: str, floor_y: float, room: str) -> None:
        """The giant cask's front head: its hinge at the side, a ``mover`` (secret wall)."""
        from gothar_worldgen.mobs import GIANT_BARREL_L, GIANT_FRONT_R

        hinge = self.world_of(cask, (-GIANT_FRONT_R, 0.0, GIANT_BARREL_L / 2), floor_y)
        self.vob("mesh", name, hinge, cask.front, room, mesh="props/giant_barrel_front.glb",
                 components={"mover": dict(FRONT_MOVER)})  # fmt: skip

    def window_light(self, key: str, room: str, rm: str, a: float, b: float) -> int:
        """Daylight at the room's first window, the court's first."""
        wins = sorted((w for w in self.windows(key) if a < w[1] < b), key=lambda w: (-w[0], w[1]))
        if not wins:
            return 0
        side, u, sill, top = wins[0]
        t = side * (self.t_in - WINDOW_LIGHT_IN_M)
        x, z = self.xz(u, t)
        y = self.storeys[key][0] + (sill + top) / 2
        self.vob("light", f"LIGHT_{rm}_FENSTER_1", [x, y, z], (0.0, -float(side)),
                 components={"light": dict(LIGHT["window"])})  # fmt: skip
        return 1

    # --- check ------------------------------------------------------------------------------
    def check(self, key: str, room: str, a: float, b: float, things: list[_Thing],
              spots: list[Any], lights: int, cap: int) -> None:  # fmt: skip
        fail = self.plan.failed
        inner = box(a, -self.t_in, b, self.t_in)
        keep = unary_union(self.keep(key, a, b))
        floor = [th for th in things if th.floor]
        for th in things:
            sh = th.shape()
            label = f"{room}: {th.model} at {th.centre[0]:.2f}/{th.centre[1]:.2f}"
            if not inner.buffer(0.01).contains(sh):
                fail.append(f"{label} outside the room")
            if th.floor and sh.intersection(keep).area > 1e-4:
                fail.append(f"{label} on the lane or a way")
            if th.wall in ("garden", "court"):
                side = -1 if th.wall == "garden" else 1
                lo, hi = th.centre[0] - th.length / 2, th.centre[0] + th.length / 2
                for ws, u, sill, top in self.windows(key):
                    if ws == side and lo < u + 0.7 and hi > u - 0.7 and th.y1 > sill - 0.05 \
                            and th.y0 < top:  # fmt: skip
                        fail.append(f"{label} in front of the window at {u:.2f}")
        for i, p in enumerate(floor):
            for q in floor[i + 1 :]:
                if p.model == q.model == "cell_bars":  # a grille's corners
                    continue
                if p.shape().intersection(q.shape()).area > 1e-4:
                    fail.append(f"{room}: {p.model} and {q.model} at {p.centre[0]:.2f}/"
                                f"{q.centre[0]:.2f} overlap")  # fmt: skip
        mounted = [th for th in things if not th.floor and th.wall]
        for i, p in enumerate(mounted):
            for q in mounted[i + 1 :]:
                if p.wall == q.wall and p.shape().buffer(0.05).intersects(q.shape()) and \
                        p.y0 < q.y1 and q.y0 < p.y1:  # fmt: skip
                    fail.append(f"{room}: {p.model} and {q.model} on the wall overlap")
        for what, st, _ in spots:
            pt = Point(st)
            if not inner.buffer(-0.25).contains(pt):
                fail.append(f"{room}: {what} at {st[0]:.2f}/{st[1]:.2f} outside the room")
            if any(th.shape().distance(pt) < 0.15 for th in floor):
                fail.append(f"{room}: {what} at {st[0]:.2f}/{st[1]:.2f} on furniture")
        if lights > cap:
            fail.append(f"{room}: {lights} lights, at most {cap}")

    # --- zones ------------------------------------------------------------------------------
    def zones(self) -> None:
        from gothar_worldgen.uses.zones import room_boxes

        for key, room, a, b in self.rooms():
            y0, y1 = self.storeys[key]
            ring = [self.xz(a, -self.t_in), self.xz(b, -self.t_in), self.xz(b, self.t_in),
                    self.xz(a, self.t_in)]  # fmt: skip
            self.plan.zones += room_boxes(f"{self.tag}_{room.upper()}_INNEN", ring, y0, y1)
        c, ri, _ = self.tower_frame()
        ring = [(c[0] + ri * math.cos(a), c[1] + ri * math.sin(a))
                for a in (self.tower_phase() + k * math.pi / 4 for k in range(8))]  # fmt: skip
        lo = min(y0 for y0, _ in self.storeys.values())
        hi = max(y1 for _, y1 in self.storeys.values())
        self.plan.zones += room_boxes(f"{self.tag}_TURM_INNEN", ring, lo, hi)

    # --- the tower --------------------------------------------------------------------------
    def tower_frame(self) -> tuple[tuple[float, float], float, float]:
        """(centre x z, inner radius of the walls, the doorway's angle)."""
        ins, w = self.ins, self.w
        c = self.xz(ins.tower_s, ins.tower_t)
        r = float(self.tower["radius"])
        ri = r * math.cos(math.pi / 8) - self.geo.interior.TOWER_WALL_M
        side = 1.0 if ins.tower_t > 0 else -1.0
        door = math.atan2(-side * w.n[1], -side * w.n[0])
        return c, ri, door

    def tower_phase(self) -> float:
        return math.radians(float(self.tower["phaseDeg"]))

    def tower_lights(self) -> None:
        """A lantern at every landing on the wall beside the doorway (over the flight below)."""
        from gothar_worldgen.mobs import LANTERN_Y

        c, ri, door = self.tower_frame()
        ang = door + math.radians(TOWER_LANTERN_DEG)
        ux, uz = math.cos(ang), math.sin(ang)
        for k, (y0, _) in enumerate(self.storeys.values()):
            name = f"PROP_{self.tag}_TURM_LANTERN_{k + 1}"
            pos = [c[0] + ux * ri, y0, c[1] + uz * ri]
            self.plan.vobs.append({"key": f"castle:{name}", "name": name, "type": "mesh",
                                   "pos": [tidy(v) for v in pos], "rot": _quat(-ux, -uz),
                                   "mesh": "props/lantern.glb"})  # fmt: skip
            light = f"LIGHT_{self.tag}_TURM_LATERNE_{k + 1}"
            at = [c[0] + ux * (ri - 0.2), y0 + LANTERN_Y, c[1] + uz * (ri - 0.2)]
            self.plan.vobs.append({"key": f"castle:{light}", "name": light, "type": "light",
                                   "pos": [tidy(v) for v in at], "rot": _quat(-ux, -uz),
                                   "components": {"light": dict(LIGHT["lantern"])}})  # fmt: skip

    # --- waypoints --------------------------------------------------------------------------
    def wp(
        self,
        name: str,
        xz: tuple[float, float],
        y: float,
        link: str | None,
        room: str,
        d: tuple[float, float] = (1.0, 0.0),
        routine: bool = False,
    ) -> str:
        p: dict[str, Any] = {"kind": "wp", "name": name, "house": HOUSE, "room": room,
                             "pos": [tidy(xz[0]), tidy(xz[1])], "dir": [tidy(d[0]), tidy(d[1])],
                             "y": tidy(y)}  # fmt: skip
        if link is not None:
            p["link"] = link
        if routine:
            p["routine"] = True
        self.plan.places.append(p)
        return name

    def waypoints(self) -> None:
        ins, tag = self.ins, self.tag
        portal = self.ins.portal_s
        u = self.vec((1.0, 0.0))
        # the court in front of the portal: the root, tied to the town's net by the generator
        out = self.wp(f"WP_{tag}_PORTAL_AUSSEN", self.xz(portal, self.w.half + 1.5),
                      ins.portal_y, None, "PORTAL", self.vec((0.0, -1.0)))  # fmt: skip
        inn = self.wp(f"WP_{tag}_PORTAL", self.xz(portal, self.t_in - 0.6), self.storeys["eg"][0],
                      out, "PORTAL", self.vec((0.0, -1.0)))  # fmt: skip
        lanes = {"eg": self.lane_chain("eg", (portal, inn), u)}
        c, ri, door = self.tower_frame()
        landings = {}
        for key, (y0, _) in self.storeys.items():
            landings[key] = (
                c[0] + SPIRAL_R_M * math.cos(door),
                c[1] + SPIRAL_R_M * math.sin(door),
                y0,
            )
        ts = ins.tower_s
        side = -1.0 if ins.tower_t < 0 else 1.0
        # EG: lane -> tower doorway -> landing
        tower_eg = self.to_tower("eg", lanes["eg"]["junction"], landings["eg"], ts, side)
        # the spiral up to OG and down to the cellar
        keys = list(self.storeys)
        above = self.spiral(keys[1], keys[2], tower_eg, c, door, up=True)
        below = self.spiral(keys[1], keys[0], tower_eg, c, door, up=False)
        for key, start in ((keys[2], above), (keys[0], below)):
            door_wp = self.wp(
                f"WP_{tag}_TURM_TUER_{key.upper()}",
                self.xz(ts, side * (self.t_in + DOOR_IN_M)),
                self.storeys[key][0],
                start,
                "TURM",
            )
            inside = self.wp(
                f"WP_{tag}_TURM_ZUGANG_{key.upper()}",
                self.xz(ts, side * (self.t_in - 1.0)),
                self.storeys[key][0],
                door_wp,
                "TURM",
            )
            self.lane_chain(key, (ts, inside), u)
        self.near_places()

    def to_tower(self, key: str, junction: str, landing: tuple[float, float, float], ts: float,
                 side: float) -> str:  # fmt: skip
        tag, y = self.tag, self.storeys[key][0]
        inside = self.wp(
            f"WP_{tag}_TURM_ZUGANG_{key.upper()}",
            self.xz(ts, side * (self.t_in - 1.0)),
            y,
            junction,
            "TURM",
        )
        door_wp = self.wp(
            f"WP_{tag}_TURM_TUER_{key.upper()}",
            self.xz(ts, side * (self.t_in + DOOR_IN_M)),
            y,
            inside,
            "TURM",
        )
        return self.wp(f"WP_{tag}_TURM_{key.upper()}", landing[:2], y, door_wp, "TURM")

    def spiral(self, start_key: str, end_key: str, start: str, c: tuple[float, float],
               door: float, up: bool) -> str:  # fmt: skip
        """Waypoints along a flight from the landing ``start`` to the other storey's landing."""
        y_a, y_b = self.storeys[start_key][0], self.storeys[end_key][0]
        land = math.radians(self.geo.interior.LANDING_DEG)
        sweep = 2 * math.pi - land
        n = round(math.degrees(sweep) / SPIRAL_STEP_DEG)
        prev = start
        tag = self.tag
        lo_key = start_key if up else end_key  # the flight belongs to the lower storey
        y_lo, y_hi = (y_a, y_b) if up else (y_b, y_a)
        for j in range(n + 1):
            f = j / n if up else 1.0 - j / n
            ang = door + land / 2 + sweep * f
            y = y_lo + (y_hi - y_lo) * f
            name = f"WP_{tag}_TURM_{lo_key.upper()}_{round(f * n):02d}"
            prev = self.wp(
                name,
                (c[0] + SPIRAL_R_M * math.cos(ang), c[1] + SPIRAL_R_M * math.sin(ang)),
                y,
                prev,
                "TURM",
            )
        end = (c[0] + SPIRAL_R_M * math.cos(door), c[1] + SPIRAL_R_M * math.sin(door))
        return self.wp(f"WP_{tag}_TURM_{end_key.upper()}", end, y_b, prev, "TURM")

    def lane_chain(
        self, key: str, root: tuple[float, str], u: tuple[float, float]
    ) -> dict[str, str]:
        """The storey's lane: room middles, doorways, the junctions to the portal and the tower,
        tied in a row both ways from ``root`` (s of the point tied to it, its name)."""
        tag, y = self.tag, self.storeys[key][0]
        pts: list[tuple[float, str, str, bool]] = []  # s, name, room, routine
        rooms = self.ins.rooms(key)
        for name, a, b in rooms:
            pts.append(((a + b) / 2, f"WP_{tag}_{name.upper()}", name.upper(), True))
        for (na, _, b), (nb, c, _) in zip(rooms, rooms[1:], strict=False):
            pts.append(((b + c) / 2, f"WP_{tag}_{na.upper()}_{nb.upper()}", na.upper(), False))
        ts = self.ins.tower_s
        jt = next(n for n, a, b in rooms if a < ts < b).upper()
        pts.append((ts, f"WP_{tag}_{jt}_TURM", jt, False))
        if key == "eg":
            ps = self.ins.portal_s
            jp = next(n for n, a, b in rooms if a < ps < b).upper()
            pts.append((ps, f"WP_{tag}_{jp}_PORTAL", jp, False))
        pts.sort()
        root_s, root_name = root
        start = min(range(len(pts)), key=lambda i: abs(pts[i][0] - root_s))
        names = {}
        prev = root_name
        for i in range(start, len(pts)):
            s, n, rm, routine = pts[i]
            prev = names[n] = self.wp(n, self.xz(s, self.door_t), y, prev, rm, u, routine)
        prev = names[pts[start][1]]
        for i in range(start - 1, -1, -1):
            s, n, rm, routine = pts[i]
            prev = names[n] = self.wp(n, self.xz(s, self.door_t), y, prev, rm, u, routine)
        self.wps[key] = [(n, (s, self.door_t)) for s, n, _, _ in pts]
        return {"junction": names[f"WP_{tag}_{jt}_TURM"]}

    def near_places(self) -> None:
        """Freepoints and mobs farther than ``FP_NEAR_M`` from a waypoint in a free line get a
        waypoint of their own nearby, tied to one in sight."""
        for key, room, a, b, things, spots in self._rooms:
            obstacles = [th.shape() for th in things if th.floor]
            y = self.storeys[key][0]
            own = [(n, st) for n, st in self.wps[key] if a - 0.3 <= st[0] <= b + 0.3]
            k = 0
            for what, st, _ in spots:
                if self._near(st, own, obstacles):
                    continue
                best = None
                for cand in self._candidates(a, b, obstacles):
                    dc = math.dist(cand, st)
                    if dc > FP_NEAR_M - 0.5 or not self._free(cand, st, obstacles):
                        continue
                    link = next((n for n, q in sorted(own, key=lambda q: math.dist(q[1], cand))
                                 if self._free(cand, q, obstacles)), None)  # fmt: skip
                    if link is not None and (best is None or dc < best[0]):
                        best = (dc, cand, link)
                if best is None:
                    self.plan.failed.append(f"{room}: no way to {what} at {st[0]:.2f}/{st[1]:.2f}")
                    continue
                k += 1
                name = f"WP_{self.tag}_{room}_{k}"
                self.wp(name, self.xz(*best[1]), y, best[2], room)
                own.append((name, best[1]))

    def _near(self, st: tuple[float, float], own: list[tuple[str, tuple[float, float]]],
              obstacles: list[Polygon]) -> bool:  # fmt: skip
        return any(math.dist(st, q) <= FP_NEAR_M and self._free(st, q, obstacles) for _, q in own)

    @staticmethod
    def _free(p: tuple[float, float], q: tuple[float, float], obstacles: list[Polygon]) -> bool:
        line = LineString([p, q]).buffer(WAY_R_M)
        ends = Point(p).buffer(0.45).union(Point(q).buffer(0.45))
        return not any(o.intersects(line) and o.distance(Point(p)) > 0.3 and
                       o.difference(ends).intersects(line) for o in obstacles)  # fmt: skip

    def _candidates(
        self, a: float, b: float, obstacles: list[Polygon]
    ) -> list[tuple[float, float]]:
        out = []
        s = a + 0.6
        while s <= b - 0.6:
            t = -self.t_in + 0.6
            while t <= self.t_in - 0.6:
                p = Point(s, t)
                if not any(o.distance(p) < 0.45 for o in obstacles):
                    out.append((round(s, 2), round(t, 2)))
                t += 0.5
            s += 0.5
        return out


def plan_castle(built: dict[str, Any], data: dict[str, Any], geo: Any) -> CastlePlan:  # noqa: ANN401
    """Everything inside the castle's walkable wing (``built``: schloss_built.json, ``data``:
    castle_inside.json, ``geo``: ``handmade.schloss_geometry()``)."""
    return _Castle(built, data, geo).build()


def assemble_vobs(plan: CastlePlan) -> list[dict[str, Any]]:
    """The vob specs without the planner's own fields (``room``)."""
    return [{k: v for k, v in s.items() if k != "room"} for s in plan.vobs]
