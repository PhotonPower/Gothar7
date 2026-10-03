import numpy as np
import pytest
from shapely.geometry import LineString

from gothar_worldgen.export.descents import dig_descents
from gothar_worldgen.export.terrain import Grid

HILL = {"descentSlopeDeg": 40.0, "descentWidthM": 1.6}


def flat(level: float = 2.0) -> Grid:
    return Grid(np.full((60, 60), level), -29.5, -29.5, 1.0)


def entry(kind: str = "descent", floor: float = 1.0) -> dict:
    # wall along z = 0 facing south (+z); the door point lies 0.6 m in front of it
    return {"id": "H", "doors": [[0.0, 0.6, floor, kind, 0.0, 1.0]]}


def test_descent_reaches_the_floor_at_the_door_and_rises_away_from_it():
    out, stats = dig_descents(flat(), [entry()], HILL)
    assert stats["doors"] == 1 and stats["cells"] > 0
    assert out.height_at(0.0, 0.5) == pytest.approx(1.0, abs=0.1)  # landing at the floor
    profile = [out.height_at(0.0, z) for z in np.arange(0.5, 4.5, 0.5)]
    assert all(b >= a - 1e-9 for a, b in zip(profile, profile[1:], strict=False))
    assert out.height_at(0.0, 6.0) == 2.0  # beyond the ramp: untouched
    assert out.height_at(6.0, 1.0) == 2.0  # beside it: untouched
    assert (out.heights <= 2.0 + 1e-9).all()  # only lowered


def test_other_doors_and_doors_next_to_a_way_are_left_alone():
    out, stats = dig_descents(flat(), [entry("ground"), entry("upper")], HILL)
    assert stats["doors"] == 0 and np.array_equal(out.heights, flat().heights)
    way = LineString([(-10.0, 1.5), (10.0, 1.5)]).buffer(0.8)
    out, stats = dig_descents(flat(), [entry()], HILL, way)
    assert stats["doors"] == 0 and stats["skippedForWays"] == 1
