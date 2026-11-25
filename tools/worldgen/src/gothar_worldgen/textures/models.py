"""Textures on hand-made models (W7: the castle), like on the generated houses.

The castle comes out of Blender (``blender/schloss/build_schloss.py``) with palette colours only.
``texture_model`` reads such a .glb, gives every primitive texture coordinates and the images of
its kind (``textures.apply``) and writes it again with the worldgen writer (``buildings.gltf``):
one render node, the ``COL_*`` nodes beside it as they were.

Texture coordinates are planar per vertex from its normal (Blender writes flat faces, so a vertex
belongs to faces of one plane), in metres like the houses' builder writes them:

- walls (steeper than ``FLAT_NY``): u along the wall (horizontal, in the face), v up;
- flat faces (floors, ceilings, the tops of cornices): u along ``axis`` (the wing), v across;
  wood (``timber``) the other way round: its grain runs with the beams across the wing;
- roofs: recomputed in ``textured`` (u along the eave, v up the slope).

Which kind a material gets and how much darker it is (the cellar) is data (``schloss.json``
``textures``); unknown materials stay untextured.
"""

from __future__ import annotations

import hashlib
from collections.abc import Mapping
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from gothar_worldgen.buildings.gltf import (
    CollisionPart,
    MeshData,
    Primitive,
    glb_bytes_multi,
    read_glb,
)
from gothar_worldgen.textures.apply import textured

FLAT_NY = 0.7  # |normal.y| above this: a flat face (floor, ceiling, top), else a wall
_TYPES = {5126: "<f4", 5123: "<u2", 5125: "<u4", 5121: "u1"}
_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


@dataclass
class ModelStyle:
    """How one model is textured: palette material -> texture kind (``None``: untextured),
    a factor on the colour (``dark`` < 1: the cellar) and a suffix for the material names
    (so darker materials do not share a name with the bright ones)."""

    kinds: Mapping[str, str | None]
    dark: float = 1.0
    suffix: str = ""
    axis: tuple[float, float] = (1.0, 0.0)  # u of flat faces (x, z)
    key: str = ""  # picks the fixed offset of the repeating textures (like a house's id)


def _accessor(doc: dict, binary: bytes, index: int) -> npt.NDArray:
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    if view.get("byteStride"):
        raise ValueError("interleaved accessors are not supported")
    width = _WIDTH[acc["type"]]
    dtype = np.dtype(_TYPES[acc["componentType"]])
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    arr = np.frombuffer(binary, dtype=dtype, count=acc["count"] * width, offset=start)
    return arr.reshape(-1, width) if width > 1 else arr.copy()


def planar_uvs(
    positions: npt.NDArray[np.float32],
    normals: npt.NDArray[np.float32],
    axis: tuple[float, float] = (1.0, 0.0),
) -> npt.NDArray[np.float32]:
    """Texture coordinates in metres (u along, v up the wall / across a flat face)."""
    p = positions.astype(np.float64)
    n = normals.astype(np.float64)
    ax, az = axis
    flat = np.abs(n[:, 1]) > FLAT_NY
    # walls: the horizontal direction in the face, turned so that u grows to the right
    tx, tz = -n[:, 2], n[:, 0]
    length = np.hypot(tx, tz)
    tx = np.where(length > 1e-9, tx / np.maximum(length, 1e-9), ax)
    tz = np.where(length > 1e-9, tz / np.maximum(length, 1e-9), az)
    wall = np.stack([p[:, 0] * tx + p[:, 2] * tz, p[:, 1]], axis=1)
    floor = np.stack([p[:, 0] * ax + p[:, 2] * az, p[:, 0] * -az + p[:, 2] * ax], axis=1)
    return np.where(flat[:, None], floor, wall).astype(np.float32)


def _offset(key: str) -> tuple[float, float]:
    h = hashlib.sha256(key.encode()).digest() if key else bytes(2)
    return (h[0] / 256.0, h[1] / 256.0)


def texture_model(data: bytes, style: ModelStyle, uri_root: str) -> bytes:
    """The .glb ``data`` with texture coordinates and images per ``style``."""
    doc, binary = read_glb(data)
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]
    render: list[Primitive] = []
    name = ""
    collision: list[CollisionPart] = []
    offset = _offset(style.key)
    for ni in roots:
        node = doc["nodes"][ni]
        if any(k in node for k in ("translation", "rotation", "scale", "matrix", "children")):
            raise ValueError(f"node {node.get('name')}: transforms are not supported")
        mesh = doc["meshes"][node["mesh"]]
        if node["name"].startswith("COL_"):
            for prim in mesh["primitives"]:
                pos = _accessor(doc, binary, prim["attributes"]["POSITION"])
                idx = _accessor(doc, binary, prim["indices"]).astype(np.uint32)
                collision.append(CollisionPart(node["name"], pos.astype(np.float32), idx))
            continue
        if name:
            raise ValueError("one render node expected")
        name = node["name"]
        for prim in mesh["primitives"]:
            mat = doc["materials"][prim["material"]]
            mname = mat["name"]
            color = [round(float(c), 6) for c in mat["pbrMetallicRoughness"]["baseColorFactor"]]
            pos = _accessor(doc, binary, prim["attributes"]["POSITION"]).astype(np.float32)
            nrm = _accessor(doc, binary, prim["attributes"]["NORMAL"]).astype(np.float32)
            idx = _accessor(doc, binary, prim["indices"]).astype(np.uint32)
            f = style.dark
            rgba = (color[0] * f, color[1] * f, color[2] * f, color[3])
            kind = style.kinds.get(mname)
            ax, az = style.axis
            axis = (-az, ax) if kind == "timber" else (ax, az)  # wood: grain across the wing
            p = Primitive(mname + style.suffix, rgba,
                          MeshData(pos, nrm, planar_uvs(pos, nrm, axis), idx))  # fmt: skip
            render.append(textured(p, uri_root, offset, kind) if kind else p)
    if not name:
        raise ValueError("no render node")
    return glb_bytes_multi(render, name, collision)
