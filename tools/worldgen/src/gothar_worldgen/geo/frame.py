"""Source coordinates (EPSG:25832, NHN) -> local engine coordinates.

Local system (docs/design/leonberg-pipeline.md, section 3): metres, origin at the site origin,
+X = east, +Y = up, -Z = north; horizontal and vertical game scale applied here only.
"""

from __future__ import annotations

from dataclasses import dataclass

import shapely
from shapely.geometry import Polygon
from shapely.geometry.polygon import orient

from gothar_worldgen.config import SiteConfig


@dataclass(frozen=True)
class LocalFrame:
    origin_e: float
    origin_n: float
    origin_nhn: float
    horizontal: float
    vertical: float

    @classmethod
    def for_site(cls, site: SiteConfig, origin_nhn: float) -> LocalFrame:
        s = site.game_scale
        return cls(site.origin.easting, site.origin.northing, origin_nhn, s.horizontal, s.vertical)

    def xz(self, e: float, n: float) -> tuple[float, float]:
        return (e - self.origin_e) * self.horizontal, -(n - self.origin_n) * self.horizontal

    def y(self, nhn: float) -> float:
        return (nhn - self.origin_nhn) * self.vertical

    def area(self, source_m2: float) -> float:
        return source_m2 * self.horizontal**2

    def origin_json(self, crs: str) -> dict[str, object]:
        return {
            "crs": crs,
            "easting": self.origin_e,
            "northing": self.origin_n,
            "heightNHN": self.origin_nhn,
        }

    def scale_json(self) -> dict[str, float]:
        return {"horizontal": self.horizontal, "vertical": self.vertical}


def round_cm(v: float) -> float:
    """Round output coordinates to centimetres; avoids "-0.0"."""
    return round(v, 2) + 0.0


def point_xz(frame: LocalFrame, e: float, n: float) -> list[float]:
    x, z = frame.xz(e, n)
    return [round_cm(x), round_cm(z)]


def line_xz(frame: LocalFrame, coords: object) -> list[list[float]]:
    """Coordinates (source CRS) -> local [x, z] points; consecutive duplicates removed."""
    out: list[list[float]] = []
    for e, n, *_ in coords:  # type: ignore[attr-defined]
        pt = point_xz(frame, e, n)
        if not out or out[-1] != pt:
            out.append(pt)
    return out


def ring_xz(frame: LocalFrame, ring: shapely.LinearRing) -> list[list[float]]:
    """Closed ring -> open list of local [x, z] points (closing point dropped)."""
    out = line_xz(frame, list(ring.coords)[:-1])
    if len(out) > 1 and out[0] == out[-1]:
        out.pop()
    return out


def polygon_fields(
    frame: LocalFrame, polygon: Polygon, min_hole_m2: float = 1.0
) -> dict[str, object]:
    """``polygon`` (counter-clockwise seen from above, north up), optional ``holes``, ``areaM2``."""
    polygon = orient(polygon, 1.0)
    fields: dict[str, object] = {"polygon": ring_xz(frame, polygon.exterior)}
    holes = [ring_xz(frame, r) for r in polygon.interiors if Polygon(r).area >= min_hole_m2]
    if holes:
        fields["holes"] = holes
    fields["areaM2"] = round(frame.area(polygon.area), 1)
    return fields
