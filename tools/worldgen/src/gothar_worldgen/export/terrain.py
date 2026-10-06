"""Heightmap -> ``.g7world`` with a ``terrain`` block (contract: docs/modules/world.md, "Gelände").

The work data (``terrain.json`` + ``terrain.r16`` from ``import``) is cropped to an area and/or
thinned to every n-th sample, re-quantized over the new height range, and written as a raw
``.r16`` next to a small ``.g7world`` that references it by VFS path. The ``.r16`` is generated
source data (git-ignored ``generated/`` folder), the ``.g7world`` is versioned.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from gothar_worldgen.export.starts import StartPoint, add_start_points
from gothar_worldgen.geo.terrain import U16_MAX, quantize_heights

WORLD_FILE_VERSION = 1
TERRAIN_BLOCK_VERSION = 1


class ExportError(Exception):
    """Missing or inconsistent input; the message is meant for the user."""


@dataclass(frozen=True)
class Grid:
    """Heights (local y) on a regular grid; row 0 = north (-Z), column 0 = west (-X)."""

    heights: npt.NDArray[np.float64]  # (rows, cols)
    first_x: float  # centre of column 0
    first_z: float  # centre of row 0
    cell: float

    @property
    def width(self) -> int:
        return int(self.heights.shape[1])

    @property
    def height(self) -> int:
        return int(self.heights.shape[0])

    def height_at(self, x: float, z: float) -> float:
        """Bilinear height at local (x, z); the edge value outside (as the engine does)."""
        fc = min(max((x - self.first_x) / self.cell, 0.0), self.width - 1.0)
        fr = min(max((z - self.first_z) / self.cell, 0.0), self.height - 1.0)
        c0, r0 = min(int(fc), self.width - 2), min(int(fr), self.height - 2)
        tc, tr = fc - c0, fr - r0
        h = self.heights
        top = h[r0, c0] * (1 - tc) + h[r0, c0 + 1] * tc
        bottom = h[r0 + 1, c0] * (1 - tc) + h[r0 + 1, c0 + 1] * tc
        return float(top * (1 - tr) + bottom * tr)


def load_grid(work_dir: Path) -> Grid:
    """Decoded heights of ``terrain.json`` + ``terrain.r16``."""
    meta_path = work_dir / "terrain.json"
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ExportError(f"{meta_path} not found (run 'gothar-worldgen import' first)") from None
    except json.JSONDecodeError as e:
        raise ExportError(f"{meta_path}: invalid JSON ({e})") from None
    raw_path = work_dir / "terrain.r16"
    if not raw_path.is_file():
        raise ExportError(f"{raw_path} not found (run 'gothar-worldgen import' first)")
    w, h = int(meta["width"]), int(meta["height"])
    raw = np.fromfile(raw_path, dtype="<u2")
    if raw.size != w * h:
        raise ExportError(f"{raw_path.name}: {raw.size} samples, expected {w} x {h}")
    hr = meta["heightRange"]
    heights = hr["minY"] + raw.reshape(h, w).astype(np.float64) / U16_MAX * (
        hr["maxY"] - hr["minY"]
    )
    first = meta["firstSample"]
    return Grid(heights, float(first["x"]), float(first["z"]), float(meta["cellSize"]))


def crop(grid: Grid, rect: dict[str, float] | None = None, step: int = 1) -> Grid:
    """Samples whose centres lie inside ``rect`` (minX/minZ/maxX/maxZ), every ``step``-th one."""
    if step < 1:
        raise ExportError("step must be >= 1")
    cols = np.arange(grid.width)
    rows = np.arange(grid.height)
    if rect is not None:
        xs = grid.first_x + cols * grid.cell
        zs = grid.first_z + rows * grid.cell
        eps = 1e-6
        cols = cols[(xs >= rect["minX"] - eps) & (xs <= rect["maxX"] + eps)]
        rows = rows[(zs >= rect["minZ"] - eps) & (zs <= rect["maxZ"] + eps)]
    cols = cols[::step]
    rows = rows[::step]
    if len(cols) < 2 or len(rows) < 2:
        raise ExportError("the area contains fewer than 2 x 2 samples")
    return Grid(
        grid.heights[np.ix_(rows, cols)],
        grid.first_x + cols[0] * grid.cell,
        grid.first_z + rows[0] * grid.cell,
        grid.cell * step,
    )


def _tidy(v: float) -> float:
    """Numbers as the engine writes them (1e-5), without "-0.0"."""
    return round(float(v), 5) + 0.0


def terrain_block(grid: Grid, heightmap_vfs: str, min_y: float, max_y: float) -> dict[str, Any]:
    """The ``terrain`` block in the engine's key order."""
    return {
        "version": TERRAIN_BLOCK_VERSION,
        "heightmap": heightmap_vfs,
        "width": grid.width,
        "height": grid.height,
        "cellSize": _tidy(grid.cell),
        "firstSample": [_tidy(grid.first_x), _tidy(grid.first_z)],
        "minY": _tidy(min_y),
        "maxY": _tidy(max_y),
    }


def check_terrain_block(block: dict[str, Any]) -> list[str]:
    """Contract rules of world.md; returns problems (empty = valid)."""
    problems = []
    if block.get("version") != TERRAIN_BLOCK_VERSION:
        problems.append("terrain.version must be 1")
    if not isinstance(block.get("heightmap"), str) or not block["heightmap"].endswith(".r16"):
        problems.append("terrain.heightmap must be a VFS path to a .r16 file")
    for key in ("width", "height"):
        if not isinstance(block.get(key), int) or block[key] < 2:
            problems.append(f"terrain.{key} must be an integer >= 2")
    if not (isinstance(block.get("cellSize"), int | float) and block["cellSize"] > 0):
        problems.append("terrain.cellSize must be > 0")
    first = block.get("firstSample")
    if not (isinstance(first, list) and len(first) == 2 and all(math.isfinite(v) for v in first)):
        problems.append("terrain.firstSample must be [x, z]")
    if not (block.get("maxY", 0) > block.get("minY", 0)):
        problems.append("terrain.maxY must be greater than terrain.minY")
    if "splat" in block:
        problems.extend(check_splat_block(block["splat"]))
    return problems


def check_splat_block(splat: Any) -> list[str]:  # noqa: ANN401
    """Rules of the ``splat`` block (world.md, "Splat-Schichten")."""
    if not isinstance(splat, dict):
        return ["terrain.splat must be an object"]
    problems = []
    layers = splat.get("layers")
    if not isinstance(layers, list) or not 1 <= len(layers) <= 8:
        return ["terrain.splat.layers must hold 1 to 8 layers"]
    for i, layer in enumerate(layers):
        if not isinstance(layer, dict) or not layer.get("name") or not layer.get("albedo"):
            problems.append(f"terrain.splat.layers[{i}] needs name and albedo")
        elif not (isinstance(layer.get("tile", 4.0), int | float) and layer.get("tile", 4.0) > 0):
            problems.append(f"terrain.splat.layers[{i}].tile must be > 0")
    maps = splat.get("maps")
    if not isinstance(maps, list) or len(maps) != math.ceil(len(layers) / 4):
        problems.append("terrain.splat.maps must hold ceil(layers / 4) images")
    return problems


def world_text(doc: dict[str, Any]) -> str:
    """Layout of the engine's writer: header keys one per line, one vob per line."""
    lines = ["{"]
    for key, value in doc.items():
        if key in ("vobs", "waynet", "zones"):
            continue
        lines.append(f'  "{key}": {json.dumps(value, separators=(",", ":"), ensure_ascii=False)},')
    vobs = doc.get("vobs", [])
    if vobs:
        body = ",\n".join(
            "    " + json.dumps(v, separators=(",", ":"), ensure_ascii=False) for v in vobs
        )
        tail = f'  "vobs": [\n{body}\n  ]'
    else:
        tail = '  "vobs": []'
    for key in ("waynet", "zones"):
        if key not in doc:
            continue
        if key == "waynet" and isinstance(doc[key], dict):  # the engine's layout, entry per line
            from gothar_worldgen.waynet.write import waynet_text

            tail += f',\n  "waynet": {waynet_text(doc[key])}'
            continue
        zones = doc[key] if key == "zones" else None
        if zones and all(isinstance(z, dict) for z in zones):  # engine layout, zone per line
            from gothar_worldgen.uses.zones import zones_text

            tail += f',\n  "zones": {zones_text(doc[key])}'
            continue
        tail += f',\n  "{key}": {json.dumps(doc[key], separators=(",", ":"), ensure_ascii=False)}'
    return "\n".join(lines) + "\n" + tail + "\n}\n"


def world_document(
    name: str, block: dict[str, Any], existing: dict[str, Any] | None
) -> dict[str, Any]:
    """New world, or ``existing`` with only its terrain block replaced (vobs and ids are kept)."""
    if existing is None:
        return {
            "version": WORLD_FILE_VERSION,
            "name": name,
            "nextVobId": 1,
            "staticMeshes": [],
            "terrain": block,
            "vobs": [],
        }
    if existing.get("version") != WORLD_FILE_VERSION:
        raise ExportError("existing world file is not version 1")
    doc: dict[str, Any] = {}
    for key in ("version", "name", "nextVobId", "staticMeshes"):
        if key in existing:
            doc[key] = existing[key]
    doc["terrain"] = block
    for key, value in existing.items():
        if key not in doc:
            doc[key] = value
    return doc


@dataclass(frozen=True)
class TerrainExport:
    grid: Grid
    min_y: float
    max_y: float
    world_path: Path
    heightmap_path: Path

    @property
    def step_mm(self) -> float:
        return (self.max_y - self.min_y) / U16_MAX * 1000


def _write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(path)


def export_terrain(
    grid: Grid,
    name: str,
    world_path: Path,
    heightmap_path: Path,
    heightmap_vfs: str,
    splat: dict[str, Any] | None = None,
    starts: tuple[StartPoint, ...] = (),
) -> TerrainExport:
    values, min_y, max_y = quantize_heights(grid.heights)
    block = terrain_block(grid, heightmap_vfs, min_y, max_y)
    if splat is not None:
        block["splat"] = splat
    problems = check_terrain_block(block)
    if problems:
        raise ExportError("; ".join(problems))
    existing = None
    if world_path.is_file():
        try:
            existing = json.loads(world_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            raise ExportError(f"{world_path.name}: invalid JSON ({e})") from None
    doc = world_document(name, block, existing)
    if starts:
        add_start_points(doc, starts, grid.height_at)
    _write_atomic(heightmap_path, values.astype("<u2").tobytes())
    _write_atomic(world_path, world_text(doc).encode("utf-8"))
    return TerrainExport(grid, min_y, max_y, world_path, heightmap_path)
