from pathlib import Path

import pytest
import shapely
from shapely.geometry import LineString, MultiLineString

from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.frame import LocalFrame
from gothar_worldgen.geo.osm import OsmObject, read_osm
from gothar_worldgen.geo.streets import (
    build_streets,
    is_square,
    parse_width,
    square_entries,
    street_entries,
    street_width,
    summarize_streets,
)

FRAME = LocalFrame(1000.0, 2000.0, 300.0, 1.0, 1.0)
OSM_EXCERPT = Path(__file__).parent / "data" / "osm" / "leonberg_marktplatz.osm.pbf"


def way(osm_id: str, tags: dict, coords) -> OsmObject:
    return OsmObject(osm_id, "way", tags, LineString(coords))


def area(osm_id: str, tags: dict, geom) -> OsmObject:
    return OsmObject(osm_id, "area", tags, geom)


@pytest.mark.parametrize(
    ("value", "width"),
    [("5", 5.0), ("5.5 m", 5.5), ("4,5", 4.5), ("3m", 3.0), ("10'", None), ("2-3", None),
     ("", None), (None, None), ("0", None)],
)  # fmt: skip
def test_parse_width(value, width):
    assert parse_width(value) == width


@pytest.mark.parametrize(
    ("tags", "expected"),
    [
        ({"highway": "residential", "width": "7"}, (7.0, "tag")),
        ({"highway": "residential", "width": "wide", "est_width": "6"}, (6.0, "tag")),
        ({"highway": "secondary", "lanes": "2"}, (6.0, "lanes")),
        ({"highway": "footway", "lanes": "x"}, (2.0, "default")),
        ({"highway": "steps"}, (2.0, "default")),
    ],
)
def test_street_width(tags, expected):
    assert street_width(tags) == expected


def test_street_entry_fields():
    tags = {
        "highway": "residential",
        "name": "Marktplatz",
        "surface": "sett",
        "layer": "1",
        "bridge": "yes",
        "oneway": "no",
    }
    (e,) = street_entries(way("w5", tags, [(1000, 2000), (1010, 2000), (1010, 2010)]), FRAME)
    assert e == {
        "osmId": "w5",
        "highway": "residential",
        "class": "road",
        "name": "Marktplatz",
        "widthM": 5.5,
        "widthSource": "default",
        "surface": "sett",
        "layer": 1,
        "bridge": True,
        "points": [[0.0, 0.0], [10.0, 0.0], [10.0, -10.0]],
    }


def test_non_streets_are_skipped():
    assert street_entries(way("w1", {"highway": "construction"}, [(0, 0), (1, 1)]), FRAME) == []
    assert street_entries(way("w2", {"barrier": "wall"}, [(0, 0), (1, 1)]), FRAME) == []
    pedestrian_area = {"highway": "pedestrian", "area": "yes"}
    assert street_entries(way("w3", pedestrian_area, [(0, 0), (1, 1)]), FRAME) == []


def test_clipped_street_becomes_two_entries():
    obj = OsmObject(
        "w9",
        "way",
        {"highway": "path"},
        MultiLineString([[(1000, 2000), (1005, 2000)], [(1010, 2000), (1020, 2000)]]),
    )
    assert [e["points"][0] for e in street_entries(obj, FRAME)] == [[0.0, 0.0], [10.0, 0.0]]


@pytest.mark.parametrize(
    ("tags", "kind"),
    [
        ({"place": "square", "name": "Marktplatz"}, "square"),
        ({"amenity": "marketplace"}, "marketplace"),
        ({"highway": "pedestrian", "area": "yes"}, "pedestrian"),
        ({"highway": "pedestrian"}, None),
        ({"landuse": "grass"}, None),
    ],
)
def test_squares(tags, kind):
    obj = area("w1", tags, shapely.box(1000, 2000, 1020, 2010))
    assert is_square(obj) == (kind is not None)
    entries = square_entries(obj, FRAME)
    if kind is None:
        assert entries == []
    else:
        (e,) = entries
        assert e["kind"] == kind
        assert e["areaM2"] == 200.0
        assert len(e["polygon"]) == 4


def test_build_streets_sorted_and_summarized():
    objects = [
        way("w10", {"highway": "footway"}, [(1000, 2000), (1001, 2000)]),
        way("w9", {"highway": "residential", "width": "6"}, [(1000, 2000), (1002, 2000)]),
        area("w3", {"place": "square"}, shapely.box(1000, 2000, 1005, 2005)),
    ]
    layers = build_streets(objects, FRAME)
    assert [s["osmId"] for s in layers["streets"]] == ["w9", "w10"]
    assert summarize_streets(layers) == {
        "streets": 2,
        "squares": 1,
        "classes": {"road": 1, "path": 1},
        "widthFromTags": 1,
    }


def test_real_marktplatz_excerpt():
    frame = LocalFrame(501115.0, 5405347.0, 386.89, 1.0, 1.0)
    extract = read_osm(OSM_EXCERPT, BBox.around(501_115, 5_405_347, 60), "EPSG:25832")
    layers = build_streets(extract.objects, frame)
    (markt,) = [q for q in layers["squares"] if q.get("name") == "Marktplatz"]
    assert markt["kind"] == "square"
    assert markt["surface"] == "sett"
    # The square contains the origin (Marktbrunnen).
    assert shapely.Polygon(markt["polygon"]).contains(shapely.Point(0, 0))
    assert any(s["class"] == "pedestrian" for s in layers["streets"])
