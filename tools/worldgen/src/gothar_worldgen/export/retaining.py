"""Retaining walls where a street is cut into a slope (W6, ``streetworks.json`` ``walls``).

Along each street of the core the ground ``probeM`` beyond its edge is compared with the street's
axis every metre; where it lies more than ``crossFallM`` higher or lower for at least ``minRunM``
a wall stands at the edge (at least ``minAxisM`` from the axis: room to walk on a narrow path; not
on the inside of a tight bend, where the edge comes back to the axis), and the street's half on
that side is levelled to the axis' height across (the heightmap, each cell to the axis' height
beside it: no terraces on a climbing way; the other half keeps its cross slope; from the wall's
ends the levelling sets in over ``fadeM``: no lip where the cut ends, and nothing changes beyond
them, where a junction, a door or steps stopped the wall). On the high side
the wall holds the ground behind it (its top that ground's height), on the low side it carries the
street with a parapet of ``parapetM`` (nobody falls down). No wall near houses (``houseM``: their
walls hold the slope), doors (``doorM``) and the way from each door to every street within
``laneReachM`` (``laneM``),
other ways (``junctionM`` beyond their half width), the hand-made landmarks, squares or steps; a
wall that meets a house or the town wall (``mortarM``) is mortared like the socles, the others
are dry stone.

The plan goes to ``generated/retaining_walls.json``; ``streetworks`` builds the walls from it.
"""

from __future__ import annotations

import json
import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from shapely.geometry import LineString, Point, Polygon
from shapely.strtree import STRtree

from gothar_worldgen.export.terrain import Grid

PLAN_FORMAT = "gothar-retaining-walls"
PLAN_VERSION = 1


@dataclass
class Wall:
    key: str
    side: str  # "high": holds the ground behind, "low": carries the street (parapet)
    style: str  # "dry" or "mortared"
    points: list[tuple[float, float]] = field(default_factory=list)  # along the wall's face line
    base: list[float] = field(default_factory=list)
    top: list[float] = field(default_factory=list)
    outward: list[tuple[float, float]] = field(default_factory=list)  # from the street away

    def json(self) -> dict[str, Any]:
        r = 3
        return {"id": self.key, "side": self.side, "style": self.style,
                "points": [[round(x, r), round(z, r)] for x, z in self.points],
                "base": [round(y, r) for y in self.base], "top": [round(y, r) for y in self.top],
                "outward": [[round(x, 4), round(z, 4)] for x, z in self.outward]}  # fmt: skip


class _Near:
    def __init__(self, shapes: Sequence[Any]) -> None:
        self.shapes = [s for s in shapes if s is not None and not s.is_empty]
        self.tree = STRtree(self.shapes) if self.shapes else None

    def within(self, p: Point, dist: float) -> bool:
        if self.tree is None:
            return False
        return len(self.tree.query(p, predicate="dwithin", distance=dist)) > 0


def plan_walls(grid: Grid, streets: Sequence[dict[str, Any]], area: Polygon,
               houses: Sequence[Polygon], doors: Sequence[tuple[float, float]],
               keep_out: Sequence[Polygon], walls_of_town: Sequence[Polygon],
               spec: dict[str, Any]) -> tuple[Grid, list[Wall], dict[str, Any]]:  # fmt: skip
    """The walls and a copy of ``grid`` with the street levelled beside them."""
    kinds = set(spec["highways"])
    fall_m, probe, run_m = float(spec["crossFallM"]), float(spec["probeM"]), float(spec["minRunM"])
    offset, behind = float(spec["offsetM"]), float(spec["behindM"])
    near_house, near_door = _Near(houses), _Near([Point(d) for d in doors])
    near_keep, near_town = _Near(keep_out), _Near(walls_of_town)
    # every walkable way (junctions stay open) and the way from each door to its street
    from gothar_worldgen.qa.begehung import WALKABLE

    axes = []
    for k, s in enumerate(streets):
        pts = s.get("points") or []
        if (s.get("highway") in WALKABLE or s.get("highway") in kinds) and len(pts) >= 2:
            axes.append((k, LineString(pts), float(s.get("widthM") or 3.0)))
    axis_tree = STRtree([a for _, a, _ in axes]) if axes else None
    lanes = []
    reach = float(spec["laneReachM"])
    for d in doors:  # to the nearest way and to every other within reach: the waynet takes any
        q = Point(d)
        if axis_tree is None:
            break
        near = {int(axis_tree.nearest(q))}
        near |= {int(i) for i in axis_tree.query(q, predicate="dwithin", distance=reach)}
        for i in sorted(near):
            _, a, _ = axes[i]
            if a.distance(q) < float(spec["doorReachM"]):  # as far as the waynet ties doors
                lanes.append(LineString([d, a.interpolate(a.project(q)).coords[0]]))
    near_lane = _Near(lanes)
    junction = float(spec["junctionM"])

    def crosses_way(edge: Point, own: int) -> bool:
        if axis_tree is None:
            return False
        for i in axis_tree.query(edge, predicate="dwithin", distance=junction + 4.0):
            k, a, wd = axes[int(i)]
            if k != own and a.distance(edge) < wd / 2 + junction:
                return True
        return False

    heights = np.array(grid.heights, dtype=np.float64, copy=True)
    walls: list[Wall] = []
    levelled = 0
    for own, s in enumerate(streets):
        pts = s.get("points") or []
        if s.get("highway") not in kinds or len(pts) < 2:
            continue
        if s.get("tunnel") or s.get("bridge") or s.get("layer"):
            continue
        w = float(s.get("widthM") or 4.0)
        reach = max(w / 2 + offset, float(spec["minAxisM"]))  # axis to the wall's face
        whole = LineString(pts)
        line = whole.intersection(area)
        for part_no, part in enumerate(getattr(line, "geoms", [line])):
            if not isinstance(part, LineString) or part.length < run_m:
                continue
            n = int(part.length) + 1
            ts = np.linspace(0.0, part.length, n)
            samples = []
            for t in ts:
                p = part.interpolate(float(t))
                q = part.interpolate(float(min(t + 0.3, part.length)))
                r = part.interpolate(float(max(t - 0.3, 0.0)))
                dx, dz = q.x - r.x, q.y - r.y
                ln = math.hypot(dx, dz) or 1.0
                nx, nz = -dz / ln, dx / ln
                hc = grid.height_at(p.x, p.y)
                row = {}
                for side in (1.0, -1.0):
                    ex, ez = p.x + nx * side * reach, p.y + nz * side * reach
                    ox, oz = p.x + nx * side * (reach + probe), p.y + nz * side * (reach + probe)
                    fall = grid.height_at(ox, oz) - hc
                    edge = Point(ex, ez)
                    free = not (whole.distance(edge) < reach - 0.05  # the inside of a bend
                                or near_house.within(edge, float(spec["houseM"]))
                                or near_door.within(edge, float(spec["doorM"]))
                                or near_keep.within(edge, float(spec["keepM"]))
                                or near_lane.within(edge, float(spec["laneM"]))
                                or crosses_way(edge, own))  # fmt: skip
                    if free and abs(fall) > fall_m:
                        row[side] = (
                            "high" if fall > 0 else "low",
                            (ex, ez),
                            (nx * side, nz * side),
                        )
                samples.append((p, hc, row, (nx, nz)))
            for side in (1.0, -1.0):
                k = 0
                while k < len(samples):
                    kind = samples[k][2].get(side, (None,))[0]
                    if kind is None:
                        k += 1
                        continue
                    j = k
                    while j + 1 < len(samples) and samples[j + 1][2].get(side, (None,))[0] == kind:
                        j += 1
                    if (j - k) * (part.length / max(1, n - 1)) >= run_m:
                        key = f"wall_{s.get('osmId', 'x')}_{part_no}_{'l' if side > 0 else 'r'}_{k}"
                        wall = Wall(key.lower(), kind, "dry")
                        fade = float(spec["fadeM"]) / (part.length / max(1, n - 1)) + 1.0
                        for i, (p, hc, row, _) in enumerate(samples[k : j + 1]):
                            _, (ex, ez), (ox, oz) = row[side]
                            gb = grid.height_at(ex + ox * behind, ez + oz * behind)
                            if kind == "high":
                                base, top = hc - 0.3, max(gb + 0.1, hc + 0.4)
                            else:
                                base, top = min(gb, hc) - 0.3, hc + float(spec["parapetM"])
                            wall.points.append((ex, ez))
                            wall.base.append(base)
                            wall.top.append(min(top, hc + float(spec["maxM"])))
                            wall.outward.append((ox, oz))
                            weight = min(1.0, (i + 1) / fade, (j - k - i + 1) / fade)
                            levelled += _level(grid, heights, p, (ox, oz), reach, weight)
                        ends = (Point(wall.points[0]), Point(wall.points[-1]))
                        mortar = float(spec["mortarM"])
                        if any(near_house.within(e, mortar) or near_town.within(e, mortar)
                               for e in ends):  # fmt: skip
                            wall.style = "mortared"
                        walls.append(wall)
                    k = j + 1
    length = sum(LineString(w.points).length for w in walls if len(w.points) > 1)
    stats = {"walls": len(walls), "wallM": round(length, 1),
             "high": sum(1 for w in walls if w.side == "high"),
             "low": sum(1 for w in walls if w.side == "low"),
             "mortared": sum(1 for w in walls if w.style == "mortared"),
             "cellsLevelled": levelled}  # fmt: skip
    return Grid(heights, grid.first_x, grid.first_z, grid.cell), walls, stats


def _level(grid: Grid, heights: np.ndarray, p: Point, out: tuple[float, float],
           reach: float, weight: float = 1.0) -> int:  # fmt: skip
    """The cells from the axis at ``p`` out to the wall (``reach``, a metre along): each to the
    axis' height beside it (``grid`` unlevelled), so a climbing way keeps its even slope; with
    ``weight`` below one only that part of the way there (near a wall's end)."""
    count = 0
    steps = max(1, int(math.ceil(1.0 / grid.cell)))
    for k in range(0, int(math.ceil(reach / grid.cell)) + 1):
        d = min(k * grid.cell, reach)
        for j in range(-steps, steps + 1):
            along = 0.5 * j / steps
            ax, az = p.x - out[1] * along, p.y + out[0] * along
            x, z = ax + out[0] * d, az + out[1] * d
            c = int(round((x - grid.first_x) / grid.cell))
            r = int(round((z - grid.first_z) / grid.cell))
            # the axis beside the cell's centre, not beside the sample (steep ways: no sawtooth)
            cx, cz = grid.first_x + c * grid.cell, grid.first_z + r * grid.cell
            on = max(-1.0, min(1.0, -(cx - p.x) * out[1] + (cz - p.y) * out[0]))
            h = grid.height_at(p.x - out[1] * on, p.y + out[0] * on)
            if not (0 <= r < heights.shape[0] and 0 <= c < heights.shape[1]):
                continue
            was = float(grid.heights[r, c])  # the strongest levelling of a cell counts, once
            new = was + (h - was) * weight
            if abs(new - was) > abs(heights[r, c] - was) + 1e-3:
                heights[r, c] = new
                count += 1
    return count


def write_plan(path: Path, walls: Sequence[Wall], stats: dict[str, Any]) -> None:
    doc = {"format": PLAN_FORMAT, "version": PLAN_VERSION, "stats": stats,
           "walls": [w.json() for w in walls]}  # fmt: skip
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8", newline="\n")
