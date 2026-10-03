"""Pure-Python parts of the market fountain (W6), shared by the Blender script and the tests.

The fountain is the project owner's own model
(``data/leonberg/marktbrunnen/marktbrunnen_quelle.glb``), adapted to Leonberg around 1700 by
``build_marktbrunnen.py``. This module adds what the script builds itself, from
``data/leonberg/marktbrunnen.json``:

- spouts: a bronze pipe on the column with a water jet falling into the basin (``spouts``);
- collision bodies (``COL_HULL_*``, docs/modules/asset.md): steps, the eight trough walls, the
  planting bowl and the column with the figure (``collision``).

Coordinates are glTF/worldgen: x east, y up, z south, metres, origin at the foot of the fountain.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

Point3 = tuple[float, float, float]


@dataclass
class Parts:
    faces: dict[str, list[list[Point3]]] = field(default_factory=dict)  # material -> polygons

    def add(self, material: str, polygon: list[Point3]) -> None:
        self.faces.setdefault(material, []).append(polygon)

    def triangles(self) -> int:
        return sum(len(p) - 2 for faces in self.faces.values() for p in faces)


def _ring(cx: float, cy: float, cz: float, axis: Point3, radius: float, sides: int) -> list[Point3]:
    """Circle of ``sides`` points around ``axis`` (unit) at (cx, cy, cz)."""
    ax, ay, az = axis
    ref = (0.0, 1.0, 0.0) if abs(ay) < 0.9 else (1.0, 0.0, 0.0)
    ux = ay * ref[2] - az * ref[1]
    uy = az * ref[0] - ax * ref[2]
    uz = ax * ref[1] - ay * ref[0]
    n = math.sqrt(ux * ux + uy * uy + uz * uz)
    ux, uy, uz = ux / n, uy / n, uz / n
    vx, vy, vz = ay * uz - az * uy, az * ux - ax * uz, ax * uy - ay * ux
    out = []
    for k in range(sides):
        a = 2 * math.pi * k / sides
        c, s = math.cos(a) * radius, math.sin(a) * radius
        out.append((cx + ux * c + vx * s, cy + uy * c + vy * s, cz + uz * c + vz * s))
    return out


def _tube(
    parts: Parts,
    material: str,
    path: list[Point3],
    radii: list[float],
    sides: int,
    cap_end: bool = False,
) -> None:
    """Tube along ``path`` (quads between rings); optionally closes the last ring."""
    rings = []
    for i, p in enumerate(path):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, len(path) - 1)]
        d = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        n = math.sqrt(sum(c * c for c in d)) or 1.0
        rings.append(_ring(*p, (d[0] / n, d[1] / n, d[2] / n), radii[i], sides))
    for r0, r1 in zip(rings, rings[1:], strict=False):
        for k in range(sides):
            j = (k + 1) % sides
            parts.add(material, [r0[k], r0[j], r1[j], r1[k]])
    if cap_end:
        parts.add(material, list(reversed(rings[-1])))


def spouts(spec: dict) -> Parts:
    """``spec["spouts"]``: pipes from the column, each with a jet falling to the water level.

    Keys: ``directionsDeg`` (angle in the x-z plane, 0 = +x, 90 = +z), ``y`` (height at the
    column), ``fromR``/``toR`` (pipe start and mouth, distance from the axis), ``dropM`` (how much
    the pipe falls), ``radius``, ``waterY`` (surface), ``reachR`` (where the jet meets it),
    ``pipe``/``jet`` (material names), ``sides``.
    """
    s = spec["spouts"]
    parts = Parts()
    sides = int(s.get("sides", 8))
    for deg in s["directionsDeg"]:
        a = math.radians(float(deg))
        dx, dz = math.cos(a), math.sin(a)

        def at(r: float, y: float, dx: float = dx, dz: float = dz) -> Point3:
            return (round(dx * r, 4), round(y, 4), round(dz * r, 4))

        y0, r0, r1 = float(s["y"]), float(s["fromR"]), float(s["toR"])
        y1 = y0 - float(s["dropM"])
        rad = float(s["radius"])
        _tube(parts, s["pipe"], [at(r0, y0), at(r1, y1)], [rad, rad], sides)
        # mouth: a short flared ring at the end of the pipe
        r_mouth = r1 + 0.03
        _tube(parts, s["pipe"], [at(r1, y1), at(r_mouth, y1 - 0.004)], [rad, rad * 1.6], sides)
        # jet: parabola from the mouth to the water surface, slightly thinning
        water, reach = float(s["waterY"]), float(s["reachR"])
        n = 8
        path, radii = [], []
        for i in range(n + 1):
            t = i / n
            r = r_mouth + (reach - r_mouth) * t
            y = (y1 - 0.004) + (water - (y1 - 0.004)) * t * t
            path.append(at(r, y))
            radii.append(rad * (0.9 - 0.3 * t))
        _tube(parts, s["jet"], path, radii, max(4, sides - 2))
    return parts


def prism(
    radius: float, y0: float, y1: float, sides: int = 8, phase_deg: float = 0.0
) -> tuple[list[Point3], list[tuple[int, int, int]]]:
    """Closed regular prism (corners at ``phase_deg`` + k * 360/sides), outward winding."""
    pts: list[Point3] = []
    for y in (y0, y1):
        for k in range(sides):
            a = math.radians(phase_deg + 360.0 * k / sides)
            pts.append(
                (round(radius * math.cos(a), 4), round(y, 4), round(radius * math.sin(a), 4))
            )
    # Corners run counter-clockwise in (x, z); with z south that is clockwise seen from +y.
    tris: list[tuple[int, int, int]] = []
    for k in range(1, sides - 1):
        tris.append((0, k, k + 1))  # bottom, facing down
        tris.append((sides, sides + k + 1, sides + k))  # top, facing up
    for k in range(sides):
        j = (k + 1) % sides
        tris += [(k, sides + j, j), (k, sides + k, sides + j)]
    return pts, tris


def _box(
    corners: list[tuple[float, float]], y0: float, y1: float
) -> tuple[list[Point3], list[tuple[int, int, int]]]:
    """Closed prism over a convex quad (x, z), counter-clockwise seen from above (+y)."""
    pts = [(x, y0, z) for x, z in corners] + [(x, y1, z) for x, z in corners]
    tris = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7)]
    for k in range(4):
        j = (k + 1) % 4
        tris += [(k, j, 4 + j), (k, 4 + j, 4 + k)]
    return pts, tris


def collision(spec: dict) -> list[tuple[str, list[Point3], list[tuple[int, int, int]]]]:
    """``COL_HULL_<i>`` bodies: steps, eight trough walls, planting bowl, column with figure."""
    c = spec["collision"]
    phase = float(c.get("phaseDeg", 0.0))
    bodies = []
    for st in c["steps"]:
        bodies.append(prism(float(st["radius"]), float(st["y0"]), float(st["y1"]), 8, phase))
    t = c["trough"]
    outer, inner = float(t["outer"]), float(t["inner"])
    for k in range(8):
        a0, a1 = math.radians(phase + 45.0 * k), math.radians(phase + 45.0 * (k + 1))
        quad = [
            (outer * math.cos(a0), outer * math.sin(a0)),
            (inner * math.cos(a0), inner * math.sin(a0)),
            (inner * math.cos(a1), inner * math.sin(a1)),
            (outer * math.cos(a1), outer * math.sin(a1)),
        ]
        # counter-clockwise seen from +y means clockwise in (x, z) with z south: check the sign
        area = sum(quad[i - 1][0] * quad[i][1] - quad[i][0] * quad[i - 1][1] for i in range(4))
        if area > 0:
            quad.reverse()
        bodies.append(
            _box([(round(x, 4), round(z, 4)) for x, z in quad], float(t["y0"]), float(t["y1"]))
        )
    for p in c["prisms"]:
        bodies.append(prism(float(p["radius"]), float(p["y0"]), float(p["y1"]), 8, phase))
    return [(f"COL_HULL_{i}", pts, tris) for i, (pts, tris) in enumerate(bodies)]
