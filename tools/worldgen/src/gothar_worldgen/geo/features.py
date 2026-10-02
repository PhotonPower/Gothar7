"""OSM features (walls, water, trees, vegetation/land use, fountains, landmarks, railways)
-> ``features.json`` (local coordinates).

Each feature has a ``type`` (what the generator does with it) and a ``kind`` (the OSM value).
"""

from __future__ import annotations

from collections import Counter
from typing import Any

import shapely
from shapely.geometry import LineString, Point, Polygon

from gothar_worldgen.geo.frame import LocalFrame, line_xz, point_xz, polygon_fields
from gothar_worldgen.geo.osm import OsmObject, osm_sort_key

WALL_BARRIERS = {"wall", "city_wall", "retaining_wall"}
WATERWAYS = {"river", "stream", "canal", "ditch", "drain"}
VEGETATION_NATURAL = {"wood", "scrub", "heath", "grassland", "wetland", "scree", "bare_rock"}
LANDUSE = {
    "forest", "meadow", "orchard", "vineyard", "farmland", "grass", "allotments", "cemetery",
    "village_green", "recreation_ground", "residential", "commercial", "industrial", "retail",
    "railway", "farmyard", "plant_nursery", "greenhouse_horticulture", "construction",
}  # fmt: skip
LEISURE_AREAS = {"park", "garden", "pitch", "playground", "sports_centre", "stadium"}
RAILWAYS = {"rail", "light_rail", "tram", "narrow_gauge"}
LANDMARK_HISTORIC = {
    "castle", "city_gate", "monument", "memorial", "ruins", "wayside_cross", "wayside_shrine",
    "fort", "tower", "boundary_stone", "building", "church", "manor",
}  # fmt: skip
# Tags copied to the output when present (``name`` is a top-level field).
KEEP_TAGS = (
    "height", "width", "est_width", "material", "surface", "intermittent", "tunnel", "layer",
    "leaf_type", "leaf_cycle", "species", "genus", "denotation", "religion", "denomination",
    "historic", "start_date", "wikidata",
)  # fmt: skip


def classify(obj: OsmObject) -> tuple[str, str] | None:
    """(type, kind) for objects that become features, else None."""
    t, k = obj.tags, obj.kind
    if k == "way":
        if t.get("barrier") in WALL_BARRIERS:
            return "wall", t["barrier"]
        if t.get("historic") == "city_wall":
            return "wall", "city_wall"
        if t.get("barrier") == "hedge":
            return "hedge", "hedge"
        if t.get("waterway") in WATERWAYS:
            return "waterway", t["waterway"]
        if t.get("natural") == "tree_row":
            return "tree_row", "tree_row"
        if t.get("railway") in RAILWAYS:
            return "railway", t["railway"]
        return None
    if k == "area":
        if t.get("natural") == "water" or t.get("waterway") == "riverbank":
            return "water", t.get("water", "water")
        if t.get("landuse") in ("reservoir", "basin"):
            return "water", t["landuse"]
        if t.get("amenity") == "fountain":
            return "fountain", t.get("fountain", "fountain")
        if t.get("amenity") == "place_of_worship" or t.get("historic") in LANDMARK_HISTORIC:
            return "landmark", t.get("historic") or t["amenity"]
        if t.get("natural") in VEGETATION_NATURAL:
            return "landuse", t["natural"]
        if t.get("landuse") in LANDUSE:
            return "landuse", t["landuse"]
        if t.get("leisure") in LEISURE_AREAS:
            return "landuse", t["leisure"]
        return None
    # nodes
    if t.get("natural") == "tree":
        return "tree", "tree"
    if t.get("amenity") == "fountain":
        return "fountain", t.get("fountain", "fountain")
    if t.get("man_made") == "water_well":
        return "fountain", "water_well"
    if t.get("amenity") == "place_of_worship" or t.get("historic") in LANDMARK_HISTORIC:
        return "landmark", t.get("historic") or t["amenity"]
    return None


def _parts(geom: shapely.Geometry, cls: type) -> list[Any]:
    if isinstance(geom, cls):
        return [geom]
    return [p for g in getattr(geom, "geoms", []) for p in _parts(g, cls)]


def feature_entries(obj: OsmObject, frame: LocalFrame) -> list[dict[str, Any]]:
    cls = classify(obj)
    if cls is None:
        return []
    ftype, kind = cls
    base: dict[str, Any] = {"osmId": obj.osm_id, "type": ftype, "kind": kind}
    if "name" in obj.tags:
        base["name"] = obj.tags["name"]
    tags = {k: obj.tags[k] for k in KEEP_TAGS if k in obj.tags}
    if tags:
        base["tags"] = tags

    if obj.kind == "node":
        (p,) = _parts(obj.geometry, Point)
        return [base | {"geometry": "point", "position": point_xz(frame, p.x, p.y)}]
    if obj.kind == "way":
        return [
            base | {"geometry": "line", "points": line_xz(frame, line.coords)}
            for line in _parts(obj.geometry, LineString)
            if len(line_xz(frame, line.coords)) >= 2
        ]
    return [
        base | {"geometry": "polygon"} | polygon_fields(frame, poly)
        for poly in _parts(obj.geometry, Polygon)
        if poly.area >= 0.5
    ]


def build_features(objects: list[OsmObject], frame: LocalFrame) -> list[dict[str, Any]]:
    features = [e for o in objects for e in feature_entries(o, frame)]
    features.sort(key=lambda e: (e["type"], *osm_sort_key(e)))
    return features


def summarize_features(features: list[dict[str, Any]]) -> dict[str, int]:
    return dict(Counter(f["type"] for f in features).most_common())
