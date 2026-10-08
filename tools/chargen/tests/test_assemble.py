"""Figures at build time (§6.2): assembly data in the parts and the pure-Python assembler."""

from __future__ import annotations

import copy
import shutil

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.assemble import (
    GENERATOR,
    AssembleError,
    _pair_rings,
    assemble_all,
    assemble_figure,
)
from gothar_chargen.figure import load_figure
from gothar_chargen.fit import check_fit
from gothar_chargen.gltf import Gltf
from gothar_chargen.partdata import data_of, geometry_hash

CHARACTERS = REPO_ROOT / "assets/source/characters"
FIGURES = CHARACTERS / "figures"


def test_body_and_head_rings_match():
    body = data_of(Gltf.load(CHARACTERS / "parts/body_m_average/body.glb"))
    head = data_of(Gltf.load(CHARACTERS / "parts/head_m_mid/head.glb"))
    assert body["part"] == "body" and head["part"] == "head"
    for level in range(3):
        b, h = body["neck"][f"body_lod{level}"], head["neck"][f"head_lod{level}"]
        assert len(b) == len(h) > 10  # same MPFB topology: same ring
        assert all(point and all(len(pv) == 2 for pv in point) for point in b)
        assert body["falloff"][f"body_lod{level}"]
    g = Gltf.load(CHARACTERS / "parts/head_m_mid/head.glb")
    mesh = g.doc["meshes"][next(n["mesh"] for n in g.doc["nodes"] if n.get("name") == "head_lod0")]
    pos = [g.accessor(p["attributes"]["POSITION"]) for p in mesh["primitives"]]
    ring = head["neck"]["head_lod0"]
    z = [pos[pt[0][0]][pt[0][1]][2] for pt in ring]
    assert z[0] == pytest.approx(max(z), abs=1e-4)  # canonical start: the front-most point


def test_garment_masks_fit_their_body():
    body = Gltf.load(CHARACTERS / "parts/body_m_heavy/body.glb")
    for garment in sorted((CHARACTERS / "parts/cloth_m_heavy").glob("*.glb")):
        covers = data_of(Gltf.load(garment))["covers"]
        assert covers["body"] == "parts/body_m_heavy/body.glb"
        assert covers["body_hash"] == geometry_hash(body)
        assert set(covers["lods"]) == {"body_lod0", "body_lod1", "body_lod2"}
        for ranges in covers["lods"].values():
            assert all(first < end for _, first, end in ranges)


def test_assembled_figures_are_marked_and_closed():
    for name in ("guard", "test_m_heavy_old"):
        g = Gltf.load(FIGURES / f"{name}.glb")
        assert g.doc["asset"]["generator"] == GENERATOR
        assert len(g.doc["asset"]["extras"]["gothar"]["inputs"]) == 16
        assert not [i for i in check_fit(g) if i.level == "error"]
        assert len(g.doc["skins"]) == 1
        names = {n.get("name") for n in g.doc["nodes"]}
        assert {"body_lod0", "head_lod0", "head_lod2"} <= names


def test_garments_hide_body_triangles():
    guard = load_figure(FIGURES / "guard.figure.toml")
    plain = copy.deepcopy(guard)
    plain.parts.clear()
    plain.parts.update({k: v for k, v in guard.parts.items() if not k.startswith("cloth_")})

    def body_triangles(fig) -> int:
        g = assemble_figure(fig, FIGURES / "guard.figure.toml", CHARACTERS, FIGURES / "x.glb")
        mesh = g.doc["meshes"][
            next(n["mesh"] for n in g.doc["nodes"] if n.get("name") == "body_lod0")
        ]
        return sum(g.doc["accessors"][p["indices"]]["count"] // 3 for p in mesh["primitives"])

    assert body_triangles(guard) < body_triangles(plain) - 1000


@pytest.fixture
def repo_tmp():
    """Scratch folder inside the repository (build/ is ignored): texture paths of a figure are
    relative, which needs the same drive as the parts (CI runners check out on D:)."""
    path = REPO_ROOT / "build" / "pytest-figures"
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    yield path
    shutil.rmtree(path, ignore_errors=True)


def test_deterministic_and_stale_outputs_removed(repo_tmp):
    figures = repo_tmp / "figures"
    figures.mkdir()
    for name in ("test_plain", "guard"):
        shutil.copy(FIGURES / f"{name}.figure.toml", figures)
    (figures / "gone.glb").write_bytes(b"old")
    (figures / "placeholder_mannequin.glb").write_bytes(b"kept")
    first = {p.name: p.read_bytes() for p in assemble_all(figures, CHARACTERS)}
    assert set(first) == {"test_plain.glb", "guard.glb"}
    assert not (figures / "gone.glb").exists()
    assert (figures / "placeholder_mannequin.glb").read_bytes() == b"kept"
    second = {p.name: p.read_bytes() for p in assemble_all(figures, CHARACTERS)}
    assert first == second  # deterministic (texture paths are relative to the output folder)


def test_stale_mask_is_an_error(tmp_path):
    figure = load_figure(FIGURES / "laborer.figure.toml")
    parts = tmp_path / "parts"
    shutil.copytree(CHARACTERS / "parts" / "cloth_m_heavy", parts / "cloth_m_heavy")
    shutil.copytree(CHARACTERS / "parts" / "body_m_heavy", parts / "body_m_heavy")
    shutil.copytree(CHARACTERS / "parts" / "head_m_young", parts / "head_m_young")
    shutil.copytree(CHARACTERS / "parts" / "hair_m_young", parts / "hair_m_young")  # stubble
    shirt = parts / "cloth_m_heavy/elvs_crude_t-shirt_male.glb"
    g = Gltf.load(shirt)
    g.doc["asset"]["extras"]["gothar"]["covers"]["body_hash"] = "0" * 16
    shirt.write_bytes(g.to_bytes())
    with pytest.raises(AssembleError, match="part-data"):
        assemble_figure(figure, FIGURES / "laborer.figure.toml", tmp_path, tmp_path / "x.glb")


def test_ring_pairing_finds_shift_and_direction():
    angles = np.linspace(0, 2 * np.pi, 12, endpoint=False)
    ring = np.stack([np.cos(angles), np.zeros(12), np.sin(angles)], axis=1)
    shifted = np.roll(ring[::-1], 5, axis=0) * 1.01
    pairing = _pair_rings(ring, shifted)
    assert np.allclose(shifted[pairing], ring * 1.01)


def test_tight_armour_hides_the_baked_trousers(monkeypatch):
    """Garments baked into a base body (its trousers) may stick out further under tight armour
    trousers than skin (POKE_THROUGH_CLOTH); with the skin limit they would poke through."""
    from gothar_chargen import partdata

    body_rel = "parts/body_m_average/body.glb"
    body = Gltf.load(CHARACTERS / body_rel)
    prim = partdata.lod_meshes(body)[0].materials.index("cloth_toigo_wool_pants")
    trousers = Gltf.load(CHARACTERS / "parts/armor_m_average/wrapped_trousers.glb")

    def hidden() -> int:
        covers = partdata.garment_data(trousers, body, body_rel)["covers"]["lods"]["body_lod0"]
        return sum(end - first for p, first, end in covers if p == prim)

    loose = hidden()
    monkeypatch.setattr(partdata, "POKE_THROUGH_CLOTH", partdata.POKE_THROUGH)
    assert loose > hidden() + 100


def test_headgear_hides_the_hair(repo_tmp):
    """Worn pieces drop whole roles (`hides`, §6.2): the union over all pieces; only hair/beard."""
    g = Gltf.load(FIGURES / "test_armor_medium_m.glb")
    names = {str(n.get("name")) for n in g.doc["nodes"]}
    assert "cloth_nasal_helmet_lod0" in names
    assert not any(n.startswith("hair_") for n in names)
    assert "head_lod0" in names
    helmet = CHARACTERS / "parts/headgear_m_average/nasal_helmet.glb"
    assert data_of(Gltf.load(helmet))["hides"] == ["hair"]
    # a piece may only hide hair or beard
    bad = Gltf.load(helmet)
    bad.doc["asset"]["extras"]["gothar"]["hides"] = ["body"]
    path = repo_tmp / "bad_helmet.glb"
    path.write_bytes(bad.to_bytes())
    figure = load_figure(FIGURES / "test_armor_medium_m.figure.toml")
    figure.parts["cloth_nasal_helmet"] = str(path)
    with pytest.raises(AssembleError, match="hides only"):
        assemble_figure(figure, FIGURES / "test_armor_medium_m.figure.toml", CHARACTERS, path)


def test_heads_sit_at_the_neck_of_the_bodies():
    """Heads are built at the neck height of the base bodies (age changes the MakeHuman stature):
    every head fits every body of its sex without stretching the neck."""
    from gothar_chargen.partdata import lod_meshes

    def ring_height(path, part):
        g = Gltf.load(path)
        mesh = lod_meshes(g)[0]
        ring = data_of(g)["neck"][f"{part}_lod0"]
        return np.mean([mesh.positions[p[0][0]][p[0][1]][1] for p in ring])

    bodies = [ring_height(p, "body") for p in sorted(CHARACTERS.glob("parts/body_*/body.glb"))]
    heads = [ring_height(p, "head") for p in sorted(CHARACTERS.glob("parts/head_*/head.glb"))]
    assert len(heads) >= 5
    assert max(abs(h - np.mean(bodies)) for h in heads) < 0.003


def test_neck_lift_is_an_error(monkeypatch):
    import gothar_chargen.assemble as assemble

    monkeypatch.setattr(assemble, "NECK_LIFT_MAX", -1.0)
    figure = load_figure(FIGURES / "test_plain.figure.toml")
    with pytest.raises(AssembleError, match="above the neck"):
        assemble_figure(figure, FIGURES / "test_plain.figure.toml", CHARACTERS, FIGURES / "x.glb")


def test_eye_height_rule():
    from gothar_chargen.fit import FitTolerances

    g = Gltf.load(FIGURES / "guard.glb")
    assert not [i for i in check_fit(g) if i.code == "head.eyes"]
    issues = check_fit(g, FitTolerances(eye_height=1.5))
    assert [i.code for i in issues if i.level == "error"] == ["head.eyes"]


def test_role_order_is_fixed(repo_tmp):
    """body, head, hair, beard, then the garments: the key order of a manifest does not matter
    (the engine assembles in the same fixed order, M6 D2)."""
    text = (FIGURES / "guard.figure.toml").read_text(encoding="utf-8")
    head, parts = text.split("[parts]\n")
    lines = parts.split("\n")
    hair = next(i for i, line in enumerate(lines) if line.startswith("hair ="))
    reordered = [lines[hair], *lines[:hair], *lines[hair + 1 :]]  # hair before body and head
    manifest = repo_tmp / "guard.figure.toml"
    manifest.write_text(head + "[parts]\n" + "\n".join(reordered), encoding="utf-8")
    a = assemble_figure(
        load_figure(FIGURES / "guard.figure.toml"),
        FIGURES / "guard.figure.toml",
        CHARACTERS,
        repo_tmp / "a.glb",
    )
    b = assemble_figure(load_figure(manifest), manifest, CHARACTERS, repo_tmp / "a.glb")
    for doc in (a.doc, b.doc):
        doc["asset"]["extras"]["gothar"].pop("inputs")  # hashes the manifest text
    assert a.doc == b.doc and a.bin == b.bin


def test_mesh_names_follow_node_names():
    """Blender numbers mesh data blocks per run; part-data names them after their nodes."""
    for path in sorted(CHARACTERS.glob("parts/*/*.glb")):
        g = Gltf.load(path)
        for node in g.doc["nodes"]:
            if "mesh" in node:
                assert g.doc["meshes"][node["mesh"]].get("name") == node["name"], path


def test_no_body_primitive_is_hidden_completely():
    """The C++ figure assembly (engine) keeps the vertices of a body primitive whose triangles are
    all hidden, assemble.py drops the primitive: both must stay equal, so no figure may hide a
    whole body primitive (e.g. the body's underwear under a long skirt with `[inside]`)."""
    from gothar_chargen.partdata import lod_meshes

    for manifest in sorted((CHARACTERS / "figures").glob("*.figure.toml")):
        figure = load_figure(manifest)
        if "body" not in figure.parts:
            continue
        body = Gltf.load(CHARACTERS / figure.parts["body"])
        garments = [
            Gltf.load(CHARACTERS / p) for r, p in figure.parts.items() if r.startswith("cloth")
        ]
        for mesh in lod_meshes(body).values():
            hidden = [np.zeros(len(t), dtype=bool) for t in mesh.triangles]
            for g in garments:
                for prim, first, end in (
                    data_of(g).get("covers", {}).get("lods", {}).get(mesh.node, [])
                ):
                    hidden[prim][first:end] = True
            for prim, flags in enumerate(hidden):
                assert not (len(flags) and flags.all()), (manifest.stem, mesh.node, prim)


def test_baked_head_beards_do_not_take_the_kit_beard_material():
    """A beard baked into a head part is called beard_head: assemble merges materials by name with
    the head first, so a head material "beard" would replace a kit beard's (fixed 2026-10-08)."""
    for head in sorted((CHARACTERS / "parts").glob("head_*/head.glb")):
        names = [m.get("name") for m in Gltf.load(head).list("materials")]
        assert "beard" not in names, head.parent.name
    for name in ("head_m_mid", "head_m_old"):
        names = [
            m.get("name")
            for m in Gltf.load(CHARACTERS / f"parts/{name}/head.glb").list("materials")
        ]
        assert "beard_head" in names, name
