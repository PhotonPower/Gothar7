"""Fit check for assembled figures: seams between parts and across LOD levels.

Figure mesh nodes are named by role (``body``, ``head``, ``hair``, ``beard``) plus the LOD suffix
(characters-pipeline.md §2.2/§6). ``body`` is the base body or the outfit that replaces it.
Every open border of a closing part (body, head) must meet an open border of another closing part
of the same LOD level (gap <= `gap` m) with matching skin weights, so the seam neither shows a gap
nor opens when animated. Loose parts (hair, beard) may have free borders. Borders of LOD levels
must stay where they are at lod0 (contract §2.2 point 6).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import MeshData, mesh_data, split_lod

CLOSING_ROLES = ("body", "head")
LOOSE_ROLES = ("hair", "beard")
ROLES = CLOSING_ROLES + LOOSE_ROLES


@dataclass(frozen=True)
class FitTolerances:
    gap: float = 0.005  # metres between seam vertices
    weight: float = 0.05  # max. difference of one joint weight across a seam
    lod_seam: float = 1e-4  # metres: border of lodN vs. lod0


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
        key: mesh_data(gltf, idx) for key, idx in parts.items()
    }
    levels = sorted({level for _, level in parts})
    for level in levels:
        closing = [(role, data[(role, level)]) for role in CLOSING_ROLES if (role, level) in data]
        issues += _check_seams(closing, level, tol)
    for role in ROLES:
        if (role, 0) not in data:
            continue
        base = data[(role, 0)]
        base_border = base.positions[base.border_vertices()]
        for level in levels:
            if level == 0 or (role, level) not in data:
                continue
            lod = data[(role, level)]
            lod_border = lod.positions[lod.border_vertices()]
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
        gaps, weight_diffs = [], []
        for v in own:
            p = data.positions[v]
            best = None
            for _, other, other_border in others:
                d = np.linalg.norm(other.positions[other_border] - p, axis=1)
                k = int(d.argmin())
                if best is None or d[k] < best[0]:
                    best = (float(d[k]), other, int(other_border[k]))
            dist, other, w = best
            if dist > tol.gap:
                gaps.append(dist)
                continue
            wa, wb = data.weights_of(int(v)), other.weights_of(w)
            diff = max(
                (abs(wa.get(j, 0.0) - wb.get(j, 0.0)) for j in set(wa) | set(wb)), default=0.0
            )
            if diff > tol.weight:
                weight_diffs.append(diff)
        if gaps:
            issues.append(
                FitIssue(
                    "error",
                    "fit.gap",
                    f"{role}_lod{level}: {len(gaps)} of {len(own)} border vertices have no partner "
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
