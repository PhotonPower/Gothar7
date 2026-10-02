"""Builds the F1 placeholder figure and test clips from Quaternius (CC0) files on the reference rig.

Run inside Blender 4.5:
    blender --background --factory-startup --python build_placeholder.py -- \
        --ual1 AnimationLibrary_Godot_Standard.glb --ual2 UAL2_Standard.glb --out <characters dir>

Writes
    figures/placeholder_mannequin.blend   Quaternius mannequin mesh bound to the reference armature
    anims/human/<set>.blend               reference armature + clips of data/clips/<list>.toml
    anims/human/<set>.events.toml         footstep events detected from foot contact
The rig geometry equals the Quaternius rig (human_reference.toml), so weights and clips transfer by
renaming bones; translation keys are kept for root and pelvis only, scale keys are dropped.
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from importlib import resources
from pathlib import Path

import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender import build_reference_rig as reference  # noqa: E402
from gothar_chargen.events import Event, detect_contacts, format_events  # noqa: E402
from gothar_chargen.mapping import BoneMap, load_mapping  # noqa: E402
from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402

FPS = 30
TRANSLATED_BONES = ("root", "pelvis")  # only these keep translation keys (contract §3)
_BONE_PATH = re.compile(r'^pose\.bones\["([^"]+)"\]\.(\w+)$')


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_placeholder")
    parser.add_argument("--ual1", type=Path, required=True, help="UAL1 Godot .glb (clips)")
    parser.add_argument("--ual2", type=Path, required=True, help="UAL2 .glb (mannequin mesh)")
    parser.add_argument("--out", type=Path, required=True, help="assets/source/characters")
    parser.add_argument("--clips", default="f1_placeholder", help="clip list in data/clips/")
    return parser.parse_args(argv)


def _import(path: Path) -> list[bpy.types.Object]:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    return [o for o in bpy.data.objects if o not in before]


def _delete(objects: list[bpy.types.Object]) -> None:
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)


def _new_reference(rig: RigSpec) -> bpy.types.Object:
    reference._clear_scene()
    bpy.context.scene.render.fps = FPS
    return reference._build_armature(rig)


# --- figure ------------------------------------------------------------------------------------


def _merge_unmapped_weights(
    mesh: bpy.types.Object, source_arm: bpy.types.Object, bone_map: BoneMap, names: set[str]
) -> None:
    """Adds weights of unmapped bones (finger-tip leaf bones) to the nearest mapped ancestor."""
    groups = mesh.vertex_groups
    for group in list(groups):
        if bone_map.target(group.name, names) is not None:
            continue
        bone = source_arm.data.bones.get(group.name)
        parent = bone.parent if bone else None
        while parent is not None and bone_map.target(parent.name, names) is None:
            parent = parent.parent
        weights = [
            (v.index, g.weight)
            for v in mesh.data.vertices
            for g in v.groups
            if g.group == group.index and g.weight > 0
        ]
        if not weights:
            continue
        if parent is None:
            raise SystemExit(f"weighted vertex group '{group.name}' has no mapped ancestor")
        target = groups.get(parent.name) or groups.new(name=parent.name)
        for index, weight in weights:
            target.add([index], weight, "ADD")
        print(f"[chargen] merged {len(weights)} weights {group.name} -> {parent.name}")


def build_figure(rig: RigSpec, ual2: Path, out: Path) -> None:
    arm = _new_reference(rig)
    imported = _import(ual2)
    mesh = next(o for o in imported if o.type == "MESH" and o.vertex_groups)
    bone_map = load_mapping("quaternius_ual2")
    names = set(rig.names)

    source_arm = next(o for o in imported if o.type == "ARMATURE")
    _merge_unmapped_weights(mesh, source_arm, bone_map, names)
    for group in list(mesh.vertex_groups):
        target = bone_map.target(group.name, names)
        if target is None:
            mesh.vertex_groups.remove(group)
        else:
            group.name = target

    mesh.parent = None
    mesh.matrix_world.identity()
    mesh.name = mesh.data.name = "placeholder_mannequin"
    mesh.parent = arm
    for mod in [m for m in mesh.modifiers if m.type == "ARMATURE"]:
        mesh.modifiers.remove(mod)
    mesh.modifiers.new("armature", "ARMATURE").object = arm
    _delete([o for o in imported if o is not mesh])
    _save(out / "figures" / "placeholder_mannequin.blend")


# --- clips -------------------------------------------------------------------------------------


def _transfer_action(
    source: bpy.types.Action, name: str, bone_map: BoneMap, rig: RigSpec
) -> bpy.types.Action:
    names = set(rig.names)
    action = source.copy()
    action.name = name
    for fc in list(action.fcurves):
        m = _BONE_PATH.match(fc.data_path)
        target = bone_map.target(m.group(1), names) if m else None
        prop = m.group(2) if m else ""
        keep = target is not None and (
            prop in ("rotation_quaternion", "rotation_euler")
            or (prop == "location" and target in TRANSLATED_BONES)
        )
        if not keep:
            action.fcurves.remove(fc)
            continue
        fc.data_path = f'pose.bones["{target}"].{prop}'
        if fc.group is not None:
            fc.group.name = target
    return action


def _foot_heights(arm: bpy.types.Object, action: bpy.types.Action, bone: str) -> list[float]:
    start, end = (int(round(f)) for f in action.frame_range)
    heights = []
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        heights.append((arm.matrix_world @ arm.pose.bones[bone].head).z)
    return heights


def build_clips(rig: RigSpec, ual1: Path, out: Path, clip_list: str) -> None:
    spec = tomllib.loads(
        resources.files("gothar_chargen.data.clips")
        .joinpath(f"{clip_list}.toml")
        .read_text("utf-8")
    )
    arm = _new_reference(rig)
    imported = _import(ual1)
    bone_map = load_mapping(spec["mapping"])
    sources = {a.name: a for a in bpy.data.actions}

    arm.animation_data_create()
    events: dict[str, list[Event]] = {}
    for clip in spec["clip"]:
        action = _transfer_action(sources[clip["source"]], clip["name"], bone_map, rig)
        slot = action.slots[0]
        slot.name_display = arm.name
        arm.animation_data.action = action
        arm.animation_data.action_slot = slot
        if clip.get("events") == "footsteps":
            events[clip["name"]] = [
                Event(frame, f"footstep_{side}")
                for side in ("l", "r")
                for frame in detect_contacts(_foot_heights(arm, action, f"foot_{side}"))
            ]
        arm.animation_data.action = None
        track = arm.animation_data.nla_tracks.new()
        track.name = clip["name"]
        track.strips.new(clip["name"], int(action.frame_range[0]), action)
        track.mute = True
        print(
            f"[chargen] clip {clip['name']} <- {clip['source']} frames {tuple(action.frame_range)}"
        )

    _delete(imported)
    for a in list(bpy.data.actions):
        if a.users == 0:
            bpy.data.actions.remove(a)
    for p in (bpy.data.meshes, bpy.data.materials, bpy.data.armatures):
        for block in list(p):
            if block.users == 0:
                p.remove(block)
    bpy.context.scene.frame_set(0)

    set_dir = out / "anims" / "human"
    _save(set_dir / f"{spec['set']}.blend")
    (set_dir / f"{spec['set']}.events.toml").write_text(
        f"# Generated by build-placeholder ({clip_list}), footsteps from foot contact\n"
        + format_events(FPS, events),
        encoding="utf-8",
        newline="\n",
    )


def _save(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path.resolve()), compress=True)
    print(f"[chargen] wrote {path}")


def main() -> None:
    args = _parse_args()
    rig = load_rig()
    build_figure(rig, args.ual2, args.out)
    build_clips(rig, args.ual1, args.out, args.clips)


if __name__ == "__main__":
    main()
