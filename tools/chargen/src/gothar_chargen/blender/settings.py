"""glTF export settings for all character assets (rigs, figures, animation sets).

Documented in docs/design/characters-pipeline.md ("glTF-Export"). Keep both in sync.
Plain data without Blender imports so tests and docs tooling can read it.
"""

from __future__ import annotations

from typing import Any

# Keyword arguments for bpy.ops.export_scene.gltf (Blender 4.5 LTS).
GLTF_EXPORT_SETTINGS: dict[str, Any] = {
    "export_format": "GLB",
    "export_yup": True,  # Blender Z-up/-Y-forward -> glTF Y-up/+Z-forward
    "export_apply": False,  # never apply modifiers: would destroy skin and shape keys
    "use_selection": False,
    "export_cameras": False,
    "export_lights": False,
    "export_extras": False,
    # geometry
    "export_texcoords": True,
    "export_normals": True,
    "export_tangents": False,
    "export_materials": "EXPORT",
    "export_image_format": "AUTO",
    # skinning
    "export_skins": True,
    "export_def_bones": False,  # keep sockets (non-deform bones) in the skeleton
    "export_influence_nb": 4,  # max. bone influences per vertex (animation.md)
    "export_all_influences": False,
    "export_rest_position_armature": True,  # nodes carry the bind pose
    "export_hierarchy_flatten_bones": False,
    # morph targets (faces)
    "export_morph": True,
    "export_morph_normal": True,
    "export_morph_tangent": False,
    # animation: one glTF animation per Blender action
    "export_animations": True,
    "export_animation_mode": "ACTIONS",
    "export_force_sampling": True,
    "export_frame_step": 1,
    "export_reset_pose_bones": True,
    "export_optimize_animation_size": False,
}
