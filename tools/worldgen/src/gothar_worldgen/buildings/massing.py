"""Grey building masses ("Klötzchen", W3): walls extruded from the footprint up to a roof surface.

Roofs are height functions over the footprint, so they work for any polygon: flat (one level),
saddle (two planes meeting in a ridge along ``ridgeDir`` through the middle of the footprint) and
shed (one plane rising across ``ridgeDir``). Gables come out of the walls by themselves. Hip and
tent roofs (rare) and LoD2 "mixed"/"other" roofs fall back to these forms; the notes say which.
Buildings with LoD2 parts are built part by part.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Polygon
from shapely.ops import split

from gothar_worldgen.buildings.gltf import MeshData

MIN_ROOF_RISE_M = 0.3  # below this a sloped roof is built flat


@dataclass(frozen=True)
class Mass:
    footprint: tuple[tuple[float, float], ...]  # local (x, z), open ring
    eave_y: float
    ridge_y: float
    roof: str  # "flat" | "saddle" | "shed"
    ridge_dir: tuple[float, float] | None = None  # (x, z) direction of the ridge


@dataclass
class MassingResult:
    masses: list[Mass]
    notes: list[str] = field(default_factory=list)  # fallbacks, e.g. "mixed->saddle"


def _roof_values(src: dict[str, Any], fallback_ground: float) -> tuple[float, float, str, Any]:
    roof = src.get("roof") or {}
    ground = float(src.get("groundY", fallback_ground))
    top = ground + float(src.get("heightM", 0.0))
    eave = roof.get("eaveY")
    ridge = roof.get("ridgeY")
    eave = float(eave) if eave is not None else (float(ridge) if ridge is not None else top)
    ridge = float(ridge) if ridge is not None else max(eave, top)
    return eave, max(ridge, eave), str(roof.get("type") or "flat"), roof.get("ridgeDir")


def masses_for_building(building: dict[str, Any]) -> MassingResult:
    """Masses of a ``buildings.json`` entry (its LoD2 parts if it has any)."""
    sources = building.get("parts") or [building]
    result = MassingResult([])
    for src in sources:
        footprint = src.get("footprint") or []
        if len(footprint) < 3:
            result.notes.append("part without footprint skipped")
            continue
        eave, ridge, kind, ridge_dir = _roof_values(src, float(building.get("groundY", 0.0)))
        rise = ridge - eave
        if kind in ("hip", "tent"):
            result.notes.append(f"{kind}->saddle")
            kind = "saddle"
        elif kind not in ("flat", "saddle", "shed"):
            if ridge_dir and rise >= MIN_ROOF_RISE_M:
                result.notes.append(f"{kind}->saddle")
                kind = "saddle"
            else:
                result.notes.append(f"{kind}->flat")
                kind, eave, ridge = "flat", (eave + ridge) / 2, (eave + ridge) / 2
        if kind != "flat" and rise < MIN_ROOF_RISE_M:
            kind = "flat"
        if kind == "flat":
            eave = ridge
        direction = None
        if ridge_dir and len(ridge_dir) == 2 and math.hypot(*ridge_dir) > 1e-9:
            n = math.hypot(*ridge_dir)
            direction = (ridge_dir[0] / n, ridge_dir[1] / n)
        ring = tuple((float(x), float(z)) for x, z in footprint)
        result.masses.append(Mass(ring, eave, ridge, kind, direction))
    return result


def _long_axis(poly: Polygon) -> tuple[float, float]:
    rect = np.asarray(poly.minimum_rotated_rectangle.exterior.coords)[:4]
    e1, e2 = rect[1] - rect[0], rect[2] - rect[1]
    e = e1 if np.hypot(*e1) >= np.hypot(*e2) else e2
    n = float(np.hypot(*e)) or 1.0
    return float(e[0] / n), float(e[1] / n)


class _Roof:
    """Height function of a mass and its crease line (saddle ridge)."""

    def __init__(self, mass: Mass, poly: Polygon) -> None:
        self.mass = mass
        u = mass.ridge_dir or _long_axis(poly)
        self.v = (-u[1], u[0])  # across the ridge
        self.u = u
        ring = np.asarray(mass.footprint)
        vs = ring @ np.asarray(self.v)
        self.vmin, self.vmax = float(vs.min()), float(vs.max())
        self.mid = (self.vmin + self.vmax) / 2
        self.half = (self.vmax - self.vmin) / 2

    def height(self, x: float, z: float) -> float:
        m = self.mass
        if m.roof == "flat" or self.half < 1e-6:
            return m.ridge_y
        v = x * self.v[0] + z * self.v[1]
        if m.roof == "saddle":
            return m.ridge_y - (m.ridge_y - m.eave_y) * min(1.0, abs(v - self.mid) / self.half)
        return m.eave_y + (m.ridge_y - m.eave_y) * (v - self.vmin) / (2 * self.half)  # shed

    def crease(self, poly: Polygon) -> LineString | None:
        if self.mass.roof != "saddle" or self.half < 1e-6:
            return None
        minx, minz, maxx, maxz = poly.bounds
        reach = 2 * math.hypot(maxx - minx, maxz - minz) + 10
        cx = self.v[0] * self.mid
        cz = self.v[1] * self.mid
        ux, uz = self.u
        return LineString([(cx - ux * reach, cz - uz * reach), (cx + ux * reach, cz + uz * reach)])


class _Builder:
    def __init__(self, origin: tuple[float, float, float]) -> None:
        self.ox, self.oy, self.oz = origin
        self.pos: list[tuple[float, float, float]] = []
        self.nrm: list[tuple[float, float, float]] = []
        self.uv: list[tuple[float, float]] = []
        self.idx: list[int] = []

    def polygon(self, pts: Sequence[tuple[float, float, float]], uvs: Sequence[tuple[float, float]],
                want: tuple[float, float, float]) -> None:  # fmt: skip
        """Planar convex polygon (3 or 4 points) facing roughly ``want``; flat normal."""
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts[0], pts[1], pts[2]
        ax, ay, az = x1 - x0, y1 - y0, z1 - z0
        bx, by, bz = x2 - x0, y2 - y0, z2 - z0
        nx, ny, nz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
        if len(pts) == 4:
            cx, cy, cz = pts[3][0] - x0, pts[3][1] - y0, pts[3][2] - z0
            nx, ny, nz = nx + by * cz - bz * cy, ny + bz * cx - bx * cz, nz + bx * cy - by * cx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length < 1e-9:
            return
        nx, ny, nz = nx / length, ny / length, nz / length
        order = list(range(len(pts)))
        if nx * want[0] + ny * want[1] + nz * want[2] < 0:
            order.reverse()
            nx, ny, nz = -nx, -ny, -nz
        base = len(self.pos)
        for i in order:
            x, y, z = pts[i]
            self.pos.append((x - self.ox, y - self.oy, z - self.oz))
            self.nrm.append((nx, ny, nz))
            self.uv.append(uvs[i])
        for k in range(1, len(order) - 1):
            self.idx += [base, base + k, base + k + 1]

    def mesh(self) -> MeshData:
        return MeshData(
            # 0.1 mm: equal shapes at different places give equal bytes (shared files).
            np.round(np.asarray(self.pos, dtype=np.float64), 4).astype(np.float32),
            np.asarray(self.nrm, dtype=np.float32),
            np.asarray(self.uv, dtype=np.float32),
            np.asarray(self.idx, dtype=np.uint32),
        )


def _valid_polygon(ring: Sequence[tuple[float, float]]) -> Polygon | None:
    poly = Polygon(ring)
    if not poly.is_valid:
        fixed = shapely.make_valid(poly)
        polys = [g for g in getattr(fixed, "geoms", [fixed]) if isinstance(g, Polygon)]
        if not polys:
            return None
        poly = max(polys, key=lambda g: g.area)
    return poly if poly.area > 1e-6 else None


def _add_mass(b: _Builder, mass: Mass, base_y: float) -> bool:
    poly = _valid_polygon(mass.footprint)
    if poly is None:
        return False
    roof = _Roof(mass, poly)
    crease = roof.crease(poly)

    # Roof: split at the ridge, each piece is planar.
    pieces = list(split(poly, crease).geoms) if crease is not None else [poly]
    for piece in pieces:
        for tri in shapely.constrained_delaunay_triangles(piece).geoms:
            pts = [(x, roof.height(x, z), z) for x, z in list(tri.exterior.coords)[:3]]
            b.polygon(pts, [(x - b.ox, z - b.oz) for x, _, z in pts], (0.0, 1.0, 0.0))

    # Walls along the (oriented) outer ring, split where the ridge crosses an edge.
    ring = list(poly.exterior.coords)[:-1]
    signed = sum(
        ring[i][0] * ring[i - 1][1] - ring[i - 1][0] * ring[i][1] for i in range(len(ring))
    )
    ccw = signed < 0  # shoelace in (x, z), see outward normal below
    pts: list[tuple[float, float]] = []
    for i, a in enumerate(ring):
        c = ring[(i + 1) % len(ring)]
        pts.append(a)
        if crease is not None:
            va = a[0] * roof.v[0] + a[1] * roof.v[1] - roof.mid
            vc = c[0] * roof.v[0] + c[1] * roof.v[1] - roof.mid
            if va * vc < 0:
                t = va / (va - vc)
                pts.append((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t))
    run = 0.0
    for i, a in enumerate(pts):
        c = pts[(i + 1) % len(pts)]
        dx, dz = c[0] - a[0], c[1] - a[1]
        length = math.hypot(dx, dz)
        if length < 1e-4:
            continue
        out = (dz / length, -dx / length) if ccw else (-dz / length, dx / length)
        ha, hc = roof.height(*a), roof.height(*c)
        quad = [(a[0], base_y, a[1]), (c[0], base_y, c[1]), (c[0], hc, c[1]), (a[0], ha, a[1])]
        uvs = [(run, 0.0), (run + length, 0.0), (run + length, hc - base_y), (run, ha - base_y)]
        b.polygon(quad, uvs, (out[0], 0.0, out[1]))
        run += length
    return True


def build_mesh(masses: Sequence[Mass], base_y: float, origin_xz: tuple[float, float]) -> MeshData:
    """One mesh for all masses; vertices relative to (origin x, base_y, origin z)."""
    b = _Builder((origin_xz[0], base_y, origin_xz[1]))
    for mass in masses:
        _add_mass(b, mass, base_y)
    if not b.idx:
        raise ValueError("no usable geometry")
    return b.mesh()
