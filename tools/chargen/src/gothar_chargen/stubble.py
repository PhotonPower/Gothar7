"""Stubble beards (F3, own work): a thin shell over the beard zone of each male head with a neutral
texture of fine hairs (characters-pipeline.md §6). Blender builds the geometry
(``blender/build_stubble.py``); this module holds the shared settings and the texture (numpy and
the standard library only, so Blender's Python can import it).

The texture has four horizontal bands of decreasing hair density; every face of the shell takes
its UVs from the band of its zone density, so the stubble thins out softly towards the edge of the
beard zone. The engine keeps the alpha coverage over the mip levels (coverage-preserving mips),
so the density is tuned for the close-up.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

import numpy as np

MALE_HEADS = ("bald", "farmer", "gaunt", "mid", "old", "rough", "stubble", "young")  # head_m_<x>
PART = "beard_stubble"  # parts/hair_m_<head>/beard_stubble.glb, material and palette key
TEXTURE = "beard_stubble_neutral.png"  # below textures/hair/
TRIANGLES = (600, 300, 120)  # lod0..2 (budget per beard, §6)
BAND_DENSITY = (0.13, 0.09, 0.05, 0.02)  # share of hair pixels per band, band 0 = beard centre
NEUTRAL_MEAN = 0.55  # texture contract §2.3: the palette colour is the mean
SIZE = 512


def stubble_texture(size: int = SIZE, seed: int = 3) -> np.ndarray:
    """(size, size, 4) uint8 RGBA: 2x2 px hairs, alpha 255 or 0, grey with mean NEUTRAL_MEAN over
    the hair pixels. Image row 0 is the top (UV v = 1): band k covers v in [k/4, (k+1)/4), so the
    bottom quarter is band 0 (densest)."""
    rng = np.random.default_rng(seed)
    half = size // 2
    bands = len(BAND_DENSITY)
    density = np.repeat(BAND_DENSITY[::-1], half // bands)[:, None]
    hairs = rng.random((half, half)) < density
    alpha = np.kron(hairs, np.ones((2, 2), dtype=bool))
    lum = 1.0 + 0.15 * (rng.random((size, size)) - 0.5)
    lum = np.clip(lum / lum[alpha].mean() * NEUTRAL_MEAN, 0.0, 1.0)
    rgba = np.zeros((size, size, 4), dtype=np.uint8)
    rgba[..., :3] = np.round(lum * 255)[..., None]
    rgba[..., 3] = np.where(alpha, 255, 0)
    return rgba


def png_bytes(rgba: np.ndarray) -> bytes:
    """Minimal RGBA PNG encoder (8 bit, filter 0 per row), standard library only."""
    height, width = rgba.shape[:2]
    zero = bytes(1)
    raw = b"".join(zero + rgba[row].tobytes() for row in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(kind + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8 bit RGBA
    signature = bytes([137, 80, 78, 71, 13, 10, 26, 10])
    return (
        signature
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def write_texture(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png_bytes(stubble_texture()))
    return path
