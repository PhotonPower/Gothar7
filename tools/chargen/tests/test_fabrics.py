"""Worn cloth textures (F3n): fabric data, UV raster, even thread density, wear."""

from __future__ import annotations

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.fabrics import (
    FabricError,
    Garment,
    apply_fray_materials,
    bake,
    distance_inside,
    fray_alpha,
    garment_of,
    hem_lines,
    load_fabrics,
    metres_per_uv,
    parse_fabrics,
    rasterize,
)
from gothar_chargen.gltf import Gltf

CHARACTERS = REPO_ROOT / "assets/source/characters"


def square(scale: float) -> Garment:
    """A 1 x 1 m square on the UV square [0.25, 0.75]^2 (two triangles)."""
    uv = np.array([[0.25, 0.25], [0.75, 0.25], [0.75, 0.75], [0.25, 0.75]])
    pos = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=float) * scale
    return Garment(uv, pos, np.array([[0, 1, 2], [0, 2, 3]]))


def test_committed_fabric_data_matches_the_parts():
    data = load_fabrics()
    assert data.targets
    textures = CHARACTERS / "textures" / "cloth"
    for target in data.targets:
        assert (textures / target.file).is_file(), target.file
        garment = garment_of(Gltf.load(CHARACTERS / target.part), target.material)
        assert 0.2 < metres_per_uv(garment) < 3.0
        assert rasterize(garment, 64).mean() > 0.1


def test_raster_and_scale():
    g = square(1.0)
    covered = rasterize(g, 64)
    assert covered[32, 32] and not covered[2, 2]
    assert covered.mean() == pytest.approx(0.25, abs=0.04)  # conservative at the edges
    assert metres_per_uv(g) == pytest.approx(2.0)  # 1 m over half the UV width
    assert metres_per_uv(square(2.0)) == pytest.approx(4.0)


def test_distance_inside():
    mask = np.zeros((20, 20), dtype=bool)
    mask[5:15, 5:15] = True
    d = distance_inside(mask, 8)
    assert d[5, 10] == 0 and d[7, 10] == 2 and d[0, 0] == 0


def test_bake_neutral_and_tint():
    tile = np.random.default_rng(1).random((32, 32))
    g = square(1.0)
    grey = bake(tile, 0.25, g, wear=0.0, seed=3, size=64)
    assert grey.shape == (64, 64, 3)
    assert np.allclose(grey[..., 0], grey[..., 1])  # neutral: grey, the palette colours it
    inside = rasterize(g, 64)
    assert grey[inside].mean() == pytest.approx(0.55, abs=0.05)
    red = bake(tile, 0.25, g, wear=0.0, seed=3, tint=(1.0, 0.0, 0.0), size=64)
    assert red[..., 1].max() == 0 and red[..., 0].mean() > 0.3


def test_wear_darkens_seams_and_is_deterministic():
    tile = np.full((16, 16), 0.5)
    g = square(1.0)
    inside = rasterize(g, 128)
    clean = bake(tile, 0.25, g, wear=0.0, seed=7, size=128)
    worn = bake(tile, 0.25, g, wear=1.0, seed=7, size=128)
    edge = inside & (distance_inside(inside, 3) < 2)
    assert worn[edge].mean() < clean[edge].mean() - 0.05  # dirty seams
    assert worn[inside].std() > clean[inside].std()  # stains
    assert np.array_equal(worn, bake(tile, 0.25, g, wear=1.0, seed=7, size=128))


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ({"version": 2}, "version"),
        ({"version": 1, "tiles": {"a": {"source": "x.jpg", "size": 5}}}, "size"),
        (
            {"version": 1, "tiles": {}, "textures": {"t.jpg": {"tile": "a"}}},
            "unknown tile",
        ),
        (
            {
                "version": 1,
                "tiles": {"a": {"source": "x.jpg", "size": 0.2}},
                "textures": {"t.jpg": {"tile": "a", "part": "p", "material": "m", "wear": 2}},
            },
            "wear",
        ),
        (
            {
                "version": 1,
                "tiles": {"a": {"source": "x.jpg", "size": 0.2}},
                "textures": {"t.jpg": {"tile": "a", "part": "p", "material": "m", "tint": "red"}},
            },
            "tint",
        ),
        (
            {
                "version": 1,
                "tiles": {"a": {"source": "x.jpg", "size": 0.2}},
                "textures": {"t.jpg": {"tile": "a", "part": "p", "material": "m", "fray": 0.5}},
            },
            "alpha channel",
        ),
        (
            {
                "version": 1,
                "tiles": {"a": {"source": "x.jpg", "size": 0.2}},
                "textures": {"t.png": {"tile": "a", "part": "p", "material": "m", "fray": 3}},
            },
            "fray",
        ),
    ],
)
def test_invalid_fabric_data(data, message):
    with pytest.raises(FabricError, match=message):
        parse_fabrics(data)


def two_squares() -> Garment:
    """Two 1 x 1 m squares side by side, joined in 3D along x = 1 but apart in UV (a seam)."""
    pos = np.array(
        [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [1, 0, 0], [2, 0, 0], [2, 1, 0], [1, 1, 0]],
        dtype=float,
    )
    uv = np.array(
        [[0.1, 0.1], [0.4, 0.1], [0.4, 0.4], [0.1, 0.4], [0.6, 0.1], [0.9, 0.1], [0.9, 0.4],
         [0.6, 0.4]]
    )  # fmt: skip
    tris = np.array([[0, 1, 2], [0, 2, 3], [4, 5, 6], [4, 6, 7]])
    return Garment(uv, pos, tris)


def test_hems_are_open_edges_not_uv_seams():
    hem = hem_lines(two_squares(), 100)
    assert hem[10, 25] and hem[25, 10]  # outer edges (v = 0.1 row, u = 0.1 column)
    assert not hem[25, 39] and not hem[25, 60]  # the shared edge is a UV seam, not a hem
    assert hem[25, 88:92].any()  # the far outer edge
    assert hem[10, 10:40].all()  # a hem is drawn without gaps


def test_fray_cuts_the_hems_only():
    g = two_squares()
    alpha = fray_alpha(g, 1.0, seed=3, size=200)
    assert alpha[50, 50] == 1.0 and alpha[50, 150] == 1.0  # inside the cloth
    assert alpha[20, 50] == 0.0  # on the hem
    band = alpha[21:48, 25:75]
    assert 0.05 < 1 - band.mean() < 0.6  # jagged: some texels cut, most kept
    assert np.array_equal(alpha, fray_alpha(g, 1.0, seed=3, size=200))
    assert (fray_alpha(g, 0.0, seed=3, size=200) == 1).all()


def test_apply_fray_materials(tmp_path):
    from gothar_chargen.items import glb_bytes

    doc = {
        "asset": {"version": "2.0"},
        "images": [{"uri": "../../textures/cloth/shirt_neutral.jpg"}, {"uri": "skin.jpg"}],
        "textures": [{"source": 0}, {"source": 1}],
        "materials": [
            {"name": "cloth_shirt", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
            {"name": "skin", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}}},
        ],
    }
    part = tmp_path / "parts" / "cloth_m" / "shirt.glb"
    part.parent.mkdir(parents=True)
    part.write_bytes(glb_bytes(doc, b""))
    target = load_fabrics().targets[0].__class__("shirt_neutral.png", "p", "m", "linen", 0.5)
    assert apply_fray_materials(tmp_path, target) == [part]
    g = Gltf.load(part)
    assert g.doc["images"][0]["uri"] == "../../textures/cloth/shirt_neutral.png"
    shirt, skin = g.doc["materials"]
    assert shirt["alphaMode"] == "MASK" and shirt["alphaCutoff"] == 0.5 and shirt["doubleSided"]
    assert "alphaMode" not in skin
    assert apply_fray_materials(tmp_path, target) == []  # nothing left to change
