"""Builds simple test parts for the figure kit (own geometry, no third-party content).

Run inside Blender 4.5:
    blender --background --factory-startup --python build_test_parts.py -- --out <parts/test dir>

Parts (each one .glb on the reference rig, rigid weights, subdivided boxes so LODs can reduce):
    body_test.glb         box body with an open neck (seam ring at the neck tail), material "skin"
    outfit_rags_test.glb  like body, wider torso/thighs in material "cloth_a" (replaces body)
    head_test.glb         tapered head whose open lower ring matches the neck ring exactly,
                          weighted like the neck there; all face morph targets; material "skin"
    hair_test.glb         loose box on top of the head, material "hair"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]
from mathutils import Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.build_reference_rig import (  # noqa: E402
    _BOX_FACES,
    _BOX_SIZE,
    FACE_HEAD_END,
    FACE_TAIL_END,
    _box_extents,
    _box_key,
)
from gothar_chargen.blender.common import new_reference  # noqa: E402
from gothar_chargen.blender.lod import make_lods  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.skeleton import RigSpec, load_rig  # noqa: E402

CUTS = 2  # subdivision cuts per box edge
WIDER = {"pelvis", "spine_01", "spine_02", "spine_03", "thigh"}  # outfit: looser cloth here


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_test_parts")
    parser.add_argument("--out", type=Path, required=True, help="output folder (parts/test)")
    return parser.parse_args(argv)


def _material(name: str, color: tuple[float, float, float]) -> bpy.types.Material:
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.8
    return mat


class _Builder:
    """Collects boxes (vertices, faces, per-vertex bone, per-face material) into one mesh."""

    def __init__(self) -> None:
        self.verts: list[Vector] = []
        self.faces: list[tuple[int, ...]] = []
        self.face_mat: list[int] = []
        self.vert_bone: list[str] = []

    def box(
        self, corners: list[Vector], bones: list[str], material: int, skip: tuple[int, ...] = ()
    ) -> None:
        base = len(self.verts)
        self.verts += corners
        self.vert_bone += bones
        for k, f in enumerate(_BOX_FACES):
            if k in skip:
                continue
            self.faces.append(tuple(base + i for i in f))
            self.face_mat.append(material)

    def to_object(self, name: str, arm: bpy.types.Object, materials: list) -> bpy.types.Object:
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(v) for v in self.verts], [], self.faces)
        for m in materials:
            mesh.materials.append(m)
        for poly, mi in zip(mesh.polygons, self.face_mat, strict=True):
            poly.material_index = mi
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        groups = {b: obj.vertex_groups.new(name=b) for b in sorted(set(self.vert_bone))}
        for i, b in enumerate(self.vert_bone):
            groups[b].add([i], 1.0, "REPLACE")
        _weld_and_subdivide(obj)
        obj.parent = arm
        obj.modifiers.new("armature", "ARMATURE").object = arm
        return obj


def _weld_and_subdivide(obj: bpy.types.Object) -> None:
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=CUTS, use_grid_fill=True)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def _box_corners(
    bone: bpy.types.Bone, size: tuple[float, float], y0: float, y1: float
) -> list[Vector]:
    wx, wz = _box_extents(bone, size)
    mat = bone.matrix_local
    return [mat @ Vector((x, y, z)) for x in (-wx, wx) for y in (y0, y1) for z in (-wz, wz)]


def _body(rig: RigSpec, arm: bpy.types.Object, outfit: bool) -> _Builder:
    b = _Builder()
    for bone in arm.data.bones:
        key = _box_key(bone.name)
        size = _BOX_SIZE.get(key)
        if size is None or not bone.use_deform or bone.name == "head":
            continue
        y0 = -0.08 if bone.name == "pelvis" else 0.0
        material = 0
        if outfit and key in WIDER:
            size = (size[0] * 1.15, size[1] * 1.15)
            material = 1
        corners = _box_corners(bone, size, y0, bone.length)
        skip = (FACE_TAIL_END,) if bone.name == "neck" else ()  # open neck: no face at the top
        b.box(corners, [bone.name] * 8, material, skip)
    return b


def _head(rig: RigSpec, arm: bpy.types.Object) -> _Builder:
    neck, head = arm.data.bones["neck"], arm.data.bones["head"]
    ring = _box_corners(neck, _BOX_SIZE["neck"], 0.0, neck.length)
    top = _box_corners(head, _BOX_SIZE["head"], 0.0, rig.height - head.head_local.z)
    # corner order (x, y, z): y index 0 = lower ring (= neck tail), 1 = top of the head
    corners, bones = [], []
    for i in range(8):
        y_hi = (i // 2) % 2 == 1
        corners.append(top[i] if y_hi else ring[i | 2])
        bones.append("head" if y_hi else "neck")
    b = _Builder()
    b.box(corners, bones, 0, skip=(FACE_HEAD_END,))  # open at the bottom
    return b


def _hair(rig: RigSpec, arm: bpy.types.Object) -> _Builder:
    head = arm.data.bones["head"]
    top = rig.height - head.head_local.z
    b = _Builder()
    b.box(_box_corners(head, (0.19, 0.23), top - 0.06, top + 0.03), ["head"] * 8, 0)
    return b


def _add_face_morphs(rig: RigSpec, obj: bpy.types.Object) -> None:
    obj.shape_key_add(name="Basis", from_mix=False)
    front = [v.index for v in obj.data.vertices if v.co.y < -0.08 and v.co.z > 1.6]
    for k, name in enumerate(rig.morph_targets):
        key = obj.shape_key_add(name=name, from_mix=False)
        offset = Vector((0.0, -0.004 * (1 + k % 3), -0.003 if k % 2 else 0.003))
        for i in front:
            key.data[i].co = obj.data.vertices[i].co + offset


def _export(objs: list[bpy.types.Object], arm: bpy.types.Object, path: Path) -> None:
    for o in bpy.context.scene.objects:
        o.hide_set(o.type == "MESH" and o not in objs)
        o.select_set(o in objs or o is arm)
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=False)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), **settings)
    print(f"[chargen] wrote {path}")


def main() -> None:
    args = _parse_args()
    rig = load_rig()
    arm = new_reference(rig)
    skin = _material("skin", (0.62, 0.45, 0.33))
    cloth = _material("cloth_a", (0.35, 0.29, 0.2))
    hair_mat = _material("hair", (0.18, 0.12, 0.07))

    parts = {
        "body_test": _body(rig, arm, outfit=False).to_object("body", arm, [skin]),
        "outfit_rags_test": _body(rig, arm, outfit=True).to_object("body", arm, [skin, cloth]),
        "head_test": _head(rig, arm).to_object("head", arm, [skin]),
        "hair_test": _hair(rig, arm).to_object("hair", arm, [hair_mat]),
    }
    _add_face_morphs(rig, parts["head_test"])
    for file_name, obj in parts.items():  # every part carries its LOD levels (§2.2)
        role = file_name.split("_")[0] if not file_name.startswith("outfit") else "body"
        levels = make_lods(obj, role, keep_borders=role != "hair")
        _export(levels, arm, args.out / f"{file_name}.glb")
        for o in levels:  # the next part reuses the node names
            bpy.data.objects.remove(o)


if __name__ == "__main__":
    main()
