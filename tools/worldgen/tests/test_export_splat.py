from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from gothar_worldgen.export.splat import (
    ACKER,
    FELS,
    KIES,
    KOPFSTEIN,
    LAYERS,
    MATSCH,
    WALDBODEN,
    WIESE,
    SplatPaths,
    composite,
    encode_maps,
    layer_masks,
    placeholder_albedo,
    write_splat,
)
from gothar_worldgen.export.terrain import Grid, check_terrain_block, terrain_block

# 100 m x 60 m at 1 m, centred on the origin; core = the western half.
GRID = Grid(np.zeros((61, 101)), -50.0, -30.0, 1.0)
CORE = {"minX": -50.0, "minZ": -30.0, "maxX": 0.0, "maxZ": 30.0}


def px(x: float, z: float) -> tuple[int, int]:
    """(row, col) of the sample at local (x, z)."""
    return round(z - GRID.first_z), round(x - GRID.first_x)


@pytest.fixture(scope="module")
def weights() -> np.ndarray:
    streets = [
        {"highway": "residential", "widthM": 4.0, "points": [[-45, -20], [-5, -20]]},  # core
        {"highway": "track", "widthM": 3.0, "points": [[5, -20], [45, -20]]},  # outside
        {"highway": "motorway", "widthM": 20.0, "points": [[-50, 25], [50, 25]]},  # skipped
    ]
    squares = [{"polygon": [[-40, 0], [-30, 0], [-30, 10], [-40, 10]]}]
    buildings = [{"footprint": [[-20, 0], [-10, 0], [-10, -8], [-20, -8]]}]
    features = [
        {
            "kind": "forest",
            "geometry": "polygon",
            "polygon": [[10, 0], [30, 0], [30, 15], [10, 15]],
        },
        {
            "kind": "farmland",
            "geometry": "polygon",
            "polygon": [[32, 0], [42, 0], [42, 15], [32, 15]],
        },
        {"kind": "tree", "geometry": "point", "position": [5, 10]},
        {"kind": "stream", "geometry": "line", "points": [[-50, -10], [-30, -10]]},
    ]
    heights = np.zeros((61, 101))
    heights[:, 95:] = np.arange(6) * 2.0  # 63° slope at the eastern edge (x 45..50)
    grid = Grid(heights, GRID.first_x, GRID.first_z, GRID.cell)
    masks = layer_masks(grid, buildings, streets, squares, features, CORE)
    return composite(masks, heights.shape)


def dominant(w: np.ndarray, x: float, z: float) -> int:
    r, c = px(x, z)
    return int(np.argmax(w[:, r, c]))


@pytest.mark.parametrize(
    ("x", "z", "layer"),
    [
        (-25, -20, KOPFSTEIN),  # street in the old town
        (25, -20, KIES),  # track outside
        (-35, 5, KOPFSTEIN),  # square in the old town
        (-15, -4, MATSCH),  # under a building
        (-15, 1, MATSCH),  # building margin
        (20, 8, WALDBODEN),
        (5, 10, WALDBODEN),  # single tree
        (37, 8, ACKER),
        (-40, -10, MATSCH),  # stream bank
        (48, 0, FELS),
        (0, 25, WIESE),  # the motorway leaves no trace
        (-45, 20, WIESE),
    ],
)
def test_layers_land_where_expected(weights: np.ndarray, x: float, z: float, layer: int):
    assert dominant(weights, x, z) == layer


def test_weights_sum_to_one_and_blend(weights: np.ndarray):
    np.testing.assert_allclose(weights.sum(axis=0), 1.0, atol=1e-5)
    assert weights.min() >= 0
    r, c = px(-25, -18)  # street edge: blended, not hard
    assert 0.05 < weights[KOPFSTEIN, r, c] < 0.95


def test_encode_maps_channel_layout(weights: np.ndarray):
    maps = encode_maps(weights)
    assert len(maps) == 2 and maps[0].shape == (61, 101, 4)
    r, c = px(48, 0)
    assert maps[1][r, c, FELS - 4] > 200  # layer 6 = map 1, channel 2
    assert (maps[1][..., 3] == 0).all()  # unused 8th channel
    total = sum(m.astype(int).sum(axis=-1) for m in maps)
    assert np.abs(total - 255).max() <= len(LAYERS) // 2 + 1  # rounding only


def test_write_splat(tmp_path: Path, weights: np.ndarray):
    paths = SplatPaths(
        tmp_path / "gen", "worlds/x/generated", tmp_path / "layers", "worlds/x/layers", "x"
    )
    block = write_splat(weights, paths)
    assert block["maps"] == ["worlds/x/generated/x_splat0.png", "worlds/x/generated/x_splat1.png"]
    assert [layer["name"] for layer in block["layers"]][:2] == ["Wiese", "Kopfstein"]
    data = (tmp_path / "gen" / "x_splat0.png").read_bytes()
    assert b"gAMA" not in data and b"iCCP" not in data and b"sRGB" not in data  # linear data
    with Image.open(tmp_path / "gen" / "x_splat0.png") as img:
        assert (img.mode, img.size) == ("RGBA", (101, 61))  # pixel = sample
    sizes = set()
    for layer in LAYERS:
        with Image.open(tmp_path / "layers" / f"{layer.key}.png") as img:
            sizes.add(img.size)
    assert sizes == {(128, 128)}  # engine: all albedos equally sized
    full = {**terrain_block(GRID, "worlds/x/generated/x.r16", -1.0, 1.0), "splat": block}
    assert check_terrain_block(full) == []
    # Deterministic: a second run writes identical files.
    before = {p.name: p.read_bytes() for p in (tmp_path / "layers").iterdir()}
    write_splat(weights, paths)
    assert before == {p.name: p.read_bytes() for p in (tmp_path / "layers").iterdir()}


def test_placeholder_albedos_are_small_and_tileable():
    for layer in LAYERS:
        img = placeholder_albedo(layer)
        assert img.shape == (128, 128, 3)
        # Tileable: the jump across the wrap is not larger than inside the texture.
        inner = np.abs(np.diff(img.astype(int), axis=1)).mean()
        wrap = np.abs(img[:, 0].astype(int) - img[:, -1].astype(int)).mean()
        assert wrap <= 3 * inner + 2


@pytest.mark.parametrize(
    ("splat", "problem"),
    [
        ({"maps": [], "layers": []}, "1 to 8"),
        ({"maps": ["a.png"], "layers": [{"name": "a"}]}, "name and albedo"),
        ({"maps": ["a.png"], "layers": [{"name": "a", "albedo": "a.png", "tile": 0}]}, "tile"),
        (
            {"maps": ["a.png"], "layers": [{"name": str(i), "albedo": "a.png"} for i in range(5)]},
            "ceil",
        ),
        ("x", "object"),
    ],
)
def test_check_splat_block(splat, problem):
    block = {**terrain_block(GRID, "a.r16", -1.0, 1.0), "splat": splat}
    assert any(problem in p for p in check_terrain_block(block))


def test_formal_garden_gravel_paths_lawn_beds_and_no_field():
    garden = {"gravel": [[[0, -20], [40, -20], [40, 0], [0, 0]]],
              "lawn": [[[4, -16], [18, -16], [18, -4], [4, -4]]]}  # fmt: skip
    field = {
        "kind": "garden",
        "geometry": "polygon",
        "polygon": [[-5, -25], [45, -25], [45, 5], [-5, 5]],
    }
    masks = layer_masks(GRID, [], [], [], [field], CORE, [garden])
    w = composite(masks, (GRID.height, GRID.width))
    path, bed = px(30, -10), px(11, -10)
    assert w[KIES][path] > 0.9 and w[WIESE][bed] > 0.9
    assert w[ACKER].max() < 1e-6  # the garden feature holding the parterre is lawn, not a field
    plain = layer_masks(GRID, [], [], [], [field], CORE)
    assert plain[ACKER][px(11, -10)] > 0.9  # without the parterre it stays a field
