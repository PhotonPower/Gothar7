"""OSM highways and squares -> ``streets.json`` (local coordinates).

Streets are centre lines with a width (from ``width``/``est_width``, else ``lanes``, else a default
per highway type) and a class for later surface rules; squares are polygons.
"""

from __future__ import annotations

import re
from collections import Counter
from typing import Any

import shapely
from shapely.geometry import LineString, Polygon

from gothar_worldgen.geo.frame import LocalFrame, line_xz, polygon_fields
from gothar_worldgen.geo.osm import OsmObject, osm_sort_key

# highway value -> (class, default width in metres). Defaults are estimates for the game's
# street layout; real widths from OSM tags take precedence.
HIGHWAYS: dict[str, tuple[str, float]] = {
    "motorway": ("road", 22.0),
    "trunk": ("road", 15.0),
    "primary": ("road", 9.0),
    "secondary": ("road", 8.0),
    "tertiary": ("road", 7.0),
    "unclassified": ("road", 5.5),
    "residential": ("road", 5.5),
    "living_street": ("road", 4.5),
    "service": ("road", 3.5),
    "road": ("road", 5.0),
    "motorway_link": ("road", 6.0),
    "trunk_link": ("road", 6.0),
    "primary_link": ("road", 6.0),
    "secondary_link": ("road", 6.0),
    "tertiary_link": ("road", 6.0),
    "pedestrian": ("pedestrian", 5.0),
    "track": ("track", 3.0),
    "footway": ("path", 2.0),
    "path": ("path", 1.5),
    "cycleway": ("path", 2.0),
    "bridleway": ("path", 2.0),
    "steps": ("steps", 2.0),
}
LANE_WIDTH_M = 3.0
# Highway values that describe a square when mapped as an area.
SQUARE_HIGHWAYS = {"pedestrian", "footway", "living_street", "service", "track"}

_WIDTH_RE = re.compile(r"^\s*(\d+(?:[.,]\d+)?)\s*(m|meter|metre)?\s*$", re.IGNORECASE)


def parse_width(value: str | None) -> float | None:
    """``"5"``, ``"5.5 m"``, ``"4,5"`` -> metres; anything else (feet, ranges) -> None."""
    if not value:
        return None
    m = _WIDTH_RE.match(value)
    if not m:
        return None
    width = float(m[1].replace(",", "."))
    return width if 0 < width < 100 else None


def street_width(tags: dict[str, str]) -> tuple[float, str]:
    """Width in metres and where it came from ("tag", "lanes" or "default")."""
    for key in ("width", "est_width"):
        w = parse_width(tags.get(key))
        if w is not None:
            return w, "tag"
    lanes = tags.get("lanes", "")
    if lanes.isdigit() and int(lanes) > 0:
        return int(lanes) * LANE_WIDTH_M, "lanes"
    return HIGHWAYS[tags["highway"]][1], "default"


def _yes(tags: dict[str, str], key: str) -> bool:
    return tags.get(key, "no") not in ("no", "false", "0")


def _layer(tags: dict[str, str]) -> int:
    try:
        return int(tags.get("layer", "0"))
    except ValueError:
        return 0


def _lines(geom: shapely.Geometry) -> list[LineString]:
    if isinstance(geom, LineString):
        return [geom]
    return [g for g in getattr(geom, "geoms", []) if isinstance(g, LineString)]


def _polygons(geom: shapely.Geometry) -> list[Polygon]:
    if isinstance(geom, Polygon):
        return [geom]
    return [p for g in getattr(geom, "geoms", []) for p in _polygons(g)]


def is_square(obj: OsmObject) -> bool:
    t = obj.tags
    if obj.kind != "area":
        return False
    if t.get("place") == "square" or t.get("amenity") == "marketplace":
        return True
    return t.get("highway") in SQUARE_HIGHWAYS and t.get("area") == "yes"


def street_entries(obj: OsmObject, frame: LocalFrame) -> list[dict[str, Any]]:
    """Centre line(s) of a highway way (several if clipping split it)."""
    t = obj.tags
    if obj.kind != "way" or t.get("highway") not in HIGHWAYS or t.get("area") == "yes":
        return []
    cls = HIGHWAYS[t["highway"]][0]
    width, source = street_width(t)
    base: dict[str, Any] = {"osmId": obj.osm_id, "highway": t["highway"], "class": cls}
    if "name" in t:
        base["name"] = t["name"]
    base |= {"widthM": width, "widthSource": source}
    for key in ("surface", "smoothness", "incline"):
        if key in t:
            base[key] = t[key]
    if _layer(t):
        base["layer"] = _layer(t)
    for key in ("bridge", "tunnel", "oneway"):
        if _yes(t, key):
            base[key] = True
    entries = []
    for line in _lines(obj.geometry):
        pts = line_xz(frame, line.coords)
        if len(pts) >= 2:
            entries.append(base | {"points": pts})
    return entries


def square_entries(obj: OsmObject, frame: LocalFrame) -> list[dict[str, Any]]:
    if not is_square(obj):
        return []
    t = obj.tags
    kind = (
        "marketplace"
        if t.get("amenity") == "marketplace"
        else "square"
        if t.get("place") == "square"
        else t["highway"]
    )
    base: dict[str, Any] = {"osmId": obj.osm_id, "kind": kind}
    if "name" in t:
        base["name"] = t["name"]
    if "surface" in t:
        base["surface"] = t["surface"]
    return [base | polygon_fields(frame, p) for p in _polygons(obj.geometry) if p.area >= 1.0]


def build_streets(objects: list[OsmObject], frame: LocalFrame) -> dict[str, list[dict[str, Any]]]:
    streets = [e for o in objects for e in street_entries(o, frame)]
    squares = [e for o in objects for e in square_entries(o, frame)]
    streets.sort(key=osm_sort_key)
    squares.sort(key=osm_sort_key)
    return {"streets": streets, "squares": squares}


def summarize_streets(layers: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
    streets = layers["streets"]
    return {
        "streets": len(streets),
        "squares": len(layers["squares"]),
        "classes": dict(Counter(s["class"] for s in streets).most_common()),
        "widthFromTags": sum(1 for s in streets if s["widthSource"] != "default"),
    }
