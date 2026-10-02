"""Equirectangular (360°) images: pixel <-> direction, sampling and perspective views.

Directions live in the local engine system (+X east, +Y up, -Z north). An image of width W and
height H = W / 2 covers longitude -180..180° (column 0 at the left edge) and latitude 90..-90°
(row 0 at the top). Longitude 0 (the image centre) points along the camera heading; positive
longitude turns clockwise seen from above, i.e. to the right in the image.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

Image = npt.NDArray[np.uint8]  # (H, W, C)
FloatArray = npt.NDArray[np.float64]


@dataclass(frozen=True)
class CameraPose:
    """Position (local metres) and orientation of a 360° camera.

    ``heading_deg``: compass direction of the image centre, clockwise from north.
    ``pitch_deg`` / ``roll_deg``: tilt of the camera (0 for a stabilised, level horizon).
    """

    x: float
    y: float
    z: float
    heading_deg: float = 0.0
    pitch_deg: float = 0.0
    roll_deg: float = 0.0

    @property
    def position(self) -> FloatArray:
        return np.array([self.x, self.y, self.z], dtype=np.float64)

    def camera_to_world(self) -> FloatArray:
        """Camera-to-world rotation (camera frame: x right, y up, -z forward)."""
        h = math.radians(self.heading_deg)
        p = math.radians(self.pitch_deg)
        r = math.radians(self.roll_deg)
        # Heading: clockwise from above = rotation by -h around +Y.
        ry = np.array([[math.cos(h), 0, -math.sin(h)], [0, 1, 0], [math.sin(h), 0, math.cos(h)]])
        rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
        rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
        return ry @ rx @ rz


def _camera_dirs(lon: FloatArray, lat: FloatArray) -> FloatArray:
    """Unit directions in the camera frame (forward = -Z, right = +X, up = +Y)."""
    cos_lat = np.cos(lat)
    return np.stack([np.sin(lon) * cos_lat, np.sin(lat), -np.cos(lon) * cos_lat], axis=-1)


def pixel_to_direction(
    u: npt.ArrayLike, v: npt.ArrayLike, width: int, height: int, pose: CameraPose
) -> FloatArray:
    """World directions (..., 3) of pixel coordinates (pixel centres at +0.5)."""
    lon = (np.asarray(u, dtype=np.float64) / width - 0.5) * 2.0 * math.pi
    lat = (0.5 - np.asarray(v, dtype=np.float64) / height) * math.pi
    return _camera_dirs(lon, lat) @ pose.camera_to_world().T


def direction_to_pixel(
    directions: npt.ArrayLike, width: int, height: int, pose: CameraPose
) -> tuple[FloatArray, FloatArray]:
    """Pixel coordinates (u, v) of world directions (..., 3); the inverse of pixel_to_direction."""
    d = np.asarray(directions, dtype=np.float64)
    cam = d @ pose.camera_to_world()  # inverse rotation (orthonormal)
    norm = np.linalg.norm(cam, axis=-1)
    norm = np.where(norm > 0, norm, 1.0)
    lon = np.arctan2(cam[..., 0], -cam[..., 2])
    lat = np.arcsin(np.clip(cam[..., 1] / norm, -1.0, 1.0))
    u = (lon / (2.0 * math.pi) + 0.5) * width
    v = (0.5 - lat / math.pi) * height
    return u, v


def sample(image: Image, u: FloatArray, v: FloatArray) -> Image:
    """Bilinear sampling with horizontal wrap-around (360°) and clamped rows."""
    h, w = image.shape[:2]
    x = np.asarray(u, dtype=np.float64) - 0.5
    y = np.clip(np.asarray(v, dtype=np.float64) - 0.5, 0.0, h - 1.0)
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    x1 = (x0 + 1) % w
    x0 %= w
    y1 = np.minimum(y0 + 1, h - 1)
    img = image.astype(np.float64)
    top = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    bottom = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return np.clip(np.rint(top * (1 - fy) + bottom * fy), 0, 255).astype(np.uint8)


def perspective_view(
    image: Image,
    pose: CameraPose,
    heading_deg: float,
    pitch_deg: float,
    fov_deg: float,
    out_width: int,
    out_height: int,
) -> Image:
    """Pinhole view towards compass ``heading_deg`` / ``pitch_deg``; ``fov_deg`` is horizontal."""
    if not 0 < fov_deg < 180:
        raise ValueError(f"field of view must be in (0, 180), got {fov_deg}")
    view = CameraPose(pose.x, pose.y, pose.z, heading_deg, pitch_deg, 0.0)
    f = (out_width / 2) / math.tan(math.radians(fov_deg) / 2)
    xs = np.arange(out_width) + 0.5 - out_width / 2
    ys = out_height / 2 - (np.arange(out_height) + 0.5)
    gx, gy = np.meshgrid(xs, ys)
    cam = np.stack([gx, gy, -np.full_like(gx, f)], axis=-1)
    world = cam @ view.camera_to_world().T
    u, v = direction_to_pixel(world, image.shape[1], image.shape[0], pose)
    return sample(image, u, v)
