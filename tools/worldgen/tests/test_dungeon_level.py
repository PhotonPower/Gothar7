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


def test_level_4_beinhaus_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 4 with crypts, the Beinhauswart once, a time gate, a teleport trap, a depot."""
    level = load_level(DATA, 4)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "BUTTON", "TELE", "NICHE", "SPAWN", "DOOR", "CHEST", "STAIRS",
                     "RELAY"}  # fmt: skip
    assert 24 <= level.width <= 32 and 20 <= level.height <= 32
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == []
    assert len(reach.cells) + len(level.blocked) >= 0.4 * level.width * level.height
    npcs = [e["monster"] for e in level.entries.values() if "monster" in e]
    assert [m["npc"] for m in npcs].count("mon_beinhauswart") == 1
    assert [m["count"] for m in npcs if m["npc"] == "mon_beinhauswart"] == [1]  # exactly one
    assert {m["npc"] for m in npcs} == {"mon_beinhauswart", "mon_mauerschlupfer"}
    assert {p["kind"] for p in level.props.values()} == {"sarcophagus", "bone_niche"}
    traps = [at for at in level.cells("T") if level.entries[at]["teleport"].get("trap")]
    assert len(traps) == 1
    assert level.relays["RELAY_E04_ZEITGATTER"]["delay"] > 0
    assert {it["at"] for it in level.items} >= {(12, 16)}  # the depot's finds
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    assert down == ["worlds/leonberg/dungeon/ebene_05.g7world"]
    assert load_level(DATA, 5).theme == "werkstatt"


def test_level_4_riddles_hold() -> None:
    """A shorter time gate shuts before the hero is through; without the loose stone the
    forgotten crypt is a dead end; without the depot's stone the depot stays shut."""
    level = load_level(DATA, 4)
    level.relays["RELAY_E04_ZEITGATTER"]["delay"] = 4.0
    assert (22, 19) in check_reach(level).unreached  # the stairs down
    level = load_level(DATA, 4)
    level.entries[(11, 5)]["switch"]["targets"] = ["RELAY_E04_ZEITGATTER"]
    reach = check_reach(level)
    assert (7, 6) in reach.stuck and reach.unreached == [(8, 7)]
    level = load_level(DATA, 4)
    level.entries[(9, 12)]["switch"]["targets"] = ["RELAY_E04_ZEITGATTER"]
    assert (12, 16) in check_reach(level).unreached


@pytest.mark.parametrize(("gap", "through"), [(2, True), (8, True), (9, False)])
def test_a_time_gate_is_open_only_for_a_few_cells(gap: int, through: bool) -> None:
    """Button at the west end, gate ``gap`` cells further; the relay shuts it after its delay
    (8 s: (8 - 2) * 4 / 3 = 8 cells)."""
    w = gap + 6
    gate = f"GATE_E01_{gap + 2}_2"
    rows = ["#" * w, "##S" + "#" * (w - 3), "#<" + "." * gap + "G..#",
            "#" * (gap + 4) + "S#", "#" * w]  # fmt: skip
    button = {"type": "wall_button", "facing": "S", "targets": [gate, "RELAY_E01_ZEIT"],
              "action": "open"}  # fmt: skip
    back = {**button, "facing": "N"}  # the way back from the far side
    relay = {"name": "zeit", "mode": "any", "delay": 8.0, "targets": [gate], "action": "close"}
    d = doc({"at": [gap + 2, 2], "gate": {"open": False}}, {"at": [2, 1], "switch": button},
            {"at": [gap + 4, 3], "switch": back})  # fmt: skip
    d["cells"][0] = {**UP, "at": [1, 2]}
    level = parse_level("\n".join(rows), {**d, "relays": [relay]})
    reach = check_reach(level)
    assert ((gap + 3, 2) in reach.cells) is through
    assert reach.stuck == []


def test_a_wall_button_only_sends_its_action() -> None:
    """A button that closes never opens (no counterpart, unlike a lever)."""
    text = "#######\n#<..G.#\n###S###\n#######"
    button = {"type": "wall_button", "targets": ["GATE_E01_4_1"], "action": "close"}
    level = parse_level(text, doc({"at": [4, 1], "gate": {"open": False}},
                                  {"at": [3, 2], "switch": button}))  # fmt: skip
    assert check_reach(level).unreached == [(4, 1), (5, 1)]
    button["type"] = "lever"
    assert check_reach(level).unreached == []


@pytest.mark.parametrize(
    ("props", "message"),
    [
        ([{"at": [2, 1], "kind": "urn"}], "kind"),
        ([{"at": [1, 1], "kind": "sarcophagus"}], "plain floor"),
        ([{"at": [2, 1], "kind": "bone_niche", "facing": "E"}], "wall behind"),
        ([{"at": [3, 1], "kind": "sarcophagus"}], "needed"),
    ],
)
def test_props_are_checked(props: list, message: str) -> None:
    d = {**doc(), "props": props, "items": [{"at": [3, 1], "instance": "it_gulden"}]}
    with pytest.raises(LevelError, match=message):
        parse_level("#####\n#<..#\n#####", d)


def test_a_sarcophagus_fills_its_cell() -> None:
    level = parse_level("######\n#<...#\n######", {**doc(), "props": [
        {"at": [3, 1], "kind": "sarcophagus", "facing": "E"}]})  # fmt: skip
    assert level.blocked == {(3, 1)}
    assert check_reach(level).unreached == [(4, 1)]


def test_level_5_werkstatt_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 5 with Gatterbock statues, Mauerschlupfer, a reset trap, weight and lever,
    a hidden depot and the shortcut up to Ebene 1 (plan F6)."""
    level = load_level(DATA, 5)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "LEVER", "BUTTON", "PLATE", "TELE", "NICHE", "SPAWN", "DOOR",
                     "CHEST", "STAIRS", "RELAY"}  # fmt: skip
    assert 24 <= level.width <= 32 and 24 <= level.height <= 32
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == []
    assert len(reach.cells) + len(level.blocked) >= 0.4 * level.width * level.height
    npcs = [e["monster"]["npc"] for e in level.entries.values() if "monster" in e]
    assert set(npcs) == {"mon_gatterbock", "mon_mauerschlupfer"}  # the owner's choice for E5
    assert {p["kind"] for p in level.props.values()} == {"workbench", "cogwheel"}
    traps = [at for at in level.cells("T") if level.entries[at]["teleport"].get("trap")]
    assert len(traps) == 1
    up = [level.entries[at]["teleport"] for at in level.cells("T")
          if "world" in level.entries[at]["teleport"]]  # fmt: skip
    assert [(t["world"], t["active"]) for t in up] == [
        ("worlds/leonberg/dungeon/ebene_01.g7world", False)
    ]
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    assert down == ["worlds/leonberg/dungeon/ebene_06.g7world"]
    assert load_level(DATA, 6).theme == "labyrinth"


def test_level_5_riddles_hold() -> None:
    """Without the loose stone the trap keeps the hall shut; without the Schmiede's lever the
    weight alone does not open the clockwork; without the depot's stone the depot stays shut."""
    level = load_level(DATA, 5)
    level.entries[(7, 1)]["switch"]["targets"] = ["RELAY_E05_GEWICHT"]
    reach = check_reach(level)
    assert (15, 9) in reach.unreached and (23, 21) in reach.unreached
    level = load_level(DATA, 5)
    lever = level.entries[(2, 15)]["switch"]  # still an input, but never thrown "on"
    lever["type"], lever["action"] = "wall_button", "close"
    assert (14, 14) in check_reach(level).unreached
    level = load_level(DATA, 5)
    level.entries[(19, 4)]["switch"]["targets"] = ["GATE_E05_8_12"]
    assert (23, 9) in check_reach(level).unreached


@pytest.mark.parametrize("number", [1, 2, 3, 4, 5, 6])
def test_chests_and_niches_name_their_lua_definition(number: int) -> None:
    """engine's contents (game/scripts/mobs/dungeon.lua): one definition per chest and niche."""
    level = load_level(DATA, number)
    defs = [level.entries[at][level.kind(at)]["definition"] for at in level.cells("*N")]
    assert defs and all(d.startswith(f"mob_dng_e{number}_") for d in defs)
    assert len(set(defs)) == len(defs)


def test_level_6_labyrinth_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 6, a maze with teleporter doors, spinners and hidden walls."""
    level = load_level(DATA, 6)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "LEVER", "BUTTON", "TELE", "NICHE", "SPAWN", "CHEST", "STAIRS"}
    assert 24 <= level.width <= 32 and 24 <= level.height <= 32
    reach = check_reach(level)
    assert reach.unreached == [] and reach.stuck == []
    assert len(reach.cells) >= 0.4 * level.width * level.height
    npcs = {e["monster"]["npc"] for e in level.entries.values() if "monster" in e}
    assert npcs == {"mon_mauerschlupfer"}  # the owner's choice: Mauerschlupfer on 4-6
    pads = [level.entries[at]["teleport"] for at in level.cells("T")]
    assert sum(1 for t in pads if t.get("spin") and t.get("trap")) >= 3  # spinners
    assert sum(1 for t in pads if "to" in t and t.get("visible")) >= 4  # teleporter doors
    assert len(level.cells("H")) >= 2
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    assert down == ["worlds/leonberg/dungeon/ebene_07.g7world"]
    assert load_level(DATA, 7).theme == "grotte"


def test_level_6_riddles_hold() -> None:
    """The lever behind the teleporter door opens the stairs; the loose stones the hidden walls."""
    for at, target in [((3, 22), (27, 27)), ((28, 6), (28, 2)), ((12, 5), (13, 3))]:
        level = load_level(DATA, 6)
        level.entries[at]["switch"]["targets"] = ["SPAWN_E06_23_3"]  # does nothing for the way
        assert target in check_reach(level).unreached, at


def test_a_spinner_only_turns() -> None:
    level = parse_level("######\n#<.T.#\n######", doc(
        {"at": [3, 1], "teleport": {"spin": True, "trap": True, "facing": "W"}}))  # fmt: skip
    assert check_reach(level).unreached == []  # the cell behind it stays in reach
    with pytest.raises(LevelError, match="spinner"):
        parse_level("######\n#<.T.#\n######", doc({"at": [3, 1], "teleport": {"spin": True}}))


def test_level_7_kristallgrotte_is_complete_and_solvable() -> None:
    """W8 D3: Ebene 7, a natural cave with crystal light, a lake with a jetty, a crystal wall,
    a crystal switch, a trap into the lower cave of the same level and the shortcut up."""
    level = load_level(DATA, 7)
    kinds = {name.split("_")[0] for name in level.targets()}
    assert kinds >= {"GATE", "WALL", "LEVER", "BUTTON", "TELE", "NICHE", "SPAWN", "CHEST",
                     "STAIRS", "TRAP"}  # fmt: skip
    assert 24 <= level.width <= 32 and 24 <= level.height <= 32
    for water in (True, False):  # the riddles open every dry cell without wading
        reach = check_reach(level, water=water)
        assert reach.unreached == [] and reach.stuck == []
    assert len(check_reach(level).cells) + len(level.blocked) >= 0.4 * level.width * level.height
    npcs = [e["monster"]["npc"] for e in level.entries.values() if "monster" in e]
    npcs += [s["npc"] for s in level.spawns.values()]
    assert set(npcs) == {"mon_kristallkrebs", "mon_brunnenmolch", "mon_mauerschlupfer"}
    molche = [at for at, s in level.spawns.items() if s["npc"] == "mon_brunnenmolch"]
    assert 1 <= len(molche) <= 2 and all(level.char(*at) == "~" for at in molche)
    assert npcs.count("mon_kristallkrebs") > npcs.count("mon_mauerschlupfer")
    assert {p["kind"] for p in level.props.values()} == {"crystal_cluster", "stalagmite",
                                                         "stalactite"}  # fmt: skip
    assert level.raw["noAutoTorches"] and level.jetty and level.shallow and level.heights
    assert min(level.heights.values()) <= -1.0  # the lower cave
    walls = [level.entries[at]["secret_wall"] for at in level.cells("H")]
    assert [w.get("look") for w in walls] == ["crystal"]
    switches = [level.entries[at]["switch"] for at in level.cells("S")]
    assert any(s.get("look") == "crystal" and s["targets"][0].startswith("GATE") for s in switches)
    (trap,) = level.cells("P")
    arrival = level.same_level_fall(trap)
    assert arrival is not None and arrival["shaft"] and level.dy(arrival["at"]) <= -1.0
    up = [level.entries[at]["teleport"] for at in level.cells("T")]
    assert [(t["world"], t["start"], t["active"]) for t in up] == [
        ("worlds/leonberg/dungeon/ebene_01.g7world", "START_E01_VON_OBEN", False)
    ]
    down = [level.entries[at]["stairs"]["world"] for at in level.cells(">")]
    assert down == ["worlds/leonberg/dungeon/ebene_08.g7world"]
    hort = load_level(DATA, 8)
    assert hort.theme == "hort" and hort.height_m >= 4.5  # the Kellermutter is 5.5 m long


def test_level_7_riddles_hold() -> None:
    """Without the loose stone the crystal wall stays shut (and with it the switch, the gate and
    the stairs); without the crystal switch the Treppenkammer; without the lever below, the
    lower cave has no way back; without the trapdoor nobody gets down there first."""
    level = load_level(DATA, 7)
    level.entries[(19, 2)]["switch"]["targets"] = ["SPAWN_E07_17_3"]
    reach = check_reach(level)
    assert (25, 3) in reach.unreached and (26, 23) in reach.unreached
    level = load_level(DATA, 7)
    level.entries[(28, 4)]["switch"]["targets"] = ["SPAWN_E07_17_3"]
    assert (26, 21) in check_reach(level).unreached
    level = load_level(DATA, 7)
    level.entries[(11, 21)]["switch"]["targets"] = ["SPAWN_E07_17_3"]
    assert (4, 19) in check_reach(level).stuck
    level = load_level(DATA, 7)
    level.entries[(6, 9)]["trapdoor"]["fall"] = {"world": "w.g7world", "start": "S"}
    assert (4, 19) in check_reach(level).unreached


def test_a_same_level_trapdoor_leads_to_its_arrival() -> None:
    shaft = {"name": "START_E01_UNTEN", "at": [5, 1], "shaft": True}
    trap = {"at": [3, 1], "trapdoor": {"fall": {"start": "START_E01_UNTEN"}}}
    d = doc(trap)
    d["starts"] = [shaft]
    reach = check_reach(parse_level("########\n#<.P#..#\n########", d))
    assert reach.unreached == [] and reach.stuck == [(3, 1), (5, 1), (6, 1)]  # no way back
    reach = check_reach(parse_level("########\n#<.P#..#\n##....##\n########", d))
    assert reach.unreached == [] and reach.stuck == []
    d["starts"] = [{**shaft, "shaft": False}]
    with pytest.raises(LevelError, match="shaft start"):
        parse_level("########\n#<.P#..#\n########", d)


@pytest.mark.parametrize(
    ("text", "heights", "message"),
    [
        ("#######\n#<....#\n#######", [{"from": [4, 1], "to": [5, 1], "dy": -0.7}], "steps"),
        ("#######\n#<....#\n#######", [{"from": [2, 1], "to": [5, 1], "dy": -0.4}],
         "mechanism"),  # the stairs' neighbour
        ("#######\n#<..~.#\n#######", [{"from": [5, 1], "to": [5, 1], "dy": 0.2}], "water"),
        ("#######\n#<....#\n#######", [{"from": [4, 1], "to": [4, 1], "dy": 0.9}], "dy"),
    ],
)  # fmt: skip
def test_heights_are_checked(text: str, heights: list, message: str) -> None:
    d = doc()
    d["heights"] = heights
    with pytest.raises(LevelError, match=message):
        parse_level(text, d)
