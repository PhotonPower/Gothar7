"""Own monsters from body descriptions (F5, creature.py, gothar-chargen creature)."""

from __future__ import annotations

import textwrap
from pathlib import Path

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.creature import (
    CreatureError,
    Shape,
    bake_texture,
    bone_flow,
    dilate,
    euler_matrix,
    fractal3,
    load_creature,
    rasterize,
    shape_distance,
    srgb,
    surface_colour,
    zone_weights,
)
from gothar_chargen.gltf import Gltf
from gothar_chargen.skeleton import load_rig, packaged_species
from gothar_chargen.validate import reference_pose, validate_gltf

DATA = REPO_ROOT / "tools/chargen/src/gothar_chargen/data/monsters"
SCHINDER = DATA / "schinder.creature.toml"
SCHINDER_REF = REPO_ROOT / "assets/source/characters/monsters/schinder/rig/schinder_reference.glb"

MINIMAL = """
art = "test"
triangles = 1000
[sockets]
socket_mouth = { parent = "head", length = 0.05 }
[[bone]]
name = "pelvis"
head = [0, 0.3, 0.5]
tail = [0, 0.1, 0.5]
[[bone]]
name = "neck_01"
parent = "pelvis"
head = [0, 0.1, 0.5]
tail = [0, -0.1, 0.6]
[[bone]]
name = "head"
parent = "neck_01"
head = [0, -0.1, 0.6]
tail = [0, -0.3, 0.6]
[[bone]]
name = "front_upper_l"
parent = "pelvis"
mirror = true
head = [0.1, 0.0, 0.5]
tail = [0.1, 0.0, 0.0]
[zone.fur]
colour = "#806040"
[zone.dark]
colour = "#202020"
[[shape]]
kind = "ellipsoid"
zone = "fur"
center = [0, 0.1, 0.5]
size = [0.2, 0.4, 0.2]
[[shape]]
kind = "chain"
zone = "dark"
mirror = true
points = [[0.1, 0.0, 0.5], [0.1, 0.0, 0.25], [0.1, 0.0, 0.02]]
radii = [0.05, 0.04, 0.03]
[[shape]]
kind = "ridge"
zone = "dark"
points = [[0, 0.3, 0.7], [0, -0.1, 0.7]]
sizes = [[0.01, 0.02, 0.03], [0.01, 0.02, 0.05]]
count = 5
"""


def _write(tmp_path: Path, text: str) -> Path:
    path = tmp_path / "test.creature.toml"
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def test_minimal_description(tmp_path):
    c = load_creature(_write(tmp_path, MINIMAL))
    names = [b.name for b in c.bones]
    assert names == ["pelvis", "neck_01", "head", "front_upper_l", "front_upper_r"]
    right = c.bone("front_upper_r")
    assert right.parent == "pelvis" and right.head[0] == pytest.approx(-0.1)
    # everything is shifted so that the pelvis head stands above the origin
    assert c.bone("pelvis").head == pytest.approx((0, 0, 0.5))
    assert c.bone("head").tail == pytest.approx((0, -0.6, 0.6))
    # 1 ellipsoid + 2 capsules per leg side (mirrored) + 5 ridge ellipsoids
    assert len(c.shapes) == 1 + 2 * 2 + 5
    kinds = [s.kind for s in c.shapes]
    assert kinds.count("capsule") == 4 and kinds.count("ellipsoid") == 6
    ridge = [s for s in c.shapes if s.zone == "dark" and s.kind == "ellipsoid"]
    assert ridge[0].size[2] == pytest.approx(0.03) and ridge[-1].size[2] == pytest.approx(0.05)
    assert c.lods == (1.0, 0.5, 0.25) and c.material == "fur" and c.ao == 0.0


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ('name = "head"\nparent', 'name = "skull"\nparent', "required bone head"),
        ('parent = "neck_01"', 'parent = "nope"', "unknown parent"),
        ('socket_mouth = { parent = "head"', 'socket_eye = { parent = "head"', "socket_mouth"),
        ('zone = "dark"\nmirror', 'zone = "nope"\nmirror', "unknown zone"),
        ('kind = "ellipsoid"', 'kind = "torus"', "kind"),
        ("radii = [0.05, 0.04, 0.03]", "radii = [0.05, 0.04]", "one radius per point"),
        ('name = "front_upper_l"', 'name = "front_upper"', "must end in _l"),
        ("triangles = 1000", "triangles = 1000\nlods = [1.0, 0.6, 0.7]", "decrease"),
    ],
)
def test_invalid_descriptions(tmp_path, old, new, message):
    assert MINIMAL.count(old) == 1
    with pytest.raises(CreatureError, match=message):
        load_creature(_write(tmp_path, MINIMAL.replace(old, new)))


def test_shape_distances():
    ell = Shape("ellipsoid", "fur", center=(0, 0, 1), size=(0.1, 0.2, 0.3))
    p = np.array([[0, 0, 1.0], [0.1, 0, 1], [0, 0, 1.6]])
    d = shape_distance(ell, p)
    assert d[0] < 0 and d[1] == pytest.approx(0, abs=1e-9) and d[2] > 0
    cap = Shape("capsule", "fur", a=(0, 0, 0), b=(0, 0, 1), r=0.2, r2=0.1)
    d = shape_distance(cap, np.array([[0.2, 0, 0], [0.1, 0, 1], [0.15, 0, 0.5], [0, 0, -0.5]]))
    assert d[:3] == pytest.approx([0, 0, 0], abs=1e-9) and d[3] == pytest.approx(0.3)
    # rotated ellipsoid: the long axis turned from z to y by 90 degrees about x
    rot = Shape("ellipsoid", "fur", center=(0, 0, 0), size=(0.1, 0.1, 0.5), rotate=(90, 0, 0))
    assert shape_distance(rot, np.array([[0, 0.5, 0]]))[0] == pytest.approx(0, abs=1e-9)


def test_euler_matches_blender_convention():
    # Blender XYZ Euler: R = Rz @ Ry @ Rx; +90 deg about z turns +x into +y
    assert euler_matrix((0, 0, 90)) @ np.array([1, 0, 0]) == pytest.approx([0, 1, 0], abs=1e-12)
    m = euler_matrix((30, 40, 50))
    assert m @ m.T == pytest.approx(np.eye(3), abs=1e-12)


def test_zones_and_flow(tmp_path):
    c = load_creature(_write(tmp_path, MINIMAL))
    p = np.array([[0, -0.2, 0.7], [0.1, 0.0, 0.1], [0.0, 0.2, 0.31]])  # shifted coordinates
    names, w = zone_weights(c, p)
    assert w.sum(axis=1) == pytest.approx(np.ones(3))
    assert names[int(np.argmax(w[1]))] == "dark"  # on the leg
    assert names[int(np.argmax(w[2]))] == "fur"  # under the belly
    flow = bone_flow(c.bones, p)
    assert np.abs(flow[1]) == pytest.approx([0, 0, 1], abs=1e-9)  # the leg points down


def test_noise_is_deterministic_and_bounded():
    p = np.random.default_rng(1).random((500, 3)) * 10
    a, b = fractal3(p, 3), fractal3(p, 3)
    assert np.array_equal(a, b) and a.min() >= 0 and a.max() <= 1
    assert not np.array_equal(a, fractal3(p, 4))
    assert srgb("#ff8000") == pytest.approx([1.0, 128 / 255, 0.0])


def test_colours_follow_the_zones(tmp_path):
    c = load_creature(_write(tmp_path, MINIMAL))
    p = np.array([[0.0, -0.2, 0.7], [0.1, 0.0, 0.1]])
    n = np.array([[0.0, 0.0, 1.0], [1.0, 0.0, 0.0]])
    col = surface_colour(c, p, n)
    assert col.shape == (2, 3) and (col >= 0).all() and (col <= 1).all()
    assert col[1].sum() < 0.5 < col[0].sum()  # dark leg, brown back


def test_rasterize_and_dilate():
    # one triangle covering the lower left half of the UV square
    tri_uv = np.array([[[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]]])
    tri_pos = np.array([[[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]])
    tri_nrm = np.tile([0.0, 0.0, 1.0], (1, 3, 1))
    pos, nrm, mask = rasterize(tri_uv, tri_pos, tri_nrm, 16)
    assert 0.4 < mask.mean() < 0.65
    assert mask[-1, 0] and not mask[0, -1]  # image row 0 = v 1
    assert pos[-1, 0, :2] == pytest.approx([0.5 / 16, 0.5 / 16])
    assert nrm[mask] == pytest.approx(np.tile([0, 0, 1.0], (mask.sum(), 1)))
    img = np.where(mask[..., None], 1.0, 0.0)
    grown = dilate(img, mask, steps=3)
    assert grown[0, -1].sum() == 0  # far corner still empty
    assert (grown[mask] == 1).all() and (grown > 0).mean() > mask.mean()


def test_bake_texture_small(tmp_path):
    c = load_creature(
        _write(tmp_path, MINIMAL.replace("triangles = 1000", "triangles = 1000\ntexture = 32"))
    )
    tri_uv = np.array([[[0.1, 0.1], [0.9, 0.1], [0.1, 0.9]]])
    tri_pos = np.array([[[0.0, -0.2, 0.7], [0.0, 0.2, 0.7], [0.0, -0.2, 0.69]]])
    tri_nrm = np.tile([0.0, 0.0, 1.0], (1, 3, 1))
    tex = bake_texture(c, tri_uv, tri_pos, tri_nrm)
    assert tex.shape == (32, 32, 3) and (tex >= 0).all() and (tex <= 1).all()
    assert tex[31, 16].sum() > 0  # dilated below the triangle (v < 0.1)
    assert tex[0, -1].sum() == 0  # far corner out of reach, no wrap-around


def test_schinder_description():
    c = load_creature(SCHINDER)
    assert c.art == "schinder" and c.material == "fur" and c.triangles == 7500
    assert {"pelvis", "spine_01", "spine_02", "neck_01", "neck_02", "head", "jaw"} <= {
        b.name for b in c.bones
    }
    for side in ("l", "r"):
        for leg in ("front", "back"):
            assert {f"{leg}_{part}_{side}" for part in ("upper", "lower", "foot")} <= {
                b.name for b in c.bones
            }
    assert any(s.kind == "cut" for s in c.shapes)  # mouth slit


def test_creature_descriptions_are_no_rigs():
    assert "schinder" in packaged_species()
    assert not any(s.endswith(".creature") for s in packaged_species())


def test_schinder_rig_matches_description():
    c = load_creature(SCHINDER)
    rig = load_rig(species="schinder")
    bones = {b.name: b for b in rig.bones}
    assert set(bones) == {b.name for b in c.bones} | {"root", "socket_mouth"}
    for b in c.bones:
        assert bones[b.name].parent == (b.parent or "root")
        assert bones[b.name].head == pytest.approx(b.head, abs=1e-5)
    assert rig.collision is not None and rig.collision.shape == "capsule_lying"


def test_schinder_reference_passes():
    rig = load_rig(species="schinder")
    gltf = Gltf.load(SCHINDER_REF)
    report = validate_gltf(gltf, rig, reference_pose(gltf), path=SCHINDER_REF)
    assert report.ok(strict=True), report.issues
    assert report.stats["triangles"] <= 7500 and report.stats["lods"] == 3
    uris = [img.get("uri", "") for img in gltf.list("images")]
    assert uris == ["../../../textures/fur/schinder.jpg"]
    assert (SCHINDER_REF.parent / uris[0]).is_file()
