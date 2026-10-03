"""Rivers and lakes as ``water`` vobs (world.md, M5 part E) and their beds in the heightmap (W2/W6).

The DGM has no river bed (laser points on water are interpolated over), so a box whose top is the
DGM would have no depth. ``export-terrain`` therefore carves a bed along each river and a basin in
each lake into the exported heightmap, which deliberately differs from the DGM there:

- River: the water level follows the smoothed DGM along the axis, never rising downstream, minus
  ``levelBelowM``. The bed lies ``depthM`` below the smoothed DGM over ``widthM``, then rises over
  ``bankM`` to the original ground. Boxes in sections of about ``segmentM`` (split at bends),
  overlapping by ``overlapM``, top = water level, bottom ``boxBelowBedM`` under the bed.
- Lake: the level is the median DGM inside the polygon minus ``levelBelowM``; the basin falls from
  the shore to ``depthM`` within ``shoreM``. One box per convex piece of the polygon.

Parameters: ``data/<site>/water.json`` (versioned). The heightmap never rises, only falls.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

import numpy as np
import numpy.typing as npt
import shapely
from PIL import Image, ImageDraw
from shapely.geometry import LineString, Polygon

from gothar_worldgen.buildings.collision import convex_pieces
from gothar_worldgen.export.terrain import Grid

INDEX_FORMAT = "gothar-water-index"
INDEX_VERSION = 1
BEND_DEG = 20.0
MAX_SAG_M = 0.75  # a section's axis may leave its chord by this much
MAX_DROP_M = 0.25  # the level may fall this much within a section (steps at the overlaps)


@dataclass
class WaterBox:
    key: str  # stable id part, e.g. glems_000
    name: str  # vob name
    center: tuple[float, float, float]
    yaw: float  # radians about +Y; the box's local +X runs along (cos, 0, -sin)
    half: tuple[float, float, float]

    @property
    def rot(self) -> list[float]:
        return [0.0, round(math.sin(self.yaw / 2), 6) + 0.0, 0.0, round(math.cos(self.yaw / 2), 6)]

    @property
    def top(self) -> float:
        return self.center[1] + self.half[1]


def _join(lines: list[list[tuple[float, float]]]) -> list[list[tuple[float, float]]]:
    """Chains lines whose ends meet (within 1 m) into longer polylines."""
    pool = [list(map(tuple, ln)) for ln in lines if len(ln) >= 2]
    out: list[list[tuple[float, float]]] = []
    while pool:
        cur = pool.pop(0)
        grown = True
        while grown:
            grown = False
            for i, ln in enumerate(pool):
                for a, b in (
                    (cur[-1], ln[0]),
                    (cur[-1], ln[-1]),
                    (cur[0], ln[-1]),
                    (cur[0], ln[0]),
                ):
                    if math.dist(a, b) <= 1.0:
                        if a is cur[-1] and b is ln[0]:
                            cur = cur + ln[1:]
                        elif a is cur[-1]:
                            cur = cur + ln[::-1][1:]
                        elif b is ln[-1]:
                            cur = ln + cur[1:]
                        else:
                            cur = ln[::-1] + cur[1:]
                        pool.pop(i)
                        grown = True
                        break
                if grown:
                    break
        out.append(cur)
    return out


def river_lines(features: list[dict[str, Any]], name: str) -> list[list[tuple[float, float]]]:
    """OSM lines of a named stream or river, without culverts, joined where they meet."""
    lines = [
        f["points"]
        for f in features
        if f.get("geometry") == "line"
        and f.get("kind") in ("stream", "river", "canal")
        and f.get("name") == name
        and not (f.get("tags") or {}).get("tunnel")
    ]
    return _join([[(float(x), float(z)) for x, z in ln] for ln in lines])


def _resample(line: list[tuple[float, float]], step: float) -> tuple[np.ndarray, np.ndarray]:
    geom = LineString(line)
    n = max(2, int(math.ceil(geom.length / step)) + 1)
    s = np.linspace(0.0, geom.length, n)
    pts = np.array([geom.interpolate(v).coords[0] for v in s])
    return s, pts


def river_level(
    grid: Grid, line: list[tuple[float, float]], spec: dict[str, Any]
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """(s, points, water level, bed) along the axis.

    The level follows the smoothed DGM minus ``levelBelowM`` but stays 0.1 m under the lowest
    ground of the cross-section (bed and banks), so no box rises above the ground beside the river;
    it never rises downstream. The bed lies ``depthM - levelBelowM`` under the level.
    """
    half, bank, depth = float(spec["widthM"]) / 2, float(spec["bankM"]), float(spec["depthM"])
    below = float(spec["levelBelowM"])
    s, pts = _resample(line, 1.0)
    h = np.array([grid.height_at(float(x), float(z)) for x, z in pts])
    k = max(1, int(round(float(spec.get("smoothM", 40.0)) / 2)))
    pad = np.pad(h, k, mode="edge")
    smooth = np.convolve(pad, np.ones(2 * k + 1) / (2 * k + 1), mode="valid")
    tangent = np.gradient(pts, axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-9)
    normal = np.stack([-tangent[:, 1], tangent[:, 0]], axis=1)
    reach = half + bank
    offsets = np.linspace(-reach, reach, 9)  # the whole cross-section, banks included
    banks = np.array([min(grid.height_at(*(p + n * o)) for o in offsets)
                      for p, n in zip(pts, normal, strict=True)])  # fmt: skip
    level = np.minimum(smooth - below, banks - 0.1)
    downstream = smooth[0] >= smooth[-1]
    level = np.minimum.accumulate(level) if downstream else np.minimum.accumulate(level[::-1])[::-1]
    return s, pts, level, level - (depth - below)


def _cells_near(grid: Grid, draw: Any, reach: float) -> np.ndarray:  # noqa: ANN401
    """(rows, cols) of grid cells covered by a raster drawing callback (with margin ``reach``)."""
    img = Image.new("L", (grid.width, grid.height), 0)
    draw(
        ImageDraw.Draw(img),
        lambda x, z: ((x - grid.first_x) / grid.cell + 0.5, (z - grid.first_z) / grid.cell + 0.5),
        max(1, int(math.ceil(2 * reach / grid.cell)) + 2),
    )
    return np.argwhere(np.asarray(img) > 0)


def carve_river(
    grid: Grid,
    heights: npt.NDArray[np.float64],
    line: list[tuple[float, float]],
    s: np.ndarray,
    pts: np.ndarray,
    bed: np.ndarray,
    spec: dict[str, Any],
) -> int:
    """Lowers ``heights`` along the river (bed + banks); returns the number of cells changed."""
    half, bank = float(spec["widthM"]) / 2, float(spec["bankM"])
    reach = half + bank

    def draw(d: Any, px: Any, width: int) -> None:  # noqa: ANN401
        d.line([px(x, z) for x, z in line], fill=255, width=width, joint="curve")

    cells = _cells_near(grid, draw, reach)
    if len(cells) == 0:
        return 0
    cx = grid.first_x + cells[:, 1] * grid.cell
    cz = grid.first_z + cells[:, 0] * grid.cell
    c = np.stack([cx, cz], axis=1)
    a, b = pts[:-1], pts[1:]
    seg = b - a
    seg_len2 = np.maximum((seg**2).sum(axis=1), 1e-12)
    best_d = np.full(len(c), np.inf)
    best_s = np.zeros(len(c))
    for k0 in range(0, len(a), 256):
        aa, ss, l2 = a[k0 : k0 + 256], seg[k0 : k0 + 256], seg_len2[k0 : k0 + 256]
        t = np.clip(((c[:, None, :] - aa[None]) * ss[None]).sum(axis=2) / l2[None], 0.0, 1.0)
        proj = aa[None] + t[..., None] * ss[None]
        d = np.linalg.norm(c[:, None, :] - proj, axis=2)
        j = d.argmin(axis=1)
        dj = d[np.arange(len(c)), j]
        better = dj < best_d
        best_d[better] = dj[better]
        best_s[better] = (s[k0 + j] + t[np.arange(len(c)), j] * np.sqrt(l2[j]))[better]
    inside = best_d < reach
    rows, cols = cells[inside, 0], cells[inside, 1]
    orig = heights[rows, cols]
    bottom = np.interp(best_s[inside], s, bed)
    f = np.clip((best_d[inside] - half) / bank, 0.0, 1.0)
    new = np.minimum(orig, bottom + (orig - bottom) * f)
    changed = int((new < orig - 1e-6).sum())
    heights[rows, cols] = new
    return changed


def river_boxes(
    name: str,
    s: np.ndarray,
    pts: np.ndarray,
    level: np.ndarray,
    bed: np.ndarray,
    spec: dict[str, Any],
    below_bed: float,
    overlap: float,
) -> list[WaterBox]:
    """Sections along the axis (≤ segmentM, split at bends), top = water level of the section."""
    half, bank, depth = float(spec["widthM"]) / 2, float(spec["bankM"]), float(spec["depthM"])
    below = float(spec["levelBelowM"])
    # Box edges where the carved bank has reached the water level again (the level sits at least
    # 0.1 m under the original ground, the bed depth - below under the level).
    surface_half = half + bank * min(1.0, (depth - below) / (depth - below + 0.1))
    seg_len = float(spec["segmentM"])
    cuts = [0]
    for i in range(1, len(s) - 1):
        d0, d1 = pts[i] - pts[cuts[-1]], pts[i + 1] - pts[i]
        bend = 0.0
        if np.linalg.norm(d0) > 1e-6 and np.linalg.norm(d1) > 1e-6:
            cosang = float(np.dot(d0, d1) / (np.linalg.norm(d0) * np.linalg.norm(d1)))
            bend = math.degrees(math.acos(max(-1.0, min(1.0, cosang))))
        chord = pts[i + 1] - pts[cuts[-1]]
        cl = float(np.linalg.norm(chord)) or 1.0
        rel = pts[cuts[-1] : i + 2] - pts[cuts[-1]]
        off = float(np.abs(chord[0] * rel[:, 1] - chord[1] * rel[:, 0]).max()) / cl
        if (s[i] - s[cuts[-1]] >= seg_len or (bend > BEND_DEG and s[i] - s[cuts[-1]] >= 3.0)
                or off > MAX_SAG_M or level[cuts[-1]] - level[i + 1] > MAX_DROP_M):  # fmt: skip
            cuts.append(i)
    if cuts[-1] != len(s) - 1:
        cuts.append(len(s) - 1)
    boxes = []
    key = name.lower().replace(" ", "_")
    for n, (i0, i1) in enumerate(zip(cuts, cuts[1:], strict=False)):
        p0, p1 = pts[i0], pts[i1]
        chord = p1 - p0
        length = float(np.linalg.norm(chord)) or 1.0
        mid = pts[i0 : i1 + 1].mean(axis=0)
        u, rel = chord / length, pts[i0 : i1 + 1] - p0
        sag = float(np.abs(u[0] * rel[:, 1] - u[1] * rel[:, 0]).max())  # curve off the chord
        top = float(level[i0 : i1 + 1].max())
        bottom = float(bed[i0 : i1 + 1].min()) - below_bed
        hy = (top - bottom) / 2
        yaw = math.atan2(-chord[1], chord[0])
        boxes.append(
            WaterBox(
                f"{key}_{n:03d}",
                f"WATER_{key.upper()}_{n:03d}",
                (round(float(mid[0]), 3), round(top - hy, 3), round(float(mid[1]), 3)),
                yaw,
                (
                    round(length / 2 + (overlap if 0 < n < len(cuts) - 2 else overlap / 2), 3),
                    round(hy, 3),
                    round(surface_half + sag, 3),
                ),
            )
        )
    return boxes


def lake(
    grid: Grid,
    heights: npt.NDArray[np.float64],
    name: str,
    polygon: list[list[float]],
    spec: dict[str, Any],
    below_bed: float,
) -> tuple[list[WaterBox], int]:
    """Basin in the heightmap and one box per convex piece of the lake polygon."""
    poly = Polygon(polygon).buffer(0)

    def draw(d: Any, px: Any, width: int) -> None:  # noqa: ANN401
        d.polygon([px(x, z) for x, z in poly.exterior.coords], fill=255)

    cells = _cells_near(grid, draw, 0.0)
    if len(cells) == 0:
        return [], 0
    cx = grid.first_x + cells[:, 1] * grid.cell
    cz = grid.first_z + cells[:, 0] * grid.cell
    pts = shapely.points(cx, cz)
    inside = shapely.contains(poly, pts)
    rows, cols = cells[inside, 0], cells[inside, 1]
    orig = heights[rows, cols]
    ground = float(np.median(orig))
    level = ground - float(spec["levelBelowM"])
    bottom = ground - float(spec["depthM"])
    d = shapely.distance(poly.exterior, pts[inside])
    f = np.clip(1.0 - d / float(spec["shoreM"]), 0.0, 1.0)
    new = np.minimum(orig, bottom + (np.maximum(orig, ground) - bottom) * f)
    changed = int((new < orig - 1e-6).sum())
    heights[rows, cols] = new
    boxes = []
    key = name.lower().replace(" ", "_")
    lo = bottom - below_bed
    hy = (level - lo) / 2
    for n, piece in enumerate(convex_pieces(poly)):
        rect = list(piece.minimum_rotated_rectangle.exterior.coords)[:4]
        e1 = np.subtract(rect[1], rect[0])
        e2 = np.subtract(rect[2], rect[1])
        c = np.mean(rect, axis=0)
        yaw = math.atan2(-e1[1], e1[0])
        boxes.append(
            WaterBox(
                f"{key}_{n:03d}",
                f"WATER_{key.upper()}_{n:03d}",
                (round(float(c[0]), 3), round(level - hy, 3), round(float(c[1]), 3)),
                yaw,
                (
                    round(float(np.linalg.norm(e1)) / 2, 3),
                    round(hy, 3),
                    round(float(np.linalg.norm(e2)) / 2, 3),
                ),
            )
        )
    return boxes, changed


def carve_and_place(
    grid: Grid, features: list[dict[str, Any]], doc: dict[str, Any]
) -> tuple[Grid, dict[str, Any]]:
    """Carved copy of ``grid`` and the water index (boxes plus statistics)."""
    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    below_bed, overlap = float(doc.get("boxBelowBedM", 0.3)), float(doc.get("overlapM", 0.5))
    boxes: list[WaterBox] = []
    stats: dict[str, Any] = {"rivers": {}, "lakes": {}}
    for spec in doc.get("rivers", []):
        name = spec["osmName"]
        total = 0
        for i, line in enumerate(river_lines(features, name)):
            s, pts, level, bed = river_level(grid, line, spec)
            total += carve_river(grid, heights, line, s, pts, bed, spec)
            label = name if i == 0 else f"{name} {i + 1}"
            boxes += river_boxes(label, s, pts, level, bed, spec, below_bed, overlap)
        stats["rivers"][name] = {
            "cellsLowered": total,
            "boxes": sum(b.name.startswith(f"WATER_{name.upper()}") for b in boxes),
        }
    for spec in doc.get("lakes", []):
        name = spec["osmName"]
        for f in features:
            if (
                f.get("geometry") == "polygon"
                and f.get("kind") == "water"
                and f.get("name") == name
            ):
                found, changed = lake(grid, heights, name, f["polygon"], spec, below_bed)
                boxes += found
                stats["lakes"][name] = {"cellsLowered": changed, "boxes": len(found)}
    carved = Grid(heights, grid.first_x, grid.first_z, grid.cell)
    entries = [
        {
            "id": b.key,
            "name": b.name,
            "pos": list(b.center),
            "rot": b.rot,
            "halfExtents": list(b.half),
        }
        for b in boxes
    ]
    return carved, {
        "format": INDEX_FORMAT,
        "version": INDEX_VERSION,
        "entries": entries,
        "stats": stats,
    }
