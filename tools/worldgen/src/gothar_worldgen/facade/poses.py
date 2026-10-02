"""Camera positions from GPS tracks and choosing the best image per facade.

Insta360 Studio exports the GPS track of a recording as GPX. A frame taken at time ``t`` gets its
position by linear interpolation; without a compass the heading follows the walking direction
(plus a fixed offset for how the camera was held). Positions are converted to the local engine
system like the geodata in W1 (EPSG:25832 via pyproj, then ``LocalFrame``).
"""

from __future__ import annotations

import bisect
import math
import xml.etree.ElementTree as ET
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import numpy as np
import numpy.typing as npt
from pyproj import Transformer

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.rectify import Facade, view_geometry
from gothar_worldgen.geo.frame import LocalFrame

GPX_NS = {"gpx": "http://www.topografix.com/GPX/1/1"}
# Camera height above ground for captures (stick above head, leonberg-pipeline.md §6).
DEFAULT_CAMERA_HEIGHT_M = 2.7


class TrackError(Exception):
    """Missing or broken GPS track; the message is meant for the user."""


@dataclass(frozen=True)
class TrackPoint:
    t: float  # seconds since the Unix epoch
    lat: float
    lon: float
    ele: float | None = None


def _parse_time(text: str) -> float:
    return datetime.fromisoformat(text.strip().replace("Z", "+00:00")).timestamp()


def parse_gpx(xml_text: str) -> list[TrackPoint]:
    """Track points (sorted by time) of all tracks in a GPX 1.1 document."""
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError as e:
        raise TrackError(f"invalid GPX: {e}") from None
    points: list[TrackPoint] = []
    for pt in root.iterfind(".//gpx:trkpt", GPX_NS):
        time = pt.findtext("gpx:time", namespaces=GPX_NS)
        if time is None:
            continue
        ele = pt.findtext("gpx:ele", namespaces=GPX_NS)
        try:
            points.append(
                TrackPoint(_parse_time(time), float(pt.attrib["lat"]), float(pt.attrib["lon"]),
                           float(ele) if ele else None)
            )  # fmt: skip
        except (KeyError, ValueError) as e:
            raise TrackError(f"invalid track point: {e}") from None
    if len(points) < 2:
        raise TrackError("GPX needs at least two timed track points")
    points.sort(key=lambda p: p.t)
    return points


def load_gpx(path: Path) -> list[TrackPoint]:
    try:
        return parse_gpx(path.read_text(encoding="utf-8"))
    except OSError as e:
        raise TrackError(f"{path}: {e}") from None


class Track:
    """A GPS track in local coordinates, interpolated over time."""

    def __init__(
        self, points: Sequence[TrackPoint], frame: LocalFrame, crs: str = "EPSG:25832"
    ) -> None:
        if len(points) < 2:
            raise TrackError("a track needs at least two points")
        to_utm = Transformer.from_crs("EPSG:4326", crs, always_xy=True)
        self.times = [p.t for p in points]
        self.xz: list[tuple[float, float]] = []
        for p in points:
            e, n = to_utm.transform(p.lon, p.lat)
            self.xz.append(frame.xz(e, n))

    @property
    def start(self) -> float:
        return self.times[0]

    @property
    def end(self) -> float:
        return self.times[-1]

    def position(self, t: float) -> tuple[float, float]:
        """Local (x, z) at time ``t`` (clamped to the track)."""
        i = self._segment(t)
        t0, t1 = self.times[i], self.times[i + 1]
        f = 0.0 if t1 <= t0 else min(1.0, max(0.0, (t - t0) / (t1 - t0)))
        (x0, z0), (x1, z1) = self.xz[i], self.xz[i + 1]
        return x0 + (x1 - x0) * f, z0 + (z1 - z0) * f

    def positions(self, ts: npt.ArrayLike) -> npt.NDArray[np.float64]:
        """Local (x, z) per time, shape (n, 2); clamped to the track like ``position``."""
        t = np.asarray(ts, dtype=np.float64)
        xs = np.interp(t, self.times, [p[0] for p in self.xz])
        zs = np.interp(t, self.times, [p[1] for p in self.xz])
        return np.stack([xs, zs], axis=-1)

    def speeds(self, ts: npt.ArrayLike, window_s: float = 2.0) -> npt.NDArray[np.float64]:
        """Ground speed (m/s) per time, central difference over ``window_s``."""
        t = np.asarray(ts, dtype=np.float64)
        a = self.positions(t - window_s / 2)
        b = self.positions(t + window_s / 2)
        return np.linalg.norm(b - a, axis=-1) / window_s

    def walking_heading(self, t: float) -> float:
        """Compass heading (degrees, clockwise from north) of the movement at time ``t``."""
        i = self._segment(t)
        # Skip standstill segments to find a direction.
        for j in [i, *range(i + 1, len(self.xz) - 1), *range(i - 1, -1, -1)]:
            (x0, z0), (x1, z1) = self.xz[j], self.xz[j + 1]
            if math.hypot(x1 - x0, z1 - z0) > 1e-3:
                return math.degrees(math.atan2(x1 - x0, -(z1 - z0))) % 360.0
        return 0.0

    def pose(
        self,
        t: float,
        heading_offset_deg: float = 0.0,
        ground_y: Callable[[float, float], float] | None = None,
        camera_height_m: float = DEFAULT_CAMERA_HEIGHT_M,
    ) -> CameraPose:
        """Camera pose at ``t``; height = ``ground_y(x, z)`` (else 0) + ``camera_height_m``."""
        x, z = self.position(t)
        y = (ground_y(x, z) if ground_y else 0.0) + camera_height_m
        return CameraPose(x, y, z, (self.walking_heading(t) + heading_offset_deg) % 360.0)

    def _segment(self, t: float) -> int:
        return min(max(bisect.bisect_right(self.times, t) - 1, 0), len(self.times) - 2)


@dataclass(frozen=True)
class Candidate:
    index: int  # into the list of poses
    distance_m: float
    angle_deg: float
    score: float  # higher is better


def rank_views(
    facade: Facade,
    poses: Sequence[CameraPose],
    image_width: int,
    max_distance_m: float = 40.0,
    max_angle_deg: float = 60.0,
) -> list[Candidate]:
    """Poses that see ``facade`` from outside, best first (frontal and close wins)."""
    ranked = []
    for i, pose in enumerate(poses):
        distance, angle, density = view_geometry(facade, pose, image_width)
        if distance > max_distance_m or angle > max_angle_deg:
            continue
        frontal = math.cos(math.radians(angle))
        ranked.append(Candidate(i, distance, angle, frontal * density))
    ranked.sort(key=lambda c: (-c.score, c.index))
    return ranked
