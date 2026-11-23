"""Dungeon levels as text maps plus JSON (W8 D2, docs/design/schloss-dungeon.md section 3.3).

``ebene_NN.txt``: one character per cell of ``cellM`` (3 m), column x to the east, row y to the
south (row 0 = north edge). ``ebene_NN.json``: what a character does not say, per cell ``at``
[x, y] (targets, locks, teleport targets, monsters, the stairs' worlds).

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
ACTIONS = {"toggle", "open", "close", "on", "off", "wake", "trigger", "lock", "unlock"}


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

    def centre(self, x: float, y: float) -> tuple[float, float, float]:
        """World position of the middle of cell (x, y) on the floor."""
        ox, oy, oz = self.origin
        return (ox + (x + 0.5) * self.cell_m, oy, oz + (y + 0.5) * self.cell_m)

    def names(self) -> dict[str, tuple[int, int]]:
        return {self.name(at): at for at in self.cells(set(ENTRY))}


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
                  float(doc.get("cellM", 3.0)), float(doc.get("heightM", 3.5)), origin)  # fmt: skip
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
    missing = [list(at) for at in level.cells(REQUIRED) if at not in level.entries]
    if missing:
        raise LevelError(f"cells without an entry: {missing}")
    if len(level.cells("<")) < 1:
        raise LevelError("every level needs stairs up ('<', plan F6)")
    _check_entries(level)
    return level


def _check_entries(level: Level) -> None:
    names = level.names()
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
            elif not (spec.get("world") and spec.get("start")):
                raise LevelError(f"{where}: needs 'to' or 'world' + 'start'")
        if kind == "stairs" and not (spec.get("world") and spec.get("start") and e.get("arrive")):
            raise LevelError(f"{where}: stairs need world, start and 'arrive'")
        if kind == "trapdoor" and not (spec.get("fall", {}).get("world")
                                       and spec.get("fall", {}).get("start")):  # fmt: skip
            raise LevelError(f"{where}: a trapdoor needs 'fall' with world and start")
        if kind == "monster" and not (spec.get("npc") and 1 <= int(spec.get("count", 1)) <= 32):
            raise LevelError(f"{where}: monster needs npc and count 1..32")


def load_level(folder: Path, number: int) -> Level:
    stem = folder / f"ebene_{number:02d}"
    text = stem.with_suffix(".txt").read_text(encoding="utf-8")
    doc = json.loads(stem.with_suffix(".json").read_text(encoding="utf-8"))
    return parse_level(text, doc)
