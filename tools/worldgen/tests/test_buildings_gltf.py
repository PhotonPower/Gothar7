import struct

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import MeshData, glb_bytes, read_glb, write_glb


def quad(n_quads: int = 1) -> MeshData:
    pos, nrm, uv, idx = [], [], [], []
    for i in range(n_quads):
        base = len(pos)
        pos += [(i, 0, 0), (i + 1, 0, 0), (i + 1, 1, 0), (i, 1, 0)]
        nrm += [(0, 0, 1)] * 4
        uv += [(0, 0), (1, 0), (1, 1), (0, 1)]
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return MeshData(np.array(pos, np.float32), np.array(nrm, np.float32),
                    np.array(uv, np.float32), np.array(idx, np.uint32))  # fmt: skip


def test_glb_structure_and_round_trip():
    data = glb_bytes(quad(), "test")
    assert data[:4] == b"glTF" and len(data) % 4 == 0
    doc, binary = read_glb(data)
    assert doc["asset"]["version"] == "2.0"
    prim = doc["meshes"][0]["primitives"][0]
    assert prim["attributes"] == {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}
    acc = doc["accessors"]
    assert acc[0]["min"] == [0.0, 0.0, 0.0] and acc[0]["max"] == [1.0, 1.0, 0.0]
    assert acc[3]["componentType"] == 5123  # uint16 indices for small meshes
    view = doc["bufferViews"][0]
    positions = np.frombuffer(binary, "<f4", count=12, offset=view["byteOffset"]).reshape(4, 3)
    np.testing.assert_array_equal(positions, quad().positions)
    for v in doc["bufferViews"]:
        assert v["byteOffset"] % 4 == 0 and v["byteOffset"] + v["byteLength"] <= len(binary)
    assert doc["buffers"][0]["byteLength"] == len(binary)


def test_large_meshes_use_uint32_indices():
    doc, _ = read_glb(glb_bytes(quad(17000), "big"))  # 68000 vertices
    assert doc["accessors"][3]["componentType"] == 5125


def test_deterministic_and_written(tmp_path):
    assert glb_bytes(quad(), "a") == glb_bytes(quad(), "a")
    write_glb(tmp_path / "x" / "a.glb", quad(), "a")
    assert (tmp_path / "x" / "a.glb").read_bytes() == glb_bytes(quad(), "a")


def test_invalid_input():
    m = quad()
    with pytest.raises(ValueError):
        glb_bytes(MeshData(m.positions, m.normals, m.uvs, m.indices[:4]), "x")
    with pytest.raises(ValueError):
        glb_bytes(MeshData(m.positions, m.normals, m.uvs, m.indices + 10), "x")
    with pytest.raises(ValueError):
        read_glb(b"glTF" + struct.pack("<II", 1, 12))
