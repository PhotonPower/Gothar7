from __future__ import annotations

from pathlib import Path

import pytest

from gothar_chargen.mapping import MappingError, load_mapping, parse_mapping
from gothar_chargen.skeleton import load_rig

REFERENCE = set(load_rig().names)
BODY = {n for n in REFERENCE if not n.startswith("socket_")}

# Bone names of the Quaternius rig in UAL2 naming (65 joints incl. leaf bones).
UAL2_BONES = (
    ["root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "Head"]
    + [
        f"{b}_{s}"
        for s in ("l", "r")
        for b in ("clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball")
    ]
    + [
        f"{f}_{i}_{s}"
        for s in ("l", "r")
        for f in ("thumb", "index", "middle", "ring", "pinky")
        for i in ("01", "02", "03", "04_leaf")
    ]
    + ["ball_leaf_l", "ball_leaf_r"]
)


def test_ual2_covers_all_body_bones():
    m = load_mapping("quaternius_ual2")
    assert len(UAL2_BONES) == 65
    resolved = m.resolve(UAL2_BONES, REFERENCE)
    assert set(resolved.values()) == BODY
    assert resolved["neck_01"] == "neck"
    assert m.target("index_04_leaf_l", REFERENCE) is None
    assert m.license == "CC0 1.0"


def test_ual1_covers_all_body_bones():
    m = load_mapping("quaternius_ual1")
    assert set(m.bones.values()) == BODY
    assert m.resolve(list(m.bones), REFERENCE) == m.bones
    assert m.target("DEF-unknown", REFERENCE) is None


def test_double_mapping_is_rejected():
    m = parse_mapping({"bones": {"a": "pelvis", "b": "pelvis"}})
    with pytest.raises(MappingError, match="both map"):
        m.resolve(["a", "b"], REFERENCE)


def test_unknown_target_is_rejected():
    m = parse_mapping({"bones": {"a": "tail_01"}})
    with pytest.raises(MappingError, match="unknown bone"):
        m.resolve(["a"], REFERENCE)


def test_invalid_mapping_files(tmp_path: Path):
    with pytest.raises(MappingError, match="unknown mapping"):
        load_mapping("does_not_exist")
    bad = tmp_path / "bad.toml"
    bad.write_text("[bones\n", encoding="utf-8")
    with pytest.raises(MappingError):
        load_mapping(bad)
    bad.write_text("[bones]\na = 1\n", encoding="utf-8")
    with pytest.raises(MappingError, match="must map"):
        load_mapping(bad)
