import json
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import box

from gothar_worldgen.buildings.gltf import CollisionPart, MeshData, Primitive, glb_bytes_multi
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.qa.begehung import (
    LIMITS,
    Character,
    citywall,
    collision_parts,
    door_steps,
    lanes,
    load_bodies,
    run,
    slab_section,
    slopes,
    slots,
)

CH = Character(0.3, 1.8, 0.4, 50.0, 1.62)
AREA = box(-50, -50, 50, 50)


def cuboid(x0: float, z0: float, x1: float, z1: float, y0: float, y1: float):
    p = np.array([[x, y, z] for y in (y0, y1) for z in (z0, z1) for x in (x0, x1)], np.float32)
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    idx = [i for a, b, c, d in quads for i in (a, b, c, a, c, d)]
    return p, np.array(idx, np.uint32)


def glb(parts) -> bytes:
    p, i = cuboid(0, 0, 1, 1, 0, 1)
    mesh = MeshData(p, np.zeros_like(p), np.zeros((len(p), 2), np.float32), i)
    col = [CollisionPart(f"COL_HULL_{k}", *cuboid(*c)) for k, c in enumerate(parts)]
    return glb_bytes_multi([Primitive("m", (1, 1, 1, 1), mesh)], "t", col)


def flat(height: float = 0.0, slope: float = 0.0) -> Grid:
    xs = np.arange(120) - 59.5
    h = np.tile(height + slope * xs, (120, 1))
    return Grid(h, -59.5, -59.5, 1.0)


def world_with(tmp_path: Path, houses: dict[str, list[tuple]], grid: Grid) -> dict:
    """World with one mesh vob per house (vob at its pos, collision cuboids relative to it)."""
    vobs = [{"id": 1, "type": "empty", "name": "G", "pos": [0.0, 0.0, 0.0], "rot": [0, 0, 0, 1]}]
    for k, (name, (pos, parts)) in enumerate(houses.items(), 2):
        path = tmp_path / f"{name}.glb"
        path.write_bytes(glb(parts))
        vobs.append(
            {
                "id": k,
                "type": "mesh",
                "name": name,
                "parent": 1,
                "pos": pos,
                "rot": [0.0, 0.0, 0.0, 1.0],
                "mesh": path.name,
            }
        )
    return {"vobs": vobs}


# Two houses with a 1.0 m lane between them (x 0..1), a third pair 0.5 m apart.
HOUSES = {
    "BLD_A": ([-6.0, 0.0, 0.0], [(-4, -10, 6, 10, -0.5, 8)]),  # x -10..0
    "BLD_B": ([6.0, 0.0, 0.0], [(-5, -10, 4, 10, -0.5, 8)]),  # x 1..10
    "BLD_C": ([0.0, 0.0, 30.0], [(-10, -3, -0.25, 3, -0.5, 8)]),
    "BLD_D": ([0.0, 0.0, 30.0], [(0.25, -3, 10, 3, -0.5, 8)]),
    "BLD_LOW": ([30.0, 0.0, 0.0], [(0, 0, 2, 2, 0, 0.3)]),  # a kerb the character steps over
}


def test_collision_parts_and_slab_section():
    parts = collision_parts(glb([(0, 0, 2, 3, 0, 5)]))
    assert [n for n, _, _ in parts] == ["COL_HULL_0"]
    _, pos, tris = parts[0]
    sec = slab_section(pos, tris, 1.0, 2.0)
    assert sec.area == pytest.approx(6.0)
    assert slab_section(pos, tris, 6.0, 7.0) is None  # above the body
    # a wedge: the cut at y 1..2 only covers the part that is that high
    p = np.array([[0, 0, 0], [4, 0, 0], [0, 0, 1], [4, 0, 1], [0, 4, 0], [0, 4, 1]], np.float64)
    t = np.array(
        [[0, 1, 4], [2, 5, 3], [0, 2, 3], [0, 3, 1], [0, 4, 5], [0, 5, 2], [1, 3, 5], [1, 5, 4]]
    )
    assert slab_section(p, t, 1.0, 2.0).bounds[2] == pytest.approx(3.0)


def test_bodies_are_placed_and_low_ones_dropped(tmp_path):
    grid = flat()
    world = world_with(tmp_path, HOUSES, grid)
    bodies = load_bodies(world, tmp_path, grid, CH)
    names = sorted(b.owner for b in bodies)
    assert names == ["BLD_A", "BLD_B", "BLD_C", "BLD_D"]  # the kerb is below step height
    a = next(b for b in bodies if b.owner == "BLD_A")
    assert a.poly.bounds == pytest.approx((-10.0, -10.0, 0.0, 10.0))


def test_lanes_slots_and_ways_through_bodies(tmp_path):
    grid = flat()
    bodies = load_bodies(world_with(tmp_path, HOUSES, grid), tmp_path, grid, CH)
    streets = [
        {"osmId": "w1", "highway": "footway", "points": [[0.5, -8.0], [0.5, 8.0]]},  # 1.0 m lane
        {"osmId": "w2", "highway": "footway", "points": [[0.0, 24.0], [0.0, 36.0]]},  # 0.5 m gap
        {"osmId": "w3", "highway": "footway", "points": [[-8.0, -20.0], [-8.0, 20.0]]},  # in A
        {"osmId": "w4", "highway": "footway", "points": [[20.0, -20.0], [20.0, 20.0]]},  # open
        {"osmId": "w5", "highway": "motorway", "points": [[0.5, -8.0], [0.5, 8.0]]},
    ]
    res = lanes(streets, bodies, AREA, LIMITS)
    by = {(f["osmId"], f["kind"]) for f in res["list"]}
    assert ("w1", "tight") in by and ("w2", "blocked") in by and ("w3", "throughBody") in by
    assert not any(f["osmId"] in ("w4", "w5") for f in res["list"])
    tight = next(f for f in res["list"] if f["osmId"] == "w1")
    assert tight["minWidthM"] == pytest.approx(1.0, abs=0.05)
    assert res["throughByOwner"] == {"BLD": 1}
    s = slots(bodies, AREA, CH)
    assert s["count"] >= 1  # the 0.5 m gap between C and D
    assert any(abs(p["at"][0]) < 0.5 and abs(p["at"][1] - 30) < 4 for p in s["largest"])


def test_slopes_and_steps():
    grid = flat(slope=0.0)
    grid.heights[:, 70:74] += np.arange(1, 5) * 1.5  # a 1.5 m per metre ramp (56°) at x 10.5..13.5
    grid.heights[:, 74:] += 6.0
    streets = [
        {"osmId": "w1", "highway": "path", "points": [[0.0, 0.0], [30.0, 0.0]]},
        {"osmId": "w2", "highway": "steps", "points": [[0.0, 5.0], [30.0, 5.0]]},
        {"osmId": "w3", "highway": "path", "points": [[0.0, -30.0], [0.0, -10.0]]},  # flat
    ]
    res = slopes(streets, grid, AREA, CH, LIMITS)
    assert res["runs"]["tooSteep"] >= 1
    assert all(f["osmId"] == "w1" for f in res["list"])
    assert res["steps"]["count"] == 1 and res["steps"]["tooSteep"] == 1
    assert res["steps"]["list"][0]["riseM"] == pytest.approx(6.0, abs=0.1)


def test_door_steps():
    grid = flat(slope=-0.2)  # falls 0.2 m per metre towards +x
    streets = [{"osmId": "w1", "highway": "residential", "points": [[-20.0, -6.0], [20.0, -6.0]]}]
    buildings = [
        # street side (north, z = -4) is level with the floor at x = 0: fine
        {"id": "OK", "footprint": [[-3, -4], [3, -4], [3, 4], [-3, 4]]},
        # floor 1 m under the ground in front of the door: buried
        {"id": "LOW", "footprint": [[-13, -4], [-7, -4], [-7, 4], [-13, 4]]},
        # floor 1 m above the ground: a high step
        {"id": "HIGH", "footprint": [[7, -4], [13, -4], [13, 4], [7, 4]]},
    ]
    index = {
        "entries": [
            {"id": "OK", "groundY": 0.0},
            {"id": "LOW", "groundY": 1.0},
            {"id": "HIGH", "groundY": -1.0},
        ]
    }
    res = door_steps(buildings, index, streets, grid, AREA, LIMITS)
    assert res["checked"] == 3
    assert [d["id"] for d in res["buriedList"]] == ["LOW"]
    assert [d["id"] for d in res["highList"]] == ["HIGH"]
    assert res["buriedList"][0]["stepM"] == pytest.approx(-1.0, abs=0.05)
    assert res["highList"][0]["stepM"] == pytest.approx(1.0, abs=0.05)
    # HIGH: the uphill (west) edge would fit (step 0.28 m); LOW: no edge within the limits
    assert res["fixableByOtherEdge"] == 1


def test_citywall_rows_against_the_character():
    rules = json.loads(
        (Path(__file__).parents[1] / "data" / "building_rules.json").read_text("utf-8")
    )
    rows = {r["name"]: r for r in citywall(rules, {"openEnds": []}, CH)}
    assert all(r["ok"] for r in rows.values())
    assert rows["Treppenstufe Höhe"]["value"] <= CH.step
    rules["cityWall"]["walkM"] = 0.7
    rules["cityWall"]["stairs"]["rise"] = 0.5
    bad = {r["name"] for r in citywall(rules, {"openEnds": [[0, 0]]}, CH) if not r["ok"]}
    assert bad == {"Wehrgang Breite", "Treppenstufe Höhe", "Treppe Steigung", "offene Mauerenden"}


def test_run_end_to_end(tmp_path):
    grid = flat()
    assets = tmp_path / "assets"
    folder = assets / "worlds" / "t"
    (folder / "generated").mkdir(parents=True)
    raw = np.zeros((120, 120), "<u2")
    raw.tofile(folder / "generated" / "t.r16")
    world = world_with(folder, HOUSES, grid)
    for v in world["vobs"]:
        if "mesh" in v:
            v["mesh"] = f"worlds/t/{v['mesh']}"
    world["terrain"] = {
        "heightmap": "worlds/t/generated/t.r16",
        "width": 120,
        "height": 120,
        "cellSize": 1.0,
        "firstSample": [-59.5, -59.5],
        "minY": 0.0,
        "maxY": 1.0,
    }
    (folder / "t.g7world").write_text(json.dumps(world), encoding="utf-8")
    (folder / "generated" / "buildings_index.json").write_text(json.dumps({"entries": []}), "utf-8")
    work = tmp_path / "work"
    work.mkdir()
    (work / "streets.json").write_text(
        json.dumps(
            {
                "streets": [
                    {"osmId": "w1", "highway": "footway", "points": [[0.5, -8.0], [0.5, 8.0]]}
                ]
            }
        ),
        "utf-8",
    )
    (work / "buildings.json").write_text(json.dumps({"buildings": []}), "utf-8")
    movement = tmp_path / "movement.toml"
    movement.write_text("[ground]\nstep_height = 0.4\nmax_slope_degrees = 50.0\n", "utf-8")
    rules = json.loads(
        (Path(__file__).parents[1] / "data" / "building_rules.json").read_text("utf-8")
    )
    rep = run(folder / "t.g7world", assets, work, rules, movement, 50.0)
    assert rep["format"] == "gothar-begehung" and rep["bodies"] == 4
    assert rep["lanes"]["runs"]["tight"] == 1
    json.dumps(rep)  # serialisable


def test_site_start_points(tmp_path):
    from gothar_worldgen.export.starts import load_starts

    assert load_starts(tmp_path / "missing.json") == ()
    path = tmp_path / "starts.json"
    path.write_text(
        json.dumps({"starts": [{"name": "START_X", "x": 1.0, "z": 2.0, "yawDeg": 90.0}]})
    )
    (s,) = load_starts(path)
    assert (s.name, s.x, s.z, s.height_above_ground) == ("START_X", 1.0, 2.0, 0.0)
    assert s.rot == pytest.approx((0.0, 0.707107, 0.0, 0.707107))
    real = load_starts(Path(__file__).parents[1] / "data" / "leonberg" / "starts.json")
    assert len(real) >= 9 and all(p.name.startswith("START_BG_") for p in real)
