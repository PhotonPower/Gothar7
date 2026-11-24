from pathlib import Path

import pytest

from gothar_worldgen.dungeon.level import LevelError, load_level, parse_level
from gothar_worldgen.dungeon.logic import check_reach

DATA = Path(__file__).resolve().parents[1] / "data" / "leonberg" / "dungeon"
UP = {"at": [1, 1], "stairs": {"world": "w.g7world", "start": "S"}, "arrive": "A"}


def doc(*cells: dict) -> dict:
    return {"format": "gothar-dungeon-level", "version": 1, "level": 1, "cells": [UP, *cells]}


def test_level_1_has_every_mechanic_and_no_dead_end() -> None:
    level = load_level(DATA, 1)
    kinds = {name.split("_")[0] for name in level.names()}
    assert kinds >= {"GATE", "WALL", "TRAP", "LEVER", "PLATE", "TELE", "NICHE", "SPAWN", "DOOR",
                     "CHEST", "STAIRS"}  # fmt: skip
    assert level.name((11, 2)) == "GATE_E01_11_2"
    assert level.facing((6, 4)) == "S"
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == []
    assert level.centre(1, 1) == (4.5, 0.0, 4.5)


@pytest.mark.parametrize(
    ("text", "cells", "message"),
    [
        ("#####\n#<.x#\n#####", (), "unknown map characters"),
        ("#####\n#<..#\n####", (), "same length"),
        ("#####\n#<..G\n#####", (), "closed by walls"),
        ("#####\n#<.G#\n#####", (), "without an entry"),
        ("#####\n#<.O#\n#####", ({"at": [3, 1], "plate": {"targets": ["GATE_E01_9_9"]}},),
         "not on the map"),
        ("#####\n#<..#\n##S##\n#...#\n#####",
         ({"at": [2, 2], "switch": {"type": "lever", "targets": ["STAIRS_E01_1_1"]}},), "facing"),
        ("#####\n#...#\n#####", (), "stairs up"),
    ],
)  # fmt: skip
def test_broken_maps_are_reported(text: str, cells: tuple, message: str) -> None:
    d = doc(*cells)
    if "<" not in text:
        d["cells"] = []
    with pytest.raises(LevelError, match=message):
        parse_level(text, d)


def test_a_one_way_teleporter_into_a_closed_room_is_a_dead_end() -> None:
    text = "#######\n#<.T#.#\n#####G#\n#.....#\n#######"
    level = parse_level(text, doc({"at": [3, 1], "teleport": {"to": [5, 1]}},
                                  {"at": [5, 2], "gate": {"open": False}}))  # fmt: skip
    reach = check_reach(level)
    assert (5, 1) in reach.stuck
    assert reach.unreached == [(1, 3), (2, 3), (3, 3), (4, 3), (5, 2), (5, 3)]


def test_a_lever_opens_the_way() -> None:
    text = "#######\n#<..G.#\n###S###\n#######"
    lever = {"type": "lever", "targets": ["GATE_E01_4_1"]}
    level = parse_level(text, doc({"at": [4, 1], "gate": {"open": False}},
                                  {"at": [3, 2], "switch": lever}))  # fmt: skip
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == [] and reach.states == 2
