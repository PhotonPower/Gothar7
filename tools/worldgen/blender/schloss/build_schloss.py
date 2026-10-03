"""Builds the Leonberg castle in Blender and exports it (W6). The only source of the model.

    blender --background --factory-startup --python build_schloss.py -- \\
        <schloss.json> <building_rules.json> <out.glb> [<out.blend>]

Usually run through ``gothar-worldgen schloss <site>``. The geometry comes from
``schloss_geometry.py`` (named parts, parameters from schloss.json). Here it becomes one mesh
object with palette materials plus wire-frame ``COL_HULL_*`` objects (collision contract,
docs/modules/asset.md); the .glb goes next to the model data, the .blend is a reproducible
by-product for inspection (not versioned). Corrections are made in the script, never by hand.
"""

import json
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schloss_geometry as geometry  # noqa: E402


def to_blender(p: tuple[float, float, float]) -> tuple[float, float, float]:
    """glTF / worldgen (x, y up, z south) -> Blender (x, y north, z up)."""
    x, y, z = p
    return (x, -z, y)


def material(name: str, rgba: list[float]) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = [float(c) for c in rgba]
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.9
    return mat


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1 :]
    spec = json.loads(Path(args[0]).read_text(encoding="utf-8"))
    palette = json.loads(Path(args[1]).read_text(encoding="utf-8"))["palette"]
    out_glb = Path(args[2])
    out_blend = Path(args[3]) if len(args) > 3 else None
    bpy.ops.wm.read_factory_settings(use_empty=True)

    model = geometry.to_local(geometry.build(spec), geometry.origin_of(spec))
    mesh = bpy.data.meshes.new("schloss")
    verts: list[tuple[float, float, float]] = []
    faces: list[list[int]] = []
    slots: list[int] = []
    for slot, name in enumerate(sorted(model.faces)):
        mesh.materials.append(material(name, palette[name]))
        for face in model.faces[name]:
            base = len(verts)
            verts += [to_blender(p) for p in face]
            faces.append(list(range(base, base + len(face))))
            slots.append(slot)
    mesh.from_pydata(verts, [], faces)
    mesh.polygons.foreach_set("material_index", slots)
    mesh.update()
    obj = bpy.data.objects.new("schloss", mesh)
    bpy.context.scene.collection.objects.link(obj)
    for name, pts, tris in model.collision:
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
    print(f"SCHLOSS_OK triangles={model.triangles()} bodies={len(model.collision)}")


main()
bpy.ops.wm.quit_blender()
