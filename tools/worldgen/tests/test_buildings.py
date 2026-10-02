import json
import math
from pathlib import Path

import pytest
import shapely

from gothar_worldgen.config import load_site
from gothar_worldgen.geo.buildings import (
    LocalFrame,
    buildings_document,
    convert_building,
    select_and_convert,
    summarize,
    write_buildings_json,
)
from gothar_worldgen.geo.lod2 import read_lod2

from .citygml import (
    building,
    flat_roof,
    flat_surfaces,
    ground_surface,
    part,
    saddle_surfaces,
    surface,
    write_citygml,
)

LGL_EXCERPT = Path(__file__).parent / "data" / "lgl_lod2" / "LoD2_32_501_5405_1_BW.gml"

# Local frame with origin at (1000, 2000), 300 m NHN; buildings below are placed around it.
FRAME = LocalFrame(1000.0, 2000.0, 300.0, 1.0, 1.0)


def read_one(tmp_path: Path, xml: str):
    path = write_citygml(tmp_path / "LoD2_32_1_2_1_BW.gml", xml)
    (b,) = list(read_lod2(path))
    return b


def shoelace_north_up(ring: list[list[float]]) -> float:
    """Signed area in map orientation (x east, -z north): > 0 means counter-clockwise."""
    pts = [(x, -z) for x, z in ring]
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1], strict=True))


def test_saddle_roof_building(tmp_path: Path):
    # 10 m (east) x 8 m (north) box at E 1000-1010, N 2000-2008; ground 301, eave 307, ridge 310.
    b = read_one(
        tmp_path, building("B1", saddle_surfaces(1000, 2000, 10, 8, 301, 307, 310), height=9.0)
    )
    e = convert_building(b, FRAME)

    assert e["id"] == "B1"
    assert e["function"] == "31001_1010"
    assert sorted(map(tuple, e["footprint"])) == [(0, -8), (0, 0), (10, -8), (10, 0)]
    assert shoelace_north_up(e["footprint"]) > 0
    assert e["areaM2"] == 80.0
    assert e["groundY"] == 1.0
    assert e["heightM"] == 9.0
    roof = e["roof"]
    assert (roof["type"], roof["alkis"]) == ("saddle", "3100")
    assert (roof["eaveY"], roof["ridgeY"]) == (7.0, 10.0)
    assert roof["ridgeDir"] == [1.0, 0.0]  # ridge runs east-west
    assert roof["pitchDeg"] == pytest.approx(math.degrees(math.atan(3 / 4)), abs=0.1)
    assert "parts" not in e
    assert "warnings" not in e
    assert "inCore" not in e


def test_ridge_direction_north_south_is_canonical(tmp_path: Path):
    # 8 m (east) x 10 m (north) house whose ridge runs north-south at E 1004.
    eave, ridge = 306, 309
    roof = surface(
        "RoofSurface",
        [(1000, 2000, eave), (1004, 2000, ridge), (1004, 2010, ridge), (1000, 2010, eave)],
        [(1004, 2000, ridge), (1008, 2000, eave), (1008, 2010, eave), (1004, 2010, ridge)],
    )
    b = read_one(tmp_path, building("B1", ground_surface(1000, 2000, 8, 10, 300) + roof))
    # The ridge edge may be stored southwards or northwards; the output is canonical.
    assert convert_building(b, FRAME)["roof"]["ridgeDir"] == [0.0, 1.0]


def test_flat_roof_has_no_ridge(tmp_path: Path):
    b = read_one(
        tmp_path, building("F", flat_surfaces(1000, 2000, 6, 6, 300, 303), roof_type="1000")
    )
    roof = convert_building(b, FRAME)["roof"]
    assert roof["type"] == "flat"
    assert roof["ridgeDir"] is None
    assert roof["eaveY"] == roof["ridgeY"] == 3.0
    assert roof["pitchDeg"] == 0.0


def test_unknown_roof_type_code(tmp_path: Path):
    b = read_one(
        tmp_path, building("U", flat_surfaces(1000, 2000, 6, 6, 300, 303), roof_type="1234")
    )
    assert convert_building(b, FRAME)["roof"]["type"] == "unknown"


def test_parts_are_merged_and_listed(tmp_path: Path):
    xml = building(
        "B",
        parts=[
            part("small", flat_surfaces(1000, 2000, 4, 8, 300, 303), roof_type="1000"),
            part("main", saddle_surfaces(1004, 2000, 10, 8, 300.5, 306, 309), height=8.5),
        ],
    )
    e = convert_building(read_one(tmp_path, xml), FRAME)

    assert e["areaM2"] == 112.0  # 14 m x 8 m, parts share an edge
    assert len(e["footprint"]) == 4  # shared edge dissolved
    assert e["groundY"] == 0.0  # lowest part
    assert e["heightM"] == 9.0
    assert e["roof"]["type"] == "saddle"  # roof of the largest part
    assert [p["id"] for p in e["parts"]] == ["small", "main"]
    assert e["parts"][0]["roof"]["type"] == "flat"
    assert e["parts"][1]["areaM2"] == 80.0
    assert e["parts"][1]["groundY"] == 0.5
    assert e["parts"][1]["heightM"] == 8.5
    assert "warnings" not in e


def test_tiny_gap_between_parts_is_closed(tmp_path: Path):
    xml = building(
        "B",
        parts=[
            part("a", flat_surfaces(1000, 2000, 4, 8, 300, 303), roof_type="1000"),
            part("b", flat_surfaces(1004.03, 2000, 4, 8, 300, 303), roof_type="1000"),
        ],
    )
    e = convert_building(read_one(tmp_path, xml), FRAME)
    assert "warnings" not in e
    assert e["areaM2"] == pytest.approx(64.2, abs=0.2)


def test_separate_pieces_keep_largest_with_warning(tmp_path: Path):
    xml = building(
        "B",
        parts=[
            part("big", flat_surfaces(1000, 2000, 10, 10, 300, 303), roof_type="1000"),
            part("far", flat_surfaces(1020, 2000, 2, 2, 300, 303), roof_type="1000"),
        ],
    )
    e = convert_building(read_one(tmp_path, xml), FRAME)
    assert e["areaM2"] == 100.0
    assert any("2 separate pieces" in w for w in e["warnings"])


def test_height_mismatch_and_missing_roof_warn(tmp_path: Path):
    xml = building("B", saddle_surfaces(1000, 2000, 10, 8, 300, 306, 309), height=5.0)
    assert any(
        "measuredHeight" in w for w in convert_building(read_one(tmp_path, xml), FRAME)["warnings"]
    )

    no_roof = ground_surface(1000, 2000, 6, 6, 300)
    e = convert_building(read_one(tmp_path, building("N", no_roof)), FRAME)
    assert e["roof"] is None
    assert any("no roof" in w for w in e["warnings"])


def test_footprint_from_roof_when_ground_missing(tmp_path: Path):
    roof_only = flat_roof(1000, 2000, 6, 6, 303)
    e = convert_building(read_one(tmp_path, building("R", roof_only, roof_type="1000")), FRAME)
    assert e["areaM2"] == 36.0
    assert any("roof projection" in w for w in e["warnings"])


def test_scale_is_applied(tmp_path: Path):
    frame = LocalFrame(1000.0, 2000.0, 300.0, 2.0, 0.5)
    b = read_one(tmp_path, building("B", saddle_surfaces(1000, 2000, 10, 8, 302, 306, 310)))
    e = convert_building(b, frame)
    assert e["areaM2"] == 320.0
    assert max(x for x, _ in e["footprint"]) == 20.0
    assert (e["groundY"], e["roof"]["ridgeY"]) == (1.0, 5.0)
    # 4 m rise over 4 m run, scaled to 2 m rise over 8 m run.
    assert e["roof"]["pitchDeg"] == pytest.approx(math.degrees(math.atan(2 / 8)), abs=0.1)


def test_select_filters_area_dedupes_and_flags_core(tmp_path: Path):
    path = write_citygml(
        tmp_path / "LoD2_32_1_2_1_BW.gml",
        building("IN_CORE", flat_surfaces(1001, 2001, 4, 4, 300, 303), roof_type="1000"),
        building("OUTER", flat_surfaces(1050, 2001, 4, 4, 300, 303), roof_type="1000"),
        building("OUTSIDE", flat_surfaces(1500, 2001, 4, 4, 300, 303), roof_type="1000"),
    )
    buildings = list(read_lod2(path)) * 2  # duplicates (e.g. from overlapping files)
    area = shapely.box(900, 1900, 1100, 2100)
    core = shapely.box(990, 1990, 1010, 2010)
    entries = select_and_convert(buildings, FRAME, area, core)
    assert [(e["id"], e["inCore"]) for e in entries] == [("IN_CORE", True), ("OUTER", False)]
    assert summarize(entries) == {
        "buildings": 2,
        "inCore": 1,
        "withParts": 0,
        "withWarnings": 0,
        "roofTypes": {"flat": 2},
    }


def test_json_document_round_trip(tmp_path: Path, config_dir: Path):
    site = load_site("testsite", config_dir)
    b = read_one(tmp_path, building("B1", saddle_surfaces(1000, 2000, 10, 8, 301, 307, 310)))
    entries = [convert_building(b, FRAME)]
    doc = buildings_document(entries, site, FRAME, ["LoD2_32_1_2_1_BW.gml"])
    out = tmp_path / "work" / "buildings.json"
    write_buildings_json(out, doc)

    text = out.read_text(encoding="utf-8")
    assert json.loads(text) == doc
    assert text.count("\n") == len(text.splitlines())
    assert '    {"id":"B1"' in text  # one compact line per building
    assert doc["source"]["credit"].startswith("Datengrundlage: LGL")
    assert doc["count"] == 1

    write_buildings_json(out, buildings_document([], site, FRAME, []))
    assert json.loads(out.read_text(encoding="utf-8"))["buildings"] == []


def test_real_marktplatz_buildings():
    frame = LocalFrame(501115.0, 5405347.0, 386.89, 1.0, 1.0)  # Leonberg origin
    entries = {e["id"]: e for e in (convert_building(b, frame) for b in read_lod2(LGL_EXCERPT))}

    saddle = entries["DEBW_00100061Zl2"]
    assert saddle["roof"]["type"] == "saddle"
    assert 15 < saddle["heightM"] < 20
    assert saddle["roof"]["ridgeDir"][0] > 0.95  # ridge roughly east-west, as in the aerial image
    assert 25 < saddle["roof"]["pitchDeg"] < 45

    parted = entries["DEBW_00100061ZkP"]
    assert len(parted["parts"]) == 3
    assert parted["areaM2"] >= max(p["areaM2"] for p in parted["parts"])

    tent = entries["DEBW_00100061ZkQ"]
    assert tent["roof"]["type"] == "tent"
    assert tent["roof"]["ridgeDir"] is None

    for e in entries.values():
        assert shoelace_north_up(e["footprint"]) > 0
        assert -3 < e["groundY"] < 3  # the market square is near the origin height
        assert e["roof"]["ridgeY"] > e["roof"]["eaveY"] or e["roof"]["type"] == "flat"
