import json
from datetime import UTC, datetime
from pathlib import Path

import pytest

from gothar_worldgen.facade import frames
from gothar_worldgen.facade.frames import (
    ToolError,
    find_ffmpeg,
    find_ffprobe,
    frame_name,
    frame_times,
    parse_probe,
)


def probe_json(width: int = 5760, height: int = 2880, created: str | None = None) -> str:
    fmt: dict = {"duration": "12.48"}
    if created:
        fmt["tags"] = {"creation_time": created}
    return json.dumps(
        {
            "streams": [
                {"codec_type": "audio", "duration": "12.5"},
                {"codec_type": "video", "width": width, "height": height,
                 "avg_frame_rate": "30000/1001", "duration": "12.479"},
            ],
            "format": fmt,
        }
    )  # fmt: skip


def test_parse_probe():
    info = parse_probe(probe_json(created="2026-10-04T07:00:00.000000Z"))
    assert (info.width, info.height, info.equirectangular) == (5760, 2880, True)
    assert info.duration_s == pytest.approx(12.479)
    assert info.fps == pytest.approx(29.97, abs=0.01)
    assert info.creation_utc == datetime(2026, 10, 4, 7, tzinfo=UTC).timestamp()
    assert parse_probe(probe_json(1920, 1080)).equirectangular is False
    assert parse_probe(probe_json(created="garbage")).creation_utc is None


@pytest.mark.parametrize("text", ["{", json.dumps({"streams": [{"codec_type": "audio"}]})])
def test_parse_probe_errors(text):
    with pytest.raises(ToolError):
        parse_probe(text)


def test_frame_times_and_names():
    assert frame_times(5.0, 2.0) == [0.0, 2.0, 4.0]
    assert frame_times(4.0, 2.0) == [0.0, 2.0]  # 4.0 is the end, not a frame
    assert frame_times(3.0, 1.0, start_s=0.5) == [0.5, 1.5, 2.5]
    assert frame_times(0.0, 1.0) == []
    with pytest.raises(ValueError):
        frame_times(5.0, 0.0)
    assert frame_name("VID_1", 12.5) == "VID_1_00012500.jpg"


def fake(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")
    return path


@pytest.fixture
def no_tools(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Environment without any ffmpeg: empty PATH lookup, no env var, empty LOCALAPPDATA."""
    monkeypatch.delenv(frames.FFMPEG_ENV, raising=False)
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "appdata"))
    monkeypatch.setattr(frames.shutil, "which", lambda name: None)
    return tmp_path


def test_find_ffmpeg_search_order(no_tools: Path, monkeypatch: pytest.MonkeyPatch):
    with pytest.raises(ToolError, match="winget install Gyan.FFmpeg"):
        find_ffmpeg()
    winget = fake(
        no_tools
        / "appdata/Microsoft/WinGet/Packages/Gyan.FFmpeg_x/ffmpeg-9.0-full_build/bin/ffmpeg.exe"
    )
    assert find_ffmpeg() == winget
    on_path = fake(no_tools / "path/ffmpeg")
    monkeypatch.setattr(frames.shutil, "which", lambda name: str(on_path))
    assert find_ffmpeg() == on_path
    configured = fake(no_tools / "configured/ffmpeg")
    assert find_ffmpeg(configured=configured) == configured
    env = fake(no_tools / "env/ffmpeg")
    monkeypatch.setenv(frames.FFMPEG_ENV, str(env))
    assert find_ffmpeg(configured=configured) == env
    explicit = fake(no_tools / "explicit/ffmpeg")
    assert find_ffmpeg(explicit, configured) == explicit
    # A configured path that does not exist is skipped.
    assert find_ffmpeg(no_tools / "missing", configured) == env


def test_find_ffprobe(no_tools: Path):
    ffmpeg = fake(no_tools / "bin/ffmpeg.exe")
    with pytest.raises(ToolError):
        find_ffprobe(ffmpeg)
    probe = fake(no_tools / "bin/ffprobe.exe")
    assert find_ffprobe(ffmpeg) == probe
