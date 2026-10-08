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
from gothar_chargen.blender.lod import make_lods  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.faces import Morph, load_morphs  # noqa: E402
from gothar_chargen.human import BASEMESH, Derive, Human, asset_stem, load_human  # noqa: E402
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
    "KitHair": "hair",  # hair kits: each style its own part
    "KitBeard": "beard",
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
NEUTRAL_MEAN = 0.55  # mean luminance of neutral kit textures (palette colours multiply it)
NEUTRAL_CONTRAST = 0.5  # pattern contrast kept in neutral kit textures
KIT_TEXTURE_MAX = 512  # kit garments (contract allows 1024 for cloth; repo size)
FACE_ROLES = ("eyes", "eyebrows", "eyelashes", "teeth", "tongue", "beard")  # joined into head.glb
# fixed budgets for heavy face assets (MPFB teeth ~7k triangles, mostly hidden; beards vary)
FIXED_TRIANGLES = {"teeth": 600, "beard": 1000}
KIT_BEARD_TRIANGLES = 600  # beards of hair kits (each carries the face morphs)
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


def _copy_skin(basemesh: bpy.types.Object, d: Derive) -> bpy.types.Object:
    """A piece derived from the skin starts as a copy of the base mesh before its helper
    geometry is masked away: only the vertices of the MPFB vertex group `d.group` remain
    (``helper-hair``: a scalp cap without ears and face)."""
    skin = basemesh.copy()
    skin.data = basemesh.data.copy()
    bpy.context.scene.collection.objects.link(skin)
    for m in [m for m in skin.modifiers if m.type == "MASK"]:
        skin.modifiers.remove(m)
    group = skin.vertex_groups.get(d.group or "")
    if group is None:
        raise SystemExit(f"derive.{d.name}: base mesh has no vertex group {d.group!r}")
    keep = {v.index for v in skin.data.vertices if any(g.group == group.index for g in v.groups)}
    bm = bmesh.new()
    bm.from_mesh(skin.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context="VERTS")
    bm.to_mesh(skin.data)
    bm.free()
    skin["gothar_asset"] = d.name
    skin["gothar_type"] = "Clothes"
    skin["gothar_derive"] = d.name
    data = Path(bpy.context.scene["gothar_mpfb_data"])
    if d.texture:
        skin["gothar_texture"] = str(data / d.texture)
    if d.normal:
        skin["gothar_normal"] = str(data / d.normal)
    return skin


DOME_BAND = (0.05, 0.09)  # metres below the top of the skull: width and depth (above the ears)
DOME_HEIGHT = 0.12  # vertical half axis of the dome (crown to about ear level)


HEAD_MARGIN = 0.004  # metres between a head and a piece pushed out over it
HEAD_FALLOFF = 0.05  # metres around a poking head point within which a piece is pushed out
HEAD_CROWN = 0.07  # metres below the highest head checked (heads differ there; the face is open)


def _head_points(characters: Path, pattern: str) -> np.ndarray:
    """Skin vertices (lod0) of the head parts matching `pattern` (e.g. head_f_*), in Blender rig
    space (glTF y-up, +z front -> z-up, -y front)."""
    from gothar_chargen.gltf import Gltf
    from gothar_chargen.partdata import lod_meshes

    points = []
    for path in sorted(characters.glob(f"parts/{pattern}/head.glb")):
        mesh = lod_meshes(Gltf.load(path))[0]
        for pos, mat in zip(mesh.positions, mesh.materials, strict=True):
            if mat == "skin":
                points.append(np.stack([pos[:, 0], -pos[:, 2], pos[:, 1]], axis=1))
    if not points:
        raise SystemExit(f"no head parts match parts/{pattern}/head.glb")
    return np.concatenate(points)


def _push_over_heads(obj: bpy.types.Object, heads: np.ndarray) -> None:
    """Push a piece out where any of the given heads would stick through it: a crown point
    further from the head centre than the piece in the same direction pushes the nearby vertices
    of the piece out along their normals, fading over HEAD_FALLOFF (independent of the piece's
    face normals – some sources have an inner lining)."""
    from mathutils.bvhtree import BVHTree

    mesh = obj.data
    mesh.update()
    co = _coords(obj)
    normals = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
    mesh.vertices.foreach_get("normal", normals)
    normals = normals.reshape(-1, 3)
    tree = BVHTree.FromPolygons([tuple(v) for v in co], [tuple(p.vertices) for p in mesh.polygons])
    crown = heads[heads[:, 2] > heads[:, 2].max() - HEAD_CROWN]
    centre = Vector((0.0, *heads[:, 1:].mean(axis=0)))
    push = np.zeros(len(co))
    for p in crown:
        ray = Vector(p) - centre
        radius = ray.length
        hit, _, _, reach = tree.ray_cast(centre, ray.normalized(), 0.3)
        if hit is None:
            continue  # the piece is open there (face)
        depth = radius + HEAD_MARGIN - reach
        if depth <= 0:
            continue  # the head stays inside the piece
        d = np.linalg.norm(co - np.array(hit), axis=1)
        push = np.maximum(push, depth * np.clip(1.0 - d / HEAD_FALLOFF, 0.0, 1.0))
    if push.any():
        mesh.vertices.foreach_set("co", (co + normals * push[:, None]).ravel())
        mesh.update()
    print(f"[chargen] pushed {obj.name} out over heads by up to {push.max() * 1000:.1f} mm")


def _dome(obj: bpy.types.Object, heads: np.ndarray | None = None) -> None:
    """Own geometry: replace the skin copy by a smooth half-ellipsoid around the skull – width and
    depth from a band above the ears, grown until every skull point lies inside, bound fully to
    the head bone."""
    co = _coords(obj)
    top = co[:, 2].max()
    band = co[(co[:, 2] < top - DOME_BAND[0]) & (co[:, 2] > top - DOME_BAND[1])]
    skull = co[co[:, 2] > top - DOME_BAND[1]]
    if heads is not None:  # every head of the sex must fit under the dome
        skull = np.concatenate([skull, heads[heads[:, 2] > top - DOME_BAND[1]]])
    centre = np.array([0.0, (band[:, 1].min() + band[:, 1].max()) / 2, top - DOME_HEIGHT])
    level = top - sum(DOME_BAND) / 2 - centre[2]
    scale = np.sqrt(1.0 - (level / DOME_HEIGHT) ** 2)  # the band's section of the ellipsoid
    radii = np.array(
        [
            np.abs(band[:, 0]).max() / scale,
            (band[:, 1].max() - band[:, 1].min()) / 2 / scale,
            DOME_HEIGHT,
        ]
    )
    radii *= max(1.0, np.sqrt((((skull - centre) / radii) ** 2).sum(axis=1)).max())
    head = obj.vertex_groups["head"].index
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.0, calc_uvs=True)
    for v in bm.verts:
        v.co = (v.co.x * radii[0], centre[1] + v.co.y * radii[1], centre[2] + v.co.z * radii[2])
    lower = [v for v in bm.verts if v.co.z < centre[2] - 0.5 * radii[2]]
    bmesh.ops.delete(bm, geom=lower, context="VERTS")  # the bisect cuts the rest
    deform = bm.verts.layers.deform.verify()
    for v in bm.verts:
        v[deform][head] = 1.0
    for f in bm.faces:
        f.material_index = 0
        f.smooth = True
    bm.to_mesh(obj.data)
    bm.free()
    print(f"[chargen] dome radii {np.round(radii, 3)} centre {np.round(centre, 3)}")


def _body_neck_height(characters: Path) -> float:
    """Mean height of the neck rings of the base body parts (glTF y-up = Blender z)."""
    from gothar_chargen.gltf import Gltf
    from gothar_chargen.partdata import data_of, lod_meshes

    heights = []
    for path in sorted(characters.glob("parts/body_*/body.glb")):
        g = Gltf.load(path)
        mesh = lod_meshes(g)[0]
        ring = data_of(g).get("neck", {}).get(mesh.node, [])
        heights += [mesh.positions[pt[0][0]][pt[0][1]][1] for pt in ring]
    if not heights:
        raise SystemExit("no base body parts with neck data (build bodies first)")
    return float(np.mean(heights))


def _to_body_neck(head: bpy.types.Object, characters: Path) -> None:
    """Moves the head parts (head, face assets, hair) up or down so that the neck seam of the
    head meets the neck of the base bodies: the age macro changes the stature in MakeHuman, so
    heads of other ages would otherwise stretch the neck and sit too high or too low."""
    bm = bmesh.new()
    bm.from_mesh(head.data)
    # the neck is the lowest open border loop: hair loaded as clothes (hair kits) deletes the
    # scalp under it, which opens more border loops on top of the head
    loops: list[list[float]] = []
    seen: set[int] = set()
    for start in [v for v in bm.verts if v.is_boundary]:
        if start.index in seen:
            continue
        loop, stack = [], [start]
        seen.add(start.index)
        while stack:
            v = stack.pop()
            loop.append(v.co.z)
            for e in v.link_edges:
                if e.is_boundary:
                    w = e.other_vert(v)
                    if w.index not in seen:
                        seen.add(w.index)
                        stack.append(w)
        loops.append(loop)
    bm.free()
    if not loops:
        raise SystemExit("head without an open neck border")
    border = min(loops, key=lambda zs: float(np.mean(zs)))
    shift = _body_neck_height(characters) - float(np.mean(border))
    for o in [o for o in bpy.data.objects if o.type == "MESH"]:
        o.data.vertices.foreach_set("co", (_coords(o) + (0.0, 0.0, shift)).ravel())
        if o.data.shape_keys is not None:
            for key in o.data.shape_keys.key_blocks:
                co = np.empty(len(key.data) * 3, dtype=np.float64)
                key.data.foreach_get("co", co)
                key.data.foreach_set("co", (co.reshape(-1, 3) + (0.0, 0.0, shift)).ravel())
        o.data.update()
    print(
        f"[chargen] head moved {shift * 1000:+.1f} mm to the neck of the base bodies"
        f" (neck border {float(np.mean(border)):.3f} m, {len(border)} vertices)"
    )


def _derive(
    obj: bpy.types.Object,
    d: Derive,
    heads: np.ndarray | None = None,
    joints: dict[str, np.ndarray] | None = None,
) -> None:
    """Own simple piece from a fitted garment: keep the vertices bound mostly to the `keep` bones
    and not to the `cut` bones (near the `near` joints; with `depth` or `band` between
    planes), smooth the shape into a plate (`smooth`), push it along the normals, scale the UVs
    for a tiling texture, add own geometry (panel, nasal, brim) and limit the weights to `bones`."""
    mesh = obj.data
    joints = joints or {}
    if d.dome:
        _dome(obj, heads)
    if d.panel and d.band is not None and d.band_at is not None:
        _panel(obj, d.panel, *_band_heights(d, joints))
    # bones only: the skin copy also carries MPFB selection groups ("body" = 1 everywhere)
    groups = {g.index: g.name for g in obj.vertex_groups if not joints or g.name in joints}
    centres = np.array([joints[j] for j in d.near]) if d.near else None
    drop = []
    for v in mesh.vertices:
        bones = [g for g in v.groups if g.group in groups]
        best = groups[max(bones, key=lambda g: g.weight).group] if bones else ""
        if (
            (d.cut and best.startswith(d.cut))
            or (d.keep and not best.startswith(d.keep))
            or (
                centres is not None
                and np.linalg.norm(centres - np.array(v.co), axis=1).min() > d.radius
            )
        ):
            drop.append(v.index)
    if drop:
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[bm.verts[i] for i in drop], context="VERTS")
        bm.to_mesh(mesh)
        bm.free()
    if d.depth is not None:  # clean cut: plane `depth` below the top, front edge raised by `tilt`
        co = _coords(obj)
        tilt = np.radians(d.tilt)
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.bisect_plane(
            bm,
            geom=bm.verts[:] + bm.edges[:] + bm.faces[:],
            plane_co=(0.0, float(co[:, 1].mean()), float(co[:, 2].max()) - d.depth),
            plane_no=(0.0, float(np.sin(tilt)), float(np.cos(tilt))),  # the figure faces -Y
            clear_inner=True,
        )
        bm.to_mesh(mesh)
        bm.free()
    if d.band is not None and not d.panel:  # clean cuts at fixed heights
        low, high = _band_heights(d, joints)
        for height, keep_above in ((low, True), (high, False)):
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bmesh.ops.bisect_plane(
                bm,
                geom=bm.verts[:] + bm.edges[:] + bm.faces[:],
                plane_co=(0.0, 0.0, height),
                plane_no=(0.0, 0.0, 1.0),
                clear_inner=keep_above,
                clear_outer=not keep_above,
            )
            bm.to_mesh(mesh)
            bm.free()
    if d.flatten:
        _flatten_front(obj, d.flatten)
    if d.smooth:
        _smooth_shape(obj, d.smooth)
    if d.offset or d.bulge:
        normals = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
        mesh.vertices.foreach_get("normal", normals)
        amount = np.full(len(mesh.vertices), d.offset)
        if d.bulge:  # domed plate: further out towards the middle, `offset` at the border
            amount += d.bulge * _inside_share(obj)
        shift = normals.reshape(-1, 3) * amount[:, None]
        co = _coords(obj) + shift
        mesh.vertices.foreach_set("co", co.ravel())
        if mesh.shape_keys is not None:
            for key in mesh.shape_keys.key_blocks:
                kco = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
                key.data.foreach_get("co", kco)
                key.data.foreach_set("co", (kco.reshape(-1, 3) + shift).ravel())
    if d.uv_scale != 1.0 and mesh.uv_layers.active is not None:
        uv = np.empty(len(mesh.loops) * 2, dtype=np.float64)
        mesh.uv_layers.active.data.foreach_get("uv", uv)
        mesh.uv_layers.active.data.foreach_set("uv", uv * d.uv_scale)
    if heads is not None and not d.dome:
        _push_over_heads(obj, heads)
    if d.nasal is not None:
        _add_nasal(obj, *d.nasal)
    if d.brim:
        _add_brim(obj, d.brim)
    if d.rim:
        _add_rim(obj, d.rim)
    if d.bones:
        _limit_weights(obj, d.bones)
    mesh.update()
    print(f"[chargen] derived {d.name}: cut {len(drop)} vertices, offset {d.offset} m")


def _band_heights(d: Derive, joints: dict[str, np.ndarray]) -> tuple[float, float]:
    """Low and high cut of `band`, relative to the heights of the `band_at` joints."""
    assert d.band is not None and d.band_at is not None
    for joint in d.band_at:
        if joint not in joints:
            raise SystemExit(f"derive {d.name}: unknown joint '{joint}'")
    return (
        float(joints[d.band_at[0]][2]) + d.band[0],
        float(joints[d.band_at[1]][2]) + d.band[1],
    )


PANEL_GRID = (12, 16)  # columns, rows of an own front panel
PANEL_REACH = (0.03, 0.025)  # metres: skin points per grid point, across and up/down
PANEL_FLARE = 0.04  # metres: the hem stands this much further out than the top (over skirts)
PANEL_HUG = 0.95  # share of the body's half width the panel covers where it lies on the body
PANEL_TAPER = 0.15  # the hem is this share narrower than the panel at the hips
PANEL_ARCH = 0.015  # metres: the middle stands further out than the sides (cloth, not a board)
PANEL_WRAP = 0.03  # metres the side edges fall back around the hips
PANEL_FOLDS = (3.0, 5.0)  # soft vertical folds (two overlaid waves: uneven), deepening downwards
PANEL_FOLD_DEPTH = 0.02  # metres at the hem
PANEL_HEM = 0.012  # metres: the hem is uneven (cloth hangs a little longer in the folds)


def _panel(obj: bpy.types.Object, width: float, low: float, high: float) -> None:
    """Own geometry: replace the skin copy by a cloth panel in front of the body between the
    heights `low` and `high`, at most `width` wide. On the body (waist, hips) it is as wide as the
    body and follows its front around the hips; below the widest row it hangs straight down
    (instead of following the legs), tapering towards the hem, with a slight arch and soft folds.
    Every grid point takes the bone weights of the nearest skin vertex."""
    co = _coords(obj)
    mesh = obj.data
    torso = co[np.abs(co[:, 0]) < width]  # no arms or hands
    cols, rows = PANEL_GRID
    zs = np.linspace(high, low, rows)  # top row first
    # half width per row: the body's where the panel lies on it, tapering below the widest row
    half = np.full(rows, width / 2)
    for r, z in enumerate(zs):
        level = torso[np.abs(torso[:, 2] - z) < PANEL_REACH[1]]
        if len(level):
            front = level[level[:, 1] <= level[:, 1].mean()]  # the front half of the section
            half[r] = min(width / 2, PANEL_HUG * float(np.abs(front[:, 0]).max()))
    widest = int(np.argmax(half[: rows // 2]))
    below = np.arange(rows) > widest
    share = (np.arange(rows) - widest) / max(1, rows - 1 - widest)
    half = np.where(below, half[widest] * (1.0 - PANEL_TAPER * share), half)
    across = np.linspace(-1.0, 1.0, cols)
    xs = half[:, None] * across[None, :]
    ys = np.empty((rows, cols))
    for r, z in enumerate(zs):
        for c in range(cols):
            near = torso[
                (np.abs(torso[:, 0] - xs[r, c]) < PANEL_REACH[0])
                & (np.abs(torso[:, 2] - z) < PANEL_REACH[1])
            ]
            front = float(near[:, 1].min()) if len(near) else np.inf  # the figure faces -Y
            ys[r, c] = front if r == 0 else min(front, ys[r - 1, c])  # hangs, never recedes
        valid = np.isfinite(ys[r])
        if not valid.any():
            raise SystemExit(f"panel: no skin at height {z:.2f} m")
        # columns beside the body take their nearest column with skin
        idx = np.arange(cols)
        nearest = idx[valid][np.abs(idx[:, None] - idx[valid][None, :]).argmin(axis=1)]
        ys[r] = ys[r, nearest]
    for _ in range(2):  # even out the columns a little (no single-column dents)
        ys[:, 1:-1] = np.minimum(ys[:, 1:-1], (ys[:, :-2] + ys[:, 1:-1] + ys[:, 2:]) / 3)
    drape = ((high - zs) / (high - low))[:, None]  # 0 at the waist .. 1 at the hem
    wave = (across + 1.0) / 2
    folds = 0.6 * (0.5 - 0.5 * np.cos(2 * np.pi * PANEL_FOLDS[0] * wave)) + 0.4 * (
        0.5 - 0.5 * np.cos(2 * np.pi * PANEL_FOLDS[1] * wave + 1.3)
    )
    ys -= PANEL_FLARE * drape  # a little flare towards the hem
    ys -= PANEL_ARCH * (1.0 - across[None, :] ** 2) + PANEL_FOLD_DEPTH * drape * folds[None, :]
    ys += PANEL_WRAP * across[None, :] ** 2 * (1.0 - 0.5 * drape)  # the sides fall back
    hem = np.where(np.arange(rows) == rows - 1, 1.0, 0.0)[:, None]
    zs_grid = zs[:, None] - PANEL_HEM * hem * folds[None, :]  # longer in the folds
    # bone weights of the nearest skin vertex
    weights: list[dict[int, float]] = [{} for _ in range(len(mesh.vertices))]
    for v in mesh.vertices:
        weights[v.index] = {g.group: g.weight for g in v.groups}
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new()
    deform = bm.verts.layers.deform.verify()
    grid = [
        [bm.verts.new((xs[r, c], ys[r, c], zs_grid[r, c])) for c in range(cols)]
        for r in range(rows)
    ]
    for r in range(rows):
        for c in range(cols):
            p = np.array([xs[r, c], ys[r, c], zs_grid[r, c]])
            nearest = int(np.argmin(np.linalg.norm(co - p, axis=1)))
            for group, w in weights[nearest].items():
                grid[r][c][deform][group] = w
    for r in range(rows - 1):
        for c in range(cols - 1):
            quad = (grid[r][c], grid[r + 1][c], grid[r + 1][c + 1], grid[r][c + 1])
            f = bm.faces.new(quad)  # normal to the front (-Y)
            f.smooth = True
            for loop, (rr, cc) in zip(
                f.loops, ((r, c), (r + 1, c), (r + 1, c + 1), (r, c + 1)), strict=True
            ):
                loop[uv].uv = ((across[cc] + 1.0) / 2, 1.0 - (high - zs[rr]) / (high - low))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for f in bm.faces:
        if f.normal.y > 0:  # face the front
            f.normal_flip()
    bm.to_mesh(mesh)
    bm.free()
    print(
        f"[chargen] panel {2 * half.min():.2f}-{2 * half.max():.2f} m wide, {high - low:.2f} m long"
    )


FLATTEN_BUMP = 0.01  # metres in front of the first fit that count as a bump, not torso


def _flatten_front(obj: bpy.types.Object, amount: float) -> None:
    """Neutral plate: the front (-Y) is pulled towards a smooth quadratic envelope over x and z
    that encloses the protruding parts (e.g. the breasts) – a cuirass instead of an anatomic
    shape. `amount` 0..1 blends from the original to the envelope."""
    mesh = obj.data
    co = _coords(obj)
    centre = co[:, 1].mean()
    depth = max(centre - co[:, 1].min(), 1e-6)
    front = np.clip((centre - co[:, 1]) / depth, 0.0, 1.0)  # 0 at the sides .. 1 at the front
    sel = front > 0.3
    x, z = co[sel, 0], co[sel, 2]
    basis = np.stack([np.ones_like(x), x, z, x * x, z * z, x * z], axis=1)
    coef = np.linalg.lstsq(basis, co[sel, 1], rcond=None)[0]
    # second pass without the points well in front of the first fit (breasts): the torso alone
    torso = (co[sel, 1] - basis @ coef) > -FLATTEN_BUMP
    coef = np.linalg.lstsq(basis[torso], co[sel, 1][torso], rcond=None)[0]
    xa, za = co[:, 0], co[:, 2]
    fit = np.stack([np.ones_like(xa), xa, za, xa * xa, za * za, xa * za], axis=1) @ coef
    ahead = co[sel, 1] - fit[sel]  # negative: in front of the fit
    envelope = fit + float(np.percentile(ahead, 3))  # encloses all but the outermost tips
    weight = amount * np.sin(front * np.pi / 2)  # sides keep their shape
    co[:, 1] = np.where(front > 0, co[:, 1] + weight * (envelope - co[:, 1]), co[:, 1])
    mesh.vertices.foreach_set("co", co.ravel())
    mesh.update()


def _smooth_shape(obj: bpy.types.Object, iterations: int) -> None:
    """Taubin smoothing (shrink-free): cloth folds become a smooth shell, ragged open borders
    become round outlines (smoothed along the border)."""
    mesh = obj.data
    co = _coords(obj)
    edges = np.array([e.vertices[:] for e in mesh.edges], dtype=np.int64)
    if not len(edges):
        return
    bm = bmesh.new()
    bm.from_mesh(mesh)
    border = np.array([v.is_boundary for v in bm.verts])
    border_edges = (
        np.array([e.verts[0].index for e in bm.edges if e.is_boundary]),
        np.array([e.verts[1].index for e in bm.edges if e.is_boundary]),
    )
    bm.free()

    def laplace(pairs: tuple[np.ndarray, np.ndarray], points: np.ndarray) -> np.ndarray:
        a, b = pairs
        acc = np.zeros_like(points)
        cnt = np.zeros(len(points))
        np.add.at(acc, a, points[b])
        np.add.at(acc, b, points[a])
        np.add.at(cnt, a, 1)
        np.add.at(cnt, b, 1)
        return np.where(cnt[:, None] > 0, acc / np.maximum(cnt, 1)[:, None] - points, 0.0)

    inner = (edges[:, 0], edges[:, 1])
    for _ in range(iterations):
        for factor in (0.5, -0.53):
            delta = laplace(inner, co)
            # open borders: smoothed along the border only (round outlines)
            delta[border] = laplace(border_edges, co)[border] if len(border_edges[0]) else 0.0
            co = co + factor * delta
    mesh.vertices.foreach_set("co", co.ravel())
    mesh.update()


def _inside_share(obj: bpy.types.Object) -> np.ndarray:
    """Per vertex 0 at the open border .. 1 at the point farthest from it (smooth falloff)."""
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    border = np.array([v.is_boundary for v in bm.verts])
    bm.free()
    co = _coords(obj)
    if not border.any():
        return np.ones(len(co))
    dist = np.linalg.norm(co[:, None, :] - co[border][None, :, :], axis=2).min(axis=1)
    share = dist / max(float(dist.max()), 1e-9)
    return np.sin(share * np.pi / 2)  # rises quickly from the border, flat in the middle


def _add_rim(obj: bpy.types.Object, depth: float) -> None:
    """Own geometry: the open border folded inwards by `depth` – a plate gets a visible edge
    (thickness) instead of a paper-thin outline."""
    mesh = obj.data
    mesh.update()
    normals = {v.index: np.array(v.normal) for v in mesh.vertices}
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    rim = [e for e in bm.edges if e.is_boundary]
    if not rim:
        bm.free()
        return
    origin = {v: v.index for e in rim for v in e.verts}
    new = bmesh.ops.extrude_edge_only(bm, edges=rim)["geom"]
    new_verts = [g for g in new if isinstance(g, bmesh.types.BMVert)]
    for v in new_verts:  # each new vertex sits on its border vertex: find it by position
        src = min(origin, key=lambda o: (o.co - v.co).length_squared)
        n = normals[origin[src]]
        v.co.x -= n[0] * depth
        v.co.y -= n[1] * depth
        v.co.z -= n[2] * depth
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def _add_brim(obj: bpy.types.Object, width: float) -> None:
    """Own geometry: a brim around the open rim (kettle helmet), sloping slightly down."""
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    rim = [e for e in bm.edges if e.is_boundary]
    if not rim:
        bm.free()
        raise SystemExit(f"{obj.name}: brim needs an open rim (use depth)")
    centre = np.mean([np.array(v.co) for v in bm.verts], axis=0)
    new = bmesh.ops.extrude_edge_only(bm, edges=rim)["geom"]
    for v in [g for g in new if isinstance(g, bmesh.types.BMVert)]:
        out = np.array(v.co) - centre
        out[2] = 0.0
        out /= max(np.linalg.norm(out), 1e-9)
        v.co.x += out[0] * width
        v.co.y += out[1] * width
        v.co.z -= 0.35 * width
    bm.to_mesh(mesh)
    bm.free()


def _limit_weights(obj: bpy.types.Object, prefixes: tuple[str, ...]) -> None:
    """Plates: weights only on the bones with these prefixes (renormalised; a vertex without any
    keeps the first such bone of the piece) – they bend like a stiff shell, not like cloth."""
    mesh = obj.data
    allowed = [g for g in obj.vertex_groups if g.name.startswith(prefixes)]
    if not allowed:
        raise SystemExit(f"{obj.name}: no vertex group matches bones {prefixes}")
    keep = {g.index for g in allowed}
    fallback = allowed[0]
    for v in mesh.vertices:
        weights = {g.group: g.weight for g in v.groups if g.group in keep and g.weight > 0}
        total = sum(weights.values())
        for g in list(v.groups):
            obj.vertex_groups[g.group].remove([v.index])
        if total <= 0:
            fallback.add([v.index], 1.0, "REPLACE")
            continue
        for gi, w in weights.items():
            obj.vertex_groups[gi].add([v.index], w / total, "REPLACE")


def _add_nasal(obj: bpy.types.Object, width: float, length: float) -> None:
    """Own geometry: a nose guard (thin bar) from the front of the rim down over the nose,
    leaning forward to clear it, bound fully to the head bone."""
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    rim = [v for v in bm.verts if v.is_boundary and abs(v.co.x) < 0.02]
    if not rim:
        bm.free()
        raise SystemExit(f"{obj.name}: nasal needs an open rim at the front (use depth)")
    top = min(rim, key=lambda v: v.co.y).co.copy()  # the figure faces -Y
    # the top reaches up into the dome (which curves back above the rim), the bottom leans
    # forward to clear the nose
    lean, thick, overlap, tuck = 0.15 * length, 0.004, 0.015, 0.012
    corners = []
    for dz, dy in ((overlap, tuck), (-length, -lean)):
        for y_off in (0.0, -thick):
            for x in (-width / 2, width / 2):
                corners.append(bm.verts.new((x, top.y + dy + y_off, top.z + dz)))
    # corners: [top back l, r, top front l, r, bottom back l, r, bottom front l, r]
    quads = ((0, 1, 3, 2), (4, 6, 7, 5), (0, 2, 6, 4), (1, 5, 7, 3), (2, 3, 7, 6), (0, 4, 5, 1))
    faces = [bm.faces.new([corners[i] for i in q]) for q in quads]
    bmesh.ops.recalc_face_normals(bm, faces=faces)
    deform = bm.verts.layers.deform.verify()
    head = obj.vertex_groups["head"].index
    uv = bm.loops.layers.uv.verify()
    for v in corners:
        v[deform].clear()
        v[deform][head] = 1.0
    for f in faces:
        f.material_index = 0
        for loop in f.loops:  # a small patch of the tiling metal texture
            co = loop.vert.co
            loop[uv].uv = (0.5 + (co.x / width) * 0.05, 0.5 + (co.z - top.z) * 2.0)
    bm.to_mesh(mesh)
    bm.free()


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
    neutral: bool = False,
    retouch: tuple[tuple[float, ...], ...] = (),
) -> bpy.types.Image:
    source_path = Path(bpy.path.abspath(src.filepath)).resolve()
    if not source_path.is_file():
        raise SystemExit(f"texture not found: {source_path} (image {src.name})")
    img = bpy.data.images.load(str(source_path), check_existing=False)
    img.pixels[0]  # force loading the pixel data
    w, h = img.size
    if retouch:  # cover marks with a shifted patch of the same texture (image coords, top left)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1]  # top row first
        src = px.copy()
        for x0, y0, x1, y1, dx, dy in retouch:
            c0, c1, r0, r1 = int(x0 * w), int(x1 * w), int(y0 * h), int(y1 * h)
            sx, sy = int(dx * w), int(dy * h)
            px[r0:r1, c0:c1] = src[r0 + sy : r1 + sy, c0 + sx : c1 + sx]
        img.pixels.foreach_set(px[::-1].ravel())
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
    if neutral:  # kit garments: grey with headroom, the figure palette gives the colour
        px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)
        lum = px[:, :3] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        seen = px[:, 3] > 0.5 if keep_alpha else np.ones(len(px), dtype=bool)  # hair cards
        mean = max(float(lum[seen].mean()) if seen.any() else float(lum.mean()), 1e-3)
        lum = mean + NEUTRAL_CONTRAST * (lum - mean)  # softer patterns, colour from the palette
        lum *= NEUTRAL_MEAN / mean
        px[:, :3] = np.clip(lum, 0.0, 1.0)[:, None]
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
    kit = (role == "cloth" and "cloth" in human.parts) or (
        human.hair_kit and role in ("hair", "beard")
    )
    piece = human.part_name(stem) if kit else stem  # our file/material name for the piece
    mat_name = role if role != "cloth" else f"cloth_{piece}"
    if role == "beard" and not kit:  # baked into a head part: assemble merges materials by name and
        mat_name = "beard_head"  # the head's would replace a kit beard's (role stays "beard")
    texture_name = piece
    old = obj.material_slots[0].material if obj.material_slots else None
    diffuse, normal = _source_images(old)
    if "gothar_texture" in obj:  # derived piece: own tiling texture, shared by name of its source
        texture_name = Path(obj["gothar_texture"]).parent.name.lower()  # e.g. ambientCG Metal021
        diffuse = bpy.data.images.load(obj["gothar_texture"], check_existing=True)
        normal = (
            bpy.data.images.load(obj["gothar_normal"], check_existing=True)
            if "gothar_normal" in obj
            else None
        )
    mat = bpy.data.materials.new(mat_name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    tint = human.tints.get(stem) or (human.tints.get("skin") if role == "skin" else None)
    category = CATEGORY[role]
    neutral = kit and human.neutral
    suffix = "_neutral" if neutral else f"_{tint[1:].lower()}" if tint else ""
    mask = role in MASK_ROLES
    if diffuse is not None:
        image = _prepare_image(
            diffuse,
            f"{category}/{texture_name}{suffix}",
            KIT_TEXTURE_MAX if kit else TEXTURE_MAX[role],
            tint,
            mask,
            tmp,
            neutral=neutral,
            retouch=human.retouch.get(piece, ()) if kit else (),
        )
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if mask:
            links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        if normal is not None and not neutral:  # kit garments: plain cloth, no normal maps
            nimg = _prepare_image(
                normal,
                f"{category}/{texture_name}_normal",
                min(image.size),
                None,
                False,
                tmp,
                normal=True,
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


def _export(objs: list[bpy.types.Object], ref: bpy.types.Object, path: Path) -> None:
    for o in bpy.context.scene.objects:
        o.hide_set(o.type == "MESH" and o not in objs)
        o.select_set(o in objs or o is ref)
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=False)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), **settings)
    tris = "/".join(str(_triangles(o)) for o in objs)
    print(f"[chargen] wrote {path} ({tris} tris per LOD)")


def main() -> None:
    args = _parse_args()
    human = load_human(args.recipe)
    names = load_mapping("mpfb_game_engine").bones
    mpfb_rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    basemesh = next(o for o in meshes if "gothar_type" not in o and o.vertex_groups.get("head"))
    for o in [o for o in meshes if o is not basemesh and "gothar_type" not in o]:
        bpy.data.objects.remove(o)  # helpers MPFB adds (none expected)
    for d in human.derive:
        if d.source == BASEMESH:
            _copy_skin(basemesh, d)
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
        if "gothar_derive" in o:
            stem_of[o.name] = o["gothar_derive"]
            d = next(d for d in human.derive if d.name == o["gothar_derive"])
            characters = args.out_dir.resolve().parent.parent
            joints = {b.name: np.array(ref.matrix_world @ b.head_local) for b in ref.data.bones}
            _derive(o, d, _head_points(characters, d.heads) if d.heads else None, joints)

    # only the parts the recipe exports count (a head recipe drops its body and vice versa)
    def part_of(o: bpy.types.Object) -> str:
        if human.hair_kit and o.get("gothar_type") in ("KitHair", "KitBeard"):
            return role_of[o.name]  # hair kit: "hair" or "beard"
        if o is head or role_of[o.name] in FACE_ROLES:
            return "head"
        if role_of[o.name] == "cloth" and "cloth" in human.parts:
            return "cloth"  # clothing kit: every garment becomes its own part
        return PART_OF_ROLE[role_of[o.name]]

    if ("head" in human.parts or human.hair_kit) and "body" not in human.parts:
        _to_body_neck(head, args.out_dir.resolve().parent.parent)  # a head for any base body
    for o in [o for o in bpy.data.objects if o.type == "MESH"]:
        if part_of(o) not in human.parts:
            bpy.data.objects.remove(o)

    # reduce: skin, clothes and hair share the budget; eyes, brows, lashes and tongue stay as
    # they are, teeth and beard get a fixed budget
    objects = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in objects:
        role = role_of[o.name]
        if role in FIXED_TRIANGLES:
            target = FIXED_TRIANGLES[role]
            if human.hair_kit and role == "beard":  # kit beards: smaller (one per head and style)
                target = human.budget.get(human.part_name(stem_of[o.name]), KIT_BEARD_TRIANGLES)
            ratio = min(1.0, target / max(1, _triangles(o)))
            _decimate(o, ratio, keep_borders=role not in CARD_ROLES)
    fixed = [o for o in objects if role_of[o.name] in FACE_ROLES]
    reducible = [o for o in objects if o not in fixed]
    budget = human.triangles - sum(_triangles(o) for o in fixed)
    ratio = min(1.0, budget / max(1, sum(_triangles(o) for o in reducible)))
    garments = [o for o in reducible if role_of[o.name] == "cloth"]
    per_garment = human.triangles / max(1, len(garments))
    for o in reducible:
        if "cloth" in human.parts and o in garments:  # kit: each garment its own budget
            target = human.budget.get(human.part_name(stem_of[o.name]), per_garment)
            _decimate(o, min(1.0, target / max(1, _triangles(o))), keep_borders=False)
        elif human.hair_kit and role_of[o.name] == "hair":  # kit: each style its own budget
            target = human.budget.get(human.part_name(stem_of[o.name]), human.triangles)
            _decimate(o, min(1.0, target / max(1, _triangles(o))), keep_borders=False)
        elif role_of[o.name] == "hair":
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
        kit_role: dict[str, str] = {}  # hair kits: part file -> hair | beard
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
        if human.hair_kit:  # parts/<kit>/hair_<style>.glb, beard_<style>.glb
            for o in [o for o in objects if role_of[o.name] in ("hair", "beard")]:
                part = human.part_name(stem_of[o.name])
                kit_role[part] = role_of[o.name]
                o.name = part
                parts[part] = o
        elif hair and "hair" in human.parts:
            parts["hair"] = _join(hair[0], hair[1:])
            parts["hair"].name = "hair"
        if "cloth" in human.parts:
            for o in clothes:
                o.name = human.part_name(stem_of[o.name])
                parts[o.name] = o  # parts/<kit>/<garment>.glb
        # every part carries its LOD levels (§2.2): seams of body and head stay fixed,
        # hair and garments are reduced freely
        for file_name, obj in parts.items():
            role = file_name if file_name in ("body", "head", "hair") else "cloth"
            role = kit_role.get(file_name, role)
            levels = make_lods(obj, role, keep_borders=role in ("body", "head"))
            _export(levels, ref, args.out_dir / f"{file_name}.glb")
            for o in levels:  # garments of a kit all use the node names cloth_lod<n>
                bpy.data.objects.remove(o)


if __name__ == "__main__":
    main()
