"""The inside of the castle's main wing (W7 D1, plan ``docs/design/schloss-dungeon.md``).

Pure Python like ``schloss_geometry`` (runs in Blender and in the tests). With ``interior`` in
schloss.json the main wing is hollow:

- three storeys inside the outer walls (``wallM`` thick): the vaulted cellar, the ground floor at
  the portal and the first floor; above the first floor's ceiling the wing stays closed;
- the rooms of each storey in a row along the wing (``rooms``: name and where it ends along the
  wing), parted by walls with a doorway (``doorT`` across the wing);
- the window rows of the facades show inside as niches where they fall into a storey;
- the open portal from the courtyard with a short ramp down to the ground floor;
- the stair tower, moved against the garden wall, hollow, with a spiral stair round a newel: one
  full turn per storey, a level landing at the doorway into the wing on every storey;
- collision: the outer walls as slabs round the portal and the tower doorways, the floors, the
  partitions round their doorways, the closed upper part of the wing with its roof, the tower's
  walls, newel, ramps and landings; every body a closed convex prism.

Coordinates as in ``schloss_geometry``: worldgen frame, ``Wing.p(s, t, y)`` along/across the wing.
"""

from __future__ import annotations

import math
from typing import Any

Vec3 = tuple[float, float, float]

DOOR_W, DOOR_H = 1.2, 2.3  # doorways between rooms and into the tower
PARTITION_M = 0.4
NICHE_BACK_M = 0.2  # a window niche ends this far behind the outer face (the facade's panel)
TOWER_WALL_M = 0.5
NEWEL_R = 1.1  # the spiral stays at most 35 deg steep from here outwards (one turn per storey)
LANDING_DEG = 60.0  # level landing at the doorway on every storey
STEPS_PER_TURN = 18
STEP_T = 0.22  # thickness of a stair block under its tread
RAMP_PIECES = 24  # collision pieces per turn (smooth ramp)


TOWER_PART = "turm"  # the stair tower's inside: a model of its own like the rooms (W7 D1b)


def part_name(key: str, room: str) -> str:
    """The model of a room: ``<storey>_<room>`` in lower case (``schloss_<part>.glb``)."""
    return f"{key}_{room}".lower()


def floors(spec: dict[str, Any]) -> dict[str, float]:
    """Floor and ceiling heights: cellar, ground (the portal's storey) and upper floor."""
    slab = float(spec.get("slabM", 0.3))
    g, u = float(spec["ground"]), float(spec["upper"])
    return {"cellar": float(spec["cellar"]), "cellar_top": g - slab, "ground": g,
            "ground_top": u - slab, "upper": u, "upper_top": float(spec["upperCeiling"]),
            "slab": slab}  # fmt: skip


def storeys(spec: dict[str, Any]) -> list[tuple[str, float, float]]:
    """(storey key, floor, ceiling) from the bottom."""
    f = floors(spec)
    return [("keller", f["cellar"], f["cellar_top"]), ("eg", f["ground"], f["ground_top"]),
            ("og", f["upper"], f["upper_top"])]  # fmt: skip


def tower_centre(w: Any, tower: dict[str, Any]) -> tuple[float, float, float]:  # noqa: ANN401
    """(s along the wing, t across, apothem) of the tower moved against the garden wall: its
    side facing the wing lies ``TOWER_WALL_M`` inside the wing's outer face."""
    tx, tz = tower["at"]
    dx, dz = tx - w.a[0], tz - w.a[1]
    s = dx * w.u[0] + dz * w.u[1]
    t = dx * w.n[0] + dz * w.n[1]
    r = float(tower["radius"])
    apothem = r * math.cos(math.pi / 8)
    side = 1.0 if t > 0 else -1.0
    return s, side * (w.half + apothem - TOWER_WALL_M), apothem


def moved_tower(w: Any, tower: dict[str, Any]) -> dict[str, Any]:  # noqa: ANN401
    """The tower spec with ``at`` moved against the wall (and turned so a side faces the wing)."""
    s, t, _ = tower_centre(w, tower)
    x, _, z = w.p(s, t, 0.0)
    out = dict(tower)
    out["at"] = [round(x, 3), round(z, 3)]
    side = 1.0 if t > 0 else -1.0
    door = math.degrees(math.atan2(-side * w.n[1], -side * w.n[0]))  # from the tower to the wing
    out["phaseDeg"] = round((door - 22.5) % 45.0, 4)  # a side's middle faces the wing
    return out


class Inside:
    """Builds the inside into a ``schloss_geometry.Model``."""

    def __init__(self, m: Any, w: Any, spec: dict[str, Any], tower: dict[str, Any] | None,  # noqa: ANN401
                 portal: dict[str, Any] | None) -> None:  # fmt: skip
        self.m, self.w, self.spec = m, w, spec
        self.wall = float(spec.get("wallM", 0.8))
        self.f = floors(spec)
        self.t_in = w.half - self.wall  # inner faces at t = +-t_in
        self.s0, self.s1 = self.wall, w.length - self.wall
        self.door_t = float(spec.get("doorT", 2.5))
        self.tower = tower
        self.tower_s = self.tower_t = None
        if tower is not None:
            self.tower_s, self.tower_t, _ = tower_centre(w, tower)
        self.portal = portal
        self.portal_s = w.length * float(portal["t"]) if portal else None
        self.portal_w = float(portal["w"]) if portal else 0.0
        self.portal_h = float(portal["h"]) if portal else 0.0
        self.portal_y = w.terrain(1, self.portal_s) if portal else None
        self.bodies = 0

    # --- helpers -----------------------------------------------------------------------------
    def quad(self, mat: str, s0: float, t0: float, s1: float, t1: float, y0: float, y1: float,
             want: Vec3, horizontal: bool = False) -> None:  # fmt: skip
        p = self.w.p
        if horizontal:  # (s0, t0) - (s1, t1) at height y0
            self.m.quad(mat, p(s0, t0, y0), p(s1, t0, y0), p(s1, t1, y0), p(s0, t1, y0), want)
        else:  # vertical from (s0, t0) to (s1, t1), y0..y1
            self.m.quad(mat, p(s0, t0, y0), p(s1, t1, y0), p(s1, t1, y1), p(s0, t0, y1), want)

    def box_body(self, name: str, s0: float, s1: float, t0: float, t1: float, y0: float,
                 y1: float) -> None:  # fmt: skip
        if s1 - s0 < 1e-3 or t1 - t0 < 1e-3 or y1 - y0 < 1e-3:
            return
        p = self.w.p
        ring = [(s0, t0), (s1, t0), (s1, t1), (s0, t1)]
        self.m.body(name, [p(s, t, y0) for s, t in ring], [p(s, t, y1) for s, t in ring])
        self.bodies += 1

    def up(self) -> Vec3:
        return (0.0, 1.0, 0.0)

    def down(self) -> Vec3:
        return (0.0, -1.0, 0.0)

    # --- rooms -------------------------------------------------------------------------------
    def rooms(self, key: str) -> list[tuple[str, float, float]]:
        """(name, s from, s to) of a storey, from the a end."""
        out, s = [], self.s0
        for r in self.spec["rooms"][key]:
            end = min(float(r["to"]), self.s1)
            out.append((r["name"], s, end))
            s = end + PARTITION_M
        return out

    def openings(
        self, key: str, y0: float, y1: float
    ) -> dict[int, list[tuple[float, float, float, float]]]:
        """Holes in the inner faces per side: (s0, s1, y0, y1) – windows, portal, tower door."""
        out: dict[int, list[tuple[float, float, float, float]]] = {1: [], -1: []}
        for side, u, wy0, wy1 in getattr(self.w, "windows", []):
            if wy0 >= y0 + 0.2 and wy1 <= y1 - 0.1:
                out[side].append((u - 0.6, u + 0.6, wy0, wy1))
        if self.portal and key == "eg":
            ps, hw = self.portal_s, self.portal_w / 2
            out[1].append((ps - hw, ps + hw, y0, self.portal_y + self.portal_h))
        if self.tower_s is not None:
            side = 1 if self.tower_t > 0 else -1
            out[side].append(
                (self.tower_s - DOOR_W / 2, self.tower_s + DOOR_W / 2, y0, y0 + DOOR_H)
            )
        return out

    def wall_face(self, side: int, s0: float, s1: float, y0: float, y1: float, mat: str,
                  holes: list[tuple[float, float, float, float]]) -> None:  # fmt: skip
        """An inner long wall face between s0 and s1 with rectangular holes (columns)."""
        t = side * self.t_in
        want = (-self.w.n[0] * side, 0.0, -self.w.n[1] * side)
        hs = sorted(h for h in holes if h[1] > s0 and h[0] < s1)
        cuts = sorted({s0, s1, *(max(s0, h[0]) for h in hs), *(min(s1, h[1]) for h in hs)})
        for a, b in zip(cuts, cuts[1:], strict=False):
            if b - a < 1e-4:
                continue
            col = [h for h in hs if h[0] <= a + 1e-6 and b <= h[1] + 1e-6]
            ya = y0
            for h in sorted(col, key=lambda h: h[2]):
                if h[2] > ya:
                    self.quad(mat, a, t, b, t, ya, h[2], want)
                ya = max(ya, h[3])
            if ya < y1:
                self.quad(mat, a, t, b, t, ya, y1, want)

    def niches(self, side: int, s0: float, s1: float, y0: float, y1: float) -> None:
        """Window niches through the inner part of the wall, ending at the facade's panel."""
        t_in, t_back = side * self.t_in, side * (self.w.half - NICHE_BACK_M)
        inward = (-self.w.n[0] * side, 0.0, -self.w.n[1] * side)
        for ws, u, wy0, wy1 in getattr(self.w, "windows", []):
            if ws != side or not (s0 < u < s1) or not (wy0 >= y0 + 0.2 and wy1 <= y1 - 0.1):
                continue
            a, b = u - 0.6, u + 0.6
            self.quad("frame", a, t_back, b, t_back, wy0, wy1, inward)  # the dark pane
            self.quad("plaster_white", a, t_in, b, t_back, wy0, wy0, self.up(), horizontal=False)
            p = self.w.p
            self.m.quad("stone", p(a, t_in, wy0), p(b, t_in, wy0), p(b, t_back, wy0),
                        p(a, t_back, wy0), self.up())  # fmt: skip
            self.m.quad("plaster_white", p(a, t_in, wy1), p(b, t_in, wy1), p(b, t_back, wy1),
                        p(a, t_back, wy1), self.down())  # fmt: skip
            self.m.quad("plaster_white", p(a, t_in, wy0), p(a, t_back, wy0), p(a, t_back, wy1),
                        p(a, t_in, wy1), self.w.along(1.0))  # fmt: skip
            self.m.quad("plaster_white", p(b, t_in, wy0), p(b, t_back, wy0), p(b, t_back, wy1),
                        p(b, t_in, wy1), self.w.along(-1.0))  # fmt: skip

    def storey(self, key: str, y0: float, y1: float) -> None:
        mats = self.spec["materials"][key]
        holes = self.openings(key, y0, y1)
        t = self.t_in
        rooms = self.rooms(key)
        for name, a, b in rooms:
            self.m.part = part_name(key, name)
            self.quad(mats["floor"], a, -t, b, t, y0, y0, self.up(), horizontal=True)
            if key == "keller":
                self.vault(a, b, y0, y1, mats["ceiling"])
            else:
                self.quad(mats["ceiling"], a, -t, b, t, y1, y1, self.down(), horizontal=True)
                self.beams(key, a, b, y1)
            for side in (1, -1):
                self.wall_face(side, a, b, y0, y1, mats["wall"], holes[side])
                self.niches(side, a, b, y0, y1)
        # gable-end faces and the partitions with their doorways (each face in its room)
        self.m.part = part_name(key, rooms[0][0])
        self.end_face(self.s0, y0, y1, mats["wall"], 1.0)
        self.m.part = part_name(key, rooms[-1][0])
        self.end_face(self.s1, y0, y1, mats["wall"], -1.0)
        for (before, _, b), (after, c, _) in zip(rooms, rooms[1:], strict=False):
            self.partition(b, c, y0, y1, mats["wall"], key, (part_name(key, before),
                                                             part_name(key, after)))  # fmt: skip
        self.m.part = None

    def beams(self, key: str, a: float, b: float, y1: float) -> None:
        """A timber-beam ceiling like the houses' (``interior.beams``): beams across the wing
        under the ceiling, the plaster fields between them; two sides and the bottom of each
        (the top is hidden, the ends sit in the walls). No collision: above head height."""
        spec = self.spec.get("beams")
        if not spec or key not in spec.get("storeys", []):
            return
        w, d = float(spec["widthM"]), float(spec["depthM"])
        every, mat = float(spec["everyM"]), str(spec["material"])
        t = self.t_in
        n = max(int((b - a) / every), 1)
        step = (b - a) / n  # evenly over the room, half a field at either end
        for k in range(n):
            s = a + step * (k + 0.5)
            s0, s1, y0 = s - w / 2, s + w / 2, y1 - d
            self.quad(mat, s0, -t, s1, t, y0, y0, self.down(), horizontal=True)
            self.quad(mat, s0, -t, s0, t, y0, y1, self.w.along(-1.0))
            self.quad(mat, s1, -t, s1, t, y0, y1, self.w.along(1.0))

    def end_face(self, s: float, y0: float, y1: float, mat: str, sign: float) -> None:
        t = self.t_in
        self.quad(mat, s, -t, s, t, y0, y1, self.w.along(sign))

    def partition(self, a: float, b: float, y0: float, y1: float, mat: str, key: str,
                  parts: tuple[str, str] | None = None) -> None:  # fmt: skip
        """A wall between s=a and s=b with a doorway at doorT; both faces (each in the model of
        its room, ``parts``: before, after; the reveal with the room before), a body each side
        of the doorway and one over it."""
        t, d0, d1 = self.t_in, self.door_t - DOOR_W / 2, self.door_t + DOOR_W / 2
        top = y0 + DOOR_H
        for s, sign in ((a, -1.0), (b, 1.0)):
            if parts is not None:
                self.m.part = parts[0] if sign < 0 else parts[1]
            want = self.w.along(sign)
            self.quad(mat, s, -t, s, d0, y0, y1, want)
            self.quad(mat, s, d1, s, t, y0, y1, want)
            self.quad(mat, s, d0, s, d1, top, y1, want)
        p = self.w.p
        if parts is not None:
            self.m.part = parts[0]
        self.m.quad(mat, p(a, d0, y0), p(b, d0, y0), p(b, d0, top), p(a, d0, top),
                    (self.w.n[0], 0.0, self.w.n[1]))  # fmt: skip
        self.m.quad(mat, p(a, d1, y0), p(b, d1, y0), p(b, d1, top), p(a, d1, top),
                    (-self.w.n[0], 0.0, -self.w.n[1]))  # fmt: skip
        self.m.quad("timber_dark", p(a, d0, top), p(b, d0, top), p(b, d1, top), p(a, d1, top),
                    self.down())  # fmt: skip
        n = f"COL_HULL_in_{key}_{a:.0f}"
        self.box_body(f"{n}_a", a, b, -t, d0, y0, y1)
        self.box_body(f"{n}_b", a, b, d1, t, y0, y1)
        self.box_body(f"{n}_c", a, b, d0, d1, top, y1)

    def vault(self, a: float, b: float, y0: float, y1: float, mat: str) -> None:
        """A segmental barrel vault across the wing, springing 1 m under the cellar's top."""
        t, rise, n = self.t_in, 1.0, 8
        spring = y1 - rise
        prev = None
        for k in range(n + 1):
            f = -1.0 + 2.0 * k / n
            pt = (f * t, spring + rise * (1.0 - f * f))
            if prev is not None:
                (ta, ya), (tb, yb) = prev, pt
                nrm = (self.w.n[0] * -(ta + tb) / 2, -1.0, self.w.n[1] * -(ta + tb) / 2)
                p = self.w.p
                self.m.quad(mat, p(a, ta, ya), p(b, ta, ya), p(b, tb, yb), p(a, tb, yb), nrm)
            prev = pt
        for s, sign in ((a, 1.0), (b, -1.0)):  # the lunettes over the end walls
            pts = [self.w.p(s, f * t, spring + rise * (1.0 - f * f))
                   for f in (-1.0 + 2.0 * k / n for k in range(n + 1))]  # fmt: skip
            self.m.poly(mat, pts, self.w.along(sign))

    # --- portal ------------------------------------------------------------------------------
    def portal_way(self) -> None:
        """The portal open through the wall, its floor ramping from the court to the ground
        floor inside."""
        if not self.portal:
            return
        w, s, hw = self.w, self.portal_s, self.portal_w / 2
        self.m.part = next((part_name("eg", n) for n, a, b in self.rooms("eg") if a <= s <= b),
                           None)  # fmt: skip
        y_out, y_in = self.portal_y, self.f["ground"]
        t_out, t_in = w.half, self.t_in
        p = w.p
        self.m.quad("stone_slab", p(s - hw, t_out, y_out), p(s + hw, t_out, y_out),
                    p(s + hw, t_in, y_in), p(s - hw, t_in, y_in), self.up())  # fmt: skip
        top = y_out + self.portal_h
        for sgn in (-1.0, 1.0):
            x = s + sgn * hw
            self.m.quad("stone", p(x, t_out - 0.5, min(y_out, y_in)), p(x, t_in, min(y_out, y_in)),
                        p(x, t_in, top), p(x, t_out - 0.5, top), w.along(-sgn))  # fmt: skip
        self.m.quad("stone", p(s - hw, t_out - 0.5, top), p(s + hw, t_out - 0.5, top),
                    p(s + hw, t_in, top), p(s - hw, t_in, top), self.down())  # fmt: skip
        # the ramp's body (a wedge under the floor of the passage)
        lo = min(y_out, y_in) - 0.4
        ring = [(s - hw, t_in), (s + hw, t_in), (s + hw, t_out + 0.3), (s - hw, t_out + 0.3)]
        bottom = [p(a, b, lo) for a, b in ring]
        top_pts = [p(ring[0][0], ring[0][1], y_in), p(ring[1][0], ring[1][1], y_in),
                   p(ring[2][0], ring[2][1], y_out), p(ring[3][0], ring[3][1], y_out)]  # fmt: skip
        self.m.body("COL_HULL_in_portal_ramp", bottom, top_pts)
        self.bodies += 1
        self.m.part = None

    # --- collision of the wing ---------------------------------------------------------------
    def wing_bodies(self) -> None:
        """The hollow wing: outer walls round their openings, floors, the closed top."""
        w, wall, f = self.w, self.wall, self.f
        h = w.half
        base = w.base
        # gable walls
        self.box_body("COL_HULL_in_wall_a", 0.0, wall, -h, h, base, w.eave)
        self.box_body("COL_HULL_in_wall_b", w.length - wall, w.length, -h, h, base, w.eave)
        for side in (1, -1):
            t0, t1 = (h - wall, h) if side > 0 else (-h, -h + wall)
            gaps: list[tuple[float, float, float, float]] = []  # s0, s1, y0, y1 through the wall
            if side > 0 and self.portal:
                ps, hw = self.portal_s, self.portal_w / 2
                gaps.append((ps - hw, ps + hw, min(self.portal_y, f["ground"]) - 0.4,
                             self.portal_y + self.portal_h))  # fmt: skip
            if self.tower_s is not None and (self.tower_t > 0) == (side > 0):
                for _, y0, _ in storeys(self.spec):
                    gaps.append((self.tower_s - DOOR_W / 2, self.tower_s + DOOR_W / 2, y0,
                                 y0 + DOOR_H))  # fmt: skip
            cuts = sorted({wall, w.length - wall, *(g[0] for g in gaps), *(g[1] for g in gaps)})
            for a, b in zip(cuts, cuts[1:], strict=False):
                col = sorted((g for g in gaps if g[0] <= a + 1e-6 and b <= g[1] + 1e-6),
                             key=lambda g: g[2])  # fmt: skip
                ya = base
                for k, g in enumerate(col):
                    self.box_body(f"COL_HULL_in_wall_{side:+d}_{a:.0f}_{k}", a, b, t0, t1, ya, g[2])
                    ya = g[3]
                self.box_body(f"COL_HULL_in_wall_{side:+d}_{a:.0f}_top", a, b, t0, t1, ya, w.eave)
        t = self.t_in
        self.box_body("COL_HULL_in_floor_keller", wall, w.length - wall, -t, t, base, f["cellar"])
        self.box_body("COL_HULL_in_floor_eg", wall, w.length - wall, -t, t, f["cellar_top"],
                      f["ground"])  # fmt: skip
        self.box_body("COL_HULL_in_floor_og", wall, w.length - wall, -t, t, f["ground_top"],
                      f["upper"])  # fmt: skip
        # the closed top: from the upper ceiling over the whole wing up to the ridge
        y = f["upper_top"]
        ring_b = [w.p(0, -h, y), w.p(w.length, -h, y), w.p(w.length, h, y), w.p(0, h, y)]
        e = w.eave
        pts = ring_b + [w.p(0, -h, e), w.p(w.length, -h, e), w.p(w.length, h, e), w.p(0, h, e),
                        w.p(0, 0, w.ridge), w.p(w.length, 0, w.ridge)]  # fmt: skip
        tris = [(0, 2, 1), (0, 3, 2)]
        for i in range(4):
            j = (i + 1) % 4
            tris += [(i, 4 + j, 4 + i), (i, j, 4 + j)]
        tris += [(4, 9, 8), (4, 5, 9), (6, 8, 9), (6, 7, 8), (5, 6, 9), (7, 4, 8)]
        self.m.collision.append(("COL_HULL_in_top", pts, tris))
        self.bodies += 1

    # --- the stair tower ---------------------------------------------------------------------
    def tower_inside(self, base: float) -> None:
        """Hollow octagon, newel, spiral flights (one turn per storey) and landings."""
        if self.tower is None:
            return
        self.m.part = TOWER_PART
        w = self.w
        c = w.p(self.tower_s, self.tower_t, 0.0)
        cx, cz = c[0], c[2]
        r = float(self.tower["radius"])
        phase = math.radians(float(moved_tower(w, self.tower)["phaseDeg"]))
        inward = (-w.n[0] if self.tower_t > 0 else w.n[0], -w.n[1] if self.tower_t > 0 else w.n[1])
        door_ang = math.atan2(inward[1], inward[0])  # towards the wing
        sts = storeys(self.spec)
        top = sts[-1][2]
        ap = r * math.cos(math.pi / 8)

        def at(rad: float, ang: float, y: float) -> Vec3:
            return (cx + rad * math.cos(ang), y, cz + rad * math.sin(ang))

        # walls: eight slabs (the one facing the wing round the doorways), inner faces plaster
        corners = [phase + k * math.pi / 4 for k in range(8)]
        for k in range(8):
            a0, a1 = corners[k], corners[(k + 1) % 8]
            mid = (a0 + a1) / 2
            ri, ro = r - TOWER_WALL_M / math.cos(math.pi / 8), r
            facing = abs(math.atan2(math.sin(mid - door_ang), math.cos(mid - door_ang))) < 0.3
            pin = [at(ri, a0, base), at(ri, a1, base)]
            want = (-math.cos(mid), 0.0, -math.sin(mid))
            ys = [base]
            if facing:
                for _, y0, _ in sts:
                    ys += [y0, y0 + DOOR_H]
            ys.append(top)
            for k2 in range(0, len(ys) - 1, 2):
                lo, hi = ys[k2], ys[k2 + 1]
                self._sector_body(
                    f"COL_HULL_in_tower_wall_{k}_{k2}", cx, cz, ri, ro, a0, a1, lo, hi
                )
                (x0, _, z0), (x1, _, z1) = pin
                self.m.quad("plaster_white", (x0, lo, z0), (x1, lo, z1), (x1, hi, z1),
                            (x0, hi, z0), want)  # fmt: skip
            if facing:  # the doorway jambs: the wall faces beside the opening
                half = DOOR_W / 2 / ap
                for _, y0, _ in sts:
                    for sgn in (-1.0, 1.0):
                        aa = door_ang + sgn * half
                        far = ap + self.wall - TOWER_WALL_M  # to the wing's inner face
                        self.m.quad("plaster_white", at(ri, aa, y0), at(far, aa, y0),
                                    at(far, aa, y0 + DOOR_H), at(ri, aa, y0 + DOOR_H),
                                    (-math.sin(aa) * sgn, 0.0, math.cos(aa) * sgn))  # fmt: skip
                    a_l, a_r = door_ang - half, door_ang + half
                    for y, mat, want_y in (
                        (y0, "stone_slab", self.up()),
                        (y0 + DOOR_H, "plaster_white", self.down()),
                    ):
                        self.m.poly(mat, [at(ri, a_l, y), at(ri, a_r, y), at(far, a_r, y),
                                          at(far, a_l, y)], want_y)  # fmt: skip
        # newel
        self._sector_body("COL_HULL_in_tower_newel", cx, cz, 0.0, NEWEL_R, 0.0, 2 * math.pi,
                          base, top, n=8)  # fmt: skip
        for k in range(8):
            a0, a1 = k * math.pi / 4, (k + 1) * math.pi / 4
            mid = (a0 + a1) / 2
            self.m.quad("stone", at(NEWEL_R, a0, base), at(NEWEL_R, a1, base),
                        at(NEWEL_R, a1, top), at(NEWEL_R, a0, top),
                        (math.cos(mid), 0.0, math.sin(mid)))  # fmt: skip
        # floor of the tower at the cellar, a ceiling over the upper landing
        ri = ap - TOWER_WALL_M
        self._sector_body("COL_HULL_in_tower_floor", cx, cz, 0.0, ri + 0.1, 0.0, 2 * math.pi,
                          base, sts[0][1], n=8)  # fmt: skip
        self._sector_body("COL_HULL_in_tower_cap", cx, cz, 0.0, ri + 0.1, 0.0, 2 * math.pi, top,
                          top + 0.3, n=8)  # fmt: skip
        self._disc("stone_slab", cx, cz, ri, sts[0][1], self.up())
        self._disc("plaster_white", cx, cz, ri, top, self.down())
        # flights: from each storey's landing one turn up to the next storey's landing
        land = math.radians(LANDING_DEG)
        for (key, y0, _), (_, y1, _) in zip(sts, sts[1:], strict=False):
            start = door_ang + land / 2  # the flight leaves the landing, turning one way round
            sweep = 2 * math.pi - land
            self._landing(f"{key}_unten", cx, cz, ri, door_ang, land, y0)
            n = RAMP_PIECES
            for k in range(n):
                a0, a1 = start + sweep * k / n, start + sweep * (k + 1) / n
                ya, yb = y0 + (y1 - y0) * k / n, y0 + (y1 - y0) * (k + 1) / n
                self._ramp_body(f"COL_HULL_in_tower_ramp_{key}_{k}", cx, cz, NEWEL_R, ri + 0.1,
                                a0, a1, ya, yb)  # fmt: skip
            steps = STEPS_PER_TURN
            for k in range(steps):
                a0, a1 = start + sweep * k / steps, start + sweep * (k + 1) / steps
                y = y0 + (y1 - y0) * (k + 1) / steps
                self._step(cx, cz, NEWEL_R, ri, a0, a1, y)
        self._landing(f"{sts[-1][0]}_oben", cx, cz, ri, door_ang, land, sts[-1][1])
        self.m.part = None

    def _sector_body(self, name: str, cx: float, cz: float, r0: float, r1: float, a0: float,
                     a1: float, y0: float, y1: float, n: int = 1) -> None:  # fmt: skip
        if y1 - y0 < 1e-3:
            return
        angs = [a0 + (a1 - a0) * k / n for k in range(n + 1)]
        if r0 <= 1e-6:
            ring = [(cx + r1 * math.cos(a), cz + r1 * math.sin(a)) for a in angs[:-1]]
            if n == 1:
                ring = [(cx, cz)] + [(cx + r1 * math.cos(a), cz + r1 * math.sin(a)) for a in angs]
        else:
            ring = [(cx + r0 * math.cos(a), cz + r0 * math.sin(a)) for a in angs]
            ring += [(cx + r1 * math.cos(a), cz + r1 * math.sin(a)) for a in reversed(angs)]
        self.m.body(name, [(x, y0, z) for x, z in ring], [(x, y1, z) for x, z in ring])
        self.bodies += 1

    def _ramp_body(self, name: str, cx: float, cz: float, r0: float, r1: float, a0: float,
                   a1: float, ya: float, yb: float) -> None:  # fmt: skip
        ring = [(r0, a0), (r1, a0), (r1, a1), (r0, a1)]
        ys = [ya, ya, yb, yb]
        bottom = [
            (cx + r * math.cos(a), y - 0.4, cz + r * math.sin(a))
            for (r, a), y in zip(ring, ys, strict=True)
        ]
        top = [
            (cx + r * math.cos(a), y, cz + r * math.sin(a))
            for (r, a), y in zip(ring, ys, strict=True)
        ]
        self.m.body(name, bottom, top)
        self.bodies += 1

    def _step(self, cx: float, cz: float, r0: float, r1: float, a0: float, a1: float,
              y: float) -> None:  # fmt: skip
        corners = [(r0, a0), (r1, a0), (r1, a1), (r0, a1)]
        pts = [(cx + r * math.cos(a), y - STEP_T, cz + r * math.sin(a)) for r, a in corners]
        self.m.box("stone", pts, STEP_T, want_bottom=True)

    def _landing(self, key: str, cx: float, cz: float, ri: float, mid: float, land: float,
                 y: float) -> None:  # fmt: skip
        a0, a1 = mid - land / 2, mid + land / 2
        self._sector_body(f"COL_HULL_in_tower_landing_{key}", cx, cz, NEWEL_R, ri + 0.1, a0, a1,
                          y - 0.4, y, n=3)  # fmt: skip
        angs = [a0 + (a1 - a0) * k / 3 for k in range(4)]
        ring = [(cx + NEWEL_R * math.cos(a), y, cz + NEWEL_R * math.sin(a)) for a in angs]
        ring += [(cx + ri * math.cos(a), y, cz + ri * math.sin(a)) for a in reversed(angs)]
        for k in range(3):
            quad = [ring[k], ring[k + 1], ring[6 - k - 1], ring[6 - k]]
            self.m.poly("stone_slab", quad, self.up())

    def _disc(self, mat: str, cx: float, cz: float, r: float, y: float, want: Vec3) -> None:
        pts = [(cx + r * math.cos(k * math.pi / 4), y, cz + r * math.sin(k * math.pi / 4))
               for k in range(8)]  # fmt: skip
        self.m.poly(mat, pts, want)

    # --- all ---------------------------------------------------------------------------------
    def build(self, base: float) -> None:
        for key, y0, y1 in storeys(self.spec):
            self.storey(key, y0, y1)
        self.portal_way()
        self.wing_bodies()
        self.tower_inside(base)


def cellar_ground(w: Any, spec: dict[str, Any], tower: dict[str, Any] | None,  # noqa: ANN401
                  ) -> tuple[list[dict[str, Any]], list[list[list[float]]]]:  # fmt: skip
    """(pads, holes) for the terrain under the hollow wing and tower: every terrain cell that
    touches their outline is left out (it would show in the rooms), and the samples inside are
    lowered under the cellar floor (the collision keeps a cell's triangles while a neighbour
    has ground). The apron (``apron``) covers the ragged edge outside the walls."""
    y = round(float(spec["cellar"]) - 0.1, 3)
    h = w.half
    ring = [w.p(0, -h, 0), w.p(w.length, -h, 0), w.p(w.length, h, 0), w.p(0, h, 0)]
    polys = [[[round(p[0], 3), round(p[2], 3)] for p in ring]]
    if tower is not None:
        s, t, ap = tower_centre(w, tower)
        c = w.p(s, t, 0)
        r = float(tower["radius"])
        ph = math.radians(float(moved_tower(w, tower)["phaseDeg"]))
        angs = [ph + k * math.pi / 4 for k in range(8)]
        polys.append([[round(c[0] + r * math.cos(a), 3), round(c[2] + r * math.sin(a), 3)]
                      for a in angs])  # fmt: skip
    pads = [{"polygon": poly, "y": y, "clampBelow": True, "exact": True} for poly in polys]
    return pads, polys


APRON_M = 1.5  # paving round the hollow wing and tower: covers the terrain cells left out
APRON_LIFT = 0.03
APRON_STEP_M = 3.0


def apron_samples(w: Any, spec: dict[str, Any], tower: dict[str, Any] | None,  # noqa: ANN401
                  height: Any) -> dict[str, Any]:  # noqa: ANN401  # fmt: skip
    """Ground heights for the apron, measured from the DGM by the schloss command: per strip a
    list of (x, z) corner pairs with heights (at the wall, ``APRON_M`` out)."""
    h, out = w.half, APRON_M
    strips = []

    def edge(points: list[tuple[Vec3, Vec3]]) -> list[list[float]]:
        return [[round(a[0], 3), round(a[2], 3), round(height(a[0], a[2]), 3), round(b[0], 3),
                 round(b[2], 3), round(height(b[0], b[2]), 3)] for a, b in points]  # fmt: skip

    skip = None
    if tower is not None:
        ts, tt, _ = tower_centre(w, tower)
        skip = (
            1 if tt > 0 else -1,
            ts - float(tower["radius"]) - out,
            ts + float(tower["radius"]) + out,
        )
    for side in (1, -1):  # the long sides, from corner to corner (the corners belong to them)
        n = max(1, math.ceil((w.length + 2 * out) / APRON_STEP_M))
        ss = [-out + (w.length + 2 * out) * k / n for k in range(n + 1)]
        run: list[tuple[Vec3, Vec3]] = []
        for s in ss:
            if skip and side == skip[0] and skip[1] < s < skip[2]:
                if len(run) > 1:
                    strips.append(edge(run))
                run = []
                continue
            run.append((w.p(s, side * h, 0.0), w.p(s, side * (h + out), 0.0)))
        if len(run) > 1:
            strips.append(edge(run))
    for s_wall, s_out in ((0.0, -out), (w.length, w.length + out)):  # the gable ends
        n = max(1, math.ceil(2 * h / APRON_STEP_M))
        ts = [-h + 2 * h * k / n for k in range(n + 1)]
        strips.append(edge([(w.p(s_wall, t, 0.0), w.p(s_out, t, 0.0)) for t in ts]))
    if tower is not None:
        s, t, _ = tower_centre(w, tower)
        c = w.p(s, t, 0.0)
        r = float(tower["radius"])
        ph = math.radians(float(moved_tower(w, tower)["phaseDeg"]))
        side = 1 if t > 0 else -1
        door = math.atan2(-side * w.n[1], -side * w.n[0])
        pts = []
        for k in range(9):
            a = ph + k * math.pi / 4
            ca, sa = math.cos(a), math.sin(a)
            pts.append(((c[0] + r * ca, 0.0, c[2] + r * sa),
                        (c[0] + (r + out) * ca, 0.0, c[2] + (r + out) * sa)))  # fmt: skip
        run = []
        for k in range(8):  # all sides but the one against the wing
            mid = ph + (k + 0.5) * math.pi / 4
            facing = abs(math.atan2(math.sin(mid - door), math.cos(mid - door))) < 0.3
            if facing:
                if len(run) > 1:
                    strips.append(edge(run))
                run = []
                continue
            if not run:
                run.append(pts[k])
            run.append(pts[k + 1])
        if len(run) > 1:
            strips.append(edge(run))
    return {"strips": strips}


def apron(m: Any, samples: dict[str, Any]) -> None:  # noqa: ANN401
    """The paving strips (stone slabs, a little over the ground) and their collision."""
    k = 0
    for strip in samples["strips"]:
        for a, b in zip(strip, strip[1:], strict=False):
            pts = [(a[0], a[2] + APRON_LIFT, a[1]), (b[0], b[2] + APRON_LIFT, b[1]),
                   (b[3], b[5] + APRON_LIFT, b[4]), (a[3], a[5] + APRON_LIFT, a[4])]  # fmt: skip
            for tri in ((pts[0], pts[1], pts[2]), (pts[0], pts[2], pts[3])):
                m.poly("stone_slab", list(tri), (0.0, 1.0, 0.0))
            bottom = [(x, y - 0.5, z) for x, y, z in pts]
            m.body(f"COL_HULL_apron_{k}", bottom, pts)
            k += 1
