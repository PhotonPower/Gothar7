from __future__ import annotations

import re
import tomllib
from pathlib import Path

import pytest

from conftest import REPO_ROOT
from gothar_chargen.skeleton import SkeletonError, load_rig, parse_rig

_FINGERS = ("thumb", "index", "middle", "ring", "pinky")

# The contract as written in docs/modules/animation.md ("Referenz-Skelett").
CONTRACT: dict[str, str | None] = {
    "root": None,
    "pelvis": "root",
    "spine_01": "pelvis",
    "spine_02": "spine_01",
    "spine_03": "spine_02",
    "neck": "spine_03",
    "head": "neck",
    "socket_helmet": "head",
    "socket_back_2h": "spine_03",
    "socket_back_bow": "spine_03",
    "socket_quiver": "spine_03",
    "socket_hip_1h": "pelvis",
}
for _s in ("l", "r"):
    CONTRACT |= {
        f"clavicle_{_s}": "spine_03",
        f"upperarm_{_s}": f"clavicle_{_s}",
        f"lowerarm_{_s}": f"upperarm_{_s}",
        f"hand_{_s}": f"lowerarm_{_s}",
        f"socket_hand_{_s}": f"hand_{_s}",
        f"thigh_{_s}": "pelvis",
        f"calf_{_s}": f"thigh_{_s}",
        f"foot_{_s}": f"calf_{_s}",
        f"ball_{_s}": f"foot_{_s}",
    }
    for _f in _FINGERS:
        CONTRACT[f"{_f}_01_{_s}"] = f"hand_{_s}"
        CONTRACT[f"{_f}_02_{_s}"] = f"{_f}_01_{_s}"
        CONTRACT[f"{_f}_03_{_s}"] = f"{_f}_02_{_s}"


def test_packaged_rig_matches_contract():
    rig = load_rig()
    assert rig.parents == CONTRACT
    assert len(rig.bones) <= rig.max_bones == 128
    assert rig.max_influences == 4
    assert rig.bind_pose == "T"


def test_sockets_flagged():
    rig = load_rig()
    assert rig.sockets == {n for n in CONTRACT if n.startswith("socket_")}


def test_contract_bones_named_in_docs():
    """Every non-finger bone of the contract appears in docs/modules/animation.md."""
    text = (REPO_ROOT / "docs/modules/animation.md").read_text(encoding="utf-8")
    words = set(re.findall(r"[a-z_0-9]+", text))
    for name in CONTRACT:
        if name.startswith(_FINGERS):
            continue
        assert name in words or name.replace("_r", "_l") in words, name


def test_morph_targets_match_docs():
    text = (REPO_ROOT / "docs/design/characters-pipeline.md").read_text(encoding="utf-8")
    documented = set(re.findall(r"`((?:vis|blink|expr)_[a-z]+)`", text))
    assert set(load_rig().morph_targets) == documented


def test_mirrored_bones_are_symmetric():
    rig = load_rig()
    left, right = rig.bone("socket_hand_l"), rig.bone("socket_hand_r")
    assert right.head == (-left.head[0], left.head[1], left.head[2])
    assert left.up is not None and right.up == (-left.up[0], left.up[1], left.up[2])
    assert right.parent == "hand_r"
    assert right.socket


def test_mirror_negates_roll():
    rig = parse_rig({"rig": {}, "bone": [_bone("a"), _bone("arm_l", "a", roll=0.5, mirror=True)]})
    assert rig.bone("arm_l").roll == 0.5
    assert rig.bone("arm_r").roll == -0.5
    assert rig.bone("arm_r").up is None


def test_body_geometry_from_quaternius():
    """Body bones carry an explicit roll (taken from the Quaternius rig), sockets an up vector."""
    rig = load_rig()
    for b in rig.bones:
        if b.socket:
            assert b.up is not None and b.roll is None, b.name
        else:
            assert b.roll is not None and b.up is None, b.name
    assert rig.bone("upperarm_l").head[0] > 0.15  # left = +X
    assert abs(rig.height - 1.83) < 1e-9


def test_parents_precede_children():
    seen: set[str] = set()
    for b in load_rig().bones:
        assert b.parent is None or b.parent in seen
        seen.add(b.name)


def test_load_from_path(tmp_path: Path):
    src = REPO_ROOT / "tools/chargen/src/gothar_chargen/data/human_reference.toml"
    copy = tmp_path / "rig.toml"
    copy.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
    assert load_rig(copy).names == load_rig().names


def _bone(name: str, parent: str | None = None, **extra) -> dict:
    b = {"name": name, "head": [0, 0, 0], "tail": [0, 0, 1], **extra}
    if "roll" not in extra:
        b["up"] = [0, 1, 0]
    if parent:
        b["parent"] = parent
    return b


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ({"bone": []}, "missing \\[rig\\]"),
        ({"rig": {}, "bone": [_bone("a"), _bone("a", "a")]}, "duplicate"),
        ({"rig": {}, "bone": [_bone("b", "a"), _bone("a")]}, "defined before"),
        ({"rig": {}, "bone": [_bone("a"), _bone("b")]}, "one root"),
        ({"rig": {}, "bone": [_bone("a"), _bone("arm", "a", mirror=True)]}, "_l"),
        (
            {
                "rig": {},
                "bone": [{"name": "a", "head": [0, 0], "tail": [0, 0, 1], "up": [0, 1, 0]}],
            },
            "3 numbers",
        ),
        ({"rig": {}, "bone": [{"head": [0, 0, 0]}]}, "missing name"),
        ({"rig": {}, "bone": [_bone("a", up=[0, 1, 0], roll=0.0)]}, "exactly one"),
        ({"rig": {}, "bone": [{"name": "a", "head": [0, 0, 0], "tail": [0, 0, 1]}]}, "exactly one"),
    ],
)
def test_invalid_definitions(data, message):
    with pytest.raises(SkeletonError, match=message):
        parse_rig(data)


def test_invalid_toml(tmp_path: Path):
    bad = tmp_path / "bad.toml"
    bad.write_text("[rig\n", encoding="utf-8")
    with pytest.raises(SkeletonError):
        load_rig(bad)


def test_toml_is_valid_for_blender_python():
    """Blender 4.5 ships Python 3.11: the data file must parse with plain tomllib."""
    path = REPO_ROOT / "tools/chargen/src/gothar_chargen/data/human_reference.toml"
    assert "bone" in tomllib.loads(path.read_text(encoding="utf-8"))
