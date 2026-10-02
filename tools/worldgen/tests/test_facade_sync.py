import io
from datetime import UTC, datetime

import numpy as np
import pytest

from gothar_worldgen.facade.capture import choose_start, terrain_ground
from gothar_worldgen.facade.poses import Track, parse_gpx
from gothar_worldgen.facade.sync import (
    build_manifest,
    estimate_start,
    iso_utc,
    motion_signal,
    parse_utc,
    thumbnail,
)

from .facade_walk import (
    FRAME,
    TRACK_START_UTC,
    distance_at,
    frame_at,
    panorama,
    speed_at,
    walk_gpx,
)


@pytest.fixture(scope="module")
def track() -> Track:
    return Track(parse_gpx(walk_gpx()), FRAME)


def test_track_speeds(track: Track):
    t = TRACK_START_UTC + np.array([0.0, 10.0, 25.0, 60.0])
    np.testing.assert_allclose(track.speeds(t), [0.0, 1.4, 0.0, 1.4], atol=0.05)


def test_thumbnail_and_motion():
    pano = panorama()
    thumb = thumbnail(pano)
    assert thumb.shape == (32, 64)
    still = motion_signal([thumbnail(frame_at(pano, 0)), thumbnail(frame_at(pano, 0))])
    moved = motion_signal([thumbnail(frame_at(pano, 0)), thumbnail(frame_at(pano, 3))])
    assert still[0] == 0.0
    assert moved[0] > 10.0


def synthetic_motion(video_start: float, times: np.ndarray, noise: float = 0.15) -> np.ndarray:
    """Image motion a camera would show: proportional to speed, plus noise."""
    rng = np.random.default_rng(1)
    mids = (times[:-1] + times[1:]) / 2 + (video_start - TRACK_START_UTC)
    return speed_at(mids) * 10.0 + rng.normal(0, noise * 10.0, len(mids)).clip(-5, 5) + 2.0


@pytest.mark.parametrize("offset", [0.0, 17.0, 43.5])
def test_estimate_start_recovers_the_true_start(track: Track, offset: float):
    times = np.arange(0.0, 90.0, 2.0)
    true_start = TRACK_START_UTC + offset
    est = estimate_start(track, times, synthetic_motion(true_start, times))
    assert est.start_utc == pytest.approx(true_start, abs=1.0)
    assert est.reliable
    assert est.correlation > 0.8


def test_estimate_start_flags_featureless_motion(track: Track):
    times = np.arange(0.0, 60.0, 2.0)
    rng = np.random.default_rng(5)
    est = estimate_start(track, times, rng.normal(5, 1, len(times) - 1))
    assert not est.reliable


def test_estimate_start_errors(track: Track):
    with pytest.raises(ValueError):
        estimate_start(track, [0, 1, 2], [1, 2])
    with pytest.raises(ValueError, match="longer"):
        estimate_start(track, np.arange(0.0, 400.0, 2.0), np.ones(199))


def test_choose_start_prefers_reliable_estimate_then_metadata(track: Track):
    times = list(np.arange(0.0, 90.0, 2.0))
    true_start = TRACK_START_UTC + 17.0
    motion = synthetic_motion(true_start, np.array(times))
    out = io.StringIO()
    start, source, est = choose_start(track, times, motion, TRACK_START_UTC + 20.0, out)
    assert source == "auto" and start == pytest.approx(true_start, abs=1.0)
    diff = float(out.getvalue().split("differs by")[1].split()[0])
    assert diff == pytest.approx(-3.0, abs=1.0)

    flat = np.full(len(times) - 1, 3.0)
    start, source, _ = choose_start(track, times, flat, TRACK_START_UTC + 20.0, io.StringIO())
    assert (start, source) == (TRACK_START_UTC + 20.0, "metadata")
    # Metadata outside the track (e.g. the export time) is not trusted.
    out = io.StringIO()
    _, source, _ = choose_start(track, times, flat, TRACK_START_UTC + 86400.0, out)
    assert source == "auto" and "pass --start" in out.getvalue()


def test_build_manifest(track: Track):
    doc = build_manifest(
        "VID_0001.mp4",
        ["frames/a.jpg", "frames/b.jpg"],
        [0.0, 10.0],
        TRACK_START_UTC + 5.0,
        "manual",
        track,
        ground_y=lambda x, z: 2.0,
        heading_offset_deg=180.0,
    )
    assert doc["startUtc"] == "2026-10-04T07:00:05.000Z"
    assert doc["startSource"] == "manual" and "sync" not in doc
    a, b = doc["frames"]
    assert a["utc"] == "2026-10-04T07:00:05.000Z"
    assert a["x"] == pytest.approx(0.0, abs=0.05)
    assert b["x"] == pytest.approx(float(distance_at(np.array(15.0))), abs=0.05)
    assert b["z"] == pytest.approx(5.0, abs=0.05)
    assert b["y"] == pytest.approx(2.0 + 2.7)
    assert b["headingDeg"] == pytest.approx(270.0, abs=0.5)  # walking east, camera turned around


def test_utc_helpers():
    assert parse_utc("2026-10-04T07:00:00Z") == TRACK_START_UTC
    assert parse_utc("2026-10-04T07:00:00") == TRACK_START_UTC  # no zone: UTC
    assert parse_utc("2026-10-04T09:00:00+02:00") == TRACK_START_UTC
    assert iso_utc(TRACK_START_UTC + 0.25) == "2026-10-04T07:00:00.250Z"
    assert datetime.fromtimestamp(TRACK_START_UTC, UTC).year == 2026
    with pytest.raises(ValueError):
        parse_utc("yesterday")


def test_terrain_ground_falls_back_outside_the_heightmap():
    ground = terrain_ground(lambda x, z: float("nan") if x > 10 else 3.0)
    assert ground(0, 0) == 3.0
    assert ground(20, 0) == 0.0
