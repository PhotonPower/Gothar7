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


def sill_near(r, x: float, z: float) -> int:  # noqa: ANN001
    """Stone vertices of a door sill within 1.5 m of (x, z), above the socle (origin
    (5.0, -0.5, -3.5)); a wall without a door has a window there instead."""
    import numpy as np

    pos = np.vstack([p.mesh.positions for p in r.primitives if p.material == "stone"])
    w = pos + np.array([5.0, -0.5, -3.5])
    near = (np.hypot(w[:, 0] - x, w[:, 2] - z) < 1.5) & (w[:, 1] > 0.2) & (w[:, 1] < 1.0)
    return int(near.sum())


def test_a_neighbour_in_front_moves_the_door_to_a_free_wall():
    # W6: a house wall to wall with the street side blocked; the east side is free
    def free(x: float, z: float) -> bool:
        return not (z > 0.0 and -2.0 < x < 12.0) and x < 10.0 + 9.0  # neighbour in front (south)

    r = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH,
                    ground_at=lambda x, z: 0.0, door_free=free)  # fmt: skip
    assert "door moved: street side blocked" in r.notes
    ((x, z, _, kind, nx, nz),) = r.doors
    assert kind == "ground" and nz <= 0.0  # not the south side any more
    assert free(x + nx * 0.6, z + nz * 0.6)


def test_the_door_turns_to_the_side_nearest_a_street():
    # south blocked, east and west free: the street runs along the west side
    west = StreetIndex([{"points": [[-5.0, 20.0], [-5.0, -30.0]]}])

    def free(x: float, z: float) -> bool:
        return z <= 0.0  # only the south is blocked

    r = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, west, ground_at=lambda x, z: 0.0,
                    door_free=free)  # fmt: skip
    ((x, _, _, _, nx, _),) = r.doors
    assert nx == pytest.approx(-1.0) and x < 0.0  # the west wall


def test_no_free_wall_keeps_the_floor_but_draws_no_door():
    plain = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0)
    r = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH,
                    ground_at=lambda x, z: 0.0, door_free=lambda x, z: False)  # fmt: skip
    assert "door without access (no free wall)" in r.notes
    ((x, z, floor, kind, _, _),) = r.doors
    assert kind == "blocked" and (x, z, floor) == tuple(plain.doors[0][:3])
    # no door frame on the shared wall (nothing pokes into the neighbour)
    assert sill_near(plain, x, z) > 0 and sill_near(r, x, z) == 0


def test_without_the_check_the_doors_stay():
    a = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0)
    b = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0,
                    door_free=lambda x, z: True)  # fmt: skip
    assert a.doors == b.doors and a.triangles == b.triangles


def test_a_wall_with_a_way_to_a_street_comes_first():
    # every wall is free, but only from the east side a street can be reached in a line
    r = build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0,
                    door_free=lambda x, z: True, door_reach=lambda x, z: x > 10.0)  # fmt: skip
    ((x, _, _, kind, nx, _),) = r.doors
    assert kind == "ground" and nx == pytest.approx(1.0) and x > 10.0
    assert "door moved: street side blocked" in r.notes
