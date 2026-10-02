"""Rectified facade views of one building from one 360° image (CLI ``facade preview``, web UI)."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image as PilImage

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.rectify import FacadeError, FacadeView, facade_from_footprint, rectify


@dataclass(frozen=True)
class EdgeResult:
    edge: int
    view: FacadeView | None
    path: Path | None
    problem: str | None = None


def load_buildings(path: Path) -> dict[str, dict[str, Any]]:
    """Buildings of a ``buildings.json`` (from ``gothar-worldgen import``) by id."""
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise FacadeError(f"{path} not found (run 'gothar-worldgen import <site>' first)") from None
    except (OSError, json.JSONDecodeError) as e:
        raise FacadeError(f"{path}: {e}") from None
    return {b["id"]: b for b in doc.get("buildings", [])}


def load_equirect(path: Path) -> np.ndarray:
    try:
        with PilImage.open(path) as img:
            rgb = np.asarray(img.convert("RGB"))
    except OSError as e:
        raise FacadeError(f"{path}: cannot read image ({e})") from None
    if rgb.shape[1] != 2 * rgb.shape[0]:
        raise FacadeError(
            f"{path}: not equirectangular ({rgb.shape[1]} x {rgb.shape[0]}, expected 2:1)"
        )
    return rgb


def preview_building(
    building: dict[str, Any],
    image: np.ndarray,
    pose: CameraPose,
    out_dir: Path,
    edges: list[int] | None = None,
    px_per_m: float = 50.0,
) -> list[EdgeResult]:
    """Writes ``<id>_edge<n>.png`` for every requested edge the camera sees from outside."""
    count = len(building.get("footprint") or [])
    results = []
    for edge in edges if edges is not None else range(count):
        try:
            view = rectify(image, pose, facade_from_footprint(building, edge), px_per_m)
        except FacadeError as e:
            results.append(EdgeResult(edge, None, None, str(e)))
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        path = out_dir / f"{building['id']}_edge{edge}.png"
        PilImage.fromarray(view.image).save(path)
        results.append(EdgeResult(edge, view, path))
    return results
