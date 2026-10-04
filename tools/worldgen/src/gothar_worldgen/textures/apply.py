"""Textures on generated houses (W5): texture coordinates per kind and the image files.

Which texture a primitive gets follows from its palette entry (``kind_of``); the palette colour
stays the material factor, divided by ``albedo_scale`` of the kind (the images store 1.0 lower so
brighter details survive; dark wood lower still, for its grain). Texture coordinates, in units of
one texture (``TILE_M``), with v pointing up in the image (glTF v grows downwards, so world-up is
``-v``):

- walls (plaster, stone): the facade metres the builder wrote (u along the facade, v up);
- the dirty foot band (``~low`` materials): u along the facade, v from the band's bottom (0) to its
  top (1); the builder writes these already in band units;
- timber and boards: the beam metres (along, across) the builder wrote;
- roofs: recomputed from the face: u along the eave, v up the slope (rows of tiles stay parallel to
  the eave whatever the builder wrote); the shady side (``~moss``) gets the mossy tiles;
- plaster under a window with a rain streak (``~streak``): part of the wall, opaque, 0..1 as the
  builder wrote it (one of the texture's variants across, down from the sill).
"""

from __future__ import annotations

import hashlib
from collections.abc import Sequence
from dataclasses import replace
from pathlib import Path

import numpy as np

from gothar_worldgen.buildings.gltf import MeshData, Primitive
from gothar_worldgen.textures.procedural import KINDS, TILE_M, albedo_scale, make, to_png

LOW = "~low"  # material suffix of the dirty foot band
MOSS = "~moss"  # material suffix of the shady roof side
STREAK = "~streak"  # material suffix of plaster under a window with a rain streak


def kind_of(material: str) -> str | None:
    """Texture kind of a palette entry (``None``: stays untextured)."""
    name = material.removesuffix(LOW).removesuffix(MOSS).removesuffix(STREAK)
    if name.startswith(("plaster", "lehm")):
        if material.endswith(STREAK):
            return "plaster_streak"
        return "plaster_low" if material.endswith(LOW) else "plaster"
    if name in ("stone", "brick"):
        return "stone"
    if name.startswith("timber"):
        return "timber"
    if name.startswith("roof"):
        return "roof_moss" if material.endswith(MOSS) or name.endswith("_moss") else "roof"
    if name == "frame":
        return "boards"
    return None


def texture_paths(uri_root: str, kind: str) -> tuple[str, str]:
    return f"{uri_root}/{kind}_albedo.png", f"{uri_root}/{kind}_normal.png"


def _roof_uvs(mesh: MeshData) -> np.ndarray:
    p = mesh.positions.astype(np.float64)
    n = mesh.normals.astype(np.float64)
    up = np.array([0.0, 1.0, 0.0])
    slope = up[None, :] - n * (n @ up)[:, None]  # up the slope, in the face
    length = np.linalg.norm(slope, axis=1, keepdims=True)
    flat = length[:, 0] < 1e-6
    slope = np.where(flat[:, None], np.array([0.0, 0.0, -1.0]), slope / np.maximum(length, 1e-9))
    eave = np.cross(slope, n)
    eave /= np.maximum(np.linalg.norm(eave, axis=1, keepdims=True), 1e-9)
    return np.stack([np.sum(p * eave, axis=1), np.sum(p * slope, axis=1)], axis=1)


def textured(prim: Primitive, uri_root: str, offset: tuple[float, float] = (0.0, 0.0)) -> Primitive:
    """The primitive with texture coordinates for its kind and its two images; ``offset`` (in
    textures) shifts the repeating kinds, so neighbouring houses do not show the same stones."""
    kind = kind_of(prim.material)
    if kind is None:
        return prim
    tu, tv = TILE_M[kind]
    mesh = prim.mesh
    uv = _roof_uvs(mesh) if kind.startswith("roof") else mesh.uvs.astype(np.float64)
    if kind == "plaster_streak":  # the builder wrote 0..1 already (v down from the sill)
        out = uv
    elif kind == "plaster_low":
        out = np.stack([uv[:, 0] / tu, 1.0 - uv[:, 1]], axis=1)  # builder: v already 0..1 in band
    else:
        out = np.stack([uv[:, 0] / tu, -uv[:, 1] / tv], axis=1)
    if kind in SHIFTED:
        out = out + np.asarray(offset)
    elif kind == "plaster_low":
        out[:, 0] += offset[0]  # along the wall only: the band runs from the ground up
    scale = albedo_scale(kind)
    color = tuple(min(c / scale, 1.0) for c in prim.color[:3]) + (prim.color[3],)
    return replace(
        prim,
        color=color,  # type: ignore[arg-type]
        mesh=replace(mesh, uvs=out.astype(np.float32)),
        textures=texture_paths(uri_root, kind),
    )


SHIFTED = {"plaster", "stone", "roof", "roof_moss", "boards"}  # shifted per house (texture_house)


def texture_house(prims: Sequence[Primitive], uri_root: str, key: str = "") -> list[Primitive]:
    """Textures for one house; ``key`` (its id) picks a fixed offset of the repeating textures."""
    h = hashlib.sha256(key.encode()).digest() if key else bytes(2)
    offset = (h[0] / 256.0, h[1] / 256.0)
    return [textured(p, uri_root, offset) for p in prims]


def write_textures(folder: Path, size: int = 1024, seed: int = 7) -> list[Path]:
    """All texture images (albedo sRGB, normal linear) into ``folder``; unchanged files stay."""
    written = []
    for kind in KINDS:
        tex = make(kind, size, seed)
        for path, img, srgb in ((folder / f"{kind}_albedo.png", tex.albedo, True),
                                (folder / f"{kind}_normal.png", tex.normal(), False)):  # fmt: skip
            to_png(img, path, srgb, albedo_scale(kind))
            written.append(path)
    return written
