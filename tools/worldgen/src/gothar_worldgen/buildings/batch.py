"""Generate the building meshes of a site and their index (``gothar-worldgen buildings``).

Old town (``inCore``): one ``.glb`` per building, placed by its own mesh vob. Surroundings
(``--area all``): merged into one ``.glb`` per 64 m cell until the engine has distance culling
and batching (engine measurement 2026-10-03: 905 single vobs 3.5 ms, 5400 single vobs 79 ms).
Identical meshes share one file. Buildings marked ``locked`` in their override keep their file.

Modes: ``massing`` (grey blocks, default) and ``medieval`` (rule-based half-timbered houses with
five shared material roles, ``buildings/medieval.py``; rules from ``data/building_rules.json``).

Base height: the lower of LoD2 ``groundY`` and the lowest heightmap sample under the footprint,
sunk a little into the ground (DGM steps along terraced houses, leonberg-pipeline.md W-C).
"""

from __future__ import annotations

import hashlib
import json
import math
from collections import Counter, defaultdict
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from shapely.geometry import Polygon

from gothar_worldgen.buildings.gltf import MeshData, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import build_mesh, masses_for_building
from gothar_worldgen.buildings.medieval import Rules, StreetIndex, build_house
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.geo.ground import ground_range

INDEX_FORMAT = "gothar-buildings-index"
INDEX_VERSION = 1
SINK_M = 0.3  # walls start this far below the lowest ground point
STEP_WARN_M = 0.5  # report buildings where LoD2 and DGM ground differ more than this
CELL_M = 64.0
MASSING_COLOR = (0.6, 0.6, 0.6, 1.0)


def file_stem(building_id: str) -> str:
    """File name for a building id that is unique on case-insensitive file systems.

    LoD2 ids differ in case only (``…ZFS`` / ``…Zfs``); NTFS and the cooker treat those as the
    same file. Lower case plus a short hash of the original id keeps them apart.
    """
    digest = hashlib.sha1(building_id.encode("utf-8")).hexdigest()[:8]
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in building_id.lower())
    return f"{safe}_{digest}"


def _merge_primitives(
    parts: Sequence[tuple[list[Primitive], tuple[float, float, float]]],
    origin: tuple[float, float, float],
) -> list[Primitive]:
    """Per material: concatenate the primitives of several buildings around ``origin``."""
    by_material: dict[str, list[tuple[MeshData, tuple[float, float, float]]]] = defaultdict(list)
    colors: dict[str, tuple[float, float, float, float]] = {}
    for prims, o in parts:
        for prim in prims:
            by_material[prim.material].append((prim.mesh, o))
            colors[prim.material] = prim.color
    return [Primitive(m, colors[m], _merge(meshes, origin)) for m, meshes in by_material.items()]


def _merge(
    meshes: Sequence[tuple[MeshData, tuple[float, float, float]]],
    origin: tuple[float, float, float],
) -> MeshData:
    """Concatenate meshes given with their own origins into one around ``origin``."""
    pos, nrm, uv, idx = [], [], [], []
    base = 0
    for m, (ox, oy, oz) in meshes:
        pos.append(
            m.positions + np.asarray([ox - origin[0], oy - origin[1], oz - origin[2]], np.float32)
        )
        nrm.append(m.normals)
        uv.append(m.uvs)
        idx.append(m.indices + base)
        base += len(m.positions)
    return MeshData(
        np.concatenate(pos), np.concatenate(nrm), np.concatenate(uv), np.concatenate(idx)
    )


@dataclass
class BatchResult:
    index: dict[str, Any]
    notes: Counter[str] = field(default_factory=Counter)
    written: int = 0
    shared: int = 0
    kept_locked: int = 0
    steps: list[tuple[str, float]] = field(
        default_factory=list
    )  # (id, groundY - dgm min) > STEP_WARN_M
    over_budget: list[tuple[str, int]] = field(default_factory=list)  # medieval: (id, triangles)
    timber_levels: Counter[int] = field(default_factory=Counter)


def generate(
    buildings: Sequence[dict[str, Any]],
    grid: Grid | None,
    out_dir: Path,
    vfs_dir: str,
    area: str = "core",
    locked: frozenset[str] = frozenset(),
    mode: str = "massing",
    rules: Rules | None = None,
    streets: StreetIndex | None = None,
    overrides: dict[str, Any] | None = None,
) -> BatchResult:
    """Writes ``<id>.glb`` (old town) and ``cell_<i>_<j>.glb`` (surroundings, area "all")."""
    if area not in ("core", "all"):
        raise ValueError("area must be 'core' or 'all'")
    if mode not in ("massing", "medieval"):
        raise ValueError("mode must be 'massing' or 'medieval'")
    if mode == "medieval" and rules is None:
        raise ValueError("medieval mode needs rules")
    budget = int(rules.get("budget", "trianglesPerBuilding")) if rules else 0
    out_dir.mkdir(parents=True, exist_ok=True)
    result = BatchResult({})
    entries: list[dict[str, Any]] = []
    cells: dict[tuple[int, int], list[tuple[list[Primitive], tuple[float, float, float]]]] = (
        defaultdict(list)
    )
    by_hash: dict[str, str] = {}

    def emit(name: str, prims: list[Primitive]) -> str:
        geometry = hashlib.sha256()
        for prim in prims:
            geometry.update(prim.material.encode())
            for arr in (prim.mesh.positions, prim.mesh.normals, prim.mesh.uvs, prim.mesh.indices):
                geometry.update(arr.tobytes())
        digest = geometry.hexdigest()  # the name inside the file does not matter for sharing
        if digest in by_hash:
            result.shared += 1
            return by_hash[digest]
        data = glb_bytes_multi(prims, name)
        path = out_dir / f"{name}.glb"
        if not path.is_file() or path.read_bytes() != data:
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_bytes(data)
            tmp.replace(path)
        result.written += 1
        by_hash[digest] = f"{vfs_dir}/{name}.glb"
        return by_hash[digest]

    for b in buildings:
        in_core = bool(b.get("inCore"))
        if not in_core and area == "core":
            continue
        footprint = b.get("footprint") or []
        if len(footprint) < 3:
            result.notes["building without footprint skipped"] += 1
            continue
        bid = b["id"]
        stem = file_stem(bid)
        if in_core and bid in locked and (out_dir / f"{stem}.glb").is_file():
            result.kept_locked += 1
            entries.append({"id": bid, "kind": "building", "locked": True,
                            "mesh": f"{vfs_dir}/{stem}.glb"})  # fmt: skip
            continue
        massing = masses_for_building(b)
        result.notes.update(massing.notes)
        grounds = [float(p.get("groundY", b.get("groundY", 0.0))) for p in (b.get("parts") or [b])]
        lod2_ground = min(grounds)
        if "groundMinY" in b and "groundMaxY" in b:  # from import (buildings.json)
            dgm = (float(b["groundMinY"]), float(b["groundMaxY"]))
        else:
            dgm = ground_range(grid, footprint)
        base = min(lod2_ground, dgm[0] if dgm else lod2_ground) - SINK_M
        if dgm and lod2_ground - dgm[0] > STEP_WARN_M:
            result.steps.append((bid, round(lod2_ground - dgm[0], 2)))
        c = Polygon(footprint).centroid
        if mode == "medieval":
            assert rules is not None
            house = build_house(b, base, (c.x, c.y), rules, streets, (overrides or {}).get(bid))
            prims = house.primitives
            result.notes.update(n for n in house.notes if n not in massing.notes)
            result.timber_levels[house.timber_level] += 1
            if house.triangles > budget:
                result.over_budget.append((bid, house.triangles))
            if not prims:
                result.notes["no usable geometry"] += 1
                continue
        else:
            try:
                prims = [
                    Primitive(
                        "massing", MASSING_COLOR, build_mesh(massing.masses, base, (c.x, c.y))
                    )
                ]
            except ValueError:
                result.notes["no usable geometry"] += 1
                continue
        triangles = sum(prim.mesh.triangle_count for prim in prims)
        origin = (round(c.x, 3), round(base, 3), round(c.y, 3))
        if in_core:
            entry = {"id": bid, "kind": "building", "mesh": emit(stem, prims),
                     "pos": list(origin), "triangles": triangles,
                     "groundY": lod2_ground}  # fmt: skip
            if dgm:
                entry["dgmMinY"], entry["dgmMaxY"] = round(dgm[0], 3), round(dgm[1], 3)
            entries.append(entry)
        else:
            cells[(math.floor(c.x / CELL_M), math.floor(c.y / CELL_M))].append((prims, origin))

    for (i, j), parts in sorted(cells.items()):
        origin = ((i + 0.5) * CELL_M, min(o[1] for _, o in parts), (j + 0.5) * CELL_M)
        merged = _merge_primitives(parts, origin)
        name = f"cell_{i}_{j}"
        entries.append({"id": name, "kind": "cell", "mesh": emit(name, merged),
                        "pos": [round(v, 3) for v in origin],
                        "triangles": sum(m.mesh.triangle_count for m in merged),
                        "buildings": len(parts)})  # fmt: skip

    building_tris = sorted(
        e["triangles"] for e in entries if e["kind"] == "building" and "triangles" in e
    )
    # Files of buildings that no longer exist (or moved into a cell) would be stale: remove them.
    referenced = {e["mesh"].rsplit("/", 1)[-1] for e in entries}
    for stale in out_dir.glob("*.glb"):
        if stale.name not in referenced:
            stale.unlink()
            result.notes["stale file removed"] += 1

    result.index = {
        "format": INDEX_FORMAT,
        "version": INDEX_VERSION,
        "area": area,
        "mode": mode,
        "entries": entries,
        "stats": {
            "buildings": sum(1 for e in entries if e["kind"] == "building"),
            "cells": sum(1 for e in entries if e["kind"] == "cell"),
            "triangles": sum(e.get("triangles", 0) for e in entries),
            "fallbacks": dict(sorted(result.notes.items())),
            "groundSteps": len(result.steps),
            "trianglesPerBuilding": {
                "median": building_tris[len(building_tris) // 2] if building_tris else 0,
                "p90": building_tris[int(0.9 * len(building_tris))] if building_tris else 0,
                "max": building_tris[-1] if building_tris else 0,
            },
        },
    }
    if mode == "medieval":
        result.index["stats"]["budget"] = {
            "trianglesPerBuilding": budget,
            "over": len(result.over_budget),
            "timberLevels": {str(k): v for k, v in sorted(result.timber_levels.items())},
        }
    return result


def write_index(path: Path, index: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(index, indent=1) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(path)
