"""Only hamlets outside the town wall (W6): which buildings stay."""

import json
from pathlib import Path

from gothar_worldgen.buildings.weiler import parent_id, select_weiler

RULES = Path(__file__).resolve().parents[1] / "data" / "building_rules.json"
SPEC = json.loads(RULES.read_text(encoding="utf-8"))["weiler"]
RING = [(0.0, 0.0), (100.0, 0.0), (100.0, 100.0), (0.0, 100.0)]
GATE = (100.0, 50.0)
ROAD = {"highway": "residential", "points": [[100.0, 50.0], [700.0, 50.0]]}  # out of the gate
RIVER = {"geometry": "line", "kind": "stream", "name": "Glems",
         "points": [[-700.0, -150.0], [800.0, -150.0]]}  # fmt: skip


def _house(bid: str, x: float, z: float, size: float = 12.0) -> dict:
    return {"id": bid, "footprint": [[x, z], [x + size, z], [x + size, z + size], [x, z + size]]}


def _select(entries: list[dict], uses: tuple[str, ...] = ()):  # noqa: ANN202
    return select_weiler(entries, RING, [GATE], [ROAD], [RIVER], uses, SPEC)


def test_houses_inside_the_wall_stay_and_the_suburb_goes():
    w = _select([_house("IN", 40.0, 40.0), _house("WALL", 95.0, 20.0),
                 _house("SUBURB", 20.0, 130.0)])  # fmt: skip
    assert w.inside == 2
    assert w.dropped == {"SUBURB"}
    assert w.keeps("IN") and w.keeps("WALL") and not w.keeps("SUBURB")


def test_farms_by_the_gate_road_keep_their_spacing_and_distance_from_the_wall():
    houses = [_house(f"F{i}", 100.0 + x, 55.0) for i, x in enumerate((30.0, 120.0, 200.0, 260.0,
                                                                      400.0))]  # fmt: skip
    houses.append(_house("FAR", 300.0, 300.0))  # off the road
    w = _select(houses)
    assert "F0" in w.dropped  # 30 m before the wall: too close
    kept = sorted(w.farms)
    assert kept and "FAR" in w.dropped
    xs = sorted(next(h for h in houses if h["id"] == k)["footprint"][0][0] for k in kept)
    assert all(b - a >= SPEC["farm"]["spacingM"] for a, b in zip(xs, xs[1:], strict=False))


def test_mills_stand_by_the_river_only():
    w = _select([_house("MILL", 300.0, -165.0, 14.0), _house("MILL2", 360.0, -165.0, 10.0),
                 _house("DRY", 300.0, -260.0, 14.0)])  # fmt: skip
    assert w.mills == ["MILL"]  # the larger one; the other too close to it
    assert {"MILL2", "DRY"} <= w.dropped


def test_gate_houses_of_the_uses_stay_only_with_their_named_parts():
    w = _select([_house("GUARD", 105.0, 60.0), _house("ROW", 110.0, 20.0)],
                uses=("GUARD", "ROW-T1"))  # fmt: skip
    assert not w.dropped
    assert w.keeps("GUARD") and w.keeps("ROW-T1")
    assert not w.keeps("ROW-T2") and not w.keeps("ROW")
    assert parent_id("ROW-T2") == "ROW"
    rows = [{"id": "ROW-T1"}, {"id": "ROW-T2"}, {"id": "GUARD"}]
    assert [e["id"] for e in w.filter(rows)] == ["ROW-T1", "GUARD"]


def test_the_selection_is_the_same_every_run():
    houses = [_house(f"H{i}", 100.0 + 15.0 * i, 40.0 + (i % 3) * 20.0) for i in range(30)]
    assert vars(_select(houses)) == vars(_select(list(reversed(houses))))


def test_halls_and_structures_are_no_farms():
    hall = _house("HALL", 300.0, 55.0, 30.0)  # 900 m2: a school or a factory
    bridge = {**_house("BRIDGE", 500.0, 55.0), "function": "53001_1800"}
    w = _select([hall, bridge])
    assert not w.farms and w.dropped == {"HALL", "BRIDGE"}


def test_a_farm_cut_by_the_rueckbau_keeps_its_two_largest_parts():
    w = _select([_house("FARM", 300.0, 55.0, 20.0)])
    assert w.farms == ["FARM"]
    parts = [_house(f"FARM-T{i}", 300.0 + 5.0 * i, 55.0, 4.0 + i) for i in range(1, 5)]
    assert [e["id"] for e in w.filter(parts)] == ["FARM-T3", "FARM-T4"]
