"""Adapts the project owner's market fountain to Leonberg around 1700 and exports it (W6).

    blender --background --factory-startup --python build_marktbrunnen.py -- \\
        <marktbrunnen.json> <building_rules.json> <out.glb> [<out.blend>]

Usually run through ``gothar-worldgen marktbrunnen <site>``. Starts from the owner's model
(``source`` in marktbrunnen.json, never changed) and, all by script:

1. removes the modern planting and the old fine pipes (``remove``, by node name prefix);
2. reduces the triangles of the figure, column and volutes (``decimate``: collapse ratio);
3. maps materials onto the palette where one fits (``materials``), keeps the others by name;
4. joins everything into one mesh, adds simple spouts with water jets and the ``COL_HULL_*``
   bodies from ``marktbrunnen_geometry.py``.

The .glb goes to handmade/marktbrunnen/, the .blend is a by-product for inspection (not versioned).
"""

import json
import math
import sys
from collections.abc import Iterator
from pathlib import Path

import bmesh
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import marktbrunnen_geometry as geometry  # noqa: E402


def to_blender(p: tuple[float, float, float]) -> tuple[float, float, float]:
    """glTF / worldgen (x, y up, z south) -> Blender (x, y north, z up)."""
    x, y, z = p
    return (x, -z, y)


def base_name(obj: bpy.types.Object) -> str:
    """Node name without Blender's .001 suffixes."""
    name = obj.name
    head, _, tail = name.rpartition(".")
    return head if head and tail.isdigit() else name


def with_ancestors(obj: bpy.types.Object) -> Iterator[bpy.types.Object]:
    while obj is not None:
        yield obj
        obj = obj.parent


def matches(obj: bpy.types.Object, prefixes: list[str]) -> str | None:
    for o in with_ancestors(obj):
        for p in prefixes:
            if base_name(o).startswith(p):
                return p
    return None


def palette_material(name: str, rgba: list[float]) -> bpy.types.Material:
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = [float(c) for c in rgba]
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.9
    bsdf.inputs["Alpha"].default_value = 1.0
    return mat


def triangles(obj: bpy.types.Object) -> int:
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def add_faces(
    name: str, faces: dict[str, list], materials: dict[str, bpy.types.Material]
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(name)
    verts, polys, slots, order = [], [], [], sorted(faces)
    for slot, mat in enumerate(order):
        mesh.materials.append(materials[mat])
        for poly in faces[mat]:
            base = len(verts)
            verts += [to_blender(p) for p in poly]
            polys.append(list(range(base, base + len(poly))))
            slots.append(slot)
    mesh.from_pydata(verts, [], polys)
    mesh.polygons.foreach_set("material_index", slots)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1 :]
    spec_path = Path(args[0])
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    palette = json.loads(Path(args[1]).read_text(encoding="utf-8"))["palette"]
    out_glb = Path(args[2])
    out_blend = Path(args[3]) if len(args) > 3 else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(spec_path.parent / spec["source"]))

    # 1. remove (with every child)
    doomed = {o for o in bpy.data.objects if matches(o, spec.get("remove", []))}
    for o in doomed:
        bpy.data.objects.remove(o, do_unlink=True)

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    # glTF exports split vertices per face; weld first so the collapse can work, then smooth by
    # angle so flat panels stay crisp.
    for o in meshes:
        if o.data.users > 1:  # instanced parts (the eight trough sides) get their own copy
            o.data = o.data.copy()
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bm.to_mesh(o.data)
        bm.free()

    # 2. decimate
    rules = spec.get("decimate", {})
    for o in meshes:
        key = matches(o, list(rules))
        if key is None:
            continue
        mod = o.modifiers.new("decimate", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = float(rules[key])
        with bpy.context.temp_override(active_object=o, object=o):
            bpy.ops.object.modifier_apply(modifier=mod.name)

    # 3. materials: palette where mapped, everything else keeps the owner's material
    mapping = spec.get("materials", {})
    for o in meshes:
        for slot in o.material_slots:
            if slot.material is None:
                continue
            name = base_name(slot.material)
            if name in mapping:
                slot.material = palette_material(mapping[name], palette[mapping[name]])

    # 4. join into one object in world space, add spouts
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    for o in [o for o in bpy.data.objects if o.type != "MESH"]:
        bpy.data.objects.remove(o, do_unlink=True)
    owner = {base_name(m): m for m in bpy.data.materials}
    jet = geometry.spouts(spec)
    for mat in jet.faces:
        if mat not in owner:
            raise SystemExit(f"material {mat} not in the source model")
    meshes.append(add_faces("spouts", jet.faces, owner))
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    model = bpy.context.view_layer.objects.active
    model.name = "marktbrunnen"
    with bpy.context.temp_override(active_object=model, object=model, selected_objects=[model]):
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(float(spec.get("smoothDeg", 35))))
    # drop materials no longer used (merged palette slots)
    with bpy.context.temp_override(active_object=model, object=model):
        bpy.ops.object.material_slot_remove_unused()

    for name, pts, tris in geometry.collision(spec):
        cm = bpy.data.meshes.new(name)
        cm.from_pydata([to_blender(p) for p in pts], [], tris)
        cm.update()
        col = bpy.data.objects.new(name, cm)
        col.display_type = "WIRE"
        col.hide_render = True
        bpy.context.scene.collection.objects.link(col)

    out_glb.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(out_glb), export_format="GLB", export_yup=True, export_apply=True,
        export_animations=False, export_materials="EXPORT", use_selection=False,
    )  # fmt: skip
    if out_blend is not None:
        out_blend.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(out_blend), compress=True)
    mats = ",".join(sorted(base_name(s.material) for s in model.material_slots if s.material))
    print(f"MARKTBRUNNEN_OK triangles={triangles(model)} bodies={len(geometry.collision(spec))} "
          f"materials={mats}")  # fmt: skip


main()
bpy.ops.wm.quit_blender()
