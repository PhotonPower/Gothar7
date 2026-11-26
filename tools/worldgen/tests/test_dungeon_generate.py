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
    assert by["NICHE_E01_9_7"]["components"]["mob"]["definition"] == "niche"
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
        assert {"rubble", "floor"} <= set(mesh.builders)
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
