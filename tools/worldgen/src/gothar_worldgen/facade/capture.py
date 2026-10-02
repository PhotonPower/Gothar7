"""Processing one capture: 360° video + GPX -> frames, synchronised time and camera poses.

Output in ``<work>/<site>/captures/<name>/``: ``frames/*.jpg`` and ``frames.json`` (see
``sync.build_manifest``). Raw videos stay where they are (``DATA_ROOT/capture/...``).
"""

from __future__ import annotations

import json
import math
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any, TextIO

import numpy as np
from PIL import Image as PilImage

from gothar_worldgen.facade.frames import extract_frames, find_ffprobe, frame_times, probe
from gothar_worldgen.facade.poses import DEFAULT_CAMERA_HEIGHT_M, Track
from gothar_worldgen.facade.sync import (
    StartEstimate,
    build_manifest,
    estimate_start,
    iso_utc,
    motion_signal,
    thumbnail,
)

_MOTION_WIDTH = 512  # frames are scaled down to this width before measuring motion


class CaptureError(Exception):
    """The capture cannot be processed; the message is meant for the user."""


@dataclass(frozen=True)
class CaptureOptions:
    every_s: float = 2.0
    start_utc: float | None = None  # given by hand: no estimation
    heading_offset_deg: float = 0.0
    camera_height_m: float = DEFAULT_CAMERA_HEIGHT_M


def frame_motion(paths: list[Path]) -> np.ndarray:
    thumbs = []
    for p in paths:
        with PilImage.open(p) as img:
            img.draft("RGB", (_MOTION_WIDTH, _MOTION_WIDTH // 2))
            small = img.convert("RGB").resize((_MOTION_WIDTH, _MOTION_WIDTH // 2))
        thumbs.append(thumbnail(np.asarray(small)))
    return motion_signal(thumbs)


def choose_start(
    track: Track,
    times: list[float],
    motion: np.ndarray,
    creation_utc: float | None,
    out: TextIO,
) -> tuple[float, str, StartEstimate | None]:
    """Reliable estimate, else video metadata (if inside the track), else the estimate."""
    estimate = None
    if len(motion) >= 3:
        try:
            estimate = estimate_start(track, times, motion)
        except ValueError as e:
            print(f"  sync: {e}", file=out)
    meta_ok = creation_utc is not None and track.start <= creation_utc <= track.end
    if estimate is not None and estimate.reliable:
        if creation_utc is not None:
            print(
                f"  sync: metadata differs by {estimate.start_utc - creation_utc:+.1f} s", file=out
            )
        return estimate.start_utc, "auto", estimate
    if meta_ok:
        print("  warning: automatic sync unreliable, using the video creation time", file=out)
        return float(creation_utc), "metadata", estimate
    if estimate is not None:
        print("  warning: automatic sync unreliable; check the poses or pass --start", file=out)
        return estimate.start_utc, "auto", estimate
    raise CaptureError("cannot determine the video start time; pass --start <ISO time>")


def process_capture(
    ffmpeg: Path,
    video: Path,
    track: Track,
    out_dir: Path,
    options: CaptureOptions,
    ground_y: Callable[[float, float], float] | None,
    out: TextIO,
) -> dict[str, Any]:
    info = probe(find_ffprobe(ffmpeg), video)
    if not info.equirectangular:
        raise CaptureError(
            f"{video.name}: {info.width} x {info.height} is not equirectangular (2:1); "
            "export the 360° video from Insta360 Studio first"
        )
    times = frame_times(info.duration_s, options.every_s)
    if not times:
        raise CaptureError(f"{video.name}: no frames (duration {info.duration_s:.1f} s)")
    print(f"  {video.name}: {info.width}x{info.height}, {info.duration_s:.1f} s, "
          f"{len(times)} frames every {options.every_s:g} s", file=out)  # fmt: skip
    paths = extract_frames(ffmpeg, video, times, out_dir / "frames")

    estimate = None
    if options.start_utc is not None:
        start, source = options.start_utc, "manual"
    else:
        start, source, estimate = choose_start(
            track, times, frame_motion(paths), info.creation_utc, out
        )
    if estimate is not None:
        print(f"  sync: start {iso_utc(start)} ({source}), correlation "
              f"{estimate.correlation:.2f}, margin {estimate.margin:.2f}", file=out)  # fmt: skip
    else:
        print(f"  sync: start {iso_utc(start)} ({source})", file=out)

    doc = build_manifest(
        video.name,
        [f"frames/{p.name}" for p in paths],
        times,
        start,
        source,
        track,
        ground_y,
        options.heading_offset_deg,
        options.camera_height_m,
        estimate,
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = out_dir / "frames.json"
    tmp = manifest.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(manifest)
    return doc


def terrain_ground(sample: Callable[[float, float], float]) -> Callable[[float, float], float]:
    """Ground height function that falls back to 0 outside the heightmap."""

    def ground(x: float, z: float) -> float:
        y = sample(x, z)
        return 0.0 if math.isnan(y) else y

    return ground
