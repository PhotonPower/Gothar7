"""Builds an animation set (anims/human/<set>.blend) from a clip list (data/clips/<set>.toml).

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
import re
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]
from mathutils import Quaternion  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import (  # noqa: E402
    delete_objects,
    import_glb,
    new_reference,
    purge_unused,
    save,
)
from gothar_chargen.clipspec import ClipSpec, SetSpec, SourceRef, load_set_spec  # noqa: E402
from gothar_chargen.events import detect_contacts  # noqa: E402
from gothar_chargen.mapping import BoneMap, load_mapping  # noqa: E402
from gothar_chargen.postprocess import TRANSLATED_BONES  # noqa: E402
from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402

_BONE_PATH = re.compile(r'^pose\.bones\["([^"]+)"\]\.(\w+)$')
_KEPT = ("rotation_quaternion", "location")


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_set")
    parser.add_argument("--set", required=True, help="clip list name in data/clips/ or .toml path")
    parser.add_argument("--sources", type=Path, required=True, help="folder with source .glb files")
    parser.add_argument("--out", type=Path, required=True, help="assets/source/characters")
    return parser.parse_args(argv)


# --- keyframe data ---

# A clip in memory: {(data_path, index): [(frame, value), ...]}, frames starting at 0.
Curves = dict[tuple[str, int], list[tuple[float, float]]]


def _read_source(
    action: bpy.types.Action, bone_map: BoneMap, rig: RigSpec, ref: SourceRef
) -> Curves:
    names = set(rig.names)
    start = action.frame_range[0] if ref.start is None else ref.start
    end = action.frame_range[1] if ref.end is None else ref.end
    curves: Curves = {}
    for fc in action.fcurves:
        m = _BONE_PATH.match(fc.data_path)
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


def _length(curves: Curves) -> float:
    return max((k[-1][0] for k in curves.values() if k), default=0.0)


def _evaluate(keys: list[tuple[float, float]], frame: float) -> float:
    if frame <= keys[0][0]:
        return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:], strict=False):
        if frame <= f1:
            t = (frame - f0) / (f1 - f0) if f1 > f0 else 0.0
            return v0 + (v1 - v0) * t
    return keys[-1][1]


def _reverse(curves: Curves) -> Curves:
    end = _length(curves)
    return {key: sorted((end - f, v) for f, v in keys) for key, keys in curves.items()}


def _concat(parts: list[Curves]) -> Curves:
    result: Curves = {}
    offset = 0.0
    for part in parts:
        for key, keys in part.items():
            dest = result.setdefault(key, [])
            dest.extend(
                (f + offset, v) for f, v in keys if not (dest and f + offset <= dest[-1][0])
            )
        offset += _length(part) + 1.0
    return result


def _blend(a: Curves, b: Curves, frames: int) -> Curves:
    """Cross-fade from clip a (looping) to clip b over `frames` frames (smoothstep weight)."""
    len_a = _length(a) or 1.0
    bones: dict[str, set[str]] = {}
    for path, _ in set(a) | set(b):
        m = _BONE_PATH.match(path)
        if m:
            bones.setdefault(m.group(1), set()).add(m.group(2))
    result: Curves = {}
    for bone, props in bones.items():
        for prop in props:
            path = f'pose.bones["{bone}"].{prop}'
            size = 4 if prop == "rotation_quaternion" else 3
            rest = (1.0, 0.0, 0.0, 0.0) if size == 4 else (0.0, 0.0, 0.0)
            for i in range(size):
                result[(path, i)] = []
            for frame in range(frames + 1):
                t = frame / frames
                w = t * t * (3 - 2 * t)
                va = [
                    _evaluate(a[(path, i)], frame % len_a) if (path, i) in a else rest[i]
                    for i in range(size)
                ]
                vb = [
                    _evaluate(b[(path, i)], frame) if (path, i) in b else rest[i]
                    for i in range(size)
                ]
                if size == 4:
                    value = list(Quaternion(va).slerp(Quaternion(vb), w))
                else:
                    value = [x + (y - x) * w for x, y in zip(va, vb, strict=True)]
                for i in range(size):
                    result[(path, i)].append((float(frame), value[i]))
    return result


def _write_action(name: str, curves: Curves) -> bpy.types.Action:
    action = bpy.data.actions.new(name)
    for (path, index), keys in sorted(curves.items()):
        bone = _BONE_PATH.match(path).group(1)
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


def _add_events(arm: bpy.types.Object, action: bpy.types.Action, clip: ClipSpec) -> None:
    for marker in list(action.pose_markers):
        action.pose_markers.remove(marker)
    if clip.events is None:
        return
    _assign(arm, action)
    left, right = _foot_heights(arm, action, "foot_l"), _foot_heights(arm, action, "foot_r")
    cyclic = clip.name.rsplit("/", 1)[-1].startswith("s_")
    events: list[tuple[int, str]] = []
    if clip.events == "footsteps":
        events += [(f, "footstep_l") for f in detect_contacts(left, cyclic=cyclic)]
        events += [(f, "footstep_r") for f in detect_contacts(right, cyclic=cyclic)]
    elif clip.events == "land":
        both_down = [max(a, b) for a, b in zip(left, right, strict=True)]  # higher foot
        starts = detect_contacts(both_down, cyclic=False)
        events.append((starts[0] if starts else 0, "land"))
    for frame, name in sorted(events):
        action.pose_markers.new(name).frame = frame


# --- set ---


def build_set(rig: RigSpec, spec: SetSpec, sources: Path, out: Path) -> None:
    arm = new_reference(rig)
    arm.animation_data_create()
    libraries: dict[str, dict[str, bpy.types.Action]] = {}
    maps: dict[str, BoneMap] = {}
    imported: list[bpy.types.Object] = []
    for key, source in spec.sources.items():
        found = sorted(sources.rglob(source.file))
        if len(found) != 1:
            raise SystemExit(f"source '{key}': expected one {source.file} below {sources}")
        before = set(bpy.data.actions)
        imported += import_glb(found[0])
        libraries[key] = {a.name: a for a in bpy.data.actions if a not in before}
        maps[key] = load_mapping(source.mapping)

    def read(ref: SourceRef) -> Curves:
        action = libraries[ref.library].get(ref.action)
        if action is None:
            raise SystemExit(f"{ref.library}: no action '{ref.action}'")
        return _read_source(action, maps[ref.library], rig, ref)

    built: dict[str, Curves] = {}
    actions: list[bpy.types.Action] = []
    for clip in spec.clips:
        if clip.op == "from":
            curves = read(clip.sources[0])
        elif clip.op == "concat":
            curves = _concat([read(r) for r in clip.sources])
        elif clip.op == "reverse":
            curves = _reverse(built[clip.clips[0]])
        else:
            curves = _blend(built[clip.clips[0]], built[clip.clips[1]], clip.frames)
        built[clip.name] = curves
        action = _write_action(clip.name, curves)
        _add_events(arm, action, clip)
        actions.append(action)
        print(f"[chargen] clip {clip.name} ({clip.op}) frames 0-{int(_length(curves))}")

    arm.animation_data.action = None
    for action in actions:
        track = arm.animation_data.nla_tracks.new()
        track.name = action.name
        track.strips.new(action.name, 0, action)
        track.mute = True
        action.use_fake_user = True
    delete_objects(imported)
    purge_unused()
    bpy.context.scene.frame_set(0)
    save(out / "anims" / "human" / f"{spec.set}.blend")


def main() -> None:
    args = _parse_args()
    build_set(load_rig(), load_set_spec(args.set), args.sources, args.out)


if __name__ == "__main__":
    main()
