import json
import math
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.handmade import (
    build_marktbrunnen,
    find_blender,
    marktbrunnen_geometry,
    marktbrunnen_item,
)
from gothar_worldgen.qa.begehung import collision_parts

DATA = Path(__file__).parents[1] / "data"
SPEC_PATH = DATA / "leonberg" / "marktbrunnen.json"
SPEC = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
GLB = (
    Path(__file__).parents[3]
    / "assets/source/worlds/leonberg/handmade/marktbrunnen/marktbrunnen.glb"
)
geo = marktbrunnen_geometry()


def test_spouts_run_from_the_column_into_the_basin():
    s = SPEC["spouts"]
    parts = geo.spouts(SPEC)
    assert set(parts.faces) == {s["pipe"], s["jet"]}
    assert 2 <= len(s["directionsDeg"]) <= 4
    assert parts.triangles() < 800  # simple geometry
    jet = np.array([p for poly in parts.faces[s["jet"]] for p in poly])
    assert jet[:, 1].min() == pytest.approx(s["waterY"], abs=0.03)  # the jets end in the water
    r = np.hypot(jet[:, 0], jet[:, 2])
    assert r.max() <= s["reachR"] + 0.05
    assert r.max() < SPEC["collision"]["trough"]["inner"]  # inside the basin
    pipe = np.array([p for poly in parts.faces[s["pipe"]] for p in poly])
    angles = {
        round(math.degrees(math.atan2(z, x)) % 360) for x, _, z in pipe if math.hypot(x, z) > 0.5
    }
    for d in s["directionsDeg"]:
        assert any(abs((a - d + 180) % 360 - 180) < 6 for a in angles)


def test_collision_bodies_are_closed_outward_and_walkable():
    bodies = geo.collision(SPEC)
    assert [n for n, _, _ in bodies] == [f"COL_HULL_{i}" for i in range(len(bodies))]
    assert sum(len(t) for _, _, t in bodies) <= 210  # about the 200 per file of asset.md
    for _, pts, tris in bodies:
        p = np.array(pts)
        c = p.mean(axis=0)
        edges: dict[tuple[int, int], int] = {}
        for a, b, d in tris:
            n = np.cross(p[b] - p[a], p[d] - p[a])
            assert np.dot(n, (p[a] + p[b] + p[d]) / 3 - c) > 0
            for e in ((a, b), (b, d), (d, a)):
                edges[e] = edges.get(e, 0) + 1
        assert all(edges.get((b, a)) == 1 for a, b in edges)  # closed, consistently wound
    steps = SPEC["collision"]["steps"]
    assert all(s["y1"] - s["y0"] <= 0.4 for s in steps)  # the controller's step height
    assert steps[0]["y1"] == steps[1]["y0"]


def test_item_sits_at_the_origin_on_the_highest_ground_with_a_pad():
    item = marktbrunnen_item(SPEC, "worlds/leonberg/handmade/marktbrunnen/marktbrunnen.glb", -0.2)
    assert item["key"] == "marktbrunnen" and item["replaces"] == []
    assert item["pos"] == [0.0, pytest.approx(-0.2), 0.0]
    (pad,) = item["pads"]  # the square is levelled, the lower step sits sinkM in it
    assert pad["y"] == pytest.approx(-0.2 + SPEC["sinkM"]) and pad["fadeM"] == SPEC["padFadeM"]
    assert min(math.hypot(x, z) for x, z in pad["polygon"]) > 3.56
    (ring,) = item["footprints"]
    assert len(ring) == 8 and max(math.hypot(x, z) for x, z in ring) == pytest.approx(
        3.56, abs=0.01
    )


def test_versioned_model_and_handmade_entry():
    doc, binary = read_glb(GLB.read_bytes())
    materials = {m["name"] for m in doc["materials"]}
    assert {"stone", "water", "water_jet", "bronze_pipe"} <= materials
    assert not {"foliage", "granite", "weathered_stone"} & materials  # planting gone, palette used
    tris = sum(
        doc["accessors"][p["indices"]]["count"] // 3 for m in doc["meshes"] for p in m["primitives"]
    )
    cols = collision_parts(GLB.read_bytes())
    col_tris = sum(len(t) for _, _, t in cols)
    assert tris - col_tris <= SPEC["budgetTriangles"]
    assert len(cols) == len(geo.collision(SPEC))
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]
    assert {doc["nodes"][i]["name"] for i in roots} >= {n for n, _, _ in cols}  # COL_ at the root
    entry = next(
        i
        for i in json.loads((DATA / "leonberg/handmade.json").read_text("utf-8"))["items"]
        if i["key"] == "marktbrunnen"
    )
    assert entry["mesh"] == "worlds/leonberg/handmade/marktbrunnen/marktbrunnen.glb"
    assert entry["pos"][0] == 0.0 and entry["pos"][2] == 0.0
    assert (DATA / "leonberg" / SPEC["source"]).is_file()


@pytest.mark.skipif(find_blender() is None, reason="Blender not installed")
def test_blender_script_builds_the_same_model(tmp_path: Path):
    out = tmp_path / "marktbrunnen.glb"
    line = build_marktbrunnen(find_blender(), SPEC_PATH, DATA / "building_rules.json", out, None)
    assert line.startswith("MARKTBRUNNEN_OK")
    built = int(line.split("triangles=")[1].split()[0])
    assert built <= SPEC["budgetTriangles"]
    doc, _ = read_glb(GLB.read_bytes())
    new, _ = read_glb(out.read_bytes())
    assert _indices(new) == _indices(doc)


def _indices(doc: dict) -> int:
    return sum(
        doc["accessors"][p["indices"]]["count"] for m in doc["meshes"] for p in m["primitives"]
    )
