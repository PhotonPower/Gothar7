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

Roofs from afar (owner 2026-11-27: "von Weitem eine glatte rotbraune Fläche"): ``texture_house``
cuts every tiled roof into patches along the rows of tiles (bands of ``PATCH_ROWS`` courses up the
slope, ``PATCH_MIN_M``..``PATCH_MAX_M`` long along the eave, staggered from band to band) and gives
each patch one of ``PATCH_SHADES`` (older, darker tiles; newer, brighter ones) as its own material
``<roof>~p1`` / ``~p2``: re-laid patches as on a real roof. The bands follow the courses of the
texture, so a patch ends between two rows. The ridge tiles (``roof_ridge``) stay whole.
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
RIDGE = "roof_ridge"  # the ridge tiles (buildings.medieval.RIDGE): tiles, but no patches
ROOF_ROWS = 13  # courses per texture (textures.procedural.roof)
PATCH_ROWS = 8  # courses per band of patches (1.23 m up the slope)
PATCH_MIN_M, PATCH_MAX_M = 1.6, 5.0  # length of a patch along the eave
PATCH_UP = 0.3  # only faces looking up at least this much (not the eaves' underside, the fascia)
# patch shade -> (colour factor, share): older darker tiles, newer redder and brighter ones
PATCH_SHADES = {"~p1": ((0.84, 0.81, 0.78), 0.26), "~p2": ((1.11, 1.05, 0.98), 0.2)}


def kind_of(material: str) -> str | None:
    """Texture kind of a palette entry (``None``: stays untextured)."""
    for patch in PATCH_SHADES:
        material = material.removesuffix(patch)
    name = material.removesuffix(LOW).removesuffix(MOSS).removesuffix(STREAK)
    if name.startswith(("plaster", "lehm")):
        if material.endswith(STREAK):
            return "plaster_streak"
        return "plaster_low" if material.endswith(LOW) else "plaster"
    if name == "stone_slab":  # the street gutters (W6)
        return "slab"
    if name in ("stone", "brick") or name.startswith("stone_"):
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


def textured(prim: Primitive, uri_root: str, offset: tuple[float, float] = (0.0, 0.0),
             kind: str | None = None) -> Primitive:  # fmt: skip
    """The primitive with texture coordinates for its kind and its two images; ``offset`` (in
    textures) shifts the repeating kinds, so neighbouring houses do not show the same stones.
    ``kind`` overrides the palette's kind (hand-made models, ``textures.models``)."""
    kind = kind or kind_of(prim.material)
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


SHIFTED = {
    "plaster",
    "stone",
    "roof",
    "roof_moss",
    "boards",
    "slab",
    "ashlar",
}  # shifted per house (texture_house)


def texture_house(prims: Sequence[Primitive], uri_root: str, key: str = "") -> list[Primitive]:
    """Textures for one house; ``key`` (its id) picks a fixed offset of the repeating textures;
    tiled roofs come in patches of shades (``roof_patches``)."""
    h = hashlib.sha256(key.encode()).digest() if key else bytes(2)
    offset = (h[0] / 256.0, h[1] / 256.0)
    out = []
    for p in prims:
        if (kind_of(p.material) or "").startswith("roof") and p.material != RIDGE:
            out += [textured(q, uri_root, offset) for q in roof_patches(p, offset, key)]
        else:
            out.append(textured(p, uri_root, offset))
    return out


def _hash01(*parts: object) -> float:
    return int.from_bytes(hashlib.sha256(repr(parts).encode()).digest()[:4], "little") / 2**32


def _clip(poly: list[np.ndarray], axis: int, cut: float, keep_low: bool) -> list[np.ndarray]:
    """Sutherland-Hodgman against ``x[axis] <= cut`` (or ``>=``); rows are vertices with all
    attributes, ``x[0], x[1]`` the roof coordinates (along the eave, up the slope)."""
    out: list[np.ndarray] = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        ina = a[axis] <= cut if keep_low else a[axis] >= cut
        inb = b[axis] <= cut if keep_low else b[axis] >= cut
        if ina:
            out.append(a)
        if ina != inb:
            t = (cut - a[axis]) / (b[axis] - a[axis])
            out.append(a + (b - a) * t)
    return out


def roof_patches(prim: Primitive, offset: tuple[float, float], key: str) -> list[Primitive]:
    """The roof cut into patches along its courses, as up to three primitives (the plain tiles
    and the two ``PATCH_SHADES``); the cut follows the face (u along the eave, v up the slope,
    in metres, as ``_roof_uvs``), so neighbouring faces of one plane share the patches."""
    mesh = prim.mesh
    if mesh.triangle_count == 0:
        return [prim]
    _, tv = TILE_M["roof"]
    course = tv / ROOF_ROWS
    band_m = course * PATCH_ROWS
    v0 = offset[1] * tv  # a course boundary (the texture's v starts at the house's offset)
    uv = _roof_uvs(mesh)
    attrs = np.concatenate([uv, mesh.positions.astype(np.float64),
                            mesh.normals.astype(np.float64), mesh.uvs.astype(np.float64)],
                           axis=1)  # fmt: skip
    groups: dict[str, list[list[np.ndarray]]] = {"": [], **{k: [] for k in PATCH_SHADES}}
    for tri in mesh.indices.reshape(-1, 3):
        pts = [attrs[i] for i in tri]
        if float(np.mean([q[6] for q in pts])) < PATCH_UP:  # normal y: underside, fascia
            groups[""].append(pts)
            continue
        vs = [q[1] for q in pts]
        b_lo = int(np.floor((min(vs) - v0) / band_m))
        b_hi = int(np.floor((max(vs) - v0) / band_m - 1e-9))
        for band in range(b_lo, b_hi + 1):
            lo, hi = v0 + band * band_m, v0 + (band + 1) * band_m
            strip = _clip(_clip(pts, 1, lo, False), 1, hi, True) if pts else []
            if len(strip) < 3:
                continue
            us = [q[0] for q in strip]
            # the band's cuts along the eave: lengths from a hash, a start shifted per band
            start = _hash01(key, band, "start") * PATCH_MAX_M
            u = np.floor((min(us) - start) / PATCH_MAX_M) * PATCH_MAX_M + start - PATCH_MAX_M
            cuts = []
            k = 0
            while u < max(us):
                length = PATCH_MIN_M + _hash01(key, band, round(u, 3), k) * (
                    PATCH_MAX_M - PATCH_MIN_M
                )
                cuts.append((u, u + length))
                u += length
                k += 1
            for c0, c1 in cuts:
                if c1 <= min(us) or c0 >= max(us):
                    continue
                piece = _clip(_clip(strip, 0, c0, False), 0, c1, True)
                if len(piece) < 3:
                    continue
                r = _hash01(key, band, round(c0, 3), "shade")
                shade = ""
                for name, (_, share) in PATCH_SHADES.items():
                    if r < share:
                        shade = name
                        break
                    r -= share
                groups[shade].append(piece)
    out = []
    for shade, polys in groups.items():
        if not polys:
            continue
        rows = [q for poly in polys for q in poly]
        idx = []
        base = 0
        for poly in polys:
            idx += [x for k in range(1, len(poly) - 1) for x in (base, base + k, base + k + 1)]
            base += len(poly)
        arr = np.asarray(rows)
        f32 = np.float32
        sub = MeshData(arr[:, 2:5].astype(f32), arr[:, 5:8].astype(f32),
                       arr[:, 8:10].astype(f32), np.asarray(idx, dtype=np.uint32))  # fmt: skip
        color = prim.color
        if shade:
            f = PATCH_SHADES[shade][0]
            color = (color[0] * f[0], color[1] * f[1], color[2] * f[2], color[3])
        out.append(replace(prim, material=prim.material + shade, color=color, mesh=sub))
    return out


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
