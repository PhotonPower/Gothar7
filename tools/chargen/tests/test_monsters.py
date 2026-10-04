"""Monster rigs (contract characters-pipeline.md §7): rig files, clip names, clip lists, checks."""

from __future__ import annotations

import copy
import tomllib
from pathlib import Path

import numpy as np
import pytest

from conftest import REPO_ROOT, node_index, set_accessor
from gothar_chargen.cli import main
from gothar_chargen.clipspec import ClipSpecError, load_set_spec, parse_set_spec
from gothar_chargen.gltf import Gltf
from gothar_chargen.naming import clip_mode, is_clip_name, is_monster_clip
from gothar_chargen.skeleton import (
    MONSTER_REQUIRED,
    SkeletonError,
    load_rig,
    monster_rig_text,
    packaged_species,
    parse_rig,
    species_of,
)
from gothar_chargen.validate import _quat_mul, _yaw_deg, reference_pose, validate_gltf

WOLF = REPO_ROOT / "assets/source/characters/monsters/wolf"
WOLF_REF = WOLF / "rig/wolf_reference.glb"
WOLF_ANIMS = WOLF / "anims/wolf.glb"


@pytest.fixture(scope="module")
def wolf_rig():
    return load_rig(species="wolf")


@pytest.fixture(scope="module")
def wolf_reference():
    return reference_pose(Gltf.load(WOLF_REF))


@pytest.fixture
def wolf_anims() -> Gltf:
    g = Gltf.load(WOLF_ANIMS)
    return Gltf(doc=copy.deepcopy(g.doc), bin=g.bin)


def codes(report, level="error"):
    return {i.code for i in report.issues if i.level == level}


# --- rig files -----------------------------------------------------------------------------------


def test_packaged_wolf_rig(wolf_rig):
    assert packaged_species() == ["keiler", "laufvogel", "wolf"]
    assert wolf_rig.is_monster and wolf_rig.species == "wolf"
    assert set(MONSTER_REQUIRED) <= set(wolf_rig.names)
    assert len(wolf_rig.bones) <= wolf_rig.max_bones == 64
    assert {a for a, _ in wolf_rig.orientation} == {"up", "forward", "left"}
    assert wolf_rig.bone("socket_mouth").socket
    assert not load_rig().is_monster


def _wolf_data() -> dict:
    return tomllib.loads(monster_rig_text("wolf"))


@pytest.mark.parametrize(
    ("change", "message"),
    [
        (lambda d: d["rig"].update(kind="dragon"), "kind"),
        (lambda d: d["rig"].update(species="Wolf 2"), "species"),
        (lambda d: d["rig"].pop("orientation"), "orientation"),
        (lambda d: d["rig"]["orientation"].update(up=["root", "tail_99"]), "two bones"),
        (lambda d: d["rig"]["orientation"].update(down=["root", "head"]), "unknown axis"),
        (
            lambda d: d.update(bone=[b for b in d["bone"] if b["name"] != "socket_mouth"]),
            "required",
        ),
    ],
)
def test_invalid_monster_rigs(change, message):
    data = _wolf_data()
    change(data)
    with pytest.raises(SkeletonError, match=message):
        parse_rig(data)


def test_species_lookup():
    assert species_of(Path("a/monsters/wolf/anims/wolf.glb")) == "wolf"
    assert species_of(Path("a/anims/human/none.glb")) is None
    assert species_of(Path("monsters/wolf.glb")) is None  # no species folder
    with pytest.raises(SkeletonError, match="no monster rig"):
        load_rig(species="dragon")
    with pytest.raises(SkeletonError, match="no monster rig"):
        load_rig(species="../human_reference")


def test_monster_clip_names():
    assert is_clip_name("wolf/s_walk") and is_monster_clip("wolf/t_attack_1", "wolf")
    assert not is_monster_clip("wolf/s_walk", "keiler")
    assert not is_clip_name("dragon/s_walk")  # no such rig
    assert not is_monster_clip("none/s_walk")
    assert clip_mode("wolf/s_walk") == "wolf"
    assert clip_mode("mob/chest/t_open") == "mob/chest"


# --- clip lists ----------------------------------------------------------------------------------

SRC = {"src": {"file": "wolf_clips.blend", "mapping": "identity"}}


def test_wolf_set():
    spec = load_set_spec("wolf")
    assert spec.rig == "wolf"
    assert spec.blend_path(Path("c")) == Path("c/monsters/wolf/anims/wolf.blend")
    contract = {
        "s_idle",
        "s_walk",
        "s_trot",
        "s_run",
        "t_attack_1",
        "t_attack_2",
        "t_hit",
        "t_die",
        "s_eat",
        "s_sleep",
        "t_threaten",
        "t_turn_l",
        "t_turn_r",
    }  # fmt: skip  (s_trot: wolf only, agreed with engine 2026-10-05)
    assert {n.split("/")[1] for n in spec.names} == contract
    attack = next(c for c in spec.clips if c.name == "wolf/t_attack_1")
    assert dict(attack.markers) == {"hit_start": 13, "hit_end": 17}


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ({"rig": "dragon", "clip": [{"name": "wolf/s_a", "from": "src:a"}]}, "rig"),
        ({"rig": "wolf", "clip": [{"name": "none/s_a", "from": "src:a"}]}, "must be named"),
        ({"clip": [{"name": "wolf/s_a", "from": "src:a"}]}, "without 'rig'"),
        (
            {"rig": "wolf", "clip": [{"name": "wolf/s_a", "from": "src:a", "markers": {"X": 1}}]},
            "markers",
        ),
        (
            {"rig": "wolf", "clip": [{"name": "wolf/t_a", "keyframe": "advance", "params": {}}]},
            "earlier clip",
        ),
        (
            {
                "rig": "wolf",
                "clip": [
                    {"name": "wolf/t_a", "keyframe": "keyposes", "params": {"base": "wolf/s_x"}}
                ],
            },
            "earlier clip",
        ),
    ],
)
def test_invalid_monster_sets(data, message):
    with pytest.raises(ClipSpecError, match=message):
        parse_set_spec({"set": "wolf", "sources": SRC, **data})


# --- validation ----------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("species", "bones", "clips"), [("wolf", 22, 13), ("keiler", 25, 12), ("laufvogel", 12, 12)]
)
def test_monster_files_pass(species, bones, clips):
    folder = REPO_ROOT / "assets/source/characters/monsters" / species
    ref_path = folder / f"rig/{species}_reference.glb"
    rig, reference = load_rig(species=species), reference_pose(Gltf.load(ref_path))
    for path in (ref_path, folder / f"anims/{species}.glb"):
        report = validate_gltf(Gltf.load(path), rig, reference, path=path)
        assert report.ok(strict=True), report.issues
    assert report.stats["bones"] == bones and report.stats["clips"] == clips
    spec = load_set_spec(species)
    assert spec.rig == species and len(spec.names) == clips


def test_keiler_rig():
    rig = load_rig(species="keiler")
    parents = rig.parents
    # IK-target feet of the source hang below the lower legs; shoulder/hip bones above the legs
    assert parents["front_foot_l"] == "front_lower_l" and parents["back_foot_r"] == "back_lower_r"
    assert (
        parents["front_upper_l"] == "front_shoulder_l" and parents["back_upper_r"] == "back_hip_r"
    )
    assert rig.height == pytest.approx(0.95, abs=0.01)


def test_laufvogel_rig():
    rig = load_rig(species="laufvogel")
    # bird naming (§7.1): thigh/calf/foot; left/right hint from the feet
    assert {"thigh_l", "calf_r", "foot_l", "foot_r"} <= set(rig.names)
    assert dict(rig.orientation)["left"] == ("foot_r", "foot_l")
    assert rig.height == pytest.approx(1.6, abs=0.01)


def test_human_rig_rejects_wolf(rig, reference):
    report = validate_gltf(Gltf.load(WOLF_REF), rig, reference, path=WOLF_REF)
    assert "skeleton.names" in codes(report)


def _anim(g: Gltf, name: str) -> dict:
    return next(a for a in g.doc["animations"] if a["name"] == name)


def _root_output(g: Gltf, clip: str, path: str) -> int:
    root = node_index(g, "root")
    anim = _anim(g, clip)
    ch = next(c for c in anim["channels"] if c["target"] == {"node": root, "path": path})
    return anim["samplers"][ch["sampler"]]["output"]


def test_root_motion_rules(wolf_anims, wolf_rig, wolf_reference):
    g = wolf_anims
    walk = _root_output(g, "wolf/s_walk", "translation")
    set_accessor(g, walk, np.zeros_like(g.accessor(walk)))  # in place
    turn = _root_output(g, "wolf/t_turn_l", "rotation")
    keys = g.accessor(turn).astype(np.float64)
    steps = np.linspace(0.0, -90.0, len(keys))
    right = np.array([_quat_mul(_yaw_quat(d), keys[0]) for d in steps])  # turns right instead
    set_accessor(g, turn, right)
    report = validate_gltf(g, wolf_rig, wolf_reference, path=WOLF_ANIMS)
    messages = [i.message for i in report.issues if i.code == "anim.root_motion"]
    assert len(messages) == 2, report.issues
    assert any("s_walk" in m for m in messages) and any("t_turn_l" in m for m in messages)


def _yaw_quat(degrees: float) -> np.ndarray:
    h = np.radians(degrees) / 2
    return np.array([0.0, np.sin(h), 0.0, np.cos(h)])


def test_yaw_measurement():
    rest = np.array([0.0, 0.70710677, 0.70710677, 0.0])  # the wolf root's rest rotation
    for degrees in (90.0, -90.0, 45.0, 170.0):
        turned = _quat_mul(_yaw_quat(degrees), rest)
        assert _yaw_deg(rest, turned) == pytest.approx(degrees, abs=1e-4)


def test_turn_clips_turn_90(wolf_anims):
    g = wolf_anims
    for clip, degrees in (("wolf/t_turn_l", 90.0), ("wolf/t_turn_r", -90.0)):
        q = g.accessor(_root_output(g, clip, "rotation")).astype(np.float64)
        assert _yaw_deg(q[0], q[-1]) == pytest.approx(degrees, abs=0.5)


def test_monster_clip_rules(wolf_anims, wolf_rig, wolf_reference, rig):
    g = wolf_anims
    _anim(g, "wolf/s_idle")["name"] = "none/s_idle"
    report = validate_gltf(g, wolf_rig, wolf_reference, path=WOLF_ANIMS)
    assert any("must start with 'wolf/'" in i.message for i in report.issues)
    human = validate_gltf(Gltf.load(WOLF_ANIMS), rig, None, path=WOLF_ANIMS)
    assert any("monster clip" in i.message for i in human.issues)


def test_orientation_and_height(wolf_rig, wolf_reference):
    g = Gltf.load(WOLF_REF)
    g = Gltf(doc=copy.deepcopy(g.doc), bin=g.bin)
    root = g.doc["nodes"][node_index(g, "root")]
    root["rotation"] = _quat_mul(_yaw_quat(180.0), np.array(root["rotation"])).tolist()  # backwards
    report = validate_gltf(g, wolf_rig, None, path=WOLF_REF)
    assert {"orientation.forward", "orientation.side"} <= codes(report)
    assert "orientation.up" not in codes(report)

    small = tomllib.loads(monster_rig_text("wolf"))
    small["rig"]["height"] = 0.6
    report = validate_gltf(Gltf.load(WOLF_REF), parse_rig(small), None, path=WOLF_REF)
    assert "mesh.height" in codes(report)


def test_validate_command_picks_rig_per_file(capsys):
    keiler = REPO_ROOT / "assets/source/characters/monsters/keiler/anims/keiler.glb"
    assert main(["validate", "--strict", str(WOLF_REF), str(WOLF_ANIMS), str(keiler)]) == 0
    out = capsys.readouterr().out
    assert "3 file(s), 0 failed" in out


# --- collision capsule (engine M5/M9, §7.1) -------------------------------------------------------


@pytest.mark.parametrize(
    ("species", "shape"),
    [("wolf", "capsule_lying"), ("keiler", "capsule_lying"), ("laufvogel", "capsule_upright")],
)
def test_collision_matches_mesh(species, shape):
    from gothar_chargen.collision import derive_collision

    rig = load_rig(species=species)
    ref = REPO_ROOT / f"assets/source/characters/monsters/{species}/rig/{species}_reference.glb"
    assert rig.collision is not None and rig.collision.shape == shape
    assert rig.collision == derive_collision(Gltf.load(ref))
    assert rig.collision.length >= 2 * rig.collision.radius
    if shape == "capsule_upright":  # stands on the ground
        assert rig.collision.offset[1] == pytest.approx(rig.collision.length / 2, abs=0.01)


def test_collision_is_required_and_checked():
    from gothar_chargen.collision import CollisionError, parse_collision

    data = _wolf_data()
    del data["rig"]["collision"]
    with pytest.raises(SkeletonError, match="collision"):
        parse_rig(data)
    good = {"shape": "capsule_lying", "radius": 0.3, "length": 1.2, "offset": [0, 0.5, 0.4]}
    assert parse_collision(good).radius == 0.3
    for change, message in (
        ({"shape": "box"}, "shape"),
        ({"radius": 0}, "radius"),
        ({"length": 0.5}, "length"),
        ({"offset": [0, 1]}, "offset"),
    ):
        with pytest.raises(CollisionError, match=message):
            parse_collision({**good, **change})


def test_write_collision_replaces_block(tmp_path):
    from gothar_chargen.collision import Collision, write_collision

    toml = tmp_path / "wolf.toml"
    toml.write_text(monster_rig_text("wolf"), encoding="utf-8")
    new = Collision("capsule_upright", 0.4, 1.5, (0.0, 0.75, 0.1))
    write_collision(toml, new)
    write_collision(toml, new)  # idempotent: one block only
    text = toml.read_text(encoding="utf-8")
    assert text.count("[rig.collision]") == 1
    assert parse_rig(tomllib.loads(text)).collision == new


def test_stale_collision_warns(wolf_reference):
    data = _wolf_data()
    data["rig"]["collision"]["radius"] = 0.6
    report = validate_gltf(Gltf.load(WOLF_REF), parse_rig(data), wolf_reference, path=WOLF_REF)
    assert "collision.stale" in codes(report, "warning")
