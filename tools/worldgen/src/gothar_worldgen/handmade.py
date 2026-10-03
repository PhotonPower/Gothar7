"""Individually modelled objects that replace generated buildings (W6, ``handmade.json``).

The castle (``gothar-worldgen schloss <site>``) is built by a Blender Python script, the only
source of the model (``blender/schloss/build_schloss.py``). The market fountain
(``gothar-worldgen marktbrunnen <site>``) is the project owner's own model, adapted by a Blender
script (``blender/marktbrunnen/build_marktbrunnen.py``). This module runs the scripts, keeps the
list of hand-made objects (key, mesh, position, replaced building ids, footprints for the city
wall) and gives the assembler what it places.
"""

from __future__ import annotations

import importlib.util
import json
import math
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
MARKTBRUNNEN_DIR = Path(__file__).resolve().parents[2] / "blender" / "marktbrunnen"


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


def garden_plan(spec: dict[str, Any], height: Any) -> dict[str, Any] | None:  # noqa: ANN401
    """Level terraces, walls and stairs of the castle garden from the DGM (``garden_terraces``)."""
    from gothar_worldgen.garden_terraces import plan

    geo = schloss_geometry()
    lay = geo.garden_layout(spec)
    if lay is None or "railing" not in lay["spec"]:
        return None
    return plan(lay["wing"], height, lay["spec"]["railing"])


WING_F = (0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0)  # terrain samples along each wing, ends included


def with_wing_terrain(spec: dict[str, Any], height: Any) -> dict[str, Any]:  # noqa: ANN401
    """Copy of the castle spec with each wing's ground measured from the DGM: per side, at the
    ``WING_F`` fractions, the highest ground 0.5..1.5 m in front of the wall within 1 m along it
    (so the stone socle reaches the ground also where the slope rises at the ends)."""
    geo = schloss_geometry()
    wings = []
    for ws in spec["wings"]:
        w = geo.Wing(ws)
        terrain: dict[str, Any] = {"f": list(WING_F)}
        for side, key in ((1, "court"), (-1, "garden")):
            vals = []
            for f in WING_F:
                s0 = f * w.length
                samples = []
                for ds in (-1.0, 0.0, 1.0):
                    for dt in (0.5, 1.0, 1.5):
                        x, _, z = w.p(min(max(s0 + ds, 0.0), w.length), side * (w.half + dt), 0)
                        samples.append(height(x, z))
                vals.append(round(max(samples), 2))
            terrain[key] = vals
        wings.append({**ws, "terrain": terrain})
    return {**spec, "wings": wings}


def with_garden_plan(spec: dict[str, Any], garden: dict[str, Any] | None) -> dict[str, Any]:
    """Copy of the castle spec with the terrace plan for the Blender script."""
    if garden is None:
        return spec
    return {**spec, "garden": {**spec["garden"], "terracePlan": garden}}


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
    garden = spec.get("garden", {}).get("terracePlan")
    if garden:
        item["pads"] = garden["pads"]  # level terraces in the heightmap (export-terrain)
        item["terraces"] = [
            {"key": tr["key"], "s": tr["s"], "y": tr["y"]} for tr in garden["terraces"]
        ]
    return item


def pads(doc: dict[str, Any]) -> list[dict[str, Any]]:
    """Heightmap pads of all hand-made objects (level ground under them)."""
    return [pad for item in doc.get("items", []) for pad in item.get("pads", [])]


def put_item(doc: dict[str, Any], item: dict[str, Any]) -> dict[str, Any]:
    items = [i for i in doc.get("items", []) if i.get("key") != item["key"]]
    items.append(item)
    return {"format": FORMAT, "version": VERSION, "items": sorted(items, key=lambda i: i["key"])}


def _run_blender(blender: Path, script: Path, ok: str, spec_path: Path, rules_path: Path,
                 out_glb: Path, out_blend: Path | None) -> str:  # fmt: skip
    cmd = [str(blender), "--background", "--factory-startup", "--python-exit-code", "1",
           "--python", str(script), "--", str(spec_path), str(rules_path),
           str(out_glb)]  # fmt: skip
    if out_blend is not None:
        cmd.append(str(out_blend))
    done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=600, check=False)  # fmt: skip
    line = next((ln for ln in done.stdout.splitlines() if ln.startswith(ok)), None)
    if done.returncode != 0 or line is None:
        raise HandmadeError("Blender build failed:\n" + (done.stdout + done.stderr)[-2000:])
    return line


def build_schloss(blender: Path, spec_path: Path, rules_path: Path, out_glb: Path,
                  out_blend: Path | None) -> str:  # fmt: skip
    """Runs the castle's Blender script headless; returns its summary line."""
    return _run_blender(blender, SCHLOSS_DIR / "build_schloss.py", "SCHLOSS_OK", spec_path,
                        rules_path, out_glb, out_blend)  # fmt: skip


def build_marktbrunnen(blender: Path, spec_path: Path, rules_path: Path, out_glb: Path,
                       out_blend: Path | None) -> str:  # fmt: skip
    """Runs the fountain's Blender script headless; returns its summary line."""
    return _run_blender(blender, MARKTBRUNNEN_DIR / "build_marktbrunnen.py", "MARKTBRUNNEN_OK",
                        spec_path, rules_path, out_glb, out_blend)  # fmt: skip


def marktbrunnen_geometry() -> ModuleType:
    """The fountain's pure-Python helper module (spouts, collision bodies)."""
    spec = importlib.util.spec_from_file_location(
        "marktbrunnen_geometry", MARKTBRUNNEN_DIR / "marktbrunnen_geometry.py"
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault("marktbrunnen_geometry", module)
    spec.loader.exec_module(module)
    return module


def marktbrunnen_item(spec: dict[str, Any], mesh: str, ground_max: float) -> dict[str, Any]:
    """handmade.json entry of the fountain: at the world origin (the model's origin is the
    fountain's foot centre). Ground rule of the hand-made models: on the highest ground under the
    lower step; a pad levels the square under it to that height (``sinkM`` above the foot, so the
    step sits a little in it) and blends into the square over ``padFadeM``."""
    r = max(float(st["radius"]) for st in spec["collision"]["steps"])
    angles = [math.radians(45 * k) for k in range(8)]
    ring = [[round(r * math.cos(a), 2), round(r * math.sin(a), 2)] for a in angles]
    y = round(ground_max, 3)
    pad_r = r + 0.3
    pad = [[round(pad_r * math.cos(a), 2), round(pad_r * math.sin(a), 2)] for a in angles]
    return {"key": "marktbrunnen", "mesh": mesh, "pos": [0.0, y, 0.0], "replaces": [],
            "footprints": [ring],
            "pads": [{"polygon": pad, "y": round(y + float(spec.get("sinkM", 0.0)), 3),
                      "fadeM": float(spec.get("padFadeM", 3.0))}]}  # fmt: skip


def splat_areas(doc: dict[str, Any]) -> list[dict[str, Any]]:
    """Garden areas of the hand-made objects for the splat map (gravel with lawn beds)."""
    return [item["splat"] for item in doc.get("items", []) if item.get("splat")]


def footprints(doc: dict[str, Any]) -> list[list[list[float]]]:
    return [fp for item in doc.get("items", []) for fp in item.get("footprints", [])]
