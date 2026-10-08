"""Own monsters from a body description (F5): spec, zones and the procedural fur texture.

A creature is described in ``data/monsters/<art>.creature.toml``: the bones of its rig (contract
§7.1), primitive shapes that are merged into one body surface (``blender/build_creature.py``) and
colour zones. Everything here is pure Python/numpy so it runs both inside Blender and in tests.

Coordinates are Blender's, like the rig TOMLs: x = left, -y = forward, z = up, metres. The tool
moves everything so that ``root`` (origin) lies on the ground below the pelvis head.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field, replace
from pathlib import Path

import numpy as np

SHAPE_KINDS = (
    "ellipsoid", "box", "capsule", "chain", "ridge", "cone", "cut", "cut_box",
    "eye", "tooth", "tongue",
    "plate", "plate_shell", "tuft_shell",
)  # fmt: skip
CUTS = ("cut", "cut_box")  # removed from the body after the first remesh
PIECES = ("eye", "tooth", "tongue")  # separate geometry with their own material (not in the union)
PLATES = ("plate",)  # separate rigid geometry with the body material (armour plates)
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
    """A primitive. ellipsoid/box: center, size (half axes), rotate (Euler XYZ degrees);
    capsule/cone: a, b, r, r2 (radius at b; cone tip r2 = 0); cut: an ellipsoid removed from the
    body after the first remesh (eye sockets, ear cups), cut_box the same as a box (mouth slit).
    Pieces (eye: ellipsoid, tooth: cone) are separate geometry with the material eyes/teeth,
    rigid on `bone`. Plates (box) are separate rigid geometry with the body material and texture,
    on `bone` or the nearest of `candidates`."""

    kind: str
    zone: str
    center: tuple[float, float, float] = (0.0, 0.0, 0.0)
    size: tuple[float, float, float] = (1.0, 1.0, 1.0)
    rotate: tuple[float, float, float] = (0.0, 0.0, 0.0)
    a: tuple[float, float, float] = (0.0, 0.0, 0.0)
    b: tuple[float, float, float] = (0.0, 0.0, 0.0)
    r: float = 0.0
    r2: float = 0.0
    bone: str = ""
    candidates: tuple[str, ...] = ()


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
    relief: float = 1.0  # strength of the strands/creases in the normal map
    strata: float = 0.0  # sandstone: layered bands and chisel marks (plates)


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
    normal_size: int = 512
    plate_bevel: float = 0.012
    orientation: dict = field(default_factory=dict)  # [rig.orientation] overrides
    normal_strength: float = 1.0
    eye_colour: str = "#140d09"
    teeth_colour: str = "#d8cdb0"
    tongue_colour: str = "#7a4440"
    eye_glow: float = 0.0  # emissive strength of the eyes (0..1; glowing in the dark)
    eye_glow_colour: str = "#b0d890"
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


def matrix_euler(m: np.ndarray) -> tuple[float, float, float]:
    """Inverse of euler_matrix: Blender Euler XYZ degrees of a rotation matrix."""
    ry = np.arcsin(np.clip(-m[2, 0], -1.0, 1.0))
    rx = np.arctan2(m[2, 1], m[2, 2])
    rz = np.arctan2(m[1, 0], m[0, 0])
    return tuple(float(np.degrees(a)) for a in (rx, ry, rz))  # type: ignore[return-value]


def _plate_shell(
    it: dict, zone: str, bone: str, candidates: tuple[str, ...], at: str, piece: str = "plate"
) -> list[Shape]:
    """Armour plates in rows on an ellipsoid: `rows` (y offsets from the centre, m), `angles`
    (degrees around the long axis, 0 = on top, positive towards +x), `plate` (half extents:
    across, along, thickness), `sink` (m the plate centre lies below the surface), `jitter`,
    `tilt` (degrees the rear edge is raised: roof tiles, each row overlapping the next).
    With piece "ellipsoid" (tuft_shell, half extents `tuft`) the same frames give shaggy tufts
    of hair flowing backwards; they join the body union and scatter more."""
    centre, size = np.array(_v3(it["center"], at)), np.array(_v3(it["size"], at))
    half = np.array(_v3(it["plate" if piece == "plate" else "tuft"], at))
    spread = 6.0 if piece == "plate" else 25.0  # random twist (degrees at jitter 0.15)
    wobble = 0.2 if piece == "plate" else 0.6  # random offset (share of the half extents)
    sink = float(it.get("sink", half[2] * 0.4))
    jitter = float(it.get("jitter", 0.0))
    tilt = np.radians(float(it.get("tilt", 0.0)))
    rng = np.random.default_rng(int(it.get("seed", 0)))
    out = []
    for y in it["rows"]:
        wx = 1.0 - (float(y) / size[1]) ** 2
        if wx <= 0.09:
            continue
        wx = float(np.sqrt(wx))
        for ang in it["angles"]:
            a = np.radians(float(ang))
            local = np.array([size[0] * wx * np.sin(a), float(y), size[2] * wx * np.cos(a)])
            normal = local / size**2
            normal /= np.linalg.norm(normal)
            along = np.array([0.0, 1.0, 0.0]) - normal[1] * normal
            along /= np.linalg.norm(along)
            across = np.cross(along, normal)
            twist = np.radians(rng.uniform(-spread, spread) * jitter / 0.15) if jitter else 0.0
            c, s = np.cos(twist), np.sin(twist)
            across, along = c * across + s * along, -s * across + c * along
            lift = tilt + (np.radians(rng.uniform(-10, 10)) if piece != "plate" and jitter else 0.0)
            c, s = np.cos(lift), np.sin(lift)  # rear edge (+along) up, under the row ahead
            along, normal = c * along + s * normal, -s * along + c * normal
            m = np.stack([across, along, normal], axis=1)  # columns: local x, y, z
            scale = 1.0 + rng.uniform(-jitter, jitter, 3) if jitter else np.ones(3)
            pos = centre + local - normal * sink + rng.uniform(-1, 1, 3) * jitter * half * wobble
            out.append(
                Shape(
                    piece,
                    zone,
                    center=tuple(float(v) for v in pos),  # type: ignore[arg-type]
                    size=tuple(float(v) for v in half * scale),  # type: ignore[arg-type]
                    rotate=matrix_euler(m),
                    bone=bone,
                    candidates=candidates,
                )
            )
    return out


def _parse_shapes(items: list[dict], zones: dict[str, Zone]) -> list[Shape]:
    shapes: list[Shape] = []
    for i, it in enumerate(items):
        kind = it.get("kind", "")
        if kind not in SHAPE_KINDS:
            raise CreatureError(f"shape {i}: kind {kind!r} not in {SHAPE_KINDS}")
        zone = it.get("zone", "")
        bone = it.get("bone", "")
        candidates = tuple(str(b) for b in it.get("bones", []))
        if kind in PIECES and not bone:
            raise CreatureError(f"shape {i}: {kind} needs a bone")
        if kind in ("plate", "plate_shell") and not (bone or candidates):
            raise CreatureError(f"shape {i}: {kind} needs a bone or bones")
        if kind not in PIECES and zone not in zones:
            raise CreatureError(f"shape {i}: unknown zone {zone!r}")
        new: list[Shape] = []
        at = f"shape {i}"
        rot = _v3(it.get("rotate", [0, 0, 0]), at)
        if kind in ("ellipsoid", "box", "cut", "cut_box", "eye", "tongue", "plate"):
            centre, size = _v3(it["center"], at), _v3(it["size"], at)
            new.append(
                Shape(
                    kind,
                    zone,
                    center=centre,
                    size=size,
                    rotate=rot,
                    bone=bone,
                    candidates=candidates,
                )
            )
        elif kind == "plate_shell":
            new += _plate_shell(it, zone, bone, candidates, at)
        elif kind == "tuft_shell":
            new += _plate_shell(it, zone, "", (), at, piece="ellipsoid")
        elif kind in ("capsule", "cone", "tooth"):
            r = float(it["r"])
            r2 = 0.0 if kind in ("cone", "tooth") else float(it.get("r2", r))
            name = "tooth" if kind == "tooth" else "capsule"
            a, b = _v3(it["a"], at), _v3(it["b"], at)
            new.append(Shape(name, zone, a=a, b=b, r=r, r2=r2, bone=bone))
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
                rot_m = (s.rotate[0], -s.rotate[1], -s.rotate[2])
                bone = _mirror_name(s.bone) if s.bone.endswith("_l") else s.bone
                mirrored = replace(s, center=_mx(s.center), rotate=rot_m, a=_mx(s.a), b=_mx(s.b))
                shapes.append(replace(mirrored, bone=bone))
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
    shapes = [replace(s, center=sh(s.center), a=sh(s.a), b=sh(s.b)) for s in shapes]
    sockets = data.get("sockets", {})
    names = {b.name for b in bones}
    for k, s in enumerate(shapes):
        unknown = [b for b in (s.bone, *s.candidates) if b and b not in names]
        if unknown:
            raise CreatureError(f"{s.kind}: unknown bone {unknown[0]}")
        if s.kind == "plate" and not s.bone:  # the nearest candidate carries the plate
            centre = np.array(s.center)[None]
            near = min(s.candidates, key=lambda b: _bone_distance(bones, b, centre))
            shapes[k] = replace(s, bone=near, candidates=())
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
        normal_size=int(data.get("normal_size", 512)),
        plate_bevel=float(data.get("plate_bevel", 0.012)),
        orientation=dict(data.get("orientation", {})),
        normal_strength=float(data.get("normal_strength", 1.0)),
        eye_colour=str(data.get("eye_colour", "#140d09")),
        teeth_colour=str(data.get("teeth_colour", "#d8cdb0")),
        tongue_colour=str(data.get("tongue_colour", "#7a4440")),
        eye_glow=float(data.get("eye_glow", 0.0)),
        eye_glow_colour=str(data.get("eye_glow_colour", "#b0d890")),
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
    if s.kind in ("box", "cut_box", "plate"):
        q = np.abs((p - np.array(s.center)) @ euler_matrix(s.rotate)) - np.array(s.size)
        outside = np.linalg.norm(np.maximum(q, 0.0), axis=1)
        return outside + np.minimum(q.max(axis=1), 0.0)
    if s.kind in ("ellipsoid", "cut", "eye", "tongue"):
        q = (p - np.array(s.center)) @ euler_matrix(s.rotate)  # into the local frame
        size = np.array(s.size)
        k = np.linalg.norm(q / size, axis=1)
        return (k - 1.0) * size.min()
    a, b = np.array(s.a), np.array(s.b)
    ab = b - a
    t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-12), 0.0, 1.0)
    closest = a + t[:, None] * ab
    return np.linalg.norm(p - closest, axis=1) - (s.r + (s.r2 - s.r) * t)


def _bone_distance(bones: list[Bone], name: str, p: np.ndarray) -> float:
    b = next(x for x in bones if x.name == name)
    h, v = np.array(b.head), np.array(b.tail) - np.array(b.head)
    t = np.clip(((p - h) @ v) / max(v @ v, 1e-12), 0.0, 1.0)
    return float(np.linalg.norm(p - (h + t[:, None] * v), axis=1)[0])


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
    shapes = [s for s in c.shapes if s.kind not in PIECES]
    dists = np.stack([np.abs(shape_distance(s, p)) for s in shapes], axis=1)
    near = dists.min(axis=1, keepdims=True)
    contrib = np.exp(-(dists - near) / ZONE_SOFTNESS)
    for k, s in enumerate(shapes):
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
    return surface(c, p, n)[0]


def surface(c: Creature, p: np.ndarray, n: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """sRGB colour (N, 3) and relief height (N,) in about -1..1 of surface points."""
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
    # sandstone: warped horizontal layers and fine chisel marks
    layers = 0.5 + 0.5 * np.cos(
        2 * np.pi * (p[:, 2] / 0.045 + 1.2 * fractal3(p * 7.0, seed + 7, 2))
    )
    chisel = fractal3(p * np.array([140.0, 140.0, 35.0]), seed + 8, 2)
    grain = fractal3(p * 350.0, seed + 9, 1)
    out = np.zeros((len(p), 3))
    height = np.zeros(len(p))
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
        if z.strata:
            col *= (1 - z.strata * (0.1 * layers + 0.1 * (1 - chisel) + 0.12 * (grain - 0.5)))[
                :, None
            ]
        out += w[:, k : k + 1] * col
        relief = z.strands * (strand - 0.5) * 2.0 - z.wrinkles * crease
        relief = relief + z.strata * ((chisel - 0.5) * 1.2 + (grain - 0.5) * 0.8 - 0.3 * layers)
        height += w[:, k] * z.relief * relief
    return np.clip(out, 0, 1), height


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
) -> tuple[np.ndarray, np.ndarray]:
    """sRGB fur texture (S, S, 3) in 0..1 and relief height (S, S) for the body's UV layout."""
    size = c.texture
    pos, nrm, mask = rasterize(tri_uv, tri_pos, tri_nrm, size)
    img = np.zeros((size, size, 3))
    height = np.zeros((size, size))
    idx = np.flatnonzero(mask.ravel())
    flat_p, flat_n = pos.reshape(-1, 3), nrm.reshape(-1, 3)
    out, out_h = img.reshape(-1, 3), height.reshape(-1)
    for start in range(0, len(idx), CHUNK):
        sel = idx[start : start + CHUNK]
        out[sel], out_h[sel] = surface(c, flat_p[sel], flat_n[sel])
    return dilate(img, mask), dilate(height[..., None], mask)[..., 0]


def height_normal(height: np.ndarray, strength: float) -> np.ndarray:
    """Tangent-space normals (S, S, 3), unit vectors, from a height field in image space.

    glTF tangent space follows the UVs (tangent +u, bitangent +v); image row 0 is v = 1, so the
    v derivative is the negative row derivative. `strength` = slope per height unit and texel.
    """
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dv = -(np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    n = np.stack([-strength * du, -strength * dv, np.ones_like(height)], axis=-1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def blend_normals(base: np.ndarray, detail: np.ndarray) -> np.ndarray:
    """Whiteout blend of two tangent-space normal fields (unit vectors)."""
    n = np.concatenate(
        [base[..., :2] + detail[..., :2], (base[..., 2] * detail[..., 2])[..., None]], -1
    )
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def downsample(img: np.ndarray, size: int) -> np.ndarray:
    """Box-filters an (S, S, C) image to (size, size, C); S must be a multiple of size."""
    k = img.shape[0] // size
    return img.reshape(size, k, size, k, -1).mean(axis=(1, 3))
