from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.geo.bbox import BBox, Tile
from gothar_worldgen.geo.dgm1 import (
    DgmError,
    build_height_grid,
    find_dgm1_files,
    read_xyz,
    sample_axis,
)

LGL_EXCERPT_DIR = Path(__file__).parent / "data" / "lgl_dgm1"


def plane(e: np.ndarray, n: np.ndarray) -> np.ndarray:
    """Synthetic terrain: rises 1 cm per metre east and 2 cm per metre north."""
    return 300.0 + 0.01 * (e - 1000) + 0.02 * (n - 2000)


def write_tile(
    dgm_dir: Path,
    e_km: int,
    n_km: int,
    window: BBox | None = None,
    year: int = 2019,
    step: int = 1,
) -> Path:
    """Write a synthetic DGM1 file for a 1 km tile, restricted to ``window`` to keep tests fast."""
    e0, n0 = e_km * 1000, n_km * 1000
    w = window or BBox(e0, n0, e0 + 1000, n0 + 1000)
    e_lo, e_hi = max(e0, int(w.min_e)), min(e0 + 1000, int(w.max_e) + 1)
    n_lo, n_hi = max(n0, int(w.min_n)), min(n0 + 1000, int(w.max_n) + 1)
    es = np.arange(e_lo, e_hi, step) + 0.5
    ns = np.arange(n_hi - 1, n_lo - 1, -step) + 0.5  # LGL files start in the north
    ee, nn = np.meshgrid(es, ns)
    pts = np.column_stack([ee.ravel(), nn.ravel(), plane(ee, nn).ravel()])
    path = dgm_dir / f"dgm1_32_{e_km}_{n_km}_1_bw_{year}.xyz"
    path.parent.mkdir(parents=True, exist_ok=True)
    np.savetxt(path, pts, fmt="%.2f")
    return path


@pytest.mark.parametrize(
    ("lo", "hi", "first", "count"),
    [
        (0.0, 10.0, 0.5, 10),
        (933.0, 1933.0, 933.5, 1000),
        (0.5, 3.5, 0.5, 3),
        (0.6, 3.5, 1.5, 2),
        (2.0, 2.4, 2.5, 0),
    ],
)
def test_sample_axis(lo: float, hi: float, first: float, count: int):
    assert sample_axis(lo, hi) == (first, count)


def test_find_files_by_tile_and_newest_year(tmp_path: Path):
    for name in [
        "a/dgm1_32_1_2_1_bw_2019.xyz",
        "b/dgm1_32_1_2_1_bw_2023.xyz",
        "dgm1_32_2_2_1_bw.xyz",
        "dgm1_32_2_2_1_bw_2019.csv",
        "notes.xyz",
    ]:
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text("0 0 0\n")
    found = find_dgm1_files(tmp_path)
    assert set(found) == {Tile(1000, 2000, 1000), Tile(2000, 2000, 1000)}
    assert found[Tile(1000, 2000, 1000)].year == 2023
    assert found[Tile(2000, 2000, 1000)].year is None


def test_read_xyz_rejects_garbage(tmp_path: Path):
    bad = tmp_path / "bad.xyz"
    bad.write_text("1 2\n")
    with pytest.raises(DgmError, match="not a valid XYZ"):
        read_xyz(bad)


def test_mosaic_across_tiles_is_north_up(tmp_path: Path):
    bbox = BBox(1990, 2100, 2010, 2105)  # crosses the border between both files
    for e_km in (1, 2):
        write_tile(tmp_path, e_km, 2, window=bbox)
    grid = build_height_grid(tmp_path, bbox)

    assert (grid.rows, grid.cols) == (5, 20)
    assert (grid.first_e, grid.first_n) == (1990.5, 2104.5)
    expected_first_row = plane(np.arange(1990.5, 2010.5), np.full(20, 2104.5))
    np.testing.assert_allclose(grid.heights[0], expected_first_row, atol=0.006)
    # Row 0 is north: heights decrease row by row because the plane rises to the north.
    assert np.all(np.diff(grid.heights[:, 0]) < 0)
    assert {f.tile.label for f in grid.files} == {"1_2", "2_2"}


def test_bilinear_sample_on_plane(tmp_path: Path):
    bbox = BBox(1100, 2100, 1110, 2110)
    write_tile(tmp_path, 1, 2, window=bbox)
    grid = build_height_grid(tmp_path, bbox)
    assert grid.sample(1105.0, 2105.0) == pytest.approx(plane(1105.0, 2105.0), abs=0.006)
    assert grid.sample(1100.5, 2109.5) == pytest.approx(float(grid.heights[0, 0]))
    with pytest.raises(DgmError, match="outside"):
        grid.sample(1100.0, 2105.0)


def test_missing_tile_is_reported(tmp_path: Path):
    write_tile(tmp_path, 1, 2, window=BBox(1990, 2100, 2000, 2105))
    with pytest.raises(DgmError, match="2_2"):
        build_height_grid(tmp_path, BBox(1990, 2100, 2010, 2105))


def test_holes_are_reported(tmp_path: Path):
    write_tile(tmp_path, 1, 2, window=BBox(1000, 2000, 1010, 2010), step=2)  # sparse
    with pytest.raises(DgmError, match="missing samples"):
        build_height_grid(tmp_path, BBox(1000, 2000, 1010, 2010))


def test_implausible_heights_are_reported(tmp_path: Path):
    path = tmp_path / "dgm1_32_0_0_1_bw.xyz"
    path.write_text("0.5 0.5 9999.0\n")
    with pytest.raises(DgmError, match="implausible"):
        build_height_grid(tmp_path, BBox(0, 0, 1, 1))


def test_real_lgl_excerpt():
    """40 m x 40 m of real LGL DGM1 around the Leonberg origin (tests/data/lgl_dgm1)."""
    bbox = BBox.around(500_933, 5_405_056, 20)
    grid = build_height_grid(LGL_EXCERPT_DIR, bbox)
    assert (grid.rows, grid.cols) == (40, 40)
    assert (grid.first_e, grid.first_n) == (500913.5, 5405075.5)
    assert grid.heights[0, 0] == pytest.approx(371.08)  # first line of the file
    assert 370.0 < grid.heights.min() <= grid.heights.max() < 374.0
    assert grid.sample(500_933, 5_405_056) == pytest.approx(371.59, abs=0.005)
