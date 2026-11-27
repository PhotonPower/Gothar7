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
- ``stalagmite``, ``stalactite``, ``crystal_cluster`` (W8 E7 "Kristallgrotte"): a rock cone on
  the floor (fills its cell), rock cones hanging from the ceiling (origin at the ceiling's height),
  crystals at the wall (origin on the floor at the wall face, towards +Z); ``crystal_ceiling``
  hangs like a stalactite, ``crystal_spire`` rises from a pool's bottom.
- ``crystal_wall`` and ``crystal_switch``: the cave's looks of ``secret_wall`` and ``wall_button``.
- The lair (W8 E8 "Hort"): ``egg_sacks`` (pale egg sacks in a slime web at the wall, origin on
  the floor at the wall face, towards +Z), ``brood_glow`` (glowing brood growth on the wall, the
  level adds an orange light), ``bones`` and ``slime`` (bone and chitin remains, slime pools: lie
  on the floor, no collision), ``pillar`` (a thick ashlar column, 1.8 m across, up into the rock),
  ``pillar_broken`` (its stump with fallen drums), ``rubble_heap`` (stones of the fallen vault)
  and ``gold_hoard`` (coins, goblets and caskets); the last four fill their cell.
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
    # the cave (W8 E7): natural rock instead of masonry, crystals as the light
    "cave_wall": ("cave_rock", (0.15, 0.15, 0.16)),  # grey-blue, lighter than the masonry
    "cave_floor": ("cave_rock", (0.1, 0.1, 0.105)),  # darker, trodden
    "cave_ceiling": ("cave_rock", (0.12, 0.12, 0.13)),
    "crystal": ("clay", (0.45, 0.78, 0.95)),  # pale blue-turquoise, smooth
    # the lair (W8 E8): the brood's remains and the hoard
    "egg": ("clay", (0.4, 0.37, 0.29)),  # pale, yellowed egg sacks
    "chitin": ("clay", (0.06, 0.042, 0.03)),  # dark brown shell fragments
    "slime": ("clay", (0.07, 0.075, 0.05)),  # dull olive slime on the floor
    "glow": ("clay", (0.95, 0.42, 0.1)),  # the brood's glowing growth: orange
    "gold": ("iron", (0.62, 0.42, 0.1)),  # coins and goblets
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


def _basis(d: Vec3) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """``d`` normalised and two unit vectors across it."""
    w = np.array(d, dtype=float)
    w /= np.linalg.norm(w)
    a = np.array([0.0, 1.0, 0.0]) if abs(w[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(w, a)
    u /= np.linalg.norm(u)
    return w, u, np.cross(w, u)


def _spike(m: KitMesh, material: str, base: Vec3, d: Vec3, r0: float, r1: float,
           length: float, tip: float, sides: int = 6, turn: float = 0.0) -> None:  # fmt: skip
    """A prism or cone along ``d``: ring ``r0`` at ``base``, ring ``r1`` after ``length``, then a
    point ``tip`` further (crystals: r0 == r1; stalagmites taper). The foot stays open (in rock)."""
    w, u, v = _basis(d)
    b = m.b(material)
    p0, p1 = np.array(base, dtype=float), np.array(base, dtype=float) + w * length
    top = p1 + w * tip
    ring0, ring1 = [], []
    for k in range(sides):
        a = turn + 2 * math.pi * k / sides
        off = u * math.cos(a) + v * math.sin(a)
        ring0.append(p0 + off * r0)
        ring1.append(p1 + off * r1)
    centre = (p0 + p1) / 2
    for k in range(sides):
        q0, q1, q2, q3 = ring0[k], ring0[(k + 1) % sides], ring1[(k + 1) % sides], ring1[k]
        out = tuple(float(c) for c in (q0 + q1 + q2 + q3) / 4 - centre)
        uv = [(0.0, 0.0), (0.3, 0.0), (0.3, length), (0.0, length)]
        b.polygon([_v(q) for q in (q0, q1, q2, q3)], uv, out)  # type: ignore[arg-type]
        if tip > 0:
            out = tuple(float(c) for c in (q3 + q2 + top) / 3 - p1)
            b.polygon([_v(q3), _v(q2), _v(top)], [(0, 0), (0.3, 0), (0.15, 0.3)], out)  # type: ignore[arg-type]


def _v(p: np.ndarray) -> Vec3:
    return (float(p[0]), float(p[1]), float(p[2]))


def crystal_cluster() -> KitMesh:
    """Blue-turquoise crystals growing out of a rock foot at the wall, the tallest 1.8 m (origin
    on the floor at the wall face, the cluster towards +Z); the level adds its light."""
    m = KitMesh()
    _spike(m, "cave_wall", (0.0, -0.05, 0.12), (0.0, 1.0, 0.25), 0.75, 0.45, 0.4, 0.0, 8, 0.3)
    shards = (  # base (x, y, z), direction, radius, length
        ((0.0, 0.25, 0.18), (0.05, 1.0, 0.3), 0.17, 1.3),
        ((-0.3, 0.2, 0.22), (-0.45, 1.0, 0.35), 0.13, 1.0),
        ((0.32, 0.15, 0.2), (0.5, 1.0, 0.3), 0.14, 1.1),
        ((0.12, 0.12, 0.45), (0.2, 0.6, 1.0), 0.1, 0.7),
        ((-0.16, 0.12, 0.48), (-0.3, 0.7, 1.0), 0.09, 0.6),
        ((0.55, 0.08, 0.12), (0.9, 0.7, 0.4), 0.09, 0.6),
        ((-0.58, 0.08, 0.12), (-0.9, 0.8, 0.3), 0.08, 0.55),
        ((0.15, 0.9, 0.05), (0.3, 0.8, 0.6), 0.08, 0.5),
        ((-0.2, 1.1, 0.04), (-0.2, 0.9, 0.5), 0.07, 0.45),
        ((0.0, 0.05, 0.6), (0.05, 0.4, 1.0), 0.07, 0.4),
    )
    for k, (base, d, r, length) in enumerate(shards):
        _spike(m, "crystal", base, d, r, r, length, r * 2.2, 6, 0.4 * k)
    m.body("crystal_cluster", (-0.75, 0.0, 0.0), (0.75, 1.2, 0.75))
    return m


def crystal_ceiling() -> KitMesh:
    """Crystals hanging from the rock overhead (origin at the ceiling's height, rooted 1 m up in
    the rock, the longest 1.2 m down); the cell stays free."""
    m = KitMesh()
    _spike(m, "cave_wall", (0.0, 1.0, 0.0), (0.0, -1.0, 0.0), 0.7, 0.5, 1.15, 0.0, 8, 0.2)
    shards = (
        ((0.0, -0.1, 0.0), (0.05, -1.0, 0.1), 0.16, 0.95),
        ((0.35, -0.1, 0.15), (0.5, -1.0, 0.2), 0.12, 0.7),
        ((-0.3, -0.1, 0.25), (-0.45, -1.0, 0.3), 0.11, 0.65),
        ((0.1, -0.1, -0.35), (0.15, -1.0, -0.5), 0.1, 0.6),
        ((-0.25, -0.1, -0.25), (-0.4, -1.0, -0.4), 0.09, 0.5),
        ((0.45, -0.1, -0.3), (0.8, -1.0, -0.4), 0.08, 0.45),
    )
    for k, (base, d, r, length) in enumerate(shards):
        _spike(m, "crystal", base, d, r, r, length, r * 2.0, 6, 0.5 * k)
    m.body("none", (-0.02, 1.5, -0.02), (0.02, 1.54, 0.02))  # in the rock: nothing to bump
    return m


def crystal_spire() -> KitMesh:
    """Crystals rising from a pool's bottom (origin on the bottom in the middle of the cell),
    the tallest 1.6 m: their tips stand out of the water and its surface takes their light."""
    m = KitMesh()
    _spike(m, "cave_wall", (0.0, -0.05, 0.0), (0.0, 1.0, 0.0), 0.6, 0.4, 0.3, 0.0, 7, 0.1)
    shards = (
        ((0.0, 0.2, 0.0), (0.05, 1.0, 0.05), 0.16, 1.15),
        ((0.3, 0.15, 0.1), (0.4, 1.0, 0.15), 0.12, 0.95),
        ((-0.25, 0.15, 0.2), (-0.35, 1.0, 0.3), 0.11, 0.85),
        ((0.05, 0.1, -0.3), (0.1, 1.0, -0.45), 0.1, 0.8),
        ((-0.3, 0.1, -0.2), (-0.5, 0.9, -0.3), 0.09, 0.6),
    )
    for k, (base, d, r, length) in enumerate(shards):
        _spike(m, "crystal", base, d, r, r, length, r * 2.2, 6, 0.6 * k)
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))  # the swimmer passes
    return m


def stalagmite() -> KitMesh:
    """A rock cone rising from the floor (about 2.3 m) with two smaller ones at its foot; it
    fills its cell like a sarcophagus (origin on the floor in its middle)."""
    m = KitMesh()
    _spike(m, "cave_wall", (0.0, -0.1, 0.0), (0.04, 1.0, 0.02), 0.62, 0.22, 1.7, 0.7, 8, 0.2)
    _spike(m, "cave_wall", (0.55, -0.1, 0.35), (0.15, 1.0, 0.1), 0.3, 0.1, 0.6, 0.35, 7, 0.5)
    _spike(m, "cave_wall", (-0.45, -0.1, -0.4), (-0.1, 1.0, -0.05), 0.24, 0.08, 0.4, 0.25, 6)
    m.body("stalagmite", (-0.65, 0.0, -0.65), (0.75, 2.2, 0.7))
    return m


def stalactite() -> KitMesh:
    """Rock cones hanging from the ceiling (origin at the ceiling's height, rooted 1 m up in the
    rock, the longest tip 0.95 m below); the cell stays free."""
    m = KitMesh()
    _spike(m, "cave_wall", (0.0, 1.0, 0.0), (0.0, -1.0, 0.03), 0.5, 0.16, 1.55, 0.4, 8, 0.1)
    _spike(m, "cave_wall", (0.6, 1.0, -0.3), (0.05, -1.0, 0.0), 0.28, 0.08, 1.3, 0.25, 6, 0.4)
    _spike(m, "cave_wall", (-0.5, 1.0, 0.45), (0.0, -1.0, -0.05), 0.22, 0.07, 1.2, 0.2, 6)
    m.body("none", (-0.02, 1.5, -0.02), (0.02, 1.54, 0.02))  # in the rock: nothing to bump
    return m


def crystal_wall() -> KitMesh:
    """The cave's secret wall: a block of rock filling the cell with crystals on its faces (it
    sinks into the floor like ``secret_wall``)."""
    m = KitMesh()
    h = CELL_M / 2 - 0.01
    spots = ((-0.8, 0.6, 0.16, 0.35), (-0.25, 1.5, 0.2, 0.4), (0.35, 0.9, 0.15, 0.32),
             (0.75, 2.0, 0.17, 0.36), (0.05, 2.6, 0.13, 0.3), (-0.65, 2.3, 0.12, 0.28),
             (0.5, 3.0, 0.1, 0.22), (-0.95, 1.6, 0.11, 0.25), (0.9, 1.1, 0.12, 0.26),
             (0.15, 0.35, 0.12, 0.24))  # fmt: skip
    for axis in (0, 2):
        for side in (-1.0, 1.0):
            c = side * h
            if axis == 0:
                pts = [(c, 0.0, -h), (c, 0.0, h), (c, WALL_H, h), (c, WALL_H, -h)]
                want = (side, 0.0, 0.0)
            else:
                pts = [(-h, 0.0, c), (h, 0.0, c), (h, WALL_H, c), (-h, WALL_H, c)]
                want = (0.0, 0.0, side)
            m.quad("cave_wall", pts, want)
            for k, (along, y, r, length) in enumerate(spots):
                lean = 0.15 * (k % 3 - 1)
                if axis == 0:
                    base, d = (c - side * 0.03, y, along * side), (side, 0.35, lean)
                else:
                    base, d = (along * side, y, c - side * 0.03), (lean, 0.35, side)
                _spike(m, "crystal", base, d, r, r, length, r * 2.0, 6, 0.3 * k)
    m.quad(
        "cave_wall",
        [(-h, WALL_H, -h), (h, WALL_H, -h), (h, WALL_H, h), (-h, WALL_H, h)],
        (0.0, 1.0, 0.0),
    )
    m.body("crystal_wall", (-h, 0.0, -h), (h, WALL_H, h))
    return m


def crystal_switch() -> KitMesh:
    """A ``wall_button`` of the cave: a crystal set into a rock socket at 1.2 m, pointing out."""
    m = KitMesh()
    m.box("cave_wall", (-0.2, BUTTON_Y - 0.17, -0.02), (0.2, BUTTON_Y + 0.17, 0.06))
    _spike(m, "crystal", (0.0, BUTTON_Y, 0.04), (0.0, 0.15, 1.0), 0.07, 0.07, 0.14, 0.12, 6)
    _spike(m, "crystal", (0.09, BUTTON_Y + 0.06, 0.04), (0.4, 0.5, 1.0), 0.04, 0.04, 0.1, 0.08)
    _spike(m, "crystal", (-0.08, BUTTON_Y - 0.05, 0.04), (-0.4, 0.2, 1.0), 0.04, 0.04, 0.09, 0.07)
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))
    return m


def _hash(i: int, j: int = 0) -> float:
    """0..1 per integer pair (integer hash: same bytes everywhere)."""
    h = ((i * 73856093) ^ (j * 19349663)) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) / 2**32


def _blob(m: KitMesh, material: str, c: Vec3, r: Vec3, seg: int = 10, rings: int = 6) -> None:
    """An ellipsoid with smooth normals (eggs, knobs, shell fragments)."""
    b = m.b(material)
    base = len(b.pos)
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(seg + 1):
            ph = 2 * math.pi * j / seg
            n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
            p = (c[0] + r[0] * n[0], c[1] + r[1] * n[1], c[2] + r[2] * n[2])
            g = (n[0] / r[0], n[1] / r[1], n[2] / r[2])
            length = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2) or 1.0
            b.pos.append((p[0] - b.ox, p[1] - b.oy, p[2] - b.oz))
            b.nrm.append((g[0] / length, g[1] / length, g[2] / length))
            b.uv.append((j / seg * 2 * max(r), i / rings * 2 * max(r)))
    for i in range(rings):
        for j in range(seg):
            a, d = base + i * (seg + 1) + j, base + (i + 1) * (seg + 1) + j
            b.idx += [a, a + 1, d + 1, a, d + 1, d]


def _cap(m: KitMesh, material: str, centre: Vec3, d: Vec3, radius: float, sides: int,
         turn: float = 0.0) -> None:  # fmt: skip
    """The flat end of a ``_spike`` prism (facing ``d``)."""
    w, u, v = _basis(d)
    c = np.array(centre, dtype=float)
    ring = [c + (u * math.cos(turn + 2 * math.pi * k / sides)
                 + v * math.sin(turn + 2 * math.pi * k / sides)) * radius
            for k in range(sides)]  # fmt: skip
    b = m.b(material)
    for k in range(sides):
        tri = [_v(c), _v(ring[k]), _v(ring[(k + 1) % sides])]
        uv = [(0.5, 0.5), (0.5 + 0.5 * math.cos(k), 0.5), (0.5, 0.5 + 0.5 * math.sin(k))]
        b.polygon(tri, uv, _v(w))  # type: ignore[arg-type]


def _drum(m: KitMesh, material: str, base: Vec3, d: Vec3, r: float, length: float,
          sides: int = 10, turn: float = 0.0) -> None:  # fmt: skip
    """A closed cylinder (column drum, goblet foot) along ``d``; the texture runs round it in
    metres (the joints of the ashlar continue from face to face)."""
    w, u, v = _basis(d)
    b = m.b(material)
    p0 = np.array(base, dtype=float)
    p1 = p0 + w * length
    edge = 2 * r * math.sin(math.pi / sides)
    for k in range(sides):
        a0 = turn + 2 * math.pi * k / sides
        a1 = turn + 2 * math.pi * (k + 1) / sides
        o0 = (u * math.cos(a0) + v * math.sin(a0)) * r
        o1 = (u * math.cos(a1) + v * math.sin(a1)) * r
        quad = [p0 + o0, p0 + o1, p1 + o1, p1 + o0]
        out = _v((o0 + o1) / 2)
        u0 = k * edge
        uv = [(u0, 0.0), (u0 + edge, 0.0), (u0 + edge, length), (u0, length)]
        b.polygon([_v(q) for q in quad], uv, out)  # type: ignore[arg-type]
    _cap(m, material, base, _v(-w), r, sides, turn)
    _cap(m, material, _v(np.array(base) + w * length), d, r, sides, turn)


def _no_body(m: KitMesh) -> None:
    m.body("none", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02))  # below the floor: nothing to bump


EGGS = (  # (x, y, z) centre, radius across, radius up
    (-0.45, 0.28, 0.32, 0.26, 0.34),
    (0.05, 0.3, 0.36, 0.3, 0.4),
    (0.5, 0.26, 0.3, 0.24, 0.32),
    (-0.2, 0.22, 0.62, 0.2, 0.26),
    (0.3, 0.2, 0.62, 0.19, 0.24),
    (-0.7, 0.2, 0.22, 0.18, 0.24),
    (0.78, 0.18, 0.2, 0.17, 0.22),
    (-0.25, 0.78, 0.22, 0.22, 0.3),
    (0.28, 0.85, 0.2, 0.2, 0.28),
    (0.02, 1.32, 0.16, 0.17, 0.24),
    (-0.5, 1.15, 0.14, 0.15, 0.2),
    (0.55, 1.45, 0.13, 0.13, 0.18),
)


def egg_sacks() -> KitMesh:
    """Pale egg sacks of the brood, a dozen in a web of dried slime at the wall (up to 1.7 m)."""
    m = KitMesh()
    _blob(m, "slime", (0.0, 0.75, 0.04), (1.05, 0.95, 0.12), 12, 6)  # the web on the wall
    _blob(m, "slime", (0.0, 0.03, 0.4), (1.0, 0.05, 0.5), 12, 4)  # its foot on the floor
    for k, (x, y, z, rx, ry) in enumerate(EGGS):
        _blob(m, "egg", (x, y, z), (rx, ry, rx * (0.9 + 0.2 * _hash(k))))
    for k in range(3):  # a burst one: a dark hole and shell rags
        x = (-0.45, 0.5, -0.25)[k]
        _blob(m, "chitin", (x + 0.1, 0.05, 0.75 + 0.1 * k), (0.12, 0.03, 0.08), 8, 3)
    m.body("egg_sacks", (-0.95, 0.0, 0.0), (0.95, 1.1, 0.85))
    return m


def brood_glow() -> KitMesh:
    """A growth of the brood on the wall: a dark crusted mass with glowing orange caps and
    knobs at 0.8-2.3 m (the level adds an orange light); nothing to bump into."""
    m = KitMesh()
    for x, y, rx, ry in ((-0.3, 1.3, 0.6, 0.5), (0.35, 1.65, 0.55, 0.6), (-0.1, 2.05, 0.45, 0.35),
                         (0.6, 1.05, 0.3, 0.3), (-0.65, 1.85, 0.3, 0.4)):  # fmt: skip
        _blob(m, "chitin", (x, y, 0.03), (rx, ry, 0.14), 10, 5)  # the crusted mass, lumpy
    caps = ((-0.45, 1.05, 0.2, 0.32), (0.3, 1.25, 0.22, 0.38), (-0.1, 1.75, 0.2, 0.3),
            (0.55, 1.95, 0.16, 0.24), (-0.6, 1.95, 0.14, 0.22),
            (0.05, 2.25, 0.12, 0.2))  # fmt: skip
    for x, y, z, r in caps:  # shelf caps sticking out of the wall
        _blob(m, "glow", (x, y, z), (r, r * 0.28, r * 0.9), 10, 4)
    for k in range(9):  # glowing knobs
        x = -0.75 + 1.5 * _hash(k, 1)
        y = 0.85 + 1.5 * _hash(k, 2)
        r = 0.05 + 0.06 * _hash(k, 3)
        _blob(m, "glow", (x, y, 0.12 + r * 0.5), (r, r, r), 8, 4)
    _no_body(m)
    return m


def brood_ceiling() -> KitMesh:
    """The brood's growth hanging from the rock overhead (origin at the ceiling's height, rooted
    1 m up): a crusted mass with glowing knobs and strands down to 1.4 m (the level adds an orange
    light below it); the cell stays free."""
    m = KitMesh()
    for x, z, r, d in ((0.0, 0.0, 0.7, 1.3), (0.45, 0.3, 0.4, 1.0), (-0.4, -0.25, 0.45, 1.05)):
        _blob(m, "chitin", (x, 1.0 - d / 2, z), (r, d / 2 + 0.1, r * 0.85), 10, 5)
    for k in range(10):
        a = 2 * math.pi * _hash(k, 71)
        rr = 0.15 + 0.55 * _hash(k, 72)
        x, z = rr * math.cos(a), rr * math.sin(a)
        y = -0.1 - 0.35 * _hash(k, 73)
        r = 0.08 + 0.1 * _hash(k, 74)
        _blob(m, "glow", (x, y, z), (r, r * 1.3, r), 8, 4)
        if k % 3 == 0:  # a strand with a glowing drop
            length = 0.5 + 0.5 * _hash(k, 75)
            _spike(m, "slime", (x, y, z), (0.0, -1.0, 0.0), 0.025, 0.015, length, 0.0, 5)
            _blob(m, "glow", (x, y - length, z), (0.06, 0.08, 0.06), 6, 3)
    m.body("none", (-0.02, 1.5, -0.02), (0.02, 1.54, 0.02))  # in the rock: nothing to bump
    return m


def bones() -> KitMesh:
    """Bone and chitin remains strewn over the floor (within 1.2 m of the middle): long bones,
    two skulls, ribs and broken shell plates of the brood; walked over, no collision."""
    m = KitMesh()
    for k in range(11):  # long bones
        a = 2 * math.pi * _hash(k, 11)
        r = 0.25 + 0.9 * _hash(k, 12)
        x, z = r * math.cos(a), r * math.sin(a)
        t = 2 * math.pi * _hash(k, 13)
        d = (math.cos(t), 0.04, math.sin(t))
        length = 0.32 + 0.22 * _hash(k, 14)
        base = (x - d[0] * length / 2, 0.035, z - d[2] * length / 2)
        _spike(m, "bone", base, d, 0.028, 0.026, length, 0.0, 5, t)
        for end in (0.0, length):  # the knobbly ends
            p = (base[0] + d[0] * end, 0.04, base[2] + d[2] * end)
            _blob(m, "bone", p, (0.05, 0.04, 0.05), 6, 3)
    _skull(m, 0.55, 0.0, -0.35, 1)
    _skull(m, -0.7, 0.0, 0.5, -1)
    for k in range(6):  # ribs: thin arcs as short bent bones
        x0, z0 = -0.3 + 0.12 * k, 0.65 - 0.05 * k
        _spike(m, "bone", (x0, 0.02, z0), (0.15, 0.6, 0.45), 0.015, 0.012, 0.22, 0.0, 4)
    for k in range(8):  # chitin plates of dead brood
        a = 2 * math.pi * _hash(k, 21)
        r = 0.2 + 1.0 * _hash(k, 22)
        size = 0.12 + 0.12 * _hash(k, 23)
        _blob(m, "chitin", (r * math.cos(a), 0.02, r * math.sin(a)),
              (size, 0.035, size * 0.7), 8, 3)  # fmt: skip
    _no_body(m)
    return m


def slime() -> KitMesh:
    """Pools and trails of slime on the floor (1-2 cm thick, a few lumps); no collision."""
    m = KitMesh()
    b = m.b("slime")
    pools = ((0.0, 0.0, 0.95), (-0.85, 0.6, 0.45), (0.8, -0.7, 0.5), (0.6, 0.85, 0.32),
             (-0.75, -0.8, 0.3))  # fmt: skip
    for k, (cx, cz, r) in enumerate(pools):
        n = 14
        ring = []
        for i in range(n):
            a = 2 * math.pi * i / n
            rr = r * (0.7 + 0.45 * _hash(k, i))
            ring.append((cx + rr * math.cos(a), 0.012 + 0.002 * k, cz + rr * math.sin(a)))
        c = (cx, 0.016 + 0.002 * k, cz)
        for i in range(n):
            tri = [c, ring[i], ring[(i + 1) % n]]
            b.polygon(tri, [(p[0], p[2]) for p in tri], (0.0, 1.0, 0.0))
    for k in range(7):  # lumps
        a = 2 * math.pi * _hash(k, 31)
        r = 0.7 * _hash(k, 32)
        size = 0.06 + 0.08 * _hash(k, 33)
        _blob(m, "slime", (r * math.cos(a), 0.01, r * math.sin(a)), (size, size * 0.45, size), 8, 3)
    _no_body(m)
    return m


PILLAR_R = 0.9  # a pillar of the old vault: 1.8 m across, up into the rock overhead
PILLAR_H = 6.2


def pillar() -> KitMesh:
    """A thick ashlar column of the old vault: plinth, shaft of drums, up into the rock; egg
    sacks of the brood at its foot."""
    m = KitMesh()
    _drum(m, "ashlar", (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), PILLAR_R + 0.15, 0.4, 8, math.pi / 8)
    y = 0.4
    for k in range(8):  # drums of 0.75 m, each turned a little: the joints show
        _drum(m, "ashlar", (0.0, y, 0.0), (0.0, 1.0, 0.0), PILLAR_R, 0.75, 8,
              math.pi / 8 + 0.06 * (k % 3 - 1))  # fmt: skip
        y += 0.75
    for a, r in ((0.6, 0.2), (1.1, 0.16), (2.9, 0.18), (4.4, 0.15)):
        x, z = (PILLAR_R + 0.25) * math.cos(a), (PILLAR_R + 0.25) * math.sin(a)
        _blob(m, "egg", (x, r * 1.2, z), (r, r * 1.3, r))
        _blob(m, "slime", (x, 0.02, z), (r * 1.8, 0.03, r * 1.8), 8, 3)
    m.body("pillar", (-PILLAR_R - 0.15, 0.0, -PILLAR_R - 0.15), (PILLAR_R + 0.15, PILLAR_H,
                                                                  PILLAR_R + 0.15))  # fmt: skip
    return m


def pillar_broken() -> KitMesh:
    """A broken column: plinth and a stump of 2.3 m with a jagged top, two drums fallen beside
    it (cover for the hero, too low to hide the Kellermutter)."""
    m = KitMesh()
    _drum(m, "ashlar", (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), PILLAR_R + 0.15, 0.4, 8, math.pi / 8)
    _drum(m, "ashlar", (0.0, 0.4, 0.0), (0.0, 1.0, 0.0), PILLAR_R, 0.75, 8, math.pi / 8)
    _drum(m, "ashlar", (0.0, 1.15, 0.0), (0.0, 1.0, 0.0), PILLAR_R, 0.7, 8, 0.5)
    _drum(m, "ashlar", (0.15, 1.85, 0.1), (0.08, 1.0, 0.05), PILLAR_R * 0.75, 0.45, 8, 0.2)
    _drum(m, "ashlar", (0.55, 0.0, 0.9), (1.0, 0.0, 0.35), PILLAR_R * 0.85, 0.75, 8)  # fallen
    _drum(m, "ashlar", (-0.95, 0.0, 0.25), (-0.3, 0.0, 1.0), PILLAR_R * 0.8, 0.7, 8, 0.3)
    for k in range(6):  # chips
        a = 2 * math.pi * _hash(k, 41)
        r = 1.1 + 0.3 * _hash(k, 42)
        s = 0.12 + 0.12 * _hash(k, 43)
        x, z = r * math.cos(a), r * math.sin(a)
        m.box("ashlar", (x - s, 0.0, z - s * 0.8), (x + s, s * 1.1, z + s * 0.8))
    m.body("pillar_broken", (-1.05, 0.0, -1.05), (1.05, 2.3, 1.05))
    m.body("pillar_broken_drum", (-1.25, 0.0, 0.1), (1.45, 1.4, 1.35))
    return m


def rubble_heap() -> KitMesh:
    """Stones of the fallen vault in a heap up to 1.9 m, a broken rib across it."""
    m = KitMesh()
    _spike(m, "rubble", (0.0, -0.05, 0.0), (0.05, 1.0, 0.02), 1.3, 0.35, 1.5, 0.35, 9, 0.3)
    for k in range(16):  # blocks on the heap
        a = 2 * math.pi * _hash(k, 51)
        r = 1.25 * _hash(k, 52)
        h = 1.6 * (1 - r / 1.35) ** 0.8
        sx, sy, sz = (
            0.22 + 0.2 * _hash(k, 53),
            0.16 + 0.12 * _hash(k, 54),
            0.2 + 0.18 * _hash(k, 55),
        )
        x, z = r * math.cos(a), r * math.sin(a)
        mat = "ashlar" if k % 3 else "rubble"
        m.box(mat, (x - sx, max(h - sy, 0.0), z - sz), (x + sx, h + sy, z + sz))
    m.box("ashlar", (-1.4, 0.55, -0.18), (1.2, 0.9, 0.18), grain=0)  # the broken rib
    m.box("ashlar", (0.9, 0.2, -0.2), (1.3, 0.55, 0.2))
    m.body("rubble_heap", (-1.3, 0.0, -1.3), (1.3, 1.5, 1.3))
    return m


def gold_hoard() -> KitMesh:
    """The hoard: a heap of coins with goblets, a crowned casket and spilled coins."""
    m = KitMesh()
    _spike(m, "gold", (0.0, -0.02, 0.0), (0.0, 1.0, 0.0), 1.05, 0.3, 0.6, 0.18, 12)
    for k in range(40):  # coins on and round the heap
        a = 2 * math.pi * _hash(k, 61)
        r = 1.35 * _hash(k, 62) ** 0.7
        h = max(0.0, 0.75 * (1 - r / 1.05)) if r < 1.05 else 0.0
        x, z = r * math.cos(a), r * math.sin(a)
        m.box("gold", (x - 0.04, h, z - 0.04), (x + 0.04, h + 0.012, z + 0.04))
    for k, (x, z) in enumerate(((0.75, -0.55), (-0.8, 0.4), (0.25, 0.85))):  # goblets
        _drum(m, "gold", (x, 0.0, z), (0.0, 1.0, 0.0), 0.08, 0.02, 8)
        _drum(m, "gold", (x, 0.02, z), (0.0, 1.0, 0.0), 0.025, 0.14, 6)
        _spike(m, "gold", (x, 0.16, z), (0.0, 1.0, 0.0), 0.04, 0.09, 0.12, 0.0, 8, 0.1 * k)
    m.box("oak", (-1.05, 0.0, -0.85), (-0.45, 0.38, -0.45))  # a casket, iron bands
    m.box("iron", (-1.06, 0.0, -0.75), (-0.44, 0.39, -0.71))
    m.box("iron", (-1.06, 0.0, -0.59), (-0.44, 0.39, -0.55))
    m.box("gold", (-0.95, 0.38, -0.8), (-0.55, 0.45, -0.5))  # coins over its brim
    m.body("gold_hoard", (-1.1, 0.0, -1.0), (1.1, 0.8, 1.0))
    return m


PROPS = {"sarcophagus": sarcophagus, "bone_niche": bone_niche, "workbench": workbench,
         "cogwheel": cogwheel, "stalagmite": stalagmite, "stalactite": stalactite,
         "crystal_cluster": crystal_cluster, "crystal_ceiling": crystal_ceiling,
         "crystal_spire": crystal_spire, "egg_sacks": egg_sacks, "brood_glow": brood_glow,
         "brood_ceiling": brood_ceiling, "bones": bones, "slime": slime, "pillar": pillar,
         "pillar_broken": pillar_broken,
         "rubble_heap": rubble_heap, "gold_hoard": gold_hoard}  # fmt: skip

KIT = {
    "gate": gate,
    "secret_wall": secret_wall,
    "trapdoor": trapdoor,
    "lever": lever,
    "wall_button": wall_button,
    "niche": niche,
    "plate": plate,
    "teleport": teleport,
    "crystal_wall": crystal_wall,
    "crystal_switch": crystal_switch,
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
