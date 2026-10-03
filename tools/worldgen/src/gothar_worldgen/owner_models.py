"""The project owner's own models (Claude Design), adapted by script and placed in the world (W6).

Each model has a spec ``data/<site>/<key>.json`` (format ``gothar-owner-model``) next to its
unchanged source ``.glb``. ``prepare_job`` turns spec and placement into a job for the generic
Blender script ``blender/owner_models/build_owner_model.py``: which nodes to remove, how much to
decimate, which materials move to the palette, the vertical shear that lays fences onto a sloping
garden, and the ``COL_HULL_`` bodies, computed here from the source's node groups. ``placements``
says where the models go (``handmade.json`` items: one model may be placed several times).

Kept here, not in Blender, so the tests can check everything except the decimation.
"""

from __future__ import annotations

import fnmatch
import json
import math
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from types import ModuleType
from typing import Any

import numpy as np
from shapely.geometry import MultiPoint, Point, Polygon
from shapely.ops import unary_union

from gothar_worldgen.buildings.gltf_scene import NodeMesh, node_meshes

FORMAT = "gothar-owner-model"
OWNER_DIR = Path(__file__).resolve().parents[2] / "blender" / "owner_models"


class OwnerModelError(Exception):
    """Inconsistent spec or source; the message is meant for the user."""


@dataclass
class Placement:
    key: str  # handmade.json key, e.g. gartenbrunnen_w
    pos: tuple[float, float, float]
    yaw: float  # radians about +Y
    shear: tuple[float, float] = (0.0, 0.0)  # dy/dx, dy/dz in model space (sloping garden)
    lifts: dict[str, float] = field(default_factory=dict)  # node prefix -> dy (one terrace each)

    @property
    def rot(self) -> list[float]:
        return [0.0, round(math.sin(self.yaw / 2), 6) + 0.0, 0.0, round(math.cos(self.yaw / 2), 6)]


def load_spec(path: Path) -> dict[str, Any]:
    spec = json.loads(path.read_text(encoding="utf-8"))
    if spec.get("format") != FORMAT or spec.get("version") != 1:
        raise OwnerModelError(f"{path.name}: not a {FORMAT} v1 file")
    return spec


def name_matches(name: str, pattern: str) -> bool:
    """A node name against a rule: a prefix, or a glob with ``*`` (``*_frame``)."""
    return fnmatch.fnmatchcase(name, pattern) if "*" in pattern else name.startswith(pattern)


def _matches(path: tuple[str, ...], prefixes: list[str]) -> bool:
    return any(name_matches(part, p) for part in path for p in prefixes)


def kept_meshes(spec: dict[str, Any], source: Path) -> list[NodeMesh]:
    """Mesh nodes of the source that stay (not under a ``remove`` prefix)."""
    return [
        m for m in node_meshes(source.read_bytes()) if not _matches(m.path, spec.get("remove", []))
    ]


def sheared(points: np.ndarray, shear: tuple[float, float]) -> np.ndarray:
    out = points.copy()
    out[:, 1] += shear[0] * points[:, 0] + shear[1] * points[:, 2]
    return out


def _rigid_key(m: NodeMesh, rigid: list[str]) -> tuple[int, ...] | None:
    """Node indices down to the outermost ancestor under a ``rigid`` prefix (one pavilion, one
    half of the railing)."""
    for depth, part in enumerate(m.path):
        if any(name_matches(part, p) for p in rigid):
            return m.ids[: depth + 1]
    return None


def rigid_lifts(
    meshes: list[NodeMesh],
    rigid: list[str],
    shear: tuple[float, float],
    lifts: dict[str, float] | None = None,
) -> dict[tuple[int, ...], float]:
    """How far each rigid group (a pavilion) moves up: the shear at the centre of all its parts,
    so its pieces stay together and level."""
    groups: dict[tuple[int, ...], list[np.ndarray]] = {}
    for m in meshes:
        key = _rigid_key(m, rigid)
        if key is not None:
            groups.setdefault(key, []).append(m.positions)
    out = {}
    names = {m.ids[i]: m.path[i] for m in meshes for i in range(len(m.ids))}
    for key, parts in groups.items():
        name = names[key[-1]]
        lift = next((dy for p, dy in (lifts or {}).items() if name_matches(name, p)), None)
        if lift is None:
            c = np.vstack(parts).mean(axis=0)
            lift = float(shear[0] * c[0] + shear[1] * c[2])
        out[key] = lift
    return out


def _hexahedron(pts: np.ndarray) -> tuple[np.ndarray, list[tuple[int, int, int]]]:
    """Outward triangles of a convex body given as 4 bottom + 4 top points (same order)."""
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return pts, _outward(pts, [t for q in quads for t in ((q[0], q[1], q[2]), (q[0], q[2], q[3]))])


def _outward(pts: np.ndarray, tris: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    c = pts.mean(axis=0)
    out = []
    for a, b, d in tris:
        n = np.cross(pts[b] - pts[a], pts[d] - pts[a])
        out.append((a, b, d) if np.dot(n, (pts[a] + pts[b] + pts[d]) / 3 - c) >= 0 else (a, d, b))
    return out


def box_body(points: np.ndarray) -> tuple[np.ndarray, list[tuple[int, int, int]]]:
    """Oriented box: the smallest rotated rectangle around the points in (x, z), their y range."""
    rect = MultiPoint([(float(x), float(z)) for x, _, z in points]).minimum_rotated_rectangle
    if rect.geom_type != "Polygon":  # degenerate (a line): widen a little
        rect = rect.buffer(0.05, cap_style="square")
        rect = rect.minimum_rotated_rectangle
    ring = list(rect.exterior.coords)[:4]
    y0, y1 = float(points[:, 1].min()), float(points[:, 1].max())
    pts = np.asarray([(x, y0, z) for x, z in ring] + [(x, y1, z) for x, z in ring])
    return _hexahedron(pts)


def prism_body(points: np.ndarray, sides: int = 8) -> tuple[np.ndarray, list[tuple[int, int, int]]]:
    """Regular prism around the points: centre of their (x, z) bounds, enclosing radius."""
    lo, hi = points.min(axis=0), points.max(axis=0)
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    r = float(np.hypot(points[:, 0] - cx, points[:, 2] - cz).max()) / math.cos(math.pi / sides)
    ring = [
        (cx + r * math.cos(2 * math.pi * k / sides), cz + r * math.sin(2 * math.pi * k / sides))
        for k in range(sides)
    ]
    pts = np.asarray([(x, lo[1], z) for x, z in ring] + [(x, hi[1], z) for x, z in ring])
    tris = [(0, k, k + 1) for k in range(1, sides - 1)]
    tris += [(sides, sides + k + 1, sides + k) for k in range(1, sides - 1)]
    for k in range(sides):
        j = (k + 1) % sides
        tris += [(k, j, sides + j), (k, sides + j, sides + k)]
    return pts, _outward(pts, tris)


def collision(
    spec: dict[str, Any],
    meshes: list[NodeMesh],
    shear: tuple[float, float] = (0.0, 0.0),
    lifts: dict[str, float] | None = None,
) -> list[tuple[str, np.ndarray, list[tuple[int, int, int]]]]:
    """``COL_HULL_<i>`` bodies from ``spec["collision"]``, in model space with the shear applied.

    Each rule: ``nodes`` (name prefixes), ``shape`` (``box`` or ``prism``), ``per``
    (``instance``: one body per node whose name matches, else one for all), optional ``sides``,
    ``belowM`` (reach that far below the lowest point, into a sloping ground).
    """
    rigid = [*(lifts or {}), *spec.get("rigid", [])]
    lift = rigid_lifts(meshes, rigid, shear, lifts)
    bodies = []
    for rule in spec.get("collision", []):
        prefixes = rule["nodes"]
        hits = [m for m in meshes if _matches(m.path, prefixes)]
        if not hits:
            raise OwnerModelError(f"collision rule {prefixes}: no node matches")
        groups: dict[tuple[int, ...], list[np.ndarray]] = {}
        for m in hits:
            if rule.get("per") == "instance":
                depth = next(
                    i
                    for i, part in enumerate(m.path)
                    if any(name_matches(part, p) for p in prefixes)
                )
                key = m.ids[: depth + 1]
            else:
                key = ()
            pts = m.positions.copy()
            key_r = _rigid_key(m, rigid)
            if key_r is not None:
                pts[:, 1] += lift[key_r]
            else:
                pts = sheared(pts, shear)
            groups.setdefault(key, []).append(pts)
        for parts in groups.values():
            pts = np.vstack(parts)
            if rule.get("belowM"):
                low = pts[pts[:, 1] <= pts[:, 1].min() + 1e-6].copy()
                low[:, 1] -= float(rule["belowM"])
                pts = np.vstack([pts, low])
            if rule.get("shape") == "prism":
                body = prism_body(pts, int(rule.get("sides", 8)))
            else:
                body = box_body(pts)
            bodies.append(body)
    return [(f"COL_HULL_{i}", np.round(p, 4), t) for i, (p, t) in enumerate(bodies)]


def triangles_kept(meshes: list[NodeMesh]) -> int:
    return sum(m.triangles for m in meshes)


def prepare_job(
    spec: dict[str, Any],
    spec_path: Path,
    palette: dict[str, Any],
    out_glb: Path,
    out_blend: Path | None,
    shear: tuple[float, float] = (0.0, 0.0),
    lifts: dict[str, float] | None = None,
    foundation: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Everything the Blender script needs, as plain data (written as JSON next to the .blend)."""
    source = spec_path.parent / spec["source"]
    if not source.is_file():
        raise OwnerModelError(f"source {source} not found")
    meshes = kept_meshes(spec, source)
    cols = collision(spec, meshes, shear, lifts)
    mapping = spec.get("materials", {})
    return {
        "key": spec["key"],
        "source": str(source),
        "remove": spec.get("remove", []),
        "decimate": spec.get("decimate", {}),
        "thinFaces": spec.get("thinFaces", {}),
        "materials": {k: [v, palette[v]] for k, v in mapping.items()},
        "smoothDeg": float(spec.get("smoothDeg", 35.0)),
        "shear": list(shear),
        "rigid": [*(lifts or {}), *spec.get("rigid", [])],
        "lifts": dict(lifts or {}),
        "foundation": foundation,
        "foundationMaterial": ["stone", palette["stone"]],
        "collision": [[n, p.tolist(), [list(t) for t in tris]] for n, p, tris in cols],
        "outGlb": str(out_glb),
        "outBlend": str(out_blend) if out_blend else "",
    }


def run_blender(blender: Path, job: dict[str, Any], job_path: Path, run: Callable[..., Any]) -> str:
    """Writes ``job`` and runs the generic Blender script; returns its summary line."""
    job_path.parent.mkdir(parents=True, exist_ok=True)
    job_path.write_text(json.dumps(job), encoding="utf-8")
    cmd = [
        str(blender),
        "--background",
        "--factory-startup",
        "--python-exit-code",
        "1",
        "--python",
        str(OWNER_DIR / "build_owner_model.py"),
        "--",
        str(job_path),
    ]
    done = run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=900,
        check=False,
    )
    line = next((ln for ln in done.stdout.splitlines() if ln.startswith("OWNER_OK")), None)
    if done.returncode != 0 or line is None:
        raise OwnerModelError("Blender build failed:\n" + (done.stdout + done.stderr)[-2000:])
    return line


def _ground_frame(
    geo: ModuleType, spec: dict[str, Any]
) -> tuple[Any, dict[str, Any], np.ndarray, np.ndarray]:
    lay = geo.garden_layout(spec)
    if lay is None:
        raise OwnerModelError("schloss.json has no garden")
    w = lay["wing"]
    o = np.asarray(w.p(0, 0, 0))[[0, 2]]
    s_hat = np.asarray(w.p(1, 0, 0))[[0, 2]] - o
    t_hat = np.asarray(w.p(0, 1, 0))[[0, 2]] - o
    return lay, lay["spec"], s_hat / np.linalg.norm(s_hat), t_hat / np.linalg.norm(t_hat)


def garden_placements(
    schloss_spec: dict[str, Any],
    radii: dict[str, float],
    sink: float = 0.02,
    terraces: list[dict[str, Any]] | None = None,
) -> list[Placement]:
    """The garden models along the parterre axes (``schloss.json`` garden, wing frame (s, t)):
    the railing around both halves, sheared onto the sloping garden plane; the obelisk fountain
    in the middle field; a garden fountain in the plaza of each half. Fountains stand on the
    lowest garden ground under them (``radii``: their footprint radius)."""
    from gothar_worldgen.handmade import schloss_geometry

    geo = schloss_geometry()
    lay, g, s_hat, t_hat = _ground_frame(geo, schloss_spec)
    w = lay["wing"]
    (s0, s1), (s2, s3) = ((float(a), float(b)) for a, b in g["parts"])
    t0, t1 = (float(v) for v in g["t"])
    yaw = math.atan2(-s_hat[1], s_hat[0])  # model +x along the parterre (wing s axis)
    z_w = np.array([math.sin(yaw), math.cos(yaw)])
    if terraces:  # level terraces (castle garden plan): every model stands on its terrace
        level = {tr["key"]: float(tr["y"]) for tr in terraces}

        def on(key: str, s: float, y: float, lifts: dict[str, float] | None = None) -> Placement:
            x, _, z = w.p(s, (t0 + t1) / 2, 0)
            return Placement(
                key, (round(x, 3), round(y, 3), round(z, 3)), yaw, (0.0, 0.0), dict(lifts or {})
            )

        mid = (s1 + s2) / 2
        return [
            on(
                "garten_gelaender",
                mid,
                level["mitte"],
                {
                    "parterre_west": round(level["west"] - level["mitte"], 3),
                    "parterre_east": round(level["ost"] - level["mitte"], 3),
                },
            ),
            on("obeliskbrunnen", mid, level["mitte"] - sink),
            on("gartenbrunnen_w", (s0 + s1) / 2, level["west"] - sink),
            on("gartenbrunnen_o", (s2 + s3) / 2, level["ost"] - sink),
        ]
    mid_s, mid_t = (s1 + s2) / 2, (t0 + t1) / 2

    def ground(s: float, t: float) -> float:
        return float(w.ground(g, s, t))  # garden frame (turned by rotDeg)

    # the ground is a plane: its slope along the model's x (frame s) and z (+-frame t) axes
    sign_z = 1.0 if float(z_w @ t_hat) >= 0 else -1.0
    shear = (
        round(ground(mid_s + 1.0, mid_t) - ground(mid_s, mid_t), 6),
        round(sign_z * (ground(mid_s, mid_t + 1.0) - ground(mid_s, mid_t)), 6),
    )

    def at(key: str, s: float, t: float, radius: float | None) -> Placement:
        x, _, z = w.p(s, t, 0)
        if radius is None:
            y = ground(s, t)
        else:  # lowest garden ground under the footprint
            y = (
                min(
                    ground(s + radius * math.cos(a) * ds, t + radius * math.sin(a) * dt)
                    for a in np.linspace(0, 2 * math.pi, 16, endpoint=False)
                    for ds, dt in ((1.0, 1.0),)
                )
                - sink
            )
        return Placement(
            key,
            (round(x, 3), round(y, 3), round(z, 3)),
            yaw,
            shear if radius is None else (0.0, 0.0),
        )

    return [
        at("garten_gelaender", mid_s, mid_t, None),
        at("obeliskbrunnen", mid_s, mid_t, radii["obeliskbrunnen"]),
        at("gartenbrunnen_w", (s0 + s1) / 2, mid_t, radii["brunnen_garten"]),
        at("gartenbrunnen_o", (s2 + s3) / 2, mid_t, radii["brunnen_garten"]),
    ]


def handmade_item(p: Placement, mesh: str, replaces: list[str] | None = None) -> dict[str, Any]:
    return {
        "key": p.key,
        "mesh": mesh,
        "pos": list(p.pos),
        "rot": p.rot,
        "replaces": list(replaces or []),
        "footprints": [],
    }


def footprint(meshes: list[NodeMesh], nodes: list[str], below: float = 1.5) -> Polygon:
    """Model-space (x, z) outline of the parts under ``nodes`` near the ground (below ``below``)."""
    hulls = []
    for m in meshes:
        if _matches(m.path, nodes):
            low = m.positions[m.positions[:, 1] < below]
            if len(low) >= 3:
                h = MultiPoint([(float(x), float(z)) for x, _, z in low]).convex_hull
                if h.geom_type == "Polygon" and h.area > 0.5:
                    hulls.append(h)
    if not hulls:
        raise OwnerModelError(f"footprint {nodes}: no part near the ground")
    u = unary_union(hulls).buffer(0.05).buffer(-0.05)
    poly = u if u.geom_type == "Polygon" else max(u.geoms, key=lambda g: g.area)
    return poly.simplify(0.2)


def world_polygon(poly: Polygon, place: Placement) -> Polygon:
    c, s = math.cos(place.yaw), math.sin(place.yaw)
    return Polygon(
        [
            (place.pos[0] + x * c + z * s, place.pos[2] - x * s + z * c)
            for x, z in poly.exterior.coords
        ]
    )


def ground_range(
    poly: Polygon, height: Callable[[float, float], float], step: float = 1.0
) -> tuple[float, float]:
    """(lowest, highest) ground under a world polygon: its outline and a grid inside."""
    pts = [
        poly.exterior.interpolate(d).coords[0]
        for d in np.arange(0.0, poly.exterior.length, step / 2)
    ]
    x0, z0, x1, z1 = poly.bounds
    pts += [
        (x, z)
        for x in np.arange(x0, x1, step)
        for z in np.arange(z0, z1, step)
        if poly.contains(Point(x, z))
    ]
    hs = [height(float(x), float(z)) for x, z in pts]
    return min(hs), max(hs)


def fitted_placement(
    spec: dict[str, Any], height: Callable[[float, float], float], meshes: list[NodeMesh]
) -> tuple[Placement, dict[str, Any] | None]:
    """Placement stored in the spec (``placement``: rotDeg, x, z, fitted once to the LoD2
    footprint) on the ground rule of the hand-made models: the floor on the highest ground under
    the footprint; a stone foundation (``foundation``) reaches below the lowest."""
    pl = spec["placement"]
    yaw = -math.radians(float(pl["rotDeg"]))  # rotDeg turns counter-clockwise in (x, z)
    probe = Placement(spec["key"], (float(pl["x"]), 0.0, float(pl["z"])), yaw)
    found = spec.get("foundation")
    poly = footprint(meshes, found["nodes"]) if found else footprint(meshes, [""])
    low, high = ground_range(world_polygon(poly, probe), height)
    place = Placement(spec["key"], (float(pl["x"]), round(high, 3), float(pl["z"])), yaw)
    if not found:
        return place, None
    return place, {
        "polygon": [[round(x, 3), round(z, 3)] for x, z in poly.exterior.coords][:-1],
        "depth": round(high - low + float(found.get("belowM", 0.3)), 3),
    }
