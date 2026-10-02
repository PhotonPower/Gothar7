"""Builds human_reference.blend: reference armature + a rigid box mannequin with face shape keys.

Run inside Blender 4.5:
    blender --background --factory-startup --python build_reference_rig.py -- --out <file.blend>

The skeleton comes from gothar_chargen/data/human_reference.toml. The mannequin is our own
placeholder geometry (one box per deform bone, weight 1.0) so engine tests have a skinned mesh.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]
from mathutils import Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402

# Mannequin box size per deform bone: (width along bone X, depth along bone Z), metres.
# Paired bones are listed without side suffix. Bones not listed get no geometry.
_BOX_SIZE: dict[str, tuple[float, float]] = {
    "pelvis": (0.30, 0.18),
    "spine_01": (0.29, 0.17),
    "spine_02": (0.31, 0.18),
    "spine_03": (0.36, 0.20),
    "neck": (0.10, 0.10),
    "head": (0.17, 0.21),
    "upperarm": (0.10, 0.10),
    "lowerarm": (0.08, 0.08),
    "hand": (0.09, 0.03),
    "thumb_01": (0.022, 0.022),
    "thumb_02": (0.020, 0.020),
    "thumb_03": (0.018, 0.018),
    "thigh": (0.15, 0.15),
    "calf": (0.11, 0.11),
    "foot": (0.09, 0.06),
    "ball": (0.09, 0.03),
}
for _finger in ("index", "middle", "ring", "pinky"):
    for _seg in ("01", "02", "03"):
        _BOX_SIZE[f"{_finger}_{_seg}"] = (0.017, 0.016)

# Box faces as quads over the 8 corners (x-, x+) x (y0, y1) x (z-, z+), outward winding.
_BOX_FACES = [
    (0, 1, 3, 2),  # x-
    (4, 6, 7, 5),  # x+
    (0, 4, 5, 1),  # z-
    (2, 3, 7, 6),  # z+
    (0, 2, 6, 4),  # y0 (head end)
    (1, 5, 7, 3),  # y1 (tail end)
]


def _box_key(bone_name: str) -> str:
    if bone_name.endswith(("_l", "_r")):
        return bone_name[:-2]
    return bone_name


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_reference_rig")
    parser.add_argument("--out", type=Path, required=True, help="output .blend")
    parser.add_argument("--rig", type=Path, default=None, help="skeleton .toml (default: packaged)")
    return parser.parse_args(argv)


def _clear_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.render.fps = 30


def _build_armature(rig: RigSpec) -> bpy.types.Object:
    arm_data = bpy.data.armatures.new(rig.name)
    arm_data.display_type = "OCTAHEDRAL"
    arm_obj = bpy.data.objects.new(rig.name, arm_data)
    bpy.context.scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    arm_obj.select_set(True)

    bpy.ops.object.mode_set(mode="EDIT")
    edit_bones = arm_data.edit_bones
    for spec in rig.bones:
        eb = edit_bones.new(spec.name)
        eb.head = Vector(spec.head)
        eb.tail = Vector(spec.tail)
        eb.align_roll(Vector(spec.up))
        eb.use_deform = not spec.socket and spec.parent is not None
        if spec.parent is not None:
            eb.parent = edit_bones[spec.parent]
            eb.use_connect = (Vector(spec.head) - edit_bones[spec.parent].tail).length < 1e-6
    bpy.ops.object.mode_set(mode="OBJECT")

    # Sockets in their own bone collection so animators can hide them.
    sockets = arm_data.collections.new("sockets")
    body = arm_data.collections.new("body")
    for bone in arm_data.bones:
        (sockets if bone.name.startswith("socket_") else body).assign(bone)
    return arm_obj


def _build_mannequin(rig: RigSpec, arm_obj: bpy.types.Object) -> bpy.types.Object:
    verts: list[Vector] = []
    faces: list[tuple[int, ...]] = []
    groups: list[tuple[str, list[int]]] = []
    head_verts: list[int] = []

    for bone in arm_obj.data.bones:
        size = _BOX_SIZE.get(_box_key(bone.name))
        if size is None or not bone.use_deform:
            continue
        wx, wz = size[0] / 2.0, size[1] / 2.0
        length = bone.length
        # head box reaches below the head joint (jaw), pelvis box down to the hip joints
        y0 = {"head": -0.05, "pelvis": -0.08}.get(bone.name, 0.0)
        base = len(verts)
        mat = bone.matrix_local  # bone space -> armature space
        for x in (-wx, wx):
            for y in (y0, length):
                for z in (-wz, wz):
                    verts.append(mat @ Vector((x, y, z)))
        faces.extend(tuple(base + i for i in f) for f in _BOX_FACES)
        idx = list(range(base, base + 8))
        groups.append((bone.name, idx))
        if bone.name == "head":
            head_verts = idx

    mesh = bpy.data.meshes.new("mannequin")
    mesh.from_pydata([tuple(v) for v in verts], [], faces)
    mesh.update()
    for poly in mesh.polygons:
        poly.use_smooth = False

    mat = bpy.data.materials.new("mannequin")
    mat.diffuse_color = (0.55, 0.55, 0.58, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.55, 0.55, 0.58, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.8
    mesh.materials.append(mat)

    obj = bpy.data.objects.new("mannequin", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = arm_obj
    for name, idx in groups:
        vg = obj.vertex_groups.new(name=name)
        vg.add(idx, 1.0, "REPLACE")
    mod = obj.modifiers.new("armature", "ARMATURE")
    mod.object = arm_obj

    _add_face_shape_keys(rig, obj, head_verts)
    return obj


def _add_face_shape_keys(rig: RigSpec, obj: bpy.types.Object, head_verts: list[int]) -> None:
    """Test shape keys: each morph target nudges the front-lower head corners differently."""
    obj.shape_key_add(name="Basis", from_mix=False)
    if not head_verts:
        return
    head_bone = obj.parent.data.bones["head"]
    forward = head_bone.matrix_local.to_3x3() @ Vector((0.0, 0.0, 1.0))  # bone Z = forward
    front = [
        i for i in head_verts if (obj.data.vertices[i].co - head_bone.head_local).dot(forward) > 0
    ]
    for k, name in enumerate(rig.morph_targets):
        key = obj.shape_key_add(name=name, from_mix=False)
        offset = 0.004 * (1 + k % 4)
        direction = Vector((0.0, 0.0, -1.0)) if k % 2 == 0 else forward
        for i in front:
            key.data[i].co = obj.data.vertices[i].co + direction * offset


def main() -> None:
    args = _parse_args()
    rig = load_rig(args.rig)
    _clear_scene()
    arm_obj = _build_armature(rig)
    _build_mannequin(rig, arm_obj)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out.resolve()), compress=True)
    print(f"[chargen] wrote {args.out} ({len(rig.bones)} bones)")


if __name__ == "__main__":
    main()
