"""Level map -> ``ebene_NN.g7world`` (W8 D2, docs/design/schloss-dungeon.md sections 3.3-3.4, 5).

The map's cells are built per block of ``BLOCK`` x ``BLOCK`` cells into one mesh vob each
(``dungeon/<level>/block_<bx>_<by>.glb``): floor slabs, rubble walls where a walkable cell meets a
solid one, the vault, the flights of stairs and the trapdoor shafts. Collision as boxes: the solid
cells along the walls merged per row, floor and ceiling per row of cells, a ramp per flight.

The mechanics follow engine's contract (dungeon.md, world.md): ``mover`` vobs for gates, secret
walls and trapdoors (kit files), mobs ``lever``/``wall_button``/``niche``/``door``/``chest`` with
``targets``/``action``, plates and teleporters as triggers, ``changeWorld`` at the top or foot of
each flight and at the bottom of each shaft (``fall``), ``spawn`` markers, start points, wall
torches with their lights (at most ``TORCHES_PER_BLOCK`` per block), the waynet ``WP_DNG_E01_x_y``
with edges to the four neighbours, ``FP_ROAM_DNG_…``, music and ambient zones and the
``environment`` block (no sky). No indoor zones: without a sky the world's ambient is the light.

Same map, same bytes; the vob ids are kept per level in ``ebene_NN_vob_ids.json`` (keys are vob
names). Vobs that the generator does not own (editor work) stay in the file, as in Leonberg.
"""

from __future__ import annotations

import json
import math
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from gothar_worldgen.assemble.world import VobIds, write_world
from gothar_worldgen.dungeon import kit
from gothar_worldgen.dungeon.kit import KitMesh
from gothar_worldgen.dungeon.level import DIRS, FLOOR_CHARS, PROPS, Level, LevelError, _cell
from gothar_worldgen.dungeon.logic import check_reach
from gothar_worldgen.textures.procedural import DUNGEON_KINDS
from gothar_worldgen.waynet.write import tidy

BLOCK = 8  # cells per block side
RISE = 1.8  # a flight climbs or falls this much over one cell (31 degrees at 3 m)
STEPS = 9
SHAFT_M = 3.0  # trapdoor shaft below the floor; arrival shaft above the ceiling
SINK_M = 0.6  # an open secret wall sinks this far below the floor (clear of the floor slab)
SLAB = 0.3  # thickness of the collision slabs
TORCHES_PER_BLOCK = 8  # plan 3.4: the engine picks the nearest lights
ITEM_SPREAD_M = 0.4  # items on one cell lie this far apart
TORCH_GAP_CELLS = 3  # torches at least this many cells apart (Chebyshev)
LIGHT_TORCH = {"color": [1.0, 0.6, 0.3], "range": 7.0, "intensity": 2.0, "flicker": 0.35}
THEMES = {  # theme -> (ambient zone, environment)
    "gewoelbe": (
        "gewoelbe",
        {
            "ambient": [0.05, 0.048, 0.055],
            "fog_color": [0.02, 0.02, 0.025],
            "fog_start": 2.0,
            "fog_distance": 22.0,
        },
    ),
    "verlies": (
        "gewoelbe",
        {
            "ambient": [0.045, 0.045, 0.05],
            "fog_color": [0.018, 0.018, 0.02],
            "fog_start": 2.0,
            "fog_distance": 20.0,
        },
    ),
    "zisterne": (  # Ebene 3: engine's ambient "zisterne" (#494)
        "zisterne",
        {
            "ambient": [0.04, 0.047, 0.055],
            "fog_color": [0.016, 0.02, 0.024],
            "fog_start": 2.0,
            "fog_distance": 18.0,
        },
    ),
    "beinhaus": (  # Ebene 4: engine's ambient "beinhaus" (#494)
        "beinhaus",
        {
            "ambient": [0.045, 0.05, 0.042],
            "fog_color": [0.02, 0.022, 0.018],
            "fog_start": 2.0,
            "fog_distance": 18.0,
        },
    ),
    "grotte": (  # Ebene 7: crystal light, a faint blue fill; engine's ambient "grotte" (#517)
        "grotte",
        {
            "ambient": [0.1, 0.14, 0.2],
            "ambient_ground": [0.05, 0.075, 0.1],
            "fog_color": [0.025, 0.045, 0.07],
            "fog_start": 4.0,
            "fog_distance": 30.0,
        },
    ),
    "hort": (  # Ebene 8: the Kellermutter's lair, warm and close; engine's ambient "hort" (#522)
        "hort",
        {
            "ambient": [0.1, 0.075, 0.058],
            "ambient_ground": [0.05, 0.036, 0.026],
            "fog_color": [0.03, 0.02, 0.014],
            "fog_start": 3.0,
            "fog_distance": 26.0,
        },
    ),
    "labyrinth": (  # Ebene 6: engine's ambient "labyrinth" (#514)
        "labyrinth",
        {
            "ambient": [0.042, 0.045, 0.05],
            "fog_color": [0.017, 0.018, 0.021],
            "fog_start": 2.0,
            "fog_distance": 16.0,
        },
    ),
    "werkstatt": (  # Ebene 5: the builders' workshops, engine's ambient "werkstatt" (#502)
        "werkstatt",
        {
            "ambient": [0.05, 0.047, 0.042],
            "fog_color": [0.022, 0.02, 0.017],
            "fog_start": 2.0,
            "fog_distance": 20.0,
        },
    ),
}
# floor, ceiling, ribs every RIB_CELLS cells (ashlar arches or oak beams), inlays of planks
# and iron plates (owner 2026-11-26: floor and ceiling unlike the walls)
SURFACES = {
    "gewoelbe": ("flag", "vault_brick", "ashlar", False),
    "verlies": ("flag", "vault_rock", "oak_beam", False),
    "zisterne": ("flag_damp", "vault_brick", "ashlar", False),
    "beinhaus": ("flag_dust", "vault_rock", "ashlar", False),
    "werkstatt": ("flag", "vault_brick", "oak_beam", True),
    "labyrinth": ("flag", "vault_brick", "ashlar", False),
    "grotte": ("cave_floor", "cave_ceiling", None, False),  # a natural cave: no ribs
    "hort": ("flag_damp", "vault_rock", "ashlar", False),  # slime-damp flags, rough vault
}
WALLS = {"grotte": "cave_wall"}  # wall material per theme (else rubble)
CAVE_THEMES = {"grotte"}  # rough rock walls and ceilings instead of masonry (W8 E7)
ROUGH_WALL_M = 0.7  # the rock steps back from the cell's edge by up to this much
ROUGH_CEIL_M = 0.9  # and rises above the ceiling's height by up to this much
ROUGH_ALONG, ROUGH_UP, ROUGH_CEIL = 10, 12, 8  # subdivisions of a wall face and a ceiling cell
ROUGH_FADE_M = 0.6  # the rock eases to the cell's edge over this far (floor, corners, walls)
LIGHT_CRYSTAL = {"color": [0.35, 0.8, 1.0], "range": 10.0, "intensity": 9.0, "flicker": 0.0}
# the brood's glow (W8 E8): warm orange like the Kellermutter's weak spot, a slow flicker
LIGHT_BROOD = {"color": [1.0, 0.5, 0.18], "range": 13.0, "intensity": 12.0, "flicker": 0.12}
LIGHT_GOLD = {"color": [1.0, 0.72, 0.38], "range": 8.0, "intensity": 6.0, "flicker": 0.0}
PROP_LIGHT_AT = {  # kind -> (light, out from the wall, up from the prop's origin)
    "crystal_cluster": (LIGHT_CRYSTAL, 0.9, 1.3),
    "crystal_ceiling": (LIGHT_CRYSTAL, 0.0, -1.2),
    "crystal_spire": (LIGHT_CRYSTAL, 0.0, None),  # just above the water's surface
    "brood_glow": (LIGHT_BROOD, 0.8, 1.5),
    "brood_ceiling": (LIGHT_BROOD, 0.0, -1.7),
    "gold_hoard": (LIGHT_GOLD, 0.0, 1.8),  # the hoard catches the eye
}
LIGHTS_PER_BLOCK = 8  # plan 3.4, torches and crystals together
DROP_M = 3.3  # a fall into the same level arrives this high above the floor (below 4 m: unhurt)
RIB_CELLS = 2  # a rib every 6 m
RIB_W, RIB_D = 0.36, 0.24  # width across the boundary, depth below the ceiling
CORBEL_D, CORBEL_H = 0.28, 0.75  # where a rib meets the wall
MUSIC = "DUNGEON"
KIT_PATH = "worlds/{site}/dungeon/kit/{name}.glb"
SIDE_WANT = {"N": (0.0, 0.0, 1.0), "S": (0.0, 0.0, -1.0), "W": (1.0, 0.0, 0.0),
             "E": (-1.0, 0.0, 0.0)}  # fmt: skip  # normal of the wall face on that side, inwards
OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
Cell = tuple[int, int]


class DungeonError(ValueError):
    pass


def _quat_front(fx: float, fz: float) -> list[float]:
    """Rotation about +Y turning the model's front (+Z) into (fx, fz)."""
    a = math.atan2(fx, fz)
    return [0.0, tidy(math.sin(a / 2)), 0.0, tidy(math.cos(a / 2))]


def _quat_look(fx: float, fz: float) -> list[float]:
    """Rotation about +Y turning the view (-Z, engine convention) into (fx, fz)."""
    return _quat_front(-fx, -fz)


def _rotate(q: list[float], p: tuple[float, float, float]) -> tuple[float, float, float]:
    """A yaw quaternion applied to ``p``."""
    a = 2 * math.atan2(q[1], q[3])
    c, s = math.cos(a), math.sin(a)
    return (c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2])


@dataclass
class Plan:
    """What the generator writes: block meshes (name -> mesh) and vob specs in order."""

    blocks: dict[tuple[int, int], KitMesh] = field(default_factory=dict)
    vobs: list[dict[str, Any]] = field(default_factory=list)
    points: list[dict[str, Any]] = field(default_factory=list)
    edges: list[list[str]] = field(default_factory=list)
    freepoints: list[dict[str, Any]] = field(default_factory=list)
    zones: list[dict[str, Any]] = field(default_factory=list)
    environment: dict[str, Any] = field(default_factory=dict)


class _Gen:
    def __init__(self, level: Level, site: str) -> None:
        self.lv = level
        self.site = site
        self.c = level.cell_m
        self.h = level.height_m
        self.plan = Plan()
        self.walk = set(level.cells(FLOOR_CHARS))
        self.wells: dict[Cell, Cell] = {level.well(at): at for at in level.cells("<>")}
        self.shafts = {st["at"] for st in level.starts if st.get("shaft")}
        self.drops = {
            st["name"] for at in level.cells("P") if (st := level.same_level_fall(at)) is not None
        }
        self.tag = level.tag
        floor, ceiling, rib, inlay = SURFACES.get(level.theme, SURFACES["gewoelbe"])
        self.floor_mat, self.ceil_mat, self.rib_mat, self.inlay = floor, ceiling, rib, inlay
        self.wall_mat = WALLS.get(level.theme, "rubble")
        # rough rock: the whole level (``CAVE_THEMES``) or its ``cave`` cells (W8 E8)
        self.caves = set(self.walk) if level.theme in CAVE_THEMES else set(level.cave)
        self.corner_h: dict[tuple[int, int], float] = {}
        if level.heights:  # corner (gx, gz) -> mean lift of the land cells round it
            land = {a for a in self.walk if level.char(*a) != "~"}
            for gz in range(level.height + 1):
                for gx in range(level.width + 1):
                    round_ = [(gx - i, gz - j) for j in (1, 0) for i in (1, 0)]
                    dys = [level.dy(a) for a in round_ if a in land]
                    if dys:
                        self.corner_h[(gx, gz)] = sum(dys) / len(dys)

    # --- geometry helpers ---------------------------------------------------------------------
    def x0(self, x: int) -> float:
        return self.lv.origin[0] + x * self.c

    def z0(self, y: int) -> float:
        return self.lv.origin[2] + y * self.c

    def mid(self, at: Cell) -> tuple[float, float]:
        return (self.x0(at[0]) + self.c / 2, self.z0(at[1]) + self.c / 2)

    def mesh(self, at: Cell) -> KitMesh:
        """The block of a cell (cells beyond the map's edge: the nearest block)."""
        x = min(max(at[0], 0), self.lv.width - 1)
        y = min(max(at[1], 0), self.lv.height - 1)
        key = (x // BLOCK, y // BLOCK)
        if key not in self.plan.blocks:
            o = self.lv.origin
            self.plan.blocks[key] = KitMesh(
                (o[0] + key[0] * BLOCK * self.c, o[1], o[2] + key[1] * BLOCK * self.c)
            )
        return self.plan.blocks[key]

    def y(self, v: float) -> float:
        return self.lv.origin[1] + v

    def base(self, at: Cell) -> float:
        """A water cell's bottom, else 0 (the floor before ``heights``)."""
        return -self.lv.depth(at) if self.lv.char(*at) == "~" else 0.0

    def floor(self, at: Cell) -> float:
        """The floor's height in the middle of the cell."""
        if self.lv.char(*at) == "~" or not self.corner_h:
            return self.base(at)
        ha, hb, hc, hd = self.corners(at)
        return max(ha + hc, hb + hd) / 2  # the upper diagonal (the collision hull's top)

    def corners(self, at: Cell) -> tuple[float, float, float, float]:
        """Floor heights at (x0, z0), (x1, z0), (x1, z1), (x0, z1) of a land cell."""
        x, y = at
        g = self.corner_h
        return (g.get((x, y), 0.0), g.get((x + 1, y), 0.0), g.get((x + 1, y + 1), 0.0),
                g.get((x, y + 1), 0.0))  # fmt: skip

    def edge_floor(self, at: Cell, side: str) -> tuple[float, float]:
        """Floor heights at the two ends of ``side`` (left, right seen from inside, as
        ``edge``)."""
        if self.lv.char(*at) == "~" or not self.corner_h:
            return (self.base(at), self.base(at))
        ha, hb, hc, hd = self.corners(at)
        return {"N": (hb, ha), "S": (hd, hc), "W": (ha, hd), "E": (hc, hb)}[side]

    def ceil(self, at: Cell) -> float:
        if at in self.wells and self.lv.char(*self.wells[at]) == "<":
            return self.h + RISE
        return self.h

    def low(self, at: Cell) -> float:
        if at in self.wells:
            return -RISE if self.lv.char(*self.wells[at]) == ">" else 0.0
        if self.lv.char(*at) == "P":
            return -SHAFT_M
        return self.floor(at)

    def connected(self, a: Cell, b: Cell) -> bool:
        if a in self.walk and b in self.walk:
            return True
        return self.wells.get(a) == b or self.wells.get(b) == a

    def open_cell(self, at: Cell) -> bool:
        return at in self.walk or at in self.wells

    def edge(self, at: Cell, side: str) -> tuple[tuple[float, float], tuple[float, float]]:
        """The two ends (x, z) of a cell's edge on ``side``, left to right seen from inside."""
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        x1, z1 = x0 + self.c, z0 + self.c
        return {
            "N": ((x1, z0), (x0, z0)),
            "S": ((x0, z1), (x1, z1)),
            "W": ((x0, z0), (x0, z1)),
            "E": ((x1, z1), (x1, z0)),
        }[side]

    def face(
        self,
        at: Cell,
        side: str,
        y0: float,
        y1: float,
        material: str | None = None,
        a: float = 0.0,
        b: float = 1.0,
    ) -> None:
        """A wall face on ``side`` of ``at`` facing into it, from ``a`` to ``b`` (0..1) along
        (``material`` None: the theme's wall)."""
        material = material or self.wall_of(at)
        (xa, za), (xb, zb) = self.edge(at, side)
        p = (xa + (xb - xa) * a, za + (zb - za) * a)
        q = (xa + (xb - xa) * b, za + (zb - za) * b)
        pts = [
            (p[0], self.y(y0), p[1]),
            (q[0], self.y(y0), q[1]),
            (q[0], self.y(y1), q[1]),
            (p[0], self.y(y1), p[1]),
        ]
        self.mesh(at).quad(material, pts, SIDE_WANT[side])

    def flat(
        self,
        at: Cell,
        y: float,
        up: bool,
        material: str,
        rect: tuple[float, float, float, float] | None = None,
    ) -> None:
        """A floor (``up``) or ceiling quad over the cell or ``rect`` (x0, z0, x1, z1)."""
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        x0, z0, x1, z1 = rect or (x0, z0, x0 + self.c, z0 + self.c)
        yy = self.y(y)
        pts = [(x0, yy, z0), (x1, yy, z0), (x1, yy, z1), (x0, yy, z1)]
        self.mesh(at).quad(material, pts, (0.0, 1.0 if up else -1.0, 0.0))

    # --- cells --------------------------------------------------------------------------------
    def build(self) -> Plan:
        for at in sorted(self.walk | set(self.wells), key=lambda a: (a[1], a[0])):
            self.cell(at)
        self.ribs()
        self.collision()
        self.merge_empty()
        self.mechanics()
        self.torches()
        self.check_lights()
        self.waynet()
        self.zones()
        return self.plan

    def floor_material(self, at: Cell) -> str:
        """Flagstones; in the workshops some cells have planks or an iron plate instead."""
        if at in self.lv.jetty:
            return "oak"
        if self.inlay and self.lv.char(*at) == ".":
            k = (at[0] * 7 + at[1] * 13) % 9
            if k == 0:
                return "oak"
            if k == 4:
                return "plate"
        if at in self.caves:
            return "cave_floor"
        return self.floor_mat

    def wall_of(self, at: Cell) -> str:
        return "cave_wall" if at in self.caves else self.wall_mat

    def _ceiled(self, at: Cell) -> bool:
        return at in self.walk and at not in self.shafts and self.ceil(at) == self.h

    def ribs(self) -> None:
        """Ribs under the ceiling every ``RIB_CELLS`` cells across the boundary of two ceiled
        cells: cross arches in a corridor, a grid in a room; a corbel where a rib meets a wall."""
        if self.rib_mat is None:  # a cave has none
            return
        c, top = self.c, self.y(self.h)
        hw = RIB_W / 2
        for at in sorted(self.walk, key=lambda a: (a[1], a[0])):
            for dx, dy in ((1, 0), (0, 1)):
                nb = (at[0] + dx, at[1] + dy)
                k = nb[0] if dx else nb[1]
                if k % RIB_CELLS or not (self._ceiled(at) and self._ceiled(nb)):
                    continue
                if at in self.caves or nb in self.caves:  # the collapsed part: rock
                    continue
                mesh = self.mesh(at)
                if dx:  # a rib along z on the line x = x0(nb)
                    x = self.x0(nb[0])
                    z0, z1 = self.z0(at[1]), self.z0(at[1]) + c
                    mesh.box(self.rib_mat, (x - hw, top - RIB_D, z0), (x + hw, top, z1), grain=2)
                    ends = (((at[0], at[1] - 1), (nb[0], nb[1] - 1), z0, 1.0),
                            ((at[0], at[1] + 1), (nb[0], nb[1] + 1), z1, -1.0))  # fmt: skip
                    for a, b, z, s in ends:
                        if not self.open_cell(a) and not self.open_cell(b):
                            za, zb = sorted((z, z + s * CORBEL_D))
                            mesh.box(self.rib_mat, (x - hw, top - RIB_D - CORBEL_H, za),
                                     (x + hw, top - RIB_D, zb), grain=1)  # fmt: skip
                else:  # a rib along x on the line z = z0(nb)
                    z = self.z0(nb[1])
                    x0, x1 = self.x0(at[0]), self.x0(at[0]) + c
                    mesh.box(self.rib_mat, (x0, top - RIB_D, z - hw), (x1, top, z + hw), grain=0)
                    ends = (((at[0] - 1, at[1]), (nb[0] - 1, nb[1]), x0, 1.0),
                            ((at[0] + 1, at[1]), (nb[0] + 1, nb[1]), x1, -1.0))  # fmt: skip
                    for a, b, x, s in ends:
                        if not self.open_cell(a) and not self.open_cell(b):
                            xa, xb = sorted((x, x + s * CORBEL_D))
                            mesh.box(self.rib_mat, (xa, top - RIB_D - CORBEL_H, z - hw),
                                     (xb, top - RIB_D, z + hw), grain=1)  # fmt: skip

    def merge_empty(self) -> None:
        """Blocks with collision only (walls beyond the last floor) give their bodies to the
        nearest block with something to draw."""
        import numpy as np

        from gothar_worldgen.buildings.gltf import CollisionPart

        full = [k for k, m in self.plan.blocks.items() if m.triangles()]
        for key in sorted(k for k, m in self.plan.blocks.items() if not m.triangles()):
            src = self.plan.blocks.pop(key)
            dst_key = min(full, key=lambda k: ((k[0] - key[0]) ** 2 + (k[1] - key[1]) ** 2, k))
            dst = self.plan.blocks[dst_key]
            shift = np.array(
                [s - d for s, d in zip(src.origin, dst.origin, strict=True)], dtype=np.float32
            )
            for part in src.collision:
                dst.collision.append(CollisionPart(part.name, part.positions + shift, part.indices))

    def cell(self, at: Cell) -> None:
        lv = self.lv
        ch = lv.char(*at)
        if at in self.wells:
            self.flight(at)
        elif ch == "P":
            self.shaft_down(at)
        elif self.corner_h and ch != "~":
            self.floor_tris(at, self.floor_material(at))
        else:
            self.flat(at, self.floor(at), True, self.floor_material(at))
        if at in self.shafts:
            self.shaft_up(at)
        elif at in self.caves and self._ceiled(at):
            self.rough_ceiling(at)
        else:
            self.flat(at, self.ceil(at), False, self.ceil_mat)
        for side, (dx, dy) in DIRS.items():
            nb = (at[0] + dx, at[1] + dy)
            if self.connected(at, nb):
                if self.ceil(nb) > self.ceil(at):
                    self.face(nb, OPPOSITE[side], self.ceil(at), self.ceil(nb))
                if at not in self.wells and nb not in self.wells and self.base(nb) < self.base(at):
                    mat = "oak_beam" if at in lv.jetty else None
                    self.face(nb, OPPOSITE[side], self.base(nb), self.base(at), mat)
                continue
            back = at in self.wells and nb == (
                2 * at[0] - self.wells[at][0],
                2 * at[1] - self.wells[at][1],
            )
            if lv.char(*nb) == "N" and at not in self.wells and self._faces(nb) == at:
                self.niche_face(at, side)
            elif self._rough(at, side):
                self.rough_face(at, side)
            else:
                self.face(at, side, self.low(at), self.ceil(at), "dark" if back else None)
        if ch == "D":
            self.door_frame(at)
        if at in lv.jetty:
            self.jetty_posts(at)

    # --- the cave (W8 E7) ---------------------------------------------------------------------
    @staticmethod
    def hash3(i: int, j: int, k: int) -> float:
        """0..1 per integer lattice point (integer hash: same bytes on every platform)."""
        h = ((i * 73856093) ^ (j * 19349663) ^ (k * 83492791)) & 0xFFFFFFFF
        h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
        h ^= h >> 16
        return h / 2**32

    @classmethod
    def noise(cls, x: float, y: float, z: float) -> float:
        """0..1 per point, from the position to the millimetre."""
        return cls.hash3(round(x * 1000), round(y * 1000), round(z * 1000))

    @classmethod
    def smooth_noise(cls, x: float, y: float, z: float, cell: float) -> float:
        """Value noise (0..1) on a lattice of ``cell`` metres, smoothly interpolated."""
        fx, fy, fz = x / cell, y / cell, z / cell
        i, j, k = math.floor(fx), math.floor(fy), math.floor(fz)
        u, v, w = fx - i, fy - j, fz - k
        u, v, w = u * u * (3 - 2 * u), v * v * (3 - 2 * v), w * w * (3 - 2 * w)
        out = 0.0
        for a in (0, 1):
            for b in (0, 1):
                for c in (0, 1):
                    wt = (u if a else 1 - u) * (v if b else 1 - v) * (w if c else 1 - w)
                    out += wt * cls.hash3(i + a, j + b, k + c)
        return out

    def rock(self, x: float, y: float, z: float) -> float:
        """0..1: broad bulges and hollows, smaller knobs, a little grit."""
        return (0.45 * self.smooth_noise(x, y, z, 1.4) + 0.35 * self.smooth_noise(x, y, z, 0.55)
                + 0.2 * self.noise(x, y, z))  # fmt: skip

    def grid_mesh(self, at: Cell, material: str, grid: list[list[tuple[float, float, float]]],
                  want: tuple[float, float, float]) -> None:  # fmt: skip
        """A displaced grid (rows of points) as triangles with smooth normals; texture projected
        like ``KitMesh.quad`` (world metres along the axis of ``want``)."""
        b = self.mesh(at).b(material)
        ni, nj = len(grid), len(grid[0])
        axis = max(range(3), key=lambda k: abs(want[k]))
        base = len(b.pos)
        for i in range(ni):
            for j in range(nj):
                p = grid[i][j]
                a0, a1 = grid[max(i - 1, 0)][j], grid[min(i + 1, ni - 1)][j]
                b0, b1 = grid[i][max(j - 1, 0)], grid[i][min(j + 1, nj - 1)]
                tu = (a1[0] - a0[0], a1[1] - a0[1], a1[2] - a0[2])
                tv = (b1[0] - b0[0], b1[1] - b0[1], b1[2] - b0[2])
                n = (tu[1] * tv[2] - tu[2] * tv[1], tu[2] * tv[0] - tu[0] * tv[2],
                     tu[0] * tv[1] - tu[1] * tv[0])  # fmt: skip
                if n[0] * want[0] + n[1] * want[1] + n[2] * want[2] < 0:
                    n = (-n[0], -n[1], -n[2])
                length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
                b.pos.append((p[0] - b.ox, p[1] - b.oy, p[2] - b.oz))
                b.nrm.append((n[0] / length, n[1] / length, n[2] / length))
                if axis == 1:
                    b.uv.append((p[0], p[2]))
                else:
                    b.uv.append((p[2] if axis == 0 else p[0], p[1]))
        p0, p1, p2 = grid[0][0], grid[1][0], grid[1][1]
        e1 = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
        e2 = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                 e1[0] * e2[1] - e1[1] * e2[0])  # fmt: skip
        flip = cross[0] * want[0] + cross[1] * want[1] + cross[2] * want[2] < 0
        for i in range(ni - 1):
            for j in range(nj - 1):
                q00, q10 = base + i * nj + j, base + (i + 1) * nj + j
                q11, q01 = q10 + 1, q00 + 1
                for tri in ((q00, q10, q11), (q00, q11, q01)):
                    b.idx += [tri[0], tri[2], tri[1]] if flip else list(tri)

    def _plain_wall(self, at: Cell, side: str) -> bool:
        """``side`` of the open cell ``at`` is a plain wall face (no niche, no stairs)."""
        dx, dy = DIRS[side]
        nb = (at[0] + dx, at[1] + dy)
        if self.open_cell(nb) or at in self.wells or self.lv.char(*at) == "P":
            return False
        return not (self.lv.char(*nb) == "N" and self._faces(nb) == at)

    def _rough(self, at: Cell, side: str) -> bool:
        return at in self.caves and self._plain_wall(at, side)

    def _rough_column(self, at: Cell, side: str, end: int) -> bool:
        """May the rock step back at this end of the face? Only where the wall runs on straight
        into the next cell's rough face with the same floor (no gaps at corners)."""
        along = {"N": (-1, 0), "S": (1, 0), "W": (0, 1), "E": (0, -1)}[side]
        s = 1 if end else -1
        q = (at[0] + s * along[0], at[1] + s * along[1])
        return q in self.walk and self._rough(q, side) and self.base(q) == self.base(at)

    def rough_face(self, at: Cell, side: str) -> None:
        """A wall face of rock: a fine grid whose points step back up to ``ROUGH_WALL_M`` into
        the solid cell (bulges, hollows, overhangs; the collision stays the cell's box), easing
        to the edge at the floor, the ceiling and corners; smooth normals."""
        (xa, za), (xb, zb) = self.edge(at, side)
        fa, fb = self.edge_floor(at, side)
        top = self.ceil(at)
        want = SIDE_WANT[side]
        ok = (self._rough_column(at, side, 0), self._rough_column(at, side, 1))
        na, nu = ROUGH_ALONG, ROUGH_UP
        grid: list[list[tuple[float, float, float]]] = []
        for i in range(na + 1):
            t = i / na
            x = xa if i == 0 else xb if i == na else xa + (xb - xa) * t
            z = za if i == 0 else zb if i == na else za + (zb - za) * t
            bottom = fa if i == 0 else fb if i == na else fa + (fb - fa) * t
            ease = 1.0
            if not ok[0]:
                ease = min(ease, t * self.c / ROUGH_FADE_M)
            if not ok[1]:
                ease = min(ease, (1 - t) * self.c / ROUGH_FADE_M)
            col = []
            for j in range(nu + 1):
                y = bottom if j == 0 else top if j == nu else bottom + (top - bottom) * j / nu
                up = min(1.0, (y - bottom) / ROUGH_FADE_M, (top - y) / ROUGH_FADE_M)
                k = min(ease, up)
                k = k * k * (3 - 2 * k)
                d = ROUGH_WALL_M * k * self.rock(x, y, z) if k > 0 else 0.0
                col.append((x - want[0] * d, self.y(y), z - want[2] * d))
            grid.append(col)
        self.grid_mesh(at, "cave_wall", grid, want)

    def _ceiling_ease(self, x: float, z: float) -> float:
        """0 on the edge of anything that is not plain cave ceiling, 1 from ``ROUGH_FADE_M``
        away (a function of the position: neighbouring cells agree)."""
        c, o = self.c, self.lv.origin
        reach = ROUGH_FADE_M
        near = reach
        for gx in range(math.floor((x - o[0] - reach) / c), math.floor((x - o[0] + reach) / c) + 1):
            for gz in range(math.floor((z - o[2] - reach) / c),
                            math.floor((z - o[2] + reach) / c) + 1):  # fmt: skip
                if self._ceiled((gx, gz)) and (gx, gz) in self.caves:
                    continue
                x0, z0 = self.x0(gx), self.z0(gz)
                dx = max(x0 - x, 0.0, x - (x0 + c))
                dz = max(z0 - z, 0.0, z - (z0 + c))
                near = min(near, math.hypot(dx, dz))
        k = near / reach
        return k * k * (3 - 2 * k)

    def rough_ceiling(self, at: Cell) -> None:
        """The rock overhead: a fine grid rising up to ``ROUGH_CEIL_M`` above the ceiling's
        height, level where it meets a wall or anything that is not plain cave ceiling."""
        n, c = ROUGH_CEIL, self.c
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        grid = []
        for i in range(n + 1):
            row = []
            x = x0 if i == 0 else x0 + c if i == n else x0 + c * i / n
            for j in range(n + 1):
                z = z0 if j == 0 else z0 + c if j == n else z0 + c * j / n
                k = self._ceiling_ease(x, z)
                d = ROUGH_CEIL_M * k * self.rock(x, self.h, z) if k > 0 else 0.0
                row.append((x, self.y(self.h + d), z))
            grid.append(row)
        self.grid_mesh(at, "cave_ceiling", grid, (0.0, -1.0, 0.0))

    def floor_tris(self, at: Cell, material: str) -> None:
        """A floor lifted by ``heights``: two triangles over the cell's corners, split along the
        upper diagonal like the collision hull."""
        ha, hb, hc, hd = self.corners(at)
        if ha == hb == hc == hd:
            self.flat(at, ha, True, material)
            return
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        x1, z1 = x0 + self.c, z0 + self.c
        a, b = (x0, self.y(ha), z0), (x1, self.y(hb), z0)
        c, d = (x1, self.y(hc), z1), (x0, self.y(hd), z1)
        tris = [(a, b, c), (a, c, d)] if ha + hc >= hb + hd else [(a, b, d), (b, c, d)]
        for tri in tris:
            self.mesh(at).quad(material, list(tri), (0.0, 1.0, 0.0))

    def jetty_posts(self, at: Cell) -> None:
        """Oak posts under a jetty's corners that stand in the water."""
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        for i, j in ((0, 0), (1, 0), (0, 1), (1, 1)):
            near = [(at[0] + i - a, at[1] + j - b) for a in (0, 1) for b in (0, 1)]
            water = [n for n in near if self.lv.char(*n) == "~"]
            if not water:
                continue
            bottom = -max(self.lv.depth(n) for n in water)
            wx, wz = self.mid(water[0])  # just off the corner, in the water
            cx, cz = x0 + i * self.c, z0 + j * self.c
            px = cx + math.copysign(0.14, wx - cx)
            pz = cz + math.copysign(0.14, wz - cz)
            self.mesh(at).box("oak_beam", (px - 0.1, self.y(bottom), pz - 0.1),
                              (px + 0.1, self.y(0.08), pz + 0.1), grain=1)  # fmt: skip

    def _faces(self, wall: Cell) -> Cell:
        dx, dy = DIRS[self.lv.facing(wall)]
        return (wall[0] + dx, wall[1] + dy)

    def niche_face(self, at: Cell, side: str) -> None:
        """The wall face with the niche's opening in its middle."""
        c, hw = self.c, kit.NICHE_W / 2
        a, b = 0.5 - hw / c, 0.5 + hw / c
        y0, y1 = kit.NICHE_Y, kit.NICHE_Y + kit.NICHE_H
        self.face(at, side, 0.0, self.h, a=0.0, b=a)
        self.face(at, side, 0.0, self.h, a=b, b=1.0)
        self.face(at, side, 0.0, y0, a=a, b=b)
        self.face(at, side, y1, self.h, a=a, b=b)

    def flight(self, well: Cell) -> None:
        """Steps from the stair cell into the wall cell behind it, up (``<``) or down (``>``)."""
        stair = self.wells[well]
        up = self.lv.char(*stair) == "<"
        dx, dy = well[0] - stair[0], well[1] - stair[1]
        m = self.mesh(well)
        x0, z0 = self.x0(well[0]), self.z0(well[1])
        c = self.c
        t, r = c / STEPS, RISE / STEPS

        def rect(d0: float, d1: float) -> tuple[float, float, float, float]:
            """Cell part from ``d0`` to ``d1`` metres away from the stair cell."""
            if dx == 1:
                return (x0 + d0, z0, x0 + d1, z0 + c)
            if dx == -1:
                return (x0 + c - d1, z0, x0 + c - d0, z0 + c)
            if dy == 1:
                return (x0, z0 + d0, x0 + c, z0 + d1)
            return (x0, z0 + c - d1, x0 + c, z0 + c - d0)

        for k in range(STEPS):
            top = (k + 1) * r if up else -(k + 1) * r
            bottom = k * r if up else -RISE - 0.05
            ax, az, bx, bz = rect(k * t, c if up else (k + 1) * t)
            m.box("ashlar", (ax, self.y(bottom), az), (bx, self.y(top), bz))
        top_end = RISE if up else -RISE
        base = -SLAB if up else -RISE - SLAB
        ax, az, bx, bz = rect(0.0, c)
        near = [(ax, az), (bx, az), (bx, bz), (ax, bz)]

        def height(px: float, pz: float) -> float:
            d = {1: px - x0, -1: x0 + c - px}.get(dx) if dx else {1: pz - z0, -1: z0 + c - pz}[dy]
            return top_end * (d / c)

        pts = [(px, self.y(base), pz) for px, pz in near]
        pts += [
            (px, self.y(max(height(px, pz), 0.0) if up else min(height(px, pz), 0.0)), pz)
            for px, pz in near
        ]
        m.hull(f"flight_{well[0]}_{well[1]}", pts)

    def shaft_down(self, at: Cell) -> None:
        """A trapdoor's hole: floor round it, the shaft's walls, its bottom."""
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        c, w = self.c, kit.TRAP_W
        a, b = (c - w) / 2, (c + w) / 2
        for r in (
            (x0, z0, x0 + c, z0 + a),
            (x0, z0 + b, x0 + c, z0 + c),
            (x0, z0 + a, x0 + a, z0 + b),
            (x0 + b, z0 + a, x0 + c, z0 + b),
        ):
            self.flat(at, 0.0, True, self.floor_mat, r)
        self.flat(at, -SHAFT_M, True, self.floor_mat, (x0 + a, z0 + a, x0 + b, z0 + b))
        m = self.mesh(at)
        ya, yb = self.y(-SHAFT_M), self.y(0.0)
        ring = [(x0 + a, z0 + a), (x0 + b, z0 + a), (x0 + b, z0 + b), (x0 + a, z0 + b)]
        for k in range(4):
            (pa, qa), (pb, qb) = ring[k], ring[(k + 1) % 4]
            cx, cz = x0 + c / 2, z0 + c / 2
            want = (cx - (pa + pb) / 2, 0.0, cz - (qa + qb) / 2)
            m.quad(self.wall_mat, [(pa, ya, qa), (pb, ya, qb), (pb, yb, qb), (pa, yb, qa)], want)

    def shaft_up(self, at: Cell) -> None:
        """An arrival shaft in the vault (the fall from the level above ends here)."""
        x0, z0 = self.x0(at[0]), self.z0(at[1])
        c, w = self.c, kit.TRAP_W
        a, b = (c - w) / 2, (c + w) / 2
        h = self.h
        for r in (
            (x0, z0, x0 + c, z0 + a),
            (x0, z0 + b, x0 + c, z0 + c),
            (x0, z0 + a, x0 + a, z0 + b),
            (x0 + b, z0 + a, x0 + c, z0 + b),
        ):
            self.flat(at, h, False, self.wall_mat, r)
        self.flat(at, h + SHAFT_M, False, self.wall_mat, (x0 + a, z0 + a, x0 + b, z0 + b))
        m = self.mesh(at)
        ya, yb = self.y(h), self.y(h + SHAFT_M)
        ring = [(x0 + a, z0 + a), (x0 + b, z0 + a), (x0 + b, z0 + b), (x0 + a, z0 + b)]
        for k in range(4):
            (pa, qa), (pb, qb) = ring[k], ring[(k + 1) % 4]
            cx, cz = x0 + c / 2, z0 + c / 2
            want = (cx - (pa + pb) / 2, 0.0, cz - (qa + qb) / 2)
            m.quad(self.wall_mat, [(pa, ya, qa), (pb, ya, qb), (pb, yb, qb), (pa, yb, qa)], want)

    def door_frame(self, at: Cell) -> None:
        """The wall across a door cell with the doorway in its middle (ashlar)."""
        m = self.mesh(at)
        cx, cz = self.mid(at)
        c, hw, t = self.c, kit.DOOR_W / 2 + 0.05, kit.FRAME_T / 2
        top = kit.DOOR_H + 0.1
        y0, y1, yt = self.y(0.0), self.y(self.h), self.y(top)
        if self.lv.passage(at) == "x":  # the wall runs along z
            parts = [
                ((cx - t, y0, cz - c / 2), (cx + t, y1, cz - hw)),
                ((cx - t, y0, cz + hw), (cx + t, y1, cz + c / 2)),
                ((cx - t, yt, cz - hw), (cx + t, y1, cz + hw)),
            ]
        else:
            parts = [
                ((cx - c / 2, y0, cz - t), (cx - hw, y1, cz + t)),
                ((cx + hw, y0, cz - t), (cx + c / 2, y1, cz + t)),
                ((cx - hw, yt, cz - t), (cx + hw, y1, cz + t)),
            ]
        for k, (lo, hi) in enumerate(parts):
            m.box("ashlar", lo, hi)
            m.body(f"frame_{at[0]}_{at[1]}_{k}", lo, hi)

    # --- collision ----------------------------------------------------------------------------
    def _runs(self, cells: set[Cell]) -> Iterator[tuple[Cell, int]]:
        """Runs of cells along x within one block row: (first cell, length)."""
        for y in sorted({c[1] for c in cells}):
            xs = sorted(c[0] for c in cells if c[1] == y)
            start = prev = None
            for x in [*xs, None]:
                if x is not None and prev is not None and x == prev + 1 and x % BLOCK != 0:
                    prev = x
                    continue
                if start is not None and prev is not None:
                    yield (start, y), prev - start + 1
                start = prev = x

    def collision(self) -> None:
        lv = self.lv
        top = self.h + RISE + 0.5
        bottom = -SHAFT_M - 0.6
        solid = set()
        for at in self.walk | set(self.wells):
            for dx, dy in DIRS.values():
                nb = (at[0] + dx, at[1] + dy)
                if not self.open_cell(nb):
                    solid.add(nb)
        niches = {at for at in solid if lv.char(*at) == "N"}
        for (x, y), n in self._runs(solid - niches):
            lo = (self.x0(x), self.y(bottom), self.z0(y))
            hi = (self.x0(x + n), self.y(top), self.z0(y) + self.c)
            self.mesh((x, y)).body(f"wall_{x}_{y}", lo, hi)
        for at in sorted(niches):
            self.niche_bodies(at, bottom, top)
        plain = {a for a in self.walk if lv.char(*a) not in "P~"}
        lifted = {a for a in plain if any(self.corners(a))} if self.corner_h else set()
        for (x, y), n in self._runs(plain - lifted):
            m = self.mesh((x, y))
            x0, x1, z0, z1 = self.x0(x), self.x0(x + n), self.z0(y), self.z0(y) + self.c
            m.body(f"floor_{x}_{y}", (x0, self.y(-SLAB), z0), (x1, self.y(0.0), z1))
        for at in sorted(lifted, key=lambda a: (a[1], a[0])):  # ``heights``: a hull per cell
            ha, hb, hc, hd = self.corners(at)
            x0, z0 = self.x0(at[0]), self.z0(at[1])
            x1, z1 = x0 + self.c, z0 + self.c
            low = self.y(min(ha, hb, hc, hd) - SLAB)
            self.mesh(at).hull(f"floor_{at[0]}_{at[1]}", [
                (x0, low, z0), (x1, low, z0), (x1, low, z1), (x0, low, z1),
                (x0, self.y(ha), z0), (x1, self.y(hb), z0), (x1, self.y(hc), z1),
                (x0, self.y(hd), z1)])  # fmt: skip
        for depth, cells in self._water_groups():
            for (x, y), n in self._runs(cells):
                x0, x1, z0, z1 = self.x0(x), self.x0(x + n), self.z0(y), self.z0(y) + self.c
                self.mesh((x, y)).body(
                    f"water_{x}_{y}",
                    (x0, self.y(-depth - SLAB), z0),
                    (x1, self.y(-depth), z1),
                )
        for at in sorted(self.lv.cells("P")):
            x0, z0 = self.x0(at[0]), self.z0(at[1])
            c, w = self.c, kit.TRAP_W
            a, b = (c - w) / 2, (c + w) / 2
            yb, yt = self.y(-SHAFT_M - SLAB), self.y(0.0)
            m = self.mesh(at)
            for k, (r0, r1, s0, s1) in enumerate(
                ((0, c, 0, a), (0, c, b, c), (0, a, a, b), (b, c, a, b))
            ):
                m.body(f"shaft_{at[0]}_{at[1]}_{k}", (x0 + r0, yb, z0 + s0), (x0 + r1, yt, z0 + s1))
            m.body(
                f"shaft_{at[0]}_{at[1]}_bottom",
                (x0 + a, yb, z0 + a),
                (x0 + b, self.y(-SHAFT_M), z0 + b),
            )
        vault = {a for a in self.walk | set(self.wells) if a not in self.shafts}
        for (x, y), n in self._runs(vault):
            # cells of a run share the ceiling only if equal; flights up are split off
            cells = [(x + k, y) for k in range(n)]
            groups: list[list[Cell]] = []
            for cc in cells:
                if groups and self.ceil(groups[-1][-1]) == self.ceil(cc):
                    groups[-1].append(cc)
                else:
                    groups.append([cc])
            for g in groups:
                h = self.ceil(g[0])
                x0, x1, z0 = self.x0(g[0][0]), self.x0(g[-1][0] + 1), self.z0(y)
                self.mesh(g[0]).body(
                    f"vault_{g[0][0]}_{y}", (x0, self.y(h), z0), (x1, self.y(h + SLAB), z0 + self.c)
                )

    def _water_groups(self) -> list[tuple[float, set[Cell]]]:
        """Water cells by depth (the deep basin first, then ``water.shallow``)."""
        groups: dict[float, set[Cell]] = {}
        for at in self.lv.cells("~"):
            groups.setdefault(self.lv.depth(at), set()).add(at)
        return sorted(groups.items(), key=lambda kv: -kv[0])

    def niche_bodies(self, at: Cell, bottom: float, top: float) -> None:
        """A niche's wall cell: solid behind the hollow, the front round its opening."""
        fx, fz = DIRS[self.lv.facing(at)]  # the hollow opens towards the floor it faces
        x0, z0, c = self.x0(at[0]), self.z0(at[1]), self.c
        d, hw = kit.NICHE_D, kit.NICHE_W / 2
        y0, y1 = kit.NICHE_Y, kit.NICHE_Y + kit.NICHE_H

        def box(u0: float, u1: float, v0: float, v1: float, ya: float, yb: float, k: str) -> None:
            """``u`` along the face from its left end, ``v`` depth from the face inwards."""
            if fz:
                zf = z0 + c if fz > 0 else z0
                za, zb = sorted((zf - fz * v0, zf - fz * v1))
                lo, hi = (x0 + u0, self.y(ya), za), (x0 + u1, self.y(yb), zb)
            else:
                xf = x0 + c if fx > 0 else x0
                xa, xb = sorted((xf - fx * v0, xf - fx * v1))
                lo, hi = (xa, self.y(ya), z0 + u0), (xb, self.y(yb), z0 + u1)
            self.mesh(at).body(f"niche_{at[0]}_{at[1]}_{k}", lo, hi)

        box(0.0, c, d, c, bottom, top, "back")
        box(0.0, c / 2 - hw, 0.0, d, bottom, top, "l")
        box(c / 2 + hw, c, 0.0, d, bottom, top, "r")
        box(c / 2 - hw, c / 2 + hw, 0.0, d, bottom, y0, "low")
        box(c / 2 - hw, c / 2 + hw, 0.0, d, y1, top, "high")

    # --- vobs ---------------------------------------------------------------------------------
    def vob(
        self,
        kind: str,
        name: str,
        pos: tuple[float, float, float],
        rot: list[float],
        **rest: Any,  # noqa: ANN401
    ) -> None:
        self.plan.vobs.append(
            {"type": kind, "name": name, "pos": [tidy(v) for v in pos], "rot": rot, **rest}
        )

    def kit_mesh(self, name: str) -> str:
        return KIT_PATH.format(site=self.site, name=name)

    def spec(self, at: Cell) -> dict[str, Any]:
        lv = self.lv
        from gothar_worldgen.dungeon.level import ENTRY

        return dict(lv.entries.get(at, {}).get(ENTRY[lv.char(*at)], {}))

    def mechanics(self) -> None:
        lv = self.lv
        ident = [0.0, 0.0, 0.0, 1.0]
        for at in sorted(set(lv.names().values()), key=lambda a: (a[1], a[0])):
            ch, name, spec = lv.char(*at), lv.name(at), self.spec(at)
            cx, cz = self.mid(at)
            y0 = self.y(self.floor(at))
            if ch == "G":
                rot = _quat_front(1.0, 0.0) if lv.passage(at) == "x" else ident
                mover = {
                    "kind": "gate",
                    "open": bool(spec.get("open", False)),
                    "seconds": float(spec.get("seconds", 2.0)),
                    "crush": spec.get("crush", "stop"),
                }
                self.vob(
                    "mesh",
                    name,
                    (cx, y0, cz),
                    rot,
                    mesh=self.kit_mesh("gate"),
                    category="gameplay",
                    components={"mover": mover},
                )
            elif ch == "H":
                mover = {
                    "kind": "secret_wall",
                    "open": bool(spec.get("open", False)),
                    "seconds": float(spec.get("seconds", 3.0)),
                    "offset": [0.0, -tidy(self.h + SINK_M), 0.0],
                    "crush": spec.get("crush", "stop"),
                }
                look = "crystal_wall" if spec.get("look") == "crystal" else "secret_wall"
                self.vob(
                    "mesh",
                    name,
                    (cx, y0, cz),
                    ident,
                    mesh=self.kit_mesh(look),
                    category="gameplay",
                    components={"mover": mover},
                )
            elif ch == "P":
                mover = {
                    "kind": "trapdoor",
                    "open": bool(spec.get("open", False)),
                    "seconds": float(spec.get("seconds", 0.6)),
                    "crush": "stop",
                }
                self.vob(
                    "mesh",
                    name,
                    (cx, y0, cz - kit.TRAP_W / 2),
                    ident,
                    mesh=self.kit_mesh("trapdoor"),
                    category="gameplay",
                    components={"mover": mover},
                )
                if spec.get("plate"):
                    # down to 1 m into the shaft: it stays pressed while the lid falls away
                    self.vob(
                        "trigger",
                        f"PLATE_{self.tag}_{at[0]}_{at[1]}",
                        (cx, y0 - 0.25, cz),
                        ident,
                        components={
                            "trigger": {
                                "shape": "box",
                                "halfExtents": [1.0, 0.75, 1.0],
                                "filter": "any",
                                "once": False,
                                "targets": [name],
                                "action": "open",
                                "plate": {"weight": spec["plate"]},
                            }
                        },
                    )
                fall = spec["fall"]
                arrival = lv.same_level_fall(at)
                if arrival is not None:  # down into this level (W8 E7): a silent teleport
                    tele = {"to": arrival["name"], "visible": False, "filter": "player",
                            "active": True, "silent": True, "keep_velocity": True}  # fmt: skip
                    self.vob(
                        "trigger",
                        f"FALL_{self.tag}_{at[0]}_{at[1]}",
                        (cx, self.y(-SHAFT_M + 1.0), cz),
                        ident,
                        components={
                            "trigger": {
                                "shape": "box",
                                "halfExtents": [0.95, 0.8, 0.95],
                                "filter": "player",
                                "once": False,
                                "teleport": tele,
                            }
                        },
                    )
                    continue
                self.vob(
                    "trigger",
                    f"FALL_{self.tag}_{at[0]}_{at[1]}",
                    (cx, self.y(-SHAFT_M + 1.0), cz),
                    ident,
                    components={
                        "trigger": {
                            "shape": "box",
                            "halfExtents": [0.95, 0.8, 0.95],
                            "filter": "player",
                            "once": False,
                            "changeWorld": {
                                "world": fall["world"],
                                "start": fall["start"],
                                "fall": True,
                            },
                        }
                    },
                )
            elif ch == "D":
                self.door(at, name, spec)
            elif ch == "S":
                self.wall_mob(at, name, spec, spec.get("type", "lever"))
            elif ch == "N":
                self.wall_mob(at, name, spec, "niche")
            elif ch == "O" and spec.get("once"):  # fires once on entering, never sends back
                if not spec.get("hidden"):
                    self.vob("mesh", f"{name}_STEIN", (cx, y0, cz), ident,
                             mesh=self.kit_mesh("plate"))  # fmt: skip
                weight = spec.get("weight", "any")
                # 1.6 m deep along the way (its edge a good 2 m from a gate next door), across
                # the whole cell: nobody slips past along the wall
                along_x = lv.is_floor(at[0] - 1, at[1]) and lv.is_floor(at[0] + 1, at[1])
                half = [0.8, 1.0, self.c / 2] if along_x else [self.c / 2, 1.0, 0.8]
                self.vob(
                    "trigger",
                    name,
                    (cx, y0 + 1.0, cz),
                    ident,
                    components={
                        "trigger": {
                            "shape": "box",
                            "halfExtents": half,
                            "filter": "player" if weight == "player" else "any",
                            "once": True,
                            "targets": list(spec["targets"]),
                            "action": spec.get("action", "trigger"),
                        }
                    },
                )
            elif ch == "O":
                self.vob("mesh", f"{name}_STEIN", (cx, y0, cz), ident, mesh=self.kit_mesh("plate"))
                plate: dict[str, Any] = {"weight": spec.get("weight", "any")}
                if spec.get("release_action"):
                    plate["release_action"] = spec["release_action"]
                self.vob(
                    "trigger",
                    name,
                    (cx, y0 + 0.25, cz),
                    ident,
                    components={
                        "trigger": {
                            "shape": "box",
                            "halfExtents": [0.8, 0.25, 0.8],
                            "filter": "any",
                            "once": False,
                            "targets": list(spec["targets"]),
                            "action": spec.get("action", "trigger"),
                            "plate": plate,
                        }
                    },
                )
            elif ch == "T":
                self.teleport(at, name, spec)
            elif ch in "<>":
                self.stairs(at, name, spec)
            elif ch == "*":
                self.chest(at, name, spec)
            elif ch == "m":
                self.spawn(name, at, spec)
        for depth, cells in self._water_groups():  # one water box per row run and depth
            some = next(iter(cells))
            hy = (depth - lv.edge(some)) / 2  # the surface ``edgeM`` below the edge
            for (x, y), n in self._runs(cells):
                cx = (self.x0(x) + self.x0(x + n)) / 2
                cz = self.mid((x, y))[1]
                self.vob(
                    "water",
                    f"WATER_{self.tag}_{x}_{y}",
                    (cx, self.y(-depth + hy), cz),
                    ident,
                    components={
                        "water": {"halfExtents": [n * self.c / 2, hy, self.c / 2], "kind": "pond"}
                    },
                )
        for st in lv.starts:
            at = st["at"]
            cx, cz = self.mid(at)
            look = DIRS[st.get("facing", "S")]
            y = self.h + 1.0 if st.get("shaft") else self.floor(at)
            if st["name"] in self.drops:  # under the vault's shaft, low enough to land unhurt
                y = min(self.h - 0.2, self.floor(at) + DROP_M)
            self.vob("start", st["name"], (cx, self.y(y), cz), _quat_look(look[0], look[1]))
        for at, sp in sorted(lv.spawns.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            self.spawn(lv.spawn_name(at), at, sp)
        self.relays()
        self.items()
        self.props()

    def spawn(self, name: str, at: Cell, sp: dict[str, Any]) -> None:
        """A monster marker; ``active`` false (a wave of the boss fight) and ``targets`` (sent
        when all its monsters are dead) only when set (engine2, W8 E8)."""
        cx, cz = self.mid(at)
        look = DIRS[sp.get("facing", "S")]
        spawn: dict[str, Any] = {
            "npc": sp["npc"],
            "count": int(sp.get("count", 1)),
            "respawn": bool(sp.get("respawn", False)),
            "dormant": bool(sp.get("dormant", True)),
        }
        if sp.get("targets"):  # engine's writer order: targets, action, active (#522)
            spawn["targets"], spawn["action"] = list(sp["targets"]), sp.get("action", "trigger")
        if sp.get("active", True) is False:
            spawn["active"] = False
        self.vob("spawn", name, (cx, self.y(self.floor(at)), cz), _quat_look(look[0], look[1]),
                 components={"spawn": spawn})  # fmt: skip

    def props(self) -> None:
        """Still pieces (``props``): a sarcophagus in the middle of its cell, a bone niche
        against the wall behind it, a stalactite under the ceiling; mesh vobs
        ``PROP_E04_<x>_<y>`` with their collision. A crystal cluster brings its light
        ``LIGHT_E07_<x>_<y>`` (unless ``light`` is false)."""
        for at, pr in sorted(self.lv.props.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            side = pr.get("facing", "S")
            dx, dy = DIRS[side]
            cx, cz = self.mid(at)
            place = PROPS[pr["kind"]]
            y = self.floor(at)
            if place == "wall":  # on the wall behind the cell
                cx, cz = cx - dx * self.c / 2, cz - dy * self.c / 2
                if self.corner_h:
                    y = sum(self.edge_floor(at, OPPOSITE[side])) / 2
            elif place == "ceiling":
                y = self.ceil(at)
            elif place in ("water", "floor"):
                y = self.floor(at)
            rot = _quat_front(dx, dy)
            self.vob("mesh", self.lv.prop_name(at), (cx, self.y(y), cz), rot,
                     mesh=self.kit_mesh(pr["kind"]))  # fmt: skip
            if pr["kind"] in PROP_LIGHT_AT and pr.get("light", True):
                light, out, up = PROP_LIGHT_AT[pr["kind"]]
                ly = y + up if up is not None else -self.lv.edge(at) + 0.4
                self.vob("light", f"LIGHT_{self.tag}_{at[0]}_{at[1]}",
                         (cx + dx * out, self.y(ly), cz + dy * out), rot,
                         components={"light": dict(light)})  # fmt: skip

    def relays(self) -> None:
        """Invisible relays (``relays``): above their cell ``at`` (else the level's origin)."""
        for name, r in sorted(self.lv.relays.items()):
            if "at" in r:
                cx, cz = self.mid(_cell(r["at"], name))
            else:
                cx, cz = self.lv.origin[0], self.lv.origin[2]
            relay: dict[str, Any] = {"mode": r.get("mode", "any")}
            if relay["mode"] == "count":
                relay["count"] = int(r["count"])
            relay["delay"] = tidy(float(r.get("delay", 0.0)))
            relay["once"] = bool(r.get("once", False))
            relay["invert"] = bool(r.get("invert", False))
            relay["targets"] = list(r["targets"])
            relay["action"] = r.get("action", "trigger")
            self.vob("relay", name, (cx, self.y(2.0), cz), [0.0, 0.0, 0.0, 1.0],
                     components={"relay": relay})  # fmt: skip

    def items(self) -> None:
        """Item vobs (``items``, the depots' finds): side by side across the cell's middle."""
        per_cell: dict[Cell, int] = {}
        for it in self.lv.items:
            at = it["at"]
            k = per_cell.get(at, 0)
            per_cell[at] = k + 1
            cx, cz = self.mid(at)
            off = ITEM_SPREAD_M * ((k + 1) // 2) * (1 if k % 2 else -1)
            item: dict[str, Any] = {"instance": it["instance"]}
            if int(it.get("count", 1)) != 1:
                item["count"] = int(it["count"])
            self.vob("item", f"ITEM_{self.tag}_{at[0]}_{at[1]}_{k}",
                     (cx + off, self.y(self.floor(at) + 0.05), cz), [0.0, 0.0, 0.0, 1.0],
                     components={"item": item})  # fmt: skip

    def door(self, at: Cell, name: str, spec: dict[str, Any]) -> None:
        cx, cz = self.mid(at)
        hw = kit.DOOR_W / 2
        if self.lv.passage(at) == "x":  # front +X: the blade runs from the hinge towards -z
            rot, hinge = _quat_front(1.0, 0.0), (cx, self.y(0.0), cz + hw)
        else:  # front +Z: the blade towards +x
            rot, hinge = [0.0, 0.0, 0.0, 1.0], (cx - hw, self.y(0.0), cz)
        mob: dict[str, Any] = {"definition": spec.get("definition", "door")}
        if spec.get("open"):
            mob["open"] = True
        if spec.get("targets"):
            mob["targets"], mob["action"] = list(spec["targets"]), spec.get("action", "trigger")
        if spec.get("no_pick"):
            mob["no_pick"] = True
        if spec.get("one_way"):
            mob["one_way"] = spec["one_way"]
        self.vob("mob", name, hinge, rot, mesh="mobs/door.glb", components={"mob": mob})

    def wall_mob(self, at: Cell, name: str, spec: dict[str, Any], typ: str) -> None:
        """A lever, button or niche on the face of its wall cell towards the floor."""
        dx, dy = DIRS[self.lv.facing(at)]
        cx, cz = self.mid(at)
        stand = self.lv.dy((at[0] + dx, at[1] + dy))  # level with its neighbours (``heights``)
        pos = (cx + dx * self.c / 2, self.y(stand), cz + dy * self.c / 2)
        mob: dict[str, Any] = {"definition": spec.get("definition", typ)}
        if spec.get("targets"):
            mob["targets"], mob["action"] = list(spec["targets"]), spec.get("action", "trigger")
        if spec.get("once"):
            mob["once"] = True
        if spec.get("hidden"):
            mob["hidden"] = True
        look = "crystal_switch" if spec.get("look") == "crystal" and typ == "wall_button" else typ
        self.vob(
            "mob", name, pos, _quat_front(dx, dy), mesh=self.kit_mesh(look), components={"mob": mob}
        )

    def teleport(self, at: Cell, name: str, spec: dict[str, Any]) -> None:
        cx, cz = self.mid(at)
        ident = [0.0, 0.0, 0.0, 1.0]
        y0 = self.y(self.floor(at))
        if not spec.get("trap"):  # a trap shows nothing: no ring, no column
            self.vob("mesh", f"{name}_RING", (cx, y0, cz), ident, mesh=self.kit_mesh("teleport"))
        filt = spec.get("filter", "player")
        tele: dict[str, Any] = {}
        if "to" in spec or spec.get("spin"):  # a spinner: its own cell, turned to ``facing``
            to = _cell(spec["to"], name) if "to" in spec else at
            tx, tz = self.mid(to)
            look = DIRS[spec.get("facing", "S")]
            self.vob(
                "empty",
                f"{name}_ZIEL",
                (tx, self.y(self.floor(to)), tz),
                _quat_look(look[0], look[1]),
            )
            tele["to"] = f"{name}_ZIEL"
        else:
            tele["world"], tele["start"] = spec["world"], spec["start"]
        tele["visible"] = bool(spec.get("visible", True)) and not spec.get("trap")
        tele["filter"] = filt
        tele["active"] = bool(spec.get("active", True))
        self.vob(
            "trigger",
            name,
            (cx, y0 + 1.0, cz),
            ident,
            components={
                "trigger": {
                    "shape": "box",
                    "halfExtents": [1.0, 1.0, 1.0],
                    "filter": "player" if filt == "player" else "any",
                    "once": False,
                    "teleport": tele,
                }
            },
        )

    def stairs(self, at: Cell, name: str, spec: dict[str, Any]) -> None:
        """The change of world at the far end of the flight and the arrival on the stair cell."""
        well = self.lv.well(at)
        up = self.lv.char(*at) == "<"
        dx, dy = well[0] - at[0], well[1] - at[1]
        wx, wz = self.mid(well)
        c = self.c
        end = RISE * 0.8 if up else -RISE * 0.8
        centre = (wx + dx * c * 0.3, self.y(end + 1.0), wz + dy * c * 0.3)
        half = [0.45, 1.2, c / 2 - 0.2] if dx else [c / 2 - 0.2, 1.2, 0.45]
        self.vob(
            "trigger",
            name,
            centre,
            [0.0, 0.0, 0.0, 1.0],
            components={
                "trigger": {
                    "shape": "box",
                    "halfExtents": half,
                    "filter": "player",
                    "once": False,
                    "changeWorld": {"world": spec["world"], "start": spec["start"]},
                }
            },
        )
        cx, cz = self.mid(at)
        self.vob(
            "start", self.lv.entries[at]["arrive"], (cx, self.y(0.0), cz), _quat_look(-dx, -dy)
        )

    def chest(self, at: Cell, name: str, spec: dict[str, Any]) -> None:
        cx, cz = self.mid(at)
        pos, front = (cx, cz), (0.0, 1.0)
        for side in ("N", "W", "E", "S"):
            dx, dy = DIRS[side]
            if not self.open_cell((at[0] + dx, at[1] + dy)):
                pos = (cx + dx * (self.c / 2 - 0.45), cz + dy * (self.c / 2 - 0.45))
                front = (-dx, -dy)
                break
        mob: dict[str, Any] = {"definition": spec.get("definition", "chest")}
        if spec.get("targets"):
            mob["targets"], mob["action"] = list(spec["targets"]), spec.get("action", "trigger")
        self.vob(
            "mob",
            name,
            (pos[0], self.y(self.floor(at)), pos[1]),
            _quat_front(front[0], front[1]),
            mesh="mobs/chest.glb",
            components={"mob": mob},
        )

    def torches(self) -> None:
        """Wall torches on plain floor cells by a wall, at least ``TORCH_GAP_CELLS`` apart,
        at most ``TORCHES_PER_BLOCK`` per block; ``torches`` in the JSON adds [x, y, side]."""
        from gothar_worldgen.mobs import torch_flame

        lv = self.lv
        busy = set()  # wall faces with something on them
        for at in lv.cells("SN"):
            busy.add((self._faces(at), OPPOSITE[lv.facing(at)]))
        for at, pr in lv.props.items():
            if PROPS[pr["kind"]] == "wall":
                busy.add((at, OPPOSITE[pr.get("facing", "S")]))
        picked: list[tuple[Cell, str]] = []
        extra = [((int(t[0]), int(t[1])), str(t[2])) for t in lv.raw.get("torches", [])]
        for at, side in extra:
            dx, dy = DIRS[side]
            if at not in self.walk or self.open_cell((at[0] + dx, at[1] + dy)):
                raise LevelError(f"torch {list(at)} {side}: needs floor with a wall on that side")
            picked.append((at, side))
        per_block: dict[tuple[int, int], int] = {}
        for at, _ in picked:
            k = (at[0] // BLOCK, at[1] // BLOCK)
            per_block[k] = per_block.get(k, 0) + 1
        gap = int(lv.raw.get("torchGapCells", TORCH_GAP_CELLS))
        if not lv.raw.get("noAutoTorches"):
            for at in sorted(
                (a for a in self.walk if lv.char(*a) in ".m*"), key=lambda a: (a[1], a[0])
            ):
                k = (at[0] // BLOCK, at[1] // BLOCK)
                if per_block.get(k, 0) >= TORCHES_PER_BLOCK:
                    continue
                if any(max(abs(at[0] - p[0]), abs(at[1] - p[1])) < gap for p, _ in picked):
                    continue
                for side in ("N", "W", "E", "S"):
                    dx, dy = DIRS[side]
                    nb = (at[0] + dx, at[1] + dy)
                    if self.open_cell(nb) or lv.char(*nb) != "#" or (at, side) in busy:
                        continue
                    picked.append((at, side))
                    per_block[k] = per_block.get(k, 0) + 1
                    break
        for at, side in picked:
            dx, dy = DIRS[side]
            cx, cz = self.mid(at)
            rot = _quat_front(-dx, -dy)
            pos = (cx + dx * self.c / 2, self.y(self.floor(at)), cz + dy * self.c / 2)
            name = f"TORCH_{self.tag}_{at[0]}_{at[1]}_{side}"
            self.vob("mesh", name, pos, rot, mesh="props/wall_torch.glb")
            f = _rotate(rot, torch_flame())
            self.vob(
                "light",
                f"LIGHT_{self.tag}_{at[0]}_{at[1]}_{side}",
                (pos[0] + f[0], pos[1] + f[1], pos[2] + f[2]),
                rot,
                components={"light": {**LIGHT_TORCH, **lv.raw.get("torchLight", {})}},
            )

    def check_lights(self) -> None:
        """At most ``LIGHTS_PER_BLOCK`` lights (torches and crystals) per block (plan 3.4)."""
        per: dict[tuple[int, int], int] = {}
        for v in self.plan.vobs:
            if v["type"] == "light":  # LIGHT_E07_<x>_<y>[_<side>]: the block of its cell
                x, y = (int(t) for t in v["name"].split("_")[2:4])
                k = (x // BLOCK, y // BLOCK)
                per[k] = per.get(k, 0) + 1
        over = sorted(k for k, n in per.items() if n > LIGHTS_PER_BLOCK)
        if over:
            raise DungeonError(f"{self.tag}: more than {LIGHTS_PER_BLOCK} lights in blocks {over}")

    def wp(self, at: Cell) -> str:
        return f"WP_DNG_{self.tag}_{at[0]}_{at[1]}"

    def waynet(self) -> None:
        walk = self.walk - self.lv.blocked  # a sarcophagus fills its cell
        for at in sorted(walk, key=lambda a: (a[1], a[0])):
            cx, cz = self.mid(at)
            self.plan.points.append(
                {"name": self.wp(at), "pos": [cx, self.y(self.floor(at)), cz], "owner": "worldgen"}
            )
            for dx, dy in ((1, 0), (0, 1)):
                nb = (at[0] + dx, at[1] + dy)
                if nb in walk:
                    self.plan.edges.append([self.wp(at), self.wp(nb), "worldgen"])
        roam = set(self.lv.cells("m"))
        for at in walk:  # room cells: all eight neighbours walkable
            if self.lv.char(*at) == "." and all(
                (at[0] + i, at[1] + j) in walk for i in (-1, 0, 1) for j in (-1, 0, 1)
            ):
                roam.add(at)
        for at in sorted(roam, key=lambda a: (a[1], a[0])):
            cx, cz = self.mid(at)
            self.plan.freepoints.append(
                {
                    "name": f"FP_ROAM_DNG_{self.tag}_{at[0]}_{at[1]}",
                    "pos": [cx, self.y(self.floor(at)), cz],
                    "owner": "worldgen",
                }
            )

    def zones(self) -> None:
        lv = self.lv
        ambient, env = THEMES.get(lv.theme, THEMES["gewoelbe"])
        env = {**env, **lv.raw.get("environment", {})}
        self.plan.environment = {"sky": False, "ambient": env["ambient"]}
        if "ambient_ground" in env:
            self.plan.environment["ambient_ground"] = env["ambient_ground"]
        self.plan.environment.update(
            {
                "fog_color": env["fog_color"],
                "fog_start": env["fog_start"],
                "fog_distance": env["fog_distance"],
            }
        )
        w, d = lv.width * self.c, lv.height * self.c
        top, bottom = self.h + RISE + SHAFT_M, -SHAFT_M - RISE
        box = {
            "center": [lv.origin[0] + w / 2, self.y((top + bottom) / 2), lv.origin[2] + d / 2],
            "halfExtents": [w / 2, (top - bottom) / 2, d / 2],
            "yaw": 0.0,
        }
        self.plan.zones.append({"type": "music", "value": lv.raw.get("music", MUSIC), "box": box})
        self.plan.zones.append(
            {"type": "ambient", "value": lv.raw.get("ambient", ambient), "box": box}
        )
        for i, f in enumerate(lv.raw.get("fog", [])):  # fog zones over cell rectangles
            (ax, ay), (bx, by) = _cell(f["from"], f"fog[{i}]"), _cell(f["to"], f"fog[{i}]")
            x0, z0 = self.x0(min(ax, bx)), self.z0(min(ay, by))
            x1, z1 = self.x0(max(ax, bx) + 1), self.z0(max(ay, by) + 1)
            self.plan.zones.append(
                {
                    "type": "fog",
                    "value": str(f.get("name", f"NEBEL_{self.tag}_{i + 1}")),
                    "box": {
                        "center": [(x0 + x1) / 2, self.y(self.h / 2), (z0 + z1) / 2],
                        "halfExtents": [(x1 - x0) / 2, self.h / 2, (z1 - z0) / 2],
                        "yaw": 0.0,
                    },
                    "color": f["color"],
                    "distance": float(f["distance"]),
                    "start": float(f.get("start", 0.0)),
                    "fade": float(f.get("fade", 3.0)),
                }
            )


def plan_level(level: Level, site: str) -> Plan:
    reach = check_reach(level)
    if reach.unreached or reach.stuck:
        raise DungeonError(f"{level.tag}: unreached {reach.unreached}, no way back {reach.stuck}")
    return _Gen(level, site).build()


def _owned(ids: set[int]) -> list[Any]:
    """Ids as single values and closed ranges (world.md "Generator-Kopf")."""
    out: list[Any] = []
    for i in sorted(ids):
        if out and isinstance(out[-1], list) and out[-1][1] == i - 1:
            out[-1][1] = i
        elif out and isinstance(out[-1], int) and out[-1] == i - 1:
            out[-1] = [out[-1], i]
        else:
            out.append(i)
    return out


def _vob_json(vid: int, spec: dict[str, Any]) -> dict[str, Any]:
    """In the key order of the engine writer (world_format vobJson)."""
    v: dict[str, Any] = {
        "id": vid,
        "type": spec["type"],
        "name": spec["name"],
        "pos": spec["pos"],
        "rot": spec["rot"],
    }
    if "mesh" in spec:
        v["mesh"] = spec["mesh"]
        if spec["type"] == "mesh" and spec.get("category") == "gameplay":
            v["category"] = "gameplay"
    comps = spec.get("components")
    if comps:
        v["components"] = _components(spec["type"], comps)
    return v


def _components(kind: str, comps: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    if "light" in comps:
        lt = comps["light"]
        out["light"] = {
            "color": lt["color"],
            "range": tidy(lt["range"]),
            "intensity": tidy(lt["intensity"]),
            "flicker": tidy(lt["flicker"]),
        }
    if "trigger" in comps:
        t = comps["trigger"]
        tr: dict[str, Any] = {
            "shape": "box",
            "halfExtents": [tidy(v) for v in t["halfExtents"]],
            "filter": t.get("filter", "player"),
            "once": bool(t.get("once")),
        }
        if t.get("targets"):
            tr["targets"], tr["action"] = t["targets"], t.get("action", "trigger")
        if "teleport" in t:
            tr["teleport"] = t["teleport"]
        if "plate" in t:
            tr["plate"] = t["plate"]
        if "changeWorld" in t:
            cw = t["changeWorld"]
            tr["changeWorld"] = {"world": cw["world"], "start": cw["start"]}
            if cw.get("fall"):
                tr["changeWorld"]["fall"] = True
        out["trigger"] = tr
    if "mob" in comps:
        m = comps["mob"]
        mob: dict[str, Any] = {"definition": m["definition"]}
        for key in ("open", "targets", "action", "once", "hidden", "no_pick", "one_way"):
            if m.get(key):
                mob[key] = m[key]
        out["mob"] = mob
    if "mover" in comps:
        mv = comps["mover"]
        mover: dict[str, Any] = {
            "kind": mv["kind"],
            "open": bool(mv["open"]),
            "seconds": tidy(mv["seconds"]),
        }
        for key in ("offset", "axis"):
            if key in mv:
                mover[key] = [tidy(v) for v in mv[key]]
        if "angle" in mv:
            mover["angle"] = tidy(mv["angle"])
        mover["crush"] = mv.get("crush", "stop")
        out["mover"] = mover
    if "spawn" in comps:
        out["spawn"] = comps["spawn"]
    if "relay" in comps:
        out["relay"] = comps["relay"]
    if "item" in comps:
        out["item"] = comps["item"]
    if "water" in comps:
        w = comps["water"]
        out["water"] = {"halfExtents": [tidy(v) for v in w["halfExtents"]], "kind": w["kind"]}
    return out


def build_world(
    level: Level, plan: Plan, site: str, ids: VobIds, existing: dict[str, Any] | None
) -> dict[str, Any]:
    """The world document: the generator's vobs with stable ids, editor vobs kept."""
    name = f"ebene_{level.number:02d}"
    floor = max([ids.next_id] + [int(v["id"]) + 1 for v in (existing or {}).get("vobs", [])])
    owned_before = ids.owned
    vobs: dict[int, dict[str, Any]] = {}
    for (bx, by), mesh in sorted(plan.blocks.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        key = f"BLOCK_{level.tag}_{bx}_{by}"
        vid = ids.get(key, floor)
        vobs[vid] = _vob_json(
            vid,
            {
                "type": "mesh",
                "name": key,
                "pos": [tidy(v) for v in mesh.origin],
                "rot": [0.0, 0.0, 0.0, 1.0],
                "mesh": f"worlds/{site}/dungeon/{name}/block_{bx}_{by}.glb",
                "category": "gameplay",
            },
        )
    names = set()
    for spec in plan.vobs:
        if spec["name"] in names:
            raise DungeonError(f"{level.tag}: vob name {spec['name']} twice")
        names.add(spec["name"])
        vid = ids.get(spec["name"], floor)
        vobs[vid] = _vob_json(vid, spec)
    mine = set(vobs)
    for v in (existing or {}).get("vobs", []):  # editor vobs stay
        vid = int(v["id"])
        if vid not in owned_before and vid not in mine:
            vobs[vid] = v
    waynet = {"points": [], "edges": [], "freepoints": []}
    old = (existing or {}).get("waynet") or {}
    for key in ("points", "freepoints"):
        waynet[key] = [p for p in old.get(key, []) if p.get("owner") != "worldgen"]
    waynet["edges"] = [e for e in old.get("edges", []) if not (len(e) > 2 and e[2] == "worldgen")]
    hand = {p["name"] for p in waynet["points"] + waynet["freepoints"]}
    waynet["points"] += [p for p in plan.points if p["name"] not in hand]
    waynet["freepoints"] += [p for p in plan.freepoints if p["name"] not in hand]
    waynet["edges"] += plan.edges
    ours = {(z["type"], z["value"]) for z in plan.zones}

    # the generator's zones over the whole level (music, ambient): also when the value changed
    # (a level's ambient from "gewoelbe" to its own), else the old one stays beside the new
    def where(z: dict[str, Any]) -> tuple[Any, ...]:
        b = z.get("box") or {}
        return (z.get("type"), *(round(float(v), 3) for k in ("center", "halfExtents")
                                 for v in b.get(k, [])))  # fmt: skip

    boxes = {where(z) for z in plan.zones}

    def stale(z: dict[str, Any]) -> bool:
        return (z.get("type"), z.get("value")) in ours or where(z) in boxes

    zones = [z for z in (existing or {}).get("zones", []) if not stale(z)] + plan.zones
    next_id = max([ids.next_id] + [vid + 1 for vid in vobs])
    ids.next_id = next_id
    doc: dict[str, Any] = {
        "version": 1,
        "name": name,
        "nextVobId": next_id,
        "generator": {"tool": "gothar-worldgen", "owned": _owned(ids.owned & set(vobs))},
        "staticMeshes": [],
        "environment": _environment(plan.environment),
        "vobs": [vobs[k] for k in sorted(vobs)],
        "waynet": waynet,
        "zones": zones,
    }
    return doc


def _environment(env: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {"sky": False, "ambient": [tidy(v) for v in env["ambient"]]}
    if "ambient_ground" in env:
        out["ambient_ground"] = [tidy(v) for v in env["ambient_ground"]]
    out["fog_color"] = [tidy(v) for v in env["fog_color"]]
    out["fog_start"] = tidy(env["fog_start"])
    out["fog_distance"] = tidy(env["fog_distance"])
    return out


def generate_level(level: Level, site: str, worlds: Path, data: Path) -> tuple[list[str], set[str]]:
    """Writes ``dungeon/ebene_NN.g7world``, its block meshes and the vob ids; returns report
    lines and the texture kinds used (the kit and textures: ``write_kit``/``write_textures``)."""
    plan = plan_level(level, site)
    name = f"ebene_{level.number:02d}"
    folder = worlds / "dungeon"
    lines = []
    kinds: set[str] = set()
    tris = 0
    blocks = folder / name
    blocks.mkdir(parents=True, exist_ok=True)
    keep = set()
    for (bx, by), mesh in sorted(plan.blocks.items()):
        path = blocks / f"block_{bx}_{by}.glb"
        kit._write(path, mesh.glb(f"block_{bx}_{by}"))
        keep.add(path.name)
        kinds |= mesh.kinds()
        tris += mesh.triangles()
    for old in blocks.glob("block_*.glb"):
        if old.name not in keep:
            old.unlink()
    ids_path = data / "dungeon" / f"{name}_vob_ids.json"
    ids = VobIds.load(ids_path)
    world_path = folder / f"{name}.g7world"
    existing = json.loads(world_path.read_text(encoding="utf-8")) if world_path.is_file() else None
    doc = build_world(level, plan, site, ids, existing)
    write_world(world_path, doc)
    ids.save(ids_path)
    lights = sum(1 for v in doc["vobs"] if v["type"] == "light")
    lines.append(
        f"{name}: {level.width}x{level.height} cells, {len(plan.blocks)} blocks, "
        f"{tris} triangles, {len(doc['vobs'])} vobs, {lights} lights, "
        f"{len(doc['waynet']['points'])} waypoints"
    )
    return lines, kinds


def generate(
    site: str, folder: Path, worlds: Path, data: Path, levels: list[int] | None = None
) -> list[str]:
    """All levels in ``folder`` (``ebene_NN.txt``) or the given ones, plus kit and textures."""
    from gothar_worldgen.dungeon.level import load_level

    numbers = levels or sorted(int(p.stem.split("_")[1]) for p in folder.glob("ebene_*.txt"))
    lines, kinds = kit.write_kit(worlds / "dungeon" / "kit")
    for n in numbers:
        level = load_level(folder, n)
        out, used = generate_level(level, site, worlds, data)
        lines += out
        kinds |= used
    shared = {k for k in kinds if k not in DUNGEON_KINDS}  # with the mobs and props
    kit.write_textures(worlds.parents[1] / "furniture" / "textures", shared)
    kit.write_textures(worlds / "dungeon" / "textures", kinds - shared)
    return lines
