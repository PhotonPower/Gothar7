from pathlib import Path

import pytest
import shapely
from shapely.geometry import LineString, Point

from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.features import (
    build_features,
    classify,
    feature_entries,
    summarize_features,
)
from gothar_worldgen.geo.frame import LocalFrame
from gothar_worldgen.geo.osm import OsmObject, read_osm

FRAME = LocalFrame(1000.0, 2000.0, 300.0, 1.0, 1.0)
OSM_EXCERPT = Path(__file__).parent / "data" / "osm" / "leonberg_marktplatz.osm.pbf"


def obj(kind: str, tags: dict, osm_id: str = "w1") -> OsmObject:
    geom = {
        "node": Point(1000, 2000),
        "way": LineString([(1000, 2000), (1010, 2000)]),
        "area": shapely.box(1000, 2000, 1010, 2010),
    }[kind]
    return OsmObject(osm_id, kind, tags, geom)


@pytest.mark.parametrize(
    ("kind", "tags", "expected"),
    [
        ("way", {"barrier": "city_wall"}, ("wall", "city_wall")),
        ("way", {"historic": "city_wall"}, ("wall", "city_wall")),
        ("way", {"barrier": "retaining_wall"}, ("wall", "retaining_wall")),
        ("way", {"barrier": "fence"}, None),
        ("way", {"barrier": "hedge"}, ("hedge", "hedge")),
        ("way", {"waterway": "stream"}, ("waterway", "stream")),
        ("way", {"natural": "tree_row"}, ("tree_row", "tree_row")),
        ("way", {"railway": "rail"}, ("railway", "rail")),
        ("way", {"landuse": "forest"}, None),  # closed way: handled as area
        ("area", {"natural": "water", "water": "pond"}, ("water", "pond")),
        ("area", {"landuse": "reservoir"}, ("water", "reservoir")),
        ("area", {"amenity": "fountain"}, ("fountain", "fountain")),
        ("area", {"historic": "castle", "name": "Schloss"}, ("landmark", "castle")),
        ("area", {"amenity": "place_of_worship"}, ("landmark", "place_of_worship")),
        ("area", {"natural": "wood"}, ("landuse", "wood")),
        ("area", {"landuse": "orchard"}, ("landuse", "orchard")),
        ("area", {"leisure": "park"}, ("landuse", "park")),
        ("area", {"barrier": "wall"}, None),  # closed wall: handled as way
        ("area", {"place": "square"}, None),  # goes to streets.json
        ("node", {"natural": "tree"}, ("tree", "tree")),
        ("node", {"amenity": "fountain", "fountain": "decorative"}, ("fountain", "decorative")),
        ("node", {"man_made": "water_well"}, ("fountain", "water_well")),
        ("node", {"historic": "wayside_cross"}, ("landmark", "wayside_cross")),
        ("node", {"amenity": "bench"}, None),
    ],
)
def test_classify(kind, tags, expected):
    assert classify(obj(kind, tags)) == expected


def test_entries_per_geometry():
    (tree,) = feature_entries(obj("node", {"natural": "tree", "species": "Tilia"}, "n4"), FRAME)
    assert tree == {
        "osmId": "n4",
        "type": "tree",
        "kind": "tree",
        "tags": {"species": "Tilia"},
        "geometry": "point",
        "position": [0.0, 0.0],
    }
    (wall,) = feature_entries(obj("way", {"barrier": "wall", "height": "2"}), FRAME)
    assert (wall["geometry"], wall["points"], wall["tags"]) == (
        "line",
        [[0.0, 0.0], [10.0, 0.0]],
        {"height": "2"},
    )
    (castle,) = feature_entries(obj("area", {"historic": "castle", "name": "Schloss"}), FRAME)
    assert (castle["geometry"], castle["name"], castle["areaM2"]) == ("polygon", "Schloss", 100.0)
    assert feature_entries(obj("node", {"amenity": "bench"}), FRAME) == []


def test_tiny_polygons_are_dropped():
    sliver = OsmObject("w1", "area", {"landuse": "grass"}, shapely.box(1000, 2000, 1000.1, 2001))
    assert feature_entries(sliver, FRAME) == []


def test_build_and_summarize():
    features = build_features(
        [
            obj("node", {"natural": "tree"}, "n2"),
            obj("way", {"barrier": "wall"}, "w5"),
            obj("node", {"natural": "tree"}, "n1"),
        ],
        FRAME,
    )
    assert [(f["type"], f["osmId"]) for f in features] == [
        ("tree", "n1"),
        ("tree", "n2"),
        ("wall", "w5"),
    ]
    assert summarize_features(features) == {"tree": 2, "wall": 1}


def test_real_marktplatz_excerpt():
    frame = LocalFrame(501115.0, 5405347.0, 386.89, 1.0, 1.0)
    extract = read_osm(OSM_EXCERPT, BBox.around(501_115, 5_405_347, 130), "EPSG:25832")
    features = build_features(extract.objects, frame)
    fountain = next(f for f in features if f.get("name") == "Marktbrunnen")
    assert fountain["type"] == "fountain"
    assert fountain["tags"]["start_date"] == "1566"
    assert any(f["type"] == "wall" and f["kind"] == "city_wall" for f in features)
