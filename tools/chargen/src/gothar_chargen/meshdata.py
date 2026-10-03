"""Mesh data of a glTF mesh node: triangles, open borders, skin weights (numpy, no Blender)."""

from __future__ import annotations

import re
from collections.abc import Callable
from dataclasses import dataclass

import numpy as np

from gothar_chargen.gltf import Gltf

_TRIANGLES = 4
LOD_RE = re.compile(r"^(?P<base>.+)_lod(?P<level>\d)$")
WELD = 1e-5  # metres: vertices closer than this count as one (glTF splits them at UV/normal seams)


@dataclass
class MeshData:
    """All triangle primitives of one mesh node, concatenated (positions in bind space)."""

    positions: np.ndarray  # (n, 3)
    triangles: np.ndarray  # (t, 3) indices into positions
    joints: np.ndarray | None  # (n, 4) node indices of the skin joints, or None
    weights: np.ndarray | None  # (n, 4)

    @property
    def triangle_count(self) -> int:
        return len(self.triangles)

    def welded(self) -> tuple[np.ndarray, np.ndarray]:
        """(weld id per vertex, representative vertex index per weld id)."""
        keys = np.round(self.positions / WELD).astype(np.int64)
        _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
        return inverse.reshape(-1), first

    def border_vertices(self) -> np.ndarray:
        """Indices (one per welded position) of vertices on open borders of the surface."""
        if len(self.triangles) == 0:
            return np.zeros(0, dtype=np.int64)
        weld, first = self.welded()
        tris = weld[self.triangles]
        edges = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
        edges = np.sort(edges, axis=1)
        unique, counts = np.unique(edges, axis=0, return_counts=True)
        border_ids = np.unique(unique[counts == 1])
        return first[border_ids]

    def weights_of(self, vertex: int) -> dict[int, float]:
        """Joint node index -> weight for one vertex (empty without skin)."""
        if self.joints is None or self.weights is None:
            return {}
        out: dict[int, float] = {}
        for j, w in zip(self.joints[vertex], self.weights[vertex], strict=True):
            if w > 1e-6:
                out[int(j)] = out.get(int(j), 0.0) + float(w)
        return out


def mesh_data(
    gltf: Gltf, node_index: int, skip_material: Callable[[str], bool] | None = None
) -> MeshData:
    """`skip_material(name)` leaves out primitives by material (e.g. teeth for seam checks)."""
    node = gltf.list("nodes")[node_index]
    materials = gltf.list("materials")
    mesh = gltf.list("meshes")[node["mesh"]]
    skin_joints = None
    if "skin" in node:
        skin_joints = np.array(gltf.list("skins")[node["skin"]].get("joints", []), dtype=np.int64)
    positions, triangles, joints, weights = [], [], [], []
    offset = 0
    for prim in mesh.get("primitives", []):
        if prim.get("mode", _TRIANGLES) != _TRIANGLES:
            continue
        material = materials[prim["material"]].get("name", "") if "material" in prim else ""
        if skip_material is not None and material and skip_material(str(material)):
            continue
        attrs = prim.get("attributes", {})
        pos = gltf.accessor(attrs["POSITION"]).astype(np.float64)
        if "indices" in prim:
            idx = gltf.accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
        else:
            idx = np.arange(len(pos), dtype=np.int64).reshape(-1, 3)
        positions.append(pos)
        triangles.append(idx + offset)
        offset += len(pos)
        if skin_joints is not None and "JOINTS_0" in attrs and "WEIGHTS_0" in attrs:
            local = gltf.accessor(attrs["JOINTS_0"]).astype(np.int64)
            joints.append(skin_joints[np.clip(local, 0, len(skin_joints) - 1)])
            weights.append(gltf.accessor(attrs["WEIGHTS_0"]).astype(np.float64))
    if not positions:
        empty = np.zeros((0, 3))
        return MeshData(empty, np.zeros((0, 3), dtype=np.int64), None, None)
    has_skin = len(joints) == len(positions)
    return MeshData(
        np.concatenate(positions),
        np.concatenate(triangles),
        np.concatenate(joints) if has_skin else None,
        np.concatenate(weights) if has_skin else None,
    )


def split_lod(name: str) -> tuple[str, int | None]:
    """'head_lod1' -> ('head', 1); 'head' -> ('head', None)."""
    m = LOD_RE.match(name)
    return (m["base"], int(m["level"])) if m else (name, None)
