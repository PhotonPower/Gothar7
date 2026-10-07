"""Trees, bushes and grass for the lanes and yards (W6 streets): ``assets/source/vegetation/``.

Built by script like the mobs and props (``mobs.Mesh``, the shared texture folder), not tied to a
place. Crowns are closed low-poly blobs with the ``leaves`` texture and rounded normals (no alpha
cut-outs), trunks are tapered prisms with ``bark``.

- Trees carry detail levels (asset.md "Detailstufen"): ``<name>``, ``<name>_lod1`` (coarse
  blobs), ``<name>_lod2`` (one blob); they collide at the trunk only (``COL_HULL_TRUNK``).
- Bushes and grass do not collide: a model without ``COL_`` would collide with all its triangles
  (asset.md), so each carries a tiny ``COL_HULL_NONE`` box a metre under the ground.

Axes as for the props: Y up, origin on the ground in the middle of the trunk or tuft.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np

from gothar_worldgen.mobs import Mesh, MobModel, Vec3, box_body

# a box a metre under the ground: the model has a COL_ node and so collides with nothing
NO_COLLISION = (("NONE", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02)),)


def frustum(m: Mesh, material: str, p0: Vec3, p1: Vec3, r0: float, r1: float,
            sides: int = 8) -> None:  # fmt: skip
    """A tapered prism from ``p0`` (radius ``r0``) to ``p1`` (``r1``), open at both ends (in the
    ground or in a crown); u around it, v along it."""
    b = m.b(material)
    axis = np.subtract(p1, p0)
    length = float(np.linalg.norm(axis))
    if length < 1e-6:
        return
    w = axis / length
    helper = (
        np.array([1.0, 0.0, 0.0]) if abs(w[1]) > 0.9 or abs(w[0]) < 0.9 else np.array([0, 1, 0])
    )
    u = np.cross(w, helper)
    u /= np.linalg.norm(u)
    v = np.cross(w, u)
    angs = [2 * math.pi * k / sides for k in range(sides + 1)]
    circ = 2 * math.pi * (r0 + r1) / 2
    for k in range(sides):
        a0, a1 = angs[k], angs[k + 1]
        d0 = math.cos(a0) * u + math.sin(a0) * v
        d1 = math.cos(a1) * u + math.sin(a1) * v
        pts = [tuple(np.add(p0, r0 * d0)), tuple(np.add(p0, r0 * d1)),
               tuple(np.add(p1, r1 * d1)), tuple(np.add(p1, r1 * d0))]  # fmt: skip
        uu0, uu1 = circ * k / sides, circ * (k + 1) / sides
        mid = (d0 + d1) / 2
        b.polygon(pts, [(uu0, 0.0), (uu1, 0.0), (uu1, length), (uu0, length)],
                  (float(mid[0]), float(mid[1]), float(mid[2])))  # fmt: skip


def _icosphere(subdiv: int) -> tuple[np.ndarray, list[tuple[int, int, int]]]:
    t = (1 + math.sqrt(5)) / 2
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t),
             (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]  # fmt: skip
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
             (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
             (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]  # fmt: skip
    pts = [np.array(p, dtype=np.float64) / np.linalg.norm(p) for p in verts]

    def mid(a: int, b: int, cache: dict[tuple[int, int], int]) -> int:
        key = (min(a, b), max(a, b))
        if key not in cache:
            p = pts[a] + pts[b]
            pts.append(p / np.linalg.norm(p))
            cache[key] = len(pts) - 1
        return cache[key]

    for _ in range(subdiv):
        cache: dict[tuple[int, int], int] = {}
        nxt = []
        for a, b, c in faces:
            ab, bc, ca = mid(a, b, cache), mid(b, c, cache), mid(c, a, cache)
            nxt += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = nxt
    return np.array(pts), faces


def blob(m: Mesh, material: str, centre: Vec3, radii: Vec3, subdiv: int,
         rng: np.random.Generator, bumps: float = 0.18) -> None:  # fmt: skip
    """A lumpy ellipsoid (a clump of leaves) with rounded normals; flattened a little below."""
    b = m.b(material)
    unit, faces = _icosphere(subdiv)
    lumps = 1.0 + bumps * (rng.random(len(unit)) - 0.5) * 2
    r = np.asarray(radii)
    pts = unit * r * lumps[:, None]
    pts[:, 1] = np.where(pts[:, 1] < 0, pts[:, 1] * 0.7, pts[:, 1])  # crowns are flatter below
    normals = unit / r * r.mean()  # the ellipsoid's normal direction
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    base = len(b.pos)
    c = np.asarray(centre)
    for p, n in zip(pts, normals, strict=True):
        q = p + c
        b.pos.append((float(q[0]), float(q[1]), float(q[2])))
        b.nrm.append((float(n[0]), float(n[1]), float(n[2])))
        b.uv.append((float(q[0] + q[2]), float(q[1])))  # the texture is even: seams do not show
    for f in faces:
        b.idx += [base + f[0], base + f[1], base + f[2]]  # icosphere faces wind outwards


@dataclass(frozen=True)
class TreeSpec:
    trunk_h: float  # where the trunk ends in the crown
    trunk_r: tuple[float, float]  # radius at the foot, at the top
    lean: tuple[float, float]  # the trunk's top offset (x, z)
    crown_y: float  # middle of the crown
    crown_r: tuple[float, float]  # horizontal, vertical half size
    clumps: int  # blobs around the middle one
    leaves: str  # material
    seed: int


TREES = {
    # a lime tree as at wells and squares: tall round crown
    "tree_linden": TreeSpec(2.8, (0.3, 0.2), (0.1, 0.05), 6.2, (3.4, 3.2), 6, "leaves_linden", 11),
    # an old oak: thick crooked trunk, broad low crown
    "tree_oak": TreeSpec(2.4, (0.42, 0.28), (-0.25, 0.2), 5.4, (4.2, 2.8), 7, "leaves_oak", 23),
    # an apple or pear tree in a garden
    "tree_fruit": TreeSpec(1.4, (0.16, 0.11), (0.15, -0.1), 2.9, (2.0, 1.6), 5, "leaves_fruit", 37),
}


def _crown(spec: TreeSpec, rng: np.random.Generator) -> list[tuple[Vec3, Vec3]]:
    """The crown's blobs: (centre, radii); the first is the middle one."""
    rh, rv = spec.crown_r
    lx, lz = spec.lean
    out: list[tuple[Vec3, Vec3]] = [((lx, spec.crown_y, lz), (rh * 0.72, rv * 0.75, rh * 0.72))]
    for k in range(spec.clumps):
        a = 2 * math.pi * (k + rng.uniform(-0.2, 0.2)) / spec.clumps
        d = rh * rng.uniform(0.42, 0.55)
        y = spec.crown_y + rv * rng.uniform(-0.35, 0.4)
        r = rh * rng.uniform(0.42, 0.52)
        out.append(((lx + d * math.cos(a), y, lz + d * math.sin(a)), (r, r * rv / rh, r)))
    return out


def tree(name: str) -> MobModel:
    spec = TREES[name]
    rng = np.random.default_rng(spec.seed)
    crown = _crown(spec, rng)
    top = (spec.lean[0], spec.trunk_h, spec.lean[1])
    levels = []
    for sides, subdiv, branches in ((8, 1, True), (6, 0, False), (4, 0, False)):
        m = Mesh()
        frustum(m, "bark", (0.0, -0.3, 0.0), top, spec.trunk_r[0], spec.trunk_r[1], sides)
        lumps = np.random.default_rng(spec.seed + 1)  # the same lumps on every level
        if subdiv == 0 and sides == 4:  # lod 2: one blob around the whole crown
            rh, rv = spec.crown_r
            blob(m, spec.leaves, (spec.lean[0], spec.crown_y, spec.lean[1]),
                 (rh * 0.95, rv * 0.95, rh * 0.95), 0, lumps, 0.1)  # fmt: skip
        else:
            for centre, radii in crown:
                blob(m, spec.leaves, centre, radii, subdiv, lumps)
                if branches and centre != crown[0][0]:  # boughs into the side clumps
                    end = tuple(np.add(top, 0.6 * np.subtract(centre, top)))
                    frustum(m, "bark", top, end, spec.trunk_r[1] * 0.6, 0.04, 5)
        levels.append(m)
    lo = (-spec.trunk_r[0], 0.0, -spec.trunk_r[0])
    hi = (spec.trunk_r[0], spec.trunk_h, spec.trunk_r[0])
    levels[0].collision.append(box_body("COL_HULL_TRUNK", lo, hi))
    return MobModel(name, levels[0], lods=levels[1:])


@dataclass(frozen=True)
class BushSpec:
    radius: float
    height: float
    clumps: int
    leaves: str
    seed: int


BUSHES = {
    "bush_hazel": BushSpec(0.9, 1.7, 4, "leaves_fruit", 51),  # loose, taller
    "bush_box": BushSpec(0.55, 0.8, 3, "leaves_box", 53),  # low, dense (gardens)
}


def bush(name: str) -> MobModel:
    spec = BUSHES[name]
    levels = []
    for subdiv in (1, 0):
        m = Mesh()
        rng = np.random.default_rng(spec.seed)
        h = spec.height / 2
        blob(m, spec.leaves, (0.0, h, 0.0), (spec.radius * 0.8, h, spec.radius * 0.8), subdiv, rng)
        for k in range(spec.clumps):
            a = 2 * math.pi * k / spec.clumps + rng.uniform(-0.3, 0.3)
            d = spec.radius * 0.45
            r = spec.radius * rng.uniform(0.45, 0.6)
            y = h * rng.uniform(0.7, 1.1)
            blob(
                m, spec.leaves, (d * math.cos(a), y, d * math.sin(a)), (r, r * 1.1, r), subdiv, rng
            )
        levels.append(m)
    _no_collision(levels[0])
    return MobModel(name, levels[0], lods=levels[1:])


@dataclass(frozen=True)
class TuftSpec:
    blades: int
    height: tuple[float, float]
    width: float
    spread: float
    material: str
    seed: int


TUFTS = {
    "grass_tuft": TuftSpec(18, (0.2, 0.4), 0.05, 0.16, "grass", 61),  # at wall feet
    "weeds": TuftSpec(12, (0.35, 0.65), 0.08, 0.22, "grass_dry", 67),  # in corners, by the wall
}


def tuft(name: str) -> MobModel:
    """Blades as thin wedges, both sides (no double-sided material needed)."""
    spec = TUFTS[name]
    rng = np.random.default_rng(spec.seed)
    m = Mesh()
    b = m.b(spec.material)
    for _ in range(spec.blades):
        a = rng.uniform(0, 2 * math.pi)
        d = spec.spread * math.sqrt(rng.random())
        x, z = d * math.cos(a), d * math.sin(a)
        h = rng.uniform(*spec.height)
        lean = rng.uniform(0.15, 0.45) * h
        face = rng.uniform(0, 2 * math.pi)
        side = (math.cos(face) * spec.width / 2, math.sin(face) * spec.width / 2)
        tip = (x + math.cos(a) * lean, h, z + math.sin(a) * lean)
        tri = [(x - side[0], 0.0, z - side[1]), (x + side[0], 0.0, z + side[1]), tip]
        n = (-math.sin(face), 0.3, math.cos(face))
        uvs = [(0.0, 0.0), (spec.width, 0.0), (spec.width / 2, h)]
        b.polygon(tri, uvs, n)
        b.polygon(tri, uvs, (-n[0], n[1], -n[2]))
    _no_collision(m)
    return MobModel(name, m)


def _no_collision(m: Mesh) -> None:
    for name, lo, hi in NO_COLLISION:
        m.collision.append(box_body(f"COL_HULL_{name}", lo, hi))


VEGETATION = {
    **{n: (lambda n=n: tree(n)) for n in TREES},
    **{n: (lambda n=n: bush(n)) for n in BUSHES},
    **{n: (lambda n=n: tuft(n)) for n in TUFTS},
}  # assets/source/vegetation/<name>.glb


def crown_radius(name: str) -> float:
    """Half the crown's width (placement keeps it off walls)."""
    if name in TREES:
        return TREES[name].crown_r[0]
    if name in BUSHES:
        return BUSHES[name].radius
    return TUFTS[name].spread


def names() -> Sequence[str]:
    return tuple(VEGETATION)
