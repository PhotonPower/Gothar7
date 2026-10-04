import json
from pathlib import Path

import pytest

from gothar_worldgen.assemble.world import (
    ROOT_NAME,
    AssembleError,
    VobIds,
    assemble,
    load_world,
    write_world,
)

TERRAIN = {"version": 1, "name": "t_terrain", "nextVobId": 1, "staticMeshes": [],
           "terrain": {"version": 1, "heightmap": "t.r16", "width": 2, "height": 2,
                       "cellSize": 1.0, "firstSample": [0.0, 0.0], "minY": 0.0, "maxY": 1.0},
           "vobs": []}  # fmt: skip


def entry(bid: str, x: float = 0.0, z: float = 0.0) -> dict:
    return {"id": bid, "kind": "building", "mesh": f"m/{bid.lower()}.glb", "pos": [x, 1.0, z]}


def index(*bids: str) -> dict:
    return {"entries": [entry(b, 10.0 * i) for i, b in enumerate(bids)]}


def ground(x: float, z: float) -> float:
    return 2.0


def run(idx: dict, existing: dict | None, ids: VobIds, **kw) -> dict:
    return assemble(TERRAIN, idx, existing, ids, "t", ground=ground, **kw).world


def by_name(world: dict) -> dict:
    return {v["name"]: v for v in world["vobs"]}


def test_first_run_layout_and_start_points():
    ids = VobIds({}, 1)
    w = run(index("A", "B"), None, ids)
    names = by_name(w)
    assert names[ROOT_NAME]["type"] == "empty" and "parent" not in names[ROOT_NAME]
    a = names["BLD_A"]
    assert (a["type"], a["mesh"], a["pos"]) == ("mesh", "m/a.glb", [0.0, 1.0, 0.0])
    assert (
        names[next(n for n in names if n.startswith("CELL_"))]["parent"] == names[ROOT_NAME]["id"]
    )
    assert names["START_MARKTPLATZ"]["pos"] == [0.0, 2.0, 8.0]
    assert names["START_UEBERSICHT"]["pos"] == [0.0, 42.0, 60.0]
    assert w["nextVobId"] == max(v["id"] for v in w["vobs"]) + 1 == ids.next_id
    assert list(w)[:5] == ["version", "name", "nextVobId", "staticMeshes", "terrain"]
    assert w["terrain"] == TERRAIN["terrain"]


def test_ids_are_stable_and_never_reused():
    ids = VobIds({}, 1)
    w1 = run(index("A", "B"), None, ids)
    first = {n: v["id"] for n, v in by_name(w1).items()}
    w2 = run(index("A", "B"), w1, ids)
    assert {n: v["id"] for n, v in by_name(w2).items()} == first
    # B disappears, C appears: C gets a brand-new id, B's id is not handed out again.
    w3 = run(index("A", "C"), w2, ids)
    names = by_name(w3)
    assert "BLD_B" not in names
    assert names["BLD_C"]["id"] > max(first.values())
    assert names["BLD_A"]["id"] == first["BLD_A"]
    assert ids.ids["building:B"] == first["BLD_B"]  # retired, kept in vob_ids.json


def test_editor_work_survives():
    ids = VobIds({}, 1)
    w = run(index("A"), None, ids)
    names = by_name(w)
    cell = next(v for n, v in names.items() if n.startswith("CELL_"))
    editor = {"id": 5000, "type": "empty", "name": "EDITOR_MARKER", "parent": cell["id"],
              "pos": [1.0, 2.0, 3.0], "rot": [0, 0, 0, 1]}  # fmt: skip
    moved_start = {**names["START_MARKTPLATZ"], "pos": [5.0, 5.0, 5.0]}
    w["vobs"] = [v for v in w["vobs"] if v["name"] != "START_MARKTPLATZ"] + [editor, moved_start]
    # Building A moves into another cell: its old cell would vanish, but the editor vob hangs on it.
    idx = {"entries": [entry("A", 500.0, 500.0), entry("NEW", 0.0, 0.0)]}
    res = assemble(TERRAIN, idx, w, ids, "t", ground=ground)
    names2 = by_name(res.world)
    assert names2["EDITOR_MARKER"] == editor
    assert cell["name"] in names2  # kept for its editor child
    assert names2["START_MARKTPLATZ"]["pos"] == [5.0, 5.0, 5.0]  # start points are never rewritten
    assert names2["BLD_NEW"]["id"] > 5000  # new ids above the editor's
    assert res.world["nextVobId"] > 5000 and res.kept_editor == 1


def test_locked_buildings_and_deleted_start_points():
    ids = VobIds({}, 1)
    w = run(index("A"), None, ids)
    for v in w["vobs"]:
        if v["name"] == "BLD_A":
            v["pos"] = [9.0, 9.0, 9.0]  # moved by hand in the editor
    w["vobs"] = [v for v in w["vobs"] if v["name"] != "START_UEBERSICHT"]  # deleted on purpose
    res = assemble(TERRAIN, index("A"), w, ids, "t", locked=frozenset({"A"}), ground=ground)
    names = by_name(res.world)
    assert names["BLD_A"]["pos"] == [9.0, 9.0, 9.0] and res.kept_locked == 1
    assert "START_UEBERSICHT" not in names  # not re-created
    # Without the lock the data wins again.
    names = by_name(run(index("A"), res.world, ids))
    assert names["BLD_A"]["pos"] == [0.0, 1.0, 0.0]


def test_vob_ids_file(tmp_path: Path):
    ids = VobIds({}, 1)
    run(index("A"), None, ids)
    path = tmp_path / "vob_ids.json"
    ids.save(path)
    again = VobIds.load(path)
    assert again.ids == ids.ids and again.next_id == ids.next_id
    path.write_text(json.dumps({"format": "x", "version": 1}), encoding="utf-8")
    with pytest.raises(AssembleError):
        VobIds.load(path)
    path.write_text(json.dumps({"format": "gothar-vob-ids", "version": 1,
                                "ids": {"a": 1, "b": 1}}), encoding="utf-8")  # fmt: skip
    with pytest.raises(AssembleError, match="twice"):
        VobIds.load(path)
    assert VobIds.load(tmp_path / "missing.json").next_id == 1


def test_world_file_round_trip(tmp_path: Path):
    w = run(index("A"), None, VobIds({}, 1))
    write_world(tmp_path / "t.g7world", w)
    text = (tmp_path / "t.g7world").read_text(encoding="utf-8")
    assert load_world(tmp_path / "t.g7world") == w
    assert text.count("\n    {") == len(w["vobs"])  # one vob per line
    assert load_world(tmp_path / "missing.g7world") is None
    with pytest.raises(AssembleError):
        assemble({"version": 1}, index("A"), None, VobIds({}, 1), "t")
    with pytest.raises(AssembleError):
        assemble(TERRAIN, index("A"), {"version": 2}, VobIds({}, 1), "t")


def test_mobs_at_houses_with_a_use():
    mob = {"key": "use:MOB_LEO_SCHMIEDE_ZNP_ANVIL_1", "name": "MOB_LEO_SCHMIEDE_ZNP_ANVIL_1",
           "pos": [3.0, 2.0, 1.0], "rot": [0.0, 0.70711, 0.0, 0.70711], "mesh": "mobs/anvil.glb",
           "definition": "anvil"}  # fmt: skip
    ids = VobIds({}, 1)
    w = run(index("A"), None, ids, mobs=[mob])
    names = by_name(w)
    v = names["MOB_LEO_SCHMIEDE_ZNP_ANVIL_1"]
    assert v["type"] == "mob" and v["mesh"] == "mobs/anvil.glb"
    assert v["components"] == {"mob": {"definition": "anvil"}}
    assert v["parent"] == names["WORLDGEN_USES"]["id"] and v["rot"] == mob["rot"]
    w2 = run(index("A"), w, ids, mobs=[mob])  # stable id, generator-owned
    assert by_name(w2)["MOB_LEO_SCHMIEDE_ZNP_ANVIL_1"]["id"] == v["id"]
    w3 = run(index("A"), w2, ids)  # the use is gone: its mob goes too
    assert "MOB_LEO_SCHMIEDE_ZNP_ANVIL_1" not in by_name(w3)


def test_light_prop_and_trigger_vobs_at_the_houses():
    specs = [
        {"key": "use:LIGHT_X", "name": "LIGHT_X", "type": "light", "pos": [1.0, 2.0, 3.0],
         "rot": [0.0, 0.0, 0.0, 1.0], "components": {"light": {"color": [1, 0.6, 0.3],
                                                              "range": 6.0, "intensity": 2.5,
                                                              "flicker": 0.3}}},
        {"key": "use:PROP_X", "name": "PROP_X", "type": "mesh", "pos": [1.0, 2.0, 3.0],
         "rot": [0.0, 0.0, 0.0, 1.0], "mesh": "props/hearth.glb"},
        {"key": "use:TRIGGER_X", "name": "TRIGGER_X", "type": "trigger", "pos": [1.0, 2.0, 3.0],
         "rot": [0.0, 0.0, 0.0, 1.0],
         "components": {"trigger": {"shape": "box", "halfExtents": [2, 1.5, 3], "owner": "npc_a"}}},
    ]  # fmt: skip
    names = by_name(run(index("A"), None, VobIds({}, 1), mobs=specs))
    assert names["LIGHT_X"]["type"] == "light" and "mesh" not in names["LIGHT_X"]
    assert names["LIGHT_X"]["components"]["light"]["flicker"] == 0.3
    assert names["PROP_X"]["type"] == "mesh" and "components" not in names["PROP_X"]
    assert names["TRIGGER_X"]["components"]["trigger"]["owner"] == "npc_a"
