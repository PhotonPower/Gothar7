"""Hillside rule of the generated houses (walkthrough decision E1)."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from gothar_worldgen.buildings.medieval import (
    Opening,
    StreetIndex,
    _above_ground,
    build_house,
    load_rules,
    make_frame,
)

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
# 10 m x 7 m; edge 0 is the south side (z = 0), the street runs south of it.
RING = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
HOUSE = {"id": "H1", "groundY": 0.0, "footprint": RING, "roof": ROOF}
STREET_SOUTH = StreetIndex([{"points": [[-20.0, 5.0], [30.0, 5.0]]}])


def house(ground_at):  # noqa: ANN001, ANN201
    return build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=ground_at)


def test_without_terrain_nothing_changes():
    r = house(None)
    assert r.doors == [] and not any("floor" in n or "stairs" in n for n in r.notes)


def test_level_ground_keeps_the_street_door():
    r = house(lambda x, z: 0.05)
    ((x, z, floor, kind, nx, nz),) = r.doors
    assert z == pytest.approx(0.6) and 0.0 < x < 10.0  # in front of the south (street) side
    assert floor == 0.0 and kind == "ground" and (nx, nz) == (0.0, 1.0)


def test_ground_above_the_floor_lifts_it():
    r = house(lambda x, z: 0.5 * z)  # rises towards the street: 0.3 m at the door
    assert "floor raised to the ground at the door" in r.notes
    assert r.doors[0][2] == pytest.approx(0.3, abs=0.01)


def test_ground_far_below_the_door_gets_stairs():
    flat = house(lambda x, z: 0.0)
    r = house(lambda x, z: -1.0 if z > 0.0 else 0.0)  # the street side falls away by 1 m
    assert "stairs in front of the door" in r.notes and r.doors[0][3] == "stairs"
    assert len(r.collision.parts) == len(flat.collision.parts) + 1  # one body for the steps
    stone = lambda h: sum(p.mesh.triangle_count for p in h.primitives if p.material == "stone")  # noqa: E731
    assert stone(r) > stone(flat)


def test_steep_street_side_moves_the_door_off_the_street():
    # the street side lies higher than a ground storey fits under the eave; the north side fits
    r = house(lambda x, z: 9.0 if z > -1.0 else 0.0)
    assert "door moved off the street (slope)" in r.notes
    assert r.doors[0][1] == pytest.approx(-7.6)


def test_openings_under_the_terrain_are_dropped():
    f = make_frame((0.0, 0.0), (10.0, 0.0), (0.0, 1.0), 0.0)
    plan = [
        Opening("window", 1.0, 0.9, 0.8, 1.0),
        Opening("window", 7.0, 0.9, 0.8, 1.0),
        Opening("door", 4.5, 0.0, 1.0, 2.1),
    ]
    ctx = SimpleNamespace(ground_at=lambda x, z: 1.5 if x < 5.0 else 0.0)
    kept = _above_ground(ctx, f, plan)
    assert [o.kind for o in kept] == ["window", "door"] and kept[0].u == 7.0


def test_house_deep_in_the_slope_gets_an_upper_door():
    # eave 8.4 m, ground 5.6 m up all round: no ground storey fits above it (8.4 - 3.0 < 5.6),
    # but two storeys of 2.8 m below a door storey of 2.8 m do
    r = house(lambda x, z: 5.6)
    assert "door in an upper storey (hillside)" in r.notes
    x, z, floor, kind = r.doors[0][:4]
    assert kind == "upper" and floor == pytest.approx(5.6) and z == pytest.approx(0.6)


def test_no_room_for_an_upper_door_means_a_descent():
    r = house(lambda x, z: 8.2)  # the ground reaches the eave on every side
    assert "door below the ground (short descent)" in r.notes
    x, z, floor, kind = r.doors[0][:4]
    assert kind == "descent" and floor == pytest.approx(8.4 - RULES.get("storeys")["groundM"])
