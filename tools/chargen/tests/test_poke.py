"""Skin through clothes (F3o): poke measurement, rule fit.poke_motion, own-skin hiding."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from gothar_chargen.gltf import Gltf
from gothar_chargen.items import glb_bytes
from gothar_chargen.partdata import hide_own_skin
from gothar_chargen.poke import PokeCheck, check_figure, poke_report, skin_matrices


class _Doc:
    """A minimal glTF: nodes root > pelvis, one skinned mesh node, accessors in one buffer."""

    def __init__(self) -> None:
        self.doc: dict = {
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "root", "children": [1]}, {"name": "pelvis"}],
            "accessors": [],
            "bufferViews": [],
            "buffers": [],
        }
        self.blob = bytearray()

    def acc(self, arr: np.ndarray, kind: str) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        data = arr.tobytes()
        self.doc["bufferViews"].append(
            {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)}
        )
        self.blob.extend(data)
        comp = {np.float32: 5126, np.uint32: 5125, np.uint16: 5123}[arr.dtype.type]
        self.doc["accessors"].append(
            {
                "bufferView": len(self.doc["bufferViews"]) - 1,
                "componentType": comp,
                "count": len(arr),
                "type": kind,
            }  # fmt: skip
        )
        return len(self.doc["accessors"]) - 1

    def gltf(self) -> Gltf:
        self.doc["buffers"] = [{"byteLength": len(self.blob)}]
        return Gltf.from_bytes(glb_bytes(self.doc, bytes(self.blob)))


def _grid(half: float, y: float, n: int = 6) -> tuple[np.ndarray, np.ndarray]:
    """n x n vertices on the square [-half, half]^2 at height y (XZ plane), normals +Y."""
    xs = np.linspace(-half, half, n)
    pos = np.array([[x, y, z] for x in xs for z in xs], dtype=np.float32)
    tris = []
    for i in range(n - 1):
        for j in range(n - 1):
            a, b, c, d = i * n + j, i * n + j + 1, (i + 1) * n + j, (i + 1) * n + j + 1
            tris += [(a, b, c), (c, b, d)]
    return pos, np.array(tris, dtype=np.uint32)


def figure(skinned: bool = True) -> Gltf:
    """Skin square (follows root) under a larger cloth square 2 cm above it (follows pelvis)."""
    d = _Doc()
    prims = []
    for half, y, joint in ((0.2, 1.0, 0), (0.4, 1.02, 1)):
        pos, tris = _grid(half, y)
        attrs = {
            "POSITION": d.acc(pos, "VEC3"),
            "NORMAL": d.acc(np.tile(np.float32([0, 1, 0]), (len(pos), 1)), "VEC3"),
        }
        if skinned:
            attrs["JOINTS_0"] = d.acc(np.tile(np.uint16([joint, 0, 0, 0]), (len(pos), 1)), "VEC4")
            attrs["WEIGHTS_0"] = d.acc(np.tile(np.float32([1, 0, 0, 0]), (len(pos), 1)), "VEC4")
        prims.append({"attributes": attrs, "indices": d.acc(tris.ravel(), "SCALAR"),
                      "material": len(prims)})  # fmt: skip
    d.doc["materials"] = [{"name": "skin"}, {"name": "cloth_test"}]
    d.doc["meshes"] = [{"name": "body_lod0", "primitives": prims}]
    node = {"name": "body_lod0", "mesh": 0}
    if skinned:
        node["skin"] = 0
        ibm = np.tile(np.eye(4, dtype=np.float32), (2, 1, 1))
        d.doc["skins"] = [{"joints": [0, 1], "inverseBindMatrices": d.acc(ibm, "MAT4")}]
    d.doc["nodes"].append(node)
    d.doc["scenes"][0]["nodes"].append(2)
    return d.gltf()


def anims() -> Gltf:
    """Set "none": s_idle lifts the pelvis (and the cloth) by 20 cm, s_walk keeps it still."""
    d = _Doc()
    times = d.acc(np.float32([0.0, 1.0]), "SCALAR")
    lift = d.acc(np.float32([[0, 0, 0], [0, 0.2, 0]]), "VEC3")
    still = d.acc(np.float32([[0, 0, 0], [0, 0, 0]]), "VEC3")
    d.doc["animations"] = [
        {"name": f"none/{name}", "samplers": [{"input": times, "output": out}],
         "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}]}
        for name, out in (("s_idle", lift), ("s_walk", still))
    ]  # fmt: skip
    return d.gltf()


def test_covered_skin_at_rest():
    check = PokeCheck(figure())
    assert int(check.covered[0].sum()) == 36  # every skin vertex lies under the cloth
    assert int(check.deep[0].sum()) == 50
    area, count, _ = check.frame(skin_matrices(check.fig, None, None, np.zeros(1))[0])
    assert area == 0.0 and count == 0


def test_cloth_lifting_off_shows_the_skin(tmp_path: Path):
    fig = tmp_path / "fig.glb"
    fig.write_bytes(figure().to_bytes())
    anim_dir = tmp_path / "anims"
    anim_dir.mkdir()
    (anim_dir / "none.glb").write_bytes(anims().to_bytes())
    results = check_figure(fig, anim_dir, ("none/s_walk", "none/s_idle"), samples=2)
    still, lifted = results
    assert still.area == 0.0
    assert lifted.area == pytest.approx(0.4 * 0.4 * 1e4, rel=1e-3)  # the whole skin square
    assert lifted.frame == pytest.approx(1.0) and lifted.bones == ["root"]
    report = poke_report(fig, results)
    assert [i.code for i in report.errors] == ["fit.poke_motion"]
    assert report.stats["poke_cm2"] == pytest.approx(1600.0, rel=1e-3)


def test_hide_own_skin_removes_covered_skin_once():
    g = figure()
    n = len(g.accessor(g.doc["meshes"][0]["primitives"][0]["indices"])) // 3
    removed = hide_own_skin(g)
    left = len(np.asarray(g.accessor(g.doc["meshes"][0]["primitives"][0]["indices"]))) // 3
    # the skin's open border counts as the neck: 5 cm around it stay, the inner 3 x 3 cells go
    assert removed == 18 and left == n - 18
    assert hide_own_skin(g) == 0
    g2 = Gltf.from_bytes(g.to_bytes())
    assert len(g2.accessor(g2.doc["meshes"][0]["primitives"][0]["indices"])) == left * 3


def test_frayed_hems_keep_the_body_under_them():
    from gothar_chargen.partdata import LodMesh, covered_triangles, hem_points

    skin_pos, skin_tris = _grid(0.2, 1.0)
    cloth_pos, cloth_tris = _grid(0.22, 1.02)
    up = np.tile([0.0, 1.0, 0.0], (len(skin_pos), 1))
    body = LodMesh("body_lod0", [skin_pos.astype(float)], [up], [skin_tris.astype(int)], ["skin"])
    up_c = np.tile([0.0, 1.0, 0.0], (len(cloth_pos), 1))
    cloth = LodMesh("c_lod0", [cloth_pos.astype(float)], [up_c], [cloth_tris.astype(int)], ["c"])
    hidden = sum(e - f for _, f, e in covered_triangles(body, cloth, []))
    assert hidden == 50  # all of the skin square lies under the cloth
    hems = hem_points(cloth)
    assert len(hems) and np.allclose(np.abs(hems[:, [0, 2]]).max(axis=1), 0.22)
    kept = sum(e - f for _, f, e in covered_triangles(body, cloth, [], keep_near=hems))
    assert 0 < kept < hidden  # the skin near the hems stays


def test_the_test_npcs_keep_their_skin_covered():
    """Rule fit.poke_motion on the built test NPCs (conftest assembles the figures)."""
    from conftest import REPO_ROOT
    from gothar_chargen.cli import POKE_FIGURES

    characters = REPO_ROOT / "assets/source/characters"
    for name in POKE_FIGURES:
        figure_path = characters / "figures" / f"{name}.glb"
        report = poke_report(figure_path, check_figure(figure_path, characters / "anims/human"))
        assert report.ok(), (name, [i.message for i in report.errors])
