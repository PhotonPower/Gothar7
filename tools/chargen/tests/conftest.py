"""Fixtures: the committed reference rig and helpers to build broken variants of it."""

from __future__ import annotations

import copy
import struct
from pathlib import Path

import numpy as np
import pytest

from gothar_chargen.gltf import Gltf
from gothar_chargen.skeleton import RigSpec, load_rig
from gothar_chargen.validate import ReferencePose, reference_pose

REPO_ROOT = Path(__file__).resolve().parents[3]
REFERENCE_GLB = REPO_ROOT / "assets/source/characters/rig/human_reference.glb"


@pytest.fixture(scope="session")
def rig() -> RigSpec:
    return load_rig()


@pytest.fixture(scope="session")
def reference_gltf() -> Gltf:
    return Gltf.load(REFERENCE_GLB)


@pytest.fixture(scope="session")
def reference(reference_gltf: Gltf) -> ReferencePose:
    return reference_pose(reference_gltf)


@pytest.fixture
def figure(reference_gltf: Gltf) -> Gltf:
    """A fresh, mutable copy of the reference rig (with skinned mannequin)."""
    return Gltf(doc=copy.deepcopy(reference_gltf.doc), bin=reference_gltf.bin)


# --- mutation helpers -------------------------------------------------------------------------


def node_index(g: Gltf, name: str) -> int:
    return next(i for i, n in enumerate(g.doc["nodes"]) if n.get("name") == name)


def node(g: Gltf, name: str) -> dict:
    return g.doc["nodes"][node_index(g, name)]


def strip_skin(g: Gltf) -> Gltf:
    """Removes mesh and skin so only the bare skeleton is checked."""
    for n in g.doc["nodes"]:
        n.pop("mesh", None)
        n.pop("skin", None)
    return g


def set_accessor(g: Gltf, index: int, data: np.ndarray) -> None:
    """Overwrites a tightly packed accessor in the binary chunk (same size and type)."""
    acc = g.doc["accessors"][index]
    view = g.doc["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    old = g.accessor(index)
    raw = np.ascontiguousarray(data.astype(old.dtype)).tobytes()
    assert len(raw) == old.nbytes
    buf = bytearray(g.bin)
    buf[start : start + len(raw)] = raw
    g.bin = bytes(buf)


def append_accessor(g: Gltf, data: np.ndarray, acc_type: str) -> int:
    """Appends float32 data as a new buffer view + accessor; returns the accessor index."""
    raw = np.ascontiguousarray(data, dtype=np.float32).tobytes()
    pad = -len(g.bin) % 4
    offset = len(g.bin) + pad
    g.bin = g.bin + b"\0" * pad + raw
    g.doc["buffers"][0]["byteLength"] = len(g.bin)
    g.doc["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(raw)})
    acc = {
        "bufferView": len(g.doc["bufferViews"]) - 1,
        "componentType": 5126,
        "count": int(data.shape[0]),
        "type": acc_type,
    }
    if acc_type == "SCALAR":
        acc["min"] = [float(data.min())]
        acc["max"] = [float(data.max())]
    g.doc["accessors"].append(acc)
    return len(g.doc["accessors"]) - 1


def add_clip(g: Gltf, name: str, bones: list[str], duration: float = 1.0) -> None:
    """Adds an animation rotating the given nodes with identity keys at 0 and ``duration``."""
    times = append_accessor(g, np.array([0.0, duration]), "SCALAR")
    quats = append_accessor(g, np.array([[0, 0, 0, 1], [0, 0, 0, 1]], dtype=np.float32), "VEC4")
    anim = {"name": name, "samplers": [], "channels": []}
    for b in bones:
        anim["samplers"].append({"input": times, "output": quats, "interpolation": "LINEAR"})
        anim["channels"].append(
            {
                "sampler": len(anim["samplers"]) - 1,
                "target": {"node": node_index(g, b), "path": "rotation"},
            }
        )
    g.doc.setdefault("animations", []).append(anim)


def rotate_node(g: Gltf, name: str, axis: tuple[float, float, float], degrees: float) -> None:
    """Pre-multiplies a node's local rotation by an axis-angle rotation."""
    n = node(g, name)
    ax = np.array(axis, dtype=np.float64)
    ax /= np.linalg.norm(ax)
    h = np.radians(degrees) / 2
    q2 = np.array([*(ax * np.sin(h)), np.cos(h)])
    q1 = np.array(n.get("rotation", [0, 0, 0, 1]), dtype=np.float64)
    x1, y1, z1, w1 = q2
    x2, y2, z2, w2 = q1
    n["rotation"] = [
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ]


def glb_header_length(data: bytes) -> int:
    return struct.unpack_from("<I", data, 8)[0]


def pytest_sessionstart(session) -> None:
    """Figures are generated, not versioned (§6.2): assemble them once before the tests."""
    from gothar_chargen.assemble import assemble_all

    characters = REPO_ROOT / "assets/source/characters"
    assemble_all(characters / "figures", characters)
