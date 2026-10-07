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
    blend_normals,
    bone_flow,
    dilate,
    downsample,
    euler_matrix,
    fractal3,
    height_normal,
    load_creature,
    matrix_euler,
    rasterize,
    shape_distance,
    srgb,
    surface,
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
        (
            'kind = "ridge"',
            'kind = "tooth"\nr = 0.01\na = [0, 0, 0]\nb = [0, 0, 0.1]',
            "needs a bone",
        ),
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
    tex, height = bake_texture(c, tri_uv, tri_pos, tri_nrm)
    assert height.shape == (32, 32) and np.abs(height).max() <= 2.0
    assert tex.shape == (32, 32, 3) and (tex >= 0).all() and (tex <= 1).all()
    assert tex[31, 16].sum() > 0  # dilated below the triangle (v < 0.1)
    assert tex[0, -1].sum() == 0  # far corner out of reach, no wrap-around


def test_schinder_description():
    c = load_creature(SCHINDER)
    assert c.art == "schinder" and c.material == "fur" and c.triangles == 7300
    assert {"pelvis", "spine_01", "spine_02", "neck_01", "neck_02", "head", "jaw"} <= {
        b.name for b in c.bones
    }
    for side in ("l", "r"):
        for leg in ("front", "back"):
            assert {f"{leg}_{part}_{side}" for part in ("upper", "lower", "foot")} <= {
                b.name for b in c.bones
            }
    assert any(s.kind == "cut_box" for s in c.shapes)  # mouth slit
    eyes = [s for s in c.shapes if s.kind == "eye"]
    teeth = [s for s in c.shapes if s.kind == "tooth"]
    assert len(eyes) == 2 and {s.bone for s in eyes} == {"head"}
    assert {s.bone for s in teeth} == {"head", "jaw"} and len(teeth) == 10
    assert [s.bone for s in c.shapes if s.kind == "tongue"] == ["jaw"]  # mouth interior


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
    assert report.stats["triangles"] <= 8000 and report.stats["lods"] == 3  # engine: lod0 <= 8 k
    uris = sorted(img.get("uri", "") for img in gltf.list("images"))
    folder = "../../../textures/fur/"
    assert uris == sorted(
        folder + f
        for f in (
            "schinder.jpg",
            "schinder_normal.png",
            "eyes_140d09.jpg",
            "teeth_d8cdb0.jpg",
            "tongue_7a4440.jpg",
        )
    )
    assert all((SCHINDER_REF.parent / u).is_file() for u in uris)
    materials = {m["name"]: m for m in gltf.list("materials")}
    assert set(materials) == {"fur", "eyes", "teeth", "tongue"}
    assert "normalTexture" in materials["fur"]


def test_pieces_and_boxes(tmp_path):
    extra = (
        MINIMAL
        + """
[[shape]]
kind = "eye"
mirror = true
bone = "head"
center = [0.05, -0.25, 0.62]
size = [0.01, 0.01, 0.01]
[[shape]]
kind = "tooth"
bone = "front_upper_l"
mirror = true
a = [0.1, 0.0, 0.05]
b = [0.1, -0.01, 0.0]
r = 0.005
[[shape]]
kind = "cut_box"
zone = "dark"
center = [0, -0.3, 0.6]
size = [0.05, 0.1, 0.005]
"""
    )
    c = load_creature(_write(tmp_path, extra))
    eyes = [s for s in c.shapes if s.kind == "eye"]
    teeth = [s for s in c.shapes if s.kind == "tooth"]
    assert [s.bone for s in eyes] == ["head", "head"]
    assert sorted(s.bone for s in teeth) == ["front_upper_l", "front_upper_r"]
    assert teeth[0].r2 == 0.0
    # pieces take no part in the colour zones
    names, w = zone_weights(c, np.array([[0.05, -0.55, 0.62]]))
    assert w.shape == (1, len(names))
    box = Shape("box", "fur", center=(0, 0, 0), size=(0.1, 0.2, 0.3))
    d = shape_distance(box, np.array([[0, 0, 0], [0.1, 0, 0], [0.2, 0, 0], [0.2, 0.3, 0.0]]))
    assert d == pytest.approx([-0.1, 0.0, 0.1, np.hypot(0.1, 0.1)])


def test_normals_from_height():
    flat = height_normal(np.zeros((8, 8)), 1.0)
    assert flat == pytest.approx(np.tile([0, 0, 1.0], (8, 8, 1)))
    ramp = np.tile(np.arange(8.0), (8, 1))  # rises towards +u (image x)
    n = height_normal(ramp, 1.0)[4, 4]
    assert n[0] < 0 and abs(n[1]) < 1e-12 and n[2] > 0  # tilts away from the slope
    rows = np.tile(np.arange(8.0)[:, None], (1, 8))  # rises downwards in the image = towards -v
    assert height_normal(rows, 1.0)[4, 4][1] > 0
    up = np.tile([0, 0, 1.0], (2, 2, 1))
    tilted = np.tile(np.array([0.6, 0.0, 0.8]), (2, 2, 1))
    assert blend_normals(up, tilted) == pytest.approx(tilted)
    big = np.arange(16.0).reshape(4, 4, 1)
    assert downsample(big, 2)[..., 0] == pytest.approx(np.array([[2.5, 4.5], [10.5, 12.5]]))


QUADERBUCKEL = DATA / "quaderbuckel.creature.toml"
QUADERBUCKEL_REF = (
    REPO_ROOT / "assets/source/characters/monsters/quaderbuckel/rig/quaderbuckel_reference.glb"
)


def test_plate_shell(tmp_path):
    extra = (
        MINIMAL
        + """
[[shape]]
kind = "plate_shell"
zone = "dark"
center = [0, 0.1, 0.5]
size = [0.2, 0.4, 0.2]
rows = [-0.2, 0.0, 0.2, 0.39]
angles = [-40, 0, 40]
plate = [0.05, 0.06, 0.01]
tilt = 10
bones = ["pelvis", "neck_01"]
"""
    )
    c = load_creature(_write(tmp_path, extra))
    plates = [s for s in c.shapes if s.kind == "plate"]
    assert len(plates) == 9  # the row at 0.39 lies beyond the shell's rounded end
    assert {s.bone for s in plates} <= {"pelvis", "neck_01"}
    assert not any(s.candidates for s in plates)
    # the plate on top in the middle row: lies on the shell, faces up, rear edge raised by the tilt
    top = min(plates, key=lambda s: abs(s.center[1] + 0.2) + abs(s.center[0]))  # pelvis shift
    normal = euler_matrix(top.rotate) @ np.array([0, 0, 1.0])
    along = euler_matrix(top.rotate) @ np.array([0, 1.0, 0])
    assert normal[2] > 0.95 and along[2] == pytest.approx(np.sin(np.radians(10)), abs=1e-6)
    with pytest.raises(CreatureError, match="bone or bones"):
        load_creature(_write(tmp_path, extra.replace('bones = ["pelvis", "neck_01"]', "")))


def test_matrix_euler_roundtrip():
    for e in ((10, 20, 30), (-40, 5, 170), (80, -30, -60)):
        m = euler_matrix(e)
        assert euler_matrix(matrix_euler(m)) == pytest.approx(m)


def test_strata_relief_and_tongue(tmp_path):
    dark = '[zone.dark]\ncolour = "#202020"'
    text = MINIMAL.replace(dark, '[zone.dark]\ncolour = "#a08060"\nstrata = 1.0')
    text += """
[[shape]]
kind = "tongue"
bone = "head"
center = [0, -0.4, 0.58]
size = [0.02, 0.04, 0.008]
"""
    c = load_creature(_write(tmp_path, text))
    assert [s.bone for s in c.shapes if s.kind == "tongue"] == ["head"]
    p = np.random.default_rng(2).random((400, 3)) * 0.02 + np.array([0.14, -0.3, 0.25])  # leg
    _, height = surface(c, p, np.tile([1.0, 0, 0], (400, 1)))
    assert height.std() > 0.05  # sandstone grain and chisel marks show in the relief


def test_quaderbuckel_reference_passes():
    c = load_creature(QUADERBUCKEL)
    shield = c.bone("brow_shield")
    # on the chest: stays when the neck bends the head under it (agreed with engine 2026-10-07)
    assert shield.parent == "chest"
    assert "socket_shield" in c.sockets and c.sockets["socket_shield"]["parent"] == "brow_shield"
    plates = [s for s in c.shapes if s.kind == "plate"]
    assert len(plates) == 60 and {"brow_shield", "tail_02"} <= {s.bone for s in plates}
    rig = load_rig(species="quaderbuckel")
    gltf = Gltf.load(QUADERBUCKEL_REF)
    report = validate_gltf(gltf, rig, reference_pose(gltf), path=QUADERBUCKEL_REF)
    assert report.ok(strict=True), report.issues
    assert report.stats["triangles"] <= 8000 and report.stats["lods"] == 3
    materials = {m["name"] for m in gltf.list("materials")}
    assert materials == {"fur", "eyes", "teeth", "tongue"}


GLEMSMAHR_REF = (
    REPO_ROOT / "assets/source/characters/monsters/glemsmahr/rig/glemsmahr_reference.glb"
)


def test_glemsmahr_reference_passes():
    c = load_creature(DATA / "glemsmahr.creature.toml")
    assert c.eye_glow > 0 and {"socket_eyes", "socket_hand_l", "socket_hand_r"} <= set(c.sockets)
    names = {b.name for b in c.bones}
    assert {f"front_finger_{k}_{s}" for k in (1, 2, 3) for s in "lr"} <= names
    assert {"back_toes_l", "back_toes_r", "spine_03", "neck_02"} <= names
    rig = load_rig(species="glemsmahr")
    gltf = Gltf.load(GLEMSMAHR_REF)
    report = validate_gltf(gltf, rig, reference_pose(gltf), path=GLEMSMAHR_REF)
    assert report.ok(strict=True), report.issues
    assert report.stats["triangles"] <= 8000 and report.stats["lods"] == 3
    eyes = next(m for m in gltf.list("materials") if m["name"] == "eyes")
    assert max(eyes.get("emissiveFactor", [0, 0, 0])) > 0.1  # glowing in the dark
