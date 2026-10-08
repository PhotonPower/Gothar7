"""Stubble beard of one head part (own work, F3): parts/hair_m_<head>/beard_stubble.glb.

Run inside Blender 4.5:
    blender --background --factory-startup --python build_stubble.py --
        --head parts/head_m_<x>/head.glb --texture textures/hair/beard_stubble_neutral.png
        --out parts/hair_m_<x>/beard_stubble.glb

The beard zone is found from the head's skin (nose tip -> mouth, chin): below the nose down to the
upper throat, back to the sideburns, the lips free, with soft borders. A shell 0.8 mm above the
skin covers the zone; each LOD level (gothar_chargen.stubble.TRIANGLES) is reduced from the full
patch and then gets its UVs: every face in the texture band of its zone density (four bands of
decreasing hair density, stubble.py), so the stubble thins out towards the edge. Weights and the
face morphs (lod0 only, contract order) follow the nearest head skin vertex, so the stubble moves
with the jaw (vis_aa). Everything sits on the reference armature like the other parts.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]
from mathutils import Vector, kdtree  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import import_glb, new_reference  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402
from gothar_chargen.stubble import PART, TRIANGLES  # noqa: E402

OFFSET = 0.0008  # m above the skin
SOFT = 2.0  # widths of the soft zone borders (x the base widths below)
UV_TILE = 0.03  # m per texture tile
BANDS = (0.8, 0.55, 0.3)  # zone density limits of the bands 0 | 1 | 2 | 3


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="build_stubble")
    p.add_argument("--head", type=Path, required=True)
    p.add_argument("--texture", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    return p.parse_args(argv)


def _clamp(v: float) -> float:
    return min(max(v, 0.0), 1.0)


class Head:
    """Skin of the head part in world space, its landmarks and the beard zone."""

    def __init__(self, obj: bpy.types.Object) -> None:
        self.obj, self.me, self.w = obj, obj.data, obj.matrix_world
        skin = self.me.materials.find("skin")
        if skin < 0:
            raise RuntimeError(f"{obj.name}: no skin material")
        self.faces = [p for p in self.me.polygons if p.material_index == skin]
        self.pts = {i: self.w @ v.co for i, v in enumerate(self.me.vertices)}
        used = sorted({v for p in self.faces for v in p.vertices})
        self.zmin = min(self.pts[i].z for i in used)
        centre = [self.pts[i] for i in used if abs(self.pts[i].x) < 0.006]
        centre = [p for p in centre if self.zmin + 0.06 < p.z < self.zmin + 0.13]
        self.nose = min(centre, key=lambda p: p.y)  # facing -Y
        self.mouth_z, self.chin_z = self.nose.z - 0.029, self.nose.z - 0.066
        self.tree = kdtree.KDTree(len(used))
        for i in used:
            self.tree.insert(self.pts[i], i)
        self.tree.balance()

    def zone(self, p: Vector) -> float:
        """0..1 stubble density at a point near the skin."""
        n, ax = self.nose, abs(p.x)
        side = _clamp((ax - 0.02) / 0.06)
        top = n.z - 0.010 + 0.032 * side  # under the nose, at the sides up to the ear lobe
        back = 0.004 + 0.02 * (1 - side)
        d = _clamp((top - p.z) / (0.014 * SOFT)) * _clamp((back - p.y) / (0.012 * SOFT))
        d *= _clamp((p.z - (self.zmin + 0.012)) / (0.012 * SOFT))
        if p.z < self.chin_z - 0.01 and p.y > -0.03:  # throat: only its upper part
            d *= _clamp((p.z - (self.zmin + 0.03)) / (0.01 * SOFT))
        if ax < 0.028 and p.y < n.y + 0.03:  # the lips stay free
            d *= _clamp((abs(p.z - (self.mouth_z - 0.002)) - 0.006) / (0.004 * SOFT))
        return d

    def face_density(self, poly: bpy.types.MeshPolygon) -> float:
        return sum(self.zone(self.pts[v]) for v in poly.vertices) / len(poly.vertices)

    def nearest(self, p: Vector) -> int:
        return self.tree.find(p)[1]


def _patch(head: Head, arm: bpy.types.Object, name: str) -> bpy.types.Object:
    """The full shell over the beard zone (before reduction), on the reference armature."""
    faces = [p for p in head.faces if head.face_density(p) > 0.08]
    used = sorted({v for p in faces for v in p.vertices})
    index = {v: k for k, v in enumerate(used)}
    w3 = head.w.to_3x3()
    verts = [head.pts[v] + (w3 @ head.me.vertices[v].normal).normalized() * OFFSET for v in used]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], [tuple(index[v] for v in p.vertices) for p in faces])
    for poly in mesh.polygons:
        poly.use_smooth = True
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = arm
    obj.modifiers.new("Armature", "ARMATURE").object = arm
    return obj


def _reduce(obj: bpy.types.Object, triangles: int) -> None:
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if tris <= triangles:
        return
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = triangles / tris
    mod.use_collapse_triangulate = True
    with bpy.context.temp_override(object=obj, active_object=obj):
        bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
        bpy.ops.object.modifier_apply(modifier=mod.name)


def _weights(obj: bpy.types.Object, head: Head, owners: list[int]) -> None:
    src = head.obj
    for g in src.vertex_groups:
        obj.vertex_groups.new(name=g.name)
    for vi, owner in enumerate(owners):
        for ge in head.me.vertices[owner].groups:
            obj.vertex_groups[src.vertex_groups[ge.group].name].add([vi], ge.weight, "REPLACE")


def _morphs(obj: bpy.types.Object, head: Head, owners: list[int], names: list[str]) -> None:
    """All face morphs in contract order; each vertex moves like its owning skin vertex."""
    keys = head.me.shape_keys.key_blocks if head.me.shape_keys else {}
    basis = keys[0] if keys else None
    obj.shape_key_add(name="Basis", from_mix=False)
    for name in names:
        key = obj.shape_key_add(name=name, from_mix=False)
        src = keys.get(name) if keys else None
        if src is None or basis is None:
            continue
        w3 = head.w.to_3x3()
        for vi, owner in enumerate(owners):
            key.data[vi].co = obj.data.vertices[vi].co + w3 @ (
                src.data[owner].co - basis.data[owner].co
            )


def _band_uvs(obj: bpy.types.Object, head: Head) -> None:
    """Even texel density (smart unwrap, UV_TILE m per tile), then every face moved into the
    texture band of its zone density; the hairs are noise, so the jumps between faces don't show."""
    obj.data.uv_layers.new(name="UVMap")
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.0, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    uv = obj.data.uv_layers.active
    area3d = sum(p.area for p in obj.data.polygons)
    area_uv = 0.0
    for poly in obj.data.polygons:
        co = [uv.data[li].uv for li in poly.loop_indices]
        area_uv += 0.5 * abs(
            sum(co[i].x * co[i - 1].y - co[i - 1].x * co[i].y for i in range(len(co)))
        )
    scale = math.sqrt(area3d / max(area_uv, 1e-12)) / UV_TILE
    for poly in obj.data.polygons:
        centre = sum((obj.data.vertices[v].co for v in poly.vertices), Vector()) / len(
            poly.vertices
        )
        dens = head.zone(centre)
        band = next((k for k, limit in enumerate(BANDS) if dens > limit), len(BANDS))
        lis = list(poly.loop_indices)
        pts = [uv.data[li].uv * scale for li in lis]
        u0, v0 = min(p.x for p in pts), min(p.y for p in pts)
        for li, p in zip(lis, pts, strict=True):
            du, dv = p.x - u0, p.y - v0  # faces are far smaller than a tile
            uv.data[li].uv = ((u0 % 1.0) + du, (band + 0.01 + 0.98 * min(dv, 1.0)) / 4)


def _material(texture: Path) -> bpy.types.Material:
    mat = bpy.data.materials.new(PART)  # role "beard" by its first word (postprocess.py)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    img = bpy.data.images.load(str(texture))
    img.name = f"hair/{texture.stem}"  # externalize_images -> textures/hair/<stem>.png
    img.alpha_mode = "STRAIGHT"
    img.pack()
    tex.image = img
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    bsdf.inputs["Roughness"].default_value = 0.85
    return mat


def _export(objs: list[bpy.types.Object], arm: bpy.types.Object, path: Path) -> None:
    for o in bpy.context.scene.objects:
        o.hide_set(o.type == "MESH" and o not in objs)
        o.select_set(o in objs or o is arm)
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=False)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), **settings)


def main() -> None:
    args = _parse_args()
    rig = load_rig()
    arm = new_reference(rig)
    imported = import_glb(args.head)
    head = Head(next(o for o in imported if o.type == "MESH" and o.name.endswith("_lod0")))
    print(f"[chargen] {args.head.parent.name}: nose at {tuple(round(v, 3) for v in head.nose)}")
    mat = _material(args.texture)
    levels = []
    for level, triangles in enumerate(TRIANGLES):
        obj = _patch(head, arm, f"beard_lod{level}")
        obj.data.name = obj.name
        _reduce(obj, triangles)
        owners = [head.nearest(v.co) for v in obj.data.vertices]
        _weights(obj, head, owners)
        if level == 0:  # face morphs only on lod0 (§2.2)
            _morphs(obj, head, owners, list(rig.morph_targets))
        _band_uvs(obj, head)
        obj.data.materials.append(mat)
        levels.append(obj)
    for o in imported:
        bpy.data.objects.remove(o, do_unlink=True)
    _export(levels, arm, args.out)
    tris = "/".join(str(sum(len(p.vertices) - 2 for p in o.data.polygons)) for o in levels)
    print(f"[chargen] wrote {args.out} ({tris} tris per LOD)")


if __name__ == "__main__":
    main()
