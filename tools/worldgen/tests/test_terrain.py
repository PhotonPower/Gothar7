import dataclasses
import json
import struct
import zlib
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.config import GameScale, load_site
from gothar_worldgen.geo.dgm1 import DgmFile, HeightGrid
from gothar_worldgen.geo.terrain import encode_heightmap, write_png16, write_terrain

from .conftest import SITE_TOML


def make_grid(heights: np.ndarray, first_e: float, first_n: float) -> HeightGrid:
    files = (DgmFile(Path("dgm1_32_500_5405_1_bw_2019.xyz"), None, 2019),)  # type: ignore[arg-type]
    return HeightGrid(heights.astype(np.float32), first_e, first_n, 1.0, files)


def read_png16(path: Path) -> np.ndarray:
    """Minimal decoder for the PNGs written by write_png16 (greyscale 16 bit, filter 0)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length : pos + 12 + length])
        assert crc == zlib.crc32(kind + body)
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert (depth, colour) == (16, 0)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    rows = np.frombuffer(zlib.decompress(idat), dtype=np.uint8).reshape(height, 1 + width * 2)
    assert np.all(rows[:, 0] == 0)
    return rows[:, 1:].copy().view(">u2").reshape(height, width).astype(np.uint16)


@pytest.fixture
def site(config_dir: Path):
    return load_site("testsite", config_dir)


@pytest.fixture
def grid(site) -> HeightGrid:
    # 4 x 3 samples around the origin (500933, 5405056); heights rise towards the east.
    heights = np.array([[370.0, 371.0, 372.0], [370.0, 371.0, 372.0]] * 2)
    return make_grid(heights, 500932.5, 5405057.5)


def test_encoding_round_trip(site, grid):
    hm = encode_heightmap(grid, site)
    # Origin lies between columns 0 and 1 -> 370.5 m NHN.
    assert hm.origin_nhn == pytest.approx(370.5)
    assert (hm.min_y, hm.max_y) == (-0.5, 1.5)
    assert hm.values[0].tolist() == [0, 32768, 65535]
    np.testing.assert_allclose(hm.decode(), grid.heights - 370.5, atol=hm.step_m / 2 + 1e-9)


def test_vertical_scale_is_applied(config_dir: Path, grid):
    (config_dir / "testsite.toml").write_text(
        SITE_TOML.replace("vertical   = 1.0", "vertical = 2.0"), encoding="utf-8"
    )
    hm = encode_heightmap(grid, load_site("testsite", config_dir))
    assert (hm.min_y, hm.max_y) == (-1.0, 3.0)


def test_flat_terrain_has_valid_range(site):
    hm = encode_heightmap(make_grid(np.full((3, 3), 380.0), 500932.0, 5405057.0), site)
    assert hm.max_y > hm.min_y
    assert np.all(hm.values == 0)


def test_png16_round_trip(tmp_path: Path):
    values = np.array([[0, 1, 256], [65535, 4660, 2]], dtype=np.uint16)
    write_png16(tmp_path / "t.png", values)
    np.testing.assert_array_equal(read_png16(tmp_path / "t.png"), values)


def test_write_terrain_files(tmp_path: Path, site, grid):
    meta = write_terrain(grid, site, tmp_path / "work")
    out = tmp_path / "work"

    r16 = np.fromfile(out / "terrain.r16", dtype="<u2").reshape(meta["height"], meta["width"])
    np.testing.assert_array_equal(r16, read_png16(out / "terrain.png"))
    assert r16[0].tolist() == [0, 32768, 65535]

    on_disk = json.loads((out / "terrain.json").read_text(encoding="utf-8"))
    assert on_disk == meta
    assert (meta["width"], meta["height"], meta["cellSize"]) == (3, 4, 1.0)
    # Sample (0,0) is the north-west one: E 500932.5 -> x -0.5, N 5405057.5 -> z -1.5.
    assert meta["firstSample"] == {"x": -0.5, "z": -1.5}
    assert meta["origin"]["heightNHN"] == 370.5
    assert meta["heightRange"]["minY"] == -0.5
    assert meta["areas"]["core"] == {"minX": -350.0, "minZ": -350.0, "maxX": 350.0, "maxZ": 350.0}
    assert meta["source"]["files"] == [{"name": "dgm1_32_500_5405_1_bw_2019.xyz", "year": 2019}]
    assert "LGL" in meta["source"]["credit"]


def test_horizontal_scale_moves_samples(site, grid, tmp_path: Path):
    scaled = dataclasses.replace(site, game_scale=GameScale(1.5, 1.0, 1.0))
    meta = write_terrain(grid, scaled, tmp_path)
    assert meta["cellSize"] == 1.5
    assert meta["firstSample"] == {"x": -0.75, "z": -2.25}
    assert meta["areas"]["surroundings"]["maxX"] == 1500.0
