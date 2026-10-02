"""Still frames from exported 360° videos via the external tool ffmpeg.

Insta360 Studio exports a recording as an equirectangular MP4 (2:1). ffmpeg is an external
program like Blender, not a Python dependency. Search order: ``--ffmpeg``, ``$G7_FFMPEG``,
``paths.ffmpeg`` in ``local.toml``, ``PATH``, then the winget installation folder.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

FFMPEG_ENV = "G7_FFMPEG"
_EXE = ".exe" if os.name == "nt" else ""


class ToolError(Exception):
    """ffmpeg/ffprobe missing or failing; the message is meant for the user."""


def _winget_candidates() -> list[Path]:
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return []
    winget = Path(local) / "Microsoft" / "WinGet"
    found = [winget / "Links" / "ffmpeg.exe"]
    packages = winget / "Packages"
    if packages.is_dir():
        found.extend(sorted(packages.glob("Gyan.FFmpeg*/ffmpeg-*/bin/ffmpeg.exe"), reverse=True))
    return found


def find_ffmpeg(explicit: Path | None = None, configured: Path | None = None) -> Path:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    if os.environ.get(FFMPEG_ENV):
        candidates.append(Path(os.environ[FFMPEG_ENV]))
    if configured is not None:
        candidates.append(configured)
    on_path = shutil.which("ffmpeg")
    if on_path:
        candidates.append(Path(on_path))
    candidates.extend(_winget_candidates())
    for c in candidates:
        if c.is_file():
            return c
    raise ToolError(
        f"ffmpeg not found; pass --ffmpeg, set {FFMPEG_ENV} or paths.ffmpeg in local.toml "
        "(Windows: winget install Gyan.FFmpeg)"
    )


def find_ffprobe(ffmpeg: Path) -> Path:
    """ffprobe next to ffmpeg (both ship together), else on PATH."""
    sibling = ffmpeg.with_name(f"ffprobe{ffmpeg.suffix}")
    if sibling.is_file():
        return sibling
    on_path = shutil.which("ffprobe")
    if on_path:
        return Path(on_path)
    raise ToolError(f"ffprobe not found next to {ffmpeg} or on PATH")


@dataclass(frozen=True)
class VideoInfo:
    duration_s: float
    width: int
    height: int
    fps: float
    creation_utc: float | None  # Unix time from the container metadata, if present

    @property
    def equirectangular(self) -> bool:
        return self.width == 2 * self.height


def _rate(text: str) -> float:
    num, _, den = text.partition("/")
    try:
        return float(num) / float(den or 1)
    except (ValueError, ZeroDivisionError):
        return 0.0


def parse_probe(text: str) -> VideoInfo:
    """VideoInfo from ``ffprobe -print_format json -show_format -show_streams`` output."""
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as e:
        raise ToolError(f"unexpected ffprobe output: {e}") from None
    video = next((s for s in doc.get("streams", []) if s.get("codec_type") == "video"), None)
    if video is None:
        raise ToolError("no video stream")
    fmt = doc.get("format", {})
    duration = float(video.get("duration") or fmt.get("duration") or 0.0)
    created = (fmt.get("tags") or {}).get("creation_time") or (video.get("tags") or {}).get(
        "creation_time"
    )
    creation = None
    if created:
        try:
            creation = datetime.fromisoformat(created.replace("Z", "+00:00")).timestamp()
        except ValueError:
            creation = None
    return VideoInfo(
        duration,
        int(video["width"]),
        int(video["height"]),
        _rate(video.get("avg_frame_rate") or video.get("r_frame_rate") or "0"),
        creation,
    )


def _run(cmd: list[str]) -> str:
    try:
        done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", check=False)
    except OSError as e:
        raise ToolError(f"{cmd[0]}: {e}") from None
    if done.returncode != 0:
        tail = (done.stderr or "").strip().splitlines()[-3:]
        raise ToolError(f"{Path(cmd[0]).name} failed: {' | '.join(tail)}")
    return done.stdout


def probe(ffprobe: Path, video: Path) -> VideoInfo:
    if not video.is_file():
        raise ToolError(f"{video} not found")
    cmd = [str(ffprobe), "-v", "error", "-print_format", "json", "-show_format", "-show_streams"]
    return parse_probe(_run([*cmd, str(video)]))


def frame_times(duration_s: float, every_s: float, start_s: float = 0.0) -> list[float]:
    """Sample times ``start, start + every, ...`` strictly inside the video."""
    if every_s <= 0:
        raise ValueError("every_s must be > 0")
    times = []
    k = 0
    while (t := start_s + k * every_s) < duration_s - 1e-6:
        times.append(round(t, 3))
        k += 1
    return times


def frame_name(stem: str, t: float) -> str:
    return f"{stem}_{round(t * 1000):08d}.jpg"


def extract_frames(
    ffmpeg: Path, video: Path, times: list[float], out_dir: Path, quality: int = 2
) -> list[Path]:
    """One JPEG per time (``<stem>_<ms>.jpg``); existing files are kept, so re-runs are cheap."""
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for t in times:
        path = out_dir / frame_name(video.stem, t)
        if not path.is_file():
            tmp = path.with_name(f"{path.stem}.tmp.jpg")
            _run([str(ffmpeg), "-v", "error", "-y", "-ss", f"{t:.3f}", "-i", str(video),
                  "-frames:v", "1", "-q:v", str(quality), str(tmp)])  # fmt: skip
            if not tmp.is_file():
                raise ToolError(f"ffmpeg wrote no frame at {t:.3f} s of {video.name}")
            tmp.replace(path)
        paths.append(path)
    return paths
