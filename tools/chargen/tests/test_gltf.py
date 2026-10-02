from __future__ import annotations

import struct

import numpy as np
import pytest

from conftest import glb_header_length
from gothar_chargen.gltf import (
    Gltf,
    GltfError,
    node_matrix,
    node_trs,
    quat_angle_deg,
    quat_from_matrix,
    quat_to_matrix,
)


def test_roundtrip(reference_gltf):
    data = reference_gltf.to_bytes()
    assert glb_header_length(data) == len(data)
    again = Gltf.from_bytes(data)
    assert again.doc == reference_gltf.doc
    assert again.bin[: len(reference_gltf.bin)] == reference_gltf.bin


@pytest.mark.parametrize(
    ("data", "message"),
    [
        (b"short", "too short"),
        (struct.pack("<III", 0x12345678, 2, 20) + b"\0" * 8, "magic"),
        (struct.pack("<III", 0x46546C67, 1, 20) + b"\0" * 8, "version"),
        (struct.pack("<III", 0x46546C67, 2, 999) + b"\0" * 8, "truncated"),
        (
            struct.pack("<III", 0x46546C67, 2, 20) + struct.pack("<II", 0, 0x004E4942),
            "missing JSON",
        ),
        (
            struct.pack("<III", 0x46546C67, 2, 24) + struct.pack("<II", 4, 0x4E4F534A) + b"{{{{",
            "invalid JSON",
        ),
    ],
)
def test_invalid_glb(data, message):
    with pytest.raises(GltfError, match=message):
        Gltf.from_bytes(data)


def test_load_missing_file(tmp_path):
    with pytest.raises(GltfError):
        Gltf.load(tmp_path / "nope.glb")


def _doc(accessor: dict, view: dict) -> dict:
    return {"accessors": [accessor], "bufferViews": [view], "buffers": [{"byteLength": 0}]}


def test_strided_normalised_accessor():
    # two vertices, each: 2 x uint8 normalised + 2 padding bytes (stride 4)
    binary = bytes([255, 0, 9, 9, 0, 255, 9, 9])
    g = Gltf(
        _doc(
            {
                "bufferView": 0,
                "componentType": 5121,
                "count": 2,
                "type": "VEC2",
                "normalized": True,
            },
            {"buffer": 0, "byteOffset": 0, "byteLength": 8, "byteStride": 4},
        ),
        binary,
    )
    np.testing.assert_allclose(g.accessor(0), [[1.0, 0.0], [0.0, 1.0]])


def test_mat4_accessor_is_column_major():
    m = np.arange(16, dtype=np.float32).reshape(4, 4)
    binary = m.T.tobytes()  # column-major in the file
    g = Gltf(
        _doc(
            {"bufferView": 0, "componentType": 5126, "count": 1, "type": "MAT4"},
            {"buffer": 0, "byteLength": 64},
        ),
        binary,
    )
    np.testing.assert_array_equal(g.accessor(0)[0], m)


@pytest.mark.parametrize(
    ("accessor", "message"),
    [
        (
            {"bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC3", "sparse": {}},
            "sparse",
        ),
        ({"bufferView": 0, "componentType": 1, "count": 1, "type": "VEC3"}, "unsupported"),
        ({"bufferView": 0, "componentType": 5126, "count": 100, "type": "VEC3"}, "exceeds"),
    ],
)
def test_bad_accessors(accessor, message):
    g = Gltf(_doc(accessor, {"buffer": 0, "byteLength": 12}), b"\0" * 12)
    with pytest.raises(GltfError, match=message):
        g.accessor(0)
    with pytest.raises(GltfError, match="out of range"):
        g.accessor(5)


def test_accessor_without_buffer_view_is_zero():
    g = Gltf({"accessors": [{"componentType": 5126, "count": 3, "type": "SCALAR"}]})
    np.testing.assert_array_equal(g.accessor(0), np.zeros((3, 1)))


def test_quaternion_helpers():
    h = np.radians(30)
    q = np.array([0.0, np.sin(h), 0.0, np.cos(h)])  # 60° about Y
    m = quat_to_matrix(q)
    np.testing.assert_allclose(m @ [1, 0, 0], [0.5, 0, -np.sqrt(3) / 2], atol=1e-12)
    assert quat_angle_deg(quat_from_matrix(m), q) < 1e-6
    assert quat_angle_deg(q, -q) < 1e-6
    assert abs(quat_angle_deg(q, np.array([0, 0, 0, 1.0])) - 60) < 1e-6
    assert quat_angle_deg(np.zeros(4), q) == 180.0


@pytest.mark.parametrize(
    "q",
    [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0.5, 0.5, 0.5, 0.5], [0.1, -0.7, 0.2, 0.68]],
)
def test_quat_from_matrix_all_branches(q):
    q = np.array(q, dtype=np.float64)
    q /= np.linalg.norm(q)
    assert quat_angle_deg(quat_from_matrix(quat_to_matrix(q)), q) < 1e-6


def test_node_matrix_and_trs_agree():
    n = {"translation": [1, 2, 3], "rotation": [0, 0.7071068, 0, 0.7071068], "scale": [2, 2, 2]}
    m = node_matrix(n)
    as_matrix = {"matrix": m.T.flatten().tolist()}
    trs = node_trs(as_matrix)
    np.testing.assert_allclose(trs.translation, [1, 2, 3])
    np.testing.assert_allclose(trs.scale, [2, 2, 2], atol=1e-6)
    assert quat_angle_deg(trs.rotation, np.array(n["rotation"])) < 1e-4
    np.testing.assert_allclose(node_matrix(as_matrix), m)


def test_world_matrices_detect_cycles():
    g = Gltf({"nodes": [{"children": [1]}, {"children": [0]}]})
    with pytest.raises(GltfError, match="cycle"):
        g.world_matrices()
