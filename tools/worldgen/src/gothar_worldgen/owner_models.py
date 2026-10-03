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
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Any

import numpy as np
from shapely.geometry import MultiPoint

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
    """Node indices down to the first ancestor under a ``rigid`` prefix (one pavilion)."""
    for depth, part in enumerate(m.path):
        if any(name_matches(part, p) for p in rigid):
            return m.ids[: depth + 1]
    return None


def rigid_lifts(
    meshes: list[NodeMesh], rigid: list[str], shear: tuple[float, float]
) -> dict[tuple[int, ...], float]:
    """How far each rigid group (a pavilion) moves up: the shear at the centre of all its parts,
    so its pieces stay together and level."""
    groups: dict[tuple[int, ...], list[np.ndarray]] = {}
    for m in meshes:
        key = _rigid_key(m, rigid)
        if key is not None:
            groups.setdefault(key, []).append(m.positions)
    lifts = {}
    for key, parts in groups.items():
        c = np.vstack(parts).mean(axis=0)
        lifts[key] = float(shear[0] * c[0] + shear[1] * c[2])
    return lifts


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
    spec: dict[str, Any], meshes: list[NodeMesh], shear: tuple[float, float] = (0.0, 0.0)
) -> list[tuple[str, np.ndarray, list[tuple[int, int, int]]]]:
    """``COL_HULL_<i>`` bodies from ``spec["collision"]``, in model space with the shear applied.

    Each rule: ``nodes`` (name prefixes), ``shape`` (``box`` or ``prism``), ``per``
    (``instance``: one body per node whose name matches, else one for all), optional ``sides``,
    ``belowM`` (reach that far below the lowest point, into a sloping ground).
    """
    rigid = spec.get("rigid", [])
    lift = rigid_lifts(meshes, rigid, shear)
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
) -> dict[str, Any]:
    """Everything the Blender script needs, as plain data (written as JSON next to the .blend)."""
    source = spec_path.parent / spec["source"]
    if not source.is_file():
        raise OwnerModelError(f"source {source} not found")
    meshes = kept_meshes(spec, source)
    cols = collision(spec, meshes, shear)
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
        "rigid": spec.get("rigid", []),
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
    schloss_spec: dict[str, Any], radii: dict[str, float], sink: float = 0.02
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


def fitted_placement(
    spec: dict[str, Any], buildings: list[dict[str, Any]], sink: float = 0.05
) -> Placement:
    """Placement stored in the spec (``placement``: rotDeg, x, z, fitted once to the LoD2
    footprint of ``lod2``); height: the lowest DGM point under that building, sunk a little."""
    pl = spec["placement"]
    b = next((b for b in buildings if b.get("id") == pl["lod2"]), None)
    if b is None:
        raise OwnerModelError(f"LoD2 building {pl['lod2']} not in buildings.json")
    y = float(b.get("groundMinY", b.get("groundY", 0.0))) - sink
    # rotDeg turns the model counter-clockwise in (x, z); a yaw about +Y turns it the other way
    return Placement(
        spec["key"],
        (float(pl["x"]), round(y, 3), float(pl["z"])),
        -math.radians(float(pl["rotDeg"])),
    )
