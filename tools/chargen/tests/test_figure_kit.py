from __future__ import annotations

import copy
from dataclasses import replace
from pathlib import Path

import numpy as np
import pytest

from conftest import REPO_ROOT, node_index, set_accessor
from gothar_chargen.figure import FigureError, load_figure, parse_figure, srgb_to_linear
from gothar_chargen.fit import FitTolerances, check_fit, figure_parts
from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import MeshData, mesh_data, split_lod
from gothar_chargen.validate import Tolerances, is_part_file, validate_gltf

CHARACTERS = REPO_ROOT / "assets/source/characters"
FIGURES = CHARACTERS / "figures"
RAGS = FIGURES / "test_rags.glb"


@pytest.fixture(scope="module")
def rags_original() -> Gltf:
    return Gltf.load(RAGS)


@pytest.fixture
def rags(rags_original) -> Gltf:
    return Gltf(doc=copy.deepcopy(rags_original.doc), bin=rags_original.bin, path=RAGS)


def codes(report, level="error"):
    return {i.code for i in report.issues if i.level == level}


# --- manifest ------------------------------------------------------------------------------------

GOOD = {
    "version": 1,
    "lods": [1.0, 0.5, 0.2],
    "parts": {"body": "parts/a.glb", "head": "parts/h.glb", "hair": "parts/x.glb"},
    "palette": {"skin": "#ffffff", "cloth_a": "#000000"},
}


def test_parse_manifest():
    fig = parse_figure(GOOD, "npc_test_01")
    assert fig.parts["head"] == "parts/h.glb"
    assert fig.lods == (1.0, 0.5, 0.2)
    assert fig.palette["skin"] == pytest.approx((1.0, 1.0, 1.0))
    assert fig.part_paths(Path("c"))["body"] == Path("c/parts/a.glb")


def test_srgb_to_linear():
    assert srgb_to_linear("#000000") == (0.0, 0.0, 0.0)
    assert srgb_to_linear("#808080")[0] == pytest.approx(0.2158, abs=1e-3)


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"version": 2}, "version"),
        ({"extra": 1}, "unknown keys"),
        ({"parts": None}, "missing \\[parts\\]"),
        ({"parts": {"body": "a.glb"}}, "missing parts"),
        ({"parts": {"body": "a.glb", "head": "h.glb", "cape": "c.glb"}}, "unknown part role"),
        ({"parts": {"body": "a.fbx", "head": "h.glb"}}, "relative .glb"),
        ({"lods": [0.5]}, "lods"),
        ({"lods": [1.0, 0.6, 0.7]}, "lods"),
        ({"lods": [1.0, 0.5, 0.2, 0.1]}, "lods"),
        ({"palette": {"skin": "red"}}, "#rrggbb"),
        ({"palette": 3}, "palette"),
    ],
)
def test_invalid_manifests(change, message):
    with pytest.raises(FigureError, match=message):
        parse_figure({**GOOD, **change}, "npc")


def test_manifest_name_and_suffix(tmp_path):
    with pytest.raises(FigureError, match="lower_snake_case"):
        parse_figure(GOOD, "Npc-1")
    bad = tmp_path / "x.toml"
    bad.write_text("version = 1", encoding="utf-8")
    with pytest.raises(FigureError, match=".figure.toml"):
        load_figure(bad)
    broken = tmp_path / "y.figure.toml"
    broken.write_text("version = [", encoding="utf-8")
    with pytest.raises(FigureError):
        load_figure(broken)


def test_committed_manifests_have_parts_and_figures():
    manifests = sorted(FIGURES.glob("*.figure.toml"))
    assert manifests
    for m in manifests:
        fig = load_figure(m)
        assert all(p.is_file() for p in fig.part_paths(CHARACTERS).values()), m.name
        assert (FIGURES / f"{fig.name}.glb").is_file(), f"{fig.name}.glb not assembled"


# --- mesh data -----------------------------------------------------------------------------------


def test_split_lod():
    assert split_lod("head_lod1") == ("head", 1)
    assert split_lod("head") == ("head", None)
    assert split_lod("head_lodx") == ("head_lodx", None)


def test_border_of_open_and_closed_surfaces():
    # two triangles forming a square: 4 border vertices; duplicated corner is welded
    pos = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 0, 0], [1, 1, 0], [0, 1, 0]], float)
    square = MeshData(pos, np.array([[0, 1, 2], [3, 4, 5]]), None, None)
    assert len(square.border_vertices()) == 4
    tetra = MeshData(
        np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], float),
        np.array([[0, 2, 1], [0, 1, 3], [1, 2, 3], [0, 3, 2]]),
        None,
        None,
    )
    assert len(tetra.border_vertices()) == 0
    assert square.weights_of(0) == {}


# --- fit check -----------------------------------------------------------------------------------


def test_test_figures_pass(rig, reference):
    for glb in sorted(FIGURES.glob("test_*.glb")):
        g = Gltf.load(glb)
        assert check_fit(g) == [], glb.name
        report = validate_gltf(g, rig, reference, path=glb)
        assert report.ok(strict=True), report.issues
        assert report.stats["lods"] >= 2


def test_figure_parts(rags):
    parts = figure_parts(rags)
    assert {("body", 0), ("head", 2), ("hair", 1)} <= set(parts)


def _move_border_vertex(g: Gltf, name: str, offset: float, all_levels: bool = False) -> None:
    names = [
        n["name"] for n in g.doc["nodes"] if n.get("name", "").startswith(name.split("_lod")[0])
    ]
    for node_name in names if all_levels else [name]:
        idx = node_index(g, node_name)
        prim = g.doc["meshes"][g.doc["nodes"][idx]["mesh"]]["primitives"][0]
        border = mesh_data(g, idx).border_vertices()
        pos = g.accessor(prim["attributes"]["POSITION"]).copy()
        if border[0] >= len(pos):
            pytest.skip("border vertex not in the first primitive")
        p = pos[border[0]].copy()
        same = np.all(np.abs(pos - p) < 1e-6, axis=1)  # all split copies of that vertex
        pos[same] += [0.0, offset, 0.0]
        set_accessor(g, prim["attributes"]["POSITION"], pos)


def test_gap_at_the_neck(rags):
    _move_border_vertex(rags, "head_lod0", 0.03)
    issues = check_fit(rags)
    assert any(i.code == "fit.gap" and "head_lod0" in i.message for i in issues)
    assert any(i.code == "lod.seam" for i in issues)  # lod0 border no longer matches lod1/2


def test_seam_weights_differ(rags):
    idx = node_index(rags, "head_lod0")
    prim = rags.doc["meshes"][rags.doc["nodes"][idx]["mesh"]]["primitives"][0]
    weights = rags.accessor(prim["attributes"]["WEIGHTS_0"]).copy()
    joints = rags.accessor(prim["attributes"]["JOINTS_0"]).copy()
    border = mesh_data(rags, idx).border_vertices()
    # move the seam vertices of the head to a different joint (spine_03)
    skin = rags.doc["skins"][rags.doc["nodes"][idx]["skin"]]["joints"]
    spine = skin.index(node_index(rags, "spine_03"))
    for v in border:
        if v < len(joints):
            joints[v] = [spine, 0, 0, 0]
            weights[v] = [1.0, 0, 0, 0]
    set_accessor(rags, prim["attributes"]["JOINTS_0"], joints)
    set_accessor(rags, prim["attributes"]["WEIGHTS_0"], weights)
    assert any(i.code == "fit.weights" for i in check_fit(rags))


def test_lonely_open_border(rags):
    for name in ("body_lod0", "body_lod1", "body_lod2"):
        rags.doc["nodes"][node_index(rags, name)]["name"] = name.replace("body", "cape")
    issues = check_fit(rags)
    assert any(i.code == "fit.gap" and "no part to close it" in i.message for i in issues)


def test_seams_are_exact(rags):
    """Test parts are built so that seams match exactly, in every LOD level."""
    assert check_fit(rags, FitTolerances(gap=1e-6, weight=1e-6, lod_seam=1e-6)) == []


# --- LOD and budget rules ------------------------------------------------------------------------


def test_lod_gap(rags, rig, reference):
    rags.doc["nodes"][node_index(rags, "hair_lod1")]["name"] = "hair_lod3"
    assert "lod.gap" in codes(validate_gltf(rags, rig, reference, path=RAGS))


def test_morphs_only_on_lod0(rags, rig, reference):
    head0 = rags.doc["nodes"][node_index(rags, "head_lod0")]
    rags.doc["nodes"][node_index(rags, "head_lod1")]["mesh"] = head0["mesh"]
    assert "lod.morph" in codes(validate_gltf(rags, rig, reference, path=RAGS))


def test_lod_transform_and_skin(rags, rig, reference):
    rags.doc["nodes"][node_index(rags, "hair_lod1")]["translation"] = [0.0, 0.1, 0.0]
    rags.doc["skins"].append(copy.deepcopy(rags.doc["skins"][0]))
    rags.doc["skins"][-1]["joints"] = list(reversed(rags.doc["skins"][-1]["joints"]))
    rags.doc["nodes"][node_index(rags, "hair_lod2")]["skin"] = len(rags.doc["skins"]) - 1
    messages = [i.message for i in validate_gltf(rags, rig, reference, path=RAGS).errors]
    assert any("other transform" in m for m in messages)
    assert any("other skin" in m for m in messages)


def test_budget_and_ratio(rags, rig, reference):
    tight = replace(Tolerances(), figure_triangles_max=1000, lod_ratio_warn=(1.0, 0.4, 0.1))
    report = validate_gltf(rags, rig, reference, tight, path=RAGS)
    assert "mesh.budget" in codes(report)
    assert "lod.ratio" in codes(report, "warning")
    assert report.stats["triangles"] > 1000


def test_part_files_skip_figure_checks(rig, reference):
    head = CHARACTERS / "parts/test/head_test.glb"
    assert is_part_file(head) and not is_part_file(RAGS)
    report = validate_gltf(Gltf.load(head), rig, reference, path=head)
    assert report.ok(strict=True), report.issues
    assert "height" not in report.stats


# --- clothing kit (F3e) ---------------------------------------------------------------------


def test_manifest_with_garments():
    from gothar_chargen.figure import cloth_role

    fig = parse_figure(
        {
            "version": 1,
            "parts": {
                "body": "parts/body_m_average/body.glb",
                "head": "parts/head_m_mid/head.glb",
                "cloth": [
                    "parts/cloth_m_average/elvs_crude_t-shirt_male.glb",
                    "parts/cloth_m_average/culturalibre_male_boots.glb",
                ],
            },
        },
        "npc",
    )
    assert cloth_role("parts/x/elvs_crude_t-shirt_male.glb") == "cloth_elvs_crude_t_shirt_male"
    assert set(fig.parts) == {
        "body",
        "head",
        "cloth_elvs_crude_t_shirt_male",
        "cloth_culturalibre_male_boots",
    }
    for bad, message in (
        ({"cloth": "parts/x/a.glb"}, "list"),
        ({"cloth": ["parts/x/a.glb", "parts/y/a.glb"]}, "twice"),
        ({"cloth": ["/abs/a.glb"]}, "relative"),
    ):
        data = {"version": 1, "parts": {"body": "b.glb", "head": "h.glb", **bad}}
        with pytest.raises(FigureError, match=message):
            parse_figure(data, "npc")


@pytest.mark.parametrize("name", ["peasant_woman", "laborer", "guard", "old_man"])
def test_test_npcs(name, rig, reference):
    path = REPO_ROOT / f"assets/source/characters/figures/{name}.glb"
    g = Gltf.load(path)
    report = validate_gltf(g, rig, reference, path=path)
    assert report.ok(strict=True), report.issues
    nodes = {n.get("name", "") for n in g.doc["nodes"]}
    assert any(n.startswith("cloth_") and n.endswith("_lod0") for n in nodes)
    # palette colours stay a factor: the kit textures are shared and neutral
    factors = [
        m["pbrMetallicRoughness"].get("baseColorFactor")
        for m in g.doc["materials"]
        if m["name"].startswith("cloth_")
        and "baseColorTexture" in m.get("pbrMetallicRoughness", {})
    ]
    assert any(f is not None and f[:3] != [1, 1, 1] for f in factors)
    uris = {i["uri"] for i in g.doc.get("images", [])}
    assert any(u.endswith("_neutral.jpg") for u in uris)
