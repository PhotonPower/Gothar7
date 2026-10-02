import math

import numpy as np
import pytest

from gothar_worldgen.facade.equirect import (
    CameraPose,
    direction_to_pixel,
    perspective_view,
    pixel_to_direction,
    sample,
)

W, H = 360, 180


def unit(heading_deg: float, pitch_deg: float = 0.0) -> np.ndarray:
    """World direction for a compass heading (clockwise from north) and elevation."""
    h, p = math.radians(heading_deg), math.radians(pitch_deg)
    return np.array([math.sin(h) * math.cos(p), math.sin(p), -math.cos(h) * math.cos(p)])


@pytest.mark.parametrize("heading", [0.0, 90.0, 213.0])
def test_image_centre_looks_along_the_heading(heading):
    pose = CameraPose(0, 0, 0, heading)
    d = pixel_to_direction(W / 2, H / 2, W, H, pose)
    np.testing.assert_allclose(d, unit(heading), atol=1e-9)


def test_columns_turn_clockwise_and_rows_go_down():
    pose = CameraPose(0, 0, 0, 0.0)
    np.testing.assert_allclose(
        pixel_to_direction(0.75 * W, H / 2, W, H, pose), unit(90), atol=1e-9
    )  # east
    np.testing.assert_allclose(
        pixel_to_direction(0.25 * W, H / 2, W, H, pose), unit(-90), atol=1e-9
    )  # west
    np.testing.assert_allclose(pixel_to_direction(W / 2, 0, W, H, pose), [0, 1, 0], atol=1e-9)  # up


def test_round_trip_with_tilted_camera():
    pose = CameraPose(1, 2, 3, heading_deg=33.0, pitch_deg=7.0, roll_deg=-4.0)
    rng = np.random.default_rng(3)
    u = rng.uniform(0, W, 200)
    v = rng.uniform(5, H - 5, 200)
    d = pixel_to_direction(u, v, W, H, pose)
    u2, v2 = direction_to_pixel(d * 5.0, W, H, pose)  # length does not matter
    np.testing.assert_allclose(np.mod(u2 - u + W / 2, W) - W / 2, 0, atol=1e-6)
    np.testing.assert_allclose(v2, v, atol=1e-6)


def test_sampling_wraps_horizontally():
    img = np.zeros((2, 4, 3), dtype=np.uint8)
    img[:, 0] = 100
    img[:, 3] = 200
    # Halfway between the last and the first column.
    assert sample(img, np.array([4.0]), np.array([1.0]))[0, 0] == 150


def marker_image(target: np.ndarray, radius_deg: float = 3.0) -> np.ndarray:
    pose = CameraPose(0, 0, 0)
    us, vs = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5)
    d = pixel_to_direction(us, vs, W, H, pose)
    close = d @ target > math.cos(math.radians(radius_deg))
    img = np.zeros((H, W, 3), dtype=np.uint8)
    img[close] = (255, 0, 0)
    return img


def test_perspective_view_centres_the_looked_at_direction():
    img = marker_image(unit(30, 10))
    pose = CameraPose(0, 0, 0)
    view = perspective_view(
        img, pose, heading_deg=30, pitch_deg=10, fov_deg=60, out_width=64, out_height=48
    )
    assert view.shape == (48, 64, 3)
    assert view[24, 32, 0] > 200  # marker in the middle
    away = perspective_view(
        img, pose, heading_deg=120, pitch_deg=10, fov_deg=60, out_width=64, out_height=48
    )
    assert away[24, 32, 0] == 0
    # The marker is to the right of a view looking 15° further left.
    left = perspective_view(
        img, pose, heading_deg=15, pitch_deg=10, fov_deg=60, out_width=64, out_height=48
    )
    reds = np.argwhere(left[..., 0] > 200)
    assert reds[:, 1].mean() > 32


def test_perspective_view_rejects_bad_fov():
    with pytest.raises(ValueError):
        perspective_view(np.zeros((H, W, 3), np.uint8), CameraPose(0, 0, 0), 0, 0, 180, 10, 10)
