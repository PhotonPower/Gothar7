"""Textures on hand-made models (W7, the castle): planar texture coordinates, kinds per style."""

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import (
    CollisionPart,
    MeshData,
    Primitive,
    glb_bytes_multi,
    read_glb,
)
from gothar_worldgen.textures.models import ModelStyle, planar_uvs, texture_model


def _quad(points, normal):
    pos = np.array(points, dtype=np.float32)
    nrm = np.tile(np.array(normal, dtype=np.float32), (4, 1))
    return MeshData(pos, nrm, np.zeros((4, 2), np.float32), np.array([0, 1, 2, 0, 2, 3], np.uint32))


def test_walls_run_along_and_up_flat_faces_along_the_axis():
    wall = _quad([(0, 0, 0), (2, 0, 0), (2, 3, 0), (0, 3, 0)], (0, 0, 1))  # faces south
    uv = planar_uvs(wall.positions, wall.normals)
    assert np.ptp(uv[:, 0]) == pytest.approx(2.0) and np.allclose(uv[:, 1], [0, 0, 3, 3])
    floor = _quad([(0, 0, 0), (0, 0, 2), (1, 0, 2), (1, 0, 0)], (0, 1, 0))
    a = (0.6, 0.8)
    uv = planar_uvs(floor.positions, floor.normals, a)
    p = floor.positions
    assert np.allclose(uv[:, 0], p[:, 0] * a[0] + p[:, 2] * a[1], atol=1e-6)  # along the wing
    assert np.allclose(uv[:, 1], -p[:, 0] * a[1] + p[:, 2] * a[0], atol=1e-6)


def _model() -> bytes:
    wall = _quad([(0, 0, 0), (2, 0, 0), (2, 3, 0), (0, 3, 0)], (0, 0, 1))
    floor = _quad([(0, 0, 0), (0, 0, 2), (2, 0, 2), (2, 0, 0)], (0, 1, 0))
    hedge = _quad([(0, 0, 3), (0, 0, 4), (1, 0, 4), (1, 0, 3)], (0, 1, 0))
    prims = [Primitive("stone", (0.45, 0.4, 0.33, 1.0), wall),
             Primitive("stone_slab", (0.2, 0.185, 0.155, 1.0), floor),
             Primitive("hedge", (0.045, 0.085, 0.03, 1.0), hedge)]  # fmt: skip
    col = CollisionPart("COL_HULL_0", wall.positions, np.array([0, 1, 2, 0, 2, 3], np.uint32))
    return glb_bytes_multi(prims, "schloss_x", [col])


def test_a_model_gets_the_images_of_its_kinds_and_keeps_its_collision():
    kinds = {"stone": "ashlar", "stone_slab": "ashlar", "hedge": None}
    out = texture_model(_model(), ModelStyle(kinds, key="schloss"), "../t")
    gl, _ = read_glb(out)
    assert [n["name"] for n in gl["nodes"]] == ["schloss_x", "COL_HULL_0"]
    mats = {m["name"]: m for m in gl["materials"]}
    assert set(mats) == {"stone", "stone_slab", "hedge"}
    assert "baseColorTexture" not in mats["hedge"]["pbrMetallicRoughness"]
    assert [i["uri"] for i in gl["images"]] == ["../t/ashlar_albedo.png", "../t/ashlar_normal.png"]
    # the palette stays the tint (the images store 1.0 lower)
    stone = mats["stone"]["pbrMetallicRoughness"]["baseColorFactor"]
    assert stone[0] == pytest.approx(0.45 / 0.75)
    assert texture_model(_model(), ModelStyle(kinds, key="schloss"), "../t") == out  # same bytes


def test_a_darker_style_renames_and_darkens_its_materials():
    style = ModelStyle({"stone": "stone", "stone_slab": "ashlar"}, 0.5, "_cellar")
    gl, _ = read_glb(texture_model(_model(), style, "../t"))
    mats = {m["name"]: m for m in gl["materials"]}
    assert set(mats) == {"stone_cellar", "stone_slab_cellar", "hedge_cellar"}
    hedge = mats["hedge_cellar"]["pbrMetallicRoughness"]["baseColorFactor"]
    assert hedge[:3] == pytest.approx([0.0225, 0.0425, 0.015])
