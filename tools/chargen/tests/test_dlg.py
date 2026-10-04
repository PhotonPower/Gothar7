"""Additive dialogue gestures (contract §3.2): the committed set and the rule anim.additive."""

from __future__ import annotations

from pathlib import Path

import numpy as np

from conftest import REPO_ROOT
from gothar_chargen.gltf import Gltf
from gothar_chargen.skeleton import load_rig
from gothar_chargen.validate import validate_file, validate_gltf

DLG = REPO_ROOT / "assets/source/characters/anims/human/dlg.glb"


def _codes(report) -> list[str]:
    return [i.code for i in report.issues]


def test_committed_gestures_follow_the_contract():
    report = validate_file(DLG, load_rig())
    assert report.ok(strict=True), report.issues
    names = {a["name"] for a in Gltf.load(DLG).list("animations")}
    assert "dlg/a_neutral" in names and len(names) == 22


def _with_change(clip: str, bone: str, path: str, change) -> Gltf:
    g = Gltf.load(DLG)
    anim = next(a for a in g.list("animations") if a["name"] == clip)
    node = next(i for i, n in enumerate(g.doc["nodes"]) if n.get("name") == bone)
    ch = next(c for c in anim["channels"] if c["target"] == {"node": node, "path": path})
    acc = anim["samplers"][ch["sampler"]]["output"]
    values = np.array(g.accessor(acc), dtype=np.float64)
    g.set_accessor(acc, change(values))
    return g


def _turned(values: np.ndarray) -> np.ndarray:
    """Turns the middle keys by about 20 degrees about X (quaternions xyzw)."""
    out = values.copy()
    mid = slice(len(out) // 3, 2 * len(out) // 3)
    out[mid] = out[mid] * np.cos(0.17) + np.array([np.sin(0.17), 0, 0, 0]) * np.linalg.norm(
        out[mid], axis=1, keepdims=True
    )
    return out / np.linalg.norm(out, axis=1, keepdims=True)


def test_rule_finds_legs_that_move():
    g = _with_change("dlg/a_nod", "thigh_l", "rotation", _turned)
    report = validate_gltf(g, load_rig(), path=Path("dlg.glb"))
    assert "anim.additive" in _codes(report)


def test_rule_finds_a_gesture_that_does_not_return():
    def stay(values: np.ndarray) -> np.ndarray:
        out = _turned(values)
        out[-1] = out[len(out) // 2]
        return out

    g = _with_change("dlg/a_shrug", "upperarm_r", "rotation", stay)
    report = validate_gltf(g, load_rig(), path=Path("dlg.glb"))
    assert any("does not end" in i.message for i in report.issues if i.code == "anim.additive")
