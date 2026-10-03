"""Blender add-on "Gothar Buildings": hand work on generated building meshes (W3/W5).

Sidebar (N) > Gothar:
- "Gebäude importieren": loads the ``.glb`` of the given building ids (or all within a radius of the
  3D cursor) from a ``buildings_index.json`` and places them at their world position.
- "Auswahl zurückschreiben": exports each selected building back to its ``.glb`` (origin and axes as
  generated) and sets ``locked: true`` in its override, so new generator runs keep the hand work.

The geometry generator itself runs outside Blender (``gothar-worldgen buildings``).
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path

import bpy
from bpy.props import FloatProperty, StringProperty

bl_info = {
    "name": "Gothar Buildings",
    "author": "Gothar",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > Gothar",
    "description": "Hand work on generated Gothar building meshes",
    "category": "Import-Export",
}

_here = str(Path(__file__).resolve().parent)
if _here not in sys.path:
    sys.path.insert(0, _here)
core = importlib.import_module("core")


def _site(context: bpy.types.Context) -> core.Site:
    path = bpy.path.abspath(context.scene.gothar_index)
    if not path or not Path(path).is_file():
        raise FileNotFoundError("buildings_index.json not set or not found")
    return core.Site(Path(path))


def import_building(site: core.Site, building_id: str) -> bpy.types.Object:
    entry = site.entries[building_id]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(site.mesh_file(building_id)))
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if not new:
        raise RuntimeError(f"{building_id}: nothing imported")
    obj = new[0]
    obj.name = f"BLD_{building_id}"
    obj.location = core.gltf_to_blender(*entry["pos"])
    obj["gothar_id"] = building_id
    obj["gothar_index"] = str(site.index_path)
    return obj


def export_building(obj: bpy.types.Object, site: core.Site) -> Path:
    building_id = obj["gothar_id"]
    target = site.mesh_file(building_id)
    saved = obj.location.copy()
    selection = list(bpy.context.selected_objects)
    try:
        obj.location = (0.0, 0.0, 0.0)  # the vob places the mesh; the file keeps its own origin
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.ops.export_scene.gltf(
            filepath=str(target), export_format="GLB", use_selection=True, export_yup=True,
            export_apply=True, export_animations=False,
        )  # fmt: skip
    finally:
        obj.location = saved
        for o in selection:
            o.select_set(True)
    core.mark_locked(site.overrides, building_id, note="von Hand in Blender bearbeitet")
    return target


class GOTHAR_OT_import(bpy.types.Operator):  # noqa: N801 - Blender naming convention
    bl_idname = "gothar.import_buildings"
    bl_label = "Gebäude importieren"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context: bpy.types.Context) -> set[str]:
        try:
            site = _site(context)
            ids = [i.strip() for i in context.scene.gothar_ids.split(",") if i.strip()]
            if not ids:
                c = context.scene.cursor.location
                x, _, z = core.blender_to_gltf(c.x, c.y, c.z)
                ids = site.nearby(x, z, context.scene.gothar_radius)
            for bid in ids:
                import_building(site, bid)
        except (OSError, KeyError, RuntimeError, ValueError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, f"{len(ids)} Gebäude importiert")
        return {"FINISHED"}


class GOTHAR_OT_export(bpy.types.Operator):  # noqa: N801 - Blender naming convention
    bl_idname = "gothar.export_buildings"
    bl_label = "Auswahl zurückschreiben"

    def execute(self, context: bpy.types.Context) -> set[str]:
        objs = [o for o in context.selected_objects if "gothar_id" in o]
        if not objs:
            self.report({"WARNING"}, "keine Gothar-Gebäude ausgewählt")
            return {"CANCELLED"}
        try:
            for obj in objs:
                export_building(obj, core.Site(Path(obj["gothar_index"])))
        except (OSError, KeyError, ValueError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, f"{len(objs)} Gebäude geschrieben und gesperrt (locked)")
        return {"FINISHED"}


class GOTHAR_PT_panel(bpy.types.Panel):  # noqa: N801 - Blender naming convention
    bl_label = "Gothar Buildings"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Gothar"

    def draw(self, context: bpy.types.Context) -> None:
        col = self.layout.column()
        col.prop(context.scene, "gothar_index")
        col.prop(context.scene, "gothar_ids")
        col.prop(context.scene, "gothar_radius")
        col.operator(GOTHAR_OT_import.bl_idname)
        col.operator(GOTHAR_OT_export.bl_idname)


_classes = (GOTHAR_OT_import, GOTHAR_OT_export, GOTHAR_PT_panel)


def register() -> None:
    bpy.types.Scene.gothar_index = StringProperty(name="Index", subtype="FILE_PATH",
                                                  description="buildings_index.json")  # fmt: skip
    bpy.types.Scene.gothar_ids = StringProperty(
        name="IDs", description="comma-separated; empty = near the cursor"
    )
    bpy.types.Scene.gothar_radius = FloatProperty(
        name="Radius", default=25.0, min=1.0, unit="LENGTH"
    )
    for cls in _classes:
        bpy.utils.register_class(cls)


def unregister() -> None:
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.gothar_index
    del bpy.types.Scene.gothar_ids
    del bpy.types.Scene.gothar_radius
