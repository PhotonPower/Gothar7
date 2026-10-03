"""Conforms an MPFB2 character (.blend from mpfb_human.py) to the reference rig and exports parts.

Run inside Blender 4.5 (no MPFB code needed):
    blender --background <x.blend> --python conform_human.py -- \
        --recipe <humans/x.human.toml> --out-dir <parts/x>

Steps
1. Mix the face morph targets (data/faces/morphs.toml) from the MPFB face shape keys, then bake
   the MPFB macro shape keys and helper/clothes masks into the meshes.
2. Pose the MPFB "game_engine" rig joint by joint onto the reference rig (direction joint -> child
   joint and length; leaves follow their parent), apply the pose to every mesh and bind the meshes
   to the reference armature (bone names via data/mappings/mpfb_game_engine.toml). The morph
   offsets are carried through the pose (evaluated per target) and added back as shape keys.
3. Split the head off the body where the head weight is >= 0.5; the border ring stays identical on
   both parts (same vertices, same weights), so fit.py finds a clean seam. Morphs do not move the
   ring (no gap at the neck while talking).
4. Reduce to the recipe's triangle budget; open borders keep weight 0 so seams and hems stay.
   Meshes with morphs (head, teeth) get their targets back by barycentric interpolation from the
   unreduced mesh. Every head mesh carries the full target list in contract order (§6).
5. Rebuild materials by role (skin, cloth_<asset>, hair, eyes, eyebrows, eyelashes) with textures
   scaled to the contract sizes (§2.3), tinted from the recipe; JPEG for opaque, PNG for masks.
6. Export body.glb (body without head + clothes), head.glb (head, eyes, brows, lashes), hair.glb.
   Images are embedded here; gothar-chargen moves them to textures/ afterwards.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]
import numpy as np
from mathutils import Matrix, Vector  # type: ignore[import-not-found]
from mathutils.bvhtree import BVHTree  # type: ignore[import-not-found]
from mathutils.interpolate import poly_3d_calc  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender import build_reference_rig as reference  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.faces import Morph, load_morphs  # noqa: E402
from gothar_chargen.human import Human, asset_stem, load_human  # noqa: E402
from gothar_chargen.mapping import load_mapping  # noqa: E402
from gothar_chargen.postprocess import MASK_ROLES  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402

KEEP_GROUP = "lod_reduce"
TEXTURE_MAX = {
    "skin": 2048,
    "cloth": 1024,
    "hair": 512,  # contract allows 1024; the MPFB hair cards look the same at 512 (repo size)
    "eyes": 256,
    "eyebrows": 256,
    "eyelashes": 256,
    "teeth": 256,
    "tongue": 256,
    "beard": 512,
}
CATEGORY = {
    "skin": "skin",
    "cloth": "cloth",
    "hair": "hair",
    "eyes": "face",
    "eyebrows": "face",
    "eyelashes": "face",
    "teeth": "face",
    "tongue": "face",
    "beard": "hair",
}
ROLE_OF_TYPE = {
    "Eyes": "eyes",
    "Eyebrows": "eyebrows",
    "Eyelashes": "eyelashes",
    "Teeth": "teeth",
    "Tongue": "tongue",
    "Beard": "beard",
    "Hair": "hair",
    "Clothes": "cloth",
}
MAIN_CHILD = {
    "root": None,
    "pelvis": "spine_01",
    "spine_03": "neck",
    "neck": "head",
    "hand_l": "middle_01_l",
    "hand_r": "middle_01_r",
    "clavicle_l": "upperarm_l",
    "clavicle_r": "upperarm_r",
    "foot_l": "ball_l",
    "foot_r": "ball_r",
}
HEAD_SPLIT = 0.5
FACE_ROLES = ("eyes", "eyebrows", "eyelashes", "teeth", "tongue", "beard")  # joined into head.glb
# fixed budgets for heavy face assets (MPFB teeth ~7k triangles, mostly hidden; beards vary)
FIXED_TRIANGLES = {"teeth": 600, "beard": 1000}
CARD_ROLES = ("beard",)  # alpha cards: no border protection when reducing
SKIN_MIN_RATIO = 0.15  # the face keeps at least this share (morph quality), whatever the budget
PART_OF_ROLE = {"skin": "body", "cloth": "body", "hair": "hair"}  # FACE_ROLES and the head: head
Morphs = dict[str, np.ndarray]  # object name -> (targets, vertices, 3) offsets


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="conform_human")
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args(argv)


def _apply_modifier(obj: bpy.types.Object, name: str) -> None:
    with bpy.context.temp_override(object=obj, active_object=obj):
        bpy.ops.object.modifier_apply(modifier=name)


def _triangles(obj: bpy.types.Object) -> int:
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


# --- 1./2. morphs, bake and conform -------------------------------------------------------------


def _coords(obj: bpy.types.Object) -> np.ndarray:
    co = np.empty(len(obj.data.vertices) * 3, dtype=np.float64)
    obj.data.vertices.foreach_get("co", co)
    return co.reshape(-1, 3)


def _extract_morphs(meshes: list[bpy.types.Object], morphs: tuple[Morph, ...]) -> Morphs:
    """Contract targets mixed from the MPFB face shape keys (offsets to the Basis key)."""
    out: Morphs = {}
    for o in meshes:
        keys = o.data.shape_keys.key_blocks if o.data.shape_keys else None
        if keys is None or not any(s in keys for m in morphs for s, _ in m.mix):
            continue
        basis = np.array([k.co[:] for k in keys[0].data])
        offsets = np.zeros((len(morphs), len(basis), 3))
        for i, m in enumerate(morphs):
            for source, weight in m.mix:
                if source in keys:
                    offsets[i] += weight * (np.array([k.co[:] for k in keys[source].data]) - basis)
        out[o.name] = offsets
    return out


def _add_shape_keys(obj: bpy.types.Object, morphs: tuple[Morph, ...], offsets: np.ndarray) -> None:
    """Basis + one key per contract target (zero offsets included: same list on every mesh)."""
    base = _coords(obj)
    obj.shape_key_add(name="Basis", from_mix=False)
    for m, off in zip(morphs, offsets, strict=True):
        key = obj.shape_key_add(name=m.name, from_mix=False)
        key.data.foreach_set("co", (base + off).astype(np.float32).ravel())


def _bake(meshes: list[bpy.types.Object], morph_offsets: Morphs) -> None:
    """Applies macro shape keys and masks; morph offsets follow the vertices the masks keep."""
    for o in meshes:
        if o.data.shape_keys is not None:
            with bpy.context.temp_override(object=o, active_object=o):
                bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
        if o.name in morph_offsets:
            ids = o.data.attributes.new("gothar_vid", "INT", "POINT")
            ids.data.foreach_set("value", np.arange(len(o.data.vertices), dtype=np.int32))
        for m in [m for m in o.modifiers if m.type == "MASK"]:
            _apply_modifier(o, m.name)
        if o.name in morph_offsets:
            kept = np.empty(len(o.data.vertices), dtype=np.int32)
            o.data.attributes["gothar_vid"].data.foreach_get("value", kept)
            o.data.attributes.remove(o.data.attributes["gothar_vid"])
            morph_offsets[o.name] = morph_offsets[o.name][:, kept]


def _conform(mpfb_rig: bpy.types.Object, ref: bpy.types.Object, names: dict[str, str]) -> None:
    inverse = {v: k for k, v in names.items()}
    bpy.context.view_layer.objects.active = mpfb_rig
    bpy.ops.object.mode_set(mode="EDIT")
    for eb in mpfb_rig.data.edit_bones:
        eb.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    for b in mpfb_rig.data.bones:
        b.inherit_scale = "NONE"

    def main_child(rb: bpy.types.Bone) -> str | None:
        if rb.name in MAIN_CHILD:
            return MAIN_CHILD[rb.name]
        kids = [c for c in rb.children if not c.name.startswith("socket_")]
        return kids[0].name if len(kids) == 1 else None

    delta: dict[str, Matrix] = {}
    for mb in mpfb_rig.data.bones:  # parents first
        rb = ref.data.bones.get(names.get(mb.name, mb.name))
        if rb is None:
            continue
        rest = mb.matrix_local.to_3x3()
        child = main_child(rb)
        if child is not None:
            src = mpfb_rig.data.bones[inverse.get(child, child)].head_local - mb.head_local
            dst = ref.data.bones[child].head_local - rb.head_local
            rot = src.normalized().rotation_difference(dst.normalized()).to_matrix()
            stretch = dst.length / src.length
        else:
            rot = delta.get(mb.parent.name if mb.parent else "", Matrix.Identity(3))
            stretch = 1.0
        delta[mb.name] = rot
        m = (rot @ rest).to_4x4() @ Matrix.Diagonal((1.0, stretch, 1.0, 1.0))
        m.translation = rb.head_local
        mpfb_rig.pose.bones[mb.name].matrix = m
        bpy.context.view_layer.update()


def _posed_offsets(obj: bpy.types.Object, offsets: np.ndarray) -> np.ndarray:
    """Morph offsets after the object's modifiers (the conform pose): evaluate base and base +
    offset per target and take the difference."""
    base = _coords(obj)

    def evaluated(coords: np.ndarray) -> np.ndarray:
        obj.data.vertices.foreach_set("co", coords.astype(np.float32).ravel())
        obj.data.update()
        ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        out = np.empty(len(ev.data.vertices) * 3)
        ev.data.vertices.foreach_get("co", out)
        return out.reshape(-1, 3)

    posed = evaluated(base)
    result = np.array([evaluated(base + off) - posed for off in offsets])
    obj.data.vertices.foreach_set("co", base.astype(np.float32).ravel())
    obj.data.update()
    return result


def _rebind(
    meshes: list[bpy.types.Object],
    ref: bpy.types.Object,
    names: dict[str, str],
    morph_offsets: Morphs,
    morphs: tuple[Morph, ...],
) -> None:
    for o in meshes:
        posed = _posed_offsets(o, morph_offsets[o.name]) if o.name in morph_offsets else None
        for m in [m for m in o.modifiers if m.type == "ARMATURE"]:
            _apply_modifier(o, m.name)
        if posed is not None:
            _add_shape_keys(o, morphs, posed)
        for vg in o.vertex_groups:
            if vg.name in names:
                vg.name = names[vg.name]
        o.parent = ref
        o.matrix_parent_inverse = Matrix.Identity(4)
        o.modifiers.new("armature", "ARMATURE").object = ref


# --- 3. split head -------------------------------------------------------------------------------


def _split_head(body: bpy.types.Object) -> bpy.types.Object:
    group = body.vertex_groups["head"].index
    head_weight = np.zeros(len(body.data.vertices))
    for v in body.data.vertices:
        for g in v.groups:
            if g.group == group:
                head_weight[v.index] = g.weight
    bm = bmesh.new()
    bm.from_mesh(body.data)
    for f in bm.faces:
        f.select_set(all(head_weight[v.index] >= HEAD_SPLIT for v in f.verts))
    bm.to_mesh(body.data)
    bm.free()
    before = set(bpy.data.objects)
    for o in bpy.context.scene.objects:
        if o is not None:
            o.select_set(o is body)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.separate(type="SELECTED")
    bpy.ops.object.mode_set(mode="OBJECT")
    head = next(o for o in bpy.data.objects if o not in before)
    head.name = "head"
    if body.data.shape_keys is not None:  # the face targets stay on the head only
        body.shape_key_clear()
    if head.data.shape_keys is not None:
        _pin_border(head)
    return head


def _pin_border(obj: bpy.types.Object) -> None:
    """Morphs must not move open borders (neck seam): their offsets there are set to 0."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    border = sorted({v.index for e in bm.edges if e.is_boundary for v in e.verts})
    bm.free()
    keys = obj.data.shape_keys.key_blocks
    basis = keys[0]
    moved = 0.0
    for key in keys[1:]:
        for i in border:
            moved = max(moved, (key.data[i].co - basis.data[i].co).length)
            key.data[i].co = basis.data[i].co
    print(
        f"[chargen] {obj.name}: neck seam pinned ({len(border)} vertices, "
        f"max {moved * 1000:.1f} mm)"
    )


# --- 4. reduce -----------------------------------------------------------------------------------


def _decimate_with_morphs(obj: bpy.types.Object, ratio: float, keep_borders: bool) -> None:
    """Decimate cannot run on meshes with shape keys: remove them, reduce, and rebuild every
    target by barycentric interpolation on the nearest triangle of the unreduced mesh."""
    keys = obj.data.shape_keys.key_blocks
    names = [k.name for k in keys[1:]]
    basis = np.array([v.co[:] for v in keys[0].data])
    offsets = np.array([np.array([v.co[:] for v in k.data]) - basis for k in keys[1:]])
    obj.data.calc_loop_triangles()
    tris = [tuple(t.vertices) for t in obj.data.loop_triangles]
    tree = BVHTree.FromPolygons([Vector(p) for p in basis], tris)
    obj.shape_key_clear()
    _decimate(obj, ratio, keep_borders)
    new = _coords(obj)
    weights = np.zeros((len(new), 3))
    corners = np.zeros((len(new), 3), dtype=np.int64)
    for i, p in enumerate(new):
        location, _, tri, _ = tree.find_nearest(Vector(p))
        a, b, c = tris[tri]
        corners[i] = (a, b, c)
        weights[i] = poly_3d_calc([Vector(basis[a]), Vector(basis[b]), Vector(basis[c])], location)
    moved = np.einsum("vk,tvkx->tvx", weights, offsets[:, corners])
    obj.shape_key_add(name="Basis", from_mix=False)
    for name, off in zip(names, moved, strict=True):
        obj.shape_key_add(name=name, from_mix=False).data.foreach_set(
            "co", (new + off).astype(np.float32).ravel()
        )


MORPH_EPSILON = 5e-5  # metres: smaller morph offsets are noise from interpolation


def _prune_morphs(obj: bpy.types.Object) -> None:
    """Sets tiny offsets to exactly 0 so the glTF export keeps the sparse accessors small."""
    keys = obj.data.shape_keys.key_blocks
    basis = _coords(obj)
    for key in keys[1:]:
        co = np.empty(len(basis) * 3, dtype=np.float32)
        key.data.foreach_get("co", co)
        co = co.reshape(-1, 3)
        still = np.linalg.norm(co - basis, axis=1) < MORPH_EPSILON
        co[still] = basis[still]
        key.data.foreach_set("co", co.ravel())


def _decimate(obj: bpy.types.Object, ratio: float, keep_borders: bool = True) -> None:
    """Collapse decimation; `keep_borders` holds open borders in place (seams, hems). Hair cards
    and beards are all border, so they are reduced without it."""
    if ratio >= 0.999:
        return
    if obj.data.shape_keys is not None:
        _decimate_with_morphs(obj, ratio, keep_borders)
        return
    border: set[int] = set()
    if keep_borders:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        border = {v.index for e in bm.edges if e.is_boundary for v in e.verts}
        bm.free()
    group = obj.vertex_groups.new(name=KEEP_GROUP)
    group.add([v.index for v in obj.data.vertices if v.index not in border], 1.0, "REPLACE")
    if border:
        group.add(sorted(border), 0.0, "REPLACE")
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.ratio = ratio
    mod.vertex_group = KEEP_GROUP
    mod.vertex_group_factor = 1000.0
    mod.use_collapse_triangulate = True
    with bpy.context.temp_override(object=obj, active_object=obj):
        bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
        bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.vertex_groups.remove(obj.vertex_groups[KEEP_GROUP])


# --- 5. materials and textures -------------------------------------------------------------------


def _source_images(
    mat: bpy.types.Material | None,
) -> tuple[bpy.types.Image | None, bpy.types.Image | None]:
    if mat is None or not mat.use_nodes:
        return None, None
    images = [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]
    skip = ("norm", "nrm", "spec", "bump", "rough", "_ao", "disp", "objnorm")
    diffuse = next(
        (i for i in images if not any(s in Path(i.filepath).name.lower() for s in skip)), None
    )
    normal = next(
        (
            i
            for i in images
            if any(s in Path(i.filepath).name.lower() for s in ("norm", "nrm"))
            and "objnorm" not in Path(i.filepath).name.lower()
        ),
        None,
    )
    return diffuse, normal


def _pow2_floor(n: float) -> int:
    p = 1
    while p * 2 <= n:
        p *= 2
    return p


def _prepare_image(
    src: bpy.types.Image,
    name: str,
    limit: int,
    tint: str | None,
    keep_alpha: bool,
    tmp: Path,
    normal: bool = False,
) -> bpy.types.Image:
    source_path = Path(bpy.path.abspath(src.filepath)).resolve()
    if not source_path.is_file():
        raise SystemExit(f"texture not found: {source_path} (image {src.name})")
    img = bpy.data.images.load(str(source_path), check_existing=False)
    img.pixels[0]  # force loading the pixel data
    w, h = img.size
    if not img.has_data or w == 0:
        raise SystemExit(f"could not load texture {source_path}")
    scale = min(1.0, limit / max(w, h))
    nw, nh = _pow2_floor(w * scale), _pow2_floor(h * scale)
    if (nw, nh) != (w, h):
        img.scale(nw, nh)
    if tint:
        px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)
        rgb = [int(tint[i : i + 2], 16) / 255.0 for i in (1, 3, 5)]
        px[:, :3] *= rgb
        img.pixels.foreach_set(px.ravel())
    ext = "png" if keep_alpha or normal else "jpg"
    path = tmp / (name.replace("/", "__") + "." + ext)
    img.file_format = "PNG" if ext == "png" else "JPEG"
    img.save(filepath=str(path), quality=90)
    bpy.data.images.remove(img)
    out = bpy.data.images.load(str(path))
    out.name = name
    if normal:
        out.colorspace_settings.name = "Non-Color"
    return out


def _rebuild_material(obj: bpy.types.Object, role: str, stem: str, human: Human, tmp: Path) -> None:
    mat_name = role if role != "cloth" else f"cloth_{stem}"
    old = obj.material_slots[0].material if obj.material_slots else None
    diffuse, normal = _source_images(old)
    mat = bpy.data.materials.new(mat_name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    tint = human.tints.get(stem) or (human.tints.get("skin") if role == "skin" else None)
    category = CATEGORY[role]
    suffix = f"_{tint[1:].lower()}" if tint else ""
    mask = role in MASK_ROLES
    if diffuse is not None:
        image = _prepare_image(
            diffuse, f"{category}/{stem}{suffix}", TEXTURE_MAX[role], tint, mask, tmp
        )
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if mask:
            links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        if normal is not None:
            nimg = _prepare_image(
                normal, f"{category}/{stem}_normal", min(image.size), None, False, tmp, normal=True
            )
            ntex = nodes.new("ShaderNodeTexImage")
            ntex.image = nimg
            nmap = nodes.new("ShaderNodeNormalMap")
            links.new(ntex.outputs["Color"], nmap.inputs["Color"])
            links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    obj.data.materials.clear()
    obj.data.materials.append(mat)


# --- 6. parts ------------------------------------------------------------------------------------


def _join(target: bpy.types.Object, others: list[bpy.types.Object]) -> bpy.types.Object:
    if not others:
        return target
    objs = [target, *others]
    with bpy.context.temp_override(
        active_object=target, selected_editable_objects=objs, object=target
    ):
        bpy.ops.object.join()
    # joined objects bring their own armature modifiers along only from the active one
    return target


def _export(obj: bpy.types.Object, ref: bpy.types.Object, path: Path) -> None:
    for o in bpy.context.scene.objects:
        o.select_set(o in (obj, ref))
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=False)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), **settings)
    print(f"[chargen] wrote {path} ({_triangles(obj)} tris)")


def main() -> None:
    args = _parse_args()
    human = load_human(args.recipe)
    names = load_mapping("mpfb_game_engine").bones
    mpfb_rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    basemesh = next(o for o in meshes if "gothar_type" not in o and o.vertex_groups.get("head"))
    for o in [o for o in meshes if o is not basemesh and "gothar_type" not in o]:
        bpy.data.objects.remove(o)  # helpers MPFB adds (none expected)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]

    rig = load_rig()
    morphs = load_morphs(rig.morph_targets)
    head_group = [
        o
        for o in meshes
        if o is basemesh or ROLE_OF_TYPE.get(o.get("gothar_type", "")) in FACE_ROLES
    ]
    morph_offsets = _extract_morphs(head_group, morphs)
    for o in head_group:  # every head mesh carries the full list (§6, engine)
        morph_offsets.setdefault(o.name, np.zeros((len(morphs), len(o.data.vertices), 3)))
    _bake(meshes, morph_offsets)
    ref = reference._build_armature(rig)
    _conform(mpfb_rig, ref, names)
    _rebind(meshes, ref, names, morph_offsets, morphs)
    bpy.data.objects.remove(mpfb_rig)

    head = _split_head(basemesh)
    role_of = {basemesh.name: "skin", head.name: "skin"}
    stem_of = {basemesh.name: asset_stem(human.skin), head.name: asset_stem(human.skin)}
    for o in meshes:
        if "gothar_type" in o:
            role_of[o.name] = ROLE_OF_TYPE[o["gothar_type"]]
            stem_of[o.name] = asset_stem(o["gothar_asset"])

    # only the parts the recipe exports count (a head recipe drops its body and vice versa)
    def part_of(o: bpy.types.Object) -> str:
        if o is head or role_of[o.name] in FACE_ROLES:
            return "head"
        return PART_OF_ROLE[role_of[o.name]]

    for o in [o for o in bpy.data.objects if o.type == "MESH"]:
        if part_of(o) not in human.parts:
            bpy.data.objects.remove(o)

    # reduce: skin, clothes and hair share the budget; eyes, brows, lashes and tongue stay as
    # they are, teeth and beard get a fixed budget
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in objects:
        role = role_of[o.name]
        if role in FIXED_TRIANGLES:
            ratio = min(1.0, FIXED_TRIANGLES[role] / max(1, _triangles(o)))
            _decimate(o, ratio, keep_borders=role not in CARD_ROLES)
    fixed = [o for o in objects if role_of[o.name] in FACE_ROLES]
    reducible = [o for o in objects if o not in fixed]
    budget = human.triangles - sum(_triangles(o) for o in fixed)
    ratio = min(1.0, budget / max(1, sum(_triangles(o) for o in reducible)))
    for o in reducible:
        if role_of[o.name] == "hair":
            _decimate(o, min(1.0, ratio * 1.4))
        elif o is head:
            _decimate(o, max(ratio, SKIN_MIN_RATIO))
        else:
            _decimate(o, ratio)

    for o in objects:
        if o.data.shape_keys is not None:
            _prune_morphs(o)

    with tempfile.TemporaryDirectory(prefix="gothar-human-") as tmp_dir:
        tmp = Path(tmp_dir)
        for o in objects:
            _rebuild_material(o, role_of[o.name], stem_of[o.name], human, tmp)
        clothes = [o for o in objects if role_of[o.name] == "cloth"]
        face = [o for o in objects if role_of[o.name] in FACE_ROLES]
        hair = [o for o in objects if role_of[o.name] == "hair"]
        parts = {}
        if "body" in human.parts:
            parts["body"] = _join(basemesh, clothes)
            parts["body"].name = "body"
        if "head" in human.parts:
            parts["head"] = _join(head, face)
        for img in bpy.data.images:  # pack prepared images so the export embeds them
            if (
                img.filepath
                and img.users
                and not img.packed_file
                and Path(bpy.path.abspath(img.filepath)).is_file()
            ):
                img.pack()
        if hair and "hair" in human.parts:
            parts["hair"] = _join(hair[0], hair[1:])
            parts["hair"].name = "hair"
        for role, obj in parts.items():
            for other in parts.values():
                other.hide_set(other is not obj)
            _export(obj, ref, args.out_dir / f"{role}.glb")


if __name__ == "__main__":
    main()
