"""Builds an own monster from its body description (F5, gothar-chargen creature).

    blender --background --factory-startup --python build_creature.py -- \
        --spec <data/monsters/<art>.creature.toml> --characters <assets/source/characters> \
        --rig-out <data/monsters/<art>.toml> --clips-out <DATA_ROOT/.../<art>_clips.blend>

Steps: armature from the bones (root added at the origin, sockets at bone tails); body = union of
the shapes (voxel remesh), cuts (mouth, eye sockets, ear cups), remesh again, smooth; reduce to the
triangle budget; UV unwrap; fur texture baked from creature.surface (3D, seamless over UV seams)
with ambient occlusion; normal map = shape of the high mesh (Cycles bake) blended with the fur
relief; automatic weights (<= 4 per vertex), jaws split along the mouth cut; eyes and teeth as
separate rigid pieces; coarser LODs by reduction. Output as prepare_monster.py, plus the
textures (external files, §2.3):
    monsters/<art>/rig/<art>_reference.blend + .glb   rig + mesh (body_lod0..n)
    textures/<material>/<art>.jpg, <art>_normal.png   fur base colour and normal map
    textures/<material>/eyes_<rgb>.jpg, teeth_<rgb>.jpg  small shared piece textures
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
    CUTS,
    PIECES,
    PLATES,
    Creature,
    Shape,
    bake_texture,
    blend_normals,
    downsample,
    euler_matrix,
    height_normal,
    load_creature,
    shape_distance,
    srgb,
)

REST_FRAMES = 60
PLATE_GROUP = "__plate"  # marks plate vertices until they are weighted


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


def _add_shape(bm: bmesh.types.BMesh, s: Shape, segments: tuple[int, int] = (32, 20)) -> list:
    """Adds the primitive to bm; returns its vertices."""
    if s.kind in ("box", "cut_box", "plate"):
        rot = Matrix([list(r) for r in euler_matrix(s.rotate)]).to_4x4()
        m = Matrix.Translation(s.center) @ rot @ Matrix.Diagonal((*s.size, 1.0))
        return bmesh.ops.create_cube(bm, size=2.0, matrix=m, calc_uvs=True)["verts"]
    if s.kind in ("ellipsoid", "cut", "eye"):
        rot = Matrix([list(r) for r in euler_matrix(s.rotate)]).to_4x4()
        m = Matrix.Translation(s.center) @ rot @ Matrix.Diagonal((*s.size, 1.0))
        u, v = segments
        res = bmesh.ops.create_uvsphere(
            bm, u_segments=u, v_segments=v, radius=1.0, matrix=m, calc_uvs=True
        )
        return res["verts"]
    a, b = Vector(s.a), Vector(s.b)
    d = b - a
    if s.kind == "tooth":  # a plain cone
        q = Vector((0, 0, 1)).rotation_difference(d.normalized())
        m = Matrix.Translation((a + b) / 2) @ q.to_matrix().to_4x4()
        res = bmesh.ops.create_cone(
            bm,
            cap_ends=True,
            segments=segments[0],
            radius1=s.r,
            radius2=0.0005,
            depth=d.length,
            matrix=m,
            calc_uvs=True,
        )
        return res["verts"]
    u, v = (24, 16) if segments[0] >= 24 else segments
    res = bmesh.ops.create_uvsphere(bm, u_segments=u, v_segments=v, radius=1.0, calc_uvs=True)
    r2 = max(s.r2, 0.0005 if s.kind == "tooth" else 0.002)
    for vert in res["verts"]:
        top = vert.co.z > 0
        rr = r2 if top else s.r
        vert.co = Vector(
            (vert.co.x * rr, vert.co.y * rr, vert.co.z * rr + (d.length if top else 0.0))
        )
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bmesh.ops.transform(
        bm, matrix=Matrix.Translation(a) @ q.to_matrix().to_4x4(), verts=res["verts"]
    )
    return res["verts"]


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


def _cut(body: bpy.types.Object, cut: Shape) -> None:
    cutter = _mesh_object("cutter", [cut])
    _apply(body, "BOOLEAN", operation="DIFFERENCE", object=cutter, solver="EXACT")
    bpy.data.objects.remove(cutter)


def _body(c: Creature) -> tuple[bpy.types.Object, bpy.types.Object]:
    """The reduced body (lod0) and its high-resolution source (for the normal bake)."""
    union = [s for s in c.shapes if s.kind not in (*CUTS, *PIECES, *PLATES)]
    body = _mesh_object("body_lod0", union)
    _apply(body, "REMESH", mode="VOXEL", voxel_size=c.voxel)
    # cut: carved before the second remesh and smoothed with the body (sockets, ear cups);
    # cut_box: a thin slit (mouth) carved into the final surface - a remesh would close it
    soft = [s for s in c.shapes if s.kind == "cut"]
    for cut in soft:  # one closed cutter each: overlapping cutters confuse the boolean
        _cut(body, cut)
    if soft:
        _apply(body, "REMESH", mode="VOXEL", voxel_size=c.voxel)
    if c.smooth:
        _apply(body, "SMOOTH", factor=0.5, iterations=c.smooth)
    for cut in [s for s in c.shapes if s.kind == "cut_box"]:
        _cut(body, cut)
    plates = _plates(c)
    high = body.copy()
    high.data = body.data.copy()
    high.name = "high"
    bpy.context.scene.collection.objects.link(high)
    if plates is not None:  # the bake sees the plates too
        extra = plates.copy()
        extra.data = plates.data.copy()
        bpy.context.scene.collection.objects.link(extra)
        _join(high, [extra])
    count = _triangles(body)
    _apply(body, "DECIMATE", decimate_type="COLLAPSE", ratio=min(1.0, c.triangles / count))
    _close_holes(body)
    _apply(body, "TRIANGULATE")
    for p in body.data.polygons:
        p.use_smooth = True
    print(f"[chargen] body: {count} -> {_triangles(body)} triangles")
    if plates is not None:
        n = _triangles(plates)
        _join(body, [plates])
        print(f"[chargen] plates: {len([s for s in c.shapes if s.kind in PLATES])} ({n} triangles)")
    return body, high


def _close_holes(ob: bpy.types.Object) -> None:
    """The reduction can tear tiny holes at thin tips (ears, ribs): weld and fill them."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0002)
    border = [e for e in bm.edges if e.is_boundary]
    if border:
        bmesh.ops.holes_fill(bm, edges=border, sides=8)
        print(f"[chargen] closed {len(border)} border edges after the reduction")
    bm.to_mesh(ob.data)
    bm.free()


def _join(target: bpy.types.Object, others: list[bpy.types.Object]) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for o in others:
        o.select_set(True)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.join()


def _plates(c: Creature) -> bpy.types.Object | None:
    """Armour plates: chamfered boxes, smooth enough for the bake, marked by the group __plate."""
    shapes = [s for s in c.shapes if s.kind in PLATES]
    if not shapes:
        return None
    ob = _mesh_object("plates", shapes)
    _apply(ob, "BEVEL", width=c.plate_bevel, segments=1, limit_method="NONE")
    _apply(ob, "TRIANGULATE")
    for p in ob.data.polygons:
        p.use_smooth = False
    ob.vertex_groups.new(name=PLATE_GROUP).add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    return ob


def _unwrap(ob: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, area_weight=1.0)
    bpy.ops.uv.pack_islands(margin=0.004, rotate=True)
    bpy.ops.object.mode_set(mode="OBJECT")


def _triangle_data(ob: bpy.types.Object) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
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
    return (
        uv.reshape(-1, 2)[loops].reshape(n, 3, 2),
        co.reshape(-1, 3)[verts].reshape(n, 3, 3),
        nrm.reshape(-1, 3)[verts].reshape(n, 3, 3),
    )


def _save(pixels: np.ndarray, name: str, path: Path, non_color: bool = False) -> bpy.types.Image:
    """Writes (S, S, 3) values in 0..1, rows top-down, as an image file (JPEG or PNG)."""
    size = pixels.shape[0]
    img = bpy.data.images.new(name, size, size, alpha=False)
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    rgba = np.ones((size, size, 4))
    rgba[..., :3] = pixels[::-1]  # Blender stores rows bottom-up
    img.pixels.foreach_set(rgba.ravel().astype(np.float32))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.filepath_raw = str(path.resolve())
    img.file_format = "PNG" if path.suffix == ".png" else "JPEG"
    bpy.context.scene.render.image_settings.quality = 90
    img.save()
    print(f"[chargen] wrote {path}")
    return img


def _textures(
    c: Creature, ob: bpy.types.Object, high: bpy.types.Object, folder: Path
) -> tuple[bpy.types.Image, bpy.types.Image]:
    """Base colour (with ambient occlusion) and tangent-space normal map of the fur."""
    colour, height = bake_texture(c, *_triangle_data(ob))
    if c.ao > 0:
        high.hide_render = True  # the coincident high mesh would occlude the body
        occlusion = _bake(ob, None, "AO", c.texture)[..., 0]
        high.hide_render = False
        colour = colour * (1.0 - c.ao * (1.0 - occlusion))[..., None]
    base = _save(colour, f"{c.material}__{c.art}", folder / f"{c.art}.jpg")  # category__name
    shape = _bake(ob, high, "NORMAL", c.normal_size) * 2.0 - 1.0  # geometry of the high mesh
    shape /= np.maximum(np.linalg.norm(shape, axis=-1, keepdims=True), 1e-6)
    detail = downsample(height_normal(height, c.normal_strength), c.normal_size)
    detail /= np.linalg.norm(detail, axis=-1, keepdims=True)
    normal = blend_normals(shape, detail) * 0.5 + 0.5
    normal_img = _save(
        normal, f"{c.material}__{c.art}_normal", folder / f"{c.art}_normal.png", True
    )
    return base, normal_img


def _bake(
    ob: bpy.types.Object, source: bpy.types.Object | None, kind: str, size: int
) -> np.ndarray:
    """Cycles bake into a float image: AO of ob on itself, or the tangent-space normals of source
    seen from ob (selected to active). Returns (size, size, 3), rows top-down."""
    sc = bpy.context.scene
    engine = sc.render.engine
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 64 if kind == "AO" else 4
    sc.cycles.device = "CPU"
    target = bpy.data.images.new("bake", size, size, alpha=False, float_buffer=True)
    target.colorspace_settings.name = "Non-Color"
    mat = bpy.data.materials.new("bake")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = target
    mat.node_tree.nodes.active = node
    ob.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    if source is not None:
        source.select_set(True)
    bpy.context.view_layer.objects.active = ob
    sc.render.bake.margin = 8
    if source is None:
        bpy.ops.object.bake(type=kind)
    else:
        bpy.ops.object.bake(
            type=kind,
            normal_space="TANGENT",
            use_selected_to_active=True,
            cage_extrusion=0.006,
            max_ray_distance=0.012,  # short: thin parts must not see their far side
        )
    px = np.zeros(size * size * 4, dtype=np.float32)
    target.pixels.foreach_get(px)
    ob.data.materials.clear()
    bpy.data.materials.remove(mat)
    bpy.data.images.remove(target)
    sc.render.engine = engine
    return px.reshape(size, size, 4)[::-1, :, :3].astype(np.float64)


def _material(
    name: str, base: bpy.types.Image, normal: bpy.types.Image | None, roughness: float
) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = roughness
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = base
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if normal is not None:
        ntex = nodes.new("ShaderNodeTexImage")
        ntex.image = normal
        nmap = nodes.new("ShaderNodeNormalMap")
        links.new(ntex.outputs["Color"], nmap.inputs["Color"])
        links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
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


def _mouth(c: Creature) -> Shape | None:
    """The cut that separates the jaws: the one nearest to the middle of the jaw bone."""
    names = {b.name for b in c.bones}
    cuts = [s for s in c.shapes if s.kind in CUTS]
    if not cuts or not {"jaw", "head"} <= names:
        return None
    jaw = c.bone("jaw")
    mid = (np.array(jaw.head) + np.array(jaw.tail)) / 2
    return min(cuts, key=lambda s: float(np.linalg.norm(np.array(s.center) - mid)))


def _split_jaw(c: Creature, body: bpy.types.Object) -> None:
    """Upper and lower jaw apart along the mouth cut: automatic weights give the whole snout to
    the jaw. Above the cut plane only the head moves; below it, inside the snout, only the jaw."""
    cut = _mouth(c)
    if cut is None:
        return
    rot = euler_matrix(cut.rotate)
    centre, size = np.array(cut.center), np.array(cut.size)
    groups = body.vertex_groups
    jaw, head = groups["jaw"], groups["head"]
    moved = 0
    for v in body.data.vertices:
        local = rot.T @ (np.array(v.co) - centre)
        weights = {groups[g.group].name: g.weight for g in v.groups}
        in_snout = (
            abs(local[0]) < 1.5 * size[0] and abs(local[1]) < 1.15 * size[1] and local[2] > -0.08
        )
        if local[2] > 0 and weights.get("jaw", 0.0) > 0:
            head.add([v.index], min(1.0, weights.get("head", 0.0) + weights["jaw"]), "REPLACE")
            jaw.remove([v.index])
            moved += 1
        elif local[2] <= 0 and in_snout:
            for name in weights:
                groups[name].remove([v.index])
            jaw.add([v.index], 1.0, "REPLACE")
            moved += 1
    print(f"[chargen] jaw split along the mouth cut: {moved} vertices")


def _linear(colour: str) -> tuple[float, float, float]:
    s = srgb(colour)
    return tuple(float(v) for v in np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4))


def _piece_texture(role: str, colour: str, folder: Path) -> bpy.types.Image:
    """Small shared texture of an eye or tooth colour: textures/<material>/<role>_<rrggbb>.jpg."""
    rng = np.random.default_rng(7)
    base = srgb(colour)
    noise = 1.0 + 0.06 * (rng.random((32, 32, 1)) - 0.5)
    stem = f"{role}_{colour.lstrip('#').lower()}"
    return _save(np.clip(base * noise, 0, 1), f"{folder.name}__{stem}", folder / f"{stem}.jpg")


def _add_pieces(c: Creature, body: bpy.types.Object, folder: Path) -> None:
    """Eyes and teeth: separate geometry with their own materials, rigid on their bone."""
    for kind, role, colour, rough in (
        ("eye", "eyes", c.eye_colour, 0.12),
        ("tooth", "teeth", c.teeth_colour, 0.4),
        ("tongue", "tongue", c.tongue_colour, 0.3),
    ):
        shapes = [s for s in c.shapes if s.kind == kind]
        if not shapes:
            continue
        bm = bmesh.new()
        bm.loops.layers.uv.new("UVMap")
        for s in shapes:
            _add_shape(bm, s, {"eye": (12, 8), "tongue": (10, 6)}.get(kind, (6, 1)))
        me = bpy.data.meshes.new(role)
        bm.to_mesh(me)
        bm.free()
        for p in me.polygons:
            p.use_smooth = True
        ob = bpy.data.objects.new(role, me)
        bpy.context.scene.collection.objects.link(ob)
        img = _piece_texture(role, colour, folder)
        mat = _material(role, img, None, rough)
        if kind == "eye" and c.eye_glow > 0:  # glTF emissiveFactor = colour * strength
            bsdf = mat.node_tree.nodes["Principled BSDF"]
            bsdf.inputs["Emission Color"].default_value = (*_linear(c.eye_glow_colour), 1.0)
            bsdf.inputs["Emission Strength"].default_value = min(1.0, c.eye_glow)
        me.materials.append(mat)
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        body.select_set(True)
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.join()
        print(f"[chargen] {role}: {len(shapes)} pieces")
    _weight_pieces(c, body)


def _weight_pieces(c: Creature, body: bpy.types.Object) -> None:
    """Every vertex of an eye, tooth or tongue rigidly on the bone of its nearest piece."""
    pieces = [s for s in c.shapes if s.kind in PIECES]
    roles = {i for i, m in enumerate(body.data.materials) if m.name in ("eyes", "teeth", "tongue")}
    verts = sorted({v for p in body.data.polygons if p.material_index in roles for v in p.vertices})
    if not verts:
        return
    co = np.array([body.data.vertices[i].co[:] for i in verts])
    dist = np.stack([np.abs(shape_distance(s, co)) for s in pieces], axis=1)
    groups = body.vertex_groups
    for i, k in zip(verts, dist.argmin(axis=1), strict=True):
        for g in list(body.data.vertices[i].groups):
            groups[g.group].remove([i])
        groups[pieces[k].bone].add([i], 1.0, "REPLACE")


def _weight_plates(c: Creature, body: bpy.types.Object) -> None:
    """Every plate vertex rigidly on the bone of its nearest plate (no bending, no stretching)."""
    groups = body.vertex_groups
    marker = groups.get(PLATE_GROUP)
    if marker is None:
        return
    plates = [s for s in c.shapes if s.kind in PLATES]
    verts = [v.index for v in body.data.vertices if any(g.group == marker.index for g in v.groups)]
    co = np.array([body.data.vertices[i].co[:] for i in verts])
    dist = np.stack([np.abs(shape_distance(s, co)) for s in plates], axis=1)
    for i, k in zip(verts, dist.argmin(axis=1), strict=True):
        for g in list(body.data.vertices[i].groups):
            groups[g.group].remove([i])
        groups[plates[k].bone].add([i], 1.0, "REPLACE")
    groups.remove(marker)


def _lods(c: Creature, body: bpy.types.Object) -> list[bpy.types.Object]:
    out = [body]
    for level, ratio in enumerate(c.lods[1:], start=1):
        ob = body.copy()
        ob.data = body.data.copy()
        ob.name = ob.data.name = f"body_lod{level}"
        bpy.context.scene.collection.objects.link(ob)
        ob.modifiers.clear()
        _apply(ob, "DECIMATE", decimate_type="COLLAPSE", ratio=ratio)
        _close_holes(ob)
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
    body, high = _body(c)
    _unwrap(body)
    folder = args.characters / "textures" / c.material
    base, normal = _textures(c, body, high, folder)
    bpy.data.objects.remove(high)
    body.data.materials.append(_material(c.material, base, normal, 0.85))
    _skin(arm, body)
    _split_jaw(c, body)
    _weight_plates(c, body)
    _add_pieces(c, body, folder)
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
        {**_ORIENTATION, **c.orientation},
        f"build_creature.py from {args.spec.name} (own work)",
    )
    print(f"[chargen] {c.art}: height {height:.3f} m, {len(arm.data.bones)} bones")


if __name__ == "__main__":
    main()
