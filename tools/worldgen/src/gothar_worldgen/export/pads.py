"""Level ground under hand-made objects (W6, decision of the project owner 2026-10-03).

A pad is a polygon (world x, z) with a height: the heightmap inside it is set to that height,
raised or lowered, so a building or a garden terrace stands on level ground. Pads come from
``handmade.json`` (``pads`` of an item: the garden terraces of the castle, the ground under the
fountains and the church). An optional ``fadeM`` blends the edge into the terrain outside; without
it the edge is a step that a retaining wall of the model covers. With ``clampBelow`` the pad only
lowers: heights above ``y`` are cut down to it (the strips that put the heightmap's slope under a
terrace ledge); ``exact`` takes only the cells whose centres lie inside (without it a half-cell
margin rounds the polygon outwards). Like the water beds, the heightmap deliberately leaves the DGM
there.
"""

from __future__ import annotations

from typing import Any

import numpy as np
import shapely
from shapely.geometry import Polygon

from gothar_worldgen.export.terrain import Grid


def apply_pads(grid: Grid, pads: list[dict[str, Any]]) -> tuple[Grid, int]:
    """Copy of ``grid`` with every pad levelled; returns the number of cells changed."""
    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    changed = np.zeros(heights.shape, dtype=bool)
    for pad in pads:
        poly = Polygon(pad["polygon"]).buffer(0)
        fade = float(pad.get("fadeM", 0.0))
        exact = bool(pad.get("exact"))  # only cell centres inside, no half-cell margin
        margin = 0.0 if exact else grid.cell * 0.5
        reach = poly.buffer(fade + grid.cell) if fade > 0 else poly.buffer(max(margin, 1e-3))
        x0, z0, x1, z1 = reach.bounds
        c0 = max(0, int((x0 - grid.first_x) / grid.cell))
        c1 = min(grid.width - 1, int(np.ceil((x1 - grid.first_x) / grid.cell)))
        r0 = max(0, int((z0 - grid.first_z) / grid.cell))
        r1 = min(grid.height - 1, int(np.ceil((z1 - grid.first_z) / grid.cell)))
        rows, cols = np.mgrid[r0 : r1 + 1, c0 : c1 + 1]
        pts = shapely.points(
            grid.first_x + cols.ravel() * grid.cell, grid.first_z + rows.ravel() * grid.cell
        )
        y = float(pad["y"])
        if fade > 0:
            d = shapely.distance(poly, pts)  # 0 inside
            w = np.clip(1.0 - d / fade, 0.0, 1.0)
        else:
            w = shapely.contains(poly.buffer(margin) if margin else poly, pts).astype(float)
        keep = w > 0
        rr, cc = rows.ravel()[keep], cols.ravel()[keep]
        target = np.minimum(heights[rr, cc], y) if pad.get("clampBelow") else y
        heights[rr, cc] = heights[rr, cc] + (target - heights[rr, cc]) * w[keep]
        changed[rr, cc] = True
    return Grid(heights, grid.first_x, grid.first_z, grid.cell), int(changed.sum())
