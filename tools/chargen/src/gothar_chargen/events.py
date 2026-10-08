"""Animation events side-car file ``<set>.events.toml`` (characters-pipeline.md §3).

Format (version 1)::

    version = 1
    fps = 30                     # frame rate the frame numbers refer to

    [clips."none/s_walk"]
    speed = 0.98                 # optional: natural ground speed in m/s (locomotion clips, §3)
    events = [
        { frame = 0,  event = "footstep_l" },
        { frame = 15, event = "footstep_r" },
    ]

``speed`` is measured from the clip (clipspeed.py; ``gothar-chargen speeds``): played at this
movement speed the feet do not slide; the engine scales the playback rate to the movement speed.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
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
    speeds: dict[str, float] = field(default_factory=dict)  # clip -> natural speed (m/s)


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
    speeds: dict[str, float] = {}
    for clip, table in clips_raw.items():
        if not is_clip_name(clip):
            errors.append(f"clip '{clip}': name violates the naming convention")
        if not isinstance(table, dict) or set(table) - {"events", "speed"}:
            errors.append(f"clip '{clip}': expected a table with 'events' and/or 'speed'")
            continue
        if "speed" in table:
            speed = table["speed"]
            if isinstance(speed, bool) or not isinstance(speed, int | float) or speed <= 0:
                errors.append(f"clip '{clip}': speed must be a number > 0 (m/s), got {speed!r}")
            else:
                speeds[clip] = float(speed)
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
    return EventFile(fps=fps, clips=clips, speeds=speeds), errors


def load_events(path: Path) -> tuple[EventFile | None, list[str]]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as e:
        return None, [str(e)]
    return parse_events(text)


def detect_contacts(
    heights: list[float], tolerance: float = 0.02, cyclic: bool = True
) -> list[int]:
    """Frames where a foot starts touching the ground.

    ``heights[i]`` is the foot height at frame i. A frame counts as contact when the foot is within
    ``tolerance`` of its lowest point; an event fires on the first frame of each contact phase.
    For looping clips (``cyclic``) the last frame repeats the first and is ignored, and a contact
    phase may wrap around the end. A foot that never lifts (idle) yields no events.
    """
    if len(heights) < 2:
        return []
    h = heights[:-1] if cyclic else heights
    low = min(h)
    contact = [v <= low + tolerance for v in h]
    if all(contact):
        return []
    n = len(h)
    starts = []
    for i in range(n):
        prev = contact[i - 1] if (cyclic or i > 0) else False
        if contact[i] and not prev:
            starts.append(i)
    return starts


def format_events(
    fps: int, clips: dict[str, list[Event]], speeds: dict[str, float] | None = None
) -> str:
    """Writes an events file (format version 1); clips without events and speed are left out."""
    speeds = speeds or {}
    lines = [f"version = {FORMAT_VERSION}", f"fps = {fps}"]
    for clip in sorted(set(clips) | set(speeds)):
        events = sorted(clips.get(clip, []), key=lambda e: (e.frame, e.name))
        if not events and clip not in speeds:
            continue
        lines += ["", f'[clips."{clip}"]']
        if clip in speeds:
            lines.append(f"speed = {speeds[clip]:.2f}")
        if events:
            lines.append("events = [")
            lines += [f'    {{ frame = {e.frame}, event = "{e.name}" }},' for e in events]
            lines.append("]")
    return "\n".join(lines) + "\n"


SPEED_TOLERANCE = 0.03  # relative: stored speeds may differ this much from the measured ones


def update_speeds(glb: Path) -> dict[str, float]:
    """Measures the locomotion clips of a set file and writes their speeds into its events file
    (events and the header comment stay). Returns the speeds."""
    from gothar_chargen.clipspeed import clip_speeds
    from gothar_chargen.gltf import Gltf

    speeds = clip_speeds(Gltf.load(glb))
    path = events_path_for(glb)
    header = "# Generated by gothar-chargen export from pose markers (edit the markers).\n"
    fps, clips = DEFAULT_FPS, {}
    if path.is_file():
        text = path.read_text(encoding="utf-8")
        header = "".join(line + "\n" for line in text.splitlines() if line.startswith("#"))
        parsed, errors = parse_events(text)
        if parsed is None or errors:
            raise ValueError(f"{path.name}: {errors}")
        fps, clips = parsed.fps, {c: list(e) for c, e in parsed.clips.items()}
    if not speeds and not clips:
        return speeds
    path.write_text(header + format_events(fps, clips, speeds), encoding="utf-8", newline="\n")
    return speeds


# events the Blender build detects from the motion (build_set.py: footsteps, landing)
DETECTED_EVENTS = frozenset(
    {"footstep_l", "footstep_r", "land"}
    | {f"footstep_{e}_{s}" for e in ("front", "back") for s in ("l", "r")}
)


def sync_marker_events(
    clips: list[tuple[str, tuple[tuple[str, int], ...], str | None]], path: Path
) -> dict[str, list[Event]]:
    """Rewrites ``path`` with the fixed events (markers) from the specs, without rebuilding the
    set in Blender (``build-set --events-only``, characters-pipeline.md §3). ``clips`` = (name,
    markers, events recipe) of the exported clips. Detected events (DETECTED_EVENTS, from clips with
    an events recipe) and the speeds stay as measured; the clips' motion must be unchanged."""
    parsed, errors = load_events(path)
    if parsed is None or errors:
        raise ValueError(f"{path.name}: {errors}")
    text = path.read_text(encoding="utf-8")
    header = "".join(line + "\n" for line in text.splitlines() if line.startswith("#"))
    out: dict[str, list[Event]] = {}
    for name, markers, recipe in clips:
        kept = {
            (e.frame, e.name)
            for e in parsed.clips.get(name, ())
            if recipe is not None and e.name in DETECTED_EVENTS
        }
        kept |= {(int(frame), event) for event, frame in markers}
        out[name] = [Event(f, n) for f, n in sorted(kept)]
    speeds = {c: s for c, s in parsed.speeds.items() if c in out}
    path.write_text(header + format_events(parsed.fps, out, speeds), encoding="utf-8", newline="\n")
    return out
