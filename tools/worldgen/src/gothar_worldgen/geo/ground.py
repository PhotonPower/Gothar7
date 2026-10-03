"""Terrain height range under building footprints (DGM steps along terraced houses, W3)."""

from __future__ import annotations

import math
from collections.abc import Sequence
from typing import TYPE_CHECKING, Any

import numpy as np
import shapely
from shapely.geometry import Polygon

if TYPE_CHECKING:
    from gothar_worldgen.export.terrain import Grid


def ground_range(
    grid: Grid | None, footprint: Sequence[Sequence[float]]
) -> tuple[float, float] | None:
    """Lowest and highest heightmap sample under (and on) the footprint."""
    if grid is None or len(footprint) < 3:
        return None
    ring = np.asarray(footprint, dtype=np.float64)
    poly = Polygon(ring)
    minx, minz = ring.min(axis=0)
    maxx, maxz = ring.max(axis=0)
    c0 = max(0, math.floor((minx - grid.first_x) / grid.cell))
    c1 = min(grid.width - 1, math.ceil((maxx - grid.first_x) / grid.cell))
    r0 = max(0, math.floor((minz - grid.first_z) / grid.cell))
    r1 = min(grid.height - 1, math.ceil((maxz - grid.first_z) / grid.cell))
    if c1 < c0 or r1 < r0:
        return None
    cols, rows = np.meshgrid(np.arange(c0, c1 + 1), np.arange(r0, r1 + 1))
    xs = grid.first_x + cols * grid.cell
    zs = grid.first_z + rows * grid.cell
    inside = shapely.contains_xy(poly, xs, zs)
    values = list(grid.heights[rows[inside], cols[inside]])
    # Vertices too (small footprints may contain no sample at all), nearest sample.
    vc = np.clip(np.rint((ring[:, 0] - grid.first_x) / grid.cell).astype(int), 0, grid.width - 1)
    vr = np.clip(np.rint((ring[:, 1] - grid.first_z) / grid.cell).astype(int), 0, grid.height - 1)
    values += list(grid.heights[vr, vc])
    return float(min(values)), float(max(values))


def add_ground_ranges(entries: Sequence[dict[str, Any]], grid: Grid) -> int:
    """Adds ``groundMinY``/``groundMaxY`` after ``groundY`` (buildings and parts); returns count."""
    count = 0
    for entry in [*entries, *(p for e in entries for p in e.get("parts") or [])]:
        rng = ground_range(grid, entry.get("footprint") or [])
        if rng is None:
            continue
        items = list(entry.items())
        entry.clear()
        for key, value in items:
            if key in ("groundMinY", "groundMaxY"):
                continue
            entry[key] = value
            if key == "groundY":
                entry["groundMinY"] = round(rng[0], 2) + 0.0
                entry["groundMaxY"] = round(rng[1], 2) + 0.0
        if "groundMinY" not in entry:
            entry["groundMinY"] = round(rng[0], 2) + 0.0
            entry["groundMaxY"] = round(rng[1], 2) + 0.0
        count += 1
    return count
