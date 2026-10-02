import numpy as np
import pytest

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.rectify import (
    Facade,
    FacadeError,
    facade_from_footprint,
    rectify,
    view_geometry,
)

from .facade_synth import away_from_edges, expected_view, render_equirect

# 4 m x 4 m house; footprint counter-clockwise seen from above with north up (local z points south).
HOUSE = {
    "id": "B1",
    "footprint": [[0.0, 0.0], [4.0, 0.0], [4.0, -4.0], [0.0, -4.0]],
    "groundY": 0.0,
    "heightM": 9.0,
    "roof": {"eaveY": 6.0, "ridgeY": 9.0},
}


def test_facade_from_footprint_orientation():
    south = facade_from_footprint(HOUSE, 0)  # edge (0,0) -> (4,0) is the south side
    assert south.left == (0.0, 0.0)  # seen from the south, west is on the left
    assert south.right == (4.0, 0.0)
    assert south.normal == pytest.approx((0.0, 1.0))  # outward = south = +z
    assert (south.width_m, south.height_m) == (4.0, 6.0)
    east = facade_from_footprint(HOUSE, 1)
    assert east.normal == pytest.approx((1.0, 0.0))
    with pytest.raises(FacadeError):
        facade_from_footprint(HOUSE, 4)


def test_facade_height_falls_back_to_building_height():
    flat = {**HOUSE, "roof": None}
    assert facade_from_footprint(flat, 0).height_m == 9.0


@pytest.mark.parametrize("heading", [0.0, 37.0, 200.0])
def test_rectified_checkerboard_matches_the_original(heading):
    facade = facade_from_footprint(HOUSE, 0)
    pose = CameraPose(2.0, 1.6, 7.0, heading)  # 7 m in front of the south facade
    image = render_equirect(4096, pose, facade)
    view = rectify(image, pose, facade, px_per_m=40)
    expected = expected_view(facade, 40)
    assert view.image.shape == expected.shape == (240, 160, 3)
    mask = away_from_edges(facade, 40)
    match = np.all(np.abs(view.image.astype(int) - expected.astype(int)) < 40, axis=-1)
    assert match[mask].mean() > 0.99
    assert view.angle_deg == pytest.approx(0.0, abs=1e-6)
    assert view.distance_m == pytest.approx(np.hypot(7.0, 3.0 - 1.6))


def test_oblique_view_still_rectifies_but_scores_lower():
    facade = facade_from_footprint(HOUSE, 0)
    frontal = CameraPose(2.0, 1.6, 7.0)
    oblique = CameraPose(9.0, 1.6, 6.0)  # 50° off the normal
    v_front = rectify(render_equirect(4096, frontal, facade), frontal, facade, px_per_m=30)
    v_obl = rectify(render_equirect(4096, oblique, facade), oblique, facade, px_per_m=30)
    expected = expected_view(facade, 30)
    mask = away_from_edges(facade, 30, margin_m=0.1)
    match = np.all(np.abs(v_obl.image.astype(int) - expected.astype(int)) < 60, axis=-1)
    assert match[mask].mean() > 0.95
    assert v_obl.angle_deg > 45
    assert v_obl.quality < v_front.quality


def test_camera_behind_the_facade_is_rejected():
    facade = facade_from_footprint(HOUSE, 0)
    inside = CameraPose(2.0, 1.6, -2.0)  # inside the house
    with pytest.raises(FacadeError, match="behind"):
        rectify(np.zeros((64, 128, 3), np.uint8), inside, facade)


def test_view_geometry_numbers():
    facade = Facade((0.0, 0.0), (4.0, 0.0), 0.0, 6.0)
    distance, angle, density = view_geometry(facade, CameraPose(2.0, 3.0, 10.0), 6283)
    assert distance == pytest.approx(10.0)
    assert angle == pytest.approx(0.0)
    assert density == pytest.approx(6283 / (2 * np.pi) / 10.0, rel=1e-3)  # ~100 px/m
