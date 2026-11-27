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
    "grotte": (  # Ebene 7 (stub): crystal light later; the vault's sound until engine has "grotte"
        "gewoelbe",
        {
            "ambient": [0.04, 0.046, 0.058],
            "fog_color": [0.014, 0.018, 0.026],
            "fog_start": 2.0,
            "fog_distance": 18.0,
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
}
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
        self.tag = level.tag
        floor, ceiling, rib, inlay = SURFACES.get(level.theme, SURFACES["gewoelbe"])
        self.floor_mat, self.ceil_mat, self.rib_mat, self.inlay = floor, ceiling, rib, inlay

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

    def floor(self, at: Cell) -> float:
        return -self.lv.water_depth if self.lv.char(*at) == "~" else 0.0

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
        material: str = "rubble",
        a: float = 0.0,
        b: float = 1.0,
    ) -> None:
        """A wall face on ``side`` of ``at`` facing into it, from ``a`` to ``b`` (0..1) along."""
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
        self.waynet()
        self.zones()
        return self.plan

    def floor_material(self, at: Cell) -> str:
        """Flagstones; in the workshops some cells have planks or an iron plate instead."""
        if self.inlay and self.lv.char(*at) == ".":
            k = (at[0] * 7 + at[1] * 13) % 9
            if k == 0:
                return "oak"
            if k == 4:
                return "plate"
        return self.floor_mat

    def _ceiled(self, at: Cell) -> bool:
        return at in self.walk and at not in self.shafts and self.ceil(at) == self.h

    def ribs(self) -> None:
        """Ribs under the ceiling every ``RIB_CELLS`` cells across the boundary of two ceiled
        cells: cross arches in a corridor, a grid in a room; a corbel where a rib meets a wall."""
        c, top = self.c, self.y(self.h)
        hw = RIB_W / 2
        for at in sorted(self.walk, key=lambda a: (a[1], a[0])):
            for dx, dy in ((1, 0), (0, 1)):
                nb = (at[0] + dx, at[1] + dy)
                k = nb[0] if dx else nb[1]
                if k % RIB_CELLS or not (self._ceiled(at) and self._ceiled(nb)):
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
        else:
            self.flat(at, self.floor(at), True, self.floor_material(at))
        if at in self.shafts:
            self.shaft_up(at)
        else:
            self.flat(at, self.ceil(at), False, self.ceil_mat)
        for side, (dx, dy) in DIRS.items():
            nb = (at[0] + dx, at[1] + dy)
            if self.connected(at, nb):
                if self.ceil(nb) > self.ceil(at):
                    self.face(nb, OPPOSITE[side], self.ceil(at), self.ceil(nb))
                if (
                    at not in self.wells
                    and nb not in self.wells
                    and self.floor(nb) < self.floor(at)
                ):
                    self.face(nb, OPPOSITE[side], self.floor(nb), self.floor(at))
                continue
            back = at in self.wells and nb == (
                2 * at[0] - self.wells[at][0],
                2 * at[1] - self.wells[at][1],
            )
            if lv.char(*nb) == "N" and at not in self.wells and self._faces(nb) == at:
                self.niche_face(at, side)
            else:
                self.face(at, side, self.low(at), self.ceil(at), "dark" if back else "rubble")
        if ch == "D":
            self.door_frame(at)

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
            m.quad("rubble", [(pa, ya, qa), (pb, ya, qb), (pb, yb, qb), (pa, yb, qa)], want)

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
            self.flat(at, h, False, "rubble", r)
        self.flat(at, h + SHAFT_M, False, "rubble", (x0 + a, z0 + a, x0 + b, z0 + b))
        m = self.mesh(at)
        ya, yb = self.y(h), self.y(h + SHAFT_M)
        ring = [(x0 + a, z0 + a), (x0 + b, z0 + a), (x0 + b, z0 + b), (x0 + a, z0 + b)]
        for k in range(4):
            (pa, qa), (pb, qb) = ring[k], ring[(k + 1) % 4]
            cx, cz = x0 + c / 2, z0 + c / 2
            want = (cx - (pa + pb) / 2, 0.0, cz - (qa + qb) / 2)
            m.quad("rubble", [(pa, ya, qa), (pb, ya, qb), (pb, yb, qb), (pa, yb, qa)], want)

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
        for (x, y), n in self._runs(plain):
            m = self.mesh((x, y))
            x0, x1, z0, z1 = self.x0(x), self.x0(x + n), self.z0(y), self.z0(y) + self.c
            m.body(f"floor_{x}_{y}", (x0, self.y(-SLAB), z0), (x1, self.y(0.0), z1))
        for (x, y), n in self._runs({a for a in self.walk if lv.char(*a) == "~"}):
            x0, x1, z0, z1 = self.x0(x), self.x0(x + n), self.z0(y), self.z0(y) + self.c
            self.mesh((x, y)).body(
                f"water_{x}_{y}",
                (x0, self.y(-self.lv.water_depth - SLAB), z0),
                (x1, self.y(-self.lv.water_depth), z1),
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
                self.vob(
                    "mesh",
                    name,
                    (cx, y0, cz),
                    ident,
                    mesh=self.kit_mesh("secret_wall"),
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
                look = DIRS[spec.get("facing", "S")]
                self.vob(
                    "spawn",
                    name,
                    (cx, y0, cz),
                    _quat_look(look[0], look[1]),
                    components={
                        "spawn": {
                            "npc": spec["npc"],
                            "count": int(spec.get("count", 1)),
                            "respawn": bool(spec.get("respawn", False)),
                            "dormant": bool(spec.get("dormant", True)),
                        }
                    },
                )
        hy = (lv.water_depth - lv.water_edge) / 2  # the surface ``edgeM`` below the edge
        for (x, y), n in self._runs(set(lv.cells("~"))):  # one water box per row run
            cx = (self.x0(x) + self.x0(x + n)) / 2
            cz = self.mid((x, y))[1]
            self.vob(
                "water",
                f"WATER_{self.tag}_{x}_{y}",
                (cx, self.y(-lv.water_depth + hy), cz),
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
            self.vob("start", st["name"], (cx, self.y(y), cz), _quat_look(look[0], look[1]))
        for at, sp in sorted(lv.spawns.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            cx, cz = self.mid(at)
            look = DIRS[sp.get("facing", "S")]
            self.vob(
                "spawn",
                lv.spawn_name(at),
                (cx, self.y(self.floor(at)), cz),
                _quat_look(look[0], look[1]),
                components={
                    "spawn": {
                        "npc": sp["npc"],
                        "count": int(sp.get("count", 1)),
                        "respawn": bool(sp.get("respawn", False)),
                        "dormant": bool(sp.get("dormant", True)),
                    }
                },
            )
        self.relays()
        self.items()
        self.props()

    def props(self) -> None:
        """Still pieces (``props``): a sarcophagus in the middle of its cell, a bone niche
        against the wall behind it; mesh vobs ``PROP_E04_<x>_<y>`` with their collision."""
        for at, pr in sorted(self.lv.props.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            dx, dy = DIRS[pr.get("facing", "S")]
            cx, cz = self.mid(at)
            if not PROPS[pr["kind"]]:  # on the wall behind the cell
                cx, cz = cx - dx * self.c / 2, cz - dy * self.c / 2
            self.vob("mesh", self.lv.prop_name(at), (cx, self.y(self.floor(at)), cz),
                     _quat_front(dx, dy), mesh=self.kit_mesh(pr["kind"]))  # fmt: skip

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
        pos = (cx + dx * self.c / 2, self.y(0.0), cz + dy * self.c / 2)
        mob: dict[str, Any] = {"definition": spec.get("definition", typ)}
        if spec.get("targets"):
            mob["targets"], mob["action"] = list(spec["targets"]), spec.get("action", "trigger")
        if spec.get("once"):
            mob["once"] = True
        if spec.get("hidden"):
            mob["hidden"] = True
        self.vob(
            "mob", name, pos, _quat_front(dx, dy), mesh=self.kit_mesh(typ), components={"mob": mob}
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
            if not PROPS[pr["kind"]]:
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
                components={"light": dict(LIGHT_TORCH)},
            )

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
    zones = [
        z for z in (existing or {}).get("zones", []) if (z.get("type"), z.get("value")) not in ours
    ] + plan.zones
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
