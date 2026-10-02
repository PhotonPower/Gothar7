"""Aligning video time with the GPS track, and the frame manifest of a capture.

The video's own creation time is unreliable (it may be the export time, and the camera clock may
drift), so the start time is estimated from the data: the image changes between consecutive frames
while walking and stays nearly constant while standing. The start time at which the GPS ground speed
correlates best with this image motion wins. A start time given by hand always takes precedence.
"""

from __future__ import annotations

import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

import numpy as np
import numpy.typing as npt

from gothar_worldgen.facade.poses import DEFAULT_CAMERA_HEIGHT_M, Track

# Rows of the equirectangular image used for motion: the horizon band. The nadir shows the person
# holding the camera, the zenith mostly sky; neither changes with walking.
_BAND = (0.3, 0.7)
_THUMB = (32, 64)  # rows, cols


def thumbnail(image: npt.NDArray[np.uint8]) -> npt.NDArray[np.float64]:
    """Grey-scale horizon band, block-averaged to a small fixed size."""
    h = image.shape[0]
    band = image[int(h * _BAND[0]) : int(h * _BAND[1])].astype(np.float64)
    grey = band.mean(axis=-1) if band.ndim == 3 else band
    rows, cols = _THUMB
    ys = np.linspace(0, grey.shape[0], rows + 1).astype(int)
    xs = np.linspace(0, grey.shape[1], cols + 1).astype(int)
    out = np.empty((rows, cols))
    for i in range(rows):
        strip = grey[ys[i] : max(ys[i + 1], ys[i] + 1)]
        sums = np.add.reduceat(strip.mean(axis=0), xs[:-1])
        out[i] = sums / np.maximum(np.diff(xs), 1)
    return out


def motion_signal(thumbs: Sequence[npt.NDArray[np.float64]]) -> npt.NDArray[np.float64]:
    """Mean absolute change of consecutive thumbnails; value i lies between frames i and i+1."""
    return np.array(
        [float(np.mean(np.abs(b - a))) for a, b in zip(thumbs, thumbs[1:], strict=False)]
    )


@dataclass(frozen=True)
class StartEstimate:
    start_utc: float  # Unix time of video second 0
    correlation: float  # Pearson correlation of image motion and GPS speed at the best start
    margin: float  # best correlation minus the best one more than 10 s away (ambiguity)

    @property
    def reliable(self) -> bool:
        return self.correlation >= 0.5 and self.margin >= 0.1


def estimate_start(
    track: Track,
    times: Sequence[float],
    motion: npt.ArrayLike,
    step_s: float = 0.5,
    min_overlap: float = 0.8,
) -> StartEstimate:
    """Best video start time; ``times`` are the frame times (video seconds) behind ``motion``."""
    t = np.asarray(times, dtype=np.float64)
    m = np.asarray(motion, dtype=np.float64)
    if len(t) != len(m) + 1 or len(m) < 3:
        raise ValueError("need motion values between at least four frames")
    mids = (t[:-1] + t[1:]) / 2
    gaps = np.diff(t)
    span = t[-1] - t[0]
    # Every start that keeps at least min_overlap of the video inside the track.
    lo = track.start - t[0] - (1 - min_overlap) * span
    hi = track.end - t[-1] + (1 - min_overlap) * span
    if hi < lo:
        raise ValueError("the video is longer than the GPS track")
    starts = np.arange(lo, hi + step_s / 2, step_s)
    mz = m - m.mean()
    m_norm = np.linalg.norm(mz)
    scores = np.full(len(starts), -1.0)
    for i, s in enumerate(starts):
        abs_mid = s + mids
        inside = (abs_mid >= track.start) & (abs_mid <= track.end)
        if inside.mean() < min_overlap:
            continue
        speed = track.speeds(abs_mid, window_s=float(np.median(gaps)))
        sz = speed - speed.mean()
        denom = m_norm * np.linalg.norm(sz)
        if denom > 0:
            scores[i] = float(mz @ sz / denom)
    best = int(np.argmax(scores))
    far = np.abs(starts - starts[best]) > 10.0
    runner_up = float(scores[far].max()) if far.any() else -1.0
    best_score = float(scores[best])
    return StartEstimate(float(starts[best]), best_score, best_score - runner_up)


def iso_utc(t: float) -> str:
    return datetime.fromtimestamp(t, UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def build_manifest(
    video_name: str,
    files: Sequence[str],
    times: Sequence[float],
    start_utc: float,
    start_source: str,
    track: Track,
    ground_y: Callable[[float, float], float] | None = None,
    heading_offset_deg: float = 0.0,
    camera_height_m: float = DEFAULT_CAMERA_HEIGHT_M,
    estimate: StartEstimate | None = None,
) -> dict[str, Any]:
    """``frames.json`` of one capture: per frame file, video time, UTC time and camera pose."""
    frames = []
    for name, t in zip(files, times, strict=True):
        pose = track.pose(start_utc + t, heading_offset_deg, ground_y, camera_height_m)
        frames.append(
            {
                "file": name,
                "videoTime": round(t, 3),
                "utc": iso_utc(start_utc + t),
                "x": round(pose.x, 3),
                "y": round(pose.y, 3),
                "z": round(pose.z, 3),
                "headingDeg": round(pose.heading_deg, 2),
            }
        )
    doc: dict[str, Any] = {
        "video": video_name,
        "startUtc": iso_utc(start_utc),
        "startSource": start_source,  # "manual" | "auto" | "metadata"
        "headingOffsetDeg": heading_offset_deg,
        "cameraHeightM": camera_height_m,
    }
    if estimate is not None:
        doc["sync"] = {
            "correlation": round(estimate.correlation, 3),
            "margin": round(estimate.margin, 3),
            "reliable": estimate.reliable,
        }
    doc["frames"] = frames
    return doc


def parse_utc(text: str) -> float:
    """ISO 8601 time; without a zone it is taken as UTC."""
    dt = datetime.fromisoformat(text.strip().replace("Z", "+00:00"))
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    value = dt.timestamp()
    if not math.isfinite(value):
        raise ValueError(text)
    return value
