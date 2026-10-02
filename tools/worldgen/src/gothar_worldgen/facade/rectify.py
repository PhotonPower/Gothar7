"""Rectified facade views from 360° images.

A facade is one edge of a building footprint (``buildings.json``: local [x, z], counter-clockwise
seen from above with north up) between ground and eave height. Every pixel of the frontal view is
a point on that vertical plane; its direction from the camera is sampled in the 360° image. This
needs no clicked points: the geometry comes from LoD2, the camera pose from GPS/SfM.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

import numpy as np

from gothar_worldgen.facade.equirect import CameraPose, Image, direction_to_pixel, sample


class FacadeError(Exception):
    """Unusable facade/camera combination; the message is meant for the user."""


@dataclass(frozen=True)
class Facade:
    """Vertical rectangle; bottom edge ``left`` to ``right`` (local x, z), seen from outside."""

    left: tuple[float, float]
    right: tuple[float, float]
    ground_y: float
    top_y: float

    @property
    def width_m(self) -> float:
        return math.dist(self.left, self.right)

    @property
    def height_m(self) -> float:
        return self.top_y - self.ground_y

    @property
    def normal(self) -> tuple[float, float]:
        """Outward unit normal (x, z)."""
        dx, dz = self.right[0] - self.left[0], self.right[1] - self.left[1]
        length = math.hypot(dx, dz)
        return -dz / length, dx / length

    @property
    def centre(self) -> np.ndarray:
        return np.array(
            [
                (self.left[0] + self.right[0]) / 2,
                (self.ground_y + self.top_y) / 2,
                (self.left[1] + self.right[1]) / 2,
            ]
        )


def facade_from_footprint(building: dict[str, Any], edge: int) -> Facade:
    """Facade of footprint edge ``edge`` (from point ``edge`` to point ``edge + 1``).

    Seen from outside, the start point of a counter-clockwise edge is on the left.
    Height: ground to eave (``roof.eaveY``); without a roof the building height.
    """
    footprint = building.get("footprint") or []
    if not 0 <= edge < len(footprint):
        raise FacadeError(
            f"building {building.get('id')}: no edge {edge} (footprint has {len(footprint)})"
        )
    a = footprint[edge]
    b = footprint[(edge + 1) % len(footprint)]
    ground = float(building.get("groundY", 0.0))
    roof = building.get("roof") or {}
    top = float(roof.get("eaveY", ground + float(building.get("heightM", 0.0))))
    if top <= ground or math.dist(a, b) <= 0:
        raise FacadeError(f"building {building.get('id')}: edge {edge} has no area")
    return Facade((float(a[0]), float(a[1])), (float(b[0]), float(b[1])), ground, top)


@dataclass(frozen=True)
class FacadeView:
    image: Image  # (rows, cols, channels); row 0 = top (eave), column 0 = left
    px_per_m: float
    distance_m: float  # camera to facade centre
    angle_deg: float  # between the view ray to the centre and the facade normal (0 = frontal)
    source_px_per_m: float  # detail the 360° image offers at the centre (higher = sharper)

    @property
    def quality(self) -> float:
        """0..1 score for picking the best image: frontal and close (sharp) is better."""
        frontal = max(0.0, math.cos(math.radians(self.angle_deg)))
        sharp = min(1.0, self.source_px_per_m / self.px_per_m)
        return frontal * sharp


def view_geometry(facade: Facade, pose: CameraPose, image_width: int) -> tuple[float, float, float]:
    """Distance, view angle (deg) and source pixel density at the facade centre."""
    to_centre = facade.centre - pose.position
    distance = float(np.linalg.norm(to_centre))
    nx, nz = facade.normal
    # Horizontal angle between "camera -> centre" and the inward normal.
    horiz = np.array([to_centre[0], to_centre[2]])
    horiz_len = float(np.linalg.norm(horiz))
    cos_angle = float(-(horiz[0] * nx + horiz[1] * nz) / horiz_len) if horiz_len > 0 else 1.0
    angle = math.degrees(math.acos(max(-1.0, min(1.0, cos_angle))))
    # An equirectangular image has width/(2*pi) pixels per radian; foreshortening shrinks it.
    density = image_width / (2 * math.pi) / max(distance, 1e-6) * max(cos_angle, 0.0)
    return distance, angle, density


def rectify(image: Image, pose: CameraPose, facade: Facade, px_per_m: float = 50.0) -> FacadeView:
    """Frontal view of ``facade`` at ``px_per_m`` from the 360° ``image`` taken at ``pose``."""
    if px_per_m <= 0:
        raise ValueError("px_per_m must be > 0")
    distance, angle, density = view_geometry(facade, pose, image.shape[1])
    if angle >= 90.0:
        raise FacadeError("the camera is behind the facade (it looks at the building from inside)")
    cols = max(1, round(facade.width_m * px_per_m))
    rows = max(1, round(facade.height_m * px_per_m))
    s = (np.arange(cols) + 0.5) / cols  # 0..1 from left to right
    t = (np.arange(rows) + 0.5) / rows  # 0..1 from top to bottom
    gs, gt = np.meshgrid(s, t)
    left = np.array(facade.left)
    right = np.array(facade.right)
    xz = left + (right - left) * gs[..., None]
    y = facade.top_y - (facade.top_y - facade.ground_y) * gt
    points = np.stack([xz[..., 0], y, xz[..., 1]], axis=-1)
    u, v = direction_to_pixel(points - pose.position, image.shape[1], image.shape[0], pose)
    return FacadeView(sample(image, u, v), px_per_m, distance, angle, density)
