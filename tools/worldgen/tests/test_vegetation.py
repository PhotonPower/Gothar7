from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.mobs import BUDGET, write_mobs
from gothar_worldgen.vegetation import BUSHES, TREES, TUFTS, VEGETATION, crown_radius

REPO = Path(__file__).resolve().parents[3]
VEG_ASSETS = REPO / "assets" / "source" / "vegetation"


def _nodes(model) -> list[str]:
    doc, _ = read_glb(model.glb())
    return [n["name"] for n in doc["nodes"]]


def _points(mesh) -> np.ndarray:
    return np.vstack([b.mesh().positions for b in mesh.builders.values()])


@pytest.mark.parametrize("name", sorted(TREES))
def test_trees_have_detail_levels_and_collide_at_the_trunk(name):
    m = VEGETATION[name]()
    nodes = _nodes(m)
    # asset.md "Detailstufen": <name>, <name>_lod1, <name>_lod2, COL_ once
    assert {name, f"{name}_lod1", f"{name}_lod2"} <= set(nodes)
    assert [n for n in nodes if n.startswith("COL_")] == ["COL_HULL_TRUNK"]
    assert 0 < m.triangles() <= BUDGET
    tris = [m.triangles(), *(lod.triangles() for lod in m.lods)]
    assert tris == sorted(tris, reverse=True) and tris[-1] <= 40  # coarser and coarser
    trunk = m.main.collision[0].positions
    spec = TREES[name]
    assert np.abs(trunk[:, [0, 2]]).max() <= spec.trunk_r[0] + 1e-6  # only the trunk
    pts = _points(m.main)
    width = pts[:, 0].max() - pts[:, 0].min()
    assert width == pytest.approx(2 * crown_radius(name), rel=0.35)
    assert pts[:, 1].max() > spec.crown_y  # the crown is above the trunk


@pytest.mark.parametrize("name", sorted([*BUSHES, *TUFTS]))
def test_bushes_and_grass_do_not_collide(name):
    m = VEGETATION[name]()
    # a model without COL_ would collide with every triangle: a box a metre under the ground
    cols = m.main.collision
    assert len(cols) == 1 and cols[0].name == "COL_HULL_NONE"
    assert cols[0].positions[:, 1].max() <= -0.99
    assert 0 < m.triangles() <= BUDGET
    pts = _points(m.main)
    assert pts[:, 1].min() >= -0.01  # standing on the ground


def test_grass_blades_are_seen_from_both_sides():
    m = VEGETATION["grass_tuft"]()
    normals = np.vstack([b.mesh().normals for b in m.main.builders.values()])
    flat = normals[:, [0, 2]]
    # every blade twice: opposite horizontal normals
    assert len(normals) % 6 == 0
    assert np.allclose(flat[0::6] + flat[3::6], 0.0, atol=1e-5)


def test_versioned_vegetation_is_current(tmp_path: Path):
    write_mobs(tmp_path / "vegetation", tuple(VEGETATION), VEGETATION)
    for name in VEGETATION:
        made = (tmp_path / "vegetation" / f"{name}.glb").read_bytes()
        assert made == (VEG_ASSETS / f"{name}.glb").read_bytes(), name
    assert not (VEG_ASSETS / "textures").exists()  # the shared folder (furniture/textures)
