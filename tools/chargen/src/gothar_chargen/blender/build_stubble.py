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

import bmesh  # type: ignore[import-not-found]
import bpy  # type: ignore[import-not-found]
from mathutils import Vector, kdtree  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.common import import_glb, new_reference  # noqa: E402
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402
from gothar_chargen.stubble import PART, STYLES, TRIANGLES, BeardStyle  # noqa: E402

OFFSET = 0.0008  # m above the skin
SOFT = 2.0  # widths of the soft zone borders (x the base widths below)
UV_TILE = 0.03  # m per texture tile
BANDS = (0.8, 0.55, 0.3)  # zone density limits of the bands 0 | 1 | 2 | 3
CARD_LENGTH = (
    0.016  # m: strands hanging from the border of a full beard (volume, not a painted edge)
)
BULGE = 0.007  # m: extra thickness of a full beard in the middle of its zone
CARD_SHARE = 0.7  # full beards: share of the triangle budget for the shell, the rest for the cards
CARD_V = (0.08, 0.62)  # texture v of a card's root (band 0, dense) and tip (band 2, sparser)
TUFT_LENGTH = (
    0.014,
    0.024,
)  # m: tufts on the beard surface, shortest at the cheeks, longest at the chin
TUFT_WIDTH = 0.006  # m at the root; the tip is ``TUFT_TAPER`` as wide
TUFT_TAPER = 0.5
TUFT_V = (0.3, 0.95)  # texture v of a tuft: root in band 1, tip in the sparse band 3 (wisps)
TUFT_LIFT = 0.42  # outward share of a tuft's direction (0 = lying on the shell, 1 = standing up)
TUFT_SINK = 0.0015  # m: roots below the shell surface, so no tuft starts in the air


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="build_stubble")
    p.add_argument("--head", type=Path, required=True)
    p.add_argument("--texture", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--style", default=PART, choices=sorted(STYLES))
    return p.parse_args(argv)


def _clamp(v: float) -> float:
    return min(max(v, 0.0), 1.0)


class Head:
    """Skin of the head part in world space, its landmarks and the beard zone."""

    def __init__(self, obj: bpy.types.Object, zone: str = "full") -> None:
        self.obj, self.me, self.w = obj, obj.data, obj.matrix_world
        self.mode = zone
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
        if self.mode == "sides":  # mutton chops: cheeks and sideburns, chin and mouth free
            d *= _clamp((ax - 0.032) / (0.01 * SOFT))
        return d

    def face_density(self, poly: bpy.types.MeshPolygon) -> float:
        return sum(self.zone(self.pts[v]) for v in poly.vertices) / len(poly.vertices)

    def nearest(self, p: Vector) -> int:
        return self.tree.find(p)[1]


def _patch(
    head: Head, arm: bpy.types.Object, name: str, offset: float, bulge: float = 0.0
) -> bpy.types.Object:
    """The full shell over the beard zone (before reduction), on the reference armature."""
    faces = [p for p in head.faces if head.face_density(p) > 0.08]
    used = sorted({v for p in faces for v in p.vertices})
    index = {v: k for k, v in enumerate(used)}
    w3 = head.w.to_3x3()

    def lift(p: Vector) -> float:  # full beards bulge in the middle: chin and jaw, not the throat
        return offset + bulge * head.zone(p) * _clamp((p.z - head.chin_z + 0.03) / 0.03)

    verts = [
        head.pts[v] + (w3 @ head.me.vertices[v].normal).normalized() * lift(head.pts[v])
        for v in used
    ]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], [tuple(index[v] for v in p.vertices) for p in faces])
    for poly in mesh.polygons:
        poly.use_smooth = True
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = arm
    obj.modifiers.new("Armature", "ARMATURE").object = arm
    return obj


def _cards(obj: bpy.types.Object, head: Head, budget: int) -> int:
    """Hair cards along the outer border of a full beard: a quad per border edge, hanging down and
    out from the skin (``CARD_LENGTH``), so the beard ends in strands, not in a flat painted edge.
    The lip opening gets none. At most ``budget`` triangles (every k-th edge). Returns the index of
    the first card face (the faces before it are the shell)."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    first = len(bm.faces)
    mouth = head.mouth_z

    def at_mouth(co: Vector) -> bool:
        return abs(co.x) < 0.035 and abs(co.z - mouth) < 0.025 and co.y < head.nose.y + 0.04

    def hangs(e: bmesh.types.BMEdge) -> bool:  # outer border along the jaw and chin, not the throat
        mid = (e.verts[0].co + e.verts[1].co) / 2
        jaw = head.chin_z + 0.005 < mid.z < mouth - 0.012 and abs(mid.x) > 0.025
        return e.is_boundary and not at_mouth(mid) and jaw

    border = [e for e in bm.edges if hangs(e)]
    step = max(1, math.ceil(2 * len(border) / max(budget, 2)))
    down = Vector((0.0, 0.0, -1.0))
    tips: dict[int, bmesh.types.BMVert] = {}

    def tip(v: bmesh.types.BMVert) -> bmesh.types.BMVert:
        if v.index not in tips:
            out = (v.normal * 0.3 + down * 0.95).normalized()
            jitter = 0.8 + 0.4 * ((v.index * 7919) % 101) / 100  # uneven strand lengths
            tips[v.index] = bm.verts.new(v.co + out * CARD_LENGTH * jitter)
        return tips[v.index]

    bm.verts.ensure_lookup_table()
    for e in border[::step]:
        a, b = e.verts
        try:
            bm.faces.new((a, b, tip(b), tip(a)))
        except ValueError:  # face exists already
            continue
    bm.to_mesh(obj.data)
    bm.free()
    for poly in obj.data.polygons[first:]:
        poly.use_smooth = True
    return first


def _tufts(obj: bpy.types.Object, head: Head, budget: int) -> None:
    """Hair cards over the inside of a full beard (volume, F3 2026-11-23): one quad per chosen
    shell face, rooted just under the shell, running with the hair (down along the skin, lifted
    out by ``TUFT_LIFT``), alternately turned by +-40 degrees about that direction, so the beard
    gets a tufted surface and a soft silhouette in profile. Lips and the upper throat get none;
    at most ``budget`` triangles (every k-th face, densest zone first)."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    bm.faces.ensure_lookup_table()
    mouth = head.mouth_z

    def free(c: Vector) -> bool:  # lips and the moustache over them, the throat
        lips = abs(c.x) < 0.04 and mouth - 0.016 < c.z < mouth + 0.02 and c.y < head.nose.y + 0.045
        return not lips and c.z > head.chin_z - 0.012

    faces = [f for f in bm.faces if len(f.verts) <= 4 and free(f.calc_center_median())]
    faces = [f for f in faces if head.zone(f.calc_center_median()) > 0.5]
    faces.sort(key=lambda f: (round(f.calc_center_median().z, 3), f.index))
    count = min(len(faces), max(0, budget // 2))
    if not count:
        bm.free()
        return
    step = len(faces) / count
    down = Vector((0.0, 0.0, -1.0))
    for k in range(count):
        f = faces[int(k * step)]
        c, n = f.calc_center_median(), f.normal.normalized()
        flow = (down - n * down.dot(n)).normalized()
        if flow.length < 0.5:
            continue
        along = (flow * (1 - TUFT_LIFT) + n * TUFT_LIFT).normalized()
        side = flow.cross(n).normalized()
        turn = math.radians(40 if k % 2 else -40)
        side = (side * math.cos(turn) + n * math.sin(turn)).normalized()
        chin = _clamp((mouth - c.z) / 0.04)  # longer towards the chin
        jitter = 0.85 + 0.3 * ((k * 7919) % 101) / 100
        length = (TUFT_LENGTH[0] + (TUFT_LENGTH[1] - TUFT_LENGTH[0]) * chin) * jitter
        root = c - n * TUFT_SINK
        half = side * TUFT_WIDTH / 2
        a, b = bm.verts.new(root - half), bm.verts.new(root + half)
        bm.faces.new(
            (
                a,
                b,
                bm.verts.new(root + half * TUFT_TAPER + along * length),
                bm.verts.new(root - half * TUFT_TAPER + along * length),
            )
        )
    bm.to_mesh(obj.data)
    bm.free()
    for poly in obj.data.polygons:
        poly.use_smooth = True


def _card_uvs(
    obj: bpy.types.Object, first: int, end: int | None = None, vs: tuple[float, float] = CARD_V
) -> None:
    """Cards: u along the border, v from the dense root to the sparser tip (strands run along v)."""
    uv = obj.data.uv_layers.active
    for k, poly in enumerate(obj.data.polygons[first:end]):
        lis = list(poly.loop_indices)  # a, b, tip(b), tip(a)
        width = (
            obj.data.vertices[poly.vertices[1]].co - obj.data.vertices[poly.vertices[0]].co
        ).length
        u0 = (k * 0.37) % 1.0
        u1 = u0 + width / UV_TILE
        for li, (u, v) in zip(
            lis, ((u0, vs[0]), (u1, vs[0]), (u1, vs[1]), (u0, vs[1])), strict=True
        ):
            uv.data[li].uv = (u, v)


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


def _band_level(dens: float, limits: tuple[float, ...]) -> float:
    """Continuous band position 0..3 of a zone density (0 = densest band): the faces at the border
    of a full beard fade through the bands from vertex to vertex instead of stepping per face."""
    edges = [1.0, *limits, 0.0]
    for k in range(len(edges) - 1):
        hi, lo = edges[k], edges[k + 1]
        if dens >= lo:
            return k if k == 0 else k - 1 + (hi - dens) / max(hi - lo, 1e-6)
    return float(len(limits))


def _band_uvs(
    obj: bpy.types.Object, head: Head, limits: tuple[float, ...] = BANDS, smooth: bool = False
) -> None:
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
        band = next((k for k, limit in enumerate(limits) if dens > limit), len(limits))
        lis = list(poly.loop_indices)
        pts = [uv.data[li].uv * scale for li in lis]
        u0, v0 = min(p.x for p in pts), min(p.y for p in pts)
        for li, p in zip(lis, pts, strict=True):
            du, dv = p.x - u0, p.y - v0  # faces are far smaller than a tile
            if smooth:  # per vertex: soft fade at the border, one even band inside
                vco = obj.data.vertices[obj.data.loops[li].vertex_index].co
                level = min(_band_level(head.zone(vco), limits), 2.99)
                uv.data[li].uv = ((u0 % 1.0) + du, (level + 0.01 * (1 + min(dv, 1.0))) / 4)
            else:
                uv.data[li].uv = ((u0 % 1.0) + du, (band + 0.01 + 0.98 * min(dv, 1.0)) / 4)


def _material(texture: Path, part: str) -> bpy.types.Material:
    mat = bpy.data.materials.new(part)  # role "beard" by its first word (postprocess.py)
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
    style: BeardStyle = STYLES[args.style]
    lod0 = next(o for o in imported if o.type == "MESH" and o.name.endswith("_lod0"))
    head = Head(lod0, style.zone)
    print(f"[chargen] {args.head.parent.name}: nose at {tuple(round(v, 3) for v in head.nose)}")
    mat = _material(args.texture, style.part)
    levels = []
    full = style.limits != BANDS  # dense beards: soft border bands and hair cards
    for level, triangles in enumerate(TRIANGLES):
        obj = _patch(head, arm, f"beard_lod{level}", style.offset, BULGE if full else 0.0)
        obj.data.name = obj.name
        _reduce(obj, int(triangles * CARD_SHARE) if full else triangles)
        shell_tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        first = len(obj.data.polygons)
        if full:  # hair cards: the hanging border first, then tufts over the beard (volume)
            _cards(obj, head, (triangles - shell_tris) // 3)
            used = sum(len(p.vertices) - 2 for p in obj.data.polygons)
            tufts = len(obj.data.polygons)
            _tufts(obj, head, triangles - used)
        owners = [head.nearest(v.co) for v in obj.data.vertices]
        _weights(obj, head, owners)
        if level == 0:  # face morphs only on lod0 (§2.2)
            _morphs(obj, head, owners, list(rig.morph_targets))
        _band_uvs(obj, head, style.limits, smooth=full)
        if full:
            _card_uvs(obj, first, tufts)
            _card_uvs(obj, tufts, vs=TUFT_V)
        obj.data.materials.append(mat)
        levels.append(obj)
    for o in imported:
        bpy.data.objects.remove(o, do_unlink=True)
    _export(levels, arm, args.out)
    tris = "/".join(str(sum(len(p.vertices) - 2 for p in o.data.polygons)) for o in levels)
    print(f"[chargen] wrote {args.out} ({tris} tris per LOD)")


if __name__ == "__main__":
    main()
