"""Clip lists for animation sets (data/clips/<set>.toml): where each clip comes from.

Format::

    set = "none"                                  # -> anims/human/none.glb

    [sources.ual1]                                # source libraries (file name + bone mapping)
    file = "AnimationLibrary_Godot_Standard.glb"
    mapping = "quaternius_ual1"

    [[clip]]
    name = "none/s_walk"
    from = "ual1:Walk_Loop"                       # <source>:<action>[<start>:<end>]
    events = "footsteps"                          # optional: footsteps | land

    [[clip]]
    name = "none/s_walk_back"
    reverse = "none/s_walk"                       # derived from an earlier clip of this set

    [[clip]]
    name = "none/t_walk_2_run"
    blend = ["none/s_walk", "none/s_run"]         # cross-fade over `frames` frames
    frames = 10

    [[clip]]
    name = "none/t_jump_run"
    concat = ["ual1:Jump_Loop[0:20]", "ual1:Jump_Land"]

    [[clip]]
    name = "none/s_strafe_l"
    keyframe = "strafe"                           # keyframe recipe (blender/keyframes.py)
    params = { base = "none/s_walk", side = "l" }

    [[clip]]
    name = "swim/s_forward"
    from = "ual1:Swim_Fwd_Loop"
    helper = true                                 # only used to build other clips, not exported

Pure Python: parsed and checked here, executed by blender/build_set.py.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from importlib import resources
from pathlib import Path

from gothar_chargen.naming import is_clip_name

EVENT_KINDS = ("footsteps", "land")

# Keyframe recipes (implemented in blender/keyframes.py) and their parameters that name earlier
# clips of the set. Other parameters are numbers or strings checked by the recipe itself.
RECIPES: dict[str, tuple[str, ...]] = {
    "strafe": ("base",),
    "turn": ("idle", "walk"),
    "scale_root": ("base",),
    "ladder": (),
    "ladder_on": ("idle",),
    "ladder_off": ("idle",),
    "yaw_wave": ("base",),
    "pitch": ("base",),
    "slide": (),
}
_SOURCE_REF = re.compile(
    r"^(?P<lib>[a-z0-9_]+):(?P<action>[^\[\]]+)(?:\[(?P<a>\d+):(?P<b>\d+)\])?$"
)


class ClipSpecError(Exception):
    """The clip list is malformed."""


@dataclass(frozen=True)
class SourceRef:
    library: str
    action: str
    start: int | None = None  # frame range within the source action (inclusive)
    end: int | None = None


@dataclass(frozen=True)
class Source:
    file: str
    mapping: str


@dataclass(frozen=True)
class ClipSpec:
    name: str
    op: str  # "from" | "reverse" | "blend" | "concat" | "keyframe"
    sources: tuple[SourceRef, ...] = ()  # for "from" / "concat"
    clips: tuple[str, ...] = ()  # for "reverse" / "blend": earlier clips of the set
    frames: int = 0  # for "blend"
    events: str | None = None
    recipe: str = ""  # for "keyframe"
    params: tuple[tuple[str, object], ...] = ()  # for "keyframe" (sorted key/value pairs)
    helper: bool = False  # built for other clips only, not exported

    @property
    def param(self) -> dict[str, object]:
        return dict(self.params)


@dataclass(frozen=True)
class SetSpec:
    set: str
    sources: dict[str, Source]
    clips: tuple[ClipSpec, ...]

    @property
    def names(self) -> list[str]:
        """Exported clips (without helpers)."""
        return [c.name for c in self.clips if not c.helper]


def parse_source_ref(text: str, libraries: dict[str, Source]) -> SourceRef:
    m = _SOURCE_REF.match(text)
    if not m:
        raise ClipSpecError(f"'{text}': expected <source>:<action>[<start>:<end>]")
    if m["lib"] not in libraries:
        raise ClipSpecError(f"'{text}': unknown source '{m['lib']}'")
    start = int(m["a"]) if m["a"] is not None else None
    end = int(m["b"]) if m["b"] is not None else None
    if start is not None and end is not None and end <= start:
        raise ClipSpecError(f"'{text}': empty frame range")
    return SourceRef(m["lib"], m["action"], start, end)


def parse_set_spec(data: dict) -> SetSpec:
    set_name = data.get("set")
    if not isinstance(set_name, str) or not set_name:
        raise ClipSpecError("missing 'set'")
    sources: dict[str, Source] = {}
    for key, value in data.get("sources", {}).items():
        if not isinstance(value, dict) or not {"file", "mapping"} <= set(value):
            raise ClipSpecError(f"source '{key}': needs 'file' and 'mapping'")
        sources[key] = Source(str(value["file"]), str(value["mapping"]))

    clips: list[ClipSpec] = []
    seen: set[str] = set()
    for i, raw in enumerate(data.get("clip", [])):
        name = raw.get("name")
        where = f"clip '{name}'" if name else f"clip #{i}"
        if not isinstance(name, str) or not is_clip_name(name):
            raise ClipSpecError(f"{where}: invalid clip name")
        if name in seen:
            raise ClipSpecError(f"{where}: duplicate")
        ops = [k for k in ("from", "reverse", "blend", "concat", "keyframe") if k in raw]
        if len(ops) != 1:
            raise ClipSpecError(f"{where}: needs exactly one of from/reverse/blend/concat/keyframe")
        op = ops[0]
        events = raw.get("events")
        if events is not None and events not in EVENT_KINDS:
            raise ClipSpecError(f"{where}: events must be one of {EVENT_KINDS}")

        def earlier(ref: object, where: str = where) -> str:
            if not isinstance(ref, str) or ref not in seen:
                raise ClipSpecError(f"{where}: '{ref}' is not an earlier clip of this set")
            return ref

        if op == "from":
            spec = ClipSpec(name, op, sources=(parse_source_ref(raw["from"], sources),))
        elif op == "concat":
            refs = raw["concat"]
            if not isinstance(refs, list) or len(refs) < 2:
                raise ClipSpecError(f"{where}: concat needs at least two sources")
            spec = ClipSpec(name, op, sources=tuple(parse_source_ref(r, sources) for r in refs))
        elif op == "reverse":
            spec = ClipSpec(name, op, clips=(earlier(raw["reverse"]),))
        elif op == "keyframe":
            recipe = raw["keyframe"]
            if recipe not in RECIPES:
                raise ClipSpecError(f"{where}: unknown keyframe recipe '{recipe}'")
            params = raw.get("params", {})
            if not isinstance(params, dict):
                raise ClipSpecError(f"{where}: params must be a table")
            refs = tuple(earlier(params.get(k)) for k in RECIPES[recipe])
            spec = ClipSpec(
                name, op, clips=refs, recipe=recipe, params=tuple(sorted(params.items()))
            )
        else:
            pair = raw["blend"]
            frames = raw.get("frames")
            if not isinstance(pair, list) or len(pair) != 2:
                raise ClipSpecError(f"{where}: blend needs [from_clip, to_clip]")
            if not isinstance(frames, int) or frames < 2:
                raise ClipSpecError(f"{where}: blend needs frames >= 2")
            spec = ClipSpec(name, op, clips=(earlier(pair[0]), earlier(pair[1])), frames=frames)
        helper = raw.get("helper", False)
        if not isinstance(helper, bool):
            raise ClipSpecError(f"{where}: helper must be true or false")
        clips.append(ClipSpec(**{**spec.__dict__, "events": events, "helper": helper}))
        seen.add(name)
    if not clips:
        raise ClipSpecError("no clips")
    return SetSpec(set_name, sources, tuple(clips))


def load_set_spec(name_or_path: str | Path) -> SetSpec:
    """Loads a packaged clip list by set name (e.g. ``none``) or a .toml file."""
    path = Path(name_or_path)
    if path.suffix == ".toml":
        text = path.read_text(encoding="utf-8")
    else:
        res = resources.files("gothar_chargen.data.clips").joinpath(f"{name_or_path}.toml")
        if not res.is_file():
            raise ClipSpecError(f"unknown clip list '{name_or_path}'")
        text = res.read_text(encoding="utf-8")
    try:
        return parse_set_spec(tomllib.loads(text))
    except tomllib.TOMLDecodeError as e:
        raise ClipSpecError(str(e)) from e


def packaged_sets() -> list[str]:
    folder = resources.files("gothar_chargen.data.clips")
    return sorted(p.name[:-5] for p in folder.iterdir() if p.name.endswith(".toml"))
