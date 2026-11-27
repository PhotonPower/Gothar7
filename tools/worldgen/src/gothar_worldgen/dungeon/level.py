"""Dungeon levels as text maps plus JSON (W8 D2, docs/design/schloss-dungeon.md section 3.3).

``ebene_NN.txt``: one character per cell of ``cellM`` (3 m), column x to the east, row y to the
south (row 0 = north edge). ``ebene_NN.json``: what a character does not say, per cell ``at``
[x, y] (targets, locks, teleport targets, monsters, the stairs' worlds).

Top level of the JSON besides ``cells``: ``starts`` (extra arrivals), ``relays`` (invisible relays
``RELAY_E01_<name>`` with ``mode`` any/all/count, ``targets``, ``action``, ``once``, ``invert``,
``delay``; switches and plates may name them) and ``items`` (item vobs ``ITEM_E01_<x>_<y>_<k>`` on a
floor cell: ``at``, ``instance``, ``count``; the finds of the hidden depots), ``spawns`` (monster
places ``SPAWN_E01_<x>_<y>`` like ``m``, but on any floor cell, also in the water ``~``: the
Brunnenmolch lurks there), ``water`` (``depthM`` of a basin's floor below the others,
``edgeM`` of the water's surface below the edge; plan W8 D3) and ``props`` (still pieces of the
kit on a plain floor cell: ``at``, ``kind``, ``facing``; vobs ``PROP_E04_<x>_<y>``). A
``sarcophagus`` or ``workbench`` stands in the middle of its cell and blocks it (no waypoint, no
way through); a ``bone_niche`` or ``cogwheel`` hangs on the wall behind it (opposite ``facing``)
and leaves the cell free.
A teleporter with ``trap: true`` is invisible and shows no ring (a trap, W8 E4); one with
``spin: true`` keeps the hero on its cell and only turns him to ``facing`` (a spinner, W8 E6).

The cave (W8 E7 "Kristallgrotte"): ``stalagmite`` fills its cell like a sarcophagus, a
``crystal_cluster`` stands at the wall behind its cell (with a bluish light), a ``stalactite``
hangs from the ceiling over the middle of its cell and leaves it free. ``heights`` lifts or lowers
the floor of cell rectangles (``from``, ``to``, ``dy`` in metres, later entries win); the corners
take the mean of the cells round them, so the floor slopes smoothly. Neighbours may differ by at
most ``MAX_STEP_M``; a cell with a mechanism (door, gate, trapdoor, teleporter, plate, secret wall,
stairs, chest, the stand of a switch or niche) must be level with all eight neighbours, and a cell
by the water stays at 0. ``water.shallow`` (cell rectangles) makes a shallow pool (``shallowM``
deep, its surface ``shallowEdgeM`` below the edge). ``jetty`` lists floor cells built of planks on
posts. A trapdoor whose ``fall`` names only a ``start`` (one of ``starts`` with ``shaft``) drops
into this level: the hero arrives in the lower part of the cave and walks back.

Characters (contract of the plan): ``#`` wall, ``.`` floor, ``D`` door, ``G`` gate, ``P`` trapdoor,
``T`` teleporter, ``S`` wall switch (lever or button), ``O`` pressure plate, ``N`` niche, ``H``
secret wall, ``<`` stairs up, ``>`` stairs down, ``~`` water, ``*`` treasure, ``m`` monster place.

``S`` and ``N`` sit in a wall and face one floor neighbour (``facing`` when there are several);
all others are floor cells. Vob names follow the plan: ``GATE_E01_<x>_<y>`` and so on.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

FORMAT = "gothar-dungeon-level"
WALL, FLOOR = "#", "."
IN_WALL = {"S", "N"}  # set into a wall, used from the floor cell they face
FLOOR_CHARS = {".", "D", "G", "P", "T", "O", "H", "<", ">", "~", "*", "m"}
CHARS = FLOOR_CHARS | IN_WALL | {WALL}
# Character -> the JSON key of its entry (None: no entry needed).
ENTRY = {"D": "door", "G": "gate", "P": "trapdoor", "T": "teleport", "S": "switch", "O": "plate",
         "N": "niche", "H": "secret_wall", "<": "stairs", ">": "stairs", "*": "chest",
         "m": "monster"}  # fmt: skip
REQUIRED = set(ENTRY) - {"*"}  # a treasure without an entry is a plain chest
# Vob name prefix per kind (plan section 5).
PREFIX = {"gate": "GATE", "secret_wall": "WALL", "trapdoor": "TRAP", "switch": "LEVER",
          "button": "BUTTON", "plate": "PLATE", "teleport": "TELE", "niche": "NICHE",
          "monster": "SPAWN", "door": "DOOR", "chest": "CHEST", "stairs": "STAIRS"}  # fmt: skip
DIRS = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
SWITCH_TYPES = {"lever", "wall_button"}
RELAY_MODES = {"any", "all", "count"}
ACTIONS = {"toggle", "open", "close", "on", "off", "wake", "trigger", "lock", "unlock"}
# kind -> where it stands: "fill" (the middle, blocks its cell), "wall" (on the wall behind the
# cell, which stays free), "ceiling" (hangs over the middle, the cell stays free) or "water"
# (on a pool's bottom in the middle of a water cell, no collision)
PROPS = {"sarcophagus": "fill", "bone_niche": "wall", "workbench": "fill", "cogwheel": "wall",
         "stalagmite": "fill", "stalactite": "ceiling", "crystal_cluster": "wall",
         "crystal_ceiling": "ceiling", "crystal_spire": "water"}  # fmt: skip
MAX_STEP_M = 0.6  # floor heights of two neighbours (``heights``)
DY_RANGE = (-1.5, 0.6)
LEVEL_CHARS = set("DGPTOH<>*")  # cells with a mechanism: level with their neighbours
AT_ZERO = set("DGPH<>")  # built at the level's floor height 0


class LevelError(ValueError):
    pass


@dataclass
class Level:
    number: int
    theme: str
    rows: list[str]
    cell_m: float
    height_m: float
    origin: tuple[float, float, float]
    entries: dict[tuple[int, int], dict[str, Any]] = field(default_factory=dict)
    starts: list[dict[str, Any]] = field(default_factory=list)  # extra arrivals (``starts``)
    relays: dict[str, dict[str, Any]] = field(default_factory=dict)  # vob name -> spec
    items: list[dict[str, Any]] = field(default_factory=list)  # item vobs (``items``)
    spawns: dict[tuple[int, int], dict[str, Any]] = field(default_factory=dict)  # ``spawns``
    props: dict[tuple[int, int], dict[str, Any]] = field(default_factory=dict)  # ``props``

    @property
    def blocked(self) -> set[tuple[int, int]]:
        """Floor cells a prop fills (a sarcophagus): no waypoint, no way through."""
        return {at for at, p in self.props.items() if PROPS[p["kind"]] == "fill"}

    @property
    def water_depth(self) -> float:
        return float(self.raw.get("water", {}).get("depthM", 1.0))

    heights: dict[tuple[int, int], float] = field(default_factory=dict)  # ``heights`` (dy != 0)
    shallow: set[tuple[int, int]] = field(default_factory=set)  # ``water.shallow`` cells
    jetty: set[tuple[int, int]] = field(default_factory=set)

    def dy(self, at: tuple[int, int]) -> float:
        """The floor's lift of a cell (``heights``; 0 without)."""
        return self.heights.get(at, 0.0)

    def depth(self, at: tuple[int, int]) -> float:
        """How far a water cell's bottom lies below the edge."""
        if at in self.shallow:
            return float(self.raw["water"].get("shallowM", 0.35))
        return self.water_depth

    def edge(self, at: tuple[int, int]) -> float:
        """How far a water cell's surface lies below the edge."""
        if at in self.shallow:
            return float(self.raw["water"].get("shallowEdgeM", 0.12))
        return self.water_edge

    def same_level_fall(self, at: tuple[int, int]) -> dict[str, Any] | None:
        """The arrival (``starts`` entry) of a trapdoor that drops into this level, else None."""
        fall = self.entries.get(at, {}).get("trapdoor", {}).get("fall", {})
        if fall.get("world"):
            return None
        return next((st for st in self.starts if st["name"] == fall.get("start")), None)

    @property
    def water_edge(self) -> float:
        """How far the water's surface lies below the floor around the basin."""
        return float(self.raw.get("water", {}).get("edgeM", 0.2))

    raw: dict[str, Any] = field(default_factory=dict)  # the JSON (environment, torches …)

    @property
    def tag(self) -> str:
        return f"E{self.number:02d}"

    @property
    def width(self) -> int:
        return len(self.rows[0])

    @property
    def height(self) -> int:
        return len(self.rows)

    def char(self, x: int, y: int) -> str:
        if 0 <= y < self.height and 0 <= x < self.width:
            return self.rows[y][x]
        return WALL

    def is_floor(self, x: int, y: int) -> bool:
        return self.char(x, y) in FLOOR_CHARS

    def cells(self, chars: set[str] | str) -> list[tuple[int, int]]:
        return [(x, y) for y, row in enumerate(self.rows) for x, c in enumerate(row) if c in chars]

    def kind(self, at: tuple[int, int]) -> str | None:
        """The entry kind of a cell (``ENTRY``); a switch of type ``wall_button`` is ``button``."""
        c = self.char(*at)
        k = ENTRY.get(c)
        if (
            k == "switch"
            and self.entries.get(at, {}).get("switch", {}).get("type") == "wall_button"
        ):
            return "button"
        return k

    def name(self, at: tuple[int, int]) -> str:
        kind = self.kind(at)
        if kind is None:
            raise LevelError(f"cell {list(at)} ({self.char(*at)!r}) has no vob")
        return f"{PREFIX[kind]}_{self.tag}_{at[0]}_{at[1]}"

    def facing(self, at: tuple[int, int]) -> str:
        """The side a wall-set cell (``S``, ``N``) is used from."""
        key = ENTRY[self.char(*at)]
        spec = self.entries.get(at, {}).get(key, {})
        if spec.get("facing"):
            return str(spec["facing"])
        sides = [d for d, (dx, dy) in DIRS.items() if self.is_floor(at[0] + dx, at[1] + dy)]
        if len(sides) != 1:
            raise LevelError(f"cell {list(at)}: set 'facing' (floor on {sides or 'no side'})")
        return sides[0]

    def well(self, at: tuple[int, int]) -> tuple[int, int]:
        """The wall cell behind stairs: the flight runs into it, away from ``facing``."""
        dx, dy = DIRS[self.facing(at)]
        return (at[0] - dx, at[1] - dy)

    def passage(self, at: tuple[int, int]) -> str:
        """``x`` or ``z``: the way through a door or gate cell (floor on two opposite sides,
        walls on the other two)."""
        we = self.is_floor(at[0] - 1, at[1]) and self.is_floor(at[0] + 1, at[1])
        ns = self.is_floor(at[0], at[1] - 1) and self.is_floor(at[0], at[1] + 1)
        wall_we = not self.is_floor(at[0] - 1, at[1]) and not self.is_floor(at[0] + 1, at[1])
        wall_ns = not self.is_floor(at[0], at[1] - 1) and not self.is_floor(at[0], at[1] + 1)
        if we and wall_ns:
            return "x"
        if ns and wall_we:
            return "z"
        raise LevelError(f"{self.name(at)}: needs floor on two opposite sides, walls on the others")

    def centre(self, x: float, y: float) -> tuple[float, float, float]:
        """World position of the middle of cell (x, y) on the floor."""
        ox, oy, oz = self.origin
        return (ox + (x + 0.5) * self.cell_m, oy, oz + (y + 0.5) * self.cell_m)

    def names(self) -> dict[str, tuple[int, int]]:
        return {self.name(at): at for at in self.cells(set(ENTRY))}

    def targets(self) -> set[str]:
        """Every name a switch, plate or relay may send to: the map's vobs and the relays."""
        return set(self.names()) | set(self.relays) | {self.spawn_name(at) for at in self.spawns}

    def spawn_name(self, at: tuple[int, int]) -> str:
        return f"{PREFIX['monster']}_{self.tag}_{at[0]}_{at[1]}"

    def prop_name(self, at: tuple[int, int]) -> str:
        return f"PROP_{self.tag}_{at[0]}_{at[1]}"


def _cell(raw: Any, what: str) -> tuple[int, int]:  # noqa: ANN401
    if not (isinstance(raw, list) and len(raw) == 2 and all(isinstance(v, int) for v in raw)):
        raise LevelError(f"{what}: 'at' must be [x, y]")
    return raw[0], raw[1]


def parse_level(text: str, doc: dict[str, Any]) -> Level:
    """Map text and JSON -> ``Level``; raises ``LevelError`` on a broken map."""
    if doc.get("format") != FORMAT or doc.get("version") != 1:
        raise LevelError(f"format must be {FORMAT} v1")
    rows = [r.rstrip("\r") for r in text.splitlines() if r.strip()]
    if not rows or len({len(r) for r in rows}) != 1:
        raise LevelError("all map rows must have the same length")
    bad = sorted({c for r in rows for c in r} - CHARS)
    if bad:
        raise LevelError(f"unknown map characters {bad}")
    if any(c != WALL for c in rows[0] + rows[-1]) or any(
        r[0] != WALL or r[-1] != WALL for r in rows
    ):
        raise LevelError("the map must be closed by walls")
    origin = tuple(float(v) for v in doc.get("origin", (0.0, 0.0, 0.0)))
    level = Level(int(doc["level"]), str(doc.get("theme", "")), rows,
                  float(doc.get("cellM", 3.0)), float(doc.get("heightM", 3.5)), origin,
                  raw=doc)  # fmt: skip
    for i, st in enumerate(doc.get("starts", [])):
        at = _cell(st.get("at"), f"starts[{i}]")
        if not st.get("name") or not level.is_floor(*at):
            raise LevelError(f"starts[{i}]: needs 'name' and a floor cell")
        if st.get("facing", "S") not in DIRS:
            raise LevelError(f"starts[{i}]: facing must be N, E, S or W")
        level.starts.append({**st, "at": at})
    for i, r in enumerate(doc.get("relays", [])):
        if not r.get("name") or not str(r["name"]).replace("_", "").isalnum():
            raise LevelError(f"relays[{i}]: needs a 'name' (letters, digits, _)")
        name = f"RELAY_{level.tag}_{str(r['name']).upper()}"
        if name in level.relays:
            raise LevelError(f"relays[{i}]: {name} twice")
        level.relays[name] = r
    for i, it in enumerate(doc.get("items", [])):
        at = _cell(it.get("at"), f"items[{i}]")
        if not it.get("instance") or not level.is_floor(*at) or level.char(*at) in "GHPT<>":
            raise LevelError(f"items[{i}]: needs 'instance' and a plain floor cell")
        if not 1 <= int(it.get("count", 1)) <= 1_000_000:
            raise LevelError(f"items[{i}]: count 1..1000000")
        level.items.append({**it, "at": at})
    for i, sp in enumerate(doc.get("spawns", [])):
        at = _cell(sp.get("at"), f"spawns[{i}]")
        if not level.is_floor(*at) or level.char(*at) not in ".~" or at in level.spawns:
            raise LevelError(f"spawns[{i}]: needs a free floor or water cell, once")
        if not (sp.get("npc") and 1 <= int(sp.get("count", 1)) <= 32):
            raise LevelError(f"spawns[{i}]: needs npc and count 1..32")
        level.spawns[at] = sp
    w = doc.get("water", {})
    if w and not 0.0 < float(w.get("edgeM", 0.2)) < float(w.get("depthM", 1.0)):
        raise LevelError("water: needs 0 < edgeM < depthM")
    if w.get("shallow"):
        if not 0.0 < float(w.get("shallowEdgeM", 0.12)) < float(w.get("shallowM", 0.35)):
            raise LevelError("water: needs 0 < shallowEdgeM < shallowM")
        for i, r in enumerate(w["shallow"]):
            for at in _rect(r, f"water.shallow[{i}]"):
                if level.char(*at) == "~":
                    level.shallow.add(at)
    for i, r in enumerate(doc.get("heights", [])):
        dy = float(r.get("dy", 0.0))
        if not DY_RANGE[0] <= dy <= DY_RANGE[1]:
            raise LevelError(f"heights[{i}]: dy {DY_RANGE[0]}..{DY_RANGE[1]}")
        for at in _rect(r, f"heights[{i}]"):
            if level.is_floor(*at) and level.char(*at) != "~":
                level.heights[at] = dy
    level.heights = {at: dy for at, dy in level.heights.items() if dy != 0.0}
    for i, raw in enumerate(doc.get("jetty", [])):
        at = _cell(raw, f"jetty[{i}]")
        if level.char(*at) != FLOOR:
            raise LevelError(f"jetty[{i}]: needs a plain floor cell '.'")
        level.jetty.add(at)
    for i, e in enumerate(doc.get("cells", [])):
        at = _cell(e.get("at"), f"cells[{i}]")
        c = level.char(*at)
        key = ENTRY.get(c)
        if key is None or key not in e:
            raise LevelError(f"cells[{i}] {list(at)}: {c!r} needs '{key}'" if key else
                             f"cells[{i}] {list(at)}: {c!r} takes no entry")  # fmt: skip
        if at in level.entries:
            raise LevelError(f"cells[{i}] {list(at)}: twice")
        level.entries[at] = e
    for i, pr in enumerate(doc.get("props", [])):
        at = _cell(pr.get("at"), f"props[{i}]")
        if pr.get("kind") not in PROPS or pr.get("facing", "S") not in DIRS:
            raise LevelError(f"props[{i}]: kind {sorted(PROPS)}, facing N, E, S or W")
        want = "~" if PROPS[pr["kind"]] == "water" else FLOOR
        if level.char(*at) != want or at in level.props:
            raise LevelError(f"props[{i}]: needs a plain floor cell {want!r}, once")
        level.props[at] = {**pr, "at": at}
    missing = [list(at) for at in level.cells(REQUIRED) if at not in level.entries]
    if missing:
        raise LevelError(f"cells without an entry: {missing}")
    if len(level.cells("<")) < 1:
        raise LevelError("every level needs stairs up ('<', plan F6)")
    _check_entries(level)
    _check_props(level)
    _check_heights(level)
    check_wells(level)
    return level


def _rect(raw: Any, what: str) -> list[tuple[int, int]]:  # noqa: ANN401
    """The cells of a rectangle ``from`` .. ``to`` (both included)."""
    (ax, ay), (bx, by) = _cell(raw.get("from"), what), _cell(raw.get("to"), what)
    return [(x, y) for y in range(min(ay, by), max(ay, by) + 1)
            for x in range(min(ax, bx), max(ax, bx) + 1)]  # fmt: skip


def _check_heights(level: Level) -> None:
    """Steps of at most ``MAX_STEP_M``; mechanisms level with their neighbours; 0 by the water
    and where the generator builds at the floor height 0 (doors, gates, shafts, stairs)."""
    if not level.heights:
        return
    land = [a for a in level.cells(FLOOR_CHARS) if level.char(*a) != "~"]
    level_cells = {a for a in land if level.char(*a) in LEVEL_CHARS}
    for at in level.cells(IN_WALL):
        dx, dy = DIRS[level.facing(at)]
        level_cells.add((at[0] + dx, at[1] + dy))
    for at in land:
        near = [(at[0] + i, at[1] + j) for i in (-1, 0, 1) for j in (-1, 0, 1) if i or j]
        for nb in near:
            if level.char(*nb) == "~" and level.dy(at) != 0.0:
                raise LevelError(f"heights: {list(at)} lies by the water and must stay at 0")
            if not level.is_floor(*nb) or level.char(*nb) == "~":
                continue
            step = abs(level.dy(at) - level.dy(nb))
            if step > MAX_STEP_M + 1e-9 and abs(nb[0] - at[0]) + abs(nb[1] - at[1]) == 1:
                raise LevelError(f"heights: {list(at)} -> {list(nb)} steps {step:.2f} m")
            if at in level_cells and step > 0.0:
                raise LevelError(f"heights: {list(at)} has a mechanism, level its neighbours")
        if level.char(*at) in AT_ZERO and level.dy(at) != 0.0:
            raise LevelError(f"heights: {list(at)} ({level.char(*at)}) must stay at 0")


def _check_props(level: Level) -> None:
    """A wall prop needs a wall behind it; a blocking prop must not fill a cell that something
    else needs (an arrival, a teleporter's target, an item, a monster, a switch's stand)."""
    need: set[tuple[int, int]] = {st["at"] for st in level.starts} | set(level.spawns)
    need |= {it["at"] for it in level.items}
    for at in level.cells(set(ENTRY)):
        spec = level.entries.get(at, {})
        if "teleport" in spec and "to" in spec["teleport"]:
            need.add(_cell(spec["teleport"]["to"], "teleport"))
        if level.char(*at) in IN_WALL:
            dx, dy = DIRS[level.facing(at)]
            need.add((at[0] + dx, at[1] + dy))
    for at, pr in level.props.items():
        dx, dy = DIRS[pr.get("facing", "S")]
        if PROPS[pr["kind"]] == "wall" and level.is_floor(at[0] - dx, at[1] - dy):
            raise LevelError(f"{level.prop_name(at)}: a wall prop needs a wall behind it")
        if PROPS[pr["kind"]] == "fill" and at in need:
            raise LevelError(f"{level.prop_name(at)}: the cell is needed (start, target, item …)")


def _check_entries(level: Level) -> None:
    names = level.targets()
    for name, r in level.relays.items():
        if r.get("mode", "any") not in RELAY_MODES:
            raise LevelError(f"{name}: mode must be any, all or count")
        if r.get("mode") == "count" and int(r.get("count", 0)) < 1:
            raise LevelError(f"{name}: mode count needs count >= 1")
        if r.get("action", "trigger") not in ACTIONS:
            raise LevelError(f"{name}: unknown action {r.get('action')!r}")
        if not r.get("targets") or any(t not in names or t == name for t in r["targets"]):
            raise LevelError(f"{name}: targets must name vobs on the map or other relays")
    for at, e in level.entries.items():
        kind = level.kind(at)
        spec = e[ENTRY[level.char(*at)]]
        where = f"{level.name(at)}"
        if kind in ("switch", "button", "plate"):
            if kind != "plate" and spec.get("type") not in SWITCH_TYPES:
                raise LevelError(f"{where}: switch type must be lever or wall_button")
            if spec.get("action", "trigger") not in ACTIONS:
                raise LevelError(f"{where}: unknown action {spec.get('action')!r}")
            for t in spec.get("targets", []):
                if t not in names:
                    raise LevelError(f"{where}: target {t!r} is not on the map")
            if not spec.get("targets"):
                raise LevelError(f"{where}: no targets")
        if kind in ("switch", "button", "niche"):
            dx, dy = DIRS[level.facing(at)]
            if not level.is_floor(at[0] + dx, at[1] + dy):
                raise LevelError(f"{where}: faces no floor")
        if kind == "teleport":
            if "to" in spec:
                to = _cell(spec["to"], where)
                if not level.is_floor(*to) or level.char(*to) == "T":
                    raise LevelError(f"{where}: 'to' must be a floor cell off the pads")
            elif spec.get("spin"):
                if spec.get("facing") not in DIRS or spec.get("world"):
                    raise LevelError(f"{where}: a spinner needs 'facing' and no other target")
            elif not (spec.get("world") and spec.get("start")):
                raise LevelError(f"{where}: needs 'to', 'spin' or 'world' + 'start'")
        if kind == "stairs":
            if not (spec.get("world") and spec.get("start") and e.get("arrive")):
                raise LevelError(f"{where}: stairs need world, start and 'arrive'")
            dx, dy = DIRS[level.facing(at)]
            if not level.is_floor(at[0] + dx, at[1] + dy):
                raise LevelError(f"{where}: faces no floor")
            if level.char(*level.well(at)) != WALL:
                raise LevelError(f"{where}: the flight needs a wall cell behind (away from facing)")
        if kind in ("door", "gate"):
            level.passage(at)
        if kind == "trapdoor":
            fall = spec.get("fall", {})
            if not fall.get("start"):
                raise LevelError(f"{where}: a trapdoor needs 'fall' with world and start")
            if not fall.get("world"):  # into this level: an arrival shaft of ``starts``
                arrival = level.same_level_fall(at)
                if arrival is None or not arrival.get("shaft"):
                    raise LevelError(f"{where}: 'fall' without world needs a shaft start here")
        if kind == "monster" and not (spec.get("npc") and 1 <= int(spec.get("count", 1)) <= 32):
            raise LevelError(f"{where}: monster needs npc and count 1..32")


def check_wells(level: Level) -> None:
    wells = [level.well(at) for at in level.cells("<>")]
    if len(set(wells)) != len(wells):
        raise LevelError("two flights of stairs run into the same wall cell")


def load_level(folder: Path, number: int) -> Level:
    stem = folder / f"ebene_{number:02d}"
    text = stem.with_suffix(".txt").read_text(encoding="utf-8")
    doc = json.loads(stem.with_suffix(".json").read_text(encoding="utf-8"))
    return parse_level(text, doc)
