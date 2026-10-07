"""Mob models: chest, anvil, bed, door (M8, contract characters-pipeline.md §3.1, mobs.toml v1).

Built by script like the houses, in the style of leonberg-stil.md (oak, forged iron, wool and
straw), with the textures of ``textures.procedural``; the files are not tied to a place:
``assets/source/mobs/<type>.glb`` (and the props in ``assets/source/props/``) with their images in
one shared folder, ``assets/source/furniture/textures/`` (engine: ``../`` in image URIs is fine).

Axes (contract): Y up, origin on the ground, the front faces +Z, metres. The figure stands in front
(slots in mobs.toml, maintained by figuren) and looks towards -Z.

- chest 0.9 x 0.6 x 0.6 m, front at z = +0.3, body 0.5 m high; the lid is the node ``MOB_LID``
  with its pivot at the rear hinge (z = -0.3, y = 0.5), turned about its X axis by the engine; its
  collision body is a child of ``MOB_LID`` and turns with it;
- anvil: wooden block and forged anvil, working surface 0.8 m, horn towards +X;
- bed 2.0 x 0.9 m along X (long side towards +Z), lying surface 0.45 m, head board at -X;
- door: the blade only, 1.0 x 2.0 x 0.05 m along +X from the hinge at the origin, handle at 1.0 m
  on both sides; the engine turns the whole mob about +Y;
- bench (M9): plank bench 1.5 x 0.35 m, seat 0.45 m, origin in the middle on the ground;
- table (W7): trestle table, top 1.6 x 0.8 m at 0.75 m, origin in the middle on the ground,
  seats along both long sides.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

from gothar_worldgen.buildings.gltf import CollisionPart, Part, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import _Builder
from gothar_worldgen.textures.procedural import TILE_M, albedo_scale, make, to_png

TYPES = ("chest", "anvil", "bed", "door", "bench", "table")
TEXTURE_SIZE = 512
TEXTURE_DIR = "../furniture/textures"  # shared by mobs and props, relative to the models
TEXTURE_FOLDER = ("furniture", "textures")  # where they are written, beside mobs/ and props/
BUDGET = 1500  # triangles per model (render)
# material -> (texture kind, linear colour); the colour is the tint like the palette of the houses
MATERIALS: dict[str, tuple[str, tuple[float, float, float]]] = {
    "oak": ("boards", (0.085, 0.052, 0.03)),  # old oak, darkened
    "oak_beam": ("timber", (0.075, 0.047, 0.026)),
    "iron": ("iron", (0.05, 0.05, 0.055)),
    "straw": ("straw", (0.42, 0.33, 0.15)),
    "wool": ("cloth", (0.13, 0.075, 0.05)),  # undyed brown wool
    "linen": ("cloth", (0.5, 0.45, 0.36)),
    "fieldstone": ("stone", (0.24, 0.22, 0.2)),  # hearth (props)
    "ash": ("stone", (0.04, 0.035, 0.03)),
    "ember": ("stone", (0.25, 0.05, 0.01)),  # glowing (EMISSIVE)
    "flame": ("straw", (0.9, 0.45, 0.1)),
    # household (W7 B, shared palette of mobs and props)
    "clay": ("clay", (0.42, 0.22, 0.12)),  # terracotta
    "clay_glaze": ("clay", (0.16, 0.11, 0.07)),  # brown glaze
    "meat": ("meat", (0.3, 0.1, 0.06)),
    "leather": ("cloth", (0.085, 0.05, 0.03)),
    "herb": ("straw", (0.15, 0.2, 0.07)),
    "water": ("stone", (0.015, 0.02, 0.025)),
    "sackcloth": ("cloth", (0.2, 0.15, 0.09)),  # coarse jute
    "pine": ("boards", (0.22, 0.15, 0.08)),  # lighter wood: wall boards (what hangs on it shows)
    "wax": ("clay", (0.6, 0.55, 0.42)),  # tallow candles
    "fur_light": ("fur", (0.5, 0.45, 0.36)),  # sheepskin
    "fur_dark": ("fur", (0.14, 0.09, 0.06)),  # a hide
    "wool_red": ("cloth", (0.32, 0.06, 0.04)),  # rugs and hangings
    "wool_blue": ("cloth", (0.06, 0.09, 0.22)),
    "wool_green": ("cloth", (0.07, 0.15, 0.06)),
    "wool_ochre": ("cloth", (0.36, 0.24, 0.07)),
    # lanes and yards (W6 streets): vegetation.py and the street props
    "bark": ("bark", (0.11, 0.085, 0.065)),
    "leaves_linden": ("leaves", (0.04, 0.075, 0.022)),
    "leaves_oak": ("leaves", (0.032, 0.058, 0.018)),
    "leaves_fruit": ("leaves", (0.05, 0.09, 0.026)),
    "leaves_box": ("leaves", (0.022, 0.05, 0.016)),
    "grass": ("leaves", (0.07, 0.11, 0.032)),
    "grass_dry": ("leaves", (0.14, 0.13, 0.055)),
    "dung": ("straw", (0.09, 0.065, 0.04)),
}
# materials that glow: the engine adds emissive after the light (render.md "Material")
EMISSIVE = {"ember": (0.9, 0.28, 0.05), "flame": (1.0, 0.55, 0.15)}
# the texture kind's grain runs along u (timber, straw) or v (boards); boxes map their grain axis so
GRAIN_U = {"timber", "straw", "iron", "cloth"}
Vec3 = tuple[float, float, float]


@dataclass
class Mesh:
    """Builders per material plus collision boxes, all in one frame."""

    builders: dict[str, _Builder] = field(default_factory=dict)
    collision: list[CollisionPart] = field(default_factory=list)

    def b(self, material: str) -> _Builder:
        if material not in self.builders:
            self.builders[material] = _Builder((0.0, 0.0, 0.0))
        return self.builders[material]

    def box(self, material: str, lo: Vec3, hi: Vec3, grain: int = 0) -> None:
        """Axis-aligned box; ``grain``: the axis (0 x, 1 y, 2 z) along which the texture runs."""
        _box(self.b(material), lo, hi, grain, MATERIALS[material][0] in GRAIN_U)

    def prism(self, material: str, centre: tuple[float, float], radius: float, y0: float,
              y1: float, sides: int = 8) -> None:  # fmt: skip
        """Upright prism (a wooden block) with ``sides`` faces, grain upwards."""
        b = self.b(material)
        ring = [(centre[0] + radius * math.cos(2 * math.pi * (k + 0.5) / sides),
                 centre[1] + radius * math.sin(2 * math.pi * (k + 0.5) / sides))
                for k in range(sides)]  # fmt: skip
        u = 0.0
        for k in range(sides):
            (xa, za), (xb, zb) = ring[k], ring[(k + 1) % sides]
            w = math.dist((xa, za), (xb, zb))
            mid = ((xa + xb) / 2 - centre[0], 0.0, (za + zb) / 2 - centre[1])
            pts = [(xa, y0, za), (xb, y0, zb), (xb, y1, zb), (xa, y1, za)]
            b.polygon(pts, [(y0, u), (y0, u + w), (y1, u + w), (y1, u)], mid)
            u += w
        for y, n in ((y1, 1.0), (y0, -1.0)):
            for k in range(1, sides - 1):
                tri = [ring[0], ring[k], ring[k + 1]]
                b.polygon([(x, y, z) for x, z in tri], [(x, z) for x, z in tri], (0.0, n, 0.0))

    def cyl(self, material: str, axis: int, centre: Vec3, radius: float, length: float,
            sides: int = 10) -> None:  # fmt: skip
        """Prism with ``sides`` faces along ``axis`` (0 x, 1 y, 2 z), ``centre`` in its middle;
        both ends closed. Barrels, jugs, sausages."""
        b = self.b(material)
        others = [k for k in range(3) if k != axis]

        def at(t: float, ang: float) -> Vec3:
            p = list(centre)
            p[axis] += t
            p[others[0]] += radius * math.cos(ang)
            p[others[1]] += radius * math.sin(ang)
            return (p[0], p[1], p[2])

        angs = [2 * math.pi * (k + 0.5) / sides for k in range(sides)]
        h = length / 2
        u = 0.0
        w = 2 * radius * math.sin(math.pi / sides)
        for k in range(sides):
            a0, a1 = angs[k], angs[(k + 1) % sides]
            mid = [0.0, 0.0, 0.0]
            mid[others[0]] = math.cos((a0 + a1) / 2 + (math.pi if k == sides - 1 else 0.0))
            mid[others[1]] = math.sin((a0 + a1) / 2 + (math.pi if k == sides - 1 else 0.0))
            pts = [at(-h, a0), at(-h, a1), at(h, a1), at(h, a0)]
            b.polygon(pts, [(-h, u), (-h, u + w), (h, u + w), (h, u)], (mid[0], mid[1], mid[2]))
            u += w
        for t, n in ((h, 1.0), (-h, -1.0)):
            nv = [0.0, 0.0, 0.0]
            nv[axis] = n
            for k in range(1, sides - 1):
                tri = [at(t, angs[0]), at(t, angs[k]), at(t, angs[k + 1])]
                uvs = [(p[others[0]], p[others[1]]) for p in tri]
                b.polygon(tri, uvs, (nv[0], nv[1], nv[2]))

    def body(self, name: str, lo: Vec3, hi: Vec3) -> None:
        self.collision.append(box_body(f"COL_HULL_{name}", lo, hi))

    def primitives(self) -> list[Primitive]:
        out = []
        for material in MATERIALS:  # fixed order: deterministic files
            b = self.builders.get(material)
            if b is None or not b.idx:
                continue
            kind, colour = MATERIALS[material]
            mesh = b.mesh()
            tu, tv = TILE_M[kind]
            mesh.uvs = (mesh.uvs / np.array([tu, -tv], dtype=np.float32)).astype(np.float32)
            scale = albedo_scale(kind)
            out.append(Primitive(
                material, (*(min(c / scale, 1.0) for c in colour), 1.0), mesh,  # type: ignore[arg-type]
                (f"{TEXTURE_DIR}/{kind}_albedo.png", f"{TEXTURE_DIR}/{kind}_normal.png"),
                EMISSIVE.get(material),
            ))  # fmt: skip
        return out

    def triangles(self) -> int:
        return sum(len(b.idx) // 3 for b in self.builders.values())


def _box(b: _Builder, lo: Vec3, hi: Vec3, grain: int, grain_u: bool) -> None:
    corners = [(lo[0], hi[0]), (lo[1], hi[1]), (lo[2], hi[2])]
    for axis in range(3):
        a, c = [k for k in range(3) if k != axis]
        # the texture's grain direction follows the box's grain axis where the face has it
        if (grain == a) != grain_u:
            a, c = c, a
        for side, value in ((-1.0, corners[axis][0]), (1.0, corners[axis][1])):
            pts = []
            uvs = []
            for ua, uc in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = [0.0, 0.0, 0.0]
                p[axis] = value
                p[a] = corners[a][ua]
                p[c] = corners[c][uc]
                pts.append((p[0], p[1], p[2]))
                uvs.append((p[a], p[c]))
            want = [0.0, 0.0, 0.0]
            want[axis] = side
            b.polygon(pts, uvs, (want[0], want[1], want[2]))


def box_body(name: str, lo: Vec3, hi: Vec3) -> CollisionPart:
    """Closed box, outward triangles."""
    xs, ys, zs = (lo[0], hi[0]), (lo[1], hi[1]), (lo[2], hi[2])
    pts = np.array([(x, y, z) for x in xs for y in ys for z in zs], dtype=np.float64)
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    centre = pts.mean(axis=0)
    tris: list[int] = []
    for q in quads:
        for t in ((q[0], q[1], q[2]), (q[0], q[2], q[3])):
            pa, pb, pc = pts[list(t)]
            if np.dot(np.cross(pb - pa, pc - pa), (pa + pb + pc) / 3 - centre) < 0:
                t = (t[0], t[2], t[1])
            tris += t
    return CollisionPart(name, pts.astype(np.float32), np.asarray(tris, dtype=np.uint32))


@dataclass
class MobModel:
    name: str
    main: Mesh
    parts: list[tuple[str, Vec3, Mesh]] = field(default_factory=list)  # (node, pivot, mesh)
    lods: list[Mesh] = field(default_factory=list)  # coarser levels: nodes <name>_lod1, _lod2

    def triangles(self) -> int:
        """Of the finest level (what the budget counts)."""
        return self.main.triangles() + sum(m.triangles() for _, _, m in self.parts)

    def meshes(self) -> list[Mesh]:
        return [self.main, *(m for _, _, m in self.parts), *self.lods]

    def glb(self) -> bytes:
        parts = [Part(n, pivot, m.primitives(), m.collision) for n, pivot, m in self.parts]
        parts += [
            Part(f"{self.name}_lod{k + 1}", (0.0, 0.0, 0.0), m.primitives())
            for k, m in enumerate(self.lods)
        ]  # asset.md "Detailstufen": same origin
        return glb_bytes_multi(self.main.primitives(), self.name, self.main.collision, parts)


# --- the four mobs ---------------------------------------------------------------------------

CHEST_W, CHEST_D, CHEST_BODY_H, CHEST_H = 0.9, 0.6, 0.5, 0.6
LID_PIVOT: Vec3 = (0.0, CHEST_BODY_H, -CHEST_D / 2)


def chest() -> MobModel:
    m = Mesh()
    hw, hd = CHEST_W / 2, CHEST_D / 2
    for x in (-0.36, 0.36):  # runners
        m.box("oak_beam", (x - 0.04, 0.0, -hd), (x + 0.04, 0.05, hd), grain=2)
    m.box("oak", (-hw, 0.05, -hd), (hw, CHEST_BODY_H, hd), grain=0)
    for x in (-0.3, 0.3):  # iron bands round the body (front, back, both proud by 6 mm)
        m.box("iron", (x - 0.025, 0.05, hd), (x + 0.025, CHEST_BODY_H, hd + 0.006), grain=1)
        m.box("iron", (x - 0.025, 0.05, -hd - 0.006), (x + 0.025, CHEST_BODY_H, -hd), grain=1)
    m.box("iron", (-0.06, 0.36, hd), (0.06, CHEST_BODY_H - 0.01, hd + 0.012))  # lock plate
    for x in (-hw, hw):  # iron angles on the four upright edges
        sx = 1.0 if x > 0 else -1.0
        for z in (-hd, hd):
            sz = 1.0 if z > 0 else -1.0
            xs = sorted((x, x + sx * 0.006))
            zs = sorted((z - sz * 0.07, z))
            m.box("iron", (xs[0], 0.05, zs[0]), (xs[1], CHEST_BODY_H, zs[1]), grain=1)
            xs = sorted((x - sx * 0.07, x))
            zs = sorted((z, z + sz * 0.006))
            m.box("iron", (xs[0], 0.05, zs[0]), (xs[1], CHEST_BODY_H, zs[1]), grain=1)
    for side in (-1.0, 1.0):  # handles
        x0, x1 = (hw, hw + 0.03) if side > 0 else (-hw - 0.03, -hw)
        m.box("iron", (x0, 0.33, -0.08), (x1, 0.36, 0.08), grain=2)
    m.body("chest", (-hw, 0.0, -hd), (hw, CHEST_BODY_H, hd))
    lid = Mesh()  # in the hinge's frame: z from the hinge forward, y up from the hinge
    lid_d = CHEST_D + 0.02
    lid.box("oak", (-hw - 0.01, 0.0, 0.0), (hw + 0.01, CHEST_H - CHEST_BODY_H, lid_d), grain=0)
    for x in (-0.3, 0.3):
        lid.box("iron", (x - 0.025, CHEST_H - CHEST_BODY_H, 0.0),
                (x + 0.025, CHEST_H - CHEST_BODY_H + 0.006, lid_d), grain=2)  # fmt: skip
        lid.box("iron", (x - 0.025, 0.0, lid_d), (x + 0.025, CHEST_H - CHEST_BODY_H, lid_d + 0.006),
                grain=1)  # fmt: skip
    lid.box("iron", (-0.05, -0.08, lid_d), (0.05, 0.03, lid_d + 0.012))  # hasp over the lock
    lid.body("lid", (-hw - 0.01, 0.0, 0.0), (hw + 0.01, CHEST_H - CHEST_BODY_H, lid_d))
    return MobModel("chest", m, [("MOB_LID", LID_PIVOT, lid)])


ANVIL_TOP = 0.8


def anvil() -> MobModel:
    m = Mesh()
    m.prism("oak_beam", (0.0, 0.0), 0.24, 0.0, 0.5, sides=8)  # the block
    m.box("iron", (-0.17, 0.5, -0.12), (0.17, 0.58, 0.12), grain=0)  # foot
    m.box("iron", (-0.1, 0.58, -0.07), (0.1, 0.7, 0.07), grain=1)  # waist
    m.box("iron", (-0.24, 0.7, -0.075), (0.2, ANVIL_TOP, 0.075), grain=0)  # face
    # horn towards +X: tapering to a point at the face's height
    b = m.b("iron")
    x0, x1, y0, y1, hz = 0.2, 0.42, 0.72, ANVIL_TOP, 0.06
    tip = (x1, (y0 + y1) / 2 + 0.02, 0.0)
    base = [(x0, y0, -hz), (x0, y0, hz), (x0, y1, hz), (x0, y1, -hz)]
    for i in range(4):
        a, c = base[i], base[(i + 1) % 4]
        mid = ((a[0] + c[0]) / 2 + 0.05 - x0, (a[1] + c[1]) / 2 - (y0 + y1) / 2,
               (a[2] + c[2]) / 2)  # fmt: skip
        b.polygon([a, c, tip], [(a[2], a[1]), (c[2], c[1]), (tip[2] + 0.2, tip[1])], mid)
    m.body("block", (-0.24, 0.0, -0.24), (0.24, 0.5, 0.24))
    m.body("anvil", (-0.24, 0.5, -0.12), (0.42, ANVIL_TOP, 0.12))
    return MobModel("anvil", m)


BED_L, BED_W, BED_LIE = 2.0, 0.9, 0.45


def bed() -> MobModel:
    m = Mesh()
    hl, hw = BED_L / 2, BED_W / 2
    post = 0.08
    for x, top in ((-hl, 0.95), (hl - post, 0.62)):  # head posts at -X, foot posts at +X
        for z in (-hw, hw - post):
            m.box("oak_beam", (x, 0.0, z), (x + post, top, z + post), grain=1)
    for z in (-hw, hw - 0.05):  # side rails
        m.box("oak_beam", (-hl + post, 0.22, z), (hl - post, 0.34, z + 0.05), grain=0)
    m.box("oak", (-hl + 0.02, 0.4, -hw + 0.02), (-hl + post - 0.01, 0.9, hw - 0.02), grain=1)
    m.box("oak", (hl - post + 0.01, 0.4, -hw + 0.02), (hl - 0.02, 0.6, hw - 0.02), grain=1)
    m.box("straw", (-hl + post, 0.3, -hw + 0.04), (hl - post, BED_LIE, hw - 0.04), grain=0)
    # blanket over the foot end, pillow at the head
    m.box(
        "wool", (-0.35, BED_LIE - 0.06, -hw + 0.02), (hl - post - 0.02, BED_LIE + 0.02, hw - 0.02)
    )
    m.box(
        "linen", (-hl + post + 0.03, BED_LIE, -0.28), (-hl + post + 0.38, BED_LIE + 0.09, 0.28), 2
    )
    m.body("bed", (-hl, 0.0, -hw), (hl, BED_LIE, hw))
    m.body("head", (-hl, BED_LIE, -hw), (-hl + post, 0.95, hw))
    return MobModel("bed", m)


DOOR_W, DOOR_H, DOOR_T, HANDLE_Y = 1.0, 2.0, 0.05, 1.0


def door() -> MobModel:
    m = Mesh()
    ht = DOOR_T / 2
    m.box("oak", (0.0, 0.0, -ht), (DOOR_W, DOOR_H, ht), grain=1)  # vertical boards
    for y in (0.3, 1.6):  # battens on the back
        m.box("oak_beam", (0.05, y, -ht - 0.03), (DOOR_W - 0.05, y + 0.12, -ht), grain=0)
    for y in (0.35, 1.65):  # strap hinges on the front, from the hinge side
        m.box("iron", (0.0, y - 0.025, ht), (0.62, y + 0.025, ht + 0.006), grain=0)
    hx = DOOR_W - 0.1
    for z0, z1 in ((ht, ht + 0.012), (-ht - 0.012, -ht)):  # handle plates, both sides
        m.box("iron", (hx - 0.03, HANDLE_Y - 0.08, z0), (hx + 0.03, HANDLE_Y + 0.08, z1))
    for z0, z1 in ((ht + 0.012, ht + 0.06), (-ht - 0.06, -ht - 0.012)):  # levers, both sides
        m.box("iron", (hx - 0.12, HANDLE_Y - 0.012, z0), (hx + 0.012, HANDLE_Y + 0.012, z1))
    m.body("door", (0.0, 0.0, -ht), (DOOR_W, DOOR_H, ht))
    return MobModel("door", m)


BENCH_L, BENCH_D, BENCH_SEAT = 1.5, 0.35, 0.45


def bench() -> MobModel:
    """Plank bench: thick seat on two slab legs with a stretcher, no back (M9, test sizes from
    figuren: seat 0.45 m, depth 0.35 m, length 1.5 m)."""
    m = Mesh()
    hl, hd = BENCH_L / 2, BENCH_D / 2
    seat_t = 0.06
    m.box("oak", (-hl, BENCH_SEAT - seat_t, -hd), (hl, BENCH_SEAT, hd), grain=0)
    for x in (-hl + 0.12, hl - 0.18):  # slab legs, slightly inset
        m.box("oak_beam", (x, 0.0, -hd + 0.03), (x + 0.06, BENCH_SEAT - seat_t, hd - 0.03), grain=1)
    m.box("oak_beam", (-hl + 0.18, 0.12, -0.03), (hl - 0.18, 0.18, 0.03), grain=0)  # stretcher
    m.body("bench", (-hl, 0.0, -hd), (hl, BENCH_SEAT, hd))
    return MobModel("bench", m)


TABLE_L, TABLE_D, TABLE_H = 1.6, 0.8, 0.75
TABLE_TOP_M = 0.06


def table() -> MobModel:
    """Trestle table for 2-4 (W7, mob type ``table`` of engine): top 1.6 x 0.8 m at 0.75 m on two
    trestles with a stretcher; seats along both long sides (+Z and -Z), the benches stand
    alongside (seat 0.45 m, its middle about 0.62 m from the table's axis)."""
    m = Mesh()
    hl, hd = TABLE_L / 2, TABLE_D / 2
    m.box("oak", (-hl, TABLE_H - TABLE_TOP_M, -hd), (hl, TABLE_H, hd), grain=0)
    for x in (-hl + 0.18, hl - 0.26):  # trestles: a slab leg with a foot across
        m.box("oak_beam", (x, 0.08, -0.06), (x + 0.08, TABLE_H - TABLE_TOP_M, 0.06), grain=1)
        m.box("oak_beam", (x - 0.04, 0.0, -hd + 0.08), (x + 0.12, 0.08, hd - 0.08), grain=0)
    m.box("oak_beam", (-hl + 0.26, 0.3, -0.04), (hl - 0.26, 0.38, 0.04), grain=0)  # stretcher
    m.body("table", (-hl, 0.0, -hd), (hl, TABLE_H, hd))
    return MobModel("table", m)


BUILDERS = {"chest": chest, "anvil": anvil, "bed": bed, "door": door, "bench": bench,
            "table": table}  # fmt: skip


HEARTH_W, HEARTH_D, HEARTH_H = 1.2, 0.9, 0.45  # raised hearth: along the wall, depth, height
HOOD_Y0, HOOD_Y1 = 1.9, 2.5  # smoke hood (above head height: no collision there)


def hearth() -> MobModel:
    """Raised hearth against a wall (W7 rooms, a prop, not a mob): a block of field stones with a
    back wall, logs on glowing embers with flames (emissive), a smoke hood above; origin in the
    middle on the floor, the open front +Z, the wall at -Z."""
    m = Mesh()
    hw, hd = HEARTH_W / 2, HEARTH_D / 2
    m.box("fieldstone", (-hw, 0.0, -hd), (hw, HEARTH_H, hd))  # the block
    m.box("fieldstone", (-hw, HEARTH_H, -hd), (hw, HOOD_Y0, -hd + 0.12))  # back wall
    for x in (-hw, hw - 0.1):  # side cheeks round the fire
        m.box("fieldstone", (x, HEARTH_H, -hd + 0.12), (x + 0.1, HEARTH_H + 0.25, hd - 0.1))
    m.box("ash", (-hw + 0.12, HEARTH_H, -hd + 0.14), (hw - 0.12, HEARTH_H + 0.02, hd - 0.12))
    m.box("ember", (-0.3, HEARTH_H + 0.02, -0.18), (0.3, HEARTH_H + 0.06, 0.18))
    m.box("oak_beam", (-0.32, HEARTH_H + 0.05, -0.06), (0.32, HEARTH_H + 0.14, 0.04), grain=0)
    m.box("oak_beam", (-0.05, HEARTH_H + 0.05, -0.28), (0.05, HEARTH_H + 0.13, 0.22), grain=1)
    for x, z, w in ((-0.12, 0.02, 0.1), (0.1, -0.04, 0.12), (0.0, 0.08, 0.08)):  # flames
        h = 0.32 if w > 0.09 else 0.24
        m.box(
            "flame",
            (x - w / 2, HEARTH_H + 0.08, z - 0.01),
            (x + w / 2, HEARTH_H + 0.08 + h, z + 0.01),
        )
        m.box(
            "flame",
            (x - 0.01, HEARTH_H + 0.08, z - w / 2),
            (x + 0.01, HEARTH_H + 0.08 + h * 0.8, z + w / 2),
        )
    # the hood: three tiers narrowing towards the wall and up
    for k, (y0, y1, wf, df) in enumerate(((HOOD_Y0, 2.1, 1.0, 1.0), (2.1, 2.3, 0.8, 0.75),
                                          (2.3, HOOD_Y1, 0.55, 0.5))):  # fmt: skip
        w = hw * wf + 0.05 * (k == 0)
        m.box("fieldstone", (-w, y0, -hd), (w, y1, -hd + HEARTH_D * df))
    m.body("hearth", (-hw, 0.0, -hd), (hw, HEARTH_H, hd))
    return MobModel("hearth", m)


# --- household (W7 B): props against the walls; origin on the floor in the middle, front +Z, wall
# at -Z (hanging ones: origin at the hook, hanging down) -------------------------------------
BARREL_R, BARREL_H = 0.3, 0.9


def _barrel(
    m: Mesh, axis: int, centre: Vec3, r: float = BARREL_R, length: float = BARREL_H,
    sides: int = 12, hoops: int = 4,
) -> None:  # fmt: skip
    """A barrel along ``axis``: bulging staves, iron hoops near each end (``hoops`` 4 or 2)."""
    for t, rr, ln in ((0.0, r, length * 0.66), (length * 0.415, r * 0.9, length * 0.17),
                      (-length * 0.415, r * 0.9, length * 0.17)):  # fmt: skip
        c = list(centre)
        c[axis] += t
        m.cyl("oak", axis, (c[0], c[1], c[2]), rr, ln, sides=sides)
    at = (length * 0.3, -length * 0.3, length * 0.45, -length * 0.45)
    for t in at if hoops == 4 else at[:2]:
        c = list(centre)
        c[axis] += t
        rr = r * (1.01 if abs(t) < length * 0.4 else 0.92)
        m.cyl("iron", axis, (c[0], c[1], c[2]), rr, 0.035, sides=sides)


def barrel() -> MobModel:
    m = Mesh()
    _barrel(m, 1, (0.0, BARREL_H / 2, 0.0))
    m.body("barrel", (-BARREL_R, 0.0, -BARREL_R), (BARREL_R, BARREL_H, BARREL_R))
    lod1 = Mesh()  # the lanes have hundreds: a plain prism from afar
    lod1.cyl("oak", 1, (0.0, BARREL_H / 2, 0.0), BARREL_R * 0.95, BARREL_H, sides=7)
    return MobModel("barrel", m, lods=[lod1])


def barrel_stack() -> MobModel:
    """Five barrels lying in a pyramid (three below, two on top) on two beams, ends to +Z."""
    m = Mesh()
    lod1 = Mesh()
    r, length, beam = 0.3, 0.8, 0.06
    for z in (-0.25, 0.25):
        m.box("oak_beam", (-0.95, 0.0, z - 0.04), (0.95, beam, z + 0.04), grain=0)
    lod1.box("oak_beam", (-0.95, 0.0, -0.29), (0.95, beam, 0.29), grain=0)
    low = beam + r
    for x, y in ((-0.62, low), (0.0, low), (0.62, low), (-0.31, low + 0.535), (0.31, low + 0.535)):
        _barrel(m, 2, (x, y, 0.0), r=r, length=length, sides=10, hoops=2)
        lod1.cyl("oak", 2, (x, y, 0.0), r * 0.95, length, sides=6)
    top = low + 0.535 + r
    m.body("barrels", (-0.93, 0.0, -0.4), (0.93, low + r * 0.5, 0.4))
    m.body("barrels_top", (-0.62, low + r * 0.5, -0.4), (0.62, top, 0.4))
    return MobModel("barrel_stack", m, lods=[lod1])


RACK_W, RACK_D, RACK_H = 1.6, 0.85, 1.15


def barrel_rack() -> MobModel:
    """The tavern's tap: a trestle with two barrels lying front to back, taps at the front."""
    m = Mesh()
    hw, hd, top = RACK_W / 2, RACK_D / 2, 0.42
    for x in (-hw + 0.05, hw - 0.05, 0.0):  # three trestles
        m.box("oak_beam", (x - 0.05, 0.0, -hd), (x + 0.05, top, -hd + 0.1), grain=1)
        m.box("oak_beam", (x - 0.05, 0.0, hd - 0.1), (x + 0.05, top, hd), grain=1)
        m.box("oak_beam", (x - 0.06, top - 0.08, -hd), (x + 0.06, top, hd), grain=2)
    r = (RACK_H - top) / 2
    for x in (-hw / 2, hw / 2):
        _barrel(m, 2, (x, top + r, -0.02), r=r, length=RACK_D - 0.04)
        y = top + r * 0.6
        m.box("oak_beam", (x - 0.02, y, hd - 0.02), (x + 0.02, y + 0.03, hd + 0.08), grain=2)  # tap
        m.box("iron", (x - 0.015, top + r * 0.45, hd + 0.05), (x + 0.015, y, hd + 0.08))
    m.body("rack", (-hw, 0.0, -hd), (hw, RACK_H, hd))
    return MobModel("barrel_rack", m)


def _ware(m: Mesh, kind: str, at: Vec3) -> None:
    """Earthenware standing at ``at`` (its foot): jug, mug, bowl, plate stack."""
    x, y, z = at
    if kind == "jug":
        m.cyl("clay", 1, (x, y + 0.1, z), 0.075, 0.2, sides=8)
        m.cyl("clay", 1, (x, y + 0.23, z), 0.045, 0.06, sides=8)
        m.box("clay", (x + 0.07, y + 0.08, z - 0.01), (x + 0.1, y + 0.2, z + 0.01))  # handle
    elif kind == "mug":
        m.cyl("clay_glaze", 1, (x, y + 0.06, z), 0.045, 0.12, sides=8)
    elif kind == "bowl":
        m.cyl("clay_glaze", 1, (x, y + 0.02, z), 0.06, 0.04, sides=8)
        m.cyl("clay_glaze", 1, (x, y + 0.055, z), 0.1, 0.03, sides=8)
    else:  # plates, stacked
        m.cyl("clay", 1, (x, y + 0.03, z), 0.12, 0.06, sides=10)


COUNTER_W, COUNTER_D, COUNTER_H = 1.8, 0.6, 1.0


def counter() -> MobModel:
    """Shop or bar counter: boarded front (+Z, the customers' side), a thick top, jugs on it."""
    m = Mesh()
    hw, hd = COUNTER_W / 2, COUNTER_D / 2
    m.box("oak", (-hw + 0.03, 0.0, hd - 0.06), (hw - 0.03, COUNTER_H - 0.06, hd - 0.02), grain=1)
    for x in (-hw + 0.03, hw - 0.09):  # sides
        m.box("oak", (x, 0.0, -hd + 0.05), (x + 0.06, COUNTER_H - 0.06, hd - 0.02), grain=1)
    m.box("oak", (-hw + 0.09, 0.45, -hd + 0.08), (hw - 0.09, 0.48, hd - 0.06), grain=0)  # shelf
    m.box("oak_beam", (-hw, COUNTER_H - 0.06, -hd), (hw, COUNTER_H, hd + 0.04), grain=0)
    for x, kind in ((-0.6, "jug"), (-0.42, "mug"), (0.5, "bowl")):
        _ware(m, kind, (x, COUNTER_H, 0.0))
    m.body("counter", (-hw, 0.0, -hd), (hw, COUNTER_H, hd + 0.04))
    return MobModel("counter", m)


SHELF_W, SHELF_D, SHELF_H = 1.2, 0.35, 1.8
SHELF_BOARDS = (0.12, 0.6, 1.08, 1.56)


def shelf() -> MobModel:
    """Open shelf against a wall with jugs, mugs, bowls and plates."""
    m = Mesh()
    hw, hd = SHELF_W / 2, SHELF_D / 2
    for x in (-hw, hw - 0.04):
        m.box("oak", (x, 0.0, -hd), (x + 0.04, SHELF_H, hd), grain=1)
    for y in SHELF_BOARDS:
        m.box("oak", (-hw + 0.04, y - 0.025, -hd), (hw - 0.04, y, hd), grain=0)
    rows = (("jug", -0.35), ("plates", 0.0), ("jug", 0.32), ("mug", 0.12), ("bowl", -0.15),
            ("mug", 0.42), ("plates", -0.38), ("bowl", 0.25), ("jug", -0.05))  # fmt: skip
    for k, (kind, x) in enumerate(rows):
        _ware(m, kind, (x, SHELF_BOARDS[1 + k % 3], 0.0))
    m.body("shelf", (-hw, 0.0, -hd), (hw, SHELF_H, hd))
    return MobModel("shelf", m)


CRATE_W, CRATE_D, CRATE_H = 0.6, 0.5, 0.45


def _crate(m: Mesh, at: Vec3) -> None:
    x, y, z = at
    hw, hd = CRATE_W / 2, CRATE_D / 2
    m.box("oak", (x - hw, y, z - hd), (x + hw, y + CRATE_H, z + hd), grain=0)
    for sx in (-1, 1):  # corner battens
        for sz in (-1, 1):
            cx, cz = x + sx * (hw - 0.02), z + sz * (hd - 0.02)
            m.box(
                "oak_beam", (cx - 0.03, y, cz - 0.03), (cx + 0.03, y + CRATE_H, cz + 0.03), grain=1
            )


def crate() -> MobModel:
    m = Mesh()
    _crate(m, (0.0, 0.0, 0.0))
    m.body("crate", (-CRATE_W / 2 - 0.01, 0.0, -CRATE_D / 2 - 0.01),
           (CRATE_W / 2 + 0.01, CRATE_H, CRATE_D / 2 + 0.01))  # fmt: skip
    return MobModel("crate", m)


def crate_stack() -> MobModel:
    """Two crates side by side, a third on top."""
    m = Mesh()
    for x in (-0.31, 0.31):
        _crate(m, (x, 0.0, 0.0))
    _crate(m, (-0.12, CRATE_H, -0.02))
    m.body("crates", (-0.62, 0.0, -0.26), (0.62, CRATE_H, 0.26))
    m.body("crate_top", (-0.43, CRATE_H, -0.28), (0.19, 2 * CRATE_H, 0.24))
    return MobModel("crate_stack", m)


def sacks() -> MobModel:
    """Three full sacks, tied at the top."""
    m = Mesh()
    lod1 = Mesh()
    for x, z, h in ((-0.25, 0.0, 0.55), (0.22, -0.03, 0.5), (0.0, 0.12, 0.45)):
        m.cyl("sackcloth", 1, (x, h * 0.42, z), 0.2, h * 0.84, sides=8)
        m.cyl("sackcloth", 1, (x, h * 0.9, z), 0.12, h * 0.12, sides=8)
        m.cyl("sackcloth", 1, (x, h * 0.98, z), 0.05, h * 0.06, sides=6)
        lod1.cyl("sackcloth", 1, (x, h * 0.48, z), 0.19, h * 0.96, sides=5)
    m.body("sacks", (-0.45, 0.0, -0.23), (0.42, 0.55, 0.32))
    return MobModel("sacks", m, lods=[lod1])


WORKBENCH_W, WORKBENCH_D, WORKBENCH_H = 1.8, 0.65, 0.85


def workbench() -> MobModel:
    """A heavy work bench: thick top, four legs, a low shelf, a vice and some tools."""
    m = Mesh()
    hw, hd, top = WORKBENCH_W / 2, WORKBENCH_D / 2, WORKBENCH_H
    m.box("oak_beam", (-hw, top - 0.09, -hd), (hw, top, hd), grain=0)
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * (hw - 0.1), sz * (hd - 0.08)
            m.box("oak_beam", (x - 0.05, 0.0, z - 0.05), (x + 0.05, top - 0.09, z + 0.05), grain=1)
    m.box("oak", (-hw + 0.1, 0.2, -hd + 0.05), (hw - 0.1, 0.23, hd - 0.05), grain=0)
    m.box("iron", (hw - 0.22, top, hd - 0.12), (hw - 0.06, top + 0.1, hd), grain=0)  # vice
    m.box("iron", (-0.5, top, 0.05), (-0.2, top + 0.02, 0.1), grain=0)  # a blade
    m.box("oak", (0.1, top, -0.1), (0.35, top + 0.03, -0.05), grain=0)  # a handle
    m.box("iron", (0.35, top, -0.12), (0.43, top + 0.05, -0.03), grain=0)  # its head
    m.body("bench", (-hw, 0.0, -hd), (hw, top, hd))
    return MobModel("workbench", m)


def tool_board() -> MobModel:
    """Board on the wall (1.1 to 1.9 m) with tongs, hammers and files; no collision."""
    m = Mesh()
    for y in (1.12, 1.74):  # two rails: the wall shows between them
        m.box("pine", (-0.6, y, -0.04), (0.6, y + 0.1, 0.0), grain=0)
    for k, x in enumerate((-0.45, -0.25, -0.05, 0.15, 0.35, 0.5)):
        m.box("iron", (x - 0.01, 1.78, 0.0), (x + 0.01, 1.8, 0.05))  # peg
        if k % 3 == 0:  # tongs: two long arms
            for dx in (-0.015, 0.015):
                m.box("iron", (x + dx - 0.008, 1.3, 0.02), (x + dx + 0.008, 1.78, 0.035))
        elif k % 3 == 1:  # hammer: handle and head
            m.box("oak", (x - 0.012, 1.42, 0.02), (x + 0.012, 1.78, 0.045), grain=1)
            m.box("iron", (x - 0.06, 1.38, 0.015), (x + 0.06, 1.44, 0.05))
        else:  # file or chisel
            m.box("iron", (x - 0.012, 1.5, 0.02), (x + 0.012, 1.78, 0.032))
    return MobModel("tool_board", m)


TROUGH_W, TROUGH_D, TROUGH_H = 1.0, 0.5, 0.6


def quench_trough() -> MobModel:
    """Stone trough of water to quench the iron."""
    m = Mesh()
    hw, hd, t = TROUGH_W / 2, TROUGH_D / 2, 0.08
    m.box("fieldstone", (-hw, 0.0, -hd), (hw, 0.12, hd))
    for z0, z1 in ((-hd, -hd + t), (hd - t, hd)):
        m.box("fieldstone", (-hw, 0.12, z0), (hw, TROUGH_H, z1))
    for x0, x1 in ((-hw, -hw + t), (hw - t, hw)):
        m.box("fieldstone", (x0, 0.12, -hd + t), (x1, TROUGH_H, hd - t))
    m.box("water", (-hw + t, TROUGH_H - 0.1, -hd + t), (hw - t, TROUGH_H - 0.08, hd - t))
    m.body("trough", (-hw, 0.0, -hd), (hw, TROUGH_H, hd))
    return MobModel("quench_trough", m)


def bellows() -> MobModel:
    """Bellows on a low frame, the nozzle towards -X (the hearth beside it)."""
    m = Mesh()
    for x in (-0.35, 0.35):
        for z in (-0.18, 0.18):
            m.box("oak_beam", (x - 0.04, 0.0, z - 0.04), (x + 0.04, 0.45, z + 0.04), grain=1)
    m.box("oak", (-0.4, 0.45, -0.22), (0.45, 0.48, 0.22), grain=0)  # lower board
    m.box("leather", (-0.3, 0.48, -0.2), (0.4, 0.62, 0.2), grain=0)  # the bag
    m.box("oak", (-0.35, 0.62, -0.22), (0.45, 0.65, 0.22), grain=0)  # upper board
    m.box("oak_beam", (0.45, 0.6, -0.03), (0.75, 0.64, 0.03), grain=0)  # handle
    m.box("iron", (-0.6, 0.5, -0.03), (-0.3, 0.56, 0.03), grain=0)  # nozzle
    m.body("bellows", (-0.45, 0.0, -0.22), (0.45, 0.65, 0.22))
    return MobModel("bellows", m)


def sausages() -> MobModel:
    """Pole with sausages and a ham, hanging from the hook (origin) down to -0.5 m."""
    m = Mesh()
    m.box("oak_beam", (-0.5, -0.05, -0.025), (0.5, 0.0, 0.025), grain=0)
    for k, x in enumerate((-0.4, -0.28, -0.16, 0.12, 0.24, 0.36)):
        ln = (0.32, 0.26, 0.38, 0.3, 0.36, 0.28)[k]
        m.box("linen", (x - 0.004, -0.09, -0.004), (x + 0.004, -0.05, 0.004))  # string
        m.cyl("meat", 1, (x, -0.09 - ln / 2, 0.0), 0.024, ln, sides=6)
    m.box("linen", (-0.004, -0.12, -0.004), (0.004, -0.05, 0.004))
    m.cyl("meat", 1, (0.0, -0.27, 0.0), 0.07, 0.3, sides=8)  # the ham
    return MobModel("sausages", m)


def herbs() -> MobModel:
    """Bundles of herbs drying on a string, hanging down to -0.4 m."""
    m = Mesh()
    m.box("linen", (-0.45, -0.01, -0.005), (0.45, 0.0, 0.005), grain=0)
    for k, x in enumerate((-0.35, -0.12, 0.1, 0.32)):
        m.box("linen", (x - 0.004, -0.06, -0.004), (x + 0.004, -0.01, 0.004))
        m.cyl("herb", 1, (x, -0.1, 0.0), 0.025, 0.08, sides=6)  # the tied stems
        m.cyl("herb", 1, (x, -0.24 - 0.02 * (k % 2), 0.0), 0.06, 0.2, sides=6)  # the leaves
    return MobModel("herbs", m)


# the smithy's weapon wall: a board with pegs; the blades are the items' own models (F6), set as
# mesh vobs hanging point down, flat against the wall (WEAPON_PEGS: x, y of the grip)
WEAPON_PEGS = ((-0.32, 1.95), (0.0, 1.95), (0.32, 1.95))
WEAPON_ITEMS = ("items/it_sword_old.glb", "items/it_sword_crude.glb", "items/it_axe.glb")


def weapon_board() -> MobModel:
    m = Mesh()
    for y in (1.0, 1.92):  # two rails: the wall shows behind the blades
        m.box("pine", (-0.6, y, -0.04), (0.6, y + 0.12, 0.0), grain=0)
    for x, y in WEAPON_PEGS:
        m.box("iron", (x - 0.06, y - 0.01, 0.0), (x + 0.06, y + 0.01, 0.06))
    return MobModel("weapon_board", m)


# --- denser rooms (W7 step 4): small things and textiles -------------------------------------


def firewood() -> MobModel:
    """A stack of split logs beside the hearth."""
    m = Mesh()
    for y, n in ((0.07, 5), (0.2, 4), (0.33, 3)):
        for k in range(n):
            x = (k - (n - 1) / 2) * 0.15
            m.cyl("oak_beam", 2, (x, y, 0.0), 0.07, 0.4 - 0.03 * (k % 2), sides=6)
    m.body("firewood", (-0.4, 0.0, -0.21), (0.4, 0.42, 0.21))
    return MobModel("firewood", m)


def pot() -> MobModel:
    """An iron cooking pot with a bail (set on the hearth's fire)."""
    m = Mesh()
    m.cyl("iron", 1, (0.0, 0.1, 0.0), 0.15, 0.2, sides=10)
    m.cyl("iron", 1, (0.0, 0.205, 0.0), 0.16, 0.015, sides=10)
    for x in (-0.16, 0.16):
        m.box("iron", (x - 0.008, 0.2, -0.008), (x + 0.008, 0.33, 0.008))
    m.box("iron", (-0.16, 0.32, -0.008), (0.16, 0.335, 0.008))
    return MobModel("pot", m)


def stool() -> MobModel:
    """A three-legged stool."""
    m = Mesh()
    m.cyl("oak", 1, (0.0, 0.43, 0.0), 0.17, 0.04, sides=10)
    for k in range(3):
        a = 2 * math.pi * k / 3
        m.cyl("oak_beam", 1, (0.11 * math.cos(a), 0.205, 0.11 * math.sin(a)), 0.022, 0.41, sides=6)
    m.body("stool", (-0.17, 0.0, -0.17), (0.17, 0.45, 0.17))
    return MobModel("stool", m)


def bucket() -> MobModel:
    """A wooden bucket with two iron hoops and a rope handle."""
    m = Mesh()
    m.cyl("oak", 1, (0.0, 0.17, 0.0), 0.15, 0.34, sides=10)
    for y in (0.06, 0.28):
        m.cyl("iron", 1, (0.0, y, 0.0), 0.155, 0.025, sides=10)
    m.cyl("water", 1, (0.0, 0.3, 0.0), 0.135, 0.01, sides=10)
    m.box("linen", (-0.15, 0.34, -0.006), (0.15, 0.36, 0.006))
    m.body("bucket", (-0.16, 0.0, -0.16), (0.16, 0.36, 0.16))
    return MobModel("bucket", m)


def basket() -> MobModel:
    """A round wicker basket with a few apples and onions in it."""
    m = Mesh()
    m.cyl("straw", 1, (0.0, 0.14, 0.0), 0.22, 0.28, sides=12)
    m.cyl("straw", 1, (0.0, 0.29, 0.0), 0.235, 0.03, sides=12)
    for x, z in ((-0.08, 0.02), (0.06, -0.06), (0.04, 0.08)):
        m.cyl("wool_red", 1, (x, 0.29, z), 0.04, 0.06, sides=6)
    m.body("basket", (-0.24, 0.0, -0.24), (0.24, 0.31, 0.24))
    return MobModel("basket", m)


def fur() -> MobModel:
    """A sheepskin on the floor (flat, nothing to bump into); a darker hide under it."""
    m = Mesh()
    m.box("fur_dark", (-0.75, 0.0, -0.48), (0.75, 0.012, 0.48))
    m.box("fur_light", (-0.6, 0.012, -0.38), (0.6, 0.03, 0.38))
    return MobModel("fur", m)


def rug() -> MobModel:
    """A rag rug in stripes under a table (flat)."""
    m = Mesh()
    colours = ("wool_red", "linen", "wool_blue", "sackcloth", "wool_ochre", "linen", "wool_green")
    w, d = 2.6, 2.1
    for k, c in enumerate(colours):
        x0 = -w / 2 + w * k / len(colours)
        m.box(c, (x0, 0.0, -d / 2), (x0 + w / len(colours), 0.012, d / 2))
    m.box("sackcloth", (-w / 2 - 0.04, 0.0, -d / 2 - 0.04), (w / 2 + 0.04, 0.006, d / 2 + 0.04))
    return MobModel("rug", m)


def wall_hanging() -> MobModel:
    """A woven hanging on a rod, 1.25 to 2.25 m above the floor (on the wall; no collision)."""
    m = Mesh()
    m.box("oak_beam", (-0.7, 2.24, -0.02), (0.7, 2.28, 0.02), grain=0)
    bands = (
        "wool_red",
        "wool_ochre",
        "wool_red",
        "wool_blue",
        "wool_red",
        "wool_green",
        "wool_red",
    )
    for k, c in enumerate(bands):
        y0 = 2.24 - (k + 1) * 0.14
        m.box(c, (-0.6, y0, -0.012), (0.6, y0 + 0.14, -0.004))
    return MobModel("wall_hanging", m)


def tableware() -> MobModel:
    """Plates, bowls and a jug set out along a table (its middle stays free for the candle)."""
    m = Mesh()
    for x, kind in (
        (-0.55, "plates"),
        (-0.3, "bowl"),
        (0.3, "plates"),
        (0.55, "jug"),
        (0.05, "bowl"),
    ):
        _ware(m, kind, (x, 0.0, 0.12 if kind == "bowl" and x > 0 else -0.12))
    return MobModel("tableware", m)


# --- lanes and yards (W6 streets): outside the houses -----------------------------------------


def cart() -> MobModel:
    """A two-wheeled handcart, shafts towards +X resting on the ground, bed 1.3 x 0.8 m."""
    m = Mesh()
    y0 = 0.55  # bed floor
    m.box("oak", (-0.65, y0, -0.4), (0.65, y0 + 0.05, 0.4))
    for z in (-0.4, 0.37):  # sides
        m.box("oak", (-0.65, y0 + 0.05, z), (0.65, y0 + 0.32, z + 0.03), grain=0)
    for x in (-0.65, 0.62):  # ends
        m.box("oak", (x, y0 + 0.05, -0.37), (x + 0.03, y0 + 0.28, 0.37), grain=2)
    for z in (-0.3, 0.3):  # shafts down to the ground in front
        m.box("oak_beam", (0.65, y0 - 0.06, z - 0.03), (1.45, y0, z + 0.03), grain=0)
        m.box("oak_beam", (1.4, 0.0, z - 0.03), (1.46, y0 - 0.06, z + 0.03), grain=1)
    m.box("oak_beam", (-0.05, 0.42, -0.5), (0.05, y0, 0.5), grain=2)  # axle block
    for z in (-0.46, 0.46):
        m.cyl("oak", 2, (0.0, 0.42, z), 0.42, 0.06, sides=12)
        m.cyl("iron", 2, (0.0, 0.42, z), 0.08, 0.08, sides=6)
    m.body("cart", (-0.65, 0.0, -0.5), (1.46, y0 + 0.32, 0.5))
    return MobModel("cart", m)


def woodpile() -> MobModel:
    """Split logs stacked against a wall, 2 x 1 m, 0.5 m deep, ends towards +Z; under a board.
    Thick logs: a street has hundreds of these (about 640 triangles, the coarse level 24)."""
    m = Mesh()
    rng = np.random.default_rng(71)
    for x in (-0.7, 0.7):  # two squared timbers keep the logs off the ground
        m.box("oak_beam", (x - 0.05, 0.0, -0.25), (x + 0.05, 0.04, 0.25), grain=2)
    for row in range(4):
        y = 0.165 + row * 0.235
        n = 8 - (row % 2)
        for k in range(n):
            x = -0.875 + (k + 0.5 * (row % 2)) * 0.25
            m.cyl("oak_beam", 2, (x, y, 0.0), 0.12 + rng.uniform(-0.01, 0.005),
                  0.5 - rng.uniform(0, 0.06), sides=6)  # fmt: skip
    m.box("oak", (-1.05, 1.0, -0.3), (1.05, 1.04, 0.32), grain=0)  # the board on top
    m.body("woodpile", (-1.0, 0.0, -0.27), (1.0, 1.04, 0.27))
    lod1 = Mesh()
    lod1.box("oak_beam", (-1.0, 0.0, -0.25), (1.0, 0.98, 0.25), grain=0)
    lod1.box("oak", (-1.05, 1.0, -0.3), (1.05, 1.04, 0.32), grain=0)
    return MobModel("woodpile", m, lods=[lod1])


def dung_heap() -> MobModel:
    """A heap of dung and straw in a back yard, about 2 m across."""
    from gothar_worldgen.vegetation import blob

    m = Mesh()
    rng = np.random.default_rng(73)
    blob(m, "dung", (0.0, 0.0, 0.0), (1.0, 0.5, 0.85), 1, rng, 0.25)
    blob(m, "straw", (0.25, 0.25, -0.1), (0.4, 0.22, 0.35), 0, rng, 0.3)
    m.body("dung", (-0.9, 0.0, -0.75), (0.9, 0.4, 0.75))
    return MobModel("dung_heap", m)


# --- market stalls (W6 streets, the market square) ---------------------------------------------

STALL_W, STALL_D = 3.0, 2.0  # frame; the cloth roof reaches 0.25 m beyond it at the front
STALL_COLOURS = {"a": ("wool_red", "linen"), "b": ("wool_blue", "linen"),
                 "c": ("wool_green", "wool_ochre")}  # fmt: skip


def _cloth(m: Mesh, material: str, pts: list[Vec3]) -> None:
    """A cloth quad seen from both sides (no double-sided material needed)."""
    b = m.b(material)
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts[0], pts[1], pts[2]
    u = math.dist(pts[0], pts[1])
    v = math.dist(pts[1], pts[2])
    uvs = [(0.0, 0.0), (u, 0.0), (u, v), (0.0, v)]
    nx = (y1 - y0) * (z2 - z0) - (z1 - z0) * (y2 - y0)
    ny = (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0)
    nz = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
    b.polygon(pts, uvs, (nx, ny, nz))
    b.polygon(pts, uvs, (-nx, -ny, -nz))


def _stall_frame(m: Mesh, colours: tuple[str, str], stripes: int, lod: bool = False) -> None:
    hw, hd = STALL_W / 2, STALL_D / 2
    front_y, back_y = 2.2, 2.6
    for x in (-hw, hw):
        for z, h in ((hd - 0.05, front_y), (-hd + 0.05, back_y)):
            m.box("oak_beam", (x - 0.05, 0.0, z - 0.05), (x + 0.05, h, z + 0.05), grain=1)
    zf, zb = hd + 0.25, -hd - 0.1  # the cloth hangs over the front
    yf = front_y - 0.25 * (back_y - front_y) / STALL_D
    n = 1 if lod else stripes
    for k in range(n):
        x0 = -hw - 0.1 + (STALL_W + 0.2) * k / n
        x1 = -hw - 0.1 + (STALL_W + 0.2) * (k + 1) / n
        roof = [(x0, back_y + 0.02, zb), (x1, back_y + 0.02, zb), (x1, yf, zf), (x0, yf, zf)]
        _cloth(m, colours[k % 2], roof)
        if not lod:  # the valance along the front
            flap = [(x0, yf, zf), (x1, yf, zf), (x1, yf - 0.3, zf), (x0, yf - 0.3, zf)]
            _cloth(m, colours[(k + 1) % 2], flap)
    if not lod:  # a wooden rim round the cloth: seen edge-on the roof is a frame, not a hairline
        from gothar_worldgen.vegetation import frustum

        x0, x1, yb = -hw - 0.1, hw + 0.1, back_y - 0.01
        m.box("oak_beam", (x0, yf - 0.06, zf - 0.03), (x1, yf, zf + 0.03), grain=0)
        m.box("oak_beam", (x0, yb - 0.03, zb - 0.03), (x1, yb + 0.03, zb + 0.03), grain=0)
        for x in (-hw - 0.1, hw + 0.1):
            frustum(m, "oak_beam", (x, back_y - 0.01, zb), (x, yf - 0.03, zf), 0.035, 0.035, 4)
    # the counter at the front: top at 0.86 m, a board down to the ground
    m.box("oak", (-hw + 0.05, 0.8, hd - 0.55), (hw - 0.05, 0.86, hd - 0.05), grain=0)
    m.box("oak", (-hw + 0.05, 0.05, hd - 0.1), (hw - 0.05, 0.8, hd - 0.06), grain=0)


def market_stall(variant: str) -> MobModel:
    """A market stall 3 x 2 m: four posts, a striped cloth roof sloping to the front (2.2 m), a
    counter with goods; front (customers) towards +Z. Variants a (food), b (cloth), c (pottery)."""
    m = Mesh()
    colours = STALL_COLOURS[variant]
    _stall_frame(m, colours, 6)
    hw, hd = STALL_W / 2, STALL_D / 2
    top, zc = 0.86, hd - 0.3
    rng = np.random.default_rng(ord(variant))
    if variant == "a":  # baskets of apples and onions, loaves
        for x in (-1.0, -0.3, 0.45):
            m.cyl("straw", 1, (x, top + 0.09, zc), 0.2, 0.18, sides=10)
            fruit = "wool_red" if x < 0 else "wool_ochre"
            for dx, dz in ((-0.06, 0.02), (0.06, -0.04), (0.0, 0.07)):
                m.cyl(fruit, 1, (x + dx, top + 0.2, zc + dz), 0.045, 0.06, sides=6)
        for k in range(4):
            m.cyl("clay", 0, (1.0, top + 0.05 + 0.09 * (k % 2), zc - 0.15 + 0.1 * k), 0.05, 0.25,
                  sides=6)  # fmt: skip
    elif variant == "b":  # bolts of cloth
        for k, c in enumerate(("wool_red", "wool_blue", "linen", "wool_green", "wool_ochre")):
            x = -1.1 + 0.52 * k
            m.cyl(c, 2, (x, top + 0.08, zc), 0.08, 0.45, sides=8)
            if k % 2 == 0:
                m.cyl(c, 2, (x + 0.12, top + 0.22, zc - 0.02), 0.07, 0.4, sides=8)
    else:  # pottery
        kinds = ("jug", "bowl", "plates", "jug", "mug", "bowl", "plates", "jug", "mug")
        for k, kind in enumerate(kinds):
            _ware(m, kind, (-1.2 + 0.3 * k, top, zc + rng.uniform(-0.12, 0.12)))
    # stock behind the counter
    _crate(m, (-0.8, 0.0, -hd + 0.4))
    m.cyl("sackcloth", 1, (0.6, 0.25, -hd + 0.35), 0.2, 0.5, sides=8)
    m.body("counter", (-hw, 0.0, hd - 0.55), (hw, 0.86, hd))
    m.body("post_back_l", (-hw - 0.05, 0.0, -hd), (-hw + 0.05, 2.6, -hd + 0.1))
    m.body("post_back_r", (hw - 0.05, 0.0, -hd), (hw + 0.05, 2.6, -hd + 0.1))
    m.body("stock", (-1.1, 0.0, -hd + 0.1), (0.8, 0.6, -hd + 0.7))
    lod1 = Mesh()
    _stall_frame(lod1, colours, 1, lod=True)
    return MobModel(f"market_stall_{variant}", m, lods=[lod1])


CANDLE_TOP = 0.26  # the flame of the candlestick, above its foot


def candlestick() -> MobModel:
    """An iron candlestick with a tallow candle and its flame (on a table; no collision)."""
    m = Mesh()
    m.cyl("iron", 1, (0.0, 0.01, 0.0), 0.06, 0.02, sides=8)  # foot
    m.cyl("iron", 1, (0.0, 0.06, 0.0), 0.012, 0.09, sides=6)  # stem
    m.cyl("iron", 1, (0.0, 0.11, 0.0), 0.035, 0.015, sides=8)  # drip pan
    m.cyl("wax", 1, (0.0, 0.17, 0.0), 0.018, 0.1, sides=8)  # the candle
    m.box("flame", (-0.008, 0.22, -0.002), (0.008, CANDLE_TOP, 0.002))
    m.box("flame", (-0.002, 0.22, -0.008), (0.002, CANDLE_TOP, 0.008))
    return MobModel("candlestick", m)


LANTERN_Y = 1.95  # the lantern's light, above the floor (on the wall)


def lantern() -> MobModel:
    """A wall lantern: iron bracket from the wall (-Z) and a small iron cage with a candle
    glowing inside; origin on the floor at the wall, no collision."""
    m = Mesh()
    y = LANTERN_Y
    m.box("iron", (-0.02, y + 0.18, 0.0), (0.02, y + 0.21, 0.24), grain=2)  # bracket
    m.box("iron", (-0.04, y + 0.1, -0.01), (0.04, y + 0.26, 0.0))  # wall plate
    c = 0.2  # the cage's middle, out from the wall
    for dx in (-0.07, 0.07):  # corner posts
        for dz in (-0.07, 0.07):
            m.box(
                "iron",
                (dx - 0.008, y - 0.12, c + dz - 0.008),
                (dx + 0.008, y + 0.12, c + dz + 0.008),
            )
    m.box("iron", (-0.08, y + 0.12, c - 0.08), (0.08, y + 0.15, c + 0.08))  # top
    m.box("iron", (-0.08, y - 0.15, c - 0.08), (0.08, y - 0.12, c + 0.08))  # bottom
    m.cyl("wax", 1, (0.0, y - 0.07, c), 0.02, 0.1, sides=6)
    m.box("flame", (-0.01, y - 0.02, c - 0.003), (0.01, y + 0.05, c + 0.003))
    m.box("flame", (-0.003, y - 0.02, c - 0.01), (0.003, y + 0.05, c + 0.01))
    return MobModel("lantern", m)


PROPS = {"hearth": hearth, "candlestick": candlestick, "lantern": lantern, "firewood": firewood,
         "pot": pot, "stool": stool, "bucket": bucket, "basket": basket, "fur": fur, "rug": rug,
         "wall_hanging": wall_hanging, "tableware": tableware, "barrel": barrel,
         "barrel_rack": barrel_rack, "counter": counter,
         "shelf": shelf, "crate": crate, "crate_stack": crate_stack, "sacks": sacks,
         "workbench": workbench, "tool_board": tool_board, "quench_trough": quench_trough,
         "bellows": bellows, "sausages": sausages, "herbs": herbs,
         "weapon_board": weapon_board, "cart": cart, "woodpile": woodpile,
         "dung_heap": dung_heap, "barrel_stack": barrel_stack,
         "market_stall_a": lambda: market_stall("a"), "market_stall_b": lambda: market_stall("b"),
         "market_stall_c": lambda: market_stall("c"),
         }  # plain mesh vobs (assets/source/props)  # fmt: skip


def write_mobs(folder: Path, types: Sequence[str] = TYPES,
               builders: dict[str, Any] | None = None) -> list[str]:  # fmt: skip
    """Writes ``<type>.glb`` into ``folder`` and the textures they use into the shared folder
    beside it (``TEXTURE_FOLDER``); returns report lines."""
    folder.mkdir(parents=True, exist_ok=True)
    kinds: set[str] = set()
    lines = []
    for t in types:
        model = (builders or BUILDERS)[t]()
        data = model.glb()
        path = folder / f"{t}.glb"
        if not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
        for mesh in model.meshes():
            kinds.update(MATERIALS[mat][0] for mat in mesh.builders)
        nodes = ", ".join(n for n, _, _ in model.parts) or "-"
        lods = "".join(f" / {m.triangles()}" for m in model.lods)
        lines.append(f"{t}: {model.triangles()}{lods} triangles, parts {nodes}")
    shared = folder.parent.joinpath(*TEXTURE_FOLDER)
    for kind in sorted(kinds):
        tex = make(kind, TEXTURE_SIZE)
        to_png(tex.albedo, shared / f"{kind}_albedo.png", True, albedo_scale(kind))
        to_png(tex.normal(), shared / f"{kind}_normal.png", False)
    return lines
