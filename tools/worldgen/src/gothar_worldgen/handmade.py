"""Individually modelled objects that replace generated buildings (W6, ``handmade.json``).

The castle (``gothar-worldgen schloss <site>``) is built by a Blender Python script, the only
source of the model (``blender/schloss/build_schloss.py``). This module runs it, keeps the list
of hand-made objects (key, mesh, position, replaced building ids, footprints for the city wall)
and gives the assembler what it places.
"""

from __future__ import annotations

import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from types import ModuleType
from typing import Any

FORMAT = "gothar-handmade"
VERSION = 1
SCHLOSS_DIR = Path(__file__).resolve().parents[2] / "blender" / "schloss"


class HandmadeError(Exception):
    """Missing tool or inconsistent data; the message is meant for the user."""


def find_blender() -> Path | None:
    """Blender from G7_BLENDER, the PATH or the usual Windows install folder."""
    env = os.environ.get("G7_BLENDER")
    candidates = [Path(env)] if env else []
    found = shutil.which("blender")
    if found:
        candidates.append(Path(found))
    foundation = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Blender Foundation"
    if foundation.is_dir():
        candidates += sorted(foundation.glob("Blender */blender.exe"), reverse=True)
    return next((c for c in candidates if c.is_file()), None)


def schloss_geometry() -> ModuleType:
    """The castle's pure-Python geometry module (shared with the Blender script)."""
    spec = importlib.util.spec_from_file_location("schloss_geometry",
                                                  SCHLOSS_DIR / "schloss_geometry.py")  # fmt: skip
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault("schloss_geometry", module)  # dataclasses look the module up
    spec.loader.exec_module(module)
    return module


def load(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"format": FORMAT, "version": VERSION, "items": []}
    doc = json.loads(path.read_text(encoding="utf-8"))
    if doc.get("format") != FORMAT or doc.get("version") != VERSION:
        raise HandmadeError(f"{path.name}: not a {FORMAT} v{VERSION} file")
    return doc


def _one_line(m: re.Match[str]) -> str:
    return "[" + re.sub(r"\s+", " ", m.group(1)).strip() + "]"


def save(path: Path, doc: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(doc, indent=1, ensure_ascii=False)
    # Number lists (points, positions) and lists of points on one line each.
    text = re.sub(r"\[\s+(-?[\d.]+(?:,\s+-?[\d.]+)*)\s+\]", _one_line, text)
    text = re.sub(r"\[\s+((?:\[[^\[\]]*\],?\s*)+)\]", _one_line, text) + "\n"
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8", newline="\n")
    tmp.replace(path)


def schloss_item(spec: dict[str, Any], mesh: str) -> dict[str, Any]:
    """handmade.json entry of the castle: position, replaced ids, footprints (wings, tower)."""
    geo = schloss_geometry()
    origin = geo.origin_of(spec)
    footprints = []
    for ws in spec["wings"]:
        w = geo.Wing(ws)
        h = w.half
        ring = [w.p(0, -h, 0), w.p(w.length, -h, 0), w.p(w.length, h, 0), w.p(0, h, 0)]
        footprints.append([[round(p[0], 2), round(p[2], 2)] for p in ring])
    tower = spec.get("stairTower")
    if tower:
        ring = geo._ring((tower["at"][0], tower["at"][1]), float(tower["radius"]), 0.0)
        footprints.append([[round(p[0], 2), round(p[2], 2)] for p in ring])
    item = {"key": "schloss", "mesh": mesh, "pos": [round(v, 3) for v in origin],
            "replaces": list(spec.get("replaces", [])), "footprints": footprints}  # fmt: skip
    splat = geo.garden_splat(spec)
    if splat:
        item["splat"] = splat  # garden: gravel paths, lawn beds (export-terrain)
    return item


def put_item(doc: dict[str, Any], item: dict[str, Any]) -> dict[str, Any]:
    items = [i for i in doc.get("items", []) if i.get("key") != item["key"]]
    items.append(item)
    return {"format": FORMAT, "version": VERSION, "items": sorted(items, key=lambda i: i["key"])}


def build_schloss(blender: Path, spec_path: Path, rules_path: Path, out_glb: Path,
                  out_blend: Path | None) -> str:  # fmt: skip
    """Runs the Blender script headless; returns its summary line."""
    cmd = [str(blender), "--background", "--factory-startup", "--python-exit-code", "1",
           "--python", str(SCHLOSS_DIR / "build_schloss.py"), "--", str(spec_path),
           str(rules_path), str(out_glb)]  # fmt: skip
    if out_blend is not None:
        cmd.append(str(out_blend))
    done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=600, check=False)  # fmt: skip
    line = next((ln for ln in done.stdout.splitlines() if ln.startswith("SCHLOSS_OK")), None)
    if done.returncode != 0 or line is None:
        raise HandmadeError("Blender build failed:\n" + (done.stdout + done.stderr)[-2000:])
    return line


def splat_areas(doc: dict[str, Any]) -> list[dict[str, Any]]:
    """Garden areas of the hand-made objects for the splat map (gravel with lawn beds)."""
    return [item["splat"] for item in doc.get("items", []) if item.get("splat")]


def footprints(doc: dict[str, Any]) -> list[list[list[float]]]:
    return [fp for item in doc.get("items", []) for fp in item.get("footprints", [])]
