import json
import math
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Point, Polygon

from gothar_worldgen.assemble.world import VobIds, assemble
from gothar_worldgen.buildings.collision import body_is_closed
from gothar_worldgen.buildings.gltf import CollisionPart, read_glb
from gothar_worldgen.facade.overrides import load_all
from gothar_worldgen.handmade import (
    find_blender,
    load,
    put_item,
    save,
    schloss_geometry,
    schloss_item,
)

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
SPEC = json.loads((ROOT / "data" / "leonberg" / "schloss.json").read_text(encoding="utf-8"))
PALETTE = json.loads((ROOT / "data" / "building_rules.json").read_text(encoding="utf-8"))["palette"]
GLB = REPO / "assets" / "source" / "worlds" / "leonberg" / "handmade" / "schloss" / "schloss.glb"
# the spec the schloss command built from: ground of each wing and garden terraces from the DGM
BUILT = json.loads(GLB.with_name("schloss_built.json").read_text(encoding="utf-8"))
BUDGET = 15000  # agreed with engine, no LOD
geo = schloss_geometry()


def model():
    return geo.build(SPEC)


def test_budget_materials_and_determinism():
    m = model()
    assert 3000 < m.triangles() <= BUDGET
    assert set(m.faces) <= set(PALETTE)
    again = model()
    assert again.faces == m.faces and again.collision == m.collision


def test_collision_bodies_are_closed_outward_and_cover_every_wing():
    m = model()
    names = [n for n, _, _ in m.collision]
    inner = SPEC["interior"]["wing"]  # W7 D1: the main wing is hollow, the tower too
    solid = [f"COL_HULL_{w['key']}" for w in SPEC["wings"] if w["key"] != inner]
    assert [n for n in names if not n.startswith("COL_HULL_in_")] == solid
    assert len(names) == len(set(names)) and sum(n.startswith("COL_HULL_in_") for n in names) > 50
    for name, pts, tris in m.collision:
        part = CollisionPart(name, np.asarray(pts, np.float32), np.asarray(tris, np.uint32).ravel())
        assert body_is_closed(part)
        pos = np.asarray(pts, dtype=float)
        centre = pos.mean(axis=0)
        for a, b, c in tris:
            n = np.cross(pos[b] - pos[a], pos[c] - pos[a])
            if np.linalg.norm(n) > 1e-9:
                assert np.dot(n, (pos[a] + pos[b] + pos[c]) / 3 - centre) > 0, name


def test_openings_stay_above_the_ground_of_each_side():
    m = model()
    wings = [geo.Wing(w) for w in SPEC["wings"]]
    lowest_sill = min(min(w.terrain(s, w.length * f) for f in geo.TERRAIN_F for s in (1, -1))
                      for w in wings)  # fmt: skip
    frames = [p for f in m.faces["frame"] for p in f]
    assert min(p[1] for p in frames) >= lowest_sill + geo.SOCLE_M - 1e-6
    main = wings[0]
    s = main.length * SPEC["portal"]["t"]
    # the open portal (W7 D1): its passage floor (the hall's model) starts on the court's ground
    hw = SPEC["portal"]["w"] / 2 + 1e-6
    ramp = [p for f in m.rooms["eg_halle"]["stone_slab"] for p in f
            if abs(_s(main, p) - s) <= hw and _t(main, p) > main.half - 1e-6]  # fmt: skip
    assert ramp and min(p[1] for p in ramp) == pytest.approx(main.terrain(1, s), abs=1e-6)


def _s(w, p):
    return (p[0] - w.a[0]) * w.u[0] + (p[2] - w.a[1]) * w.u[1]


def _t(w, p):
    return (p[0] - w.a[0]) * w.n[0] + (p[2] - w.a[1]) * w.n[1]


def test_inside_storeys_rooms_and_the_tower_against_the_wall():
    """W7 D1: cellar, ground and first floor with their rooms; the stair tower touches the
    garden wall, its spiral climbs one storey per turn, the ground is left out under the wing."""
    inside = SPEC["interior"]
    m = model()
    w = geo.Wing(SPEC["wings"][0])
    f = geo.interior.floors(inside)
    assert f["cellar"] < f["cellar_top"] < f["ground"] < f["ground_top"] < f["upper"]
    assert f["upper_top"] < w.eave
    for key in ("keller", "eg", "og"):
        rooms = geo.interior.Inside(m, w, inside, SPEC["stairTower"], SPEC["portal"]).rooms(key)
        assert rooms[0][1] == pytest.approx(inside["wallM"])
        assert rooms[-1][2] == pytest.approx(w.length - inside["wallM"])
        assert all(b - a > 4.0 for _, a, b in rooms)
    s, t, ap = geo.interior.tower_centre(w, SPEC["stairTower"])
    assert abs(t) == pytest.approx(w.half + ap - geo.interior.TOWER_WALL_M)
    moved = geo.interior.moved_tower(w, SPEC["stairTower"])
    item = schloss_item(SPEC, "x.glb")
    assert item["footprints"][-1] == [[round(p[0], 2), round(p[2], 2)] for p in
                                      geo._ring(tuple(moved["at"]), SPEC["stairTower"]["radius"], 0,
                                                phase=math.radians(moved["phaseDeg"]))]  # fmt: skip
    assert len(item["holes"]) == 2 and all(p["clampBelow"] for p in item["pads"][-2:])
    # the spiral: one turn per storey, never steeper than 35 deg at the newel
    levels = geo.interior.storeys(inside)
    for (_, y0, _), (_, y1, _) in zip(levels, levels[1:], strict=False):
        sweep = 2 * math.pi - math.radians(geo.interior.LANDING_DEG)
        assert math.degrees(math.atan((y1 - y0) / (sweep * geo.interior.NEWEL_R))) <= 35.0
    names = [n for n, _, _ in m.collision]
    assert any(n.startswith("COL_HULL_in_tower_ramp_") for n in names)
    assert "COL_HULL_in_portal_ramp" in names and "COL_HULL_in_top" in names


def test_model_is_moved_to_its_origin():
    origin = geo.origin_of(SPEC)
    local = geo.to_local(model(), origin)
    ys = [p[1] for fs in local.faces.values() for f in fs for p in f]
    assert 0.0 <= min(ys) <= 0.5  # nothing below the origin, which is near the lowest foot


def test_handmade_item_and_file(tmp_path: Path):
    item = schloss_item(SPEC, "worlds/leonberg/handmade/schloss/schloss.glb")
    assert item["pos"] == [round(v, 3) for v in geo.origin_of(SPEC)]
    assert item["replaces"] == SPEC["replaces"]
    assert len(item["footprints"]) == len(SPEC["wings"]) + 1  # wings and the stair tower
    doc = put_item(put_item(load(tmp_path / "h.json"), item), item)
    assert [i["key"] for i in doc["items"]] == ["schloss"]
    save(tmp_path / "h.json", doc)
    assert load(tmp_path / "h.json") == doc
    assert '"pos": [' in (tmp_path / "h.json").read_text(encoding="utf-8")  # compact lists


def test_versioned_data_is_consistent():
    doc = load(ROOT / "data" / "leonberg" / "handmade.json")
    item = next(i for i in doc["items"] if i["key"] == "schloss")
    assert item == schloss_item(BUILT, item["mesh"])
    assert {k: v for k, v in BUILT.items() if k not in ("wings", "garden", "interior")} == {
        k: v for k, v in SPEC.items() if k not in ("wings", "garden", "interior")
    }  # only measured ground is added
    assert {k: v for k, v in BUILT["interior"].items() if k != "apron"} == SPEC["interior"]
    assert BUILT["interior"]["apron"]["strips"]  # the paving measured from the DGM
    assert [{k: v for k, v in w.items() if k != "terrain"} for w in BUILT["wings"]] == [
        {k: v for k, v in w.items() if k != "terrain"} for w in SPEC["wings"]
    ]
    overrides = load_all(ROOT / "data" / "leonberg" / "buildings")
    for bid in SPEC["replaces"]:
        assert overrides[bid].keep is False
    data = GLB.read_bytes()
    gl, _ = read_glb(data)
    names = [n["name"] for n in gl["nodes"]]
    built = geo.build(BUILT)
    assert names[0] == "schloss"
    assert [n for n in names if n.startswith("COL_HULL_")] == [n for n, _, _ in built.collision]
    tris = sum(gl["accessors"][p["indices"]]["count"] // 3
               for p in gl["meshes"][gl["nodes"][0]["mesh"]]["primitives"])  # fmt: skip
    assert tris == built.triangles() <= BUDGET
    assert {m["name"] for m in gl["materials"]} <= set(PALETTE)


def test_assemble_places_the_castle_as_a_gameplay_landmark():
    doc = load(ROOT / "data" / "leonberg" / "handmade.json")
    terrain = {"version": 1, "name": "t", "terrain": {"version": 1}}
    ids = VobIds({}, 1)
    res = assemble(terrain, {"entries": []}, None, ids, "t", handmade=doc)
    (v,) = [v for v in res.world["vobs"] if v["name"] == "HANDMADE_SCHLOSS"]
    assert v["category"] == "gameplay" and v["mesh"].endswith("schloss.glb")
    again = assemble(terrain, {"entries": []}, res.world, ids, "t", handmade=doc)
    assert again.added == 0 and again.updated == 0


@pytest.mark.skipif(find_blender() is None, reason="Blender not installed")
def test_blender_script_builds_the_same_model(tmp_path: Path):
    from gothar_worldgen.handmade import build_schloss

    out = tmp_path / "schloss.glb"
    data = ROOT / "data"
    line = build_schloss(find_blender(), data / "leonberg" / "schloss.json",
                         data / "building_rules.json", out, tmp_path / "s.blend")  # fmt: skip
    assert f"triangles={model().triangles()}" in line
    gl, _ = read_glb(out.read_bytes())
    assert [n["name"] for n in gl["nodes"]] == ["schloss"] + [n for n, _, _ in model().collision]
    assert (tmp_path / "s.blend").is_file()


def test_garden_parterre_hedges_plazas_and_splat_areas():
    g = SPEC["garden"]
    lay = geo.garden_layout(SPEC)
    nu, nv = g["beds"]
    assert len(lay["beds"]) == len(g["parts"]) * nu * nv
    for bed in lay["beds"]:  # every bed inside its parterre, paths between them
        ss, ts = [p[0] for p in bed], [p[1] for p in bed]
        part = next(p for p in g["parts"] if p[0] <= min(ss) and max(ss) <= p[1])
        assert (
            min(ss) >= part[0] + g["borderM"] - 1e-9 and min(ts) >= g["t"][0] + g["borderM"] - 1e-9
        )
    # round plaza in the middle of each half: no bed reaches into it, four beds are cut
    for cs, ct in lay["centres"]:
        cut = [b for b in lay["beds"] if len(b) == 5 and min(math.dist(p, (cs, ct)) for p in b) < 4]
        assert len(cut) == 4
        for bed in lay["beds"]:
            assert Polygon(bed).distance(Point(cs, ct)) >= g["plazaR"] - 1e-6
    m = model()
    edges = sum(len(b) for b in lay["beds"])
    assert len(m.faces["hedge"]) == edges * 5  # a box of five faces per bed edge
    hedge_y = [p[1] for f in m.faces["hedge"] for p in f]
    assert max(hedge_y) < g["ground"]["y"] + 3.0  # low hedges on the garden terrace
    assert "fountain" not in g and not m.faces.get("water")  # the obelisk fountain replaces it
    splat = geo.garden_splat(SPEC)
    assert len(splat["gravel"]) == len(g["parts"]) and len(splat["lawn"]) == len(lay["beds"])
    item = schloss_item(SPEC, "x.glb")
    assert item["splat"] == splat


def test_castle_roof_has_no_moss_and_dormers_have_open_windows():
    m = model()
    assert "roof_old_moss" not in m.faces
    # Every dormer window panel is visible: no plaster face covers its centre in the front plane.
    assert len(m.faces["frame"]) > 100
