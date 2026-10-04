"""Minimal glTF 2.0 binary (.glb) reader/writer: JSON + BIN chunk, accessors, node transforms.

Only what the validator needs; not a general glTF library (no sparse accessors or external
buffers).
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

_GLB_MAGIC = 0x46546C67  # "glTF"
_CHUNK_JSON = 0x4E4F534A
_CHUNK_BIN = 0x004E4942

_COMPONENT_DTYPES: dict[int, np.dtype] = {
    5120: np.dtype(np.int8),
    5121: np.dtype(np.uint8),
    5122: np.dtype(np.int16),
    5123: np.dtype(np.uint16),
    5125: np.dtype(np.uint32),
    5126: np.dtype(np.float32),
}
_TYPE_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


class GltfError(Exception):
    """The file is not a readable glTF binary or uses unsupported features."""


@dataclass
class Gltf:
    """Parsed .glb: the JSON document and the binary chunk."""

    doc: dict[str, Any]
    bin: bytes = b""
    path: Path | None = None

    # --- I/O -------------------------------------------------------------------------------

    @classmethod
    def from_bytes(cls, data: bytes, path: Path | None = None) -> Gltf:
        if len(data) < 20:
            raise GltfError("file too short for a glTF binary")
        magic, version, length = struct.unpack_from("<III", data, 0)
        if magic != _GLB_MAGIC:
            raise GltfError("not a .glb file (bad magic)")
        if version != 2:
            raise GltfError(f"unsupported glTF version {version}")
        if length > len(data):
            raise GltfError("truncated .glb file")
        offset = 12
        doc: dict[str, Any] | None = None
        binary = b""
        while offset + 8 <= length:
            chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
            chunk = data[offset + 8 : offset + 8 + chunk_len]
            if chunk_type == _CHUNK_JSON:
                try:
                    doc = json.loads(chunk.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as e:
                    raise GltfError(f"invalid JSON chunk: {e}") from e
            elif chunk_type == _CHUNK_BIN and not binary:
                binary = bytes(chunk)
            offset += 8 + chunk_len
        if doc is None:
            raise GltfError("missing JSON chunk")
        return cls(doc=doc, bin=binary, path=path)

    @classmethod
    def load(cls, path: Path) -> Gltf:
        try:
            data = path.read_bytes()
        except OSError as e:
            raise GltfError(str(e)) from e
        return cls.from_bytes(data, path)

    def to_bytes(self) -> bytes:
        js = json.dumps(self.doc, separators=(",", ":")).encode("utf-8")
        js += b" " * (-len(js) % 4)
        bn = self.bin + b"\0" * (-len(self.bin) % 4)
        chunks = struct.pack("<II", len(js), _CHUNK_JSON) + js
        if bn:
            chunks += struct.pack("<II", len(bn), _CHUNK_BIN) + bn
        return struct.pack("<III", _GLB_MAGIC, 2, 12 + len(chunks)) + chunks

    # --- accessors -------------------------------------------------------------------------

    def list(self, key: str) -> list[dict[str, Any]]:
        value = self.doc.get(key, [])
        return value if isinstance(value, list) else []

    def accessor(self, index: int) -> np.ndarray:
        """Returns accessor data as array of shape (count, components) (MAT4: (count, 4, 4))."""
        accessors = self.list("accessors")
        if not 0 <= index < len(accessors):
            raise GltfError(f"accessor {index} out of range")
        acc = accessors[index]
        if "sparse" in acc:
            raise GltfError(f"accessor {index}: sparse accessors are not supported")
        dtype = _COMPONENT_DTYPES.get(acc.get("componentType", 0))
        ncomp = _TYPE_COUNTS.get(acc.get("type", ""))
        if dtype is None or ncomp is None:
            raise GltfError(f"accessor {index}: unsupported type")
        count = int(acc.get("count", 0))
        if "bufferView" not in acc:
            return np.zeros((count, ncomp), dtype=dtype)
        views = self.list("bufferViews")
        view = views[acc["bufferView"]]
        if view.get("buffer", 0) != 0:
            raise GltfError("only the embedded binary buffer is supported")
        start = int(view.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
        elem_size = dtype.itemsize * ncomp
        stride = int(view.get("byteStride", elem_size)) or elem_size
        end = start + stride * (count - 1) + elem_size if count else start
        if end > len(self.bin) or end > int(view.get("byteOffset", 0)) + int(view["byteLength"]):
            raise GltfError(f"accessor {index} exceeds its buffer view")
        raw = np.frombuffer(self.bin, dtype=np.uint8, count=end - start, offset=start)
        if stride == elem_size:
            arr = raw.view(dtype).reshape(count, ncomp)
        else:
            rows = np.lib.stride_tricks.as_strided(
                raw, shape=(count, elem_size), strides=(stride, 1)
            )
            arr = np.ascontiguousarray(rows).view(dtype).reshape(count, ncomp)
        if acc.get("normalized", False) and dtype.kind in "iu":
            arr = arr.astype(np.float64) / float(np.iinfo(dtype).max)
        if acc["type"] == "MAT4":
            # glTF matrices are column-major
            return arr.reshape(count, 4, 4).transpose(0, 2, 1).copy()
        return arr

    def set_accessor(self, index: int, values: np.ndarray) -> None:
        """Overwrites the data of a tightly packed float accessor in place (same count and type),
        e.g. animation keys; min/max are updated when the accessor has them."""
        acc = self.list("accessors")[index]
        old = self.accessor(index)
        if acc.get("componentType") != 5126 or "sparse" in acc or "bufferView" not in acc:
            raise GltfError(f"accessor {index}: only plain float accessors can be written")
        values = np.asarray(values, dtype=np.float32).reshape(old.shape)
        view = self.list("bufferViews")[acc["bufferView"]]
        elem = 4 * values[0].size if len(values) else 0
        if int(view.get("byteStride", elem) or elem) != elem:
            raise GltfError(f"accessor {index}: interleaved data cannot be written")
        start = int(view.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
        data = bytearray(self.bin)
        data[start : start + values.nbytes] = values.tobytes()
        self.bin = bytes(data)
        if "min" in acc and "max" in acc and len(values):
            flat = values.reshape(len(values), -1)
            acc["min"] = [float(x) for x in flat.min(axis=0)]
            acc["max"] = [float(x) for x in flat.max(axis=0)]

    def set_indices(self, index: int, values: np.ndarray) -> None:
        """Writes fewer (or as many) indices into an index accessor in place: the count shrinks,
        the buffer view keeps its place (unused bytes stay in the buffer)."""
        acc = self.list("accessors")[index]
        old = self.accessor(index)
        values = np.asarray(values).ravel()
        if len(values) > old.size or "bufferView" not in acc or "sparse" in acc:
            raise GltfError(f"accessor {index}: indices can only shrink in place")
        dtype = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32}[acc["componentType"]]
        data = values.astype(dtype)
        view = self.list("bufferViews")[acc["bufferView"]]
        start = int(view.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
        buf = bytearray(self.bin)
        buf[start : start + data.nbytes] = data.tobytes()
        self.bin = bytes(buf)
        acc["count"] = int(len(values))
        if "min" in acc and "max" in acc and len(values):
            acc["min"], acc["max"] = [int(values.min())], [int(values.max())]

    # --- nodes -----------------------------------------------------------------------------

    def node_parents(self) -> dict[int, int]:
        parents: dict[int, int] = {}
        for i, node in enumerate(self.list("nodes")):
            for c in node.get("children", []):
                parents[c] = i
        return parents

    def local_matrix(self, index: int) -> np.ndarray:
        return node_matrix(self.list("nodes")[index])

    def world_matrices(self) -> dict[int, np.ndarray]:
        parents = self.node_parents()
        cache: dict[int, np.ndarray] = {}
        for start in range(len(self.list("nodes"))):
            # walk up to the first known ancestor, then compose downwards
            chain: list[int] = []
            i: int | None = start
            while i is not None and i not in cache:
                if i in chain:
                    raise GltfError("node hierarchy contains a cycle")
                chain.append(i)
                i = parents.get(i)
            m = cache[i] if i is not None else np.eye(4)
            for j in reversed(chain):
                m = m @ self.local_matrix(j)
                cache[j] = m
        return cache


@dataclass(frozen=True)
class Trs:
    translation: np.ndarray = field(default_factory=lambda: np.zeros(3))
    rotation: np.ndarray = field(default_factory=lambda: np.array([0.0, 0.0, 0.0, 1.0]))  # x,y,z,w
    scale: np.ndarray = field(default_factory=lambda: np.ones(3))


def node_trs(node: dict[str, Any]) -> Trs:
    """Local TRS of a node (decomposes ``matrix`` if given, assuming no shear)."""
    if "matrix" in node:
        m = np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        t = m[:3, 3].copy()
        s = np.linalg.norm(m[:3, :3], axis=0)
        r = m[:3, :3] / np.where(s == 0, 1, s)
        return Trs(t, quat_from_matrix(r), s)
    return Trs(
        np.array(node.get("translation", [0, 0, 0]), dtype=np.float64),
        np.array(node.get("rotation", [0, 0, 0, 1]), dtype=np.float64),
        np.array(node.get("scale", [1, 1, 1]), dtype=np.float64),
    )


def node_matrix(node: dict[str, Any]) -> np.ndarray:
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    trs = node_trs(node)
    m = np.eye(4)
    m[:3, :3] = quat_to_matrix(trs.rotation) * trs.scale
    m[:3, 3] = trs.translation
    return m


def quat_to_matrix(q: np.ndarray) -> np.ndarray:
    x, y, z, w = (float(v) for v in q / (np.linalg.norm(q) or 1.0))
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


def quat_from_matrix(m: np.ndarray) -> np.ndarray:
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    arr = np.array(q, dtype=np.float64)
    return arr / np.linalg.norm(arr)


def quat_angle_deg(a: np.ndarray, b: np.ndarray) -> float:
    """Angle between two rotations in degrees (sign-independent)."""
    na, nb = np.linalg.norm(a), np.linalg.norm(b)
    if na == 0 or nb == 0:
        return 180.0
    d = abs(float(np.dot(a / na, b / nb)))
    return float(np.degrees(2.0 * np.arccos(min(1.0, d))))
