import numpy as np
import pytest
from shapely.geometry import LineString, Point, box

from gothar_worldgen.export.retaining import plan_walls
from gothar_worldgen.export.terrain import Grid

SPEC = {
    "highways": ["residential", "footway", "path"],
    "crossFallM": 1.0,
    "probeM": 1.5,
    "minRunM": 3.0,
    "offsetM": 0.2,
    "minAxisM": 1.0,
    "fadeM": 3.0,
    "behindM": 0.6,
    "parapetM": 0.6,
    "maxM": 3.5,
    "houseM": 1.2,
    "doorM": 3.0,
    "keepM": 2.0,
    "laneM": 1.2,
    "laneReachM": 15.0,
    "doorReachM": 30.0,
    "junctionM": 1.5,
    "mortarM": 1.5,
    "thicknessM": 0.5,
}
AREA = box(-60, -60, 60, 60)


def grid(fn) -> Grid:  # noqa: ANN001
    xs = np.arange(-60.0, 60.5, 0.5)
    zs = np.arange(-60.0, 60.5, 0.5)
    heights = np.array([[fn(x, z) for x in xs] for z in zs])
    return Grid(heights, -60.0, -60.0, 0.5)


def plan(g: Grid, streets: list[dict], doors=()):  # noqa: ANN001, ANN201
    return plan_walls(g, streets, AREA, [], list(doors), [], [], SPEC)


STREET = {"osmId": "w1", "highway": "residential", "widthM": 4.0, "points": [[-30, 0], [30, 0]]}


def test_a_street_cut_into_a_slope_gets_walls_and_a_level_half():
    g = grid(lambda x, z: 0.5 * z)  # rises across the street: 1.75 m at the probe
    levelled, walls, stats = plan(g, [STREET])
    sides = {w.side for w in walls}
    assert sides == {"high", "low"} and stats["walls"] == 2
    high = next(w for w in walls if w.side == "high")
    assert all(z == pytest.approx(2.2, abs=0.05) for _, z in high.points)  # at the edge
    assert all(t > b for t, b in zip(high.top, high.base, strict=True))
    # in the middle of the wall the street's half is level with its axis, the far ground is not
    assert levelled.height_at(0.0, 1.5) == pytest.approx(0.0, abs=0.05)
    assert levelled.height_at(0.0, 5.0) == pytest.approx(2.5)


def test_levelling_keeps_a_climbing_way_even_and_sets_in_from_the_ends():
    g = grid(lambda x, z: 0.5 * z + 0.3 * x)  # the street climbs 0.3 m per metre
    levelled, walls, _ = plan(g, [STREET])
    high = next(w for w in walls if w.side == "high")
    xs = [x for x, _ in high.points]
    # beside the axis each cell has the axis' height there: no terraces a metre long
    hs = [levelled.height_at(x, 1.0) for x in np.arange(-20.0, 20.0, 0.5)]
    assert max(abs(b - a) for a, b in zip(hs, hs[1:], strict=False)) < 0.3 * 0.5 + 0.03
    assert levelled.height_at(0.0, 1.0) == pytest.approx(0.0, abs=0.05)
    # at the wall's ends the ground is as it was: no lip where the cut ends
    end = min(xs)
    assert levelled.height_at(end, 1.0) == pytest.approx(g.height_at(end, 1.0), abs=0.2)


def test_doors_keep_a_way_through_the_wall_to_every_street_near():
    g = grid(lambda x, z: 0.5 * z)
    door = (0.0, 26.0)  # up the slope, beyond the old 25 m
    _, walls, _ = plan(g, [STREET], doors=[door])
    lane = LineString([door, (0.0, 0.0)])
    high = [w for w in walls if w.side == "high"]
    assert len(high) == 2  # the wall leaves a gap where the door's way comes down
    assert all(LineString(w.points).distance(lane) > SPEC["laneM"] - 0.5 for w in high)


def test_no_wall_on_the_inside_of_a_tight_bend_and_room_on_a_narrow_path():
    g = grid(lambda x, z: 0.5 * z)
    path = {"osmId": "w2", "highway": "path", "widthM": 1.0,
            "points": [[-20, 0], [0, 0], [-6, -14]]}  # fmt: skip
    _, walls, _ = plan(g, [path])
    line = LineString(path["points"])
    for w in walls:
        for p in w.points:  # a metre from the path wherever it is, not only from its own leg
            assert line.distance(Point(p)) > SPEC["minAxisM"] - 0.1
