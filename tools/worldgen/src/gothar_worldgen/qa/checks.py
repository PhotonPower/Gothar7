"""Plausibility checks over the intermediate data -> ``report.json``.

Each check yields ``ok``, ``warn`` (worth a look, data still usable) or ``fail`` (data is
inconsistent and should not be used). Limits are deliberately generous; they catch broken
inputs and mismatched coordinate systems, not small mapping errors.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Callable
from dataclasses import asdict, dataclass
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Polygon

from gothar_worldgen.qa.workdata import WorkData

OK, WARN, FAIL = "ok", "warn", "fail"
_RANK = {OK: 0, WARN: 1, FAIL: 2}

MAX_STEP_M = 0.01
GROUND_MEDIAN_OK_M, GROUND_MEDIAN_WARN_M = 0.5, 2.0
GROUND_OUTLIER_M, GROUND_OUTLIER_SHARE = 2.0, 0.05
HEIGHT_RANGE_M = (0.5, 120.0)
WIDTH_RANGE_M = (0.5, 40.0)
SHARE_WARN = 0.01  # buildings with warnings / implausible heights
ROADS_IN_BUILDINGS_OK, ROADS_IN_BUILDINGS_WARN = 0.03, 0.10
AREA_TOLERANCE_M = 0.01


@dataclass
class CheckResult:
    id: str
    status: str
    value: Any
    message: str


def _graded(value: float, ok: float, warn: float) -> str:
    return OK if value <= ok else WARN if value <= warn else FAIL


def check_terrain(d: WorkData) -> list[CheckResult]:
    hr = d.terrain["heightRange"]
    step = hr["stepM"]
    inside = hr["minY"] <= 0.0 <= hr["maxY"]
    return [
        CheckResult(
            "terrain.resolution",
            OK if step <= MAX_STEP_M else WARN,
            step,
            f"height step {step * 1000:.2f} mm (limit {MAX_STEP_M * 1000:.0f} mm)",
        ),
        CheckResult(
            "terrain.originInRange",
            OK if inside else FAIL,
            [hr["minY"], hr["maxY"]],
            "origin height lies inside the terrain height range"
            if inside
            else "origin height lies outside the terrain (wrong origin or reference height)",
        ),
    ]


ORIGIN_KEYS = ("crs", "easting", "northing", "heightNHN")


def check_origins(d: WorkData) -> list[CheckResult]:
    def key(o: dict[str, Any]) -> tuple[Any, ...]:
        return tuple(o.get(k) for k in ORIGIN_KEYS)

    ref = key(d.origins["terrain.json"])
    different = [name for name, o in d.origins.items() if key(o) != ref]
    return [
        CheckResult(
            "layers.sameOrigin",
            FAIL if different else OK,
            different,
            f"origin differs from terrain.json in: {', '.join(different)} (re-run import)"
            if different
            else "all layers use the same origin and reference height",
        )
    ]


def _footprint_min_terrain(d: WorkData, footprint: list[list[float]]) -> float:
    pts = np.asarray(footprint, dtype=np.float64)
    samples = d.sample_heights(pts[:, 0], pts[:, 1])
    return float(np.nanmin(samples)) if np.isfinite(samples).any() else float("nan")


def check_buildings(d: WorkData) -> list[CheckResult]:
    b = d.buildings
    in_core = sum(1 for e in b if e.get("inCore"))
    results = [
        CheckResult(
            "buildings.count",
            FAIL if not b else WARN if not in_core else OK,
            {"total": len(b), "inCore": in_core},
            f"{len(b)} buildings, {in_core} in the core area",
        )
    ]
    if not b:
        return results

    diffs = np.array(
        [e["groundY"] - _footprint_min_terrain(d, e["footprint"]) for e in b if "groundY" in e]
    )
    diffs = diffs[np.isfinite(diffs)]
    median = float(np.median(np.abs(diffs))) if diffs.size else float("nan")
    outliers = float(np.mean(np.abs(diffs) > GROUND_OUTLIER_M)) if diffs.size else 1.0
    results += [
        CheckResult(
            "buildings.groundVsTerrain",
            _graded(median, GROUND_MEDIAN_OK_M, GROUND_MEDIAN_WARN_M) if diffs.size else FAIL,
            round(median, 3),
            f"median |ground - terrain| {median:.2f} m over {diffs.size} buildings "
            f"(ok <= {GROUND_MEDIAN_OK_M} m)",
        ),
        CheckResult(
            "buildings.groundOutliers",
            OK if outliers <= GROUND_OUTLIER_SHARE else WARN,
            round(outliers, 4),
            f"{outliers:.1%} of buildings deviate > {GROUND_OUTLIER_M:.0f} m from the terrain "
            f"(typically on slopes; ok <= {GROUND_OUTLIER_SHARE:.0%})",
        ),
    ]

    lo, hi = HEIGHT_RANGE_M
    odd = [e["id"] for e in b if not lo <= e.get("heightM", 0.0) <= hi]
    share = len(odd) / len(b)
    results.append(
        CheckResult(
            "buildings.heights",
            OK if share <= SHARE_WARN else WARN,
            {"count": len(odd), "examples": odd[:5]},
            f"{len(odd)} buildings with height outside {lo} .. {hi} m",
        )
    )
    warned = [e["id"] for e in b if e.get("warnings")]
    share = len(warned) / len(b)
    results.append(
        CheckResult(
            "buildings.warnings",
            OK if share <= SHARE_WARN else WARN,
            {"count": len(warned), "examples": warned[:5]},
            f"{len(warned)} buildings carry conversion warnings",
        )
    )
    return results


def _area_rect(d: WorkData) -> tuple[float, float, float, float]:
    a = d.terrain["areas"]["surroundings"]
    t = AREA_TOLERANCE_M
    return a["minX"] - t, a["minZ"] - t, a["maxX"] + t, a["maxZ"] + t


def _all_points(records: list[dict[str, Any]]) -> np.ndarray:
    pts: list[list[float]] = []
    for r in records:
        for key in ("points", "polygon"):
            pts.extend(r.get(key, []))
        if "position" in r:
            pts.append(r["position"])
    return np.asarray(pts, dtype=np.float64).reshape(-1, 2)


def check_streets_and_features(d: WorkData) -> list[CheckResult]:
    min_x, min_z, max_x, max_z = _area_rect(d)
    pts = _all_points(d.streets + d.squares + d.features)
    outside = int(
        np.count_nonzero(
            (pts[:, 0] < min_x) | (pts[:, 0] > max_x) | (pts[:, 1] < min_z) | (pts[:, 1] > max_z)
        )
    )
    lo, hi = WIDTH_RANGE_M
    odd_widths = [s["osmId"] for s in d.streets if not lo <= s["widthM"] <= hi]
    return [
        CheckResult(
            "streets.count",
            OK if d.streets else FAIL,
            {"streets": len(d.streets), "squares": len(d.squares)},
            f"{len(d.streets)} street lines, {len(d.squares)} squares",
        ),
        CheckResult(
            "osm.withinArea",
            OK if outside == 0 else FAIL,
            outside,
            f"{outside} OSM points outside the area (clipping)",
        ),
        CheckResult(
            "streets.widths",
            OK if not odd_widths else WARN,
            {"count": len(odd_widths), "examples": odd_widths[:5]},
            f"{len(odd_widths)} streets with width outside {lo} .. {hi} m",
        ),
        CheckResult(
            "features.count",
            OK,
            dict(Counter(f["type"] for f in d.features).most_common()),
            f"{len(d.features)} features",
        ),
    ]


def roads_in_buildings_share(d: WorkData) -> float:
    """Share of road centre-line length that runs through building footprints (LGL vs OSM)."""
    roads = [
        LineString(s["points"])
        for s in d.streets
        if s["class"] == "road" and not s.get("tunnel") and s.get("layer", 0) >= 0
    ]
    total = sum(r.length for r in roads)
    if total == 0 or not d.buildings:
        return 0.0
    footprints = shapely.union_all(
        [shapely.make_valid(Polygon(b["footprint"])) for b in d.buildings]
    )
    inside = sum(r.intersection(footprints).length for r in roads)
    return inside / total


def check_alignment(d: WorkData) -> list[CheckResult]:
    share = roads_in_buildings_share(d)
    return [
        CheckResult(
            "alignment.roadsInBuildings",
            _graded(share, ROADS_IN_BUILDINGS_OK, ROADS_IN_BUILDINGS_WARN),
            round(share, 4),
            f"{share:.1%} of the road length runs through building footprints "
            f"(passages are normal; ok <= {ROADS_IN_BUILDINGS_OK:.0%})",
        )
    ]


CHECKS: tuple[Callable[[WorkData], list[CheckResult]], ...] = (
    check_terrain,
    check_origins,
    check_buildings,
    check_streets_and_features,
    check_alignment,
)


def run_checks(d: WorkData) -> list[CheckResult]:
    return [r for check in CHECKS for r in check(d)]


def overall_status(results: list[CheckResult]) -> str:
    return max((r.status for r in results), key=_RANK.__getitem__, default=OK)


def report_document(site: str, results: list[CheckResult]) -> dict[str, Any]:
    return {
        "format": "gothar-report",
        "version": 1,
        "site": site,
        "status": overall_status(results),
        "checks": [asdict(r) for r in results],
    }
