"""Builds an own monster from its body description (F5, gothar-chargen creature).

    blender --background --factory-startup --python build_creature.py -- \
        --spec <data/monsters/<art>.creature.toml> --characters <assets/source/characters> \
        --rig-out <data/monsters/<art>.toml> --clips-out <DATA_ROOT/.../<art>_clips.blend>

Steps: armature from the bones (root added at the origin, sockets at bone tails); body = union of
the shapes (voxel remesh), mouth cuts, remesh again, smooth; reduce to the triangle budget; UV
unwrap; fur texture baked from creature.surface_colour (3D, seamless over UV seams); automatic
weights (<= 4 per vertex); coarser LODs by reduction. Output as prepare_monster.py, plus the
texture (external file, §2.3):
    monsters/<art>/rig/<art>_reference.blend + .glb   rig + mesh (body_lod0..n)
    textures/<material>/<art>.jpg                     fur texture (base colour)
    <clips-out>                                       .blend with a still action "rest" (clip base)
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]
import numpy as np
from mathutils import Matrix, Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.prepare_monster import (  # noqa: E402
    _ORIENTATION,
    FPS,
    _export,
    _write_rig,
)
from gothar_chargen.creature import (  # noqa: E402
    Creature,
    Shape,
    bake_texture,
    euler_matrix,
    load_creature,
)

REST_FRAMES = 60


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--spec", type=Path, required=True)
    p.add_argument("--characters", type=Path, required=True)
    p.add_argument("--rig-out", type=Path, required=True)
    p.add_argument("--clips-out", type=Path, required=True)
    return p.parse_args(argv)


def _armature(c: Creature) -> bpy.types.Object:
    data = bpy.data.armatures.new(f"{c.art}_reference")
    arm = bpy.data.objects.new(f"{c.art}_reference", data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = data.edit_bones
    root = eb.new("root")
    root.head, root.tail = Vector((0, 0, 0)), Vector((0, -0.2, 0))
    for b in c.bones:
        e = eb.new(b.name)
        e.head, e.tail = Vector(b.head), Vector(b.tail)
    for b in c.bones:
        e = eb[b.name]
        e.parent = eb[b.parent] if b.parent else root
        e.use_connect = False
    for name, spec in c.sockets.items():
        parent = eb[spec["parent"]]
        s = eb.new(name)
        direction = (parent.tail - parent.head).normalized()
        s.head = parent.tail.copy()
        s.tail = parent.tail + direction * float(spec.get("length", 0.05))
        s.roll = parent.roll
        s.parent = parent
        s.use_deform = False
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def _add_shape(bm: bmesh.types.BMesh, s: Shape) -> None:
    if s.kind in ("ellipsoid", "cut"):
        rot = Matrix([list(r) for r in euler_matrix(s.rotate)]).to_4x4()
        m = Matrix.Translation(s.center) @ rot @ Matrix.Diagonal((*s.size, 1.0))
        bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=20, radius=1.0, matrix=m)
        return
    a, b = Vector(s.a), Vector(s.b)
    d = b - a
    res = bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=1.0)
    r2 = max(s.r2, 0.002)
    for v in res["verts"]:
        top = v.co.z > 0
        rr = r2 if top else s.r
        v.co = Vector((v.co.x * rr, v.co.y * rr, v.co.z * rr + (d.length if top else 0.0)))
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bmesh.ops.transform(
        bm, matrix=Matrix.Translation(a) @ q.to_matrix().to_4x4(), verts=res["verts"]
    )


def _mesh_object(name: str, shapes: list) -> bpy.types.Object:
    bm = bmesh.new()
    for s in shapes:
        _add_shape(bm, s)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def _apply(ob: bpy.types.Object, kind: str, **props: object) -> None:
    mod = ob.modifiers.new(kind.lower(), kind)
    for k, v in props.items():
        setattr(mod, k, v)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def _triangles(ob: bpy.types.Object) -> int:
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def _body(c: Creature) -> bpy.types.Object:
    body = _mesh_object("body_lod0", [s for s in c.shapes if s.kind != "cut"])
    _apply(body, "REMESH", mode="VOXEL", voxel_size=c.voxel)
    cuts = [s for s in c.shapes if s.kind == "cut"]
    if cuts:
        cutter = _mesh_object("cutter", cuts)
        _apply(body, "BOOLEAN", operation="DIFFERENCE", object=cutter, solver="EXACT")
        bpy.data.objects.remove(cutter)
        _apply(body, "REMESH", mode="VOXEL", voxel_size=c.voxel)
    if c.smooth:
        _apply(body, "SMOOTH", factor=0.5, iterations=c.smooth)
    high = _triangles(body)
    _apply(body, "DECIMATE", decimate_type="COLLAPSE", ratio=min(1.0, c.triangles / high))
    _apply(body, "TRIANGULATE")
    for p in body.data.polygons:
        p.use_smooth = True
    print(f"[chargen] body: {high} -> {_triangles(body)} triangles")
    return body


def _unwrap(ob: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, area_weight=1.0)
    bpy.ops.uv.pack_islands(margin=0.004, rotate=True)
    bpy.ops.object.mode_set(mode="OBJECT")


def _texture(c: Creature, ob: bpy.types.Object, path: Path) -> bpy.types.Image:
    me = ob.data
    me.calc_loop_triangles()
    tris = me.loop_triangles
    n = len(tris)
    loops = np.zeros(n * 3, dtype=np.int64)
    tris.foreach_get("loops", loops)
    verts = np.zeros(n * 3, dtype=np.int64)
    tris.foreach_get("vertices", verts)
    uv = np.zeros(len(me.loops) * 2)
    me.uv_layers.active.data.foreach_get("uv", uv)
    co = np.zeros(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    nrm = np.zeros(len(me.vertices) * 3)
    me.vertices.foreach_get("normal", nrm)
    tri_uv = uv.reshape(-1, 2)[loops].reshape(n, 3, 2)
    tri_pos = co.reshape(-1, 3)[verts].reshape(n, 3, 3)
    tri_nrm = nrm.reshape(-1, 3)[verts].reshape(n, 3, 3)
    tex = bake_texture(c, tri_uv, tri_pos, tri_nrm)
    size = c.texture
    img = bpy.data.images.new(f"{c.material}__{c.art}", size, size, alpha=False)  # category__name
    if c.ao > 0:
        occlusion = _bake_ao(ob, size)[::-1]  # rows top-down like tex
        tex = tex * (1.0 - c.ao * (1.0 - occlusion))[..., None]
    rgba = np.ones((size, size, 4))
    rgba[..., :3] = tex[::-1]  # Blender stores rows bottom-up
    img.pixels.foreach_set(rgba.ravel().astype(np.float32))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.filepath_raw = str(path.resolve())
    img.file_format = "JPEG"
    bpy.context.scene.render.image_settings.quality = 90
    img.save()
    print(f"[chargen] wrote {path}")
    return img


def _bake_ao(ob: bpy.types.Object, size: int) -> np.ndarray:
    """Ambient occlusion of the body on itself (Cycles bake): (size, size) in 0..1, bottom-up."""
    sc = bpy.context.scene
    engine = sc.render.engine
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 64
    sc.cycles.device = "CPU"
    target = bpy.data.images.new("ao", size, size, alpha=False, float_buffer=True)
    mat = bpy.data.materials.new("ao_bake")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = target
    mat.node_tree.nodes.active = node
    ob.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    sc.render.bake.margin = 8
    bpy.ops.object.bake(type="AO")
    px = np.zeros(size * size * 4, dtype=np.float32)
    target.pixels.foreach_get(px)
    ob.data.materials.clear()
    bpy.data.materials.remove(mat)
    bpy.data.images.remove(target)
    sc.render.engine = engine
    return px.reshape(size, size, 4)[..., 0].astype(np.float64)


def _material(c: Creature, img: bpy.types.Image) -> bpy.types.Material:
    mat = bpy.data.materials.new(c.material)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = img
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def _skin(arm: bpy.types.Object, body: bpy.types.Object) -> None:
    root = arm.data.bones["root"]
    root.use_deform = False  # weights only on the body bones
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    root.use_deform = True
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=4)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)
    unweighted = sum(1 for v in body.data.vertices if not v.groups)
    if unweighted:
        raise SystemExit(f"{unweighted} vertices without bone weights (automatic weights failed)")


def _lods(c: Creature, body: bpy.types.Object) -> list[bpy.types.Object]:
    out = [body]
    for level, ratio in enumerate(c.lods[1:], start=1):
        ob = body.copy()
        ob.data = body.data.copy()
        ob.name = ob.data.name = f"body_lod{level}"
        bpy.context.scene.collection.objects.link(ob)
        ob.modifiers.clear()
        _apply(ob, "DECIMATE", decimate_type="COLLAPSE", ratio=ratio)
        _apply(ob, "TRIANGULATE")
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=4)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)
        arm_mod = ob.modifiers.new("armature", "ARMATURE")
        arm_mod.object = body.parent
        out.append(ob)
    return out


def _rest_action(arm: bpy.types.Object) -> bpy.types.Action:
    """A still clip in the bind pose: base of the keyframe placeholders (data/clips/<art>.toml)."""
    arm.animation_data_create()
    action = bpy.data.actions.new("rest")
    arm.animation_data.action = action
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
        for frame in (0, REST_FRAMES):
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            pb.keyframe_insert("location", frame=frame)
    arm.animation_data.action = None
    action.use_fake_user = True
    return action


def main() -> None:
    args = _parse_args()
    c = load_creature(args.spec)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS
    arm = _armature(c)
    body = _body(c)
    _unwrap(body)
    img = _texture(c, body, args.characters / "textures" / c.material / f"{c.art}.jpg")
    body.data.materials.append(_material(c, img))
    _skin(arm, body)
    meshes = _lods(c, body)
    for ob in meshes:
        ob.data.name = ob.name
    print("[chargen] lods: " + ", ".join(f"{ob.name} {_triangles(ob)}" for ob in meshes))

    action = _rest_action(arm)
    args.clips_out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.clips_out.resolve()), copy=True, compress=True)
    print(f"[chargen] wrote {args.clips_out}")
    bpy.data.actions.remove(action)

    rig_dir = args.characters / "monsters" / c.art / "rig"
    rig_dir.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(
        filepath=str((rig_dir / f"{c.art}_reference.blend").resolve()), compress=True
    )
    _export([arm, *meshes], rig_dir / f"{c.art}_reference.glb", animations=False)
    height = max((body.matrix_world @ v.co).z for v in body.data.vertices)
    _write_rig(
        arm,
        c.art,
        height,
        set(c.sockets),
        args.rig_out,
        dict(_ORIENTATION),
        f"build_creature.py from {args.spec.name} (own work)",
    )
    print(f"[chargen] {c.art}: height {height:.3f} m, {len(arm.data.bones)} bones")


if __name__ == "__main__":
    main()
