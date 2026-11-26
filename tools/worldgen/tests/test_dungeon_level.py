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


def test_level_2_verlies_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 2 with every mechanic, two riddles, depots, monsters and the ways on."""
    level = load_level(DATA, 2)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "TRAP", "LEVER", "BUTTON", "PLATE", "TELE", "NICHE", "SPAWN",
                     "DOOR", "CHEST", "STAIRS", "RELAY"}  # fmt: skip
    assert 24 <= level.width <= 32 and 24 <= level.height <= 32  # plan 3.2
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == []
    assert len(reach.cells) >= 0.4 * level.width * level.height  # 40-60 % walkable
    npcs = {e["monster"]["npc"] for e in level.entries.values() if "monster" in e}
    assert npcs == {"mon_mauerkriecher", "mon_gatterbock"}  # the owner's choice for E2
    buttons = [e["switch"] for e in level.entries.values() if "switch" in e
               and e["switch"]["type"] == "wall_button"]  # fmt: skip
    assert len(buttons) >= 2 and all(b["hidden"] for b in buttons)  # two hidden depots
    assert {it["at"] for it in level.items} >= {(22, 10), (13, 17)}  # their finds
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    fall = [level.entries[at]["trapdoor"]["fall"]["world"] for at in level.cells("P")]
    assert down == fall == ["worlds/leonberg/dungeon/ebene_03.g7world"]


def test_without_both_levers_the_relay_keeps_the_stairs_shut() -> None:
    """Riddle 2 of Ebene 2: the relay ``all`` opens the gate only with both levers thrown."""
    level = load_level(DATA, 2)
    level.relays["RELAY_E02_TREPPE"]["targets"] = []  # the relay never reaches the gate
    reach = check_reach(level)
    assert (16, 20) in reach.unreached and (16, 21) in reach.unreached


def test_relay_all_needs_every_input() -> None:
    text = "#########\n#<..G...#\n###S#S###\n#########"
    lever = {"type": "lever", "facing": "N", "targets": ["RELAY_E01_BOTH"], "action": "open"}
    relay = {"name": "both", "mode": "all", "action": "open", "targets": ["GATE_E01_4_1"]}
    # the second lever stands behind the gate: one input alone never opens it
    level = parse_level(text, {**doc({"at": [4, 1], "gate": {"open": False}},
                                     {"at": [3, 2], "switch": lever},
                                     {"at": [5, 2], "switch": lever}),
                               "relays": [relay]})  # fmt: skip
    assert (5, 1) in check_reach(level).unreached
    level.relays["RELAY_E01_BOTH"]["mode"] = "any"
    assert check_reach(level).unreached == []


@pytest.mark.parametrize(
    ("extra", "message"),
    [
        ({"relays": [{"name": "r", "mode": "most", "targets": ["STAIRS_E01_1_1"]}]}, "mode"),
        ({"relays": [{"name": "r", "targets": ["NOPE"]}]}, "targets"),
        ({"relays": [{"name": "r", "mode": "count", "targets": ["STAIRS_E01_1_1"]}]}, "count"),
        ({"items": [{"at": [0, 0], "instance": "it_gulden"}]}, "plain floor"),
        ({"items": [{"at": [2, 1], "instance": "it_gulden", "count": 0}]}, "count"),
    ],
)  # fmt: skip
def test_broken_relays_and_items(extra: dict, message: str) -> None:
    with pytest.raises(LevelError, match=message):
        parse_level("#####\n#<..#\n#####", {**doc(), **extra})


def test_level_3_zisterne_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 3 with the cistern, the Brunnenmolch in the water and two new riddles."""
    level = load_level(DATA, 3)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "TRAP", "LEVER", "BUTTON", "TELE", "NICHE", "SPAWN", "DOOR",
                     "CHEST", "STAIRS", "RELAY"}  # fmt: skip
    for water in (True, False):  # the riddles open every dry cell without wading
        reach = check_reach(level, water=water)
        assert reach.unreached == [] and reach.stuck == []
    assert level.water_depth - level.water_edge > 0 and level.water_edge >= 0.4  # snap-up edge
    molche = [sp for at, sp in level.spawns.items() if level.char(*at) == "~"]
    assert len(molche) >= 2 and {sp["npc"] for sp in molche} == {"mon_brunnenmolch"}
    npcs = {e["monster"]["npc"] for e in level.entries.values() if "monster" in e}
    assert npcs == {"mon_mauerkriecher"}
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    fall = [level.entries[at]["trapdoor"]["fall"]["world"] for at in level.cells("P")]
    assert down == fall == ["worlds/leonberg/dungeon/ebene_04.g7world"]
    assert load_level(DATA, 4).theme == "beinhaus"


def test_level_3_riddles_hold() -> None:
    """Without the pump lever the island stays out of reach (dry); with a fourth stone needed the
    depot stays shut."""
    level = load_level(DATA, 3)
    pump = level.entries[(3, 15)]["switch"]
    pump["targets"] = ["RELAY_E03_STEINE"]  # no longer switches the teleporter on
    dry = check_reach(level, water=False)
    assert (13, 13) in dry.unreached and (14, 23) in dry.unreached  # island, stairs down
    level = load_level(DATA, 3)
    level.relays["RELAY_E03_STEINE"]["count"] = 4
    assert (25, 10) in check_reach(level).unreached


def test_spawns_and_water_are_checked() -> None:
    with pytest.raises(LevelError, match="spawns"):
        parse_level("#####\n#<..#\n#####", {**doc(), "spawns": [{"at": [0, 0], "npc": "m"}]})
    with pytest.raises(LevelError, match="edgeM"):
        parse_level("#####\n#<..#\n#####", {**doc(), "water": {"depthM": 0.3, "edgeM": 0.5}})
