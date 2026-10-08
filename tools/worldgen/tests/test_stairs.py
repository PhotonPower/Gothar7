import math

import numpy as np
import pytest
from shapely.geometry import LineString, box

from gothar_worldgen.buildings.collision import body_is_closed
from gothar_worldgen.buildings.stairs import plan_stair, ramp_body, stair_from_json

ROOM = box(0.0, 0.0, 7.0, 6.0)  # counter-clockwise from shapely's box
DOOR = (3.5, 0.0)  # in the middle of the south wall


def test_a_flight_along_a_wall_at_most_35_degrees():
    st = plan_stair(ROOM, ROOM, 0.0, 3.0, [box(2.5, -1.0, 4.5, 1.6)], [], DOOR)
    assert st is not None
    assert math.degrees(math.atan(st.rise / st.tread)) <= 35.0
    assert st.steps * st.rise == pytest.approx(3.0)
    inside = ROOM.buffer(0.01)
    assert inside.contains(st.footprint.union(st.foot_landing)) and inside.contains(st.head_landing)
    assert st.footprint.distance(box(2.5, -1.0, 4.5, 1.6)) > 0.0  # off the door's way in
    assert st.footprint.distance(LineString([(0, 6), (7, 6)])) < 0.05  # against the far wall


def test_too_short_for_a_landing_in_line_the_landing_goes_beside():
    room = box(0.0, 0.0, 5.0, 4.0)  # 5 m: steps and toe 4.4 m, no room for a landing before
    st = plan_stair(room, room.buffer(3.0), 0.0, 3.0, [], [], (0.0, 0.0))
    assert st is not None and st.side_entry
    assert room.buffer(0.01).contains(st.foot_landing)
    assert st.foot_landing.intersection(st.footprint).area < 1e-6


def test_windows_only_where_the_steps_stay_below_the_sill():
    window = (LineString([(1.0, 6.0), (6.0, 6.0)]), 0.9)  # the whole north wall
    st = plan_stair(ROOM, ROOM, 0.0, 3.0, [], [window], DOOR)
    if st is not None and st.footprint.distance(window[0]) < 0.05:
        pytest.fail("the flight stands in front of the window")
    assert plan_stair(ROOM, ROOM, 0.0, 3.0, [], [], DOOR) is not None


def test_nothing_fits_returns_none():
    tiny = box(0.0, 0.0, 3.0, 3.0)
    assert plan_stair(tiny, tiny, 0.0, 3.0, [], [], (0.0, 0.0)) is None


def test_opening_gives_headroom_and_reaches_into_the_wall():
    st = plan_stair(ROOM, ROOM, 0.0, 3.0, [], [], DOOR)
    hole = st.opening(0.15, 2.0)
    length = st.opening_length(0.15, 2.0)
    slope = st.rise / st.tread
    assert length * slope >= 2.0 + 0.15  # where the ceiling starts the ramp is low enough
    assert not ROOM.contains(hole)  # into the wall behind: no sliver of slab


def test_ramp_is_a_closed_wedge_from_the_floor_to_the_top():
    st = plan_stair(ROOM, ROOM, 0.0, 3.0, [], [], DOOR)
    body = ramp_body(st, (0.0, 0.0, 0.0), "COL_HULL_RAMP")
    assert body_is_closed(body)
    ys = body.positions[:, 1]
    assert ys.min() == pytest.approx(0.0) and ys.max() == pytest.approx(3.0)
    run = math.dist(st.at(-st.tread, 0.0), st.at(st.run, 0.0))
    assert math.degrees(math.atan(3.0 / run)) <= 35.0


def test_json_round_trip():
    st = plan_stair(ROOM, ROOM, 0.0, 3.0, [], [], DOOR)
    d = st.json(0.15, 2.0)
    back = stair_from_json(d)
    assert back.steps == st.steps and back.side_entry == st.side_entry
    assert np.allclose(back.foot, st.foot, atol=1e-3) and np.allclose(back.up, st.up, atol=1e-4)
