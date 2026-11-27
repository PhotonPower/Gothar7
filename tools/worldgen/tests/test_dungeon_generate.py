"""Dungeon generator and kit (W8 D2): vobs after engine's contract, geometry, same bytes."""

import json
import math
from pathlib import Path

import pytest

from gothar_worldgen.assemble.world import VobIds
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.dungeon import kit
from gothar_worldgen.dungeon.generate import (
    BLOCK,
    TORCHES_PER_BLOCK,
    build_world,
    generate,
    plan_level,
)
from gothar_worldgen.dungeon.level import LevelError, load_level, parse_level
from gothar_worldgen.export.terrain import world_text
from gothar_worldgen.uses.zones import zones_text

DATA = Path(__file__).resolve().parents[1] / "data" / "leonberg" / "dungeon"
UP = {"at": [1, 1], "stairs": {"world": "w.g7world", "start": "S", "facing": "S"}, "arrive": "A"}


def level1():
    return load_level(DATA, 1)


def vobs1() -> dict[str, dict]:
    return {v["name"]: v for v in plan_level(level1(), "leonberg").vobs}


def test_every_mechanic_follows_the_contract() -> None:
    by = vobs1()
    gate = by["GATE_E01_11_2"]["components"]["mover"]
    assert list(gate) == ["kind", "open", "seconds", "crush"] and gate["kind"] == "gate"
    wall = by["WALL_E01_6_8"]["components"]["mover"]
    assert wall["kind"] == "secret_wall" and wall["offset"][1] < -3.5  # sinks into the floor
    trap = by["TRAP_E01_9_10"]
    assert trap["components"]["mover"]["kind"] == "trapdoor"
    assert trap["pos"][2] == pytest.approx(10.5 * 3 - kit.TRAP_W / 2)  # the hinge edge
    plate = by["PLATE_E01_9_10"]["components"]["trigger"]
    assert plate["targets"] == ["TRAP_E01_9_10"] and plate["plate"] == {"weight": "player"}
    fall = by["FALL_E01_9_10"]["components"]["trigger"]["changeWorld"]
    assert fall == {
        "world": "worlds/leonberg/dungeon/ebene_02.g7world",
        "start": "START_E02_SCHACHT",
        "fall": True,
    }
    lever = by["LEVER_E01_6_4"]
    assert lever["components"]["mob"] == {
        "definition": "lever",
        "targets": ["GATE_E01_11_2"],
        "action": "toggle",
    }
    assert lever["pos"][2] == pytest.approx(5 * 3.0)  # on the wall face towards the south
    assert (
        by["NICHE_E01_9_7"]["components"]["mob"]["definition"] == "mob_dng_e1_niche"
    )  # content from Lua (engine #491)
    door = by["DOOR_E01_13_8"]["components"]["mob"]
    assert door == {"definition": "door", "one_way": "back"}
    tele = by["TELE_E01_16_7"]["components"]["trigger"]["teleport"]
    assert tele["to"] == "TELE_E01_16_7_ZIEL" and by["TELE_E01_16_7_ZIEL"]["type"] == "empty"
    assert by["TELE_E01_12_12"]["components"]["trigger"]["teleport"]["visible"] is False
    up = by["STAIRS_E01_1_1"]["components"]["trigger"]["changeWorld"]
    assert up == {"world": "worlds/leonberg/leonberg.g7world", "start": "START_SCHLOSS_FASS"}
    assert by["START_E01_VON_OBEN"]["type"] == "start" and "START_E01_VON_UNTEN" in by
    spawn = by["SPAWN_E01_16_10"]["components"]["spawn"]
    assert spawn == {"npc": "mon_mauerkriecher", "count": 3, "respawn": False, "dormant": True}
    assert by["PLATE_E01_17_5"]["components"]["trigger"]["plate"] == {"weight": "any"}


def test_torches_and_lights_per_block() -> None:
    by = vobs1()
    torches = [n for n in by if n.startswith("TORCH_")]
    lights = [n for n in by if by[n]["type"] == "light"]
    assert len(torches) == len(lights) > 0
    per: dict[tuple[int, int], int] = {}
    for n in torches:
        x, y = (int(v) for v in n.split("_")[2:4])
        per[(x // BLOCK, y // BLOCK)] = per.get((x // BLOCK, y // BLOCK), 0) + 1
    assert max(per.values()) <= TORCHES_PER_BLOCK


def test_blocks_have_walls_floor_and_collision() -> None:
    plan = plan_level(level1(), "leonberg")
    assert len(plan.blocks) == 6
    for mesh in plan.blocks.values():
        assert mesh.triangles() > 0 and mesh.collision
        assert {"rubble", "flag", "vault_brick"} <= set(mesh.builders)
    names = {c.name for m in plan.blocks.values() for c in m.collision}
    assert "COL_HULL_flight_1_0" in names and "COL_HULL_flight_17_11" in names  # both flights
    assert any(n.startswith("COL_HULL_niche_9_7_") for n in names)


def test_waynet_follows_the_floor() -> None:
    lv = level1()
    plan = plan_level(lv, "leonberg")
    names = {p["name"] for p in plan.points}
    assert len(names) == len(lv.cells(set(".DGPTOH<>~*m")))
    assert all(a in names and b in names for a, b, _ in plan.edges)
    assert all(math.dist(_xy(a), _xy(b)) == 1 for a, b, _ in plan.edges)
    assert any(f["name"].startswith("FP_ROAM_DNG_E01_") for f in plan.freepoints)


def _xy(name: str) -> tuple[int, int]:
    x, y = name.split("_")[-2:]
    return int(x), int(y)


def test_same_bytes_and_editor_vobs_stay(tmp_path: Path) -> None:
    lv = level1()
    plan = plan_level(lv, "leonberg")
    first = build_world(lv, plan, "leonberg", VobIds({}, 1), None)
    ids = VobIds({}, 1)
    again = build_world(lv, plan_level(lv, "leonberg"), "leonberg", ids, None)
    assert world_text(first) == world_text(again)
    assert list(first)[:6] == [
        "version",
        "name",
        "nextVobId",
        "generator",
        "staticMeshes",
        "environment",
    ]
    assert first["environment"]["sky"] is False
    hand = {
        "id": 500,
        "type": "mesh",
        "name": "HAND_SKULL",
        "pos": [1.0, 0.0, 1.0],
        "rot": [0.0, 0.0, 0.0, 1.0],
        "mesh": "props/x.glb",
    }
    edited = {**again, "vobs": [*again["vobs"], hand]}
    third = build_world(lv, plan_level(lv, "leonberg"), "leonberg", ids, edited)
    assert third["vobs"][-1] == hand
    assert [v["id"] for v in third["vobs"][:-1]] == [v["id"] for v in again["vobs"]]


def test_generate_writes_levels_kit_and_ids(tmp_path: Path) -> None:
    worlds = tmp_path / "assets" / "source" / "worlds" / "leonberg"
    data = tmp_path / "data"
    lines = generate("leonberg", DATA, worlds, data, [1, 2])
    assert any(line.startswith("ebene_02:") for line in lines)
    world = json.loads((worlds / "dungeon" / "ebene_01.g7world").read_text(encoding="utf-8"))
    for v in world["vobs"]:
        if v.get("mesh", "").startswith("worlds/"):
            assert (tmp_path / "assets" / "source" / v["mesh"]).is_file(), v["mesh"]
    second = json.loads((worlds / "dungeon" / "ebene_02.g7world").read_text(encoding="utf-8"))
    shaft = next(v for v in second["vobs"] if v["name"] == "START_E02_SCHACHT")
    assert shaft["pos"][1] > 3.5  # in the arrival shaft above the vault
    before = (worlds / "dungeon" / "ebene_01.g7world").read_bytes()
    generate("leonberg", DATA, worlds, data, [1])
    assert (worlds / "dungeon" / "ebene_01.g7world").read_bytes() == before
    assert (data / "dungeon" / "ebene_01_vob_ids.json").is_file()
    assert (
        tmp_path / "assets" / "source" / "furniture" / "textures" / "stone_albedo.png"
    ).is_file()


def test_kit_parts_have_their_frames() -> None:
    for name, build in kit.KIT.items():
        m = build()
        doc, _ = read_glb(m.glb(name))
        assert m.triangles() > 0 and any(n["name"].startswith("COL_") for n in doc["nodes"])
    gate = kit.gate()
    ys = [p[1] for b in gate.builders.values() for p in b.pos]
    assert min(ys) == pytest.approx(0.0) and max(ys) == pytest.approx(kit.GATE_H)
    trap = kit.trapdoor()
    zs = [p[2] for b in trap.builders.values() for p in b.pos]
    assert min(zs) == pytest.approx(0.0) and max(zs) == pytest.approx(kit.TRAP_W)


@pytest.mark.parametrize(
    ("text", "cells", "message"),
    [
        ("#####\n#<..#\n#.D.#\n#...#\n#####", [{"at": [2, 2], "door": {}}], "opposite"),
        ("#####\n#...#\n#<..#\n#...#\n#####", [], "wall cell behind"),
    ],
)
def test_broken_doors_and_stairs(text: str, cells: list, message: str) -> None:
    up = (
        UP
        if "wall cell" not in message
        else {**UP, "at": [1, 2], "stairs": {**UP["stairs"], "facing": "N"}}
    )
    doc = {"format": "gothar-dungeon-level", "version": 1, "level": 1, "cells": [up, *cells]}
    with pytest.raises(LevelError, match=message):
        parse_level(text, doc)


def test_fog_zone_in_the_engine_layout() -> None:
    zone = {
        "type": "fog",
        "value": "GRUFT",
        "box": {"center": [1, 2, 3], "halfExtents": [4, 1.5, 4], "yaw": 0},
        "color": [0.4, 0.42, 0.45],
        "distance": 8,
        "start": 0.5,
    }
    line = zones_text([zone]).splitlines()[1].strip()
    assert line == (
        '{"type":"fog","value":"GRUFT","box":{"center":[1.0,2.0,3.0],'
        '"halfExtents":[4.0,1.5,4.0],"yaw":0.0},"color":[0.4,0.42,0.45],'
        '"distance":8.0,"start":0.5,"fade":3.0}'
    )


def test_relays_and_items_in_the_engine_layout() -> None:
    by = {v["name"]: v for v in plan_level(load_level(DATA, 2), "leonberg").vobs}
    relay = by["RELAY_E02_TREPPE"]
    assert relay["type"] == "relay"
    assert relay["components"]["relay"] == {"mode": "all", "delay": 0.0, "once": False,
                                            "invert": False, "targets": ["GATE_E02_16_19"],
                                            "action": "open"}  # fmt: skip
    gulden = by["ITEM_E02_22_10_2"]
    assert gulden["type"] == "item" and gulden["components"]["item"] == {"instance": "it_gulden",
                                                                         "count": 60}  # fmt: skip
    assert by["ITEM_E02_22_10_0"]["components"]["item"] == {"instance": "it_ring_silver"}
    xs = sorted(by[f"ITEM_E02_22_10_{k}"]["pos"][0] for k in range(3))
    assert xs[1] - xs[0] == pytest.approx(0.4) and xs[2] - xs[1] == pytest.approx(0.4)
    button = by["BUTTON_E02_19_9"]["components"]["mob"]
    assert button["hidden"] is True and button["targets"] == ["WALL_E02_21_10"]
    lv = load_level(DATA, 2)
    world = json.loads(world_text(build_world(lv, plan_level(lv, "leonberg"), "leonberg",
                                              VobIds({}, 1), None)))  # fmt: skip
    assert [v["type"] for v in world["vobs"] if v["name"] == "RELAY_E02_TREPPE"] == ["relay"]


def test_cistern_water_boxes_and_molch_spawns() -> None:
    lv = load_level(DATA, 3)
    by = {v["name"]: v for v in plan_level(lv, "leonberg").vobs}
    water = [v for v in by.values() if v["type"] == "water"]
    assert len(water) < len(lv.cells("~")) // 3  # one box per row run, not per cell
    for v in water:
        hx, hy, _ = v["components"]["water"]["halfExtents"]
        assert v["pos"][1] + hy == pytest.approx(-lv.water_edge)  # surface below the edge
        assert v["pos"][1] - hy == pytest.approx(-lv.water_depth)  # on the basin floor
    assert sum(v["components"]["water"]["halfExtents"][0] * 2 / 3 for v in water) == len(
        lv.cells("~")
    )
    molch = by["SPAWN_E03_10_9"]
    assert molch["pos"][1] == pytest.approx(-lv.water_depth)
    assert molch["components"]["spawn"]["npc"] == "mon_brunnenmolch"
    tele = by["TELE_E03_2_12"]["components"]["trigger"]["teleport"]
    assert tele["active"] is False and tele["to"] == "TELE_E03_2_12_ZIEL"
    relay = by["RELAY_E03_STEINE"]["components"]["relay"]
    assert list(relay)[:2] == ["mode", "count"] and relay["count"] == 3 and relay["once"] is True


def test_beinhaus_props_trap_and_time_gate() -> None:
    lv = load_level(DATA, 4)
    plan = plan_level(lv, "leonberg")
    by = {v["name"]: v for v in plan.vobs}
    sarc = by["PROP_E04_22_10"]
    assert sarc["type"] == "mesh" and sarc["mesh"] == "worlds/leonberg/dungeon/kit/sarcophagus.glb"
    assert sarc["pos"] == [67.5, 0.0, 31.5]  # in the middle of its cell
    niche = by["PROP_E04_18_11"]  # facing E: against the west wall of its cell
    assert niche["mesh"].endswith("bone_niche.glb") and niche["pos"] == [54.0, 0.0, 34.5]
    points = {p["name"] for p in plan.points}
    assert "WP_DNG_E04_22_10" not in points and "WP_DNG_E04_18_11" in points
    assert not any("WP_DNG_E04_22_10" in e[:2] for e in plan.edges)
    trap = by["TELE_E04_10_2"]["components"]["trigger"]["teleport"]
    assert trap["visible"] is False and "TELE_E04_10_2_RING" not in by
    relay = by["RELAY_E04_ZEITGATTER"]["components"]["relay"]
    assert relay["delay"] == 8.0 and relay["action"] == "close"
    assert any(z["type"] == "ambient" and z["value"] == "beinhaus" for z in plan.zones)
    assert not any(name.startswith("TORCH_E04_18_11_W") for name in by)  # the niche's wall


def test_kit_props_stand_on_the_floor() -> None:
    from gothar_worldgen.dungeon import kit

    for build in kit.PROPS.values():
        m = build()
        assert m.collision and 0 < m.triangles() < 3000


def test_werkstatt_props_and_shortcut() -> None:
    lv = load_level(DATA, 5)
    plan = plan_level(lv, "leonberg")
    by = {v["name"]: v for v in plan.vobs}
    cog = by["PROP_E05_10_9"]  # facing E: on the west wall of its cell
    assert cog["mesh"].endswith("cogwheel.glb") and cog["pos"] == [30.0, 0.0, 28.5]
    bench = by["PROP_E05_12_18"]
    assert bench["mesh"].endswith("workbench.glb") and bench["pos"] == [37.5, 0.0, 55.5]
    points = {p["name"] for p in plan.points}
    assert "WP_DNG_E05_12_18" not in points and "WP_DNG_E05_10_9" in points
    tele = by["TELE_E05_25_16"]["components"]["trigger"]["teleport"]
    assert tele["world"].endswith("ebene_01.g7world") and tele["start"] == "START_E01_VON_OBEN"
    assert tele["active"] is False and tele["visible"] is True
    trap = by["TELE_E05_9_2"]["components"]["trigger"]["teleport"]
    assert trap["visible"] is False and trap["to"] == "TELE_E05_9_2_ZIEL"
    assert by["BUTTON_E05_7_1"]["components"]["mob"]["action"] == "off"


def test_labyrinth_spinner_and_doors() -> None:
    lv = load_level(DATA, 6)
    by = {v["name"]: v for v in plan_level(lv, "leonberg").vobs}
    spin = by["TELE_E06_11_9"]
    tele = spin["components"]["trigger"]["teleport"]
    assert tele["to"] == "TELE_E06_11_9_ZIEL" and tele["visible"] is False
    assert "TELE_E06_11_9_RING" not in by
    goal = by["TELE_E06_11_9_ZIEL"]
    assert goal["pos"][0] == spin["pos"][0] and goal["pos"][2] == spin["pos"][2]  # its own cell
    door = by["TELE_E06_9_1"]["components"]["trigger"]["teleport"]
    assert door["visible"] is True and "TELE_E06_9_1_RING" in by


@pytest.mark.parametrize("number", [1, 2, 3, 4, 5, 6])
def test_floor_and_ceiling_unlike_the_walls(number: int) -> None:
    """Owner 2026-11-26: flagstones on the floor, bricks or rough rock above, ribs or beams
    every few metres; the walls stay rubble."""
    from gothar_worldgen.dungeon.generate import SURFACES

    lv = load_level(DATA, number)
    floor, ceiling, rib, _ = SURFACES[lv.theme]
    plan = plan_level(lv, "leonberg")
    mats = set().union(*(m.builders for m in plan.blocks.values()))
    assert {"rubble", floor, ceiling, rib} <= mats
    assert floor.startswith("flag") and ceiling.startswith("vault_")
    kinds = {k for m in plan.blocks.values() for k in m.kinds()}
    assert {"stone", "brick" if ceiling == "vault_brick" else "rock"} <= kinds
    assert any(k.startswith("flagstone") for k in kinds)


def test_workshop_floor_has_inlays_and_themes_differ() -> None:
    from gothar_worldgen.dungeon.generate import SURFACES

    plan = plan_level(load_level(DATA, 5), "leonberg")
    mats = set().union(*(m.builders for m in plan.blocks.values()))
    assert {"oak", "plate", "oak_beam"} <= mats
    assert SURFACES["zisterne"][0] == "flag_damp" and SURFACES["beinhaus"][0] == "flag_dust"


def test_dungeon_textures_tile_and_differ() -> None:
    import numpy as np

    from gothar_worldgen.textures.procedural import DUNGEON_KINDS, make

    for kind in DUNGEON_KINDS:
        t = make(kind, 128)
        assert t.albedo.shape[:2] == (128, 128) and abs(float(t.albedo.mean()) - 1.0) < 1e-6
        assert np.all(np.isfinite(t.normal()))


def test_cave_rock_walls_ceiling_and_floor() -> None:
    """W8 E7: rock instead of masonry, no ribs; the rock steps back from the cell's edge but
    never into the walkway; the ceiling rises; the lower cave lies 1.2 m down."""
    lv = load_level(DATA, 7)
    plan = plan_level(lv, "leonberg")
    mats = set().union(*(m.builders for m in plan.blocks.values()))
    assert {"cave_wall", "cave_floor", "vault_rock", "oak", "oak_beam"} <= mats
    assert "rubble" not in mats and "flag" not in mats
    walk = set(lv.cells(set(".DGPTOH<>~*m")))
    behind = 0
    top = []
    for mesh in plan.blocks.values():
        o = mesh.origin
        for p in mesh.builders["cave_wall"].pos:
            x, y, z = p[0] + o[0], p[1] + o[1], p[2] + o[2]
            cell = (math.floor(x / 3), math.floor(z / 3))
            inside = min(x - cell[0] * 3, (cell[0] + 1) * 3 - x, z - cell[1] * 3,
                         (cell[1] + 1) * 3 - z)  # fmt: skip
            if inside > 0.01 and y < lv.height_m - 0.01 and lv.char(*cell) != "P":
                assert cell not in walk, (x, y, z)  # behind the edge, in the solid cell
                behind += 1
        top += [p[1] + o[1] for p in mesh.builders["vault_rock"].pos]
    assert behind > 1000
    assert min(top) == pytest.approx(lv.height_m) and max(top) > lv.height_m + 0.5
    points = {p["name"]: p["pos"] for p in plan.points}
    assert points["WP_DNG_E07_4_19"][1] == pytest.approx(-1.2)
    assert -1.2 < points["WP_DNG_E07_8_20"][1] < 0.0  # on the slope
    names = {c.name for m in plan.blocks.values() for c in m.collision}
    assert "COL_HULL_floor_4_19" in names and "COL_HULL_floor_8_20" in names


def test_cave_crystals_trap_and_mechanisms() -> None:
    lv = load_level(DATA, 7)
    plan = plan_level(lv, "leonberg")
    by = {v["name"]: v for v in plan.vobs}
    lights = {n: v for n, v in by.items() if v["type"] == "light"}
    clusters = [at for at, p in lv.props.items() if p["kind"] == "crystal_cluster"]
    assert len(lights) == len(clusters) and not any(n.startswith("TORCH_") for n in by)
    for v in lights.values():  # bluish
        color = v["components"]["light"]["color"]
        assert color[2] > color[0]
    fall = by["FALL_E07_6_9"]["components"]["trigger"]
    assert "changeWorld" not in fall
    assert fall["teleport"] == {"to": "START_E07_SCHACHT_UNTEN", "visible": False,
                                "filter": "player", "active": True, "silent": True,
                                "keep_velocity": True}  # fmt: skip
    start = by["START_E07_SCHACHT_UNTEN"]["pos"]
    assert start[1] == pytest.approx(-1.2 + 3.3)  # under the shaft, below 4 m: unhurt
    assert by["WALL_E07_21_4"]["mesh"].endswith("crystal_wall.glb")
    assert by["BUTTON_E07_28_4"]["mesh"].endswith("crystal_switch.glb")
    assert by["PROP_E07_4_20"]["pos"][1] == pytest.approx(lv.height_m)  # a stalactite
    depths = {v["components"]["water"]["halfExtents"][1] for v in by.values()
              if v["type"] == "water"}  # fmt: skip
    assert len(depths) == 2  # the lake and the shallow pool
    ambient = [z["value"] for z in plan.zones if z["type"] == "ambient"]
    assert ambient == ["gewoelbe"]  # until engine has its own ambient "grotte"


def test_too_many_lights_in_a_block_are_refused() -> None:
    from gothar_worldgen.dungeon.generate import DungeonError

    text = "############\n#<.........#\n#..........#\n############"
    d = {"format": "gothar-dungeon-level", "version": 1, "level": 1, "theme": "grotte",
         "cells": [UP], "noAutoTorches": True,
         "props": [{"at": [x, y], "kind": "crystal_cluster", "facing": "S" if y == 1 else "N"}
                   for x in range(2, 8) for y in (1, 2)]}  # fmt: skip
    with pytest.raises(DungeonError, match="lights"):
        plan_level(parse_level(text, d), "leonberg")


def test_cave_kit_parts() -> None:
    hang = kit.stalactite()
    ys = [p[1] for b in hang.builders.values() for p in b.pos]
    assert min(ys) < -0.8 and max(ys) >= 1.0  # tip below, root up in the rock
    rock = kit.stalagmite()
    assert max(p[1] for b in rock.builders.values() for p in b.pos) > 2.0
    assert "crystal" in kit.crystal_cluster().builders
    assert "crystal" in kit.crystal_wall().builders and "crystal" in kit.crystal_switch().builders
