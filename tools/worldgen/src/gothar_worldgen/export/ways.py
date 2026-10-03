"""Steep stretches of walkable ways smoothed in the heightmap (W3 decision E5).

The walkthrough (``leonberg-begehung.md``) found short stretches of OSM ways steeper than the
character can climb (50°), mostly OSM ``steps`` that the DGM shows as ramps. Until stair meshes
exist (W5), ``export-terrain`` limits the slope along each walkable way in the core to ``maxDeg``:
the height profile along the way is clamped from both ends (never steeper than ``maxDeg``
between neighbouring samples) and the cells within ``halfWidthM`` of the way move by the
profile's change, fading out over ``fadeM``. Like the water beds, the heightmap deliberately
leaves the DGM there; only stretches that need it change. Terrain steps of a metre or more
(retaining walls, terraces next to OSM steps) stay obstacles until stair meshes exist.
"""

from __future__ import annotations

import math
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Polygon

from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.qa.begehung import WALKABLE

STEP_M = 1.0
TOLERANCE_DEG = 0.5  # the heightmap is quantised (about 2 mm), so the limit is met within this
PROFILE_MARGIN_DEG = 3.0  # aim below the limit: the 1 m grid follows the profile only roughly


def limit_profile(h: np.ndarray, step: float, max_deg: float) -> np.ndarray:
    """Profile as close to ``h`` as possible with |dh| <= tan(max_deg) * step between samples.

    Clamping from both ends moves the high side down and the low side up symmetrically: first
    each sample is limited by its neighbours walking forward and backward (upper envelope from the
    low points, lower envelope from the high points), then the two envelopes are averaged.
    """
    k = math.tan(math.radians(max_deg)) * step
    lower = h.astype(float).copy()  # raised where a dip is too steep
    upper = h.astype(float).copy()  # lowered where a hump is too steep
    for _ in range(2):
        for i in range(1, len(h)):
            upper[i] = min(upper[i], upper[i - 1] + k)
            lower[i] = max(lower[i], lower[i - 1] - k)
        for i in range(len(h) - 2, -1, -1):
            upper[i] = min(upper[i], upper[i + 1] + k)
            lower[i] = max(lower[i], lower[i + 1] - k)
    mid = (upper + lower) / 2
    for _ in range(len(h)):  # the average may still step too much where both moved
        d = np.diff(mid)
        bad = np.abs(d) > k + 1e-9
        if not bad.any():
            break
        for i in np.nonzero(bad)[0]:
            excess = (abs(d[i]) - k) / 2 * np.sign(d[i])
            mid[i] += excess
            mid[i + 1] -= excess
    return mid


def _smooth_pass(
    grid: Grid, streets: list[dict[str, Any]], area: Polygon, spec: dict[str, Any]
) -> tuple[Grid, dict[str, Any]]:
    """Copy of ``grid`` with the steep stretches of the ways in ``area`` limited to ``maxDeg``."""
    max_deg = float(spec.get("maxDeg", 45.0))
    half, fade = float(spec.get("halfWidthM", 1.5)), float(spec.get("fadeM", 2.0))
    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    target = np.full(heights.shape, np.nan)
    weight = np.zeros(heights.shape)
    ways = 0
    worst_before = 0.0
    for w in streets:
        if w["highway"] not in WALKABLE or w.get("tunnel") or w.get("bridge") or w.get("layer"):
            continue
        line = LineString(w["points"]).intersection(area)
        for part in getattr(line, "geoms", [line]):
            if not isinstance(part, LineString) or part.length < 2 * STEP_M:
                continue
            n = int(part.length / STEP_M) + 1
            d = np.linspace(0.0, part.length, n)
            pts = np.array([part.interpolate(v).coords[0] for v in d])
            h = np.array([grid.height_at(float(x), float(z)) for x, z in pts])
            step = part.length / (n - 1)
            slope = np.degrees(np.arctan(np.abs(np.diff(h)) / step))
            if slope.max() <= max_deg + TOLERANCE_DEG:
                continue
            worst_before = max(worst_before, float(slope.max()))
            prof = limit_profile(h, step, max_deg - PROFILE_MARGIN_DEG)
            changed = np.abs(prof - h) > 1e-3
            if not changed.any():
                continue
            ways += 1
            # cells near the changed samples move by the profile's change at their nearest sample
            # (the cross slope stays; levelling across the way made junctions and switchbacks
            # worse in the walkthrough)
            reach = half + fade
            idx = np.nonzero(changed)[0]
            lo, hi = max(0, idx.min() - int(reach) - 1), min(n - 1, idx.max() + int(reach) + 1)
            seg_pts, seg_delta = pts[lo : hi + 1], (prof - h)[lo : hi + 1]
            x0, z0 = seg_pts.min(axis=0) - reach
            x1, z1 = seg_pts.max(axis=0) + reach
            c0 = max(0, int((x0 - grid.first_x) / grid.cell))
            c1 = min(grid.width - 1, int(math.ceil((x1 - grid.first_x) / grid.cell)))
            r0 = max(0, int((z0 - grid.first_z) / grid.cell))
            r1 = min(grid.height - 1, int(math.ceil((z1 - grid.first_z) / grid.cell)))
            rows, cols = np.mgrid[r0 : r1 + 1, c0 : c1 + 1]
            cx = grid.first_x + cols * grid.cell
            cz = grid.first_z + rows * grid.cell
            cells = np.stack([cx.ravel(), cz.ravel()], axis=1)
            # each cell moves by the profile change at its projection onto the way
            pts_c = shapely.points(cells[:, 0], cells[:, 1])
            along_c = shapely.line_locate_point(part, pts_c)
            dmin = shapely.distance(part, pts_c)
            inside = (along_c >= d[lo] - reach) & (along_c <= d[hi] + reach)
            wgt = np.where(inside, np.clip(1.0 - (dmin - half) / fade, 0.0, 1.0), 0.0)
            delta = np.interp(along_c, d[lo : hi + 1], seg_delta)
            keep = (wgt > 0) & (np.abs(delta) > 1e-3)
            rr, cc = rows.ravel()[keep], cols.ravel()[keep]
            want = heights[rr, cc] + delta[keep]
            old = weight[rr, cc]
            take = wgt[keep] > old  # where ways overlap, the strongest influence wins
            target[rr[take], cc[take]] = want[take]
            weight[rr[take], cc[take]] = wgt[keep][take]
    mask = weight > 0
    heights[mask] = heights[mask] + (target[mask] - heights[mask]) * weight[mask]
    smoothed = Grid(heights, grid.first_x, grid.first_z, grid.cell)
    stats = {
        "ways": ways,
        "cellsChanged": int(mask.sum()),
        "maxDegBefore": round(worst_before, 1),
        "maxDeg": max_deg,
    }
    return smoothed, stats


def smooth_ways(
    grid: Grid, streets: list[dict[str, Any]], area: Polygon, spec: dict[str, Any]
) -> tuple[Grid, dict[str, Any]]:
    """Copy of ``grid`` with the steep stretches of the ways in ``area`` limited to ``maxDeg``.

    Repeats the pass (up to ``passes``) because the fade at the edges and the bilinear terrain
    between the samples can leave short kinks; statistics are those of the first pass plus the
    total of changed cells.
    """
    first: dict[str, Any] | None = None
    changed = np.zeros(grid.heights.shape, dtype=bool)
    for _ in range(int(spec.get("passes", 8))):
        before = grid.heights
        grid, stats = _smooth_pass(grid, streets, area, spec)
        changed |= np.abs(grid.heights - before) > 1e-6
        first = first or stats
        if stats["ways"] == 0:
            break
    assert first is not None
    return grid, {**first, "cellsChanged": int(changed.sum()), "remaining": stats["ways"]}
