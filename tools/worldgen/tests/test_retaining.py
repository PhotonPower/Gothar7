import numpy as np
import pytest
from shapely.geometry import LineString, Point, box

from gothar_worldgen.export.retaining import GapSteps, plan_walls
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
    "minDropM": 0.4,
    "gapSteps": {"maxDeg": 33.0, "widthM": 1.2, "fadeM": 1.0, "maxLenM": 12.0},
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


def test_steps_lead_up_the_bank_in_a_wall_gap_before_a_door():
    g = grid(lambda x, z: min(max(z - 2.5, 0.0), 2.0))  # a 45 degree bank behind the edge
    door = (0.0, 12.0)  # up on the bank: its way comes down through the wall
    steps: list[GapSteps] = []
    levelled, walls, stats = plan_walls(g, [STREET], AREA, [], [door], [], [], SPEC, steps)
    assert stats["gapSteps"] == 1 and len([w for w in walls if w.side == "high"]) == 2
    (st,) = steps
    (ax, az), (bx, bz) = st.points
    assert abs(ax) < 1.5 and az == pytest.approx(2.2, abs=0.05) and bz > az  # out from the edge
    rise = levelled.height_at(bx, bz) - levelled.height_at(ax, az)
    run = bz - az
    assert rise >= SPEC["minDropM"] and rise <= np.tan(np.radians(33.0)) * run + 0.05
    # the ground under them a straight ramp
    mid = levelled.height_at((ax + bx) / 2, (az + bz) / 2)
    assert mid == pytest.approx(
        (levelled.height_at(ax, az) + levelled.height_at(bx, bz)) / 2, abs=0.08
    )
    # a hill rising on behind the bank: the steps get longer, never steeper
    hill: list[GapSteps] = []
    hg = grid(lambda x, z: max(z - 2.5, 0.0) if z < 4.5 else 2.0 + 0.4 * (z - 4.5))
    hl, _, _ = plan_walls(hg, [STREET], AREA, [], [door], [], [], SPEC, hill)
    (hs,) = hill
    (hx, hz), (kx, kz) = hs.points
    assert (
        hl.height_at(kx, kz) - hl.height_at(hx, hz) <= np.tan(np.radians(33.0)) * (kz - hz) + 0.05
    )
    # no steps where no wall stands beside the gap (a door on a gentle slope)
    gentle: list[GapSteps] = []
    plan_walls(grid(lambda x, z: 0.1 * z), [STREET], AREA, [], [door], [], [], SPEC, gentle)
    assert gentle == []


def test_no_teeth_of_old_ground_in_front_of_a_wall_on_a_diagonal_street():
    g = grid(lambda x, z: 0.5 * (z - x) / np.sqrt(2.0))  # rises across a street at 45 degrees
    diagonal = {"osmId": "w3", "highway": "residential", "widthM": 4.0,
                "points": [[-30, -30], [30, 30]]}  # fmt: skip
    levelled, walls, _ = plan(g, [diagonal])
    high = next(w for w in walls if w.side == "high")
    (ox, oz) = high.outward[len(high.points) // 2]
    worst = 0.0
    for t in np.arange(-15.0, 15.0, 0.13):  # just in front of the face, all along the middle
        x, z = t / np.sqrt(2.0), t / np.sqrt(2.0)
        px, pz = x + ox * (2.2 - 0.15), z + oz * (2.2 - 0.15)
        worst = max(worst, abs(levelled.height_at(px, pz) - levelled.height_at(x, z)))
    assert worst < 0.05


def test_walls_are_founded_below_the_ground_on_both_sides():
    g = grid(lambda x, z: 0.5 * z + 0.3 * x)  # a climbing street, the walls' ends fade
    levelled, walls, _ = plan(g, [STREET])
    for w in walls:
        for (x, z), (ox, oz), base in zip(w.points, w.outward, w.base, strict=True):
            for d in (-0.3, 0.0, 0.25, 0.5, 0.8):
                assert base <= levelled.height_at(x + ox * d, z + oz * d) - 0.3 + 1e-6


def test_the_levelling_fades_smoothly_along_a_wall():
    g = grid(lambda x, z: 0.5 * z)
    levelled, walls, _ = plan(g, [STREET])
    high = next(w for w in walls if w.side == "high")
    x0 = min(x for x, _ in high.points)
    hs = [levelled.height_at(x, 1.8) for x in np.arange(x0 - 1.0, x0 + 4.0, 0.25)]
    assert max(abs(b - a) for a, b in zip(hs, hs[1:], strict=False)) < 0.12  # no teeth


def test_a_gap_before_a_door_is_level_and_its_bank_an_even_ramp_up_to_the_plateau():
    # a 45 degree bank, then a gentle rise (about 14 degrees) for two metres, then flat
    def bank(x: float, z: float) -> float:
        return min(max(z - 2.5, 0.0), 2.0) + min(max(z - 4.5, 0.0), 2.0) * 0.25

    door = (0.0, 12.0)
    steps: list[GapSteps] = []
    levelled, walls, _ = plan_walls(grid(bank), [STREET], AREA, [], [door], [], [], SPEC, steps)
    (st,) = steps
    (ax, az), (bx, bz) = st.points
    assert bz >= 6.5 - 0.3  # up to the plateau, not only to where it gets walkable
    for x in (-1.5, 0.0, 1.5):  # the street before the gap level with its axis: no hump
        assert levelled.height_at(x, 1.5) == pytest.approx(levelled.height_at(x, 0.0), abs=0.05)
    mid = (az + bz) / 2
    centre = levelled.height_at(0.0, mid)
    for x in (-1.0, 1.0):  # the bank beside the steps as high as under them: an even ramp
        assert levelled.height_at(x, mid) == pytest.approx(centre, abs=0.1)
