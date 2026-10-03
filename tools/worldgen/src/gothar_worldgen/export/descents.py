"""Short descents dug in front of hillside doors (W3 decision E1-C B).

Where a house sits so deep in the slope that its door fits neither the ground storey nor an upper
storey (``buildings/medieval.py``), the generator writes a door of kind ``descent`` into the
buildings index (point in front of the door, floor height, outward normal). ``export-terrain``
lowers the ground there: level with the floor right at the wall, then rising at
``descentSlopeDeg`` away from it, ``descentWidthM`` wide, with side banks just as steep. Like the
water beds and the smoothed ways, the heightmap deliberately leaves the DGM there.
"""

from __future__ import annotations

import math
from typing import Any

import numpy as np
from shapely.geometry import Polygon

from gothar_worldgen.export.terrain import Grid

PROBE_M = 0.6  # the door point lies this far in front of the wall (medieval.DOOR_PROBE_M)
LANDING_M = 0.6  # level ground in front of the door before the ramp starts


def dig_descents(grid: Grid, entries: list[dict[str, Any]], hillside: dict[str, Any],
                 ways: Any = None) -> tuple[Grid, dict[str, int]]:  # noqa: ANN401  # fmt: skip
    """Copy of ``grid`` with the descents of all ``descent`` doors in ``entries``.

    ``ways`` (shapely geometry of the walkable ways): a descent that would cut into a way is left
    out, the way has priority (the door then stays partly in the ground).
    """
    slope = math.tan(math.radians(float(hillside.get("descentSlopeDeg", 40.0))))
    half = float(hillside.get("descentWidthM", 1.6)) / 2
    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    doors = cells = skipped = 0
    for e in entries:
        for d in e.get("doors", []):
            if len(d) < 6 or d[3] != "descent":
                continue
            x, z, floor, _, nx, nz = d[:6]
            wx, wz = x - nx * PROBE_M, z - nz * PROBE_M  # foot of the wall
            depth = grid.height_at(x, z) - floor
            if depth <= 0.05:
                continue
            length = LANDING_M + depth / slope + 0.5
            bank = depth / slope + grid.cell  # the sides slope up as steep as the ramp
            reach = math.hypot(length, half + bank) + grid.cell
            c0 = max(0, int((wx - reach - grid.first_x) / grid.cell))
            c1 = min(grid.width - 1, int(math.ceil((wx + reach - grid.first_x) / grid.cell)))
            r0 = max(0, int((wz - reach - grid.first_z) / grid.cell))
            r1 = min(grid.height - 1, int(math.ceil((wz + reach - grid.first_z) / grid.cell)))
            if ways is not None:
                corners = [(wx + nx * a - nz * b, wz + nz * a + nx * b)
                           for a, b in ((0, -half - bank), (length, -half - bank),
                                        (length, half + bank), (0, half + bank))]  # fmt: skip
                if Polygon(corners).intersects(ways):
                    skipped += 1
                    continue
            rows, cols = np.mgrid[r0 : r1 + 1, c0 : c1 + 1]
            dx = grid.first_x + cols * grid.cell - wx
            dz = grid.first_z + rows * grid.cell - wz
            along = dx * nx + dz * nz
            across = np.abs(-dx * nz + dz * nx)
            inside = (along >= -grid.cell) & (along <= length) & (across <= half + bank)
            target = floor + (np.maximum(along - LANDING_M, 0.0)
                              + np.maximum(across - half, 0.0)) * slope  # fmt: skip
            lower = inside & (target < heights[rows, cols])
            heights[rows[lower], cols[lower]] = target[lower]
            doors += 1
            cells += int(lower.sum())
    stats = {"doors": doors, "cells": cells, "skippedForWays": skipped}
    return Grid(heights, grid.first_x, grid.first_z, grid.cell), stats
