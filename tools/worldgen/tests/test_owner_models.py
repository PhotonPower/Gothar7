"""The project owner's garden models: specs, collision, placement (W6)."""

import json
import math
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.handmade import find_blender
from gothar_worldgen.owner_models import (
    box_body,
    collision,
    garden_placements,
    kept_meshes,
    load_spec,
    prepare_job,
    prism_body,
    run_blender,
)
from gothar_worldgen.qa.begehung import collision_parts

DATA = Path(__file__).parents[1] / "data"
ASSETS = Path(__file__).parents[3] / "assets" / "source" / "worlds" / "leonberg"
SCHLOSS = json.loads((DATA / "leonberg" / "schloss.json").read_text(encoding="utf-8"))
KEYS = {
    "obeliskbrunnen": "obeliskbrunnen",
    "brunnen_garten": "gartenbrunnen",
    "garten_gelaender": "garten_gelaender",
}
# the four corner pavilions of the real garden (LoD2 DEBW_001000623WX/WY/WZ/X0, centres)
LOD2_PAVILIONS = [(-254.1, 1.7), (-245.0, -11.2), (-196.8, 42.1), (-187.7, 29.2)]


def spec(key: str) -> dict:
    return load_spec(DATA / "leonberg" / f"{key}.json")


def closed_outward(pts: np.ndarray, tris) -> bool:  # noqa: ANN001
    c = pts.mean(axis=0)
    edges: dict[tuple[int, int], int] = {}
    for a, b, d in tris:
        if (
            np.dot(np.cross(pts[b] - pts[a], pts[d] - pts[a]), (pts[a] + pts[b] + pts[d]) / 3 - c)
            < 0
        ):
            return False
        for e in ((a, b), (b, d), (d, a)):
            edges[e] = edges.get(e, 0) + 1
    return all(edges.get((b, a)) == 1 for a, b in edges)


def test_bodies_are_closed_and_outward():
    rng = np.random.default_rng(1)
    pts = rng.uniform(-1, 1, (50, 3)) * [3.0, 1.0, 0.2]
    for p, t in (box_body(pts), prism_body(pts, 8)):
        assert closed_outward(p, t)
        assert p[:, 1].min() == pytest.approx(pts[:, 1].min())


def test_specs_sources_and_collision_rules():
    for key in KEYS:
        s = spec(key)
        assert s["key"] == key and s["status"].startswith("festgelegt")
        meshes = kept_meshes(s, DATA / "leonberg" / s["source"])
        cols = collision(s, meshes)
        assert cols and all(closed_outward(np.asarray(p), t) for _, p, t in cols)
    fence = spec("garten_gelaender")
    cols = collision(fence, kept_meshes(fence, DATA / "leonberg" / fence["source"]))
    assert len(cols) == 16 + 4  # a box per fence field (gates stay open) and per pavilion
    assert sum(len(t) for _, _, t in cols) <= 260


def test_shear_lays_the_fence_on_the_slope_but_keeps_pavilions_level():
    fence = spec("garten_gelaender")
    meshes = kept_meshes(fence, DATA / "leonberg" / fence["source"])
    flat = collision(fence, meshes)
    slope = collision(fence, meshes, (0.0, 0.1))
    # a fence field along z rises with z, a pavilion box keeps its height
    dz = [float(np.ptp(s[1][:, 1]) - np.ptp(f[1][:, 1])) for f, s in zip(flat, slope, strict=True)]
    assert max(dz[:16]) > 0.6  # the short sides (7 m on each side of the gate) rise 0.7 m
    assert all(abs(d) < 1e-6 for d in dz[16:])


def test_garden_placements_follow_the_real_garden():
    radii = {"obeliskbrunnen": 3.55, "brunnen_garten": 1.88}
    ps = {p.key: p for p in garden_placements(SCHLOSS, radii)}
    assert set(ps) == {"garten_gelaender", "obeliskbrunnen", "gartenbrunnen_w", "gartenbrunnen_o"}
    centre = np.mean(LOD2_PAVILIONS, axis=0)
    assert math.dist(ps["garten_gelaender"].pos[::2], centre) < 0.5
    assert len({round(p.yaw, 6) for p in ps.values()}) == 1  # all along the parterre axes
    fence, obelisk = ps["garten_gelaender"], ps["obeliskbrunnen"]
    assert fence.pos[::2] == obelisk.pos[::2] and obelisk.pos[1] < fence.pos[1]  # lowest ground
    w, o = ps["gartenbrunnen_w"].pos, ps["gartenbrunnen_o"].pos
    assert math.dist(w[::2], o[::2]) == pytest.approx(38.0, abs=0.01)  # the two half centres
    assert math.dist(((w[0] + o[0]) / 2, (w[2] + o[2]) / 2), fence.pos[::2]) < 0.01
    assert fence.shear != (0.0, 0.0) and obelisk.shear == (0.0, 0.0)


def test_versioned_models_and_handmade_entries():
    doc = json.loads((DATA / "leonberg" / "handmade.json").read_text(encoding="utf-8"))
    items = {i["key"]: i for i in doc["items"]}
    for key, name in KEYS.items():
        data = (ASSETS / "handmade" / name / f"{name}.glb").read_bytes()
        gl, _ = read_glb(data)
        cols = collision_parts(data)
        tris = sum(
            gl["accessors"][p["indices"]]["count"] // 3
            for m in gl["meshes"]
            for p in m["primitives"]
        ) - sum(len(t) for _, _, t in cols)
        assert tris <= spec(key)["budgetTriangles"] * 1.05
        assert len(cols) == len(spec(key)["collision"]) or key == "garten_gelaender"
    assert items["gartenbrunnen_w"]["mesh"] == items["gartenbrunnen_o"]["mesh"]
    assert set(items["garten_gelaender"]["replaces"]) == set(spec("garten_gelaender")["replaces"])
    for bid in spec("garten_gelaender")["replaces"]:
        override = json.loads((DATA / "leonberg" / "buildings" / f"{bid}.json").read_text("utf-8"))
        assert override["keep"] is False


@pytest.mark.skipif(find_blender() is None, reason="Blender not installed")
def test_blender_builds_the_same_obelisk_fountain(tmp_path: Path):
    import subprocess

    s = spec("obeliskbrunnen")
    rules = json.loads((DATA / "building_rules.json").read_text(encoding="utf-8"))
    job = prepare_job(
        s, DATA / "leonberg" / "obeliskbrunnen.json", rules["palette"], tmp_path / "o.glb", None
    )
    line = run_blender(find_blender(), job, tmp_path / "job.json", subprocess.run)
    assert line.startswith("OWNER_OK key=obeliskbrunnen")
    new, _ = read_glb((tmp_path / "o.glb").read_bytes())
    old, _ = read_glb((ASSETS / "handmade" / "obeliskbrunnen" / "obeliskbrunnen.glb").read_bytes())
    assert _indices(new) == _indices(old)


def _indices(doc: dict) -> int:
    return sum(
        doc["accessors"][p["indices"]]["count"] for m in doc["meshes"] for p in m["primitives"]
    )


def test_church_fits_the_lod2_footprint_and_budget():
    from gothar_worldgen.owner_models import fitted_placement

    s = spec("kirche")
    meshes = kept_meshes(s, DATA / "leonberg" / s["source"])
    cols = collision(s, meshes)
    assert len(cols) == 17 and sum(len(t) for _, _, t in cols) <= 230
    lod2 = {"id": "DEBW_00100061Zjs", "groundMinY": -2.12, "footprint": [[0, 0], [1, 0], [1, 1]]}
    p = fitted_placement(s, [lod2])
    assert p.pos[1] == pytest.approx(-2.17) and p.key == "kirche"
    # the tower stands at the west end, where the LoD2 tower block is (centre -160.0 / -50.2)
    tw = np.vstack([m.positions for m in meshes if m.path[-1] == "tower_shaft"])
    c, sn = math.cos(p.yaw), math.sin(p.yaw)
    x, z = tw[:, 0].mean(), tw[:, 2].mean()
    world = (p.pos[0] + x * c + z * sn, p.pos[2] - x * sn + z * c)
    assert math.dist(world, (-160.0, -50.2)) < 1.0
    data = (ASSETS / "handmade" / "kirche" / "kirche.glb").read_bytes()
    gl, _ = read_glb(data)
    tris = sum(
        gl["accessors"][q["indices"]]["count"] // 3 for m in gl["meshes"] for q in m["primitives"]
    )
    assert tris - sum(len(t) for _, _, t in collision_parts(data)) <= s["budgetTriangles"]
    override = json.loads((DATA / "leonberg/buildings/DEBW_00100061Zjs.json").read_text("utf-8"))
    assert override["keep"] is False
