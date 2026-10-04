"""Skin showing through clothes in motion (F3o): pure Python + numpy, so the CI can check it.

The figure is skinned (linear blend skinning, glTF) with clips of the reference rig: bone lengths of
the figure, rotations from the clip, translations only for ``root`` and ``pelvis`` (clip contract
§3). Skin vertices that lie under clothing at rest (a ray along the normal meets a garment from
inside within REST_REACH) keep their nearby garment triangles; in every sampled frame the ray is
cast again against those. A skin triangle whose three vertices were covered at rest and are all
uncovered now is visible skin that should be hidden – it is reported with its area and the bone
it follows. Skin that already sticks out of a garment at rest (coarse clothing) counts as covered
at rest when it sticks out by at most NEAR (the rule of the cover masks, §6.2). Hems and cuffs
are not reported: skin whose nearest garment point lies within BORDER of an open garment edge is
not expected covered.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from gothar_chargen.clipspeed import _quat_to_matrix, _sample, duration
from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import WELD
from gothar_chargen.partdata import POKE_THROUGH, _closest_points
from gothar_chargen.postprocess import material_role

REST_REACH = 0.05  # metres along the normal within which a garment covers the skin at rest
NEAR = POKE_THROUGH  # metres skin may stick out of a garment at rest and still count as covered
BORDER = 0.04  # metres: skin whose nearest garment point is this close to a hem is not expected
MOTION_REACH = 0.08  # metres in motion (cloth lifts off the body)
CANDIDATES = 48  # nearest garment triangles per covered skin vertex
SAMPLES = 8  # frames per clip
# clips that bend the body most: standing, walking, running, sneaking, weapon stance, picking up
CLIPS = (
    "none/s_idle",
    "none/s_walk",
    "none/s_run",
    "none/s_sneak",
    "1h/s_idle",
    "none/t_pickup_ground",
)
ROOT_BONES = ("root", "pelvis")  # the only bones whose clip translation is used
COVER_ROLES = ("cloth", "armor")


@dataclass
class Prim:
    """One primitive of a lod0 node: bind-pose geometry and skin weights."""

    material: str
    positions: np.ndarray
    normals: np.ndarray
    joints: np.ndarray
    weights: np.ndarray
    triangles: np.ndarray


@dataclass
class ClipResult:
    clip: str
    area: float  # cm² of visible skin that should be covered (worst sampled frame)
    triangles: int
    frame: float  # seconds into the clip
    bones: list[str] = field(default_factory=list)  # bones the visible skin follows, most first


def _prims(fig: Gltf) -> list[Prim]:
    out = []
    for node in fig.list("nodes"):
        if "mesh" not in node or not str(node.get("name", "")).endswith("_lod0"):
            continue
        for p in fig.doc["meshes"][node["mesh"]]["primitives"]:
            a = p["attributes"]
            if "JOINTS_0" not in a:
                continue
            material = fig.doc["materials"][p["material"]]["name"] if "material" in p else ""
            out.append(
                Prim(
                    material,
                    np.asarray(fig.accessor(a["POSITION"]), dtype=np.float64),
                    np.asarray(fig.accessor(a["NORMAL"]), dtype=np.float64),
                    np.asarray(fig.accessor(a["JOINTS_0"]), dtype=np.int64),
                    np.asarray(fig.accessor(a["WEIGHTS_0"]), dtype=np.float64),
                    np.asarray(fig.accessor(p["indices"]), dtype=np.int64).reshape(-1, 3),
                )
            )
    return out


def skin_matrices(
    fig: Gltf, anim: Gltf | None, animation: dict | None, t: np.ndarray
) -> np.ndarray:
    """(len(t), joints, 4, 4) skinning matrices of the figure's skin posed by the clip
    (no clip: bind pose)."""
    nodes = fig.doc["nodes"]
    parents = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    channels: dict[tuple[str, str], tuple[np.ndarray, np.ndarray]] = {}
    if anim is not None and animation is not None:
        anim_names = [n.get("name") for n in anim.doc["nodes"]]
        for ch in animation["channels"]:
            sampler = animation["samplers"][ch["sampler"]]
            times = np.asarray(anim.accessor(sampler["input"]), dtype=np.float64).ravel()
            values = np.asarray(anim.accessor(sampler["output"]), dtype=np.float64)
            name = anim_names[ch["target"]["node"]]
            channels[(name, ch["target"]["path"])] = (times, values.reshape(len(times), -1))
    cache: dict[int, np.ndarray] = {}

    def global_of(i: int) -> np.ndarray:
        if i in cache:
            return cache[i]
        node = nodes[i]
        name = node.get("name")
        trans = np.tile(np.asarray(node.get("translation", [0, 0, 0]), float), (len(t), 1))
        rot = np.tile(np.asarray(node.get("rotation", [0, 0, 0, 1]), float), (len(t), 1))
        if (name, "rotation") in channels:
            rot = _sample(*channels[(name, "rotation")], t, rotation=True)
        if name in ROOT_BONES and (name, "translation") in channels:
            trans = _sample(*channels[(name, "translation")], t, rotation=False)
        local = np.zeros((len(t), 4, 4))
        local[:, :3, :3] = _quat_to_matrix(rot) * np.asarray(node.get("scale", [1, 1, 1]), float)
        local[:, :3, 3] = trans
        local[:, 3, 3] = 1.0
        out = global_of(parents[i]) @ local if i in parents else local
        cache[i] = out
        return out

    skin = fig.doc["skins"][0]
    ibm = np.asarray(fig.accessor(skin["inverseBindMatrices"]), dtype=np.float64).reshape(-1, 4, 4)
    return np.stack([global_of(j) @ ibm[k] for k, j in enumerate(skin["joints"])], axis=1)


def _skin(prim: Prim, mats: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Posed positions and normals (V, 3) for one frame's skinning matrices (J, 4, 4)."""
    m = np.einsum("vk,vkij->vij", prim.weights, mats[prim.joints])
    pos = np.einsum("vij,vj->vi", m[:, :3, :3], prim.positions) + m[:, :3, 3]
    nrm = np.einsum("vij,vj->vi", m[:, :3, :3], prim.normals)
    return pos, nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)


def _hits(origins: np.ndarray, dirs: np.ndarray, tri: np.ndarray, reach: float) -> np.ndarray:
    """origins/dirs (N, 3), tri (N, K, 3, 3): ray along dir meets a triangle facing along the
    ray (seen from inside) within reach."""
    a, b, c = tri[:, :, 0], tri[:, :, 1], tri[:, :, 2]
    e1, e2 = b - a, c - a
    d = np.broadcast_to(dirs[:, None, :], e1.shape)
    p = np.cross(d, e2)
    det = np.einsum("nkj,nkj->nk", p, e1)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    tvec = origins[:, None, :] - a
    u = np.einsum("nkj,nkj->nk", tvec, p) * inv
    q = np.cross(tvec, e1)
    v = np.einsum("nkj,nkj->nk", d, q) * inv
    dist = np.einsum("nkj,nkj->nk", e2, q) * inv
    facing = np.einsum("nkj,nkj->nk", d, np.cross(e1, e2)) > 0
    good = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (dist > -0.005) & (dist <= reach) & facing
    return good.any(axis=1)


class PokeCheck:
    """Covered skin of one figure and its candidate garment triangles (computed at rest)."""

    def __init__(self, fig: Gltf) -> None:
        self.fig = fig
        prims = _prims(fig)
        self.skin = [p for p in prims if material_role(p.material) == "skin"]
        self.cloth = [p for p in prims if material_role(p.material) in COVER_ROLES]
        joint_nodes = fig.doc["skins"][0]["joints"]
        self.joint_names = [fig.doc["nodes"][j].get("name", "") for j in joint_nodes]
        # all garment triangles as (prim, vertex) references
        self.cloth_tris = (
            np.concatenate(
                [
                    np.stack([np.full(len(p.triangles), i), *p.triangles.T], axis=1)
                    for i, p in enumerate(self.cloth)
                ]  # fmt: skip
            )
            if self.cloth
            else np.zeros((0, 4), dtype=np.int64)
        )
        rest = skin_matrices(fig, None, None, np.zeros(1))[0]
        cloth_pos = [_skin(p, rest)[0] for p in self.cloth]
        tri_rest = self._tri_positions(cloth_pos)
        border = self._border_points(cloth_pos)
        self.covered: list[np.ndarray] = []  # per skin prim: covered vertex mask
        self.candidates: list[np.ndarray] = []  # per skin prim: (V_covered, K) garment triangles
        self.deep: list[np.ndarray] = []  # per skin prim: triangles fully covered at rest
        for prim in self.skin:
            pos, nrm = _skin(prim, rest)
            used = np.zeros(len(pos), dtype=bool)
            used[prim.triangles.ravel()] = True
            mask = np.zeros(len(pos), dtype=bool)
            cand = np.zeros((len(pos), CANDIDATES), dtype=np.int64)
            if len(tri_rest):
                centres = tri_rest.mean(axis=1)
                k = min(CANDIDATES, len(centres))
                for s in range(0, len(pos), 512):
                    d = np.linalg.norm(pos[s : s + 512, None] - centres[None], axis=2)
                    near = np.argpartition(d, k - 1, axis=1)[:, :k]
                    cand[s : s + 512, :k] = near
                    if k < CANDIDATES:
                        cand[s : s + 512, k:] = near[:, :1]
                under = _hits(pos + nrm * 1e-4, nrm, tri_rest[cand], REST_REACH)
                near = np.zeros(len(pos), dtype=bool)
                interior = np.zeros(len(pos), dtype=bool)
                idx = np.flatnonzero(used)
                closest = _closest_points(pos[idx], tri_rest)
                offset = closest - pos[idx]
                near[idx] = (np.linalg.norm(offset, axis=1) < NEAR) & (
                    np.einsum("ij,ij->i", offset, nrm[idx]) < 0
                )
                if len(border):
                    gap = np.full(len(idx), np.inf)
                    for s in range(0, len(idx), 512):
                        d = np.linalg.norm(closest[s : s + 512, None] - border[None], axis=2)
                        gap[s : s + 512] = d.min(axis=1)
                    interior[idx] = gap > BORDER
                else:
                    interior[idx] = True
                mask = used & (under | near) & interior
            self.covered.append(mask)
            self.candidates.append(cand)
            self.deep.append(mask[prim.triangles].all(axis=1))

    def _border_points(self, cloth_pos: list[np.ndarray]) -> np.ndarray:
        """Positions of the garments' open edges (hems, cuffs, collars), welded across UV seams."""
        points = []
        for prim, pos in zip(self.cloth, cloth_pos, strict=True):
            _, weld = np.unique(np.round(pos / WELD).astype(np.int64), axis=0, return_inverse=True)
            weld = weld.ravel()
            tris = weld[prim.triangles]
            edges = np.sort(
                np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]]), axis=1
            )
            uniq, counts = np.unique(edges, axis=0, return_counts=True)
            open_edges = uniq[counts == 1]
            if len(open_edges):
                first = np.zeros(weld.max() + 1, dtype=np.int64)
                first[weld] = np.arange(len(weld))
                points.append(pos[first[np.unique(open_edges)]])
        return np.concatenate(points) if points else np.zeros((0, 3))

    def _tri_positions(self, cloth_pos: list[np.ndarray]) -> np.ndarray:
        if not len(self.cloth_tris):
            return np.zeros((0, 3, 3))
        out = np.empty((len(self.cloth_tris), 3, 3))
        for i, pos in enumerate(cloth_pos):
            sel = self.cloth_tris[:, 0] == i
            out[sel] = pos[self.cloth_tris[sel, 1:]]
        return out

    def frame(self, mats: np.ndarray) -> tuple[float, int, dict[str, float]]:
        """Visible skin that should be covered in one pose: area m², triangles, area per bone."""
        cloth_pos = [_skin(p, mats)[0] for p in self.cloth]
        tri = self._tri_positions(cloth_pos)
        area_total, count, per_bone = 0.0, 0, {}
        for prim, mask, cand, deep in zip(
            self.skin, self.covered, self.candidates, self.deep, strict=True
        ):
            if not deep.any():
                continue
            pos, nrm = _skin(prim, mats)
            idx = np.flatnonzero(mask)
            ok = np.zeros(len(pos), dtype=bool)
            ok[idx] = _hits(pos[idx] + nrm[idx] * 1e-4, nrm[idx], tri[cand[idx]], MOTION_REACH)
            shown = deep & ~ok[prim.triangles].any(axis=1)
            if not shown.any():
                continue
            t = prim.triangles[shown]
            area = 0.5 * np.linalg.norm(
                np.cross(pos[t[:, 1]] - pos[t[:, 0]], pos[t[:, 2]] - pos[t[:, 0]]), axis=1
            )
            area_total += float(area.sum())
            count += int(shown.sum())
            heaviest = prim.joints[t[:, 0], prim.weights[t[:, 0]].argmax(axis=1)]
            for j, a in zip(heaviest, area, strict=True):
                name = self.joint_names[j]
                per_bone[name] = per_bone.get(name, 0.0) + float(a)
        return area_total, count, per_bone

    def clip(self, anim: Gltf, animation: dict, samples: int = SAMPLES) -> ClipResult:
        length = duration(anim, animation)
        t = np.linspace(0.0, length, samples) if length > 0 else np.zeros(1)
        mats = skin_matrices(self.fig, anim, animation, t)
        worst = ClipResult(animation.get("name", ""), 0.0, 0, 0.0)
        for i in range(len(t)):
            area, count, per_bone = self.frame(mats[i])
            if area * 1e4 > worst.area:
                bones = sorted(per_bone, key=per_bone.__getitem__, reverse=True)[:3]
                worst = ClipResult(worst.clip, area * 1e4, count, float(t[i]), bones)
        return worst


def check_figure(
    figure: Path, anims_dir: Path, clips: tuple[str, ...] = CLIPS, samples: int = SAMPLES
) -> list[ClipResult]:
    """Worst frame per clip of a built figure; clips are looked up in anims_dir/<set>.glb."""
    check = PokeCheck(Gltf.load(figure))
    sets: dict[str, Gltf] = {}
    results = []
    for clip in clips:
        name = clip.split("/")[0]
        if name not in sets:
            sets[name] = Gltf.load(anims_dir / f"{name}.glb")
        anim = sets[name]
        animation = next((a for a in anim.list("animations") if a.get("name") == clip), None)
        if animation is None:
            raise KeyError(f"clip {clip} not in {name}.glb")
        results.append(check.clip(anim, animation, samples))
    return results
