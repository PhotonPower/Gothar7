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


def test_deterministic_and_stale_outputs_removed(tmp_path):
    figures = tmp_path / "figures"
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
