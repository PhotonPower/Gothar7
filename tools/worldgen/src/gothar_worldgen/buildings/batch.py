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
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from shapely.geometry import Polygon

from gothar_worldgen.buildings.collision import (
    BUDGET,
    CollisionResult,
    collision_for,
    merge_collision,
)
from gothar_worldgen.buildings.gaps import Filler, find_fillers, footprint_of, prism_part
from gothar_worldgen.buildings.gltf import CollisionPart, MeshData, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import build_mesh, masses_for_building
from gothar_worldgen.buildings.medieval import Rules, StreetIndex, barn_hearths, build_house
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.geo.ground import ground_range

INDEX_FORMAT = "gothar-buildings-index"
INDEX_VERSION = 1
SINK_M = 0.3  # walls start this far below the lowest ground point
STEP_WARN_M = 0.5  # report buildings where LoD2 and DGM ground differ more than this
CELL_M = 64.0
MASSING_COLOR = (0.6, 0.6, 0.6, 1.0)
COLLISION_BUDGET = BUDGET


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
    replaced: list[tuple[str, int]] = field(default_factory=list)  # over budget -> (id, new houses)
    timber_levels: Counter[int] = field(default_factory=Counter)
    collision: Counter[str] = field(default_factory=Counter)  # hulls, fallbacks, decomposed
    styles: dict[str, Counter[str]] = field(default_factory=lambda: defaultdict(Counter))


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
    replace: Callable[[dict[str, Any]], list[dict[str, Any]]] | None = None,
    wall: Any = None,  # noqa: ANN401  medieval.WallContext: wall houses on the city wall line (W6)
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
    cells: dict[
        tuple[int, int],
        list[tuple[list[Primitive], list[CollisionPart], tuple[float, float, float]]],
    ] = defaultdict(list)
    by_hash: dict[str, str] = {}
    deferred: list[tuple[dict[str, Any], str, list[Primitive], list[CollisionPart],
                         tuple[float, float, float]]] = []  # fmt: skip

    def emit(name: str, prims: list[Primitive], collision: list[CollisionPart]) -> str:
        geometry = hashlib.sha256()
        for prim in prims:
            geometry.update(prim.material.encode())
            for arr in (prim.mesh.positions, prim.mesh.normals, prim.mesh.uvs, prim.mesh.indices):
                geometry.update(arr.tobytes())
        for part in collision:
            geometry.update(part.name.encode())
            geometry.update(part.positions.tobytes())
            geometry.update(part.indices.tobytes())
        digest = geometry.hexdigest()  # the name inside the file does not matter for sharing
        if digest in by_hash:
            result.shared += 1
            return by_hash[digest]
        data = glb_bytes_multi(prims, name, collision)
        path = out_dir / f"{name}.glb"
        if not path.is_file() or path.read_bytes() != data:
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_bytes(data)
            tmp.replace(path)
        result.written += 1
        by_hash[digest] = f"{vfs_dir}/{name}.glb"
        return by_hash[digest]

    hearths: set[str] = set()
    if mode == "medieval":
        assert rules is not None
        wanted = [b for b in buildings if area == "all" or b.get("inCore")]
        hearths = barn_hearths(wanted, overrides, streets, rules)
    queue = list(buildings)
    while queue:
        b = queue.pop(0)
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
            house = build_house(b, base, (c.x, c.y), rules, streets, (overrides or {}).get(bid),
                                hearth=bid in hearths, wall=wall,
                                ground_at=grid.height_at if grid is not None else None)  # fmt: skip
            if house.triangles > budget and replace is not None and "derivedFrom" not in b:
                houses = replace(b)  # rueckbau: smaller half-timbered houses instead
                if houses:
                    result.replaced.append((bid, len(houses)))
                    queue[0:0] = houses
                    continue
            prims = house.primitives
            col = house.collision or CollisionResult([])
            result.notes.update(n for n in house.notes if n not in massing.notes)
            result.timber_levels[house.timber_level] += 1
            if house.style is not None:
                st = house.style
                for key, value in (("style", st.style), ("pattern", st.pattern or "-"),
                                   ("brustung", st.brustung or "-"), ("infill", st.infill),
                                   ("roof", st.roof), ("timberColor", st.timber_color),
                                   ("massiveGround", str(st.massive_ground)),
                                   ("jetty", str(st.jetty))):  # fmt: skip
                    result.styles[key][value] += 1
                result.styles["roofSteepened"]["masses"] += house.steepened
                result.styles["age"][f"{min(int(st.age * 5), 4) / 5:.1f}"] += 1
                result.styles["ridgeSagCm"]["total"] += round(house.sag_m * 100)
                result.styles["dormers"][st.style] += house.dormers
                result.styles["dormerHouses"][st.style] += int(house.dormers > 0)
                result.styles["chimneys"][st.style] += house.chimneys
                result.styles["wallHouse"][str(st.wall_house)] += 1
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
            col = collision_for(massing.masses, base, (c.x, c.y))
        result.collision["hulls"] += sum(p.name.startswith("COL_HULL_") for p in col.parts)
        result.collision["fallbacks"] += int(col.fallback)
        result.collision["decomposed"] += int(col.decomposed > 0)
        result.collision["over"] += int(col.triangles > COLLISION_BUDGET)
        triangles = sum(prim.mesh.triangle_count for prim in prims)
        origin = (round(c.x, 3), round(base, 3), round(c.y, 3))
        if in_core:
            entry = {"id": bid, "kind": "building", "mesh": "", "pos": list(origin),
                     "triangles": triangles, "collisionTriangles": col.triangles,
                     "groundY": lod2_ground}  # fmt: skip
            deferred.append((entry, stem, prims, list(col.parts), origin))
            if dgm:
                entry["dgmMinY"], entry["dgmMaxY"] = round(dgm[0], 3), round(dgm[1], 3)
            if mode == "medieval" and house.doors:  # where the doors are and their floor (E1)
                entry["doors"] = [list(d) for d in house.doors]
            entries.append(entry)
        else:
            cells[(math.floor(c.x / CELL_M), math.floor(c.y / CELL_M))].append(
                (prims, col.parts, origin)
            )

    # E4: slots between the houses get filler bodies, written with the neighbouring house.
    gap_spec = rules.data.get("gapFill") if (mode == "medieval" and rules is not None) else None
    if gap_spec and gap_spec.get("enabled", True) and grid is not None:
        footprints = {}
        for entry, _, _, parts, origin in deferred:
            fp = footprint_of(parts, origin)
            if fp is not None:
                footprints[entry["id"]] = fp
        fillers = find_fillers(footprints, grid.height_at, gap_spec)
        extra: dict[str, list[Filler]] = defaultdict(list)
        for f in fillers:
            extra[f.owner].append(f)
        result.collision["gapFillers"] = len(fillers)
        result.collision["gapHouses"] = len(extra)
    else:
        extra = {}
    for entry, stem, prims, parts, origin in deferred:
        if entry["id"] in extra:
            parts = parts + [prism_part(f.piece, f.y0, f.y1, origin, "COL_HULL_x")
                             for f in extra[entry["id"]]]  # fmt: skip
            parts = [CollisionPart(f"COL_HULL_{k}", q.positions, q.indices)
                     if q.name.startswith("COL_HULL_") else q
                     for k, q in enumerate(parts)]  # fmt: skip
            entry["collisionTriangles"] = sum(q.triangle_count for q in parts)
        entry["mesh"] = emit(stem, prims, parts)

    for (i, j), parts in sorted(cells.items()):
        origin = ((i + 0.5) * CELL_M, min(o[1] for _, _, o in parts), (j + 0.5) * CELL_M)
        merged = _merge_primitives([(p, o) for p, _, o in parts], origin)
        cols = merge_collision([(cp, o) for _, cp, o in parts], origin)
        name = f"cell_{i}_{j}"
        entries.append({"id": name, "kind": "cell", "mesh": emit(name, merged, cols),
                        "pos": [round(v, 3) for v in origin],
                        "triangles": sum(m.mesh.triangle_count for m in merged),
                        "buildings": len(parts)})  # fmt: skip

    col_tris = sorted(
        e["collisionTriangles"] for e in entries if e["kind"] == "building" and "triangles" in e
    )
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
            "collision": {
                "budget": COLLISION_BUDGET,
                "median": col_tris[len(col_tris) // 2] if col_tris else 0,
                "max": col_tris[-1] if col_tris else 0,
                **{
                    k: result.collision[k]
                    for k in ("hulls", "fallbacks", "decomposed", "over", "gapFillers", "gapHouses")
                },  # fmt: skip
            },
        },
    }
    if mode == "medieval":
        result.index["stats"]["budget"] = {
            "trianglesPerBuilding": budget,
            "over": len(result.over_budget),
            "timberLevels": {str(k): v for k, v in sorted(result.timber_levels.items())},
        }
        result.index["stats"]["style"] = {
            k: dict(sorted(v.items(), key=lambda kv: -kv[1]))
            for k, v in sorted(result.styles.items())
        }
    return result


def write_index(path: Path, index: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(index, indent=1) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(path)
