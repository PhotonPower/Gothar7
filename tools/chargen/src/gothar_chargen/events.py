"""Animation events side-car file ``<set>.events.toml`` (characters-pipeline.md §3).

Format (version 1)::

    version = 1
    fps = 30                     # frame rate the frame numbers refer to

    [clips."none/s_walk"]
    events = [
        { frame = 0,  event = "footstep_l" },
        { frame = 15, event = "footstep_r" },
    ]
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path

from gothar_chargen.naming import is_clip_name, is_event_name

FORMAT_VERSION = 1
DEFAULT_FPS = 30


@dataclass(frozen=True)
class Event:
    frame: int
    name: str


@dataclass(frozen=True)
class EventFile:
    fps: int
    clips: dict[str, tuple[Event, ...]]


def events_path_for(glb: Path) -> Path:
    """``anims/human/1h.glb`` -> ``anims/human/1h.events.toml``."""
    return glb.with_name(glb.stem + ".events.toml")


def parse_events(text: str) -> tuple[EventFile | None, list[str]]:
    """Parses and checks an events file. Returns the parsed file (None if unusable) and errors."""
    errors: list[str] = []
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        return None, [f"invalid TOML: {e}"]

    unknown = set(data) - {"version", "fps", "clips"}
    if unknown:
        errors.append(f"unknown top-level keys: {sorted(unknown)}")
    version = data.get("version")
    if version != FORMAT_VERSION:
        errors.append(f"version must be {FORMAT_VERSION}, got {version!r}")
        return None, errors
    fps = data.get("fps", DEFAULT_FPS)
    if not isinstance(fps, int) or isinstance(fps, bool) or fps <= 0:
        errors.append(f"fps must be a positive integer, got {fps!r}")
        fps = DEFAULT_FPS

    clips_raw = data.get("clips", {})
    if not isinstance(clips_raw, dict):
        return None, [*errors, "clips must be a table"]
    clips: dict[str, tuple[Event, ...]] = {}
    for clip, table in clips_raw.items():
        if not is_clip_name(clip):
            errors.append(f"clip '{clip}': name violates the naming convention")
        if not isinstance(table, dict) or set(table) - {"events"}:
            errors.append(f"clip '{clip}': expected a table with only 'events'")
            continue
        events: list[Event] = []
        for i, ev in enumerate(table.get("events", [])):
            where = f"clip '{clip}' event #{i}"
            if not isinstance(ev, dict) or set(ev) != {"frame", "event"}:
                errors.append(f"{where}: expected {{ frame = <int>, event = <name> }}")
                continue
            frame, name = ev["frame"], ev["event"]
            if not isinstance(frame, int) or isinstance(frame, bool) or frame < 0:
                errors.append(f"{where}: frame must be an integer >= 0, got {frame!r}")
                continue
            if not isinstance(name, str) or not is_event_name(name):
                errors.append(f"{where}: invalid event name {name!r}")
                continue
            events.append(Event(frame, name))
        if [e.frame for e in events] != sorted(e.frame for e in events):
            errors.append(f"clip '{clip}': events must be sorted by frame")
        clips[clip] = tuple(events)
    return EventFile(fps=fps, clips=clips), errors


def load_events(path: Path) -> tuple[EventFile | None, list[str]]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as e:
        return None, [str(e)]
    return parse_events(text)
