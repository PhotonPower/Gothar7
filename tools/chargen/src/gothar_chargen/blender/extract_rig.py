"""Writes the rest geometry (head, tail, roll) of a source rig as [[bone]] tables (TOML).

Used once to take the reference geometry from the Quaternius rig (CC0); kept for reproducibility.
Run inside Blender 4.5:
    blender --background --factory-startup --python extract_rig.py -- \
        --glb UAL2_Standard.glb --mapping quaternius_ual2 --out bones.toml
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.mapping import load_mapping  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="extract_rig")
    parser.add_argument("--glb", type=Path, required=True)
    parser.add_argument("--mapping", required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args(argv)


def _fmt(v: tuple[float, ...]) -> str:
    return "[" + ", ".join("0" if abs(x) < 5e-7 else f"{x:.6f}" for x in v) + "]"


def main() -> None:
    args = _parse_args()
    rig = load_rig()
    bone_map = load_mapping(args.mapping)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(args.glb))
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    m = arm.matrix_world
    mapped = bone_map.resolve([b.name for b in arm.data.edit_bones], set(rig.names))

    lines: list[str] = []
    for spec in rig.bones:  # contract order: parents first
        source = next((s for s, t in mapped.items() if t == spec.name), None)
        if source is None:
            continue
        eb = arm.data.edit_bones[source]
        parent = mapped.get(eb.parent.name) if eb.parent else None
        if parent != spec.parent:
            raise SystemExit(f"{source}: parent maps to {parent}, contract says {spec.parent}")
        lines.append("[[bone]]")
        lines.append(f'name = "{spec.name}"')
        if parent:
            lines.append(f'parent = "{parent}"')
        lines.append(f"head = {_fmt(tuple(m @ eb.head))}")
        lines.append(f"tail = {_fmt(tuple(m @ eb.tail))}")
        lines.append(f"roll = {eb.roll:.6f}")
        lines.append("")
    bpy.ops.object.mode_set(mode="OBJECT")
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"[chargen] wrote {args.out} ({len(mapped)} bones from {args.glb.name})")


if __name__ == "__main__":
    main()
