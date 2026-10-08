import json
import math
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import LineString, box

from gothar_worldgen.export.ways import monotone_profile
from gothar_worldgen.streetworks import gutter_pieces, gutter_strips, steps_piece, wall_piece

RULES = json.loads(
    (Path(__file__).parents[1] / "data" / "leonberg" / "streetworks.json").read_text("utf-8")
)
AREA = box(-100, -100, 100, 100)


def test_the_steps_profile_rises_all_the_way():
    h = np.array([0.0, 0.5, 0.3, 1.0, 1.4, 1.2, 2.0])  # a dip and a hump
    p = monotone_profile(h)
    assert all(b >= a for a, b in zip(p, p[1:], strict=False))
    assert p[0] == pytest.approx(0.0) and p[-1] == pytest.approx(2.0)
    assert list(p) == pytest.approx([0.0, 0.4, 0.4, 1.0, 1.3, 1.3, 2.0])  # dip filled, hump cut
    falling = monotone_profile(h[::-1])
    assert np.allclose(falling, p[::-1])


def test_steps_have_even_risers_and_never_lie_under_the_ground():
    ramp = lambda x, z: 0.3 * x  # noqa: E731  3 m up over 10 m
    piece = steps_piece("steps_t", LineString([(10, 0), (0, 0)]), 1.5, ramp, RULES["steps"])
    assert piece is not None and piece.kind == "steps"
    b = piece.builders["stone"]
    tops = sorted({round(y + b.oy, 3) for _, y, _ in b.pos})
    n = math.ceil(3.0 / RULES["steps"]["riseM"])
    rise = 3.0 / n
    assert rise <= RULES["steps"]["riseM"]
    assert all(any(abs(t - k * rise) < 2e-3 for t in tops) for k in range(1, n + 1))  # each tread
    assert max(tops) == pytest.approx(3.0, abs=2e-3)
    # each tread's top at least as high as the ground at its upper end: no stone under the ground
    for k in range(1, n + 1):
        xs = [x + b.ox for x, y, _ in b.pos if abs(y + b.oy - k * rise) < 2e-3]
        assert xs and ramp(max(xs), 0.0) <= k * rise + 2e-3
    flat = steps_piece("steps_f", LineString([(0, 0), (10, 0)]), 1.5, lambda x, z: 0.0,
                       RULES["steps"])  # fmt: skip
    assert flat is None  # no steps where the way hardly rises


def test_gutters_run_in_the_middle_of_narrow_streets_and_at_the_sides_of_wide_ones():
    spec = RULES["gutters"]
    narrow = {"osmId": "w1", "highway": "residential", "widthM": 4.0, "points": [[-30, 0], [30, 0]]}
    wide = {"osmId": "w2", "highway": "residential", "widthM": 8.0, "points": [[-30, 50], [30, 50]]}
    strips = gutter_strips([narrow, wide], AREA, [], spec)
    mids = sorted(round(s.interpolate(0.5, normalized=True).y, 2) for s in strips)
    side = 8.0 / 2 - spec["sideOffsetM"]
    assert mids == [0.0, pytest.approx(50 - side), pytest.approx(50 + side)]
    # cut where a house stands on the street
    house = box(-5, -3, 5, 3)
    cut = gutter_strips([narrow], AREA, [house], spec)
    assert len(cut) == 2 and all(s.distance(house) >= spec["clearM"] - 1e-6 for s in cut)
    assert gutter_strips([dict(narrow, highway="footway")], AREA, [], spec) == []


def test_a_retaining_wall_collides_and_is_dry_or_mortared():
    pts = [[float(x), 2.2] for x in range(0, 9)]
    plan = {"id": "wall_t", "side": "high", "style": "dry", "points": pts,
            "base": [-0.3] * 9, "top": [2.0] * 9, "outward": [[0.0, 1.0]] * 9}  # fmt: skip
    piece = wall_piece(plan, RULES["walls"])
    assert piece is not None and piece.gameplay and set(piece.builders) == {"stone_dry"}
    assert len(piece.collision) == 4  # prisms of two metres along it
    b = piece.builders["stone_dry"]
    zs = [z + b.oz for _, _, z in b.pos]
    assert min(zs) == pytest.approx(2.2) and max(zs) == pytest.approx(
        2.2 + RULES["walls"]["thicknessM"]
    )
    ys = [y + b.oy for _, y, _ in b.pos]
    assert min(ys) == pytest.approx(-0.3) and max(ys) == pytest.approx(2.0)
    mortared = wall_piece(dict(plan, style="mortared"), RULES["walls"])
    assert mortared is not None and set(mortared.builders) == {"stone"}
    assert wall_piece(dict(plan, points=pts[:1]), RULES["walls"]) is None


def test_a_gutter_lies_on_a_street_falling_across_with_its_bottom_above_the_ground():
    spec = RULES["gutters"]
    fall = lambda x, z: 0.12 * z  # noqa: E731  the street falls 12 % across
    (piece,) = gutter_pieces([LineString([(0, 0), (10, 0)])], fall, spec)
    b = piece.builders["stone_slab"]
    tops = [(x + b.ox, y + b.oy, z + b.oz) for x, y, z in b.pos]
    surface = [p for p in tops if p[1] > fall(p[0], p[2]) - 0.01]  # the slabs, not their skirts
    assert surface and all(y >= fall(x, z) + spec["liftM"] - spec["depthM"] - 1e-6
                           for x, y, z in surface)  # fmt: skip
    edges = [p for p in surface if abs(abs(p[2]) - spec["widthM"] / 2) < 1e-6]
    assert all(y == pytest.approx(fall(x, z) + spec["liftM"]) for x, y, z in edges)
    assert min(y - fall(x, z) for x, y, z in tops) < 0  # the skirts reach into the ground
