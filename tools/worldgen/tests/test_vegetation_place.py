"""Vegetation on areas (W6 vegetation): gardens, meadows, the formal garden's hedges."""

import json
from pathlib import Path

import pytest
from shapely.geometry import LineString, Point, Polygon, box

from gothar_worldgen.outdoor import OutdoorError, Rules, Site, plan_outdoor
from gothar_worldgen.vegetation_place import VegRules

DATA = Path(__file__).resolve().parents[1] / "data" / "leonberg"
HOUSE = box(0.0, 0.0, 10.0, 8.0)
WALL = box(-40.0, -40.0, 60.0, -39.0)  # a piece of town wall: the old town reaches round it
STREET = {"highway": "residential", "widthM": 6.0, "points": [[-40.0, -5.0], [60.0, -5.0]],
          "osmId": "w1"}  # fmt: skip
GARDEN = {"osmId": "w2", "kind": "garden", "geometry": "polygon",
          "polygon": [[15.0, 2.0], [45.0, 2.0], [45.0, 30.0], [15.0, 30.0]]}  # fmt: skip
FORMAL = {"osmId": "w3", "kind": "garden", "geometry": "polygon", "name": "Pomeranzengarten",
          "polygon": [[-40.0, 5.0], [-10.0, 5.0], [-10.0, 35.0], [-40.0, 35.0]]}  # fmt: skip
PATH = {"highway": "footway", "widthM": 2.0, "points": [[-40.0, 20.0], [-10.0, 20.0]],
        "osmId": "w4"}  # fmt: skip


def _outdoor() -> Rules:
    data = json.loads((DATA / "outdoor.json").read_text(encoding="utf-8"))
    for key in ("kerb", "market"):
        data.pop(key, None)
    data["trees"]["osm"] = False
    data["houses"]["front"]["chance"] = data["houses"]["back"]["chance"] = 0.0
    data["yards"]["fruitTree"]["chance"] = data["yards"]["bush"]["chance"] = 0.0
    data["grass"]["chance"] = 0.0
    return Rules(data)


def _veg(**over) -> VegRules:
    data = json.loads((DATA / "vegetation.json").read_text(encoding="utf-8"))
    data.update(over)
    return VegRules(data)


def _site() -> Site:
    return Site(entries=[{"id": "A", "doors": [[5.0, 0.0, 0.0, "ground", 0.0, -1.0]]}],
                uses={}, places=[], bodies=[("BLD_A", HOUSE), ("CITYWALL_1", WALL)],
                streets=[STREET, PATH], features=[GARDEN, FORMAL],
                height_at=lambda x, z: 0.0, ways=[((5.0, -2.0), (5.0, 40.0))])  # fmt: skip


def _plan(veg: VegRules):  # noqa: ANN202
    return plan_outdoor(_outdoor(), _site(), veg)


def _at(v) -> Point:  # noqa: ANN001
    return Point(v["pos"][0], v["pos"][2])


def test_gardens_get_trees_bushes_and_grass_clear_of_the_masks():
    plan = _plan(_veg())
    garden = [v for v in plan.vobs if v["name"].startswith("VEG_GARTEN_W2")]
    kinds = {v["mesh"].split("/")[-1][:-4] for v in garden}
    assert {"tree_fruit", "grass_patch"} <= kinds
    inside = Polygon(GARDEN["polygon"])
    lane = LineString([(5.0, -2.0), (5.0, 40.0)]).buffer(1.0)  # the waynet edge
    for v in garden:
        p = _at(v)
        assert inside.contains(p) and not lane.contains(p)
        assert HOUSE.distance(p) > 0.9
    assert plan.vobs == _plan(_veg()).vobs  # the same every run


def test_the_formal_garden_has_hedges_along_its_path_and_beds_beside():
    plan = _plan(_veg())
    hedges = [v for v in plan.vobs if v["name"].startswith("HECKE_")]
    assert len(hedges) >= 2 * 3  # both sides, runs of at least three pieces
    path = LineString(PATH["points"])
    for v in hedges:
        d = path.distance(_at(v))
        assert 1.0 + 0.4 <= d <= 1.0 + 1.1  # beside the path, off its edge
        assert v["mesh"] == "vegetation/hedge_box.glb"
    beds = [v for v in plan.vobs if v["name"].startswith("VEG_FORMAL")]
    assert beds and all(path.distance(_at(v)) > 1.5 for v in beds)
    # no other area rule fills the formal garden
    assert not any(v["name"].startswith("VEG_GARTEN_W3") for v in plan.vobs)


def test_short_runs_of_hedge_are_left_out():
    formal = _veg().data["formal"]
    plan = _plan(_veg(formal={**formal, "hedge": {**formal["hedge"], "minRun": 99}}))
    assert not [v for v in plan.vobs if v["name"].startswith("HECKE_")]


def test_areas_reach_only_round_the_old_town():
    plan = _plan(_veg(reachM=5.0))  # the garden lies 40 m from the wall
    assert not [v for v in plan.vobs if v["name"].startswith("VEG_GARTEN_W2")]


def test_unknown_models_are_refused(tmp_path: Path):
    data = json.loads((DATA / "vegetation.json").read_text(encoding="utf-8"))
    data["areas"][0]["layers"][0]["pick"] = {"tree_palm": 1}
    path = tmp_path / "vegetation.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(OutdoorError):
        VegRules.load(path)
    assert VegRules.load(DATA / "vegetation.json").data["version"] == 1


def _site_with(features: list, height=lambda x, z: 0.0) -> Site:  # noqa: ANN001
    return Site(entries=[{"id": "A", "doors": [[5.0, 0.0, 0.0, "ground", 0.0, -1.0]]}],
                uses={}, places=[], bodies=[("BLD_A", HOUSE), ("CITYWALL_1", WALL)],
                streets=[STREET], features=features, height_at=height)  # fmt: skip


def _only(**keep) -> VegRules:
    data = json.loads((DATA / "vegetation.json").read_text(encoding="utf-8"))
    for key in ("areas", "formal", "banks", "wall", "slopes"):
        if key not in keep:
            data.pop(key, None)
    data.update(keep)
    return VegRules(data)


def test_reed_and_bushes_line_the_river_banks():
    river = {"osmId": "w9", "kind": "stream", "geometry": "line", "name": "Glems",
             "points": [[-40.0, -60.0], [60.0, -60.0]]}  # fmt: skip
    plan = plan_outdoor(_outdoor(), _site_with([river]), _only(banks=_veg().data["banks"]))
    banks = [v for v in plan.vobs if v["name"].startswith("VEG_UFER")]
    spec = _veg().data["banks"]
    assert banks and any(v["mesh"].endswith("reed.glb") for v in banks)
    axis = LineString(river["points"])
    assert all(spec["fromM"] <= axis.distance(_at(v)) <= spec["toM"] for v in banks)


def test_a_band_outside_the_town_wall_keeps_its_foot_free():
    spec = _veg().data["wall"]
    plan = plan_outdoor(_outdoor(), _site_with([]), _only(wall=spec))
    band = [v for v in plan.vobs if v["name"].startswith("VEG_MAUER")]
    assert band
    hull = WALL.convex_hull
    assert all(spec["fromM"] <= hull.distance(_at(v)) <= spec["toM"] for v in band)


def test_scrub_and_grass_grow_on_steep_ground_only():
    def height(x: float, z: float) -> float:  # flat, then 30 degrees up south of z = -20
        return 0.0 if z < -20.0 else (z + 20.0) * 0.577

    plan = plan_outdoor(_outdoor(), _site_with([], height), _only(slopes=_veg().data["slopes"]))
    hang = [v for v in plan.vobs if v["name"].startswith("VEG_HANG")]
    assert hang and all(v["pos"][2] > -21.5 for v in hang)
