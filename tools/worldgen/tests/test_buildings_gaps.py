"""Filler bodies in the slots between houses (walkthrough decision E4)."""

from shapely.geometry import box

from gothar_worldgen.buildings.collision import body_is_closed
from gothar_worldgen.buildings.gaps import find_fillers, footprint_of, prism_part

SPEC = {"maxWidthM": 0.7, "minAreaM2": 0.3, "enclosedShare": 0.5, "aboveGroundM": 3.0}


def flat(x: float, z: float) -> float:
    return 1.0


def test_narrow_slot_between_two_houses_is_filled():
    houses = {"A": box(0, 0, 10, 8), "B": box(10.4, 0, 20, 8)}  # 0.4 m slot, 8 m deep
    fillers = find_fillers(houses, flat, SPEC)
    assert fillers
    covered = sum(f.piece.area for f in fillers)
    assert covered >= 0.8 * 0.4 * 8
    assert all(f.owner in houses for f in fillers)
    assert all(f.y0 < 1.0 and f.y1 == 4.0 for f in fillers)


def test_wide_lanes_and_open_notches_stay_free():
    lane = {"A": box(0, 0, 10, 8), "B": box(11.5, 0, 20, 8)}  # 1.5 m: a lane
    assert find_fillers(lane, flat, SPEC) == []
    # a lone house: its outside is not enclosed by houses
    assert find_fillers({"A": box(0, 0, 10, 8)}, flat, SPEC) == []


def test_prism_bodies_are_closed_and_outward():
    part = prism_part(box(2, 3, 2.4, 9), 0.5, 4.0, (1.0, 0.0, 1.0), "COL_HULL_0")
    assert body_is_closed(part)
    assert part.positions[:, 1].min() == 0.5 and part.positions[:, 1].max() == 4.0
    fp = footprint_of([part], (1.0, 0.0, 1.0))
    assert fp.equals_exact(box(2, 3, 2.4, 9), 1e-3) or abs(fp.area - box(2, 3, 2.4, 9).area) < 1e-3
