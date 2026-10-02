import json
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.qa.checks import (
    FAIL,
    OK,
    WARN,
    overall_status,
    report_document,
    roads_in_buildings_share,
    run_checks,
)
from gothar_worldgen.qa.preview import View, hillshade, render, write_previews
from gothar_worldgen.qa.workdata import QaError, WorkData, load_work

ORIGIN = {"crs": "EPSG:25832", "easting": 1000.0, "northing": 2000.0, "heightNHN": 300.0}


def terrain_meta(size: int = 40, min_y: float = -1.0, max_y: float = 1.0) -> dict:
    half = size / 2
    rect = {"minX": -half, "minZ": -half, "maxX": half, "maxZ": half}
    core = {k: v / 2 for k, v in rect.items()}
    return {
        "width": size,
        "height": size,
        "cellSize": 1.0,
        "firstSample": {"x": -half + 0.5, "z": -half + 0.5},
        "heightRange": {"minY": min_y, "maxY": max_y, "stepM": (max_y - min_y) / 65535},
        "areas": {"core": core, "surroundings": rect},
        "origin": ORIGIN | {"heightReference": "test"},
    }


def box(x0: float, z0: float, x1: float, z1: float) -> list[list[float]]:
    return [[x0, z0], [x1, z0], [x1, z1], [x0, z1]]


def make_data(**overrides) -> WorkData:
    """Flat terrain (y = 0), one core building, one street beside it, one tree."""
    data = {
        "terrain": terrain_meta(),
        "heights": np.zeros((40, 40), dtype=np.float32),
        "buildings": [
            {
                "id": "B1",
                "inCore": True,
                "footprint": box(-4, -4, 4, 4),
                "groundY": 0.05,
                "heightM": 9.0,
                "roof": {"type": "saddle"},
            },
        ],  # fmt: skip
        "streets": [
            {"osmId": "w1", "class": "road", "widthM": 5.5, "points": [[-15, 8], [15, 8]]},
        ],
        "squares": [{"osmId": "w2", "polygon": box(6, -6, 12, -2)}],
        "features": [
            {"osmId": "n1", "type": "tree", "geometry": "point", "position": [10.0, 10.0]},
            {"osmId": "w3", "type": "wall", "geometry": "line", "points": [[-10, 12], [0, 12]]},
        ],
        "origins": {name: dict(ORIGIN) for name in ("terrain.json", "buildings.json")},
    }
    data.update(overrides)
    return WorkData(**data)


def statuses(data: WorkData) -> dict[str, str]:
    return {r.id: r.status for r in run_checks(data)}


def test_consistent_data_passes():
    results = run_checks(make_data())
    assert {r.status for r in results} == {OK}, [r for r in results if r.status != OK]
    assert overall_status(results) == OK


def test_origin_outside_terrain_fails():
    data = make_data(terrain=terrain_meta(min_y=5.0, max_y=20.0))
    assert statuses(data)["terrain.originInRange"] == FAIL


def test_coarse_height_steps_warn():
    data = make_data(terrain=terrain_meta(min_y=-1000.0, max_y=1000.0))
    assert statuses(data)["terrain.resolution"] == WARN


def test_different_origins_fail():
    origins = {"terrain.json": ORIGIN, "buildings.json": ORIGIN | {"heightNHN": 301.0}}
    s = statuses(make_data(origins=origins))
    assert s["layers.sameOrigin"] == FAIL


def test_reference_height_label_is_ignored_for_origin_comparison():
    origins = {"terrain.json": ORIGIN | {"heightReference": "x"}, "buildings.json": ORIGIN}
    assert statuses(make_data(origins=origins))["layers.sameOrigin"] == OK


@pytest.mark.parametrize(("ground", "expected"), [(0.3, OK), (1.5, WARN), (3.0, FAIL)])
def test_building_ground_vs_terrain(ground, expected):
    data = make_data()
    data.buildings[0]["groundY"] = ground
    s = statuses(data)
    assert s["buildings.groundVsTerrain"] == expected
    assert s["buildings.groundOutliers"] == (WARN if ground > 2 else OK)


def test_building_counts_and_heights():
    assert statuses(make_data(buildings=[]))["buildings.count"] == FAIL
    data = make_data()
    data.buildings[0]["inCore"] = False
    data.buildings[0]["heightM"] = 300.0
    data.buildings[0]["warnings"] = ["something"]
    s = statuses(data)
    assert (s["buildings.count"], s["buildings.heights"], s["buildings.warnings"]) == (
        WARN,
        WARN,
        WARN,
    )


def test_roads_through_buildings():
    data = make_data()
    assert roads_in_buildings_share(data) == 0.0
    # 30 m road, 8 m of it inside the building -> 27 %.
    data.streets[0]["points"] = [[-15, 0], [15, 0]]
    assert roads_in_buildings_share(data) == pytest.approx(8 / 30)
    assert statuses(data)["alignment.roadsInBuildings"] == FAIL
    # Tunnels and paths do not count.
    data.streets[0]["tunnel"] = True
    assert roads_in_buildings_share(data) == 0.0


def test_osm_outside_area_and_odd_widths():
    data = make_data()
    data.features[0]["position"] = [25.0, 0.0]
    data.streets[0]["widthM"] = 60.0
    s = statuses(data)
    assert (s["osm.withinArea"], s["streets.widths"]) == (FAIL, WARN)
    assert statuses(make_data(streets=[]))["streets.count"] == FAIL


def test_report_document():
    results = run_checks(make_data())
    doc = report_document("testsite", results)
    assert (doc["format"], doc["site"], doc["status"]) == ("gothar-report", "testsite", OK)
    assert {c["id"] for c in doc["checks"]} >= {"terrain.resolution", "buildings.count"}
    json.dumps(doc)  # serialisable
    assert overall_status([]) == OK


def test_sample_heights_nearest_and_outside():
    heights = np.arange(16, dtype=np.float32).reshape(4, 4)
    data = make_data(terrain=terrain_meta(size=4), heights=heights)
    # Sample (row 1, col 2) has its centre at x = 0.5, z = -0.5.
    assert data.sample_heights([0.5, 0.6], [-0.5, -0.4]).tolist() == [6.0, 6.0]
    assert np.isnan(data.sample_heights([5.0], [0.0])[0])


def test_hillshade_shape_and_view_mapping():
    h = np.random.default_rng(1).random((8, 6)).astype(np.float32)
    assert hillshade(h, 1.0).shape == (8, 6, 3)
    v = View(-10, -20, 10, 20, 0.5)
    assert v.size == (40, 80)
    assert v.px(0, 0) == (20.0, 40.0)


def test_render_and_write_previews(tmp_path: Path):
    data = make_data()
    img = render(data, View(-100, -100, 100, 100, 0.25))
    assert img.size == (800, 800)
    # The building centre (0, 0) is drawn in the core building colour (legend is top left).
    assert img.getpixel((400, 410)) == (178, 74, 52)
    # Outside the 40 m x 40 m terrain the view is filled with neutral grey.
    assert img.getpixel((790, 790)) == (200, 200, 200)
    paths = write_previews(data, tmp_path)
    assert [p.name for p in paths] == ["preview.png", "preview_core.png"]
    assert all(p.stat().st_size > 0 for p in paths)


def test_load_work_errors(tmp_path: Path):
    with pytest.raises(QaError, match="terrain.json not found"):
        load_work(tmp_path)
    (tmp_path / "terrain.json").write_text(json.dumps(terrain_meta(size=4)), encoding="utf-8")
    with pytest.raises(QaError, match="terrain.r16 not found"):
        load_work(tmp_path)
    np.zeros(5, dtype="<u2").tofile(tmp_path / "terrain.r16")
    with pytest.raises(QaError, match="expected 4 x 4"):
        load_work(tmp_path)
    np.zeros(16, dtype="<u2").tofile(tmp_path / "terrain.r16")
    (tmp_path / "buildings.json").write_text("{", encoding="utf-8")
    with pytest.raises(QaError, match="invalid JSON"):
        load_work(tmp_path)
