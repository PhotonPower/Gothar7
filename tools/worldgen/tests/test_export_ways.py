import math

import numpy as np
import pytest
from shapely.geometry import box

from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.export.ways import limit_profile, smooth_ways

SPEC = {"maxDeg": 42.0, "halfWidthM": 1.5, "fadeM": 2.0, "passes": 8}
AREA = box(-50, -50, 50, 50)


def test_profile_is_limited_and_stays_close():
    h = np.array([0.0, 0.0, 0.0, 3.0, 3.0, 3.0])  # a 3 m step
    k = math.tan(math.radians(42.0))
    p = limit_profile(h, 1.0, 42.0)
    assert np.abs(np.diff(p)).max() <= k + 1e-6
    assert p[0] < 0.5 and p[-1] > 2.5  # the ends stay near the terrain
    assert np.allclose(limit_profile(np.linspace(0, 2, 6), 1.0, 42.0), np.linspace(0, 2, 6))


def ramp_grid() -> Grid:
    """Flat at 0 west of x = 0, a 3 m wall at x 0..1, flat at 3 east of it."""
    xs = np.arange(100) - 49.5
    row = np.where(xs < 0, 0.0, np.where(xs > 1, 3.0, 3.0 * xs))
    return Grid(np.tile(row, (100, 1)), -49.5, -49.5, 1.0)


def test_steep_way_is_smoothed_only_near_the_way():
    grid = ramp_grid()
    streets = [
        {"osmId": "w1", "highway": "steps", "points": [[-10.0, 0.5], [10.0, 0.5]]},
        {"osmId": "w2", "highway": "motorway", "points": [[-10.0, 20.5], [10.0, 20.5]]},
    ]
    out, stats = smooth_ways(grid, streets, AREA, SPEC)
    assert stats["ways"] == 1 and stats["cellsChanged"] > 0
    along = [out.height_at(x, 0.5) for x in np.arange(-6.0, 7.0, 1.0)]
    steepest = max(
        math.degrees(math.atan(abs(b - a))) for a, b in zip(along, along[1:], strict=False)
    )
    assert steepest <= SPEC["maxDeg"] + 1.0
    # far from the way (and along the motorway, not walkable) nothing changes
    assert out.height_at(5.0, 15.0) == pytest.approx(grid.height_at(5.0, 15.0))
    assert out.height_at(0.5, 20.5) == pytest.approx(grid.height_at(0.5, 20.5))


def test_gentle_ways_are_left_alone():
    xs = np.arange(100) - 49.5
    grid = Grid(np.tile(0.3 * xs, (100, 1)), -49.5, -49.5, 1.0)  # 17 degrees
    streets = [{"osmId": "w1", "highway": "footway", "points": [[-10.0, 0.5], [10.0, 0.5]]}]
    out, stats = smooth_ways(grid, streets, AREA, SPEC)
    assert stats["ways"] == 0 and stats["cellsChanged"] == 0
    assert np.array_equal(out.heights, grid.heights)
