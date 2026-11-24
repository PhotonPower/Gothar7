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
from gothar_worldgen.textures.procedural import TILE_M, albedo_scale, make, to_png

Vec3 = tuple[float, float, float]
TEXTURE_SIZE = 512
TEXTURE_DIR = "../../../../furniture/textures"  # from worlds/<site>/dungeon/<level or kit>/
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
                    (f"{TEXTURE_DIR}/{kind}_albedo.png", f"{TEXTURE_DIR}/{kind}_normal.png"),
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


KIT = {
    "gate": gate,
    "secret_wall": secret_wall,
    "trapdoor": trapdoor,
    "lever": lever,
    "wall_button": wall_button,
    "niche": niche,
    "plate": plate,
    "teleport": teleport,
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
