"""Assembles a figure from its manifest (figures/<name>.figure.toml) and exports <name>.glb.

Run inside Blender 4.5:
    blender --background --factory-startup --python assemble_figure.py -- \
        --manifest <figures/x.figure.toml> --characters <assets/source/characters> --out <x.glb>

Steps: reference armature; import each part (.glb on the reference rig) and bind it to that
armature as one mesh per role (body, head, hair, beard); merge equally named materials and tint
them from the palette; build LOD levels with Decimate (open borders keep weight 0, so seams stay
put; morph targets only on lod0); export with the project glTF settings. Node names follow the LOD
contract: <role>_lod<n> (characters-pipeline.md §2.2).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import delete_objects, import_glb, new_reference  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.figure import Figure, load_figure  # noqa: E402
from gothar_chargen.meshdata import WELD  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402

_DUPLICATE = re.compile(r"^(?P<base>.+)\.\d{3}$")
KEEP_GROUP = "lod_reduce"


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


def _merge_materials(objects: list[bpy.types.Object], figure: Figure) -> None:
    """Equally named materials (skin, skin.001 ...) become one; palette colours tint them."""
    canonical: dict[str, bpy.types.Material] = {}
    for obj in objects:
        for slot in obj.material_slots:
            mat = slot.material
            if mat is None:
                continue
            m = _DUPLICATE.match(mat.name)
            base = m["base"] if m else mat.name
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


def _border_weights(obj: bpy.types.Object) -> None:
    """Vertex group for Decimate: 1 everywhere, 0 on open borders (seams must not move)."""
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
