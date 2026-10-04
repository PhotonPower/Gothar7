from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.batch import generate
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.buildings.medieval import build_house, load_rules, lod2_primitives
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.facade.overrides import from_json

RULES_PATH = Path(__file__).resolve().parents[1] / "data" / "building_rules.json"
RING = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
HOUSE = {"id": "H1", "groundY": 0.0, "footprint": RING, "roof": ROOF}
GRID = Grid(np.zeros((101, 101)), -50.0, -50.0, 1.0)


def house(bid: str, x: float, z: float) -> dict:
    fp = [[x, z], [x + 6.0, z], [x + 6.0, z - 4], [x, z - 4]]
    roof = {"type": "saddle", "eaveY": 5.0, "ridgeY": 8.0, "ridgeDir": [1, 0]}
    return {"id": bid, "inCore": True, "footprint": fp, "groundY": 0.0, "roof": roof}


def rules(lod: dict | None = None):
    r = load_rules(RULES_PATH)
    r.data["textures"] = {}
    default = {"enabled": True, "suffixes": ["", "_lod1", "_lod2"]}
    r.data["lod"] = lod if lod is not None else default
    return r


def tris(prims) -> int:
    return sum(p.mesh.triangle_count for p in prims)


def test_lod1_is_simpler_and_never_has_more_timber_than_lod0():
    r = rules()
    style = from_json({"id": "H1", "style": "handwerkerhaus"})
    h0 = build_house(HOUSE, -0.3, (5.0, -3.5), r, None, style)
    h1 = build_house(HOUSE, -0.3, (5.0, -3.5), r, None, style, lod=1, lod0_level=h0.timber_level)
    assert tris(h1.primitives) <= 0.5 * tris(h0.primitives)
    assert h1.timber_level >= h0.timber_level and h1.dormers == 0
    late = build_house(HOUSE, -0.3, (5.0, -3.5), r, None, style, lod=1, lod0_level=4)
    assert not any(p.material.startswith("timber") for p in late.primitives)  # lod0 had none
    p2 = lod2_primitives(h0, -0.3, (5.0, -3.5), r)
    assert 0 < tris(p2) <= 0.1 * tris(h0.primitives)
    assert {p.material for p in p2} == {h0.style.infill, h0.style.roof}


@pytest.mark.parametrize("suffixes", [["_lod0", "_lod1", "_lod2"], ["", "_lod1", "_lod2"]])
def test_generated_files_name_the_levels(tmp_path: Path, suffixes):
    r = rules({"enabled": True, "suffixes": suffixes})
    res = generate([house("A", 0, 0)], GRID, tmp_path, "v", mode="medieval", rules=r)
    (entry,) = [e for e in res.index["entries"] if e["kind"] == "building"]
    assert len(entry["lodTriangles"]) == 2 and entry["lodTriangles"][0] < entry["triangles"]
    doc, _ = read_glb((tmp_path / Path(entry["mesh"]).name).read_bytes())
    names = [n["name"] for n in doc["nodes"]]
    stem = Path(entry["mesh"]).stem
    assert names[0] == stem + suffixes[0]
    assert names[-2:] == [stem + suffixes[1], stem + suffixes[2]]
    assert sum(n.startswith("COL_") for n in names) >= 1  # once, for all levels
    roots = [doc["nodes"][i]["name"] for i in doc["scenes"][0]["nodes"]]
    assert roots == names  # all at the root, the collision once
    for i in doc["scenes"][0]["nodes"][-2:]:
        assert (
            doc["nodes"][i]["translation"] == [0.0, 0.0, 0.0] and "children" not in doc["nodes"][i]
        )


def test_without_lod_the_files_stay_as_before(tmp_path: Path):
    res = generate([house("A", 0, 0)], GRID, tmp_path, "v", mode="medieval", rules=rules({}))
    (entry,) = [e for e in res.index["entries"] if e["kind"] == "building"]
    assert "lodTriangles" not in entry
    doc, _ = read_glb((tmp_path / Path(entry["mesh"]).name).read_bytes())
    assert not any("_lod" in n["name"] for n in doc["nodes"])


def test_preview_draws_a_level_as_lod0(tmp_path: Path):
    r = rules({"enabled": True, "suffixes": ["_lod0", "_lod1", "_lod2"], "preview": 2})
    res = generate([house("A", 0, 0)], GRID, tmp_path, "v", mode="medieval", rules=r)
    (entry,) = [e for e in res.index["entries"] if e["kind"] == "building"]
    doc, _ = read_glb((tmp_path / Path(entry["mesh"]).name).read_bytes())
    assert not any(n["name"].endswith(("_lod1", "_lod2")) for n in doc["nodes"])
