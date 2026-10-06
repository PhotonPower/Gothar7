"""Gait cycles for animals (gaits.py, F5): parameters, foot paths, leg IK, body vault."""

from __future__ import annotations

import math

import pytest

from gothar_chargen.gaits import (
    FPS,
    GaitError,
    foot_offset,
    parse_gait,
    required_drop,
    smooth_cyclic,
    solve_leg,
    vault,
)

LEGS = [
    {"upper": "a_u", "lower": "a_l", "foot": "a_f", "phase": 0.0},
    {"upper": "b_u", "lower": "b_l", "foot": "b_f", "phase": 0.5, "curl": 30},
]
TROT = {"speed": 3.0, "period": 10, "duty": 0.4, "lift": 0.09, "legs": LEGS}


def test_parse_and_stride():
    g = parse_gait({**TROT, "bob": 0.01, "flex": {"bone": "spine_01", "degrees": 8}})
    assert g.stride == pytest.approx(3.0 * 0.4 * 10 / FPS)
    assert g.legs[1].curl == 30.0 and g.flex is not None and g.flex.bones == ("spine_01",)


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"x": 1}, "unknown keys"),
        ({"legs": LEGS[:1]}, "2 to 4 legs"),
        ({"legs": [{**LEGS[0], "knee": 1}, LEGS[1]]}, "leg keys"),
        ({"legs": [{**LEGS[0], "upper": ""}, LEGS[1]]}, "upper, lower and foot"),
        ({"period": 2}, "period"),
        ({"period": 10.5}, "period"),
        ({"duty": 0.95}, "duty"),
        ({"bob_cycles": 3}, "bob_cycles"),
        ({"tail": {"bones": [], "degrees": 5}}, "tail"),
        ({"speed": True}, "speed"),
    ],
)
def test_invalid_gaits(change, message):
    with pytest.raises(GaitError, match=message):
        parse_gait({**TROT, **change})


def test_planted_foot_moves_back_at_the_speed():
    """Planted, the foot target moves backwards relative to the body at exactly `speed`: with the
    root moving forward at that speed it stands still in the world (no sliding)."""
    g = parse_gait(TROT)
    leg = g.legs[0]
    planted = [f for f in range(g.period) if foot_offset(g, leg, f)[2]]
    assert len(planted) == round(g.duty * g.period)
    for a, b in zip(planted, planted[1:], strict=False):
        back = foot_offset(g, leg, a)[0] - foot_offset(g, leg, b)[0]
        assert back * FPS == pytest.approx(g.speed)
        assert foot_offset(g, leg, a)[1] == 0.0
    # the loop closes: the last frame repeats the first
    assert foot_offset(g, leg, g.period) == pytest.approx(foot_offset(g, leg, 0))


def test_swing_lifts_and_returns_forward():
    g = parse_gait(TROT)
    leg = g.legs[0]
    swing = [foot_offset(g, leg, f) for f in range(g.period) if not foot_offset(g, leg, f)[2]]
    assert max(up for _, up, _ in swing) == pytest.approx(g.lift, rel=0.1)
    assert swing[0][0] < swing[-1][0]  # from the back of the stride to the front


@pytest.mark.parametrize("target", [(0.156, -0.377), (0.0, -0.35), (-0.2, -0.33), (0.1, -0.3)])
def test_solve_leg_reaches_the_target(target):
    upper, lower = (0.031, -0.198), (0.125, -0.179)  # wolf hind leg at rest (y back, z up)

    def rotate(v, a):
        return v[0] * math.cos(a) - v[1] * math.sin(a), v[0] * math.sin(a) + v[1] * math.cos(a)

    alpha, beta, reached = solve_leg(upper, lower, target)
    knee = rotate(upper, alpha)
    foot = rotate(lower, alpha + beta)
    assert reached == 1.0
    assert (knee[0] + foot[0], knee[1] + foot[1]) == pytest.approx(target, abs=1e-6)


def test_solve_leg_keeps_rest_and_reports_overreach():
    upper, lower = (0.031, -0.198), (0.125, -0.179)
    rest = (upper[0] + lower[0], upper[1] + lower[1])
    alpha, beta, _ = solve_leg(upper, lower, rest)
    assert alpha == pytest.approx(0.0, abs=1e-6) and beta == pytest.approx(0.0, abs=1e-6)
    *_, reached = solve_leg(upper, lower, (0.5, -0.4))  # too far: the leg stays stretched
    assert reached < 1.0


def test_vault_lowers_and_pitches():
    dz, phi = vault(0.05, 0.0, -0.56, 0.0, 0.0)  # front hips down 5 cm, rear unchanged
    assert -dz + (-0.56) * phi == pytest.approx(-0.05)
    assert -dz + 0.0 * phi == pytest.approx(0.0)
    assert vault(0.02, 0.04, None, None, 0.0) == (0.04, 0.0)  # bipeds: lowering only


def test_required_drop_and_smoothing():
    assert required_drop(0.3, 0.0, 0.42) == 0.0  # bent leg: reachable
    assert required_drop(0.365, 0.2, 0.366) > 0.05  # straight leg reaching forward
    values = [0.0, 0.0, 0.05, 0.0, 0.0, 0.0, 0.0]  # last repeats the first
    smooth = smooth_cyclic(values)
    assert all(s >= v - 1e-12 for s, v in zip(smooth, values, strict=True))  # never below
    assert smooth[-1] == smooth[0]


def test_fixed_posture_and_stepping_in_place():
    g = parse_gait(
        {**TROT, "speed": 0.0, "pose": {"neck_01": [["X", 20]], "ear_l": [["X", 30], ["Z", 5]]}}
    )
    assert g.speed == 0.0 and g.stride == 0.0
    assert g.pose == (("neck_01", (("X", 20.0),)), ("ear_l", (("X", 30.0), ("Z", 5.0))))
    with pytest.raises(GaitError, match="pose"):
        parse_gait({**TROT, "pose": {"neck_01": [["W", 20]]}})
    with pytest.raises(GaitError, match="pose"):
        parse_gait({**TROT, "pose": [1, 2]})
