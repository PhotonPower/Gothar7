from pathlib import Path

import pytest
import shapely

from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.osm import OsmError, find_osm_file, osm_sort_key, read_osm

from .osmxml import OsmXml, square

CRS = "EPSG:25832"
AREA = BBox(500_000, 5_400_000, 500_200, 5_400_200)
OSM_EXCERPT = Path(__file__).parent / "data" / "osm" / "leonberg_marktplatz.osm.pbf"


def by_id(extract):
    return {o.osm_id: o for o in extract.objects}


def test_find_osm_file(tmp_path: Path):
    with pytest.raises(OsmError, match="no OSM extract"):
        find_osm_file(tmp_path)
    (tmp_path / "b.osm.pbf").write_bytes(b"")
    (tmp_path / "a.osm.pbf").write_bytes(b"")
    assert find_osm_file(tmp_path).name == "a.osm.pbf"


def test_sort_key():
    records = [{"osmId": "w10"}, {"osmId": "n2"}, {"osmId": "w9"}]
    assert [r["osmId"] for r in sorted(records, key=osm_sort_key)] == ["n2", "w9", "w10"]


def test_nodes_ways_and_areas(tmp_path: Path):
    osm = OsmXml()
    osm.node(500_050, 5_400_050, {"natural": "tree"})
    osm.node(500_900, 5_400_050, {"natural": "tree"})  # outside
    osm.node(500_060, 5_400_060, {"shop": "bakery"})  # key not requested
    osm.way(1, [(499_900, 5_400_100), (500_100, 5_400_100)], {"highway": "residential"})
    osm.way(2, square(500_010, 5_400_010, 20), {"landuse": "grass"}, closed=True)
    osm.way(3, square(499_000, 5_399_000, 3000), {"natural": "wood"}, closed=True)  # encloses
    extract = read_osm(osm.write(tmp_path / "t.osm"), AREA, CRS)
    objs = by_id(extract)

    assert set(objs) == {"n1", "w1", "w2", "w3"}  # closed ways appear as way and as area
    assert [o.kind for o in extract.objects].count("area") == 2
    tree = objs["n1"]
    assert (tree.kind, tree.tags) == ("node", {"natural": "tree"})
    assert tree.geometry.x == pytest.approx(500_050, abs=0.01)
    assert tree.geometry.y == pytest.approx(5_400_050, abs=0.01)

    street = objs["w1"].geometry  # clipped at the western area border
    assert street.bounds[0] == pytest.approx(500_000, abs=0.01)
    assert street.length == pytest.approx(100, abs=0.05)

    areas = {o.osm_id: o for o in extract.objects if o.kind == "area"}
    assert areas["w2"].geometry.area == pytest.approx(400, rel=1e-3)
    assert areas["w3"].geometry.area == pytest.approx(200 * 200, rel=1e-3)  # clipped to area
    assert extract.counts["outside"] >= 1
    assert extract.source_file == "t.osm"


def test_multipolygon_with_hole(tmp_path: Path):
    osm = OsmXml()
    osm.multipolygon(
        7,
        (10, square(500_020, 5_400_020, 100)),
        (11, square(500_040, 5_400_040, 20)),
        {"natural": "water"},
    )
    extract = read_osm(osm.write(tmp_path / "t.osm"), AREA, CRS)
    (area,) = [o for o in extract.objects if o.kind == "area"]
    assert area.osm_id == "r7"
    assert area.geometry.area == pytest.approx(100 * 100 - 20 * 20, rel=1e-3)


def test_broken_file(tmp_path: Path):
    bad = tmp_path / "bad.osm"
    bad.write_text("<osm><node id=", encoding="utf-8")
    with pytest.raises(OsmError):
        read_osm(bad, AREA, CRS)


def test_real_marktplatz_excerpt():
    area = BBox.around(501_115, 5_405_347, 60)
    extract = read_osm(OSM_EXCERPT, area, CRS)
    tags = [o.tags for o in extract.objects]
    assert any(t.get("place") == "square" and t.get("name") == "Marktplatz" for t in tags)
    fountain = next(o for o in extract.objects if o.tags.get("name") == "Marktbrunnen")
    # The OSM fountain lies within a few metres of the chosen origin (aerial image).
    origin = shapely.Point(501_115, 5_405_347)
    assert fountain.geometry.distance(origin) == pytest.approx(3.7, abs=1.0)
