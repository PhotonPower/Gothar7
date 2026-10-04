from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from conftest import (
    add_clip,
    node,
    node_index,
    rotate_node,
    set_accessor,
    strip_skin,
)
from gothar_chargen.gltf import Gltf
from gothar_chargen.validate import Report, validate_file, validate_gltf


def codes(report: Report, level: str = "error") -> set[str]:
    return {i.code for i in report.issues if i.level == level}


def check(g: Gltf, rig, reference, path: Path | None = None) -> Report:
    return validate_gltf(g, rig, reference, path=path)


# --- reference -----------------------------------------------------------------------------------


def test_reference_rig_passes_without_warnings(figure, rig, reference):
    report = check(figure, rig, reference)
    assert report.issues == []
    assert report.ok(strict=True)
    assert report.stats["bones"] == len(rig.bones) == 60
    assert report.stats["skinned_meshes"] == 1
    assert sorted(report.stats["morph_targets"]) == sorted(rig.morph_targets)
    assert 1.75 < report.stats["height"] < 1.85


def test_reference_rig_passes_without_reference_pose(figure, rig):
    assert check(figure, rig, None).ok(strict=True)


def test_reference_uses_glb_conventions(reference_gltf):
    world = reference_gltf.world_matrices()

    def pos(name: str) -> np.ndarray:
        return world[node_index(reference_gltf, name)][:3, 3]

    assert pos("head")[1] > 1.5  # Y up
    assert pos("ball_l")[2] > pos("foot_l")[2]  # faces +Z
    assert pos("hand_l")[0] > 0.6 > -0.6 > pos("hand_r")[0]  # left = +X, T-pose
    assert abs(pos("hand_l")[1] - pos("upperarm_l")[1]) < 0.01  # arms horizontal


# --- skeleton ------------------------------------------------------------------------------------


def test_renamed_bone_is_missing_and_unknown(figure, rig, reference):
    node(figure, "calf_l")["name"] = "shin_l"
    report = check(figure, rig, reference)
    messages = " ".join(i.message for i in report.errors)
    assert "skeleton.names" in codes(report)
    assert "calf_l" in messages and "shin_l" in messages


def test_missing_root_stops_skeleton_checks(figure, rig, reference):
    node(figure, "root")["name"] = "Armature"
    report = check(figure, rig, reference)
    assert "skeleton.missing" in codes(report)


def test_duplicate_root(figure, rig, reference):
    node(figure, "mannequin")["name"] = "root"
    assert "skeleton.missing" in codes(check(figure, rig, reference))


def test_wrong_parent(figure, rig, reference):
    strip_skin(figure)
    hand = node_index(figure, "hand_l")
    lower = node(figure, "lowerarm_l")
    upper = node(figure, "upperarm_l")
    lower["children"].remove(hand)
    upper["children"].append(hand)
    report = check(figure, rig, reference)
    assert "skeleton.hierarchy" in codes(report)
    assert "hand_l (parent upperarm_l, expected lowerarm_l)" in " ".join(
        i.message for i in report.errors
    )


def test_too_many_bones(figure, rig, reference):
    from dataclasses import replace

    small = replace(rig, max_bones=10)
    assert "skeleton.count" in codes(check(figure, small, reference))


def test_scaled_armature_node(figure, rig, reference):
    node(figure, "human_reference")["scale"] = [0.01, 0.01, 0.01]
    assert "skeleton.scale" in codes(check(figure, rig, reference))


def test_root_not_at_origin(figure, rig, reference):
    strip_skin(figure)
    node(figure, "root")["translation"] = [0.0, 0.2, 0.0]
    assert codes(check(figure, rig, reference)) == {"skeleton.root_origin"}


# --- orientation ---------------------------------------------------------------------------------


def test_figure_facing_backwards(figure, rig, reference):
    strip_skin(figure)
    rotate_node(figure, "human_reference", (0, 1, 0), 180)
    report = check(figure, rig, reference)
    assert {"orientation.forward", "orientation.side"} <= codes(report)


def test_figure_upside_down(figure, rig, reference):
    strip_skin(figure)
    rotate_node(figure, "human_reference", (1, 0, 0), 180)
    assert "orientation.up" in codes(check(figure, rig, reference))


def test_blender_z_up_without_conversion(figure, rig, reference):
    strip_skin(figure)
    rotate_node(figure, "human_reference", (1, 0, 0), -90)
    assert "orientation.up" in codes(check(figure, rig, reference))


# --- bind pose -----------------------------------------------------------------------------------


def test_a_pose_arm_is_rejected(figure, rig, reference):
    strip_skin(figure)
    rotate_node(figure, "upperarm_l", (0, 0, 1), 40)
    report = check(figure, rig, reference)
    assert codes(report) == {"pose.rotation"}
    assert "upperarm_l" in report.errors[0].message


def test_small_rotation_is_a_warning(figure, rig, reference):
    strip_skin(figure)
    rotate_node(figure, "lowerarm_r", (0, 0, 1), 3)
    report = check(figure, rig, reference)
    assert report.ok()
    assert not report.ok(strict=True)
    assert codes(report, "warning") == {"pose.rotation"}


@pytest.mark.parametrize(("factor", "level"), [(1.3, "error"), (1.08, "warning")])
def test_bone_length(figure, rig, reference, factor, level):
    strip_skin(figure)
    calf = node(figure, "calf_l")
    calf["translation"] = [v * factor for v in calf["translation"]]
    assert codes(check(figure, rig, reference), level) == {"pose.proportion"}


def test_pose_differing_from_skin_bind_pose(figure, rig, reference):
    rotate_node(figure, "thigh_r", (1, 0, 0), 30)
    assert "skin.bind" in codes(check(figure, rig, reference))


# --- skin ----------------------------------------------------------------------------------------


def _prim(g: Gltf) -> dict:
    return g.doc["meshes"][0]["primitives"][0]


def test_weights_not_normalised(figure, rig, reference):
    idx = _prim(figure)["attributes"]["WEIGHTS_0"]
    weights = figure.accessor(idx).copy()
    weights[:5] *= 0.5
    set_accessor(figure, idx, weights)
    report = check(figure, rig, reference)
    assert codes(report) == {"skin.weights"}
    assert "5 vertices" in report.errors[0].message


def test_socket_with_weights(figure, rig, reference):
    skin = figure.doc["skins"][0]
    socket = skin["joints"].index(node_index(figure, "socket_hand_r"))
    idx = _prim(figure)["attributes"]["JOINTS_0"]
    joints = figure.accessor(idx).copy()
    joints[0, 0] = socket
    set_accessor(figure, idx, joints)
    report = check(figure, rig, reference)
    assert codes(report) == {"skin.socket_weights"}
    assert "socket_hand_r" in report.errors[0].message


def test_more_than_four_influences(figure, rig, reference):
    attrs = _prim(figure)["attributes"]
    attrs["JOINTS_1"] = attrs["JOINTS_0"]
    attrs["WEIGHTS_1"] = attrs["WEIGHTS_0"]
    assert "skin.influences" in codes(check(figure, rig, reference))


def test_skin_without_weights(figure, rig, reference):
    del _prim(figure)["attributes"]["WEIGHTS_0"]
    assert "skin.weights" in codes(check(figure, rig, reference))


def test_joint_index_out_of_range(figure, rig, reference):
    idx = _prim(figure)["attributes"]["JOINTS_0"]
    joints = figure.accessor(idx).copy()
    joints[0, 0] = 200
    set_accessor(figure, idx, joints)
    assert "skin.weights" in codes(check(figure, rig, reference))


def test_skin_joint_outside_skeleton(figure, rig, reference):
    figure.doc["skins"][0]["joints"][-1] = node_index(figure, "human_reference")
    assert "skin.joints" in codes(check(figure, rig, reference))


def test_unskinned_mesh_is_a_warning(figure, rig, reference):
    node(figure, "mannequin").pop("skin")
    report = check(figure, rig, reference)
    assert report.ok()
    assert codes(report, "warning") == {"skin.missing"}


# --- mesh and morph targets ----------------------------------------------------------------------


def test_unknown_morph_target(figure, rig, reference):
    figure.doc["meshes"][0]["extras"]["targetNames"][0] = "smile"
    report = check(figure, rig, reference)
    assert codes(report) == {"morph.name", "morph.set"}
    assert any("smile" in e.message for e in report.errors if e.code == "morph.name")


@pytest.mark.parametrize(("top", "level"), [(2.5, "error"), (1.6, "warning"), (1.0, "error")])
def test_figure_height(figure, rig, reference, top, level):
    pos = figure.doc["accessors"][_prim(figure)["attributes"]["POSITION"]]
    pos["max"][1] = top
    assert "mesh.height" in codes(check(figure, rig, reference), level)


def test_figure_floating(figure, rig, reference):
    pos = figure.doc["accessors"][_prim(figure)["attributes"]["POSITION"]]
    pos["min"][1] = 0.2
    assert "mesh.ground" in codes(check(figure, rig, reference), "warning")


# --- animations and events -----------------------------------------------------------------------


def test_valid_clips(figure, rig, reference):
    add_clip(figure, "none/s_walk", ["root", "pelvis", "thigh_l"])
    add_clip(figure, "mob/anvil/s_work", ["spine_01"])
    report = check(figure, rig, reference)
    assert report.ok(strict=True)
    assert report.stats["clips"] == 2


def test_badly_named_and_duplicate_clips(figure, rig, reference):
    add_clip(figure, "Walk", ["pelvis"])
    add_clip(figure, "none/s_run", ["pelvis"])
    add_clip(figure, "none/s_run", ["pelvis"])
    report = check(figure, rig, reference)
    messages = [i.message for i in report.errors]
    assert codes(report) == {"anim.name"}
    assert any("'Walk'" in m for m in messages)
    assert any("duplicate" in m for m in messages)


def _set_rotation_keys(g: Gltf, clip: str, keys: list[list[float]]) -> None:
    """Replaces the rotation keys of the clip's first channel (and its times) with `keys`."""
    from conftest import append_accessor

    anim = next(a for a in g.doc["animations"] if a["name"] == clip)
    sampler = anim["samplers"][anim["channels"][0]["sampler"]]
    sampler["input"] = append_accessor(g, np.arange(len(keys), dtype=np.float32) / 30.0, "SCALAR")
    sampler["output"] = append_accessor(g, np.array(keys, dtype=np.float32), "VEC4")


def _quat_z(degrees: float) -> list[float]:
    h = np.radians(degrees) / 2
    return [0.0, 0.0, float(np.sin(h)), float(np.cos(h))]


@pytest.mark.parametrize(
    ("clip", "angles", "error", "warning"),
    [
        ("none/t_jump_start", [0, 10, 20, 30], set(), set()),
        ("none/t_jump_start", [0, 10, 150, 160], {"anim.jump"}, set()),  # 130° in one frame
        ("none/t_jump_start", [0, 100, 110], set(), {"anim.jump"}),  # 100°: very fast
        ("none/s_walk", [0, 10, 20, 10, 0], set(), set()),  # closed loop
        ("none/s_walk", [0, 10, 20, 30], set(), {"anim.loop"}),  # 30° open
        ("none/t_walk_2_run", [0, 10, 20, 30], set(), set()),  # not a loop
    ],
)
def test_motion_checks(figure, rig, reference, clip, angles, error, warning):
    add_clip(figure, clip, ["pelvis"])
    _set_rotation_keys(figure, clip, [_quat_z(a) for a in angles])
    report = check(figure, rig, reference)
    assert codes(report) == error
    assert codes(report, "warning") == warning


def test_translation_jump(figure, rig, reference):
    from conftest import append_accessor

    add_clip(figure, "none/s_walk", ["pelvis"])
    anim = figure.doc["animations"][0]
    times = append_accessor(figure, np.array([0.0, 1 / 30, 2 / 30]), "SCALAR")
    values = append_accessor(figure, np.array([[0, 0.9, 0], [0, 0.9, 0], [0, 1.9, 0]]), "VEC3")
    anim["samplers"].append({"input": times, "output": values})
    anim["channels"].append(
        {"sampler": 1, "target": {"node": node_index(figure, "pelvis"), "path": "translation"}}
    )
    assert "anim.jump" in codes(check(figure, rig, reference))


def test_clip_animating_foreign_node(figure, rig, reference):
    add_clip(figure, "none/s_idle", ["mannequin"])
    assert codes(check(figure, rig, reference)) == {"anim.target"}


def _write(tmp_path: Path, g: Gltf, events: str | None) -> Path:
    glb = tmp_path / "none.glb"
    glb.write_bytes(g.to_bytes())
    if events is not None:
        (tmp_path / "none.events.toml").write_text(events, encoding="utf-8")
    return glb


def test_events_file_ok(tmp_path, figure, rig, reference):
    add_clip(figure, "none/s_walk", ["pelvis"], duration=1.0)
    events = """
version = 1
fps = 30
[clips."none/s_walk"]
events = [ { frame = 0, event = "footstep_l" }, { frame = 15, event = "footstep_r" } ]
"""
    report = validate_file(_write(tmp_path, figure, events), rig, reference)
    assert report.ok(strict=True), report.issues
    assert report.stats["events"] == 2


def test_events_for_unknown_clip_and_late_frame(tmp_path, figure, rig, reference):
    add_clip(figure, "none/s_walk", ["pelvis"], duration=1.0)
    events = """
version = 1
[clips."none/s_walk"]
events = [ { frame = 45, event = "footstep_l" } ]
[clips."none/s_run"]
events = [ { frame = 0, event = "footstep_l" } ]
"""
    report = validate_file(_write(tmp_path, figure, events), rig, reference)
    assert codes(report) == {"events.frame", "events.clip"}


@pytest.mark.parametrize(
    ("clip", "frame", "ok"),
    [("none/s_walk", 29, True), ("none/s_walk", 30, False), ("none/t_jump_land", 30, True)],
)
def test_events_on_last_frame(tmp_path, figure, rig, reference, clip, frame, ok):
    add_clip(figure, clip, ["pelvis"], duration=1.0)
    events = f"""
version = 1
[clips."{clip}"]
events = [ {{ frame = {frame}, event = "land" }} ]
"""
    report = validate_file(_write(tmp_path, figure, events), rig, reference)
    assert report.ok() == ok, report.issues


def test_same_frame_events_allowed(tmp_path, figure, rig, reference):
    add_clip(figure, "1h/t_attack_combo1_t2", ["pelvis"], duration=1.0)
    events = """
version = 1
[clips."1h/t_attack_combo1_t2"]
events = [
    { frame = 6, event = "hit_start" },
    { frame = 6, event = "sound:swing_light" },
]
"""
    assert validate_file(_write(tmp_path, figure, events), rig, reference).ok(strict=True)


def test_events_format_error(tmp_path, figure, rig, reference):
    add_clip(figure, "none/s_walk", ["pelvis"])
    report = validate_file(_write(tmp_path, figure, "version = 2\n"), rig, reference)
    assert codes(report) == {"events.format"}


def test_unreadable_file(tmp_path, rig, reference):
    bad = tmp_path / "broken.glb"
    bad.write_bytes(b"not a glb file at all")
    assert codes(validate_file(bad, rig, reference)) == {"gltf.parse"}


def test_malformed_structure_is_reported(figure, rig, reference):
    node(figure, "mannequin")["mesh"] = 99
    assert "gltf.structure" in codes(check(figure, rig, reference))


def test_report_to_dict(figure, rig, reference):
    node(figure, "calf_l")["name"] = "shin_l"
    d = check(figure, rig, reference, path=Path("x.glb")).to_dict()
    assert d["path"] == "x.glb"
    assert d["ok"] is False
    assert {"level", "code", "message"} <= set(d["issues"][0])
