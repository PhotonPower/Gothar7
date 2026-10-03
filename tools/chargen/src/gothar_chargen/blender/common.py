"""Helpers shared by the Blender scripts (import, reference armature, saving). Needs bpy."""

from __future__ import annotations

from pathlib import Path

import bpy  # type: ignore[import-not-found]

from gothar_chargen.blender import build_reference_rig as reference
from gothar_chargen.skeleton import RigSpec

FPS = 30


def import_glb(path: Path, merge_vertices: bool = False) -> list[bpy.types.Object]:
    """Imports a .glb and returns the new objects.

    `merge_vertices` joins the vertices glTF splits at hard edges/UV seams, so the mesh is
    connected again (needed before Decimate, which would otherwise tear it apart).
    """
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path), merge_vertices=merge_vertices)
    return [o for o in bpy.data.objects if o not in before]


def delete_objects(objects: list[bpy.types.Object]) -> None:
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)


def new_reference(rig: RigSpec) -> bpy.types.Object:
    """Empty scene (30 fps) with the reference armature; glTF imports then key at 30 fps."""
    reference._clear_scene()
    bpy.context.scene.render.fps = FPS
    return reference._build_armature(rig)


def purge_unused() -> None:
    for collection in (bpy.data.actions, bpy.data.meshes, bpy.data.materials, bpy.data.armatures):
        for block in list(collection):
            if block.users == 0:
                collection.remove(block)


def save(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path.resolve()), compress=True)
    print(f"[chargen] wrote {path}")
