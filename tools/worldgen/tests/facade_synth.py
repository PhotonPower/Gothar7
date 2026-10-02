"""Synthetic 360° images for facade tests: a checkerboard facade rendered by ray casting."""

import numpy as np

from gothar_worldgen.facade.equirect import CameraPose, pixel_to_direction
from gothar_worldgen.facade.rectify import Facade

BLACK = (20, 20, 20)
WHITE = (235, 235, 235)
SKY = (60, 120, 200)


def checker(s: np.ndarray, h: np.ndarray, cell_m: float) -> np.ndarray:
    """Colour of facade coordinates (s along the facade from the left, h above ground)."""
    parity = (np.floor(s / cell_m) + np.floor(h / cell_m)).astype(np.int64) % 2
    out = np.empty((*s.shape, 3), dtype=np.uint8)
    out[parity == 0] = BLACK
    out[parity == 1] = WHITE
    return out


def render_equirect(
    width: int, pose: CameraPose, facade: Facade, cell_m: float = 0.5
) -> np.ndarray:
    """Equirectangular image (width x width/2) of a checkerboard facade in front of a blue sky."""
    height = width // 2
    us, vs = np.meshgrid(np.arange(width) + 0.5, np.arange(height) + 0.5)
    d = pixel_to_direction(us, vs, width, height, pose)
    nx, nz = facade.normal
    normal = np.array([nx, 0.0, nz])
    left = np.array([facade.left[0], facade.ground_y, facade.left[1]])
    right = np.array([facade.right[0], facade.ground_y, facade.right[1]])
    along = (right - left) / np.linalg.norm(right - left)
    denom = d @ normal
    with np.errstate(divide="ignore", invalid="ignore"):
        t = ((left - pose.position) @ normal) / denom
    hit = pose.position + d * t[..., None]
    s = (hit - left) @ along
    h = hit[..., 1] - facade.ground_y
    on = (t > 0) & (s >= 0) & (s <= facade.width_m) & (h >= 0) & (h <= facade.height_m)
    image = np.empty((height, width, 3), dtype=np.uint8)
    image[:] = SKY
    image[on] = checker(s[on], h[on], cell_m)
    return image


def expected_view(facade: Facade, px_per_m: float, cell_m: float = 0.5) -> np.ndarray:
    """What a perfect rectification of the checkerboard facade looks like."""
    cols = round(facade.width_m * px_per_m)
    rows = round(facade.height_m * px_per_m)
    s, t = np.meshgrid((np.arange(cols) + 0.5) / px_per_m, (np.arange(rows) + 0.5) / px_per_m)
    return checker(s, facade.height_m - t, cell_m)


def away_from_edges(
    facade: Facade, px_per_m: float, cell_m: float = 0.5, margin_m: float = 0.06
) -> np.ndarray:
    """Mask of view pixels not close to a checker cell border (border pixels mix both colours)."""
    cols = round(facade.width_m * px_per_m)
    rows = round(facade.height_m * px_per_m)
    s, t = np.meshgrid((np.arange(cols) + 0.5) / px_per_m, (np.arange(rows) + 0.5) / px_per_m)
    h = facade.height_m - t

    def inner(x: np.ndarray) -> np.ndarray:
        r = np.mod(x, cell_m)
        return (r > margin_m) & (r < cell_m - margin_m)

    return inner(s) & inner(h)
