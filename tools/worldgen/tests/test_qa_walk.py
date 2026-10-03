import json

import pytest
from shapely.geometry import Polygon, box

from gothar_worldgen.qa.begehung import Body
from gothar_worldgen.qa.walk import (
    MIN_RUN_M,
    evaluate,
    gates_route,
    read_log,
    station_route,
    way_runs,
    ways_route,
    write_routes,
)

AREA = box(-100, -100, 100, 100)
HOUSE = Body("BLD_A", Polygon([(-5, -5), (5, -5), (5, 5), (-5, 5)]), 0.0, 8.0)


def test_station_route_runs_through_the_target():
    stations = [
        {"name": "START_BG_X", "x": 0.0, "z": 10.0, "target": [0.0, 0.0]},
        {"name": "START_BG_NO_TARGET", "x": 5.0, "z": 5.0},
    ]
    r = station_route(stations, beyond=6.0)
    names = [p["name"] for p in r["points"]]
    assert names == ["X_START", "X_ZIEL", "X_DAHINTER"]
    assert r["points"][0]["teleport"] is True
    assert r["points"][1]["screenshot"] is True
    assert r["points"][2]["pos"] == [0.0, -6.0]
    assert r["version"] == 1 and r["timeLimit"] > 0


def test_ways_are_cut_at_bodies_and_start_with_a_teleport():
    streets = [
        {"osmId": "w1", "highway": "footway", "points": [[-40.0, 0.0], [40.0, 0.0]]},  # through A
        {"osmId": "w2", "highway": "motorway", "points": [[-40.0, 20.0], [40.0, 20.0]]},
        {"osmId": "w3", "highway": "footway", "points": [[50.0, 50.0], [52.0, 50.0]]},  # short
        {"osmId": "w4", "highway": "footway", "tunnel": "yes", "points": [[0, 30], [30, 30]]},
    ]
    runs = way_runs(streets, [HOUSE], AREA)
    assert [w["osmId"] for w, _ in runs] == ["w1", "w1"]
    assert all(run.length >= MIN_RUN_M for _, run in runs)
    assert all(run.distance(HOUSE.poly) >= 0.3 for _, run in runs)
    r = ways_route(runs, step=8.0)
    teleports = [p for p in r["points"] if p.get("teleport")]
    assert len(teleports) == 2
    assert all(p.get("radius") == 1.0 for p in r["points"] if not p.get("teleport"))
    assert r["points"][0]["name"].startswith("W0000_w1_")


def test_gates_route_goes_straight_through_and_back():
    ring = [[-50.0, -50.0], [50.0, -50.0], [50.0, 50.0], [-50.0, 50.0]]  # ccw square
    gates = [{"key": "tor", "at": [0.0, -50.0]}]
    r = gates_route(ring, gates, [], reach=8.0)
    pts = {p["name"]: p["pos"] for p in r["points"]}
    assert pts["TOR_INNEN"] == pytest.approx([0.0, -42.0])  # nearer the origin
    assert pts["TOR_DURCH"] == pytest.approx([0.0, -50.0])
    assert pts["TOR_AUSSEN"] == pytest.approx([0.0, -58.0])
    assert pts["TOR_ZURUECK"] == pts["TOR_INNEN"]
    # a house on the line in: the inside point moves onto the gate's street
    house = Body("BLD_H", Polygon([(-3, -46), (3, -46), (3, -43), (-3, -43)]), 0.0, 8.0)
    streets = [
        {
            "osmId": "w",
            "name": "Gasse",
            "highway": "residential",
            "points": [[0.0, -60.0], [0.0, -49.0], [-10.0, -40.0]],
        }
    ]
    r = gates_route(ring, [{"key": "tor", "at": [0.0, -50.0], "street": "Gasse"}], [house], streets)
    inside = next(p["pos"] for p in r["points"] if p["name"] == "TOR_INNEN")
    assert inside[0] < -3.0  # off the straight line, along the street


def test_evaluate_matches_static_findings(tmp_path):
    route = {
        "points": [
            {"name": "W0001_w7_01", "pos": [10.0, 10.0]},
            {"name": "P_X", "pos": [50.0, 50.0]},
        ]
    }
    events = [
        {"event": "start", "t": 0.0},
        {
            "event": "stuck",
            "t": 3.0,
            "name": "W0001_w7_01",
            "pos": [30.0, 1.0, 30.0],
            "vob": "terrain",
            "state": "ground",
        },
        {"event": "stuck", "t": 5.0, "name": "P_X", "pos": [49.0, 2.0, 50.0], "vob": "BLD_B_1"},
        {"event": "slide_start", "t": 6.0, "pos": [0.0, 0.0, 0.0]},
        {"event": "slide_end", "t": 6.02, "pos": [0.0, 0.0, 0.0]},  # a graze: dropped
        {"event": "slide_start", "t": 7.0, "pos": [80.0, 5.0, 80.0]},
        {"event": "slide_end", "t": 8.0, "pos": [80.0, 4.0, 80.0]},
        {"event": "fall", "t": 9.0, "height": 3.0, "damage": 0.0, "water": True, "pos": [0, 0, 0]},
    ]
    static = {
        "lanes": {"list": [{"kind": "blocked", "osmId": "w9", "at": [50.0, 51.0]}]},
        "slopes": {
            "list": [{"kind": "steep", "osmId": "w7", "at": [-70.0, -70.0]}],
            "steps": {"list": []},
        },
    }
    rep = evaluate(route, events, {"reached": 0}, static)
    kinds = [p["event"] for p in rep["problems"]]
    assert kinds == ["stuck", "stuck", "slide"]
    assert rep["problems"][0]["static"] == "slope:steep"  # same OSM way
    assert rep["problems"][1]["static"] == "lane:blocked"  # within 3 m
    assert rep["problems"][2]["seconds"] == 1.0 and "static" not in rep["problems"][2]
    assert rep["confirmed"] == 2 and rep["new"] == 1
    assert rep["stuckBy"] == {"terrain": 1, "BLD": 1}
    out = tmp_path / "run"
    out.mkdir()
    (out / "walk.jsonl").write_text("\n".join(json.dumps(e) for e in events) + "\n", "utf-8")
    (out / "walk_summary.json").write_text(json.dumps({"points": 2}), "utf-8")
    ev, summary = read_log(out)
    assert len(ev) == len(events) and summary == {"points": 2}


def test_write_routes(tmp_path):
    counts = write_routes(
        tmp_path / "walk",
        [{"name": "START_BG_X", "x": 20.0, "z": 20.0, "target": [30.0, 20.0]}],
        [{"osmId": "w1", "highway": "path", "points": [[-40.0, 30.0], [40.0, 30.0]]}],
        [HOUSE],
        [[-50.0, -50.0], [50.0, -50.0], [50.0, 50.0], [-50.0, 50.0]],
        [{"key": "tor", "at": [0.0, -50.0]}],
        100.0,
    )
    assert counts == {"stations": 3, "ways": 11, "gates": 4}
    for name in counts:
        doc = json.loads((tmp_path / "walk" / f"{name}.json").read_text("utf-8"))
        assert doc["version"] == 1 and doc["gait"] == "run" and doc["points"]
