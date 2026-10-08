"""Own stubble beards (F3, characters-pipeline.md §6): texture and the committed parts."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

import numpy as np

from gothar_chargen.gltf import Gltf
from gothar_chargen.skeleton import load_rig
from gothar_chargen.stubble import (
    BAND_DENSITY,
    MALE_HEADS,
    NEUTRAL_MEAN,
    PART,
    TEXTURE,
    png_bytes,
    stubble_texture,
)

CHARACTERS = Path(__file__).resolve().parents[3] / "assets" / "source" / "characters"


def _decode_png(data: bytes) -> np.ndarray:
    """Reads back our own RGBA PNG (filter 0) without Pillow."""
    assert data[:8] == bytes([137, 80, 78, 71, 13, 10, 26, 10])
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
            assert body[8:10] == bytes([8, 6])  # 8 bit RGBA
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = np.frombuffer(zlib.decompress(idat), dtype=np.uint8).reshape(height, 1 + 4 * width)
    assert (raw[:, 0] == 0).all()
    return raw[:, 1:].reshape(height, width, 4)


def test_texture_bands_and_neutral_mean():
    rgba = stubble_texture()
    alpha = rgba[..., 3]
    assert set(np.unique(alpha)) <= {0, 255}  # alpha MASK: hairs or nothing
    lum = rgba[..., 0][alpha > 0] / 255
    assert abs(lum.mean() - NEUTRAL_MEAN) < 0.01  # texture contract §2.3
    quarter = rgba.shape[0] // 4
    shares = [float((alpha[k * quarter : (k + 1) * quarter] > 0).mean()) for k in range(4)]
    # image rows from the top: band 3 (sparse) ... band 0 (dense) at the bottom (UV v = 0)
    for share, density in zip(shares, BAND_DENSITY[::-1], strict=True):
        assert abs(share - density) < 0.02
    assert shares == sorted(shares)
    assert (_decode_png(png_bytes(rgba)) == rgba).all()


def test_committed_texture_matches():
    path = CHARACTERS / "textures" / "hair" / TEXTURE
    assert (_decode_png(path.read_bytes()) == stubble_texture()).all()


def test_stubble_parts_of_all_male_heads():
    names = list(load_rig().morph_targets)
    for head in MALE_HEADS:
        g = Gltf.load(CHARACTERS / "parts" / f"hair_m_{head}" / f"{PART}.glb")
        nodes = {n.get("name"): n for n in g.list("nodes")}
        assert {"beard_lod0", "beard_lod1", "beard_lod2"} <= set(nodes), head
        counts = []
        for level in range(3):
            mesh = g.doc["meshes"][nodes[f"beard_lod{level}"]["mesh"]]
            counts.append(
                sum(g.doc["accessors"][p["indices"]]["count"] // 3 for p in mesh["primitives"])
            )
            targets = (mesh.get("extras") or {}).get("targetNames", [])
            assert targets == (names if level == 0 else []), (head, level)
        assert counts[0] <= 600 and counts[1] < counts[0] and counts[2] < counts[1], (head, counts)
        mats = g.list("materials")
        assert [m["name"] for m in mats] == [PART]  # not "beard": the heads mid/old carry one
        assert mats[0]["alphaMode"] == "MASK"
        assert g.list("images")[0]["uri"].endswith(f"textures/hair/{TEXTURE}")
