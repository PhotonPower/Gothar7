"""OpenStreetMap extract (.osm.pbf / .osm) -> tagged geometries in source coordinates.

Reads nodes, ways and assembled areas (closed ways and multipolygon relations) with selected
keys, keeps those touching the area, converts WGS84 to the site CRS and clips them to the area.
OSM is referenced to WGS84; the difference to ETRS89 (< 1 m) is ignored.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

import osmium
import shapely
from pyproj import Transformer
from shapely.geometry import LineString, Point, Polygon

from gothar_worldgen.geo.bbox import BBox

# Keys of objects any OSM import step is interested in.
DEFAULT_KEYS = (
    "highway",
    "barrier",
    "waterway",
    "natural",
    "landuse",
    "leisure",
    "amenity",
    "historic",
    "place",
    "railway",
    "water",
    "man_made",
)
# Margin for the WGS84 pre-filter box (degrees, ~50 m) – exact clipping happens in UTM.
_LONLAT_MARGIN = 0.0005


class OsmError(Exception):
    """Missing or broken OSM data; the message is meant for the user."""


@dataclass(frozen=True)
class OsmObject:
    """``kind`` is "node", "way" or "area"; geometry in source CRS, clipped to the area."""

    osm_id: str  # "n123", "w123" or "r123" (areas: id of the originating way/relation)
    kind: str
    tags: dict[str, str]
    geometry: shapely.Geometry


@dataclass
class OsmExtract:
    objects: list[OsmObject]
    source_file: str
    timestamp: str | None  # replication timestamp of the extract, if known
    counts: dict[str, int] = field(default_factory=dict)


def osm_sort_key(record: dict[str, object]) -> tuple[str, int]:
    """Stable output order for records with an ``osmId`` like "w123": by type, then number."""
    osm_id = str(record["osmId"])
    return osm_id[0], int(osm_id[1:])


def find_osm_file(osm_dir: Path) -> Path:
    files = sorted(osm_dir.glob("*.osm.pbf")) + sorted(osm_dir.glob("*.osm"))
    if not files:
        raise OsmError(
            f"no OSM extract (*.osm.pbf) in {osm_dir} "
            "(run 'gothar-worldgen download <site> --only osm')"
        )
    return files[0]


class _Projector:
    def __init__(self, crs: str) -> None:
        self._t = Transformer.from_crs("EPSG:4326", crs, always_xy=True)
        self._inv = Transformer.from_crs(crs, "EPSG:4326", always_xy=True)

    def coords(self, lonlat: list[tuple[float, float]]) -> list[tuple[float, float]]:
        lons, lats = zip(*lonlat, strict=True)
        es, ns = self._t.transform(lons, lats)
        return list(zip(es, ns, strict=True))

    def lonlat_box(self, bbox: BBox) -> tuple[float, float, float, float]:
        corners = [
            self._inv.transform(e, n)
            for e in (bbox.min_e, bbox.max_e)
            for n in (bbox.min_n, bbox.max_n)
        ]
        lons, lats = [c[0] for c in corners], [c[1] for c in corners]
        m = _LONLAT_MARGIN
        return min(lons) - m, min(lats) - m, max(lons) + m, max(lats) + m


def _in_box(lon: float, lat: float, box: tuple[float, float, float, float]) -> bool:
    return box[0] <= lon <= box[2] and box[1] <= lat <= box[3]


def _overlaps_box(pts: list[tuple[float, float]], box: tuple[float, float, float, float]) -> bool:
    """Envelope test: also true for shapes that enclose or cross the box without a vertex in it."""
    lons, lats = [p[0] for p in pts], [p[1] for p in pts]
    return (
        min(lons) <= box[2] and max(lons) >= box[0] and min(lats) <= box[3] and max(lats) >= box[1]
    )


def _area_polygons(area: osmium.osm.Area) -> list[list[list[tuple[float, float]]]]:
    """Rings (outer first, then inner) per outer ring, in lon/lat."""
    result = []
    for outer in area.outer_rings():
        rings = [[(n.lon, n.lat) for n in outer]]
        rings += [[(n.lon, n.lat) for n in inner] for inner in area.inner_rings(outer)]
        result.append(rings)
    return result


def read_osm(path: Path, bbox: BBox, crs: str, keys: Iterable[str] = DEFAULT_KEYS) -> OsmExtract:
    """All objects with one of ``keys`` that touch ``bbox`` (source CRS), clipped to it."""
    proj = _Projector(crs)
    box = proj.lonlat_box(bbox)
    clip = shapely.box(bbox.min_e, bbox.min_n, bbox.max_e, bbox.max_n)
    keys = tuple(keys)
    objects: list[OsmObject] = []
    counts = {"node": 0, "way": 0, "area": 0, "outside": 0, "broken": 0}

    try:
        fp = (
            osmium.FileProcessor(str(path))
            .with_locations()
            .with_areas()
            .with_filter(osmium.filter.KeyFilter(*keys))
        )
        timestamp = fp.header.get("osmosis_replication_timestamp") or None
        for o in fp:
            try:
                obj = _convert(o, box, proj, clip)
            except (osmium.InvalidLocationError, ValueError, shapely.errors.GEOSException):
                counts["broken"] += 1
                continue
            if obj is None:
                counts["outside"] += 1
                continue
            counts[obj.kind] += 1
            objects.append(obj)
    except RuntimeError as e:  # pyosmium reports I/O and format errors as RuntimeError
        raise OsmError(f"{path.name}: {e}") from None
    return OsmExtract(objects, path.name, timestamp, counts)


def _convert(
    o: osmium.osm.OSMObject,
    box: tuple[float, float, float, float],
    proj: _Projector,
    clip: shapely.Geometry,
) -> OsmObject | None:
    if o.is_node():
        loc = o.location
        if not loc.valid() or not _in_box(loc.lon, loc.lat, box):
            return None
        geom: shapely.Geometry = Point(proj.coords([(loc.lon, loc.lat)])[0])
        kind, osm_id = "node", f"n{o.id}"
    elif o.is_way():
        pts = [(n.lon, n.lat) for n in o.nodes]
        if len(pts) < 2 or not _overlaps_box(pts, box):
            return None
        geom = LineString(proj.coords(pts))
        kind, osm_id = "way", f"w{o.id}"
    elif o.is_area():
        polys = _area_polygons(o)
        if not any(_overlaps_box(rings[0], box) for rings in polys):
            return None
        shapes = [
            shapely.make_valid(Polygon(proj.coords(r[0]), [proj.coords(i) for i in r[1:]]))
            for r in polys
            if len(r[0]) >= 4
        ]
        geom = shapely.union_all(shapes) if shapes else Polygon()
        kind = "area"
        osm_id = f"{'w' if o.from_way() else 'r'}{o.orig_id()}"
    else:
        return None
    clipped = geom.intersection(clip)
    if clipped.is_empty:
        return None
    return OsmObject(osm_id, kind, {t.k: t.v for t in o.tags}, clipped)
