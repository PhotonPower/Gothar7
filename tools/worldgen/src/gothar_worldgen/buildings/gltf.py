"""Minimal glTF 2.0 binary (.glb) writer for generated meshes.

Limits (on purpose): one mesh with one or more triangle primitives (one untextured PBR material
each), one node; attributes POSITION, NORMAL and TEXCOORD_0 (float32), indices uint16 or uint32
per primitive. No textures, skins, animations, morph targets or extensions. Output is
byte-deterministic.
"""

from __future__ import annotations

import json
import struct
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


def glb_bytes(
    mesh: MeshData, name: str, color: tuple[float, float, float, float] = (0.6, 0.6, 0.6, 1.0)
) -> bytes:
    """Single-material file (massing)."""
    return glb_bytes_multi([Primitive("massing", color, mesh)], name)


def glb_bytes_multi(primitives: list[Primitive], name: str) -> bytes:
    """One mesh, a primitive per entry (empty ones skipped), materials in first-use order."""
    prims = [p for p in primitives if len(p.mesh.indices)]
    if not prims:
        raise ValueError("mesh needs vertices and whole triangles")
    blobs: list[bytes] = []
    accessors: list[dict] = []
    gl_prims: list[dict] = []
    materials: list[dict] = []
    material_index: dict[str, int] = {}
    for p in prims:
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
        if p.material not in material_index:
            material_index[p.material] = len(materials)
            materials.append({"name": p.material, "pbrMetallicRoughness": {
                "baseColorFactor": [float(c) for c in p.color], "metallicFactor": 0.0,
                "roughnessFactor": 0.9}})  # fmt: skip
        gl_prims.append({
            "attributes": {"POSITION": first, "NORMAL": first + 1, "TEXCOORD_0": first + 2},
            "indices": first + 3, "material": material_index[p.material], "mode": 4})  # fmt: skip

    views, offset = [], 0
    for i, blob in enumerate(blobs):
        target = _ELEMENT_ARRAY_BUFFER if i % 4 == 3 else _ARRAY_BUFFER
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob), "target": target})
        offset += len(blob) + (-len(blob) % 4)
    binary = b"".join(_pad(b, b"\0") for b in blobs)

    doc = {
        "asset": {"version": "2.0", "generator": "gothar-worldgen"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": gl_prims}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
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
