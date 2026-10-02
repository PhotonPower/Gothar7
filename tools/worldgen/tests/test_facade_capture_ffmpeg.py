"""End-to-end: synthetic 360° video (made with ffmpeg) + GPX -> frames.json; needs ffmpeg."""

import io
import json
import subprocess
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from gothar_worldgen.cli import EXIT_ERROR, EXIT_OK, main
from gothar_worldgen.facade.frames import ToolError, find_ffmpeg, find_ffprobe, probe
from gothar_worldgen.facade.sync import parse_utc

from .facade_walk import TRACK_START_UTC, distance_at, frame_at, panorama, walk_gpx

try:
    FFMPEG: Path | None = find_ffmpeg()
except ToolError:
    FFMPEG = None

pytestmark = pytest.mark.skipif(FFMPEG is None, reason="ffmpeg not installed")

VIDEO_OFFSET = 30.0  # the video starts 30 s after the track
VIDEO_LENGTH = 90
FPS = 2


def make_video(tmp_path: Path, width: int = 512) -> Path:
    pano = panorama(width)
    src = tmp_path / "src"
    src.mkdir()
    for i in range(VIDEO_LENGTH * FPS):
        t = VIDEO_OFFSET + i / FPS
        Image.fromarray(frame_at(pano, float(distance_at(np.array(t))))).save(src / f"f{i:04d}.png")
    video = tmp_path / "VID_TEST.mp4"
    subprocess.run(
        [str(FFMPEG), "-v", "error", "-y", "-framerate", str(FPS), "-i", str(src / "f%04d.png"),
         "-c:v", "mpeg4", "-q:v", "3", "-metadata", "creation_time=2030-01-01T00:00:00Z",
         str(video)],
        check=True,
    )  # fmt: skip
    return video


@pytest.fixture(scope="module")
def video(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return make_video(tmp_path_factory.mktemp("video"))


def test_probe_real_video(video: Path):
    info = probe(find_ffprobe(FFMPEG), video)
    assert (info.width, info.height) == (512, 256)
    assert info.duration_s == pytest.approx(VIDEO_LENGTH, abs=0.6)
    assert info.creation_utc is not None


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    return main(list(argv), out=out), out.getvalue()


def test_facade_frames_end_to_end(config_dir: Path, tmp_path: Path, video: Path, monkeypatch):
    data_root = tmp_path / "data"
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(data_root))
    gpx = tmp_path / "track.gpx"
    gpx.write_text(walk_gpx(), encoding="utf-8")
    base = ["--config-dir", str(config_dir), "facade", "frames", "testsite", str(video)]

    code, out = run(*base, "--gpx", str(gpx), "--every", "1")
    assert code == EXIT_OK, out
    capture = data_root / "work" / "testsite" / "captures" / "VID_TEST"
    doc = json.loads((capture / "frames.json").read_text(encoding="utf-8"))
    assert len(doc["frames"]) == VIDEO_LENGTH
    assert doc["startSource"] == "auto"  # the metadata (2030) lies outside the track
    assert doc["sync"]["reliable"]
    start = doc["frames"][0]
    assert start["utc"] == doc["startUtc"]
    assert parse_utc(doc["startUtc"]) == pytest.approx(TRACK_START_UTC + VIDEO_OFFSET, abs=1.0)
    assert (capture / start["file"]).is_file()
    assert "no terrain" in out  # no import ran in this data root

    # Manual start: frames are reused, only the poses change.
    mtime = (capture / start["file"]).stat().st_mtime_ns
    code, _ = run(*base, "--gpx", str(gpx), "--every", "1", "--start", "2026-10-04T07:00:10Z")
    assert code == EXIT_OK
    doc = json.loads((capture / "frames.json").read_text(encoding="utf-8"))
    assert doc["startSource"] == "manual"
    assert doc["frames"][0]["utc"] == "2026-10-04T07:00:10.000Z"
    assert (capture / start["file"]).stat().st_mtime_ns == mtime


def test_facade_frames_errors(config_dir: Path, tmp_path: Path, video: Path, monkeypatch, capsys):
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path / "data"))
    gpx = tmp_path / "track.gpx"
    gpx.write_text(walk_gpx(), encoding="utf-8")
    base = ["--config-dir", str(config_dir), "facade", "frames", "testsite"]
    assert run(*base, str(video), "--gpx", str(tmp_path / "none.gpx"))[0] == EXIT_ERROR
    assert run(*base, str(video), "--gpx", str(gpx), "--start", "soon")[0] == EXIT_ERROR
    assert run(*base, str(video), "--gpx", str(gpx), "--every", "0")[0] == EXIT_ERROR
    assert run(*base, str(tmp_path / "none.mp4"), "--gpx", str(gpx))[0] == EXIT_ERROR
    flat = tmp_path / "flat.mp4"
    subprocess.run([str(FFMPEG), "-v", "error", "-f", "lavfi", "-i", "color=c=gray:s=320x240:d=2",
                    "-c:v", "mpeg4", str(flat)], check=True)  # fmt: skip
    assert run(*base, str(flat), "--gpx", str(gpx))[0] == EXIT_ERROR
    assert "not equirectangular" in capsys.readouterr().err
