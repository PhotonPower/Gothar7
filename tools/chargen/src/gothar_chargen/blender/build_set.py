"""Builds an animation set from a clip list (data/clips/<set>.toml).

Output: anims/human/<set>.blend, for monster sets (``rig = "<species>"``) on the species rig
monsters/<species>/anims/<set>.blend.

Run inside Blender 4.5:
    blender --background --factory-startup --python build_set.py -- \
        --set none --sources <folder with source .glb files> --out <characters dir>

Clips come from source libraries (bones renamed via data/mappings/, the rest pose equals the
reference rig) or are derived from earlier clips (reverse, cross-fade blend, concat). Translation
keys are kept for root and pelvis only, scale keys are dropped (contract characters-pipeline.md §3).
Events are stored as pose markers on each action; export_glb.py writes them to <set>.events.toml.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import (  # noqa: E402
    delete_objects,
    import_glb,
    new_reference,
    purge_unused,
    save,
)
from gothar_chargen.blender.curves import (  # noqa: E402
    BONE_PATH,
    Curves,
    blend,
    close_loop,
    concat,
    hold_outside,
    in_place,
    layer,
    length,
    reverse,
)
from gothar_chargen.blender.keyframes import RECIPES, RigInfo  # noqa: E402
from gothar_chargen.clipspec import ClipSpec, SetSpec, SourceRef, load_set_spec  # noqa: E402
from gothar_chargen.events import detect_contacts  # noqa: E402
from gothar_chargen.mapping import BoneMap, load_mapping  # noqa: E402
from gothar_chargen.naming import ADDITIVE_ROOT, is_loop_clip  # noqa: E402
from gothar_chargen.postprocess import TRANSLATED_BONES  # noqa: E402
from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402

_KEPT = ("rotation_quaternion", "location")


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_set")
    parser.add_argument("--set", required=True, help="clip list name in data/clips/ or .toml path")
    parser.add_argument("--sources", type=Path, required=True, help="folder with source .glb files")
    parser.add_argument("--out", type=Path, required=True, help="assets/source/characters")
    return parser.parse_args(argv)


# --- keyframe data ---


def _read_source(
    action: bpy.types.Action, bone_map: BoneMap, rig: RigSpec, ref: SourceRef
) -> Curves:
    names = set(rig.names)
    start = action.frame_range[0] if ref.start is None else ref.start
    end = action.frame_range[1] if ref.end is None else ref.end
    curves: Curves = {}
    for fc in action.fcurves:
        m = BONE_PATH.match(fc.data_path)
        if not m:
            continue
        target = bone_map.target(m.group(1), names)
        prop = m.group(2)
        if target is None or prop not in _KEPT:
            continue
        if prop == "location" and target not in TRANSLATED_BONES:
            continue
        keys = [(k.co.x - start, k.co.y) for k in fc.keyframe_points if start <= k.co.x <= end]
        if not keys:  # range between keys: sample the curve
            keys = [(0.0, fc.evaluate(start)), (end - start, fc.evaluate(end))]
        curves[(f'pose.bones["{target}"].{prop}', fc.array_index)] = keys
    return curves


def _write_action(name: str, curves: Curves) -> bpy.types.Action:
    action = bpy.data.actions.new(name)
    for (path, index), keys in sorted(curves.items()):
        bone = BONE_PATH.match(path).group(1)
        fc = action.fcurves.new(path, index=index, action_group=bone)
        fc.keyframe_points.add(len(keys))
        fc.keyframe_points.foreach_set("co", [c for k in keys for c in k])
        for kp in fc.keyframe_points:
            kp.interpolation = "LINEAR"
        fc.update()
    return action


# --- events ---


def _assign(arm: bpy.types.Object, action: bpy.types.Action) -> None:
    arm.animation_data.action = action
    if action.slots:
        arm.animation_data.action_slot = action.slots[0]


def _foot_heights(arm: bpy.types.Object, action: bpy.types.Action, bone: str) -> list[float]:
    start, end = (int(round(f)) for f in action.frame_range)
    heights = []
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        heights.append((arm.matrix_world @ arm.pose.bones[bone].head).z)
    return heights


# foot bone -> footstep event; quadrupeds (contract §7) have four feet
_FEET = {
    "foot_l": "footstep_l",
    "foot_r": "footstep_r",
    "front_foot_l": "footstep_front_l",
    "front_foot_r": "footstep_front_r",
    "back_foot_l": "footstep_back_l",
    "back_foot_r": "footstep_back_r",
}


def _add_events(arm: bpy.types.Object, action: bpy.types.Action, clip: ClipSpec) -> None:
    for marker in list(action.pose_markers):
        action.pose_markers.remove(marker)
    events: list[tuple[int, str]] = [(frame, name) for name, frame in clip.markers]
    if clip.events is not None:
        events += _detected_events(arm, action, clip)
    for frame, name in sorted(events):
        action.pose_markers.new(name).frame = frame


def _detected_events(
    arm: bpy.types.Object, action: bpy.types.Action, clip: ClipSpec
) -> list[tuple[int, str]]:
    _assign(arm, action)
    cyclic = clip.name.rsplit("/", 1)[-1].startswith("s_")
    events: list[tuple[int, str]] = []
    if clip.events == "footsteps":
        for bone, event in _FEET.items():
            if bone in arm.pose.bones:
                heights = _foot_heights(arm, action, bone)
                events += [(f, event) for f in detect_contacts(heights, cyclic=cyclic)]
        return events
    left, right = _foot_heights(arm, action, "foot_l"), _foot_heights(arm, action, "foot_r")
    if clip.events == "land":
        both_down = [max(a, b) for a, b in zip(left, right, strict=True)]  # higher foot
        starts = detect_contacts(both_down, cyclic=False)
        events.append((starts[0] if starts else 0, "land"))
    return events


# --- set ---


class _Sources:
    """Source libraries, each .glb imported once (shared by a set and its dependencies)."""

    def __init__(self, folder: Path, rig: RigSpec) -> None:
        self.folder = folder
        self.rig = rig
        self.imported: list[bpy.types.Object] = []
        self._actions: dict[str, dict[str, bpy.types.Action]] = {}  # file -> actions

    def read(self, spec: SetSpec, ref: SourceRef) -> Curves:
        source = spec.sources[ref.library]
        if source.file not in self._actions:
            found = sorted(self.folder.rglob(source.file))
            if len(found) != 1:
                raise SystemExit(f"expected one {source.file} below {self.folder}")
            before = set(bpy.data.actions)
            if found[0].suffix == ".blend":  # monster clip source: actions only
                with bpy.data.libraries.load(str(found[0])) as (src, dst):
                    dst.actions = list(src.actions)
            else:
                self.imported += import_glb(found[0])
            self._actions[source.file] = {a.name: a for a in bpy.data.actions if a not in before}
        action = self._actions[source.file].get(ref.action)
        if action is None:
            raise SystemExit(f"{source.file}: no action '{ref.action}'")
        return _read_source(action, load_mapping(source.mapping), self.rig, ref)


def _compute(
    spec: SetSpec, sources: _Sources, rig_info: RigInfo, built: dict[str, Curves]
) -> list[ClipSpec]:
    """Computes all clips of `spec` (after its dependencies) into `built`; returns spec.clips."""
    for dep in spec.depends:
        dep_spec = load_set_spec(dep)
        if not all(c.name in built for c in dep_spec.clips):
            _compute(dep_spec, sources, rig_info, built)
            print(f"[chargen] dependency {dep} computed")
    for clip in spec.clips:
        if clip.op == "from":
            curves = sources.read(spec, clip.sources[0])
        elif clip.op == "concat":
            curves = concat([sources.read(spec, r) for r in clip.sources])
        elif clip.op == "reverse":
            curves = reverse(built[clip.clips[0]])
        elif clip.op == "blend":
            curves = blend(built[clip.clips[0]], built[clip.clips[1]], clip.frames, rig_info.bones)
        elif clip.op == "layer":
            upper = set().union(*(rig_info.subtree(bone) for bone in clip.bones))
            loop = is_loop_clip(clip.name)
            curves = layer(built[clip.clips[0]], built[clip.clips[1]], upper, rig_info.bones, loop)
        else:
            curves = RECIPES[clip.recipe](rig_info, clip.param, built)
        if clip.in_place:
            curves = in_place(curves, rig_info.rest, rig_info.bones)
        if clip.additive:
            curves = hold_outside(curves, rig_info.subtree(ADDITIVE_ROOT), rig_info.bones)
        if clip.close:
            curves = close_loop(curves, clip.close, rig_info.bones)
        built[clip.name] = curves
    return list(spec.clips)


def build_set(rig: RigSpec, spec: SetSpec, sources_dir: Path, out: Path) -> None:
    arm = new_reference(rig)
    arm.animation_data_create()
    rig_info = RigInfo(arm)
    sources = _Sources(sources_dir, rig)
    built: dict[str, Curves] = {}
    actions: list[bpy.types.Action] = []
    for clip in _compute(spec, sources, rig_info, built):
        kind = f"keyframe:{clip.recipe}" if clip.op == "keyframe" else clip.op
        if clip.helper:
            print(f"[chargen] helper {clip.name} ({kind})")
            continue
        curves = built[clip.name]
        action = _write_action(clip.name, curves)
        _add_events(arm, action, clip)
        actions.append(action)
        print(f"[chargen] clip {clip.name} ({kind}) frames 0-{int(length(curves))}")

    arm.animation_data.action = None
    for action in actions:
        track = arm.animation_data.nla_tracks.new()
        track.name = action.name
        track.strips.new(action.name, 0, action)
        track.mute = True
        action.use_fake_user = True
    delete_objects(sources.imported)
    purge_unused()
    bpy.context.scene.frame_set(0)
    save(spec.blend_path(out))


def main() -> None:
    args = _parse_args()
    spec = load_set_spec(args.set)
    rig = load_rig(species=spec.rig) if spec.rig else load_rig()
    build_set(rig, spec, args.sources, args.out)


if __name__ == "__main__":
    main()
