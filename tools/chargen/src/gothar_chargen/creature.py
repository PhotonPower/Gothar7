"""Own monsters from a body description (F5): spec, zones and the procedural fur texture.

A creature is described in ``data/monsters/<art>.creature.toml``: the bones of its rig (contract
§7.1), primitive shapes that are merged into one body surface (``blender/build_creature.py``) and
colour zones. Everything here is pure Python/numpy so it runs both inside Blender and in tests.

Coordinates are Blender's, like the rig TOMLs: x = left, -y = forward, z = up, metres. The tool
moves everything so that ``root`` (origin) lies on the ground below the pelvis head.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

SHAPE_KINDS = ("ellipsoid", "capsule", "chain", "ridge", "cone", "cut")
REQUIRED_BONES = ("pelvis", "neck_01", "head")
ZONE_SOFTNESS = 0.012  # metres: zones blend over about this distance
CHUNK = 65536  # texels per batch (memory)


class CreatureError(ValueError):
    """Invalid creature description."""


@dataclass(frozen=True)
class Bone:
    name: str
    parent: str | None
    head: tuple[float, float, float]
    tail: tuple[float, float, float]


@dataclass(frozen=True)
class Shape:
    """A primitive. ellipsoid: center, size (half axes), rotate (Euler XYZ degrees);
    capsule/cone: a, b, r, r2 (radius at b; cone tip r2 = 0); cut: an ellipsoid removed from the
    body after the first remesh (mouth slit)."""

    kind: str
    zone: str
    center: tuple[float, float, float] = (0.0, 0.0, 0.0)
    size: tuple[float, float, float] = (1.0, 1.0, 1.0)
    rotate: tuple[float, float, float] = (0.0, 0.0, 0.0)
    a: tuple[float, float, float] = (0.0, 0.0, 0.0)
    b: tuple[float, float, float] = (0.0, 0.0, 0.0)
    r: float = 0.0
    r2: float = 0.0


@dataclass(frozen=True)
class Zone:
    """Colour zone of the fur texture (all colours "#rrggbb", sRGB)."""

    colour: str
    colour2: str = ""  # second colour, mixed in by large-scale noise
    variation: float = 0.15  # brightness noise
    strands: float = 0.0  # fur streaks along the hair flow (0 = bare skin)
    stripes: float = 0.0  # dark bands across the body axis on the flanks
    stripe_colour: str = "#000000"
    spots: float = 0.0  # dark mottling
    belly: float = 0.0  # lighter towards the underside
    wrinkles: float = 0.0  # fine skin creases


@dataclass(frozen=True)
class Creature:
    art: str
    material: str
    voxel: float
    smooth: int
    triangles: int
    lods: tuple[float, ...]
    texture: int
    seed: int
    ao: float
    bones: tuple[Bone, ...]
    sockets: dict[str, dict]
    shapes: tuple[Shape, ...]
    zones: dict[str, Zone]
    stripe_spacing: float = 0.09
    extra: dict = field(default_factory=dict)

    def bone(self, name: str) -> Bone:
        return next(b for b in self.bones if b.name == name)


def _v3(v: object, what: str) -> tuple[float, float, float]:
    if not (isinstance(v, list) and len(v) == 3):
        raise CreatureError(f"{what}: expected [x, y, z], got {v!r}")
    return (float(v[0]), float(v[1]), float(v[2]))


def _mirror_name(name: str) -> str:
    if name.endswith("_l"):
        return name[:-2] + "_r"
    raise CreatureError(f"mirrored bone {name!r} must end in _l")


def _mx(v: tuple[float, float, float]) -> tuple[float, float, float]:
    return (-v[0], v[1], v[2])


def _parse_bones(items: list[dict]) -> list[Bone]:
    bones: list[Bone] = []
    for it in items:
        b = Bone(
            it["name"], it.get("parent"), _v3(it["head"], it["name"]), _v3(it["tail"], it["name"])
        )
        bones.append(b)
        if it.get("mirror"):
            parent = b.parent
            if parent and parent.endswith("_l"):
                parent = _mirror_name(parent)
            bones.append(Bone(_mirror_name(b.name), parent, _mx(b.head), _mx(b.tail)))
    names = [b.name for b in bones]
    if len(set(names)) != len(names):
        raise CreatureError("duplicate bone names")
    for b in bones:
        if b.parent is not None and b.parent not in names:
            raise CreatureError(f"bone {b.name}: unknown parent {b.parent}")
        if b.name == "root":
            raise CreatureError("root is added by the tool")
    for req in REQUIRED_BONES:
        if req not in names:
            raise CreatureError(f"required bone {req} missing")
    return bones


def _parse_shapes(items: list[dict], zones: dict[str, Zone]) -> list[Shape]:
    shapes: list[Shape] = []
    for i, it in enumerate(items):
        kind = it.get("kind", "")
        if kind not in SHAPE_KINDS:
            raise CreatureError(f"shape {i}: kind {kind!r} not in {SHAPE_KINDS}")
        zone = it.get("zone", "")
        if zone not in zones:
            raise CreatureError(f"shape {i}: unknown zone {zone!r}")
        new: list[Shape] = []
        at = f"shape {i}"
        rot = _v3(it.get("rotate", [0, 0, 0]), at)
        if kind in ("ellipsoid", "cut"):
            new.append(
                Shape(
                    kind, zone, center=_v3(it["center"], at), size=_v3(it["size"], at), rotate=rot
                )
            )
        elif kind in ("capsule", "cone"):
            r = float(it["r"])
            r2 = 0.0 if kind == "cone" else float(it.get("r2", r))
            new.append(Shape("capsule", zone, a=_v3(it["a"], at), b=_v3(it["b"], at), r=r, r2=r2))
        elif kind == "chain":
            pts = [_v3(p, at) for p in it["points"]]
            radii = [float(r) for r in it["radii"]]
            if len(radii) != len(pts) or len(pts) < 2:
                raise CreatureError(f"shape {i}: chain needs >= 2 points and one radius per point")
            for k in range(len(pts) - 1):
                new.append(
                    Shape("capsule", zone, a=pts[k], b=pts[k + 1], r=radii[k], r2=radii[k + 1])
                )
        else:  # ridge: ellipsoids along a polyline, sizes interpolated
            pts = np.array([_v3(p, at) for p in it["points"]])
            sizes = np.array([_v3(s, at) for s in it["sizes"]])
            seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
            along = np.concatenate([[0.0], np.cumsum(seg)]) / max(seg.sum(), 1e-9)
            steps = np.linspace(0, 1, len(sizes))
            for t in np.linspace(0.0, 1.0, int(it["count"])):
                c = tuple(float(np.interp(t, along, pts[:, k])) for k in range(3))
                s = tuple(float(np.interp(t, steps, sizes[:, k])) for k in range(3))
                new.append(Shape("ellipsoid", zone, center=c, size=s, rotate=rot))  # type: ignore[arg-type]
        shapes += new
        if it.get("mirror"):
            for s in new:
                mirrored_rot = (s.rotate[0], -s.rotate[1], -s.rotate[2])
                shapes.append(
                    Shape(
                        s.kind,
                        s.zone,
                        _mx(s.center),
                        s.size,
                        mirrored_rot,
                        _mx(s.a),
                        _mx(s.b),
                        s.r,
                        s.r2,
                    )
                )
    return shapes


def load_creature(path: Path) -> Creature:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    zones = {name: Zone(**z) for name, z in data.get("zone", {}).items()}
    if not zones:
        raise CreatureError("no zones")
    bones = _parse_bones(data.get("bone", []))
    shapes = _parse_shapes(data.get("shape", []), zones)
    pelvis = next(b for b in bones if b.name == "pelvis").head
    shift = np.array([pelvis[0], pelvis[1], 0.0])  # root on the ground below the pelvis

    def sh(v: tuple[float, float, float]) -> tuple[float, float, float]:
        return tuple(float(x) for x in np.array(v) - shift)  # type: ignore[return-value]

    bones = [Bone(b.name, b.parent, sh(b.head), sh(b.tail)) for b in bones]
    shapes = [
        Shape(s.kind, s.zone, sh(s.center), s.size, s.rotate, sh(s.a), sh(s.b), s.r, s.r2)
        for s in shapes
    ]
    sockets = data.get("sockets", {})
    names = {b.name for b in bones}
    for name, spec in sockets.items():
        if spec.get("parent") not in names:
            raise CreatureError(f"socket {name}: unknown parent {spec.get('parent')}")
    if "socket_mouth" not in sockets:
        raise CreatureError("socket_mouth missing (contract §7.1)")
    lods = tuple(float(x) for x in data.get("lods", [1.0, 0.5, 0.25]))
    if lods[0] != 1.0 or any(b >= a for a, b in zip(lods, lods[1:], strict=False)):
        raise CreatureError("lods must start at 1.0 and decrease")
    return Creature(
        art=data["art"],
        material=data.get("material", "fur"),
        voxel=float(data.get("voxel", 0.006)),
        smooth=int(data.get("smooth", 6)),
        triangles=int(data["triangles"]),
        lods=lods,
        texture=int(data.get("texture", 1024)),
        seed=int(data.get("seed", 0)),
        ao=float(data.get("ao", 0.0)),
        bones=tuple(bones),
        sockets=sockets,
        shapes=tuple(shapes),
        zones=zones,
        stripe_spacing=float(data.get("stripe_spacing", 0.09)),
    )


# --- geometry helpers -----------------------------------------------------------------------------


def euler_matrix(deg: tuple[float, float, float]) -> np.ndarray:
    """Blender Euler XYZ (degrees) as a 3x3 matrix (R = Rz @ Ry @ Rx)."""
    x, y, z = np.radians(deg)
    rx = np.array([[1, 0, 0], [0, np.cos(x), -np.sin(x)], [0, np.sin(x), np.cos(x)]])
    ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    rz = np.array([[np.cos(z), -np.sin(z), 0], [np.sin(z), np.cos(z), 0], [0, 0, 1]])
    return rz @ ry @ rx


def shape_distance(s: Shape, p: np.ndarray) -> np.ndarray:
    """Approximate signed distance of points p (N, 3) to the shape surface (negative inside)."""
    if s.kind in ("ellipsoid", "cut"):
        q = (p - np.array(s.center)) @ euler_matrix(s.rotate)  # into the local frame
        size = np.array(s.size)
        k = np.linalg.norm(q / size, axis=1)
        return (k - 1.0) * size.min()
    a, b = np.array(s.a), np.array(s.b)
    ab = b - a
    t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-12), 0.0, 1.0)
    closest = a + t[:, None] * ab
    return np.linalg.norm(p - closest, axis=1) - (s.r + (s.r2 - s.r) * t)


def bone_flow(bones: tuple[Bone, ...], p: np.ndarray) -> np.ndarray:
    """Hair flow per point: the direction of the nearest bone (unit vectors, sign irrelevant)."""
    heads = np.array([b.head for b in bones])
    tails = np.array([b.tail for b in bones])
    d = tails - heads
    best = np.full(len(p), np.inf)
    flow = np.zeros_like(p)
    for h, v in zip(heads, d, strict=True):
        t = np.clip(((p - h) @ v) / max(v @ v, 1e-12), 0.0, 1.0)
        dist = np.linalg.norm(p - (h + t[:, None] * v), axis=1)
        closer = dist < best
        best[closer] = dist[closer]
        flow[closer] = v / max(np.linalg.norm(v), 1e-12)
    return flow


def zone_weights(c: Creature, p: np.ndarray) -> tuple[list[str], np.ndarray]:
    """Soft zone weights (N, Z) at surface points: nearer shapes count more."""
    names = list(c.zones)
    w = np.zeros((len(p), len(names)))
    dists = np.stack([np.abs(shape_distance(s, p)) for s in c.shapes], axis=1)
    near = dists.min(axis=1, keepdims=True)
    contrib = np.exp(-(dists - near) / ZONE_SOFTNESS)
    for k, s in enumerate(c.shapes):
        w[:, names.index(s.zone)] += contrib[:, k]
    return names, w / w.sum(axis=1, keepdims=True)


# --- noise ----------------------------------------------------------------------------------------


def _lattice(seed: int) -> np.ndarray:
    return np.random.default_rng(seed).random(4096)


def value_noise3(p: np.ndarray, seed: int) -> np.ndarray:
    """Smooth value noise in 0..1 at points p (N, 3), unit lattice."""
    table = _lattice(seed)
    i = np.floor(p).astype(np.int64)
    f = p - i
    u = f * f * (3 - 2 * f)
    out = np.zeros(len(p))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                h = (
                    (i[:, 0] + dx) * 73856093
                    ^ (i[:, 1] + dy) * 19349663
                    ^ (i[:, 2] + dz) * 83492791
                )
                v = table[h % len(table)]
                wx = u[:, 0] if dx else 1 - u[:, 0]
                wy = u[:, 1] if dy else 1 - u[:, 1]
                wz = u[:, 2] if dz else 1 - u[:, 2]
                out += v * wx * wy * wz
    return out


def fractal3(p: np.ndarray, seed: int, octaves: int = 4) -> np.ndarray:
    out, amp, total = np.zeros(len(p)), 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise3(p * (2.0**o), seed + o)
        total += amp
        amp *= 0.5
    return out / total


def srgb(hex_colour: str) -> np.ndarray:
    return np.array([int(hex_colour[i : i + 2], 16) / 255.0 for i in (1, 3, 5)])


def surface_colour(c: Creature, p: np.ndarray, n: np.ndarray) -> np.ndarray:
    """sRGB colour (N, 3) of surface points p with normals n."""
    names, w = zone_weights(c, p)
    flow = bone_flow(c.bones, p)
    seed = c.seed
    big = fractal3(p * 6.0, seed + 1, 3)  # patches
    fine = fractal3(p * 40.0, seed + 2, 3)
    # strands: noise stretched along the hair flow
    along = np.sum(p * flow, axis=1)[:, None] * flow
    strand = fractal3((p - along) * 260.0 + along * 18.0, seed + 3, 2)
    # stripes: bands across the body axis (y), warped
    warp = fractal3(p * 9.0, seed + 4, 2)
    band = 0.5 + 0.5 * np.cos(
        2 * np.pi * (p[:, 1] / c.stripe_spacing + 1.6 * warp + 0.3 * p[:, 2] / c.stripe_spacing)
    )
    stripe = np.clip((band - 0.72) * 5.0, 0, 1) * np.clip((np.abs(n[:, 0]) - 0.25) * 2.5, 0, 1)
    spot = np.clip((fractal3(p * 22.0, seed + 5, 3) - 0.58) * 6.0, 0, 1)
    crease = np.clip(
        1 - np.abs(fractal3(p * np.array([25.0, 25.0, 90.0]), seed + 6, 2) - 0.5) / 0.05, 0, 1
    )
    under = np.clip(-n[:, 2] * 1.5, 0, 1)
    out = np.zeros((len(p), 3))
    for k, name in enumerate(names):
        z = c.zones[name]
        if not w[:, k].any():
            continue
        col = np.tile(srgb(z.colour), (len(p), 1))
        if z.colour2:
            col = col + (srgb(z.colour2) - col) * np.clip((big - 0.35) * 2.5, 0, 1)[:, None]
        col *= (1 - z.variation + 2 * z.variation * fine)[:, None]
        if z.strands:
            col *= (1 - z.strands * 0.45 + z.strands * 0.7 * strand)[:, None]
        if z.stripes:
            col += (srgb(z.stripe_colour) - col) * (z.stripes * stripe)[:, None]
        if z.spots:
            col += (srgb(z.stripe_colour) - col) * (z.spots * spot)[:, None]
        if z.belly:
            col += (1.0 - col) * (z.belly * under)[:, None] * 0.5
        if z.wrinkles:
            col *= (1 - z.wrinkles * 0.35 * crease)[:, None]
        out += w[:, k : k + 1] * col
    return np.clip(out, 0, 1)


# --- texture baking -------------------------------------------------------------------------------


def rasterize(
    tri_uv: np.ndarray, tri_pos: np.ndarray, tri_nrm: np.ndarray, size: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Surface position and normal per texel from UV triangles (T, 3, 2) in 0..1 (v up).

    Returns (pos (S, S, 3), nrm (S, S, 3), mask (S, S) bool); image row 0 = v 1.
    """
    pos = np.zeros((size, size, 3))
    nrm = np.zeros((size, size, 3))
    mask = np.zeros((size, size), dtype=bool)
    px = tri_uv * size - 0.5  # texel centres at integer coordinates
    px[..., 1] = (size - 1) - (tri_uv[..., 1] * size - 0.5)
    for t in range(len(tri_uv)):
        q = px[t]
        x0, y0 = np.floor(q.min(axis=0)).astype(int)
        x1, y1 = np.ceil(q.max(axis=0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        ys, xs = np.mgrid[y0 : y1 + 1, x0 : x1 + 1]
        a, b, c = q
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            continue
        l0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / den
        l1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
        if not inside.any():
            continue
        lam = np.stack([l0[inside], l1[inside], l2[inside]], axis=1)
        yy, xx = ys[inside], xs[inside]
        pos[yy, xx] = lam @ tri_pos[t]
        nrm[yy, xx] = lam @ tri_nrm[t]
        mask[yy, xx] = True
    length = np.linalg.norm(nrm, axis=2, keepdims=True)
    nrm = np.where(length > 1e-9, nrm / np.maximum(length, 1e-9), 0.0)
    return pos, nrm, mask


def dilate(img: np.ndarray, mask: np.ndarray, steps: int = 8) -> np.ndarray:
    """Grows the filled texels into the empty ones (no dark seams with mipmaps)."""
    img, mask = img.copy(), mask.copy()
    for _ in range(steps):
        acc = np.zeros_like(img)
        cnt = np.zeros(mask.shape)
        pm = np.pad(mask, 1)
        pi = np.pad(img, ((1, 1), (1, 1), (0, 0)))
        h, w = mask.shape
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            m = pm[1 + dy : 1 + dy + h, 1 + dx : 1 + dx + w]  # neighbour, no wrap at the edges
            acc += pi[1 + dy : 1 + dy + h, 1 + dx : 1 + dx + w] * m[..., None]
            cnt += m
        grow = ~mask & (cnt > 0)
        img[grow] = acc[grow] / cnt[grow][:, None]
        mask |= grow
    return img


def bake_texture(
    c: Creature, tri_uv: np.ndarray, tri_pos: np.ndarray, tri_nrm: np.ndarray
) -> np.ndarray:
    """sRGB fur texture (S, S, 3) in 0..1 for the UV layout of the body mesh."""
    size = c.texture
    pos, nrm, mask = rasterize(tri_uv, tri_pos, tri_nrm, size)
    img = np.zeros((size, size, 3))
    idx = np.flatnonzero(mask.ravel())
    flat_p, flat_n = pos.reshape(-1, 3), nrm.reshape(-1, 3)
    out = img.reshape(-1, 3)
    for start in range(0, len(idx), CHUNK):
        sel = idx[start : start + CHUNK]
        out[sel] = surface_colour(c, flat_p[sel], flat_n[sel])
    return dilate(img, mask)
