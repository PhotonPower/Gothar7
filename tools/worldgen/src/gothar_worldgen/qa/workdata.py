"""Loading the intermediate data written by ``import`` from ``work/<site>/``."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

WORK_FILES = ("terrain.json", "terrain.r16", "buildings.json", "streets.json", "features.json")


class QaError(Exception):
    """Missing or unreadable intermediate data; the message is meant for the user."""


@dataclass
class WorkData:
    terrain: dict[str, Any]
    heights: npt.NDArray[np.float32]  # local y per sample, row 0 = north
    buildings: list[dict[str, Any]]
    streets: list[dict[str, Any]]
    squares: list[dict[str, Any]]
    features: list[dict[str, Any]]
    origins: dict[str, dict[str, Any]]  # origin block per file, must all be equal

    def sample_heights(self, xs: npt.ArrayLike, zs: npt.ArrayLike) -> npt.NDArray[np.float64]:
        """Terrain height (nearest sample) at local positions; NaN outside the heightmap."""
        t = self.terrain
        cell = t["cellSize"]
        cols = np.rint((np.asarray(xs, dtype=np.float64) - t["firstSample"]["x"]) / cell)
        rows = np.rint((np.asarray(zs, dtype=np.float64) - t["firstSample"]["z"]) / cell)
        inside = (cols >= 0) & (cols < t["width"]) & (rows >= 0) & (rows < t["height"])
        out = np.full(cols.shape, np.nan)
        out[inside] = self.heights[rows[inside].astype(np.intp), cols[inside].astype(np.intp)]
        return out


def _json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise QaError(f"{path} not found (run 'gothar-worldgen import <site>' first)") from None
    except json.JSONDecodeError as e:
        raise QaError(f"{path}: invalid JSON: {e}") from None


def decode_heights(terrain: dict[str, Any], raw: npt.NDArray[np.uint16]) -> npt.NDArray[np.float32]:
    hr = terrain["heightRange"]
    span = hr["maxY"] - hr["minY"]
    return (hr["minY"] + raw.astype(np.float64) / 65535.0 * span).astype(np.float32)


def load_work(work_dir: Path) -> WorkData:
    terrain = _json(work_dir / "terrain.json")
    r16 = work_dir / "terrain.r16"
    if not r16.is_file():
        raise QaError(f"{r16} not found (run 'gothar-worldgen import <site>' first)")
    raw = np.fromfile(r16, dtype="<u2")
    if raw.size != terrain["width"] * terrain["height"]:
        raise QaError(
            f"{r16.name}: {raw.size} samples, expected {terrain['width']} x {terrain['height']}"
        )
    heights = decode_heights(terrain, raw.reshape(terrain["height"], terrain["width"]))
    buildings = _json(work_dir / "buildings.json")
    streets = _json(work_dir / "streets.json")
    features = _json(work_dir / "features.json")
    return WorkData(
        terrain=terrain,
        heights=heights,
        buildings=buildings["buildings"],
        streets=streets["streets"],
        squares=streets["squares"],
        features=features["features"],
        origins={
            "terrain.json": terrain["origin"],
            "buildings.json": buildings["origin"],
            "streets.json": streets["origin"],
            "features.json": features["origin"],
        },
    )
