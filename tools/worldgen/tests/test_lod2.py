from pathlib import Path

import pytest

from gothar_worldgen.geo.bbox import BBox, Tile
from gothar_worldgen.geo.lod2 import Lod2Error, files_for_area, find_lod2_files, read_lod2

from .citygml import building, flat_surfaces, part, saddle_surfaces, write_citygml

LGL_EXCERPT = Path(__file__).parent / "data" / "lgl_lod2" / "LoD2_32_501_5405_1_BW.gml"


def test_find_and_select_files(tmp_path: Path):
    for name in ["a/LoD2_32_1_2_1_BW.gml", "b/LoD2_32_2_2_1_BW.gml", "x/other.gml"]:
        write_citygml(tmp_path / name)
    assert set(find_lod2_files(tmp_path)) == {Tile(1000, 2000, 1000), Tile(2000, 2000, 1000)}
    files = files_for_area(tmp_path, BBox(1990, 2100, 2010, 2105))
    assert [f.name for f in files] == ["LoD2_32_1_2_1_BW.gml", "LoD2_32_2_2_1_BW.gml"]
    with pytest.raises(Lod2Error, match="3_2"):
        files_for_area(tmp_path, BBox(2990, 2100, 3010, 2105))


def test_read_simple_and_parted_buildings(tmp_path: Path):
    path = write_citygml(
        tmp_path / "LoD2_32_0_0_1_BW.gml",
        building("B1", saddle_surfaces(0, 0, 10, 8, 300, 306, 309), height=9.0),
        building(
            "B2",
            function="31001_2463",
            parts=[
                part("P1", flat_surfaces(20, 0, 5, 5, 300, 303), roof_type="1000", height=3.0),
                part("P2", saddle_surfaces(25, 0, 6, 5, 300, 304, 306)),
            ],
        ),
    )
    b1, b2 = list(read_lod2(path))

    assert (b1.id, b1.function, b1.has_parts, b1.source_file) == (
        "B1",
        "31001_1010",
        False,
        "LoD2_32_0_0_1_BW.gml",
    )
    (body,) = b1.bodies
    assert (body.id, body.roof_type, body.measured_height) == ("B1", "3100", 9.0)
    assert [s.kind for s in body.surfaces] == ["ground", "wall", "roof"]
    assert len(body.polygons("roof")) == 2
    ring = body.polygons("ground")[0].exterior
    assert ring.shape == (5, 3)
    assert tuple(ring[0]) == tuple(ring[-1]) == (0.0, 0.0, 300.0)

    assert b2.has_parts
    assert b2.function == "31001_2463"
    assert [(p.id, p.roof_type, p.measured_height) for p in b2.bodies] == [
        ("P1", "1000", 3.0),
        ("P2", "3100", None),
    ]


def test_invalid_xml(tmp_path: Path):
    bad = tmp_path / "LoD2_32_0_0_1_BW.gml"
    bad.write_text("<core:CityModel", encoding="utf-8")
    with pytest.raises(Lod2Error, match="invalid XML"):
        list(read_lod2(bad))


def test_broken_pos_list(tmp_path: Path):
    path = write_citygml(
        tmp_path / "LoD2_32_0_0_1_BW.gml",
        building("B1", saddle_surfaces(0, 0, 10, 8, 300, 306, 309)).replace(
            "0 0 300 0 8 300", "0 0 300 0 8"
        ),
    )
    with pytest.raises(Lod2Error, match="posList"):
        list(read_lod2(path))


def test_real_lgl_excerpt():
    buildings = {b.id: b for b in read_lod2(LGL_EXCERPT)}
    assert set(buildings) == {"DEBW_00100061ZkP", "DEBW_00100061ZkQ", "DEBW_00100061Zl2"}
    parted = buildings["DEBW_00100061ZkP"]
    assert parted.has_parts
    assert len(parted.bodies) == 3
    assert parted.function == "31001_3012"
    assert all(b.roof_type and b.polygons("roof") for b in parted.bodies)
    assert buildings["DEBW_00100061ZkQ"].bodies[0].roof_type == "3500"
