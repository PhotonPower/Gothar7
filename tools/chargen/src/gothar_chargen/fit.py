"""Fit check for assembled figures: seams between parts and across LOD levels.

Figure mesh nodes are named by role (``body``, ``head``, ``hair``, ``beard``) plus the LOD suffix
(characters-pipeline.md §2.2/§6). ``body`` is the base body or the outfit that replaces it.
Where an open border of a closing part (body, head) comes near an open border of another closing
part of the same LOD level (within `search` m), it is a seam: every seam vertex needs a partner
within `gap` m with matching skin weights, so the seam neither shows a gap nor opens when animated.
Other open borders (hems, sleeves, holes hidden under clothes) are free. A closing part with open
borders must be attached to another closing part by at least one seam. Loose parts (hair, beard)
may have free borders. Borders of LOD levels
must stay where they are at lod0 (contract §2.2 point 6).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import MeshData, mesh_data, split_lod
from gothar_chargen.postprocess import material_role

# parts on/inside the head (eyes, mouth, beard): their open borders are not seams
INNER_ROLES = frozenset({"eyes", "eyebrows", "eyelashes", "teeth", "tongue", "beard"})


def _inner(material: str) -> bool:
    return material_role(material) in INNER_ROLES


CLOSING_ROLES = ("body", "head")
LOOSE_ROLES = ("hair", "beard")
ROLES = CLOSING_ROLES + LOOSE_ROLES


@dataclass(frozen=True)
class FitTolerances:
    gap: float = 0.005  # metres between seam vertices
    search: float = 0.04  # metres: borders closer than this to another part form a seam
    weight: float = 0.05  # max. difference of one joint weight across a seam
    lod_seam: float = 1e-4  # metres: border of lodN vs. lod0
    eye_height: float = 1.62  # metres: eye height of the engine capsule (physics.md)
    eye_tolerance: float = 0.025  # women sit ~2 cm lower on the same neck height


@dataclass(frozen=True)
class FitIssue:
    level: str  # "error" | "warning"
    code: str
    message: str


def figure_parts(gltf: Gltf) -> dict[tuple[str, int], int]:
    """(role, lod level) -> mesh node index, for nodes named by role (no suffix = level 0)."""
    parts: dict[tuple[str, int], int] = {}
    for i, node in enumerate(gltf.list("nodes")):
        if "mesh" not in node:
            continue
        base, level = split_lod(str(node.get("name", "")))
        if base in ROLES:
            parts[(base, level or 0)] = i
    return parts


def check_fit(gltf: Gltf, tol: FitTolerances | None = None) -> list[FitIssue]:
    tol = tol or FitTolerances()
    issues: list[FitIssue] = []
    parts = figure_parts(gltf)
    if not parts:
        return issues
    data: dict[tuple[str, int], MeshData] = {
        key: mesh_data(gltf, idx, skip_material=_inner) for key, idx in parts.items()
    }
    levels = sorted({level for _, level in parts})
    if ("head", 0) in parts:
        issues += _check_eyes(gltf, parts[("head", 0)], tol)
    for level in levels:
        closing = [(role, data[(role, level)]) for role in CLOSING_ROLES if (role, level) in data]
        issues += _check_seams(closing, level, tol)
    for role in CLOSING_ROLES:  # loose parts (hair cards, beards) have no seams to keep
        if (role, 0) not in data:
            continue
        others = [d for (r, lv), d in data.items() if r != role and r in CLOSING_ROLES and lv == 0]
        base_border = _seam_border(data[(role, 0)], others, tol)
        for level in levels:
            if level == 0 or (role, level) not in data:
                continue
            others_lv = [
                d for (r, lv), d in data.items() if r != role and r in CLOSING_ROLES and lv == level
            ]
            lod_border = _seam_border(data[(role, level)], others_lv, tol)
            moved = _unmatched(lod_border, base_border, tol.lod_seam) + _unmatched(
                base_border, lod_border, tol.lod_seam
            )
            if moved:
                issues.append(
                    FitIssue(
                        "error",
                        "lod.seam",
                        f"{role}_lod{level}: {moved} border vertices differ from {role}_lod0 "
                        "(keep borders when reducing)",
                    )
                )
    return issues


def _check_eyes(gltf: Gltf, node: int, tol: FitTolerances) -> list[FitIssue]:
    """The eyes (material "eyes" of head_lod0) sit at the engine's eye height."""
    mesh = gltf.doc["meshes"][gltf.doc["nodes"][node]["mesh"]]
    materials = gltf.doc.get("materials", [])
    heights = []
    for prim in mesh["primitives"]:
        mat = prim.get("material")
        name = str(materials[mat].get("name", "")) if mat is not None else ""
        if name.split(".")[0] == "eyes":
            heights.append(float(gltf.accessor(prim["attributes"]["POSITION"])[:, 1].mean()))
    if not heights:
        return []
    eyes = sum(heights) / len(heights)
    if abs(eyes - tol.eye_height) <= tol.eye_tolerance:
        return []
    return [
        FitIssue(
            "error",
            "head.eyes",
            f"eyes at {eyes:.3f} m, expected {tol.eye_height} ± {tol.eye_tolerance} m "
            "(engine capsule; heads are built at the bodies' neck height)",
        )
    ]


def _seam_border(data: MeshData, others: list[MeshData], tol: FitTolerances) -> np.ndarray:
    """Positions of the open-border vertices that form a seam with another closing part (holes
    under garments are no seams; only the seam must stay the same in every LOD)."""
    border = data.positions[data.border_vertices()]
    targets = [o.positions[o.border_vertices()] for o in others]
    targets = [t for t in targets if len(t)]
    if not targets or not len(border):
        return border
    other = np.concatenate(targets)
    dist = np.linalg.norm(border[:, None, :] - other[None, :, :], axis=2).min(axis=1)
    return border[dist <= tol.search]


def _unmatched(a: np.ndarray, b: np.ndarray, tol: float) -> int:
    if len(a) == 0:
        return 0
    if len(b) == 0:
        return len(a)
    dist = np.linalg.norm(a[:, None, :] - b[None, :, :], axis=2).min(axis=1)
    return int(np.count_nonzero(dist > tol))


def _check_seams(
    closing: list[tuple[str, MeshData]], level: int, tol: FitTolerances
) -> list[FitIssue]:
    issues: list[FitIssue] = []
    borders = {role: data.border_vertices() for role, data in closing}
    for role, data in closing:
        own = borders[role]
        if len(own) == 0:
            continue
        others = [(r, d, borders[r]) for r, d in closing if r != role and len(borders[r])]
        if not others:
            issues.append(
                FitIssue(
                    "error", "fit.gap", f"{role}_lod{level}: open border but no part to close it"
                )
            )
            continue
        gaps, weight_diffs, seam = [], [], 0
        for v in own:
            p = data.positions[v]
            best = None
            for _, other, other_border in others:
                d = np.linalg.norm(other.positions[other_border] - p, axis=1)
                k = int(d.argmin())
                if best is None or d[k] < best[0]:
                    best = (float(d[k]), other, int(other_border[k]))
            dist, other, w = best
            if dist > tol.search:
                continue  # free border (hem, sleeve, hidden hole)
            seam += 1
            if dist > tol.gap:
                gaps.append(dist)
                continue
            wa, wb = data.weights_of(int(v)), other.weights_of(w)
            diff = max(
                (abs(wa.get(j, 0.0) - wb.get(j, 0.0)) for j in set(wa) | set(wb)), default=0.0
            )
            if diff > tol.weight:
                weight_diffs.append(diff)
        if seam == 0:
            issues.append(
                FitIssue(
                    "error",
                    "fit.gap",
                    f"{role}_lod{level}: not attached – no open border within "
                    f"{tol.search * 100:.0f} cm of another part",
                )
            )
        if gaps:
            issues.append(
                FitIssue(
                    "error",
                    "fit.gap",
                    f"{role}_lod{level}: {len(gaps)} of {seam} seam vertices have no partner "
                    f"(largest gap {max(gaps) * 1000:.1f} mm, allowed {tol.gap * 1000:.0f} mm)",
                )
            )
        if weight_diffs:
            issues.append(
                FitIssue(
                    "error",
                    "fit.weights",
                    f"{role}_lod{level}: {len(weight_diffs)} seam vertices with different skin "
                    f"weights (up to {max(weight_diffs):.2f}) – the seam opens when animated",
                )
            )
    return issues
