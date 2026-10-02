"""Builds the placeholder figure from the Quaternius mannequin (CC0) on the reference rig.

Run inside Blender 4.5:
    blender --background --factory-startup --python build_placeholder.py -- \
        --ual2 UAL2_Standard.glb --out <characters dir>

Writes figures/placeholder_mannequin.blend. The rig geometry equals the Quaternius rig
(human_reference.toml), so the weights transfer by renaming vertex groups.
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
    save,
)
from gothar_chargen.mapping import BoneMap, load_mapping  # noqa: E402
from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_placeholder")
    parser.add_argument("--ual2", type=Path, required=True, help="UAL2 .glb (mannequin mesh)")
    parser.add_argument("--out", type=Path, required=True, help="assets/source/characters")
    return parser.parse_args(argv)


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
    arm = new_reference(rig)
    imported = import_glb(ual2)
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
    delete_objects([o for o in imported if o is not mesh])
    save(out / "figures" / "placeholder_mannequin.blend")


def main() -> None:
    args = _parse_args()
    build_figure(load_rig(), args.ual2, args.out)


if __name__ == "__main__":
    main()
