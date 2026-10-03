"""Adapts one of the project owner's models (Claude Design) by script and exports it (W6).

    blender --background --factory-startup --python build_owner_model.py -- <job.json>

The job comes from ``gothar_worldgen.owner_models.prepare_job`` (usually through
``gothar-worldgen garten <site>`` or ``kirche``). Starting from the unchanged source .glb:

1. removes nodes under the ``remove`` prefixes (with their children);
2. decimates the meshes under the ``decimate`` prefixes (collapse ratio; shared meshes once);
   under ``thinFaces`` prefixes keeps only faces of at least that share of the largest face
   (slats: front and back stay, the narrow sides go);
3. lays the model onto a sloping ground: ``shear`` (dy/dx, dy/dz in model space) for every vertex,
   except nodes under ``rigid`` prefixes, which move up as a whole (pavilions stay level);
4. maps materials onto the palette where the job says so, keeps the owner's others;
5. joins everything into one object, adds the ``COL_HULL_*`` bodies of the job and exports.
"""

import fnmatch
import json
import math
import sys
from collections.abc import Iterator
from pathlib import Path

import bmesh
import bpy


def to_blender(p: list[float]) -> tuple[float, float, float]:
    """glTF / worldgen (x, y up, z south) -> Blender (x, y north, z up)."""
    x, y, z = p
    return (x, -z, y)


def base_name(name: str) -> str:
    head, _, tail = name.rpartition(".")
    return head if head and tail.isdigit() else name


def ancestors(obj: bpy.types.Object) -> Iterator[bpy.types.Object]:
    while obj is not None:
        yield obj
        obj = obj.parent


def name_matches(name: str, pattern: str) -> bool:
    return fnmatch.fnmatchcase(name, pattern) if "*" in pattern else name.startswith(pattern)


def rigid_root(obj: bpy.types.Object, prefixes: list[str]) -> bpy.types.Object | None:
    """The outermost ancestor under a ``rigid`` prefix (one pavilion), or None."""
    found = None
    for o in ancestors(obj):
        if any(name_matches(base_name(o.name), p) for p in prefixes):
            found = o
    return found


def rule(obj: bpy.types.Object, prefixes: list[str]) -> str | None:
    for o in ancestors(obj):
        for p in prefixes:
            if name_matches(base_name(o.name), p):
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


def weld(mesh: bpy.types.Mesh) -> None:
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(mesh)
    bm.free()


def main() -> None:
    job = json.loads(Path(sys.argv[sys.argv.index("--") + 1]).read_text(encoding="utf-8"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=job["source"])

    # 1. remove
    for o in [o for o in bpy.data.objects if rule(o, job["remove"])]:
        bpy.data.objects.remove(o, do_unlink=True)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]

    # 2. decimate: once per shared mesh, then every instance gets the result
    users: dict[str, list[bpy.types.Object]] = {}
    for o in meshes:
        users.setdefault(o.data.name, []).append(o)
    for objs in users.values():
        key = next((k for k in (rule(o, list(job["decimate"])) for o in objs) if k), None)
        if key is None:
            continue
        data = objs[0].data.copy()
        weld(data)
        tmp = bpy.data.objects.new("decimate_tmp", data)
        bpy.context.scene.collection.objects.link(tmp)
        mod = tmp.modifiers.new("decimate", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = float(job["decimate"][key])
        with bpy.context.temp_override(active_object=tmp, object=tmp):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        for o in objs:
            o.data = tmp.data
        bpy.data.objects.remove(tmp, do_unlink=True)

    # 2b. thin parts (slats): only their two large faces stay, the narrow ones are dropped
    for objs in users.values():
        key = next((k for k in (rule(o, list(job["thinFaces"])) for o in objs) if k), None)
        if key is None:
            continue
        data = objs[0].data.copy()
        bm = bmesh.new()
        bm.from_mesh(data)
        largest = max((f.calc_area() for f in bm.faces), default=0.0)
        small = [f for f in bm.faces if f.calc_area() < float(job["thinFaces"][key]) * largest]
        bmesh.ops.delete(bm, geom=small, context="FACES")
        bm.to_mesh(data)
        bm.free()
        for o in objs:
            o.data = data

    # 3. own copies in world space, shear onto the sloping ground (Blender: z up, y = -z_gltf);
    # rigid groups (pavilions) move up as a whole by the shear at the centre of all their parts
    a, b = (float(v) for v in job["shear"])
    roots = {o.name: rigid_root(o, job["rigid"]) for o in meshes}
    for o in meshes:
        mw = o.matrix_world.copy()
        o.data = o.data.copy()
        o.parent = None
        o.data.transform(mw)
        o.matrix_world.identity()
        if a == 0.0 and b == 0.0:
            continue
        if roots[o.name] is None:
            for v in o.data.vertices:
                v.co.z += a * v.co.x + b * (-v.co.y)
    groups: dict[str, list[bpy.types.Object]] = {}
    for o in meshes:
        if roots[o.name] is not None:
            groups.setdefault(roots[o.name].name, []).append(o)
    for objs in groups.values():
        vs = [v.co for o in objs for v in o.data.vertices]
        cx = sum(v.x for v in vs) / len(vs)
        cy = sum(v.y for v in vs) / len(vs)
        dz = a * cx + b * (-cy)
        for o in objs:
            for v in o.data.vertices:
                v.co.z += dz
    for o in [o for o in bpy.data.objects if o.type != "MESH"]:
        bpy.data.objects.remove(o, do_unlink=True)

    # 4. materials
    for o in meshes:
        for slot in o.material_slots:
            if slot.material is not None and base_name(slot.material.name) in job["materials"]:
                name, rgba = job["materials"][base_name(slot.material.name)]
                slot.material = palette_material(name, rgba)

    # 5. one object, collision, export
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    model = bpy.context.view_layer.objects.active
    model.name = job["key"]
    with bpy.context.temp_override(active_object=model, object=model, selected_objects=[model]):
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(job["smoothDeg"]))
        bpy.ops.object.material_slot_remove_unused()
    for name, pts, tris in job["collision"]:
        cm = bpy.data.meshes.new(name)
        cm.from_pydata([to_blender(p) for p in pts], [], tris)
        cm.update()
        col = bpy.data.objects.new(name, cm)
        col.display_type = "WIRE"
        col.hide_render = True
        bpy.context.scene.collection.objects.link(col)

    out = Path(job["outGlb"])
    out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(out), export_format="GLB", export_yup=True, export_apply=True,
        export_animations=False, export_materials="EXPORT", use_selection=False,
    )  # fmt: skip
    if job["outBlend"]:
        Path(job["outBlend"]).parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=job["outBlend"], compress=True)
    tris = sum(len(p.vertices) - 2 for p in model.data.polygons)
    mats = ",".join(sorted(base_name(s.material.name) for s in model.material_slots if s.material))
    print(f"OWNER_OK key={job['key']} triangles={tris} bodies={len(job['collision'])} "
          f"materials={mats}")  # fmt: skip


main()
bpy.ops.wm.quit_blender()
