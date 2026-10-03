"""Face morph targets (contract §6.1): the morph table and the validator rules engine relies on."""

from __future__ import annotations

import copy

import pytest

from conftest import REPO_ROOT
from gothar_chargen.faces import MAX_TARGETS, FaceError, load_morphs, parse_morphs, source_targets
from gothar_chargen.fit import check_fit
from gothar_chargen.gltf import Gltf
from gothar_chargen.human import parse_human
from gothar_chargen.meshdata import mesh_data
from gothar_chargen.postprocess import material_role
from gothar_chargen.validate import validate_gltf

FARMER = REPO_ROOT / "assets/source/characters/figures/farmer.glb"
FARMER_HEAD = REPO_ROOT / "assets/source/characters/parts/farmer/head.glb"
CONTRACT = (
    "vis_aa", "vis_ee", "vis_ih", "vis_oh", "vis_ou", "vis_mbp", "vis_fv", "vis_l",
    "blink_l", "blink_r",
    "expr_angry", "expr_friendly", "expr_fear", "expr_pain", "expr_sleep",
)  # fmt: skip


def codes(report):
    return {i.code for i in report.issues if i.level == "error"}


# --- morph table ---------------------------------------------------------------------------------


def test_contract_list(rig):
    assert rig.morph_targets == CONTRACT
    assert "vis_rest" not in rig.morph_targets  # rest = all visemes at 0 (engine, a)
    morphs = load_morphs(rig.morph_targets)
    assert [m.name for m in morphs] == list(CONTRACT)
    assert len(morphs) <= MAX_TARGETS == 16
    sources = source_targets(morphs)
    assert sources[:2] == ["viseme_aa", "viseme_E"]
    assert "eyeBlinkLeft" in sources and len(sources) == len(set(sources))
    blink_l = next(m for m in morphs if m.name == "blink_l")
    assert blink_l.mix == (("eyeBlinkLeft", 1.0),)  # ARKit left = character's left (+X)


def _table(**change) -> dict:
    data = {"version": 1, "morph": [{"name": "vis_aa", "mix": {"viseme_aa": 1.0}}]}
    data.update(change)
    return data


@pytest.mark.parametrize(
    ("data", "message"),
    [
        (_table(version=2), "version"),
        (_table(morph=[{"name": "vis_aa"}]), "mix"),
        (_table(morph=[{"name": "vis_aa", "mix": {"viseme aa": 1.0}}]), "bad MPFB target"),
        (_table(morph=[{"name": "vis_aa", "mix": {"viseme_aa": 0}}]), "weight"),
        (
            _table(morph=[{"name": "a", "mix": {"x": 1}}, {"name": "a", "mix": {"x": 1}}]),
            "duplicate",
        ),
        (_table(morph=[{"name": f"m{i}", "mix": {"x": 1}} for i in range(17)]), "limit of 16"),
    ],
)
def test_invalid_tables(data, message):
    with pytest.raises(FaceError, match=message):
        parse_morphs(data)


def test_table_must_match_contract():
    with pytest.raises(FaceError, match="differ from the rig contract"):
        parse_morphs(_table(), contract=("vis_ee",))


# --- validator: full ordered list on every primitive, at most 16 ---------------------------------


@pytest.fixture
def head() -> Gltf:
    g = Gltf.load(FARMER_HEAD)
    return Gltf(doc=copy.deepcopy(g.doc), bin=g.bin, path=FARMER_HEAD)


def test_farmer_head_has_full_set(head, rig):
    report = validate_gltf(head, rig, None, path=FARMER_HEAD)
    assert report.ok(strict=True), report.issues
    mesh = head.doc["meshes"][0]
    assert tuple(mesh["extras"]["targetNames"]) == CONTRACT
    roles = {
        material_role(head.doc["materials"][p["material"]]["name"]) for p in mesh["primitives"]
    }
    assert {"skin", "eyes", "eyebrows", "eyelashes", "teeth", "tongue"} <= roles
    for prim in mesh["primitives"]:
        assert len(prim["targets"]) == len(CONTRACT)
        assert all(set(t) == {"POSITION", "NORMAL"} for t in prim["targets"])


def test_morph_order(head, rig):
    names = head.doc["meshes"][0]["extras"]["targetNames"]
    names[0], names[1] = names[1], names[0]
    assert codes(validate_gltf(head, rig, None, path=FARMER_HEAD)) == {"morph.order"}


def test_morph_incomplete_primitive(head, rig):
    head.doc["meshes"][0]["primitives"][-1]["targets"].pop()
    assert "morph.primitives" in codes(validate_gltf(head, rig, None, path=FARMER_HEAD))


def test_morph_limit(head, rig):
    mesh = head.doc["meshes"][0]
    mesh["extras"]["targetNames"] += [f"x{i}" for i in range(2)]
    for prim in mesh["primitives"]:
        prim["targets"] += prim["targets"][:2]
    assert {"morph.count", "morph.name", "morph.order"} <= codes(
        validate_gltf(head, rig, None, path=FARMER_HEAD)
    )


# --- seams: inner face parts are not seams; recipes with teeth/tongue -----------------------------


def test_inner_parts_are_not_seams():
    g = Gltf.load(FARMER)
    head = next(i for i, n in enumerate(g.doc["nodes"]) if n.get("name") == "head_lod0")
    full = mesh_data(g, head)
    skin = mesh_data(g, head, skip_material=lambda m: material_role(m) != "skin")
    assert 0 < len(skin.positions) < len(full.positions)
    assert not [i for i in check_fit(g) if i.level == "error"]


def test_recipe_with_teeth_and_tongue():
    data = {
        "version": 1,
        "assets": {
            "skin": "s/s.mhmat",
            "eyes": "e/e.mhclo",
            "teeth": "teeth/teeth_base/teeth_base.mhclo",
            "tongue": "tongue/tongue01/tongue01.mhclo",
        },
    }
    h = parse_human(data, "npc")
    assert [t for t, _ in h.assets()] == ["Eyes", "Teeth", "Tongue"]
    assert material_role("teeth") == "teeth" and material_role("tongue.001") == "tongue"


def test_hair_lods_may_change_their_borders():
    """Loose parts (hair cards) are reduced freely; only body/head seams must stay (lod.seam)."""
    g = Gltf.load(REPO_ROOT / "assets/source/characters/figures/test_m_heavy_old.glb")
    names = {n.get("name") for n in g.doc["nodes"]}
    assert {"hair_lod0", "hair_lod2", "head_lod0", "body_lod0"} <= names
    assert not [i for i in check_fit(g) if i.level == "error"]
