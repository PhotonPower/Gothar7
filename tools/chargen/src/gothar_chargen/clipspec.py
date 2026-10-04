"""Clip lists for animation sets (data/clips/<set>.toml): where each clip comes from.

Format::

    set = "none"                                  # -> anims/human/none.glb
    depends = []                                  # other sets whose clips may be used here

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

    [[clip]]
    name = "1h/s_walk"
    layer = ["none/s_walk", "1h/s_idle"]          # legs from the first, upper body from the second
    from_bone = ["clavicle_l", "clavicle_r", "neck"]  # optional: bone(s) whose subtrees come
                                                      # from the second clip (default spine_02)

    [[clip]]
    name = "1h/t_attack_l"
    ...
    markers = { hit_start = 8, hit_end = 14 }     # fixed events (frame numbers)

    [[clip]]
    name = "amb/s_train_sword"
    from = "ual2:Sword_Regular_Combo"
    close = 12                                    # loops only: fade the last 12 frames into the
                                                  # first, for sources that do not loop
    in_place = true                               # no horizontal travel (sources that walk)

Monster sets (§7) name their rig: ``rig = "wolf"`` -> data/monsters/wolf.toml, clips
``wolf/<type>_<action>``, output monsters/wolf/anims/<set>.blend. Their clip sources are .blend
files written by ``gothar-chargen monster`` (bones already renamed: mapping "identity").

Pure Python: parsed and checked here, executed by blender/build_set.py.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from importlib import resources
from pathlib import Path

from gothar_chargen.naming import clip_mode, is_clip_name, is_event_name, is_monster_clip
from gothar_chargen.skeleton import SkeletonError, monster_rig_text

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
    "pose": (),
    "keyposes": (),
    "advance": ("base",),
    "hold": ("base",),
}
# optional parameters naming earlier clips
RECIPE_OPTIONAL: dict[str, tuple[str, ...]] = {"keyposes": ("base",)}
DEFAULT_LAYER_BONE = "spine_02"
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
    bones: tuple[str, ...] = ()  # for "layer": bones whose subtrees come from the second clip
    recipe: str = ""  # for "keyframe"
    params: tuple[tuple[str, object], ...] = ()  # for "keyframe" (sorted key/value pairs)
    helper: bool = False  # built for other clips only, not exported
    markers: tuple[tuple[str, int], ...] = ()  # fixed events (name, frame)
    close: int = 0  # loops: frames at the end faded into the first frame
    in_place: bool = False  # no horizontal travel of root and pelvis (sources that walk)

    @property
    def param(self) -> dict[str, object]:
        return dict(self.params)


@dataclass(frozen=True)
class SetSpec:
    set: str
    sources: dict[str, Source]
    clips: tuple[ClipSpec, ...]
    depends: tuple[str, ...] = ()
    rig: str = ""  # monster species; "" = human reference rig

    @property
    def names(self) -> list[str]:
        """Exported clips (without helpers)."""
        return [c.name for c in self.clips if not c.helper]

    def blend_path(self, characters: Path) -> Path:
        """Where the set's .blend (and the exported .glb) lives below assets/source/characters."""
        if self.rig:
            return characters / "monsters" / self.rig / "anims" / f"{self.set}.blend"
        return characters / "anims" / "human" / f"{self.set}.blend"


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


def parse_set_spec(data: dict, external: frozenset[str] = frozenset()) -> SetSpec:
    """Parses a clip list; `external` are clip names provided by the sets in `depends`."""
    set_name = data.get("set")
    if not isinstance(set_name, str) or not set_name:
        raise ClipSpecError("missing 'set'")
    sources: dict[str, Source] = {}
    for key, value in data.get("sources", {}).items():
        if not isinstance(value, dict) or not {"file", "mapping"} <= set(value):
            raise ClipSpecError(f"source '{key}': needs 'file' and 'mapping'")
        sources[key] = Source(str(value["file"]), str(value["mapping"]))

    depends = data.get("depends", [])
    if not isinstance(depends, list) or not all(isinstance(d, str) for d in depends):
        raise ClipSpecError("depends must be a list of set names")
    rig = data.get("rig", "")
    if rig:
        try:
            monster_rig_text(str(rig))
        except SkeletonError as e:
            raise ClipSpecError(f"rig: {e}") from e
    clips: list[ClipSpec] = []
    seen: set[str] = set()
    for i, raw in enumerate(data.get("clip", [])):
        name = raw.get("name")
        where = f"clip '{name}'" if name else f"clip #{i}"
        if not isinstance(name, str) or not is_clip_name(name):
            raise ClipSpecError(f"{where}: invalid clip name")
        if rig and clip_mode(name) != rig:
            raise ClipSpecError(f"{where}: clips of rig '{rig}' must be named '{rig}/...'")
        if not rig and is_monster_clip(name):
            raise ClipSpecError(f"{where}: monster clip in a set without 'rig'")
        if name in seen:
            raise ClipSpecError(f"{where}: duplicate")
        ops = [k for k in ("from", "reverse", "blend", "concat", "keyframe", "layer") if k in raw]
        if len(ops) != 1:
            raise ClipSpecError(
                f"{where}: needs exactly one of from/reverse/blend/concat/keyframe/layer"
            )
        op = ops[0]
        events = raw.get("events")
        if events is not None and events not in EVENT_KINDS:
            raise ClipSpecError(f"{where}: events must be one of {EVENT_KINDS}")

        def earlier(ref: object, where: str = where) -> str:
            if not isinstance(ref, str) or (ref not in seen and ref not in external):
                raise ClipSpecError(
                    f"{where}: '{ref}' is neither an earlier clip of this set nor in 'depends'"
                )
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
        elif op == "layer":
            pair = raw["layer"]
            if not isinstance(pair, list) or len(pair) != 2:
                raise ClipSpecError(f"{where}: layer needs [base_clip, upper_clip]")
            bones = raw.get("from_bone", DEFAULT_LAYER_BONE)
            bones = [bones] if isinstance(bones, str) else bones
            if (
                not isinstance(bones, list)
                or not bones
                or not all(isinstance(b, str) for b in bones)
            ):
                raise ClipSpecError(f"{where}: from_bone must be a bone name or a list of them")
            spec = ClipSpec(
                name, op, clips=(earlier(pair[0]), earlier(pair[1])), bones=tuple(bones)
            )
        elif op == "keyframe":
            recipe = raw["keyframe"]
            if recipe not in RECIPES:
                raise ClipSpecError(f"{where}: unknown keyframe recipe '{recipe}'")
            params = raw.get("params", {})
            if not isinstance(params, dict):
                raise ClipSpecError(f"{where}: params must be a table")
            refs = tuple(earlier(params.get(k)) for k in RECIPES[recipe])
            refs += tuple(
                earlier(params[k]) for k in RECIPE_OPTIONAL.get(recipe, ()) if k in params
            )
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
        markers = raw.get("markers", {})
        if not isinstance(markers, dict) or not all(
            is_event_name(k) and isinstance(v, int) and v >= 0 for k, v in markers.items()
        ):
            raise ClipSpecError(f"{where}: markers must map event names to frames >= 0")
        marker_list = tuple(sorted(markers.items(), key=lambda m: (m[1], m[0])))
        close = raw.get("close", 0)
        if not isinstance(close, int) or close < 0 or close == 1:
            raise ClipSpecError(f"{where}: close must be a number of frames >= 2")
        if close and not name.split("/")[-1].startswith("s_"):
            raise ClipSpecError(f"{where}: close is for loops (s_...) only")
        place = raw.get("in_place", False)
        if not isinstance(place, bool):
            raise ClipSpecError(f"{where}: in_place must be true or false")
        clips.append(
            ClipSpec(
                **{
                    **spec.__dict__,
                    "events": events,
                    "helper": helper,
                    "markers": marker_list,
                    "close": close,
                    "in_place": place,
                }
            )
        )
        seen.add(name)
    if not clips:
        raise ClipSpecError("no clips")
    return SetSpec(set_name, sources, tuple(clips), tuple(depends), str(rig))


def _read_spec_data(name_or_path: str | Path) -> dict:
    path = Path(name_or_path)
    if path.suffix == ".toml":
        text = path.read_text(encoding="utf-8")
    else:
        res = resources.files("gothar_chargen.data.clips").joinpath(f"{name_or_path}.toml")
        if not res.is_file():
            raise ClipSpecError(f"unknown clip list '{name_or_path}'")
        text = res.read_text(encoding="utf-8")
    try:
        return tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise ClipSpecError(str(e)) from e


def load_set_spec(name_or_path: str | Path, _chain: tuple[str, ...] = ()) -> SetSpec:
    """Loads a packaged clip list by set name (e.g. ``none``) or a .toml file.

    Sets in ``depends`` are loaded too (packaged lists only) so their clip names can be used.
    """
    data = _read_spec_data(name_or_path)
    external: set[str] = set()
    for dep in data.get("depends", []) if isinstance(data.get("depends"), list) else []:
        if dep in _chain or dep == data.get("set"):
            raise ClipSpecError(f"circular depends: {' -> '.join((*_chain, dep))}")
        dep_spec = load_set_spec(dep, (*_chain, str(data.get("set"))))
        external |= {c.name for c in dep_spec.clips} | dependency_names(dep_spec)
    return parse_set_spec(data, frozenset(external))


def dependency_names(spec: SetSpec) -> set[str]:
    """All clip names (incl. helpers) of the sets a spec depends on, recursively."""
    names: set[str] = set()
    for dep in spec.depends:
        dep_spec = load_set_spec(dep)
        names |= {c.name for c in dep_spec.clips} | dependency_names(dep_spec)
    return names


def packaged_sets(monsters: bool | None = None) -> list[str]:
    """Packaged clip lists; ``monsters`` = True/False filters monster/human sets."""
    folder = resources.files("gothar_chargen.data.clips")
    names = sorted(p.name[:-5] for p in folder.iterdir() if p.name.endswith(".toml"))
    if monsters is None:
        return names
    return [n for n in names if bool(_read_spec_data(n).get("rig")) == monsters]
