"""Synthetic capture walk: GPS track with walking and standing phases, matching 360° frames."""

from datetime import UTC, datetime

import numpy as np

from .test_facade_poses import FRAME, gpx  # noqa: F401  (FRAME re-exported for tests)

TRACK_START = "2026-10-04T07:00:00Z"
TRACK_START_UTC = datetime(2026, 10, 4, 7, 0, 0, tzinfo=UTC).timestamp()
SPEED = 1.4  # m/s while walking
# (from, to) seconds after the track start in which the walker moves; irregular on purpose.
WALKING = [(5, 21), (33, 38), (52, 75), (81, 84), (96, 118), (125, 140)]
TRACK_LENGTH = 150


def speed_at(t: np.ndarray) -> np.ndarray:
    """Speed (m/s) at seconds after the track start."""
    v = np.zeros_like(np.asarray(t, dtype=np.float64))
    for a, b in WALKING:
        v[(t >= a) & (t < b)] = SPEED
    return v


def distance_at(t: np.ndarray) -> np.ndarray:
    """Distance walked (m) at seconds after the track start."""
    t = np.asarray(t, dtype=np.float64)
    d = np.zeros_like(t)
    for a, b in WALKING:
        d += SPEED * np.clip(t - a, 0, b - a)
    return d


def walk_gpx() -> str:
    """Walking east along the line 5 m south of the origin, one point per second."""
    ts = np.arange(TRACK_LENGTH + 1)
    points = []
    for t, d in zip(ts, distance_at(ts), strict=True):
        m, s = divmod(int(t), 60)
        points.append((f"2026-10-04T07:{m:02d}:{s:02d}Z", 501115.0 + float(d), 5405342.0))
    return gpx(points)


def panorama(width: int = 512, seed: int = 7) -> np.ndarray:
    """Smooth random 360° texture (survives video compression)."""
    rng = np.random.default_rng(seed)
    coarse = rng.integers(0, 256, size=(8, 32, 3)).astype(np.uint8)
    return np.kron(coarse, np.ones((width // 16, width // 32, 1), dtype=np.uint8))


def frame_at(pano: np.ndarray, distance_m: float, px_per_m: float = 6.0) -> np.ndarray:
    """The view after walking ``distance_m``: the texture slides sideways while walking."""
    return np.roll(pano, int(round(distance_m * px_per_m)), axis=1)
