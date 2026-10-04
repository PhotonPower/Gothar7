"""Weapons and hand items (F6): geometry from code, the item contract, committed files."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.cli import main
from gothar_chargen.gltf import Gltf
from gothar_chargen.items import (
    ITEMS,
    PROCEDURAL,
    TEXTURES,
    Mesh,
    build_items,
    circle,
    item_gltf,
    lathe,
    loft,
    procedural_texture,
    texture_file,
    validate_item,
    write_glb,
)

ITEMS_DIR = REPO_ROOT / "assets/source/items"


def codes(path: Path) -> set[str]:
    return {i.code for i in validate_item(path).issues}


@pytest.fixture
def built(tmp_path: Path) -> Path:
    """All items built into tmp_path, with empty stand-in textures."""
    build_items(tmp_path)
    (tmp_path / "textures").mkdir()
    for name in [*TEXTURES, *PROCEDURAL]:
        (tmp_path / "textures" / texture_file(name)).write_bytes(b"")
    return tmp_path


def test_committed_items_follow_the_contract_and_are_fresh():
    files = sorted(ITEMS_DIR.glob("*.glb"))
    assert {f.stem for f in files} == set(ITEMS)
    for f in files:
        report = validate_item(f)
        assert report.ok(strict=True), (f.name, report.issues)


def test_every_item_builds_lods_within_budget(built: Path):
    for item in ITEMS:
        g = Gltf.load(built / f"{item}.glb")
        assert not g.doc.get("skins")
        triangles = [
            sum(g.doc["accessors"][p["indices"]]["count"] // 3 for p in m["primitives"])
            for m in g.doc["meshes"]
        ]
        assert [m["name"] for m in g.doc["meshes"]] == [f"{item}_lod{i}" for i in range(3)]
        assert triangles[0] <= 1500 and triangles[0] >= triangles[1] >= triangles[2], item
        assert validate_item(built / f"{item}.glb").ok(strict=True), item


def test_items_are_deterministic():
    a, b = item_gltf("it_sword_old", texture_file), item_gltf("it_sword_old", texture_file)
    assert a[1] == b[1]


def test_grip_axes_of_weapons_and_the_bow():
    def lod0(item: str) -> np.ndarray:
        doc, binary = item_gltf(item, texture_file)
        g = Gltf.from_bytes(_glb(doc, binary))
        prims = g.doc["meshes"][0]["primitives"]
        return np.concatenate([np.asarray(g.accessor(p["attributes"]["POSITION"])) for p in prims])

    sword = lod0("it_sword_old")
    assert sword[:, 1].max() > 0.85 and sword[:, 1].min() > -0.15  # blade along +Y, pommel below
    blade = sword[sword[:, 1] > 0.3]
    assert np.ptp(blade[:, 2]) > 4 * np.ptp(blade[:, 0])  # the edges face +-Z, flat sides +-X
    bow = lod0("it_bow_short")
    assert bow[:, 0].max() == pytest.approx(0.1, abs=0.01)  # string side +X (towards the archer)
    assert bow[:, 0].min() > -0.04


def _glb(doc: dict, binary: bytes) -> bytes:
    from gothar_chargen.items import glb_bytes

    return glb_bytes(doc, binary)


def test_loft_rings_and_caps():
    mesh = Mesh("m")
    path = np.array([[0, 0, 0], [0, 1, 0]], dtype=float)
    loft(mesh, path, [circle(0.1, 8)] * 2)
    pos, uv, tris = mesh.arrays()
    assert len(pos) == 2 * 9 + 2 * 9  # two rings with a seam point, two caps with a centre
    assert len(tris) == 2 * 8 + 2 * 8
    radii = np.linalg.norm(pos[:18][:, [0, 2]], axis=1)
    assert np.allclose(radii, 0.1)
    assert uv.shape == (len(pos), 2)


def test_lathe_closes_to_points_without_caps():
    mesh = Mesh("m")
    lathe(mesh, [(0.0, 0.0), (0.05, 0.05), (0.0, 0.1)], 6)
    pos, _, tris = mesh.arrays()
    assert len(tris) == 2 * 2 * 6  # two bands, no caps on the closed ends
    assert pos[:, 1].min() == 0.0 and pos[:, 1].max() == pytest.approx(0.1)


def test_procedural_textures():
    for name in PROCEDURAL:
        tex = procedural_texture(name, 32)
        assert tex.shape == (32, 32, 3) and tex.min() >= 0.0 and tex.max() <= 1.0
    with pytest.raises(ValueError):
        procedural_texture("velvet")


def test_validate_item_finds_contract_errors(built: Path):
    path = built / "it_key.glb"
    assert codes(path) == set()

    # moved away from the grip and turned (longest along X): origin, axis and stale
    raw = path.read_bytes()
    js_len = struct.unpack_from("<I", raw, 12)[0]
    doc = json.loads(raw[20 : 20 + js_len])
    binary = bytearray(raw[20 + js_len + 8 :])
    for p in doc["meshes"][0]["primitives"]:
        acc = doc["accessors"][p["attributes"]["POSITION"]]
        view = doc["bufferViews"][acc["bufferView"]]
        pos = np.frombuffer(binary, np.float32, acc["count"] * 3, view["byteOffset"]).copy()
        pos = pos.reshape(-1, 3)[:, [1, 0, 2]] + [0.5, 0.0, 0.0]
        binary[view["byteOffset"] : view["byteOffset"] + pos.nbytes] = pos.astype(
            np.float32
        ).tobytes()
    doc["skins"] = [{"joints": [0]}]
    del doc["nodes"][2]["name"]
    write_glb(path, doc, bytes(binary))
    assert {
        "item.origin",
        "item.axis",
        "item.size",
        "item.stale",
        "item.skin",
        "item.lod",
    } <= codes(path)


def test_validate_item_texture_and_unknown(built: Path):
    (built / "textures" / texture_file("iron_forged")).unlink()
    assert "item.texture" in codes(built / "it_key.glb")
    other = built / "it_torch.glb"
    other.write_bytes((built / "it_key.glb").read_bytes())
    report = validate_item(other)
    assert "item.lod" in {i.code for i in report.issues}  # nodes are named after the file
    assert not report.ok(strict=True)


def test_cli_build_items_geometry_only(tmp_path: Path, capsys: pytest.CaptureFixture[str]):
    (tmp_path / "textures").mkdir()
    for name in [*TEXTURES, *PROCEDURAL]:
        (tmp_path / "textures" / texture_file(name)).write_bytes(b"")
    rc = main(["build-items", "--skip-textures", "--only", "it_apple", "--out-dir", str(tmp_path)])
    assert rc == 0
    assert (tmp_path / "it_apple.glb").is_file() and not (tmp_path / "it_key.glb").exists()
    assert "it_apple.glb" in capsys.readouterr().out
