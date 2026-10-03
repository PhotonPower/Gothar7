"""Reading the node tree of a foreign .glb (the project owner's models) in model space.

``read_glb`` gives the JSON and the binary chunk; this resolves the node transforms (matrix or
TRS) so the positions of every mesh node are known in the model's frame. Used to fit hand-made
models into the world (measure parts, align them) before the Blender script adapts them.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from typing import Any

import numpy as np

from gothar_worldgen.buildings.gltf import read_glb

_DTYPES = {5126: "<f4", 5125: "<u4", 5123: "<u2", 5121: "u1"}
_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


@dataclass
class NodeMesh:
    path: tuple[str, ...]  # node names from the root down to this node
    material: str
    positions: np.ndarray  # (n, 3) in model space
    triangles: int
    ids: tuple[int, ...] = ()  # node indices along ``path`` (instances share names)


def accessor(doc: dict[str, Any], binary: bytes, index: int) -> np.ndarray:
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    width = _WIDTH[acc["type"]]
    dtype = np.dtype(_DTYPES[acc["componentType"]])
    stride = view.get("byteStride", 0) or dtype.itemsize * width
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    if stride == dtype.itemsize * width:
        a = np.frombuffer(binary, dtype=dtype, count=acc["count"] * width, offset=start)
        return a.reshape(-1, width) if width > 1 else a
    rows = [
        np.frombuffer(binary, dtype=dtype, count=width, offset=start + i * stride)
        for i in range(acc["count"])
    ]
    return np.stack(rows)


def local_matrix(node: dict[str, Any]) -> np.ndarray:
    if "matrix" in node:
        return np.asarray(node["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if "scale" in node:
        m = np.diag([*node["scale"], 1.0]) @ m
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        r = np.eye(4)
        r[:3, :3] = [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
        m = r @ m
    if "translation" in node:
        t = np.eye(4)
        t[:3, 3] = node["translation"]
        m = t @ m
    return m


def node_meshes(data: bytes) -> Iterator[NodeMesh]:
    """Every primitive of every mesh node, positions in model space."""
    doc, binary = read_glb(data)
    nodes = doc["nodes"]
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]

    def walk(
        i: int, parent: np.ndarray, path: tuple[str, ...], ids: tuple[int, ...]
    ) -> Iterator[NodeMesh]:
        node = nodes[i]
        world = parent @ local_matrix(node)
        here = (*path, node.get("name", str(i)))
        here_ids = (*ids, i)
        if "mesh" in node:
            for prim in doc["meshes"][node["mesh"]]["primitives"]:
                pos = accessor(doc, binary, prim["attributes"]["POSITION"]).astype(np.float64)
                pos = (world @ np.c_[pos, np.ones(len(pos))].T).T[:, :3]
                count = (
                    doc["accessors"][prim["indices"]]["count"] if "indices" in prim else len(pos)
                ) // 3
                mat = doc["materials"][prim["material"]]["name"] if "material" in prim else ""
                yield NodeMesh(here, mat, pos, count, here_ids)
        for c in node.get("children", []):
            yield from walk(c, world, here, here_ids)

    for r in roots:
        yield from walk(r, np.eye(4), (), ())


def bounds(meshes: list[NodeMesh]) -> tuple[np.ndarray, np.ndarray]:
    pts = np.vstack([m.positions for m in meshes])
    return pts.min(axis=0), pts.max(axis=0)
