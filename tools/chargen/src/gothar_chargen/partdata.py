"""Assembly data stored in the parts (contract characters-pipeline.md §6.2): pure Python + numpy.

Every part .glb carries ``asset.extras.gothar`` so the figure can be assembled without Blender –
by `gothar-chargen assemble` today and by the engine at run time later (armour/head swaps, mods):

* body and head parts: ``neck`` – per LOD node the neck ring (open border of the skin) in loop
  order, one entry per ring point listing its glTF vertices ``[primitive, vertex]`` (glTF splits
  a point at UV seams). The ring starts at the front-most point (largest +Z) and runs towards +X.
  The body adds ``falloff``: vertices up to 5 cm from the ring, ``[primitive, vertex, ring point,
  weight]``; when the body ring is moved onto the head ring they follow with that weight.
* garment parts (clothing kit): ``covers`` – the body part they were fitted to, a hash of its
  geometry and per body LOD node the triangles the garment hides, as ``[primitive, first, end)``
  ranges. Hidden: a ray along the vertex normal hits the garment within 3 cm from inside, or
  the body pokes out of the garment by up to 1.5 cm; all three vertices hidden -> triangle hidden.
  Holes in ragged garments, folds seen from behind and a 5 cm band at the neck seam keep the body.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import WELD, split_lod
from gothar_chargen.postprocess import material_role

FORMAT_VERSION = 1
NECK_FALLOFF = 0.05  # metres below the ring that follow the neck snap
NECK_KEEP = 0.05  # metres around the neck ring where garments never hide the body
COVER_DISTANCE = 0.03  # metres along the normal within which a garment covers the body
POKE_THROUGH = 0.015  # metres a body vertex may stick out of a garment and still be hidden
_CHUNK = 512  # body vertices per vectorised batch


class PartDataError(Exception):
    """A part cannot provide the assembly data."""


@dataclass
class LodMesh:
    """One mesh node of a part: per primitive positions, normals, triangles, material names."""

    node: str
    positions: list[np.ndarray]
    normals: list[np.ndarray]
    triangles: list[np.ndarray]
    materials: list[str]

    def all_positions(self) -> np.ndarray:
        return np.concatenate(self.positions) if self.positions else np.zeros((0, 3))


def lod_meshes(gltf: Gltf) -> dict[int, LodMesh]:
    """LOD level -> mesh node data (part files: one mesh node per level)."""
    nodes, meshes, materials = gltf.list("nodes"), gltf.list("meshes"), gltf.list("materials")
    out: dict[int, LodMesh] = {}
    for node in nodes:
        if "mesh" not in node:
            continue
        _, level = split_lod(str(node.get("name", "")))
        lm = LodMesh(str(node.get("name", "")), [], [], [], [])
        for prim in meshes[node["mesh"]].get("primitives", []):
            attrs = prim.get("attributes", {})
            pos = gltf.accessor(attrs["POSITION"]).astype(np.float64)
            nrm = (
                gltf.accessor(attrs["NORMAL"]).astype(np.float64)
                if "NORMAL" in attrs
                else np.zeros_like(pos)
            )
            idx = (
                gltf.accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
                if "indices" in prim
                else np.arange(len(pos), dtype=np.int64).reshape(-1, 3)
            )
            lm.positions.append(pos)
            lm.normals.append(nrm)
            lm.triangles.append(idx)
            mat = prim.get("material")
            lm.materials.append(str(materials[mat].get("name", "")) if mat is not None else "")
        out[0 if level is None else level] = lm
    return out


# --- neck ring -----------------------------------------------------------------------------------


def _weld(positions: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    keys = np.round(positions / WELD).astype(np.int64)
    _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    return inverse.reshape(-1), first


def neck_ring(mesh: LodMesh, lowest: bool) -> list[list[list[int]]]:
    """The skin's open border loop at the neck, canonical order (see module docstring).

    `lowest`: the head's neck is its lowest skin border, the body's its highest one.
    """
    skin = [i for i, m in enumerate(mesh.materials) if material_role(m) == "skin"]
    if not skin:
        raise PartDataError(f"{mesh.node}: no skin primitive")
    offsets = np.cumsum([0] + [len(p) for p in mesh.positions])
    pos = mesh.all_positions()
    weld, first = _weld(pos)
    tris = np.concatenate([mesh.triangles[i] + offsets[i] for i in skin])
    w = weld[tris]
    edges = np.sort(np.concatenate([w[:, [0, 1]], w[:, [1, 2]], w[:, [2, 0]]]), axis=1)
    unique, counts = np.unique(edges, axis=0, return_counts=True)
    border = unique[counts == 1]
    adjacency: dict[int, list[int]] = {}
    for a, b in border:
        adjacency.setdefault(int(a), []).append(int(b))
        adjacency.setdefault(int(b), []).append(int(a))
    loops: list[list[int]] = []
    seen: set[int] = set()
    for start in sorted(adjacency):
        if start in seen:
            continue
        loop, prev, cur = [start], -1, start
        seen.add(start)
        while True:
            nxt = [n for n in adjacency[cur] if n != prev]
            if not nxt or nxt[0] == start:
                break
            prev, cur = cur, nxt[0]
            if cur in seen:
                break
            loop.append(cur)
            seen.add(cur)
        loops.append(loop)
    if not loops:
        raise PartDataError(f"{mesh.node}: skin has no open border (no neck)")
    height = [float(pos[first[loop], 1].mean()) for loop in loops]
    loop = loops[int(np.argmin(height) if lowest else np.argmax(height))]
    pts = pos[first[loop]]
    # canonical start: front-most point (max z; ties: smallest |x|, then larger x)
    order = sorted(
        range(len(loop)), key=lambda k: (-round(pts[k, 2], 5), abs(pts[k, 0]), -pts[k, 0])
    )
    s = order[0]
    loop = loop[s:] + loop[:s]
    pts = pos[first[loop]]
    if len(loop) > 1 and pts[1, 0] < pts[-1, 0]:  # run towards +X
        loop = [loop[0]] + loop[1:][::-1]
    members: dict[int, list[list[int]]] = {}
    for g in np.flatnonzero(np.isin(weld, loop)):
        prim = int(np.searchsorted(offsets, g, side="right") - 1)
        members.setdefault(int(weld[g]), []).append([prim, int(g - offsets[prim])])
    return [sorted(members[w_id]) for w_id in loop]


def neck_falloff(mesh: LodMesh, ring: list[list[list[int]]]) -> list[list[float]]:
    """Body vertices near the ring: [primitive, vertex, ring point, weight], 1 at the ring."""
    ring_pos = np.array([mesh.positions[p[0][0]][p[0][1]] for p in ring])
    on_ring = {(p, v) for point in ring for p, v in point}
    out: list[list[float]] = []
    for prim, pos in enumerate(mesh.positions):
        d = np.linalg.norm(pos[:, None, :] - ring_pos[None, :, :], axis=2)
        k = d.argmin(axis=1)
        dist = d[np.arange(len(pos)), k]
        for v in np.flatnonzero(dist < NECK_FALLOFF):
            if (prim, int(v)) in on_ring:
                continue
            out.append([prim, int(v), int(k[v]), round(float(1.0 - dist[v] / NECK_FALLOFF), 4)])
    return out


# --- garment masks -----------------------------------------------------------------------------


def _triangles_of(mesh: LodMesh) -> np.ndarray:
    return np.concatenate([p[t] for p, t in zip(mesh.positions, mesh.triangles, strict=True)])


def _ray_hits(origins: np.ndarray, dirs: np.ndarray, tri: np.ndarray, max_t: float) -> np.ndarray:
    """Per ray: hit within max_t on a triangle facing along the ray (seen from inside)."""
    a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
    e1, e2 = b - a, c - a
    normal = np.cross(e1, e2)
    hit = np.zeros(len(origins), dtype=bool)
    for s in range(0, len(origins), _CHUNK):
        o, d = origins[s : s + _CHUNK, None, :], dirs[s : s + _CHUNK, None, :]
        p = np.cross(d, e2[None])
        det = np.einsum("ijk,jk->ij", p, e1)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tvec = o - a[None]
        u = np.einsum("ijk,ijk->ij", tvec, p) * inv
        q = np.cross(tvec, e1[None])
        v = np.einsum("ijk,ijk->ij", d, q) * inv
        t = np.einsum("jk,ijk->ij", e2, q) * inv
        facing = np.einsum("ijk,jk->ij", np.broadcast_to(d, p.shape), normal) > 0
        good = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0) & (t <= max_t) & facing
        hit[s : s + _CHUNK] = good.any(axis=1)
    return hit


def _closest_points(points: np.ndarray, tri: np.ndarray) -> np.ndarray:
    """Closest point on the triangle set for each point (Ericson, vectorised over triangles)."""
    out = np.empty_like(points)
    a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
    ab, ac = b - a, c - a
    for s in range(0, len(points), _CHUNK):
        p = points[s : s + _CHUNK, None, :]
        ap, bp, cp = p - a, p - b, p - c
        d1, d2 = np.einsum("ijk,jk->ij", ap, ab), np.einsum("ijk,jk->ij", ap, ac)
        d3, d4 = np.einsum("ijk,jk->ij", bp, ab), np.einsum("ijk,jk->ij", bp, ac)
        d5, d6 = np.einsum("ijk,jk->ij", cp, ab), np.einsum("ijk,jk->ij", cp, ac)
        va = d3 * d6 - d5 * d4
        vb = d5 * d2 - d1 * d6
        vc = d1 * d4 - d3 * d2
        denom = va + vb + vc
        denom = np.where(np.abs(denom) < 1e-18, 1e-18, denom)
        v = vb / denom
        w = vc / denom
        res = a + ab * v[..., None] + ac * w[..., None]  # inside the face
        with np.errstate(divide="ignore", invalid="ignore"):
            # edge regions
            v_ab = d1 / (d1 - d3)
            m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
            res = np.where(m[..., None], a + ab * v_ab[..., None], res)
            w_ac = d2 / (d2 - d6)
            m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
            res = np.where(m[..., None], a + ac * w_ac[..., None], res)
            w_bc = (d4 - d3) / ((d4 - d3) + (d5 - d6))
            m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
            res = np.where(m[..., None], b + (c - b) * w_bc[..., None], res)
        # vertex regions
        res = np.where(((d1 <= 0) & (d2 <= 0))[..., None], a, res)
        res = np.where(((d3 >= 0) & (d4 <= d3))[..., None], b, res)
        res = np.where(((d6 >= 0) & (d5 <= d6))[..., None], c, res)
        dist = np.linalg.norm(res - p, axis=2)
        out[s : s + _CHUNK] = res[np.arange(len(dist)), dist.argmin(axis=1)]
    return out


def covered_triangles(
    body: LodMesh, garment: LodMesh, ring: list[list[list[int]]]
) -> list[list[int]]:
    """Triangle ranges [primitive, first, end) of `body` hidden under `garment`."""
    tri = _triangles_of(garment)
    ring_pos = np.array([body.positions[p[0][0]][p[0][1]] for p in ring]) if ring else None
    ranges: list[list[int]] = []
    for prim, (pos, nrm, tris) in enumerate(
        zip(body.positions, body.normals, body.triangles, strict=True)
    ):
        n = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        ray = _ray_hits(pos + n * 1e-4, n, tri, COVER_DISTANCE)
        near = _closest_points(pos, tri)
        offset = near - pos
        poke = (np.linalg.norm(offset, axis=1) < POKE_THROUGH) & (
            np.einsum("ij,ij->i", offset, n) < 0
        )
        covered = ray | poke
        if ring_pos is not None:
            d = np.linalg.norm(pos[:, None, :] - ring_pos[None], axis=2).min(axis=1)
            covered &= d >= NECK_KEEP
        hidden = covered[tris].all(axis=1)
        ranges += _ranges(prim, hidden)
    return ranges


def _ranges(prim: int, flags: np.ndarray) -> list[list[int]]:
    out: list[list[int]] = []
    start = None
    for i, f in enumerate(list(flags) + [False]):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append([prim, start, i])
            start = None
    return out


# --- reading / writing ---------------------------------------------------------------------------


def geometry_hash(gltf: Gltf) -> str:
    """Hash of a part's geometry (binary chunk): masks of a garment name the body they fit."""
    return hashlib.sha256(gltf.bin).hexdigest()[:16]


def data_of(gltf: Gltf) -> dict[str, Any]:
    return gltf.doc.get("asset", {}).get("extras", {}).get("gothar", {})


def set_data(gltf: Gltf, data: dict[str, Any]) -> None:
    asset = gltf.doc.setdefault("asset", {"version": "2.0"})
    asset.setdefault("extras", {})["gothar"] = data


def body_or_head_data(gltf: Gltf, role: str) -> dict[str, Any]:
    """`neck` (and for the body `falloff`) per LOD node."""
    neck: dict[str, Any] = {}
    falloff: dict[str, Any] = {}
    for _level, mesh in sorted(lod_meshes(gltf).items()):
        ring = neck_ring(mesh, lowest=role == "head")
        neck[mesh.node] = ring
        if role == "body":
            falloff[mesh.node] = neck_falloff(mesh, ring)
    data: dict[str, Any] = {"version": FORMAT_VERSION, "part": role, "neck": neck}
    if role == "body":
        data["falloff"] = falloff
    return data


def garment_data(garment: Gltf, body: Gltf, body_ref: str) -> dict[str, Any]:
    """`covers` of a garment part on the body part it was fitted to (`body_ref`: its path)."""
    garment_lod0 = lod_meshes(garment)[0]
    body_data = data_of(body)
    lods: dict[str, Any] = {}
    for _level, mesh in sorted(lod_meshes(body).items()):
        ring = body_data.get("neck", {}).get(mesh.node, [])
        lods[mesh.node] = covered_triangles(mesh, garment_lod0, ring)
    return {
        "version": FORMAT_VERSION,
        "part": "cloth",
        "covers": {"body": body_ref, "body_hash": geometry_hash(body), "lods": lods},
    }


def write_part(path: Path, gltf: Gltf, data: dict[str, Any]) -> None:
    set_data(gltf, data)
    path.write_bytes(gltf.to_bytes())
