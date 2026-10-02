"""LGL DGM1 (XYZ ASCII, 1 m raster) -> height grid for a bounding box.

An LGL DGM1 file covers 1 km x 1 km and is named ``dgm1_32_<E km>_<N km>_1_bw_<year>.xyz``.
Each line holds ``easting northing height`` (EPSG:25832, heights NHN/DHHN2016) for the
*centre* of a 1 m cell, i.e. coordinates end in ``.50``.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt

from gothar_worldgen.geo.bbox import BBox, Tile, tiles_covering

CELL_SIZE_M = 1.0
FILE_TILE_SIZE_M = 1000
# Plausible terrain heights in Germany (m NHN); values outside indicate broken data.
PLAUSIBLE_HEIGHT_RANGE = (-10.0, 3000.0)

_NAME_RE = re.compile(r"^dgm1_32_(\d+)_(\d+)_1_bw(?:_(\d{4}))?\.xyz$", re.IGNORECASE)


class DgmError(Exception):
    """Missing or inconsistent DGM1 data; the message is meant for the user."""


@dataclass(frozen=True)
class DgmFile:
    path: Path
    tile: Tile
    year: int | None


@dataclass(frozen=True)
class HeightGrid:
    """Regular grid of cell-centre heights; row 0 is the northernmost row, column 0 the western.

    ``first_e``/``first_n`` are the source coordinates of sample (row 0, column 0).
    """

    heights: npt.NDArray[np.float32]
    first_e: float
    first_n: float
    cell_size: float
    files: tuple[DgmFile, ...]

    @property
    def rows(self) -> int:
        return int(self.heights.shape[0])

    @property
    def cols(self) -> int:
        return int(self.heights.shape[1])

    def sample(self, easting: float, northing: float) -> float:
        """Bilinearly interpolated height at a source coordinate inside the sample area."""
        fx = (easting - self.first_e) / self.cell_size
        fy = (self.first_n - northing) / self.cell_size
        if not (0.0 <= fx <= self.cols - 1 and 0.0 <= fy <= self.rows - 1):
            raise DgmError(f"point E {easting}, N {northing} lies outside the height grid")
        c0, r0 = max(min(int(fx), self.cols - 2), 0), max(min(int(fy), self.rows - 2), 0)
        c1, r1 = min(c0 + 1, self.cols - 1), min(r0 + 1, self.rows - 1)
        tx, ty = fx - c0, fy - r0
        h = self.heights
        top = float(h[r0, c0]) * (1 - tx) + float(h[r0, c1]) * tx
        bottom = float(h[r1, c0]) * (1 - tx) + float(h[r1, c1]) * tx
        return top * (1 - ty) + bottom * ty


def find_dgm1_files(dgm_dir: Path) -> dict[Tile, DgmFile]:
    """All DGM1 XYZ files below ``dgm_dir`` by 1 km tile; the newest year wins on duplicates."""
    found: dict[Tile, DgmFile] = {}
    for path in sorted(dgm_dir.rglob("*.xyz")):
        m = _NAME_RE.match(path.name)
        if not m:
            continue
        tile = Tile(int(m[1]) * 1000, int(m[2]) * 1000, FILE_TILE_SIZE_M)
        year = int(m[3]) if m[3] else None
        current = found.get(tile)
        if current is None or (year or 0) > (current.year or 0):
            found[tile] = DgmFile(path, tile, year)
    return found


def read_xyz(path: Path) -> npt.NDArray[np.float64]:
    """Read an XYZ file into an (n, 3) array."""
    values = np.fromfile(path, dtype=np.float64, sep=" ")
    if values.size == 0 or values.size % 3 != 0:
        raise DgmError(f"{path.name}: not a valid XYZ file ({values.size} numbers)")
    return values.reshape(-1, 3)


def sample_axis(lo: float, hi: float, cell: float = CELL_SIZE_M) -> tuple[float, int]:
    """First cell centre >= ``lo`` and the number of centres < ``hi``.

    Cell centres lie at ``k * cell + cell / 2``.
    """
    half = cell / 2
    first = math.ceil((lo - half) / cell) * cell + half
    count = math.ceil((hi - first) / cell)
    return first, max(count, 0)


def build_height_grid(dgm_dir: Path, bbox: BBox) -> HeightGrid:
    """Mosaic all DGM1 samples whose cell centres lie in ``bbox`` into one grid."""
    available = find_dgm1_files(dgm_dir)
    needed = tiles_covering(bbox, FILE_TILE_SIZE_M)
    missing = [t.label for t in needed if t not in available]
    if missing:
        raise DgmError(
            f"DGM1 tiles missing in {dgm_dir}: {', '.join(missing)} "
            "(run 'gothar-worldgen download <site> --only dgm1')"
        )

    first_e, cols = sample_axis(bbox.min_e, bbox.max_e)
    west_s, rows = sample_axis(bbox.min_n, bbox.max_n)
    first_n = west_s + (rows - 1) * CELL_SIZE_M  # row 0 = north
    heights = np.full((rows, cols), np.nan, dtype=np.float32)

    files = tuple(available[t] for t in needed)
    for f in files:
        pts = read_xyz(f.path)
        col = np.rint((pts[:, 0] - first_e) / CELL_SIZE_M)
        row = np.rint((first_n - pts[:, 1]) / CELL_SIZE_M)
        inside = (col >= 0) & (col < cols) & (row >= 0) & (row < rows)
        heights[row[inside].astype(np.intp), col[inside].astype(np.intp)] = pts[inside, 2]

    holes = int(np.count_nonzero(np.isnan(heights)))
    if holes:
        raise DgmError(f"DGM1 data has {holes} missing samples inside the area")
    lo, hi = float(heights.min()), float(heights.max())
    if lo < PLAUSIBLE_HEIGHT_RANGE[0] or hi > PLAUSIBLE_HEIGHT_RANGE[1]:
        raise DgmError(f"implausible DGM1 heights {lo:.2f} .. {hi:.2f} m NHN")
    return HeightGrid(heights, first_e, first_n, CELL_SIZE_M, files)
