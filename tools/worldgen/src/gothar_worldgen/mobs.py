"""Mob models: chest, anvil, bed, door (M8, contract characters-pipeline.md §3.1, mobs.toml v1).

Built by script like the houses, in the style of leonberg-stil.md (oak, forged iron, wool and
straw), with the textures of ``textures.procedural``; the files are not tied to a place:
``assets/source/mobs/<type>.glb`` with their images in ``assets/source/mobs/textures/``.

Axes (contract): Y up, origin on the ground, the front faces +Z, metres. The figure stands in front
(slots in mobs.toml, maintained by figuren) and looks towards -Z.

- chest 0.9 x 0.6 x 0.6 m, front at z = +0.3, body 0.5 m high; the lid is the node ``MOB_LID``
  with its pivot at the rear hinge (z = -0.3, y = 0.5), turned about its X axis by the engine; its
  collision body is a child of ``MOB_LID`` and turns with it;
- anvil: wooden block and forged anvil, working surface 0.8 m, horn towards +X;
- bed 2.0 x 0.9 m along X (long side towards +Z), lying surface 0.45 m, head board at -X;
- door: the blade only, 1.0 x 2.0 x 0.05 m along +X from the hinge at the origin, handle at 1.0 m
  on both sides; the engine turns the whole mob about +Y.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from gothar_worldgen.buildings.gltf import CollisionPart, Part, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import _Builder
from gothar_worldgen.textures.procedural import TILE_M, albedo_scale, make, to_png

TYPES = ("chest", "anvil", "bed", "door")
TEXTURE_SIZE = 512
TEXTURE_DIR = "textures"  # next to the models, relative URIs like glTF wants
BUDGET = 1500  # triangles per model (render)
# material -> (texture kind, linear colour); the colour is the tint like the palette of the houses
MATERIALS: dict[str, tuple[str, tuple[float, float, float]]] = {
    "oak": ("boards", (0.085, 0.052, 0.03)),  # old oak, darkened
    "oak_beam": ("timber", (0.075, 0.047, 0.026)),
    "iron": ("iron", (0.05, 0.05, 0.055)),
    "straw": ("straw", (0.42, 0.33, 0.15)),
    "wool": ("cloth", (0.13, 0.075, 0.05)),  # undyed brown wool
    "linen": ("cloth", (0.5, 0.45, 0.36)),
}
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

    def triangles(self) -> int:
        return self.main.triangles() + sum(m.triangles() for _, _, m in self.parts)

    def glb(self) -> bytes:
        parts = [Part(n, pivot, m.primitives(), m.collision) for n, pivot, m in self.parts]
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


BUILDERS = {"chest": chest, "anvil": anvil, "bed": bed, "door": door}


def write_mobs(folder: Path, types: Sequence[str] = TYPES) -> list[str]:
    """Writes ``<type>.glb`` and the textures they use into ``folder``; returns report lines."""
    folder.mkdir(parents=True, exist_ok=True)
    kinds: set[str] = set()
    lines = []
    for t in types:
        model = BUILDERS[t]()
        data = model.glb()
        path = folder / f"{t}.glb"
        if not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
        kinds.update(MATERIALS[mat][0] for mat in model.main.builders)
        for _, _, mesh in model.parts:
            kinds.update(MATERIALS[mat][0] for mat in mesh.builders)
        nodes = ", ".join(n for n, _, _ in model.parts) or "-"
        lines.append(f"{t}: {model.triangles()} triangles, parts {nodes}")
    for kind in sorted(kinds):
        tex = make(kind, TEXTURE_SIZE)
        to_png(tex.albedo, folder / TEXTURE_DIR / f"{kind}_albedo.png", True, albedo_scale(kind))
        to_png(tex.normal(), folder / TEXTURE_DIR / f"{kind}_normal.png", False)
    return lines
