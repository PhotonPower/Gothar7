"""LoD2 buildings -> ``buildings.json`` in the local engine system.

Per building: footprint, ground height, roof (type, eave/ridge height, ridge direction, pitch)
and, for buildings made of several LoD2 parts, the same per part. Heights are local y
(same system as ``terrain.json``), not relative to the building's ground.
"""

from __future__ import annotations

import math
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import shapely
from shapely.geometry import Polygon
from shapely.geometry.polygon import orient

from gothar_worldgen.config import SiteConfig
from gothar_worldgen.geo.frame import LocalFrame, ring_xz, round_cm
from gothar_worldgen.geo.jsonio import write_json_records
from gothar_worldgen.geo.lod2 import Body, Lod2Building, Polygon3

FORMAT_NAME = "gothar-buildings"
FORMAT_VERSION = 1
LGL_CREDIT = "Datengrundlage: LGL, www.lgl-bw.de"

# ALKIS roof type codes (AdV "Dachform") -> names used by the generator.
ROOF_TYPES = {
    "1000": "flat",
    "2100": "shed",
    "2200": "offset_shed",
    "3100": "saddle",
    "3200": "hip",
    "3300": "half_hip",
    "3400": "mansard",
    "3500": "tent",
    "3600": "cone",
    "3700": "dome",
    "3800": "sawtooth",
    "3900": "arch",
    "4000": "tower",
    "5000": "mixed",
    "9999": "other",
}
# Roof types without a meaningful ridge line.
NO_RIDGE = {"flat", "tent", "cone", "dome", "tower"}

RIDGE_TOLERANCE_M = 0.05  # vertices this close to the top count as ridge
MIN_RIDGE_LENGTH_M = 0.5
HEIGHT_MISMATCH_M = 1.0  # warn if derived height differs more from LoD2 measuredHeight
GAP_CLOSE_M = 0.05  # gaps between building parts up to twice this are closed
MIN_HOLE_M2 = 1.0  # smaller holes in footprints (slivers) are dropped
SIMPLIFY_M = 0.01  # removes collinear vertices left over from merging parts


def _polygon_2d(p: Polygon3) -> Polygon:
    return Polygon(p.exterior[:, :2], [r[:, :2] for r in p.interiors])


def _polygons_in(geom: shapely.Geometry) -> list[Polygon]:
    """All non-empty polygons contained in a (possibly nested) geometry."""
    if isinstance(geom, Polygon):
        return [] if geom.is_empty else [geom]
    return [p for part in getattr(geom, "geoms", []) for p in _polygons_in(part)]


def clean_footprint(geom: shapely.Geometry, warnings: list[str]) -> Polygon | None:
    """Make a single, valid, counter-clockwise (seen from above, north up) polygon.

    Parts that only touch with tiny gaps are merged; otherwise the largest piece is kept.
    """
    polys = _polygons_in(shapely.make_valid(geom))
    if not polys:
        return None
    merged = shapely.union_all(polys)
    if not isinstance(merged, Polygon):
        closed = merged.buffer(GAP_CLOSE_M, join_style="mitre").buffer(
            -GAP_CLOSE_M, join_style="mitre"
        )
        if isinstance(closed, Polygon):
            merged = closed
        else:
            pieces = _polygons_in(merged)
            warnings.append(f"footprint has {len(pieces)} separate pieces, kept the largest")
            merged = max(pieces, key=lambda g: g.area)
    merged = merged.simplify(SIMPLIFY_M)
    return orient(merged, 1.0) if isinstance(merged, Polygon) and merged.area > 0 else None


def footprint_of(polys: Iterable[Polygon3], warnings: list[str]) -> Polygon | None:
    """Footprint from the 2D projection of surface polygons."""
    shapes = [shapely.make_valid(_polygon_2d(p)) for p in polys]
    return clean_footprint(shapely.union_all(shapes), warnings) if shapes else None


def _footprint_fields(footprint: Polygon, frame: LocalFrame) -> dict[str, Any]:
    fields: dict[str, Any] = {"footprint": ring_xz(frame, footprint.exterior)}
    holes = [ring_xz(frame, r) for r in footprint.interiors if Polygon(r).area >= MIN_HOLE_M2]
    if holes:
        fields["holes"] = holes
    fields["areaM2"] = round(frame.area(footprint.area), 1)
    return fields


def _newell_normal(ring: np.ndarray) -> np.ndarray:
    """Polygon normal (not normalised; length = 2 * area) in (e, n, h)."""
    p, q = ring[:-1], ring[1:]
    return np.array(
        [
            np.sum((p[:, 1] - q[:, 1]) * (p[:, 2] + q[:, 2])),
            np.sum((p[:, 2] - q[:, 2]) * (p[:, 0] + q[:, 0])),
            np.sum((p[:, 0] - q[:, 0]) * (p[:, 1] + q[:, 1])),
        ]
    )


def roof_pitch_deg(roofs: list[Polygon3], height_ratio: float = 1.0) -> float:
    """Area-weighted mean slope of the roof surfaces in degrees.

    ``height_ratio`` (vertical / horizontal game scale) makes the slope match the scaled roof.
    """
    total, weighted = 0.0, 0.0
    for p in roofs:
        nrm = _newell_normal(p.exterior * np.array([1.0, 1.0, height_ratio]))
        area = float(np.linalg.norm(nrm))
        if area <= 1e-9:
            continue
        slope = math.degrees(math.acos(min(1.0, abs(nrm[2]) / area)))
        total += area
        weighted += slope * area
    return weighted / total if total else 0.0


def ridge_direction(roofs: list[Polygon3]) -> list[float] | None:
    """Horizontal direction (local x, z) of the longest edge at the top of the roof."""
    top = max(float(p.exterior[:, 2].max()) for p in roofs)
    best: tuple[float, float, float] | None = None  # (length, de, dn)
    for p in roofs:
        ring = p.exterior
        for a, b in zip(ring[:-1], ring[1:], strict=True):
            if min(a[2], b[2]) < top - RIDGE_TOLERANCE_M:
                continue
            de, dn = float(b[0] - a[0]), float(b[1] - a[1])
            length = math.hypot(de, dn)
            if length >= MIN_RIDGE_LENGTH_M and (best is None or length > best[0]):
                best = (length, de, dn)
    if best is None:
        return None
    length, de, dn = best
    x, z = de / length, -dn / length  # local: -Z is north
    if x < -1e-9 or (abs(x) <= 1e-9 and z < 0):  # a ridge has no sign: canonical x >= 0
        x, z = -x, -z
    return [round(x, 3) + 0.0, round(z, 3) + 0.0]


def roof_of(body: Body, frame: LocalFrame) -> dict[str, Any] | None:
    roofs = body.polygons("roof")
    if not roofs:
        return None
    heights = np.concatenate([p.exterior[:, 2] for p in roofs])
    name = ROOF_TYPES.get(body.roof_type or "", "unknown")
    return {
        "type": name,
        "alkis": body.roof_type,
        "eaveY": round_cm(frame.y(float(heights.min()))),
        "ridgeY": round_cm(frame.y(float(heights.max()))),
        "ridgeDir": None if name in NO_RIDGE else ridge_direction(roofs),
        "pitchDeg": round(roof_pitch_deg(roofs, frame.vertical / frame.horizontal), 1),
    }


@dataclass
class BodyResult:
    data: dict[str, Any]
    footprint: Polygon | None
    ground_nhn: float | None
    ridge_y: float | None


def _body_result(body: Body, frame: LocalFrame, warnings: list[str]) -> BodyResult:
    grounds = body.polygons("ground")
    footprint = footprint_of(grounds, warnings) if grounds else None
    if footprint is None:
        footprint = footprint_of(body.polygons("roof"), warnings)
        if footprint is not None:
            warnings.append(f"{body.id}: no ground surface, footprint from roof projection")
    low_polys = grounds or body.polygons("wall")
    ground_nhn = float(min(p.exterior[:, 2].min() for p in low_polys)) if low_polys else None
    roof = roof_of(body, frame)
    if roof is None:
        warnings.append(f"{body.id}: no roof surfaces")

    data: dict[str, Any] = {"id": body.id}
    if footprint is not None:
        data.update(_footprint_fields(footprint, frame))
    if ground_nhn is not None:
        data["groundY"] = round_cm(frame.y(ground_nhn))
    ridge_y = roof["ridgeY"] if roof else None
    if ridge_y is not None and ground_nhn is not None:
        data["heightM"] = round_cm(ridge_y - frame.y(ground_nhn))
        if body.measured_height is not None:
            measured = body.measured_height * frame.vertical
            if abs(data["heightM"] - measured) > HEIGHT_MISMATCH_M:
                warnings.append(
                    f"{body.id}: height {data['heightM']} m differs from LoD2 "
                    f"measuredHeight {measured:.2f} m"
                )
    data["roof"] = roof
    return BodyResult(data, footprint, ground_nhn, ridge_y)


def convert_building(
    b: Lod2Building, frame: LocalFrame, core: shapely.Geometry | None = None
) -> dict[str, Any] | None:
    """One ``buildings.json`` entry, or None if the building has no usable geometry."""
    warnings: list[str] = []
    bodies = [_body_result(body, frame, warnings) for body in b.bodies]
    usable = [r for r in bodies if r.footprint is not None]
    if not usable:
        return None
    footprint = clean_footprint(shapely.union_all([r.footprint for r in usable]), warnings)
    if footprint is None:
        return None
    main = max(usable, key=lambda r: r.footprint.area if r.footprint else 0.0)
    grounds = [r.ground_nhn for r in bodies if r.ground_nhn is not None]
    ridges = [r.ridge_y for r in bodies if r.ridge_y is not None]

    entry: dict[str, Any] = {"id": b.id, "function": b.function}
    if core is not None:
        entry["inCore"] = bool(core.contains(footprint.representative_point()))
    entry.update(_footprint_fields(footprint, frame))
    if grounds:
        entry["groundY"] = round_cm(frame.y(min(grounds)))
        if ridges:
            entry["heightM"] = round_cm(max(ridges) - frame.y(min(grounds)))
    entry["roof"] = main.data["roof"]
    if b.has_parts:
        entry["parts"] = [r.data for r in bodies]
    if warnings:
        entry["warnings"] = warnings
    return entry


def select_and_convert(
    buildings: Iterable[Lod2Building],
    frame: LocalFrame,
    area: shapely.Geometry,
    core: shapely.Geometry,
) -> list[dict[str, Any]]:
    """Convert all buildings whose footprint lies (by representative point) inside ``area``."""
    seen: set[str] = set()
    result = []
    for b in buildings:
        if b.id in seen:
            continue
        seen.add(b.id)
        grounds = [p for body in b.bodies for p in body.polygons("ground")] or [
            p for body in b.bodies for p in body.polygons("roof")
        ]
        if not grounds:
            continue
        probe = shapely.union_all([shapely.make_valid(_polygon_2d(p)) for p in grounds])
        if probe.is_empty or not area.contains(probe.representative_point()):
            continue
        entry = convert_building(b, frame, core)
        if entry is not None:
            result.append(entry)
    result.sort(key=lambda e: e["id"])
    return result


def buildings_document(
    entries: list[dict[str, Any]], site: SiteConfig, frame: LocalFrame, files: list[str]
) -> dict[str, Any]:
    return {
        "format": FORMAT_NAME,
        "version": FORMAT_VERSION,
        "origin": frame.origin_json(site.crs),
        "gameScale": frame.scale_json(),
        "source": {
            "product": "LGL LoD2",
            "heightDatum": "DHHN2016",
            "files": sorted(files),
            "credit": LGL_CREDIT,
        },
        "count": len(entries),
        "buildings": entries,
    }


def write_buildings_json(path: Path, doc: dict[str, Any]) -> None:
    """Pretty header, one building per line (readable, diff-friendly, compact)."""
    header = {k: v for k, v in doc.items() if k != "buildings"}
    write_json_records(path, header, {"buildings": doc["buildings"]})


def summarize(entries: list[dict[str, Any]]) -> dict[str, Any]:
    roofs = Counter((e.get("roof") or {}).get("type", "none") for e in entries)
    return {
        "buildings": len(entries),
        "inCore": sum(1 for e in entries if e.get("inCore")),
        "withParts": sum(1 for e in entries if "parts" in e),
        "withWarnings": sum(1 for e in entries if "warnings" in e),
        "roofTypes": dict(roofs.most_common()),
    }
