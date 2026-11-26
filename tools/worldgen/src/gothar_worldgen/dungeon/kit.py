"""The dungeon construction kit (W8 D2, docs/design/schloss-dungeon.md section 3.3).

Modules in Python like the mobs (``mobs.py``), no Blender: deterministic and testable. Rubble
walls, a slab floor and the vault are built per block by the generator (``KitMesh``); the moving
and usable parts are files of their own in ``worlds/<site>/dungeon/kit/``, shared by all levels.
Textures come from ``textures.procedural``; they are the ones the mobs and props share
(``assets/source/furniture/textures/``, written there when missing).

Axes of the kit files (contract with engine, dungeon.md): Y up, metres, the front +Z.
- ``gate``: a portcullis of iron bars, ``GATE_W`` wide, ``GATE_H`` high, origin at the bottom in
  the middle; the engine lifts it by its height (``mover`` ``gate``).
- ``secret_wall``: a rubble block filling a cell, origin on the floor in its middle; it sinks into
  the floor (``mover`` ``secret_wall`` with ``offset`` [0, -height, 0]).
- ``trapdoor``: an oak lid, origin at the hinge edge, the lid along +Z, top at y = 0; the engine
  turns it 90 degrees about +X (``mover`` ``trapdoor``).
- ``lever``: an iron plate on the wall (-Z) with a handle, origin on the floor at the wall.
- ``wall_button``: a loose stone at 1.2 m, origin on the floor at the wall.
- ``niche``: the lining of a hollow in the wall (``NICHE_W`` x ``NICHE_H``, ``NICHE_D`` deep),
  floor at ``NICHE_Y``; origin on the floor at the wall face, the hollow towards -Z.
- ``plate``: a pressure plate, a flat slab ``PLATE_W`` square, origin on the floor in its middle.
- ``teleport``: a ring of dark stones in the floor, origin in its middle.

Still pieces (``props`` of a level, W8 E4 "Beinhaus"; mesh vobs with their collision):
- ``sarcophagus``: an ashlar coffer with a lid, ``SARC_L`` long along Z, origin on the floor in
  its middle.
- ``bone_niche``: an ossuary shelf against the wall, origin on the floor at the wall face, the
  shelves towards +Z: ashlar posts and boards with skulls and long bones.
- ``workbench`` (W8 E5 "Werkstatt"): an oak bench ``BENCH_L`` long along X with a vice and tools,
  origin on the floor in its middle.
- ``cogwheel``: an iron gear wheel of the builders' clockwork on the wall, origin on the floor at
  the wall face, the wheel towards +Z.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from gothar_worldgen.buildings.gltf import CollisionPart, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import _Builder
from gothar_worldgen.mobs import GRAIN_U, box_body
from gothar_worldgen.textures.procedural import (
    DUNGEON_KINDS,
    TILE_M,
    albedo_scale,
    make,
    to_png,
)

Vec3 = tuple[float, float, float]
TEXTURE_SIZE = 512
TEXTURE_DIR = "../../../../furniture/textures"  # from worlds/<site>/dungeon/<level or kit>/
# the dungeon's own floor and ceiling textures, not shared: worlds/<site>/dungeon/textures
DUNGEON_TEXTURE_DIR = "../textures"


def texture_dir(kind: str) -> str:
    return DUNGEON_TEXTURE_DIR if kind in DUNGEON_KINDS else TEXTURE_DIR


# material -> (texture kind, linear colour)
MATERIALS: dict[str, tuple[str, tuple[float, float, float]]] = {
    "rubble": ("stone", (0.16, 0.145, 0.13)),  # Bruchstein: walls and vault
    "ashlar": ("stone", (0.24, 0.22, 0.19)),  # Quader: door frames, steps, pillars
    "floor": ("stone", (0.12, 0.11, 0.1)),  # trodden stone
    "loose": ("stone", (0.2, 0.17, 0.14)),  # the loose stone: a shade warmer than the wall
    "oak": ("boards", (0.085, 0.052, 0.03)),
    "oak_beam": ("timber", (0.075, 0.047, 0.026)),
    "iron": ("iron", (0.05, 0.05, 0.055)),
    "dark": ("stone", (0.012, 0.011, 0.01)),  # the stairs' far end: darkness
    "rune": ("stone", (0.05, 0.045, 0.06)),  # the teleporter's ring
    "bone": ("stone", (0.36, 0.33, 0.26)),  # skulls and bones: yellowed, a little dusty
    # floors and ceilings of the levels (owner 2026-11-26: unlike the walls; textures mean 1.0,
    # the rubble walls come out at 0.45 x their colour, about 0.07)
    "flag": ("flagstone", (0.052, 0.048, 0.044)),  # worn flagstones: darker than the wall
    "flag_damp": ("flagstone_damp", (0.04, 0.043, 0.04)),  # Zisterne: damp, darker
    "flag_dust": ("flagstone_dust", (0.06, 0.059, 0.057)),  # Beinhaus: dusty grey
    "plate": ("iron", (0.035, 0.035, 0.038)),  # Werkstatt: iron plates let into the floor
    "vault_brick": ("brick", (0.12, 0.074, 0.054)),  # small bricks: lighter and redder
    "vault_rock": ("rock", (0.05, 0.046, 0.042)),  # rough dark natural stone
}
CELL_M = 3.0
WALL_H = 3.5
GATE_W, GATE_H, GATE_T = 3.0, 3.5, 0.12
DOOR_W, DOOR_H = 1.0, 2.0  # mobs.py door blade
FRAME_T = 0.4  # the wall across a door cell
TRAP_W = 2.0  # the hole of a trapdoor (square)
TRAP_T = 0.08
NICHE_W, NICHE_H, NICHE_D, NICHE_Y = 1.0, 0.8, 0.6, 0.9
PLATE_W, PLATE_T = 1.6, 0.03
BUTTON_Y = 1.2
LEVER_Y = 1.2
SARC_L, SARC_W, SARC_H = 2.2, 0.95, 0.95  # sarcophagus with its lid
OSS_W, OSS_D, OSS_H = 1.9, 0.42, 2.3  # bone niche: width, depth from the wall, height
BENCH_L, BENCH_W, BENCH_H = 2.0, 0.85, 0.85  # workbench
COG_R, COG_Y, COG_T = 1.0, 1.75, 0.12  # cogwheel: radius (to the teeth), hub height, thickness


@dataclass
class KitMesh:
    """Builders per material plus collision bodies, relative to ``origin``."""

    origin: Vec3 = (0.0, 0.0, 0.0)
    builders: dict[str, _Builder] = field(default_factory=dict)
    collision: list[CollisionPart] = field(default_factory=list)

    def b(self, material: str) -> _Builder:
        if material not in self.builders:
            self.builders[material] = _Builder(self.origin)
        return self.builders[material]

    def quad(self, material: str, pts: Sequence[Vec3], want: Vec3) -> None:
        """A planar quad; the texture is projected along the axis of ``want`` (world metres,
        so walls and floors continue across cells)."""
        axis = max(range(3), key=lambda k: abs(want[k]))
        if axis == 1:
            uvs = [(p[0], p[2]) for p in pts]
        else:
            h = 2 if axis == 0 else 0
            uvs = [(p[h], p[1]) for p in pts]
        self.b(material).polygon(pts, uvs, want)

    def box(self, material: str, lo: Vec3, hi: Vec3, grain: int = 0) -> None:
        from gothar_worldgen.mobs import _box

        _box(self.b(material), lo, hi, grain, MATERIALS[material][0] in GRAIN_U)

    def body(self, name: str, lo: Vec3, hi: Vec3) -> None:
        o = self.origin
        self.collision.append(
            box_body(
                f"COL_HULL_{name}",
                (lo[0] - o[0], lo[1] - o[1], lo[2] - o[2]),
                (hi[0] - o[0], hi[1] - o[1], hi[2] - o[2]),
            )
        )

    def hull(self, name: str, pts: Sequence[Vec3]) -> None:
        """A convex body from eight corners (bottom four, top four)."""
        from gothar_worldgen.mobs import _hull

        o = self.origin
        self.collision.append(
            _hull(f"COL_HULL_{name}", [(p[0] - o[0], p[1] - o[1], p[2] - o[2]) for p in pts])
        )

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
            out.append(
                Primitive(
                    material,
                    (*(min(c / scale, 1.0) for c in colour), 1.0),  # type: ignore[arg-type]
                    mesh,
                    (
                        f"{texture_dir(kind)}/{kind}_albedo.png",
                        f"{texture_dir(kind)}/{kind}_normal.png",
                    ),
                )
            )
        return out

    def kinds(self) -> set[str]:
        return {MATERIALS[m][0] for m, b in self.builders.items() if b.idx}

    def triangles(self) -> int:
        return sum(len(b.idx) // 3 for b in self.builders.values())

    def glb(self, name: str) -> bytes:
        return glb_bytes_multi(self.primitives(), name, self.collision)


# --- the moving and usable parts -----------------------------------------------------------


def gate() -> KitMesh:
    """A portcullis: upright bars, three cross bars, spikes at the foot; one collision slab."""
    m = KitMesh()
    hw, t = GATE_W / 2, GATE_T / 2
    n = 9
    for k in range(n):
        x = -hw + 0.15 + k * (GATE_W - 0.3) / (n - 1)
        m.box("iron", (x - 0.03, 0.12, -t / 2), (x + 0.03, GATE_H, t / 2), grain=1)
        m.box("iron", (x - 0.018, 0.0, -0.018), (x + 0.018, 0.12, 0.018), grain=1)  # the spike
    for y in (0.5, 1.7, 2.9):
        m.box("iron", (-hw, y - 0.04, -t), (hw, y + 0.04, t), grain=0)
    m.body("gate", (-hw, 0.0, -t), (hw, GATE_H, t))
    return m


def secret_wall() -> KitMesh:
    """A rubble block filling one cell (it sinks into the floor), slightly inset."""
    m = KitMesh()
    h = CELL_M / 2 - 0.01
    lo, hi = (-h, 0.0, -h), (h, WALL_H, h)
    for axis in (0, 2):
        for side in (-1.0, 1.0):
            c = side * h
            if axis == 0:
                pts = [(c, 0.0, -h), (c, 0.0, h), (c, WALL_H, h), (c, WALL_H, -h)]
                want = (side, 0.0, 0.0)
            else:
                pts = [(-h, 0.0, c), (h, 0.0, c), (h, WALL_H, c), (-h, WALL_H, c)]
                want = (0.0, 0.0, side)
            m.quad("rubble", pts, want)
    m.quad(
        "rubble",
        [(-h, WALL_H, -h), (h, WALL_H, -h), (h, WALL_H, h), (-h, WALL_H, h)],
        (0.0, 1.0, 0.0),
    )
    m.body("secret_wall", lo, hi)
    return m


def trapdoor() -> KitMesh:
    """An oak lid with two iron bands, the hinge edge at the origin."""
    m = KitMesh()
    hw = TRAP_W / 2
    m.box("oak", (-hw, -TRAP_T, 0.0), (hw, 0.0, TRAP_W), grain=2)
    for x in (-0.6, 0.6):
        m.box("iron", (x - 0.04, 0.0, 0.0), (x + 0.04, 0.006, TRAP_W), grain=2)
    m.body("trapdoor", (-hw, -TRAP_T, 0.0), (hw, 0.0, TRAP_W))
    return m


def lever() -> KitMesh:
    """An iron plate on the wall with a handle pointing up and out."""
    m = KitMesh()
    m.box("iron", (-0.12, LEVER_Y - 0.2, 0.0), (0.12, LEVER_Y + 0.2, 0.03))
    m.box("iron", (-0.025, LEVER_Y - 0.03, 0.03), (0.025, LEVER_Y + 0.03, 0.12))  # the pivot
    b = m.b("iron")
    a = math.radians(30.0)
    r = 0.022
    p0 = np.array([0.0, LEVER_Y, 0.1])
    p1 = p0 + 0.42 * np.array([0.0, math.cos(a), math.sin(a)])
    for (dx0, dy0), (dx1, dy1) in (((-r, 0.0), (r, 0.0)), ((0.0, -r), (0.0, r))):
        quad = [
            (p0[0] + dx0, p0[1] + dy0, p0[2]),
            (p0[0] + dx1, p0[1] + dy1, p0[2]),
            (p1[0] + dx1, p1[1] + dy1, p1[2]),
            (p1[0] + dx0, p1[1] + dy0, p1[2]),
        ]
        b.polygon(quad, [(0, 0), (0.1, 0), (0.1, 0.5), (0, 0.5)], (0.0, 0.0, 1.0))
        b.polygon(quad, [(0, 0), (0.1, 0), (0.1, 0.5), (0, 0.5)], (0.0, 0.0, -1.0))
    m.box(
        "oak_beam",
        (p1[0] - 0.035, p1[1] - 0.02, p1[2] - 0.035),
        (p1[0] + 0.035, p1[1] + 0.12, p1[2] + 0.035),
        grain=1,
    )
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def wall_button() -> KitMesh:
    """A loose stone standing 2 cm out of the wall."""
    m = KitMesh()
    m.box("loose", (-0.14, BUTTON_Y - 0.09, -0.01), (0.14, BUTTON_Y + 0.09, 0.02))
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def niche() -> KitMesh:
    """The inside of a hollow in the wall: floor, top, two sides and the back, an ashlar sill."""
    m = KitMesh()
    hw, y0, y1, d = NICHE_W / 2, NICHE_Y, NICHE_Y + NICHE_H, -NICHE_D
    m.quad("ashlar", [(-hw, y0, d), (hw, y0, d), (hw, y0, 0.0), (-hw, y0, 0.0)], (0.0, 1.0, 0.0))
    m.quad("rubble", [(-hw, y1, d), (hw, y1, d), (hw, y1, 0.0), (-hw, y1, 0.0)], (0.0, -1.0, 0.0))
    m.quad("rubble", [(-hw, y0, d), (hw, y0, d), (hw, y1, d), (-hw, y1, d)], (0.0, 0.0, 1.0))
    for x, s in ((-hw, 1.0), (hw, -1.0)):
        m.quad("rubble", [(x, y0, d), (x, y0, 0.0), (x, y1, 0.0), (x, y1, d)], (s, 0.0, 0.0))
    m.box("ashlar", (-hw - 0.08, y0 - 0.1, 0.0), (hw + 0.08, y0, 0.05))
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def plate() -> KitMesh:
    """A pressure plate: one flat slab with a dark joint round it."""
    m = KitMesh()
    hw = PLATE_W / 2
    m.box("dark", (-hw - 0.03, 0.0, -hw - 0.03), (hw + 0.03, 0.004, hw + 0.03))
    m.box("ashlar", (-hw, 0.0, -hw), (hw, PLATE_T, hw))
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def teleport() -> KitMesh:
    """A ring of twelve dark stones in the floor."""
    m = KitMesh()
    for k in range(12):
        a = 2 * math.pi * k / 12
        cx, cz = 1.0 * math.cos(a), 1.0 * math.sin(a)
        m.box("rune", (cx - 0.13, 0.0, cz - 0.13), (cx + 0.13, 0.012, cz + 0.13))
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def _skull(m: KitMesh, x: float, y: float, z: float, turn: float = 0.0) -> None:
    """A skull of four boxes on a shelf at height ``y``, face towards +Z."""
    dx = turn * 0.02
    m.box("bone", (x - 0.075, y + 0.02, z - 0.09), (x + 0.075, y + 0.17, z + 0.08))  # cranium
    m.box("bone", (x - 0.06 + dx, y, z - 0.02), (x + 0.06 + dx, y + 0.05, z + 0.1))  # jaw
    m.box("dark", (x - 0.055 + dx, y + 0.08, z + 0.08), (x - 0.012 + dx, y + 0.12, z + 0.083))
    m.box("dark", (x + 0.012 + dx, y + 0.08, z + 0.08), (x + 0.055 + dx, y + 0.12, z + 0.083))


def sarcophagus() -> KitMesh:
    """An ashlar coffer on a plinth, the lid overhanging with a ridge and a carved cross."""
    m = KitMesh()
    hl, hw = SARC_L / 2, SARC_W / 2
    m.box("ashlar", (-hw - 0.05, 0.0, -hl - 0.05), (hw + 0.05, 0.12, hl + 0.05))  # plinth
    m.box("ashlar", (-hw + 0.04, 0.12, -hl + 0.04), (hw - 0.04, SARC_H - 0.16, hl - 0.04))
    m.box("ashlar", (-hw, SARC_H - 0.16, -hl), (hw, SARC_H - 0.06, hl), grain=2)  # lid
    m.box("ashlar", (-0.18, SARC_H - 0.06, -hl + 0.06), (0.18, SARC_H, hl - 0.06), grain=2)
    m.box("dark", (-0.03, SARC_H, -0.5), (0.03, SARC_H + 0.008, 0.6))  # the carved cross
    m.box("dark", (-0.26, SARC_H, 0.18), (0.26, SARC_H + 0.008, 0.24))
    m.box("dark", (-hw + 0.04, SARC_H - 0.16, -hl + 0.04), (hw - 0.04, SARC_H - 0.15, -hl + 0.05))
    m.body("sarcophagus", (-hw - 0.05, 0.0, -hl - 0.05), (hw + 0.05, SARC_H, hl + 0.05))
    return m


def bone_niche() -> KitMesh:
    """An ossuary shelf: three ashlar posts, four boards, skulls in rows, long bones stacked."""
    m = KitMesh()
    hw, d = OSS_W / 2, OSS_D
    m.box("dark", (-hw, 0.0, 0.0), (hw, OSS_H, 0.02))  # the soot-black wall behind
    for x in (-hw, -0.06, hw - 0.12):
        m.box("ashlar", (x, 0.0, 0.0), (x + 0.12, OSS_H, d), grain=0)
    shelves = (0.0, 0.62, 1.24, 1.86)
    for y in shelves:
        m.box("ashlar", (-hw, y, 0.0), (hw, y + 0.08, d))
    m.box("ashlar", (-hw - 0.04, OSS_H - 0.1, 0.0), (hw + 0.04, OSS_H, d + 0.04))  # lintel
    for k, y in enumerate(shelves):
        y0 = y + 0.08
        for side, (a, b) in enumerate(((-hw + 0.12, -0.06), (0.06, hw - 0.12))):
            if (k + side) % 2 == 0:  # a row of skulls
                n = 3
                for i in range(n):
                    x = a + (i + 0.5) * (b - a) / n
                    _skull(m, x, y0, d * 0.5, turn=((i + k) % 3) - 1)
            else:  # long bones stacked, ends towards the room
                for row in range(3):
                    for i in range(5):
                        x = a + 0.06 + i * (b - a - 0.12) / 4
                        yy = y0 + row * 0.07
                        m.box("bone", (x - 0.03, yy, 0.04), (x + 0.03, yy + 0.06, d - 0.03))
                        m.box("bone", (x - 0.042, yy, d - 0.06), (x + 0.042, yy + 0.07, d - 0.02))
    m.body("bone_niche", (-hw - 0.04, 0.0, 0.0), (hw + 0.04, OSS_H, d + 0.04))
    return m


def workbench() -> KitMesh:
    """An oak bench on four legs with a stretcher, a vice at one end, a hammer and tongs."""
    m = KitMesh()
    hl, hw, top = BENCH_L / 2, BENCH_W / 2, BENCH_H
    m.box("oak", (-hl, top - 0.1, -hw), (hl, top, hw), grain=0)
    for x in (-hl + 0.1, hl - 0.18):
        for z in (-hw + 0.08, hw - 0.16):
            m.box("oak_beam", (x, 0.0, z), (x + 0.08, top - 0.1, z + 0.08), grain=1)
    m.box("oak_beam", (-hl + 0.1, 0.2, -0.04), (hl - 0.1, 0.28, 0.04), grain=0)
    m.box("oak", (-hl + 0.12, 0.3, -hw + 0.1), (hl - 0.12, 0.34, hw - 0.1), grain=0)  # shelf
    m.box("iron", (hl - 0.32, top, -0.12), (hl - 0.08, top + 0.16, 0.12))  # the vice
    m.box("iron", (hl - 0.36, top + 0.04, -0.02), (hl - 0.32, top + 0.08, 0.02))
    m.box("iron", (-0.5, top, 0.1), (-0.38, top + 0.06, 0.16))  # hammer head
    m.box("oak_beam", (-0.46, top, 0.16), (-0.42, top + 0.03, 0.5), grain=2)
    m.box("iron", (0.1, top, -0.3), (0.6, top + 0.02, -0.26))  # tongs
    m.box("iron", (0.1, top, -0.22), (0.6, top + 0.02, -0.18))
    m.box("iron", (-0.9, top, -0.3), (-0.7, top + 0.12, -0.12))  # a cast part
    m.body("workbench", (-hl, 0.0, -hw), (hl, top + 0.16, hw))
    return m


def cogwheel() -> KitMesh:
    """A gear of the builders' clockwork: rim with teeth, six spokes, hub and axle in the wall."""
    m = KitMesh()
    z0, z1 = 0.08, 0.08 + COG_T
    b = m.b("iron")
    n_rim = 24
    r0, r1 = COG_R - 0.3, COG_R - 0.12  # inner and outer radius of the rim
    for k in range(n_rim):
        a0, a1 = 2 * math.pi * k / n_rim, 2 * math.pi * (k + 1) / n_rim
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        ring = [(r0 * c0, COG_Y + r0 * s0), (r1 * c0, COG_Y + r1 * s0),
                (r1 * c1, COG_Y + r1 * s1), (r0 * c1, COG_Y + r0 * s1)]  # fmt: skip
        uv = [(0, 0), (0.2, 0), (0.2, 0.2), (0, 0.2)]
        b.polygon([(x, y, z1) for x, y in ring], uv, (0.0, 0.0, 1.0))
        b.polygon([(x, y, z0) for x, y in reversed(ring)], uv, (0.0, 0.0, -1.0))
        for r, sgn in ((r1, 1.0), (r0, -1.0)):  # outer and inner band
            p, q = (r * c0, COG_Y + r * s0), (r * c1, COG_Y + r * s1)
            want = (sgn * math.cos((a0 + a1) / 2), sgn * math.sin((a0 + a1) / 2), 0.0)
            b.polygon([(p[0], p[1], z0), (q[0], q[1], z0), (q[0], q[1], z1), (p[0], p[1], z1)],
                      uv, want)  # fmt: skip
    for k in range(16):  # the teeth
        a = 2 * math.pi * (k + 0.5) / 16
        c, s = math.cos(a), math.sin(a)
        mid = (COG_R - 0.06) * c, COG_Y + (COG_R - 0.06) * s
        hx, hy = 0.07 * abs(c) + 0.06 * abs(s), 0.07 * abs(s) + 0.06 * abs(c)
        m.box("iron", (mid[0] - hx, mid[1] - hy, z0), (mid[0] + hx, mid[1] + hy, z1))
    for k in range(3):  # three bars crossing at the hub: six spokes
        a = math.pi * k / 3
        c, s = math.cos(a), math.sin(a)
        steps = 6
        for i in range(-steps, steps):
            t0, t1 = (r0 - 0.02) * i / steps, (r0 - 0.02) * (i + 1) / steps
            x0, y0 = t0 * c, COG_Y + t0 * s
            x1, y1 = t1 * c, COG_Y + t1 * s
            m.box("iron", (min(x0, x1) - 0.04, min(y0, y1) - 0.04, z0 + 0.02),
                  (max(x0, x1) + 0.04, max(y0, y1) + 0.04, z1 - 0.02))  # fmt: skip
    m.box("iron", (-0.16, COG_Y - 0.16, z0 - 0.02), (0.16, COG_Y + 0.16, z1 + 0.04))  # hub
    m.box("iron", (-0.06, COG_Y - 0.06, 0.0), (0.06, COG_Y + 0.06, z1 + 0.08))  # axle
    m.body("cogwheel", (-COG_R, COG_Y - COG_R, 0.0), (COG_R, COG_Y + COG_R, z1 + 0.08))
    return m


PROPS = {"sarcophagus": sarcophagus, "bone_niche": bone_niche, "workbench": workbench,
         "cogwheel": cogwheel}  # fmt: skip

KIT = {
    "gate": gate,
    "secret_wall": secret_wall,
    "trapdoor": trapdoor,
    "lever": lever,
    "wall_button": wall_button,
    "niche": niche,
    "plate": plate,
    "teleport": teleport,
    **PROPS,
}


def write_kit(folder: Path) -> tuple[list[str], set[str]]:
    """Writes ``<module>.glb`` into ``folder`` (unchanged files stay); returns report lines and
    the texture kinds used."""
    folder.mkdir(parents=True, exist_ok=True)
    lines, kinds = [], set()
    for name, build in KIT.items():
        m = build()
        _write(folder / f"{name}.glb", m.glb(name))
        kinds |= m.kinds()
        lines.append(f"kit {name}: {m.triangles()} triangles")
    return lines, kinds


def write_textures(folder: Path, kinds: set[str]) -> None:
    for kind in sorted(kinds):
        tex = make(kind, TEXTURE_SIZE)
        _write_png(tex.albedo, folder / f"{kind}_albedo.png", True, albedo_scale(kind))
        _write_png(tex.normal(), folder / f"{kind}_normal.png", False, 1.0)


def _write(path: Path, data: bytes) -> None:
    if not path.is_file() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def _write_png(img: np.ndarray, path: Path, srgb: bool, scale: float) -> None:
    tmp = path.with_name(path.stem + ".tmp.png")
    to_png(img, tmp, srgb, scale)
    data = tmp.read_bytes()
    tmp.unlink()
    _write(path, data)
