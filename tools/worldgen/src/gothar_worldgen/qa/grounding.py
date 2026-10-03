"""Do the hand-made models stand on the ground? (W6, decision of the project owner 2026-10-03)

The owner saw pavilions, fountains and parts of the church sunk into the terrain. This check reads
the world as the engine does: every ``HANDMADE_*`` mesh vob, its render mesh (not the ``COL_``
bodies) placed by the vob transform, and the exported heightmap. A vertex is buried if the ground
at its (x, z) lies above it. Vertices within ``FOOT_M`` above the model's base (its lowest point:
foundation, the bottom of a sunk step) may be in the ground; everything higher must not be, apart
from a small tolerance for the bilinear terrain between the samples.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.buildings.gltf_scene import accessor, local_matrix
from gothar_worldgen.export.terrain import Grid

FOOT_M = 0.35  # foundation band: a model may reach this far above its lowest point into the ground
TOLERANCE_M = 0.1  # the bilinear terrain between the 1 m samples
# Materials that are foundation by design and may reach into the ground: the castle's stone socle
# and terrace walls (built into its slope, like the hillside houses), its hedges (sunk 0.3 m).
FOUNDATION = {"HANDMADE_SCHLOSS": {"stone", "hedge"}}


@dataclass
class GroundReport:
    name: str
    vertices: int
    buried: int  # visible vertices under the ground
    deepest: float  # how far the worst one lies under it (m)
    worst: tuple[float, float]  # (x, z) of the worst vertex

    @property
    def ok(self) -> bool:
        return self.buried == 0


def render_vertices(data: bytes, skip: frozenset[str] = frozenset()) -> np.ndarray:
    """All vertices of the non-``COL_`` mesh nodes, in model space (without ``skip`` materials)."""
    doc, binary = read_glb(data)
    nodes = doc["nodes"]
    out = []

    def walk(i: int, parent: np.ndarray) -> None:
        node = nodes[i]
        world = parent @ local_matrix(node)
        if "mesh" in node and not node.get("name", "").startswith("COL_"):
            for prim in doc["meshes"][node["mesh"]]["primitives"]:
                if "material" in prim and doc["materials"][prim["material"]].get("name") in skip:
                    continue
                pos = accessor(doc, binary, prim["attributes"]["POSITION"]).astype(np.float64)
                out.append((world @ np.c_[pos, np.ones(len(pos))].T).T[:, :3])
        for c in node.get("children", []):
            walk(c, world)

    for r in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        walk(r, np.eye(4))
    return np.vstack(out) if out else np.zeros((0, 3))


def placed(points: np.ndarray, pos: list[float], rot: list[float]) -> np.ndarray:
    """Model points by a vob transform turned only about Y (as the assembler writes them)."""
    yaw = 2.0 * math.atan2(rot[1], rot[3])
    c, s = math.cos(yaw), math.sin(yaw)
    out = np.empty_like(points)
    out[:, 0] = pos[0] + points[:, 0] * c + points[:, 2] * s
    out[:, 1] = pos[1] + points[:, 1]
    out[:, 2] = pos[2] - points[:, 0] * s + points[:, 2] * c
    return out


def check_vob(name: str, points: np.ndarray, grid: Grid, base: float | None = None) -> GroundReport:
    """``base``: the model's foot (default: its lowest vertex)."""
    if len(points) == 0:
        return GroundReport(name, 0, 0, 0.0, (0.0, 0.0))
    base = float(points[:, 1].min()) if base is None else base
    visible = points[points[:, 1] > base + FOOT_M]
    ground = np.array([grid.height_at(float(x), float(z)) for x, _, z in visible])
    under = ground - visible[:, 1] - TOLERANCE_M
    bad = under > 0
    if not bad.any():
        return GroundReport(name, len(points), 0, 0.0, (0.0, 0.0))
    k = int(np.argmax(under))
    return GroundReport(
        name,
        len(points),
        int(bad.sum()),
        round(float(under[k] + TOLERANCE_M), 3),
        (round(float(visible[k, 0]), 2), round(float(visible[k, 2]), 2)),
    )


def check_world(
    world: dict[str, Any], assets: Path, grid: Grid, prefix: str = "HANDMADE_"
) -> list[GroundReport]:
    reports = []
    for v in world["vobs"]:
        if v.get("type") != "mesh" or not str(v.get("name", "")).startswith(prefix):
            continue
        data = (assets / v["mesh"]).read_bytes()
        skip = frozenset(FOUNDATION.get(v["name"], ()))
        foot = float(render_vertices(data)[:, 1].min()) + float(v["pos"][1])
        pts = placed(render_vertices(data, skip), v["pos"], v["rot"])
        reports.append(check_vob(v["name"], pts, grid, foot))
    return reports


def report_json(reports: list[GroundReport]) -> str:
    return json.dumps([r.__dict__ | {"ok": r.ok} for r in reports], ensure_ascii=False, indent=1)
