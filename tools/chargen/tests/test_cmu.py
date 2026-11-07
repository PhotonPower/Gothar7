"""CMU mocap reader (F4): ASF/AMC parsing, forward kinematics, segment list."""

from __future__ import annotations

import numpy as np
import pytest

from gothar_chargen.cmu import CMU_FPS, INCH, euler, forward, load_segments, parse_amc, parse_asf

ASF = """:version 1.10
:units
  mass 1.0
  length 0.45
  angle deg
:root
   order TX TY TZ RX RY RZ
   axis XYZ
   position 0 0 0
   orientation 0 0 0
:bonedata
  begin
     id 1
     name lfemur
     direction 0 -1 0
     length 9.0
     axis 0 0 0  XYZ
    dof rx ry rz
  end
  begin
     id 2
     name ltibia
     direction 0 -1 0
     length 9.0
     axis 0 0 0  XYZ
    dof rx
  end
:hierarchy
  begin
    root lfemur
    lfemur ltibia
  end
"""
AMC = """:FULLY-SPECIFIED
:DEGREES
1
root 0 18 0 0 0 0
lfemur 0 0 0
ltibia 0
2
root 0 18 0 0 90 0
lfemur -90 0 0
ltibia 0
"""


def test_asf_units_and_hierarchy():
    sk = parse_asf(ASF)
    assert sk.bones["lfemur"].length == pytest.approx(9.0 * INCH / 0.45)
    assert sk.order() == ["root", "lfemur", "ltibia"]
    assert sk.bones["ltibia"].parent == "lfemur"


def test_forward_kinematics_rest_and_raised_leg():
    sk = parse_asf(ASF)
    frames = parse_amc(AMC)
    assert len(frames) == 2
    leg = 9.0 * INCH / 0.45
    rest = forward(sk, frames[0])
    assert rest.end["ltibia"] == pytest.approx([0.0, 18 * INCH / 0.45 - 2 * leg, 0.0])
    raised = forward(sk, frames[1])  # thigh -90 about X: the leg points forward (+Z), turned 90
    tip = raised.end["ltibia"] - raised.start["lfemur"]
    assert np.linalg.norm(tip) == pytest.approx(2 * leg)
    assert abs(tip[1]) < 1e-9  # horizontal


def test_euler_order_x_first():
    r = euler(90, 0, 90)
    assert r @ np.array([0, 1, 0]) == pytest.approx(np.array([0, 0, 1]), abs=1e-9)


def test_segments_are_cmu_takes_at_120_fps():
    segments = load_segments()
    assert {s.name for s in segments} >= {"fist_jab", "fist_cross_low"}
    for s in segments:
        assert s.end > s.start and (s.end - s.start) % (CMU_FPS // 30) == 0, s
        assert s.take.split("_")[0].isdigit()
