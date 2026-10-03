"""LOD levels of a part (contract §2.2): copies named <role>_lod<n>, reduced with Decimate.
Needs bpy.

Seams (open borders of body and head) keep their vertices in every level, so parts still fit
together at any distance; loose parts (hair cards, garments) are reduced freely. Morph targets
stay on lod0 only.
"""

from __future__ import annotations

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]

KEEP_GROUP = "lod_reduce"
DEFAULT_RATIOS = (1.0, 0.5, 0.2)


def _border_weights(obj: bpy.types.Object, keep_borders: bool) -> None:
    """Vertex group for Decimate: 1 everywhere, 0 on open borders (they must not move)."""
    border: set[int] = set()
    if keep_borders:
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


def make_lods(
    obj: bpy.types.Object,
    role: str,
    ratios: tuple[float, ...] = DEFAULT_RATIOS,
    keep_borders: bool = True,
) -> list[bpy.types.Object]:
    """Renames `obj` to <role>_lod0 and adds reduced copies; returns all levels."""
    obj.name = obj.data.name = f"{role}_lod0"
    _border_weights(obj, keep_borders)
    levels = [obj]
    for level, ratio in enumerate(ratios[1:], start=1):
        lod = obj.copy()
        lod.data = obj.data.copy()
        lod.name = lod.data.name = f"{role}_lod{level}"
        for collection in obj.users_collection:
            collection.objects.link(lod)
        if lod.data.shape_keys is not None:  # morph targets only on lod0 (§2.2)
            lod.shape_key_clear()
        mod = lod.modifiers.new("decimate", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = ratio
        mod.vertex_group = KEEP_GROUP  # low weight = expensive to collapse: borders stay
        mod.vertex_group_factor = 1000.0
        mod.use_collapse_triangulate = True
        with bpy.context.temp_override(object=lod, active_object=lod):
            bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
            bpy.ops.object.modifier_apply(modifier=mod.name)
        levels.append(lod)
    for o in levels:
        group = o.vertex_groups.get(KEEP_GROUP)
        if group is not None:
            o.vertex_groups.remove(group)
    return levels
