"""Blender add-on: core logic always; the headless round trip only where Blender is installed."""

import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.batch import generate, write_index
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.export.terrain import Grid

ADDON = Path(__file__).resolve().parents[1] / "blender"
spec = importlib.util.spec_from_file_location(
    "gothar_buildings_core", ADDON / "gothar_buildings" / "core.py"
)
core = importlib.util.module_from_spec(spec)
sys.modules["gothar_buildings_core"] = core
spec.loader.exec_module(core)


def make_repo(tmp_path: Path) -> Path:
    """Fake checkout: assets/source/worlds/t/generated + index with one building."""
    gen = tmp_path / "assets" / "source" / "worlds" / "t" / "generated"
    roof = {"type": "saddle", "eaveY": 7.0, "ridgeY": 10.0, "ridgeDir": [1, 0]}
    fp = [[0, 0], [6, 0], [6, -4], [0, -4]]
    b = {"id": "B1", "inCore": True, "footprint": fp, "groundY": 2.0, "roof": roof}
    res = generate([b], Grid(np.zeros((3, 3)), -100.0, -100.0, 100.0), gen / "buildings",
                   "worlds/t/generated/buildings")  # fmt: skip
    write_index(gen / "buildings_index.json", res.index)
    return gen / "buildings_index.json"


def test_axes_round_trip():
    assert core.gltf_to_blender(1.0, 2.0, -3.0) == (1.0, 3.0, 2.0)  # north (-Z) -> Blender +Y
    assert core.blender_to_gltf(*core.gltf_to_blender(1.0, 2.0, -3.0)) == (1.0, 2.0, -3.0)


def test_site_paths(tmp_path: Path):
    site = core.Site(make_repo(tmp_path))
    assert site.name == "t"
    assert site.overrides == tmp_path / "tools" / "worldgen" / "data" / "t" / "buildings"
    assert site.mesh_file("B1").is_file()
    assert site.nearby(3.0, -2.0, 5.0) == ["B1"] and site.nearby(100.0, 0.0, 5.0) == []


def test_mark_locked_keeps_other_keys(tmp_path: Path):
    path = core.mark_locked(tmp_path, "B1", note="Hand")
    expected = {"id": "B1", "keep": True, "locked": True, "notes": "Hand"}
    assert json.loads(path.read_text(encoding="utf-8")) == expected
    path.write_text(
        json.dumps({"id": "B1", "keep": True, "storeys": [3.0], "x": 1}), encoding="utf-8"
    )
    core.mark_locked(tmp_path, "B1")
    expected = {"id": "B1", "keep": True, "storeys": [3.0], "x": 1, "locked": True}
    assert json.loads(path.read_text(encoding="utf-8")) == expected
    with pytest.raises(ValueError):
        core.mark_locked(tmp_path, "../B1")
    path.write_text(json.dumps({"id": "OTHER"}), encoding="utf-8")
    with pytest.raises(ValueError):
        core.mark_locked(tmp_path, "B1")


def _blender() -> Path | None:
    env = os.environ.get("G7_BLENDER")
    candidates = [Path(env)] if env else []
    found = shutil.which("blender")
    if found:
        candidates.append(Path(found))
    foundation = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Blender Foundation"
    if foundation.is_dir():
        candidates += sorted(foundation.glob("Blender */blender.exe"), reverse=True)
    return next((c for c in candidates if c.is_file()), None)


@pytest.mark.skipif(_blender() is None, reason="Blender not installed")
def test_headless_round_trip(tmp_path: Path):
    index = make_repo(tmp_path)
    site = core.Site(index)
    before = site.mesh_file("B1").read_bytes()
    done = subprocess.run(
        [str(_blender()), "--background", "--factory-startup", "--python-exit-code", "1",
         "--python", str(ADDON / "roundtrip_check.py"), "--", str(index), "B1"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
        check=False,
    )  # fmt: skip
    assert done.returncode == 0 and "ROUNDTRIP_OK" in done.stdout, (
        done.stdout[-3000:] + done.stderr[-3000:]
    )
    after = site.mesh_file("B1").read_bytes()
    assert after != before
    doc, _ = read_glb(after)
    # Origin and axes as generated: the raised roof is 1 m higher, x/z extents unchanged.
    acc = doc["accessors"][doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    old, _ = read_glb(before)
    assert acc["max"][1] == pytest.approx(old["accessors"][0]["max"][1] + 1.0, abs=1e-3)
    assert acc["min"][0] == pytest.approx(old["accessors"][0]["min"][0], abs=1e-3)
    override = json.loads((site.overrides / "B1.json").read_text(encoding="utf-8"))
    assert override["locked"] is True
