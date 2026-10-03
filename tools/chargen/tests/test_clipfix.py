"""Clips against a reference mesh: feet that stand, lying poses above the ground (clipfix)."""

from __future__ import annotations

import shutil

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.clipfix import (
    _Skin,
    _times,
    check_set,
    fit_root_speed,
    foot_slide,
    lift_to_ground,
    reference_mesh_for,
)
from gothar_chargen.gltf import Gltf

CHARACTERS = REPO_ROOT / "assets/source/characters"
WOLF = CHARACTERS / "monsters/wolf/anims/wolf.glb"


def anim(g: Gltf, name: str) -> dict:
    return next(a for a in g.doc["animations"] if a["name"] == name)


def test_reference_meshes():
    assert reference_mesh_for(WOLF).name == "wolf_reference.glb"
    assert reference_mesh_for(CHARACTERS / "anims/human/none.glb").name == "human_reference.glb"
    assert reference_mesh_for(CHARACTERS / "figures/farmer.glb") is None


def test_committed_sets_pass():
    for glb in [*CHARACTERS.glob("anims/*/*.glb"), *CHARACTERS.glob("monsters/*/anims/*.glb")]:
        assert check_set(glb, Gltf.load(glb)) == [], glb.name


def test_feet_stand_in_root_motion_clips():
    g = Gltf.load(WOLF)
    for clip in ("wolf/s_walk", "wolf/s_run"):
        speed, stride = foot_slide(g, anim(g, clip))
        assert speed == pytest.approx(stride, rel=0.02)


def test_sliding_is_found_and_fixed(tmp_path):
    glb = tmp_path / "anims" / "wolf.glb"
    glb.parent.mkdir()
    shutil.copy(WOLF, glb)
    g = Gltf.load(glb)
    run = anim(g, "wolf/s_run")
    root = next(i for i, n in enumerate(g.doc["nodes"]) if n["name"] == "root")
    out = next(
        run["samplers"][c["sampler"]]["output"]
        for c in run["channels"]
        if c["target"]["node"] == root and c["target"]["path"] == "translation"
    )
    keys = np.asarray(g.accessor(out), dtype=np.float64)
    g.set_accessor(out, keys * np.array([1.0, 1.0, 0.7]))  # root 30 % too slow
    assert [c for c, _ in check_set(glb, g)] == ["anim.slide"]
    assert fit_root_speed(g, run) == pytest.approx(1 / 0.7, rel=0.02)
    assert check_set(glb, g) == []


def test_sinking_is_found_and_lifted(tmp_path):
    art = tmp_path / "wolf"
    shutil.copytree(WOLF.parent.parent, art)
    glb = art / "anims" / "wolf.glb"
    g = Gltf.load(glb)
    die = anim(g, "wolf/t_die")
    root = next(i for i, n in enumerate(g.doc["nodes"]) if n["name"] == "root")
    out = next(
        die["samplers"][c["sampler"]]["output"]
        for c in die["channels"]
        if c["target"]["node"] == root and c["target"]["path"] == "translation"
    )
    keys = np.asarray(g.accessor(out), dtype=np.float64)
    g.set_accessor(out, keys - np.array([0.0, 0.1, 0.0]))  # the whole pose 10 cm down
    assert [c for c, _ in check_set(glb, g)] == ["anim.ground"]
    skin = _Skin(Gltf.load(reference_mesh_for(glb)))
    assert lift_to_ground(g, die, skin) == pytest.approx(0.1, abs=0.01)
    assert skin.lowest(g, die, _times(g, die)).min() == pytest.approx(0.0, abs=0.005)
    assert check_set(glb, g) == []
