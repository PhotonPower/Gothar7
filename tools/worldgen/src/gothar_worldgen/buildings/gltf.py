"""Minimal glTF 2.0 binary (.glb) writer for generated meshes.

Limits (on purpose): one render mesh with one or more triangle primitives (one PBR material each,
untextured or with an external base colour and normal image, W5 textures) in one node;
attributes POSITION, NORMAL and TEXCOORD_0 (float32), indices uint16 or uint32 per primitive.
Optional collision nodes (``COL_HULL_*`` convex hulls, ``COL_*`` triangle meshes; contract with
engine, M5 part B): one mesh each with POSITION and indices only, no
material, at the root next to the render node with the same origin. No embedded images, skins,
animations, morph targets or extensions. Output is byte-deterministic.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt

_GLB_MAGIC = 0x46546C67  # "glTF"
_CHUNK_JSON = 0x4E4F534A
_CHUNK_BIN = 0x004E4942
_FLOAT = 5126
_UINT16 = 5123
_UINT32 = 5125
_ARRAY_BUFFER = 34962
_ELEMENT_ARRAY_BUFFER = 34963


@dataclass
class MeshData:
    positions: npt.NDArray[np.float32]  # (n, 3), metres, +Y up
    normals: npt.NDArray[np.float32]  # (n, 3), unit length
    uvs: npt.NDArray[np.float32]  # (n, 2)
    indices: npt.NDArray[np.uint32]  # (m,), triangles, counter-clockwise = front

    @property
    def triangle_count(self) -> int:
        return len(self.indices) // 3


def _pad(data: bytes, fill: bytes) -> bytes:
    return data + fill * (-len(data) % 4)


@dataclass
class Primitive:
    material: str  # material name; equal names share one material entry
    color: tuple[float, float, float, float]
    mesh: MeshData
    textures: tuple[str, str] | None = None  # (base colour, normal) image URIs from the VFS root


@dataclass
class Part:
    """A named node beside the render node (``glb_bytes_multi(..., parts=)``): its own mesh at
    ``translation`` (the pivot, e.g. a chest lid's hinge ``MOB_LID``), with its collision bodies as
    child nodes, so they move with it."""

    name: str
    translation: tuple[float, float, float]
    primitives: list[Primitive]
    collision: Sequence[CollisionPart] = ()


@dataclass
class CollisionPart:
    name: str  # node name: COL_HULL_<i> (convex hull of the points) or COL_<i> (triangle mesh)
    positions: npt.NDArray[np.float32]  # (n, 3), same origin as the render mesh
    indices: npt.NDArray[np.uint32]  # (m,), closed triangle mesh, counter-clockwise = outside

    @property
    def triangle_count(self) -> int:
        return len(self.indices) // 3


def glb_bytes(
    mesh: MeshData, name: str, color: tuple[float, float, float, float] = (0.6, 0.6, 0.6, 1.0)
) -> bytes:
    """Single-material file (massing)."""
    return glb_bytes_multi([Primitive("massing", color, mesh)], name)


def glb_bytes_multi(
    primitives: list[Primitive],
    name: str,
    collision: Sequence[CollisionPart] = (),
    parts: Sequence[Part] = (),
) -> bytes:
    """One mesh, a primitive per entry (empty ones skipped), materials in first-use order; then
    one node and mesh per collision part; then one node per ``parts`` entry (mesh, translation,
    its collision as children)."""
    prims = [p for p in primitives if len(p.mesh.indices)]
    if not prims:
        raise ValueError("mesh needs vertices and whole triangles")
    blobs: list[bytes] = []
    targets: list[int] = []
    accessors: list[dict] = []
    gl_prims: list[dict] = []
    materials: list[dict] = []
    material_index: dict[tuple[str, tuple[str, str] | None], int] = {}
    images: list[dict] = []
    image_index: dict[str, int] = {}

    def texture(uri: str) -> int:  # one texture per image, all with the repeating sampler 0
        if uri not in image_index:
            image_index[uri] = len(images)
            images.append({"uri": uri})
        return image_index[uri]

    def encode(prims: list[Primitive]) -> list[dict]:
        gl_prims: list[dict] = []
        for p in prims:
            gl_prims.append(encode_one(p))
        return gl_prims

    def encode_one(p: Primitive) -> dict:
        m = p.mesh
        if len(m.positions) == 0 or len(m.indices) % 3:
            raise ValueError("mesh needs vertices and whole triangles")
        if m.indices.max() >= len(m.positions):
            raise ValueError("index out of range")
        pos = np.ascontiguousarray(m.positions, dtype="<f4")
        small = len(pos) <= 0xFFFF
        arrays = [
            (pos, "VEC3", _FLOAT),
            (np.ascontiguousarray(m.normals, dtype="<f4"), "VEC3", _FLOAT),
            (np.ascontiguousarray(m.uvs, dtype="<f4"), "VEC2", _FLOAT),
            (np.ascontiguousarray(m.indices, dtype="<u2" if small else "<u4"), "SCALAR",
             _UINT16 if small else _UINT32),
        ]  # fmt: skip
        first = len(accessors)
        for i, (arr, kind, ctype) in enumerate(arrays):
            acc = {
                "bufferView": len(blobs),
                "componentType": ctype,
                "count": len(arr),
                "type": kind,
            }
            if i == 0:
                acc["min"] = [float(v) for v in pos.min(axis=0)]
                acc["max"] = [float(v) for v in pos.max(axis=0)]
            accessors.append(acc)
            blobs.append(arr.tobytes())
            targets.append(_ELEMENT_ARRAY_BUFFER if i == 3 else _ARRAY_BUFFER)
        key = (p.material, p.textures)
        if key not in material_index:
            material_index[key] = len(materials)
            pbr: dict = {"baseColorFactor": [float(c) for c in p.color], "metallicFactor": 0.0,
                         "roughnessFactor": 0.9}  # fmt: skip
            material: dict = {"name": p.material, "pbrMetallicRoughness": pbr}
            if p.textures is not None:
                pbr["baseColorTexture"] = {"index": texture(p.textures[0])}
                material["normalTexture"] = {"index": texture(p.textures[1])}
            materials.append(material)
        return {
            "attributes": {"POSITION": first, "NORMAL": first + 1, "TEXCOORD_0": first + 2},
            "indices": first + 3, "material": material_index[key], "mode": 4}  # fmt: skip

    gl_prims = encode(prims)
    meshes: list[dict] = [{"name": name, "primitives": gl_prims}]
    nodes: list[dict] = [{"mesh": 0, "name": name}]
    roots = [0]

    def collision_node(part: CollisionPart) -> int:
        if not part.name.startswith("COL_"):
            raise ValueError("collision node names start with COL_")
        if len(part.positions) == 0 or len(part.indices) == 0 or len(part.indices) % 3:
            raise ValueError("collision part needs vertices and whole triangles")
        if part.indices.max() >= len(part.positions):
            raise ValueError("index out of range")
        pos = np.ascontiguousarray(part.positions, dtype="<f4")
        small = len(pos) <= 0xFFFF
        idx = np.ascontiguousarray(part.indices, dtype="<u2" if small else "<u4")
        first = len(accessors)
        accessors.append({"bufferView": len(blobs), "componentType": _FLOAT, "count": len(pos),
                          "type": "VEC3", "min": [float(v) for v in pos.min(axis=0)],
                          "max": [float(v) for v in pos.max(axis=0)]})  # fmt: skip
        blobs.append(pos.tobytes())
        targets.append(_ARRAY_BUFFER)
        accessors.append({"bufferView": len(blobs), "componentType": _UINT16 if small else _UINT32,
                          "count": len(idx), "type": "SCALAR"})  # fmt: skip
        blobs.append(idx.tobytes())
        targets.append(_ELEMENT_ARRAY_BUFFER)
        meshes.append({"name": part.name,
                       "primitives": [{"attributes": {"POSITION": first}, "indices": first + 1,
                                       "mode": 4}]})  # fmt: skip
        nodes.append({"mesh": len(meshes) - 1, "name": part.name})
        return len(nodes) - 1

    for part in collision:
        roots.append(collision_node(part))
    for part in parts:
        gl = encode([p for p in part.primitives if len(p.mesh.indices)])
        if not gl:
            raise ValueError(f"part {part.name} needs vertices and whole triangles")
        meshes.append({"name": part.name, "primitives": gl})
        node: dict = {"mesh": len(meshes) - 1, "name": part.name,
                      "translation": [float(v) for v in part.translation]}  # fmt: skip
        nodes.append(node)
        roots.append(len(nodes) - 1)
        children = [collision_node(c) for c in part.collision]
        if children:
            node["children"] = children

    views, offset = [], 0
    for blob, target in zip(blobs, targets, strict=True):
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob), "target": target})
        offset += len(blob) + (-len(blob) % 4)
    binary = b"".join(_pad(b, b"\0") for b in blobs)

    doc = {
        "asset": {"version": "2.0", "generator": "gothar-worldgen"},
        "scene": 0,
        "scenes": [{"nodes": roots}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
    if images:
        doc["images"] = images
        doc["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        doc["textures"] = [{"sampler": 0, "source": i} for i in range(len(images))]
    js = _pad(json.dumps(doc, separators=(",", ":")).encode("utf-8"), b" ")
    total = 12 + 8 + len(js) + 8 + len(binary)
    return b"".join([
        struct.pack("<III", _GLB_MAGIC, 2, total),
        struct.pack("<II", len(js), _CHUNK_JSON), js,
        struct.pack("<II", len(binary), _CHUNK_BIN), binary,
    ])  # fmt: skip


def write_glb(path: Path, mesh: MeshData, name: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(glb_bytes(mesh, name))
    tmp.replace(path)


def read_glb(data: bytes) -> tuple[dict, bytes]:
    """JSON document and binary chunk of a .glb (for tests and checks)."""
    magic, version, total = struct.unpack_from("<III", data, 0)
    if magic != _GLB_MAGIC or version != 2 or total != len(data):
        raise ValueError("not a glTF 2.0 binary")
    jlen, jtype = struct.unpack_from("<II", data, 12)
    if jtype != _CHUNK_JSON:
        raise ValueError("first chunk is not JSON")
    doc = json.loads(data[20 : 20 + jlen])
    blen, btype = struct.unpack_from("<II", data, 20 + jlen)
    if btype != _CHUNK_BIN:
        raise ValueError("second chunk is not BIN")
    return doc, data[28 + jlen : 28 + jlen + blen]
