"""Ground of the left-out town edge evened out (W6 rural ways, plan approved 2026-11-08).

The DGM still shows what modern Leonberg did to the ground outside the wall: pits where buildings
stood (cellars, garages, terraces cut into the slope) and the cuts and embankments of the modern
streets. Where the buildings are left out (``buildings/weiler.py``) and the streets narrowed or
dropped (``geo/rural.py``), ``export-terrain`` fills that ground harmonically: inside the fill area
the heights solve Laplace's equation with the surrounding terrain as the boundary, a smooth
membrane spanned over the hole (a pit fills up, a dam settles, a cut closes up to the slopes on
both sides). Kept buildings, the town wall and bridges stay as the DGM has them.

The solver works on the grid's bounding box of the area: red-black over-relaxation, started from
the same fill on a grid of half the resolution (so wide areas converge quickly).
"""

from __future__ import annotations

from typing import Any

import numpy as np
import numpy.typing as npt
import shapely
from shapely.geometry import Polygon

from gothar_worldgen.export.terrain import Grid

OMEGA = 1.9  # over-relaxation
TOLERANCE_M = 0.001
MAX_ITERATIONS = 4000
COARSE_MIN_CELLS = 16  # below this extent no coarser start


def rasterise(grid: Grid, area: shapely.Geometry) -> npt.NDArray[np.bool_]:
    """Cells whose centres lie in ``area`` (the outermost ring of cells never: the boundary)."""
    mask = np.zeros((grid.height, grid.width), dtype=bool)
    if area.is_empty:
        return mask
    polys = [area] if isinstance(area, Polygon) else list(getattr(area, "geoms", []))
    for poly in polys:
        if not isinstance(poly, Polygon) or poly.is_empty:
            continue
        x0, z0, x1, z1 = poly.bounds
        c0 = max(1, int(np.floor((x0 - grid.first_x) / grid.cell)))
        c1 = min(grid.width - 2, int(np.ceil((x1 - grid.first_x) / grid.cell)))
        r0 = max(1, int(np.floor((z0 - grid.first_z) / grid.cell)))
        r1 = min(grid.height - 2, int(np.ceil((z1 - grid.first_z) / grid.cell)))
        if c1 < c0 or r1 < r0:
            continue
        rows, cols = np.mgrid[r0 : r1 + 1, c0 : c1 + 1]
        inside = shapely.contains_xy(
            poly, grid.first_x + cols * grid.cell, grid.first_z + rows * grid.cell
        )
        mask[r0 : r1 + 1, c0 : c1 + 1] |= inside
    return mask


def _relax(h: npt.NDArray[np.float64], mask: npt.NDArray[np.bool_]) -> int:
    """Red-black SOR on the masked cells of ``h`` (in place); returns the iterations."""
    rows, cols = np.indices(h.shape)
    colours = [mask & ((rows + cols) % 2 == k) for k in (0, 1)]
    for it in range(1, MAX_ITERATIONS + 1):
        worst = 0.0
        for colour in colours:
            avg = np.zeros_like(h)
            avg[1:-1, 1:-1] = (h[:-2, 1:-1] + h[2:, 1:-1] + h[1:-1, :-2] + h[1:-1, 2:]) / 4
            step = (avg - h)[colour]
            h[colour] += OMEGA * step
            if step.size:
                worst = max(worst, float(np.abs(step).max()))
        if worst < TOLERANCE_M:
            return it
    return MAX_ITERATIONS


def laplace_fill(h: npt.NDArray[np.float64], mask: npt.NDArray[np.bool_]) -> int:
    """Masked cells of ``h`` (in place) as the harmonic fill of the rest; the mask must not touch
    the array's border. Returns the iterations of the finest level."""
    if not mask.any():
        return 0
    if min(h.shape) >= 2 * COARSE_MIN_CELLS:
        rr, cc = (h.shape[0] // 2) * 2, (h.shape[1] // 2) * 2
        known = np.where(mask, 0.0, h)[:rr, :cc]
        n = (~mask[:rr, :cc]).reshape(rr // 2, 2, cc // 2, 2).sum(axis=(1, 3))
        coarse = known.reshape(rr // 2, 2, cc // 2, 2).sum(axis=(1, 3)) / np.maximum(n, 1)
        cmask = n < 4  # a block with a masked cell is unknown at the coarse level
        cmask[0, :] = cmask[-1, :] = cmask[:, 0] = cmask[:, -1] = False
        laplace_fill(coarse, cmask)
        start = np.repeat(np.repeat(coarse, 2, axis=0), 2, axis=1)
        sub = mask[:rr, :cc]
        h[:rr, :cc][sub] = start[sub]
    return _relax(h, mask)


def harmonise(
    grid: Grid, fill: shapely.Geometry, keep: shapely.Geometry
) -> tuple[Grid, dict[str, Any]]:
    """Copy of ``grid`` with ``fill`` minus ``keep`` filled harmonically, and stats."""
    area = fill.difference(keep) if not keep.is_empty else fill
    mask = rasterise(grid, area)
    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    stats: dict[str, Any] = {"cells": int(mask.sum()), "iterations": 0, "raisedM": 0.0,
                             "loweredM": 0.0}  # fmt: skip
    if not mask.any():
        return Grid(heights, grid.first_x, grid.first_z, grid.cell), stats
    rows, cols = np.nonzero(mask)
    r0, r1 = max(0, rows.min() - 1), min(grid.height, rows.max() + 2)
    c0, c1 = max(0, cols.min() - 1), min(grid.width, cols.max() + 2)
    sub = heights[r0:r1, c0:c1].copy()
    before = sub.copy()
    stats["iterations"] = laplace_fill(sub, mask[r0:r1, c0:c1])
    heights[r0:r1, c0:c1] = sub
    delta = (sub - before)[mask[r0:r1, c0:c1]]
    stats["raisedM"] = round(float(max(delta.max(), 0.0)), 2)
    stats["loweredM"] = round(float(max(-delta.min(), 0.0)), 2)
    stats["movedOver1M"] = int((np.abs(delta) > 1.0).sum())
    return Grid(heights, grid.first_x, grid.first_z, grid.cell), stats


def rural_areas(
    raw_streets: list[dict[str, Any]],
    ring: list[tuple[float, float]],
    dropped: list[list[list[float]]],
    kept: list[list[list[float]]],
    rails: list[list[list[float]]],
    spec: dict[str, Any],
) -> tuple[shapely.Geometry, shapely.Geometry]:
    """(fill, keep) for ``harmonise`` from ``rural_ways.json`` (``spec`` = the whole file).

    Fill: the footprints of the left-out buildings (``dropped``) plus ``buildingM``; outside the
    ring the corridor of every OSM street (half its width plus ``streetMarginM`` or
    ``streetMarginShare`` of the width, the larger) and of the railways (``railWidthM``). Keep:
    the town within ``wallM`` of the ring, the kept buildings (``kept``) plus ``keepM``, bridges
    and tunnels."""
    from shapely.geometry import LineString
    from shapely.ops import unary_union

    h = spec.get("harmonise", {})
    town = Polygon(ring).buffer(0)
    inner = town.buffer(float(spec.get("insideM", 5.0)))
    margin, share = float(h.get("streetMarginM", 3.0)), float(h.get("streetMarginShare", 0.5))
    fills, keeps = [], [town.buffer(float(h.get("wallM", 30.0)))]
    for fp in dropped:
        if len(fp) >= 3:
            fills.append(Polygon(fp).buffer(0).buffer(float(h.get("buildingM", 3.0))))
    for fp in kept:
        if len(fp) >= 3:
            keeps.append(Polygon(fp).buffer(0).buffer(float(h.get("keepM", 5.0))))
    for s in raw_streets:
        pts = s.get("points") or []
        if len(pts) < 2:
            continue
        w = float(s.get("widthM") or 3.0)
        corridor = LineString(pts).buffer(w / 2 + max(margin, share * w))
        if s.get("bridge") or s.get("tunnel") or s.get("layer"):
            keeps.append(corridor)
        else:
            fills.append(corridor.difference(inner))
    rail_half = float(h.get("railWidthM", 8.0)) / 2
    for pts in rails:
        if len(pts) >= 2:
            fills.append(LineString(pts).buffer(rail_half + margin).difference(inner))
    return unary_union(fills), unary_union(keeps)
