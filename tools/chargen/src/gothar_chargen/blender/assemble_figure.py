"""Assembles a figure from its manifest (figures/<name>.figure.toml) and exports <name>.glb.

Run inside Blender 4.5:
    blender --background --factory-startup --python assemble_figure.py -- \
        --manifest <figures/x.figure.toml> --characters <assets/source/characters> --out <x.glb>

Steps: reference armature; import each part (.glb on the reference rig) and bind it to that
armature as one mesh per role (body, head, hair, beard); pull the body's neck ring onto the head's
(bodies and heads of different builds come from the same MPFB topology, so the rings pair up by
angle; the neck and collar below follow smoothly); merge equally named materials – the head's skin
wins, so body and face share one skin texture – and tint them from the palette; build LOD levels
with Decimate (open borders keep weight 0, so seams stay put; morph targets only on lod0); export
with the project glTF settings. Node names follow the LOD contract: <role>_lod<n> (§2.2).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]
from mathutils.kdtree import KDTree  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import delete_objects, import_glb, new_reference  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.figure import Figure, load_figure  # noqa: E402
from gothar_chargen.meshdata import WELD  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402

_DUPLICATE = re.compile(r"^(?P<base>.+)\.\d{3}$")
KEEP_GROUP = "lod_reduce"
NECK_SEARCH = 0.06  # metres: body skin border this close to the head's ring is the neck ring
NECK_FALLOFF = 0.05  # metres below the ring over which the body follows the snap


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="assemble_figure")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--characters", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args(argv)


def _import_part(path: Path, role: str, arm: bpy.types.Object) -> bpy.types.Object:
    if not path.is_file():
        raise SystemExit(f"part '{role}': {path} not found")
    imported = import_glb(path, merge_vertices=True)
    # skinned meshes only: the importer also creates bone display shapes ("Icosphere")
    meshes = [o for o in imported if o.type == "MESH" and o.vertex_groups]
    if not meshes:
        raise SystemExit(f"part '{role}': {path.name} has no mesh")
    for o in meshes:
        bones = {g.name for g in o.vertex_groups}
        unknown = bones - {b.name for b in arm.data.bones}
        if unknown:
            raise SystemExit(
                f"part '{role}': vertex groups not in the reference rig: {sorted(unknown)}"
            )
        o.parent = None
    if len(meshes) > 1:
        with bpy.context.temp_override(
            active_object=meshes[0], selected_editable_objects=meshes, object=meshes[0]
        ):
            bpy.ops.object.join()
    obj = meshes[0]
    _weld(obj)
    obj.name = obj.data.name = f"{role}_lod0"
    obj.matrix_world.identity()
    obj.parent = arm
    for mod in [m for m in obj.modifiers if m.type == "ARMATURE"]:
        obj.modifiers.remove(mod)
    obj.modifiers.new("armature", "ARMATURE").object = arm
    delete_objects([o for o in imported if o is not obj and o.name in bpy.data.objects])
    return obj


def _weld(obj: bpy.types.Object) -> None:
    """Joins vertices at equal positions: glTF splits them at hard edges and UV seams, which
    leaves the surface open for Blender – Decimate would then tear it apart."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=WELD)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def _skin_ring(obj: bpy.types.Object) -> list[int]:
    """Vertices on open borders of the skin faces (other materials: eyes, mouth, clothes)."""
    skin = {
        i for i, s in enumerate(obj.material_slots) if s.material and _base(s.material) == "skin"
    }
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    ring = {
        v.index
        for e in bm.edges
        if e.is_boundary and e.link_faces[0].material_index in skin
        for v in e.verts
    }
    bm.free()
    return sorted(ring)


def _loop_order(obj: bpy.types.Object, ring: list[int]) -> list[int]:
    """The ring's vertices in their order along the border edges (exact, from the topology)."""
    members = set(ring)
    nxt: dict[int, list[int]] = {i: [] for i in ring}
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    for e in bm.edges:
        a, b = e.verts[0].index, e.verts[1].index
        if e.is_boundary and a in members and b in members:
            nxt[a].append(b)
            nxt[b].append(a)
    bm.free()
    order = [ring[0]]
    prev = -1
    while len(order) < len(ring):
        options = [n for n in nxt[order[-1]] if n != prev and n not in order[-2:]]
        if not options:
            raise SystemExit("neck ring is not a single closed loop")
        prev = order[-1]
        order.append(options[0])
    return order


def _snap_neck(body: bpy.types.Object, head: bpy.types.Object) -> None:
    """Pulls the body's neck ring exactly onto the head's (different builds, same topology)."""
    head_ring = _skin_ring(head)
    if not head_ring:
        return
    tree = KDTree(len(head_ring))
    for k, i in enumerate(head_ring):
        tree.insert(head.data.vertices[i].co, k)
    tree.balance()
    body_ring = [
        i for i in _skin_ring(body) if tree.find(body.data.vertices[i].co)[2] < NECK_SEARCH
    ]
    if len(body_ring) != len(head_ring):
        raise SystemExit(
            f"neck rings differ: body {len(body_ring)} vs head {len(head_ring)} vertices "
            "(parts from different MPFB topologies?)"
        )
    a = _loop_order(head, head_ring)
    b = _loop_order(body, body_ring)
    hv, bv = head.data.vertices, body.data.vertices
    n = len(a)

    def cost(order: list[int], s: int) -> float:
        return sum((hv[a[k]].co - bv[order[(k + s) % n]].co).length for k in range(n))

    best = min(((cost(o, s), o, s) for o in (b, b[::-1]) for s in range(n)), key=lambda c: c[0])
    _, order, shift = best
    target = {order[(k + shift) % n]: hv[a[k]].co.copy() for k in range(n)}
    delta = {i: target[i] - bv[i].co for i in target}
    ring_tree = KDTree(n)
    for k, i in enumerate(target):
        ring_tree.insert(bv[i].co, k)
    ring_tree.balance()
    ring_ids = list(target)
    moved = max(d.length for d in delta.values())
    for v in bv:
        if v.index in target:
            continue
        _, k, dist = ring_tree.find(v.co)
        if dist < NECK_FALLOFF:
            v.co += delta[ring_ids[k]] * (1.0 - dist / NECK_FALLOFF)
    for i, co in target.items():
        bv[i].co = co
    body.data.update()
    print(f"[chargen] neck: {n} ring vertices snapped (max {moved * 1000:.1f} mm)")


def _base(mat: bpy.types.Material) -> str:
    m = _DUPLICATE.match(mat.name)
    return m["base"] if m else mat.name


def _merge_materials(objects: list[bpy.types.Object], figure: Figure) -> None:
    """Equally named materials (skin, skin.001 ...) become one; palette colours tint them. The
    head comes first, so its skin texture is used for the body too (one skin per figure)."""
    canonical: dict[str, bpy.types.Material] = {}
    for obj in sorted(objects, key=lambda o: not o.name.startswith("head_")):
        for slot in obj.material_slots:
            mat = slot.material
            if mat is None:
                continue
            base = _base(mat)
            if base not in canonical:
                canonical[base] = mat
                mat.name = base
            slot.material = canonical[base]
    for name, color in figure.palette.items():
        mat = canonical.get(name)
        if mat is None:
            print(f"[chargen] palette: no material '{name}' in this figure")
            continue
        mat.diffuse_color = (*color, 1.0)
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.use_nodes else None
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (*color, 1.0)


FREE_BORDER_ROLES = ("hair",)  # alpha cards: their borders are no seams and may move


def _border_weights(obj: bpy.types.Object) -> None:
    """Vertex group for Decimate: 1 everywhere, 0 on open borders (seams must not move)."""
    border: set[int] = set()
    if obj.name.rsplit("_lod", 1)[0] not in FREE_BORDER_ROLES:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        border = {v.index for e in bm.edges if e.is_boundary for v in e.verts}
        bm.free()
    group = obj.vertex_groups.new(name=KEEP_GROUP)
    inner = [v.index for v in obj.data.vertices if v.index not in border]
    if inner:
        group.add(inner, 1.0, "REPLACE")
    if border:
        group.add(sorted(border), 0.0, "REPLACE")


def _make_lod(obj: bpy.types.Object, level: int, ratio: float) -> bpy.types.Object:
    role = obj.name.rsplit("_lod", 1)[0]
    lod = obj.copy()
    lod.data = obj.data.copy()
    lod.name = lod.data.name = f"{role}_lod{level}"
    bpy.context.scene.collection.objects.link(lod)
    if lod.data.shape_keys is not None:  # morph targets only on lod0 (§2.2)
        lod.shape_key_clear()
    mod = lod.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = ratio
    # low weight = expensive to collapse: borders have weight 0, everything else 1
    mod.vertex_group = KEEP_GROUP
    mod.vertex_group_factor = 1000.0
    mod.use_collapse_triangulate = True
    # the decimate modifier must run before the armature modifier when applied
    with bpy.context.temp_override(object=lod, active_object=lod):
        bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return lod


def _remove_helper_groups(objects: list[bpy.types.Object]) -> None:
    for obj in objects:
        group = obj.vertex_groups.get(KEEP_GROUP)
        if group is not None:
            obj.vertex_groups.remove(group)


def assemble(figure: Figure, characters: Path, out: Path) -> None:
    rig = load_rig()
    arm = new_reference(rig)
    lod0 = [_import_part(path, role, arm) for role, path in figure.part_paths(characters).items()]
    by_role = {o.name.rsplit("_lod", 1)[0]: o for o in lod0}
    if "body" in by_role and "head" in by_role:
        _snap_neck(by_role["body"], by_role["head"])
    _merge_materials(lod0, figure)
    objects = list(lod0)
    for obj in lod0:
        _border_weights(obj)
        for level, ratio in enumerate(figure.lods[1:], start=1):
            objects.append(_make_lod(obj, level, ratio))
    _remove_helper_groups(objects)
    for o in bpy.context.scene.objects:
        o.select_set(o is arm or o in objects)
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=False)
    out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(out.resolve()), **settings)
    tris = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objects}
    print(f"[chargen] wrote {out} {tris}")


def main() -> None:
    args = _parse_args()
    assemble(load_figure(args.manifest), args.characters, args.out)


if __name__ == "__main__":
    main()
