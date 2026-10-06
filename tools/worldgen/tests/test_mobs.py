import subprocess
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.collision import body_is_closed
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.buildings.gltf_scene import accessor
from gothar_worldgen.mobs import (
    ANVIL_TOP,
    BED_LIE,
    BUDGET,
    BUILDERS,
    CHEST_BODY_H,
    EMISSIVE,
    HEARTH_D,
    HEARTH_H,
    HEARTH_W,
    HOOD_Y1,
    LID_PIVOT,
    PROPS,
    TYPES,
    write_mobs,
)

REPO = Path(__file__).resolve().parents[3]
ASSETS = REPO / "assets" / "source" / "mobs"
PROP_ASSETS = REPO / "assets" / "source" / "props"
TEXTURES = REPO / "assets" / "source" / "furniture" / "textures"  # shared by mobs and props


def _bounds(model) -> tuple[np.ndarray, np.ndarray]:
    pts = [b.mesh().positions for b in model.main.builders.values()]
    pts += [b.mesh().positions + np.asarray(pivot, np.float32) for _, pivot, m in model.parts
            for b in m.builders.values()]  # fmt: skip
    allp = np.vstack(pts)
    return allp.min(axis=0), allp.max(axis=0)


def test_sizes_follow_the_contract():
    lo, hi = _bounds(BUILDERS["chest"]())
    assert hi[0] - lo[0] == pytest.approx(0.98, abs=0.03)  # 0.9 m + handles
    assert lo[1] == pytest.approx(0.0) and hi[1] == pytest.approx(0.6, abs=0.01)
    assert hi[2] == pytest.approx(0.33, abs=0.01)  # front at z = 0.3 (+ fittings)
    lo, hi = _bounds(BUILDERS["anvil"]())
    assert hi[1] == pytest.approx(ANVIL_TOP) and hi[0] > 0.4  # horn towards +X
    lo, hi = _bounds(BUILDERS["bed"]())
    assert (hi - lo)[0] == pytest.approx(2.0, abs=0.01) and (hi - lo)[2] == pytest.approx(
        0.9, abs=0.01
    )
    lo, hi = _bounds(BUILDERS["bench"]())
    assert hi[1] == pytest.approx(0.45) and (hi - lo)[0] == pytest.approx(1.5)
    assert (hi - lo)[2] == pytest.approx(0.35) and lo[0] == pytest.approx(-hi[0])  # centred
    lo, hi = _bounds(BUILDERS["table"]())
    assert hi[1] == pytest.approx(0.75) and (hi - lo)[0] == pytest.approx(1.6)
    assert (hi - lo)[2] == pytest.approx(0.8) and lo[0] == pytest.approx(-hi[0]) and lo[1] == 0.0
    lo, hi = _bounds(BUILDERS["door"]())
    assert lo[0] == pytest.approx(0.0) and hi[0] == pytest.approx(1.0)  # from the hinge along +X
    assert hi[1] == pytest.approx(2.0)


def test_bed_head_is_at_minus_x_and_the_lying_surface_at_045():
    m = BUILDERS["bed"]()
    straw = m.main.builders["straw"].mesh().positions
    assert straw[:, 1].max() == pytest.approx(BED_LIE)
    linen = m.main.builders["linen"].mesh().positions  # the pillow
    assert linen[:, 0].mean() < -0.5


def test_chest_lid_turns_about_its_hinge_with_its_collision():
    m = BUILDERS["chest"]()
    ((name, pivot, lid),) = m.parts
    assert name == "MOB_LID" and pivot == LID_PIVOT
    lid_pts = np.vstack([b.mesh().positions for b in lid.builders.values()])
    assert lid_pts[:, 2].min() >= -1e-6 and lid_pts[:, 1].min() >= -0.081  # hinge at the back top
    doc, _ = read_glb(m.glb())
    nodes = {n["name"]: n for n in doc["nodes"]}
    assert nodes["MOB_LID"]["translation"] == list(LID_PIVOT)
    children = [doc["nodes"][i]["name"] for i in nodes["MOB_LID"]["children"]]
    assert children == ["COL_HULL_lid"]
    roots = [doc["nodes"][i]["name"] for i in doc["scenes"][0]["nodes"]]
    assert roots == ["chest", "COL_HULL_chest", "MOB_LID"]
    assert pivot[1] == CHEST_BODY_H


@pytest.mark.parametrize("kind", TYPES)
def test_models_budget_collision_and_textures(kind):
    m = BUILDERS[kind]()
    assert 0 < m.triangles() <= BUDGET
    bodies = list(m.main.collision) + [c for _, _, p in m.parts for c in p.collision]
    assert bodies and all(body_is_closed(b) for b in bodies)
    doc, binary = read_glb(m.glb())
    assert all(i["uri"].startswith("../furniture/textures/") for i in doc["images"])
    for mesh in doc["meshes"]:
        for prim in mesh["primitives"]:
            if "material" in prim:
                n = accessor(doc, binary, prim["attributes"]["NORMAL"])
                assert np.allclose(np.linalg.norm(n, axis=1), 1.0, atol=1e-4)


def test_versioned_models_are_current(tmp_path: Path):
    write_mobs(tmp_path / "mobs")
    write_mobs(tmp_path / "props", tuple(PROPS), PROPS)
    for kind in TYPES:
        made = (tmp_path / "mobs" / f"{kind}.glb").read_bytes()
        assert made == (ASSETS / f"{kind}.glb").read_bytes(), kind
    # one folder with exactly these images; their bytes are not compared: the noise and zlib
    # differ in the last bits between platforms (a byte diff of two images also takes pytest hours)
    shared = sorted(p.name for p in (tmp_path / "furniture" / "textures").glob("*.png"))
    assert shared == sorted(p.name for p in TEXTURES.glob("*.png"))
    assert not (ASSETS / "textures").exists() and not (PROP_ASSETS / "textures").exists()


def _find_cook() -> Path | None:
    for c in (REPO / "build/release/tools/asset-cooker/g7-cook.exe",
              REPO / "build/debug/tools/asset-cooker/g7-cook.exe",
              REPO / "build/release/tools/asset-cooker/g7-cook"):  # fmt: skip
        if c.is_file():
            return c
    return None


@pytest.mark.timeout(600)  # the real cooker compresses textures (minutes)
@pytest.mark.skipif(_find_cook() is None, reason="g7-cook not built")
def test_mobs_cook(tmp_path: Path):
    src = tmp_path / "source"
    write_mobs(src / "mobs")
    out = tmp_path / "cooked"
    done = subprocess.run([str(_find_cook()), "--source", str(src), "--out", str(out)],
                          capture_output=True, text=True, check=False)  # fmt: skip
    assert done.returncode == 0, done.stdout + done.stderr
    assert len(list(out.rglob("*.g7mesh"))) == len(TYPES)  # incl. the bench


def test_hearth_glows_with_hood_and_collision_only_at_the_block():
    m = PROPS["hearth"]()
    lo, hi = _bounds(m)
    assert (hi - lo)[0] == pytest.approx(HEARTH_W, abs=0.11) and (hi - lo)[2] == pytest.approx(
        HEARTH_D
    )
    assert lo[1] == 0.0 and hi[1] == pytest.approx(HOOD_Y1)  # the smoke hood on top
    ((body),) = m.main.collision
    assert body_is_closed(body) and body.positions[:, 1].max() == pytest.approx(HEARTH_H)
    assert 0 < m.triangles() <= BUDGET
    doc, _ = read_glb(m.glb())
    glow = sorted(mat["emissiveFactor"] for mat in doc["materials"] if "emissiveFactor" in mat)
    want = sorted(list(e) for e in EMISSIVE.values())
    assert len(glow) == len(want)
    assert all(g == pytest.approx(w, abs=1e-6) for g, w in zip(glow, want, strict=True))
    assert all(0.0 <= c <= 1.0 for g in glow for c in g)  # the engine's range


def test_versioned_props_are_current(tmp_path: Path):
    tmp_path = tmp_path / "props"
    write_mobs(tmp_path, tuple(PROPS), PROPS)
    for kind in PROPS:
        assert (tmp_path / f"{kind}.glb").read_bytes() == (PROP_ASSETS / f"{kind}.glb").read_bytes()


@pytest.mark.parametrize("kind", sorted(PROPS))
def test_props_budget_and_collision(kind):
    m = PROPS[kind]()
    assert 0 < m.triangles() <= BUDGET
    assert all(body_is_closed(b) for b in m.main.collision)
    lo, hi = _bounds(m)
    if kind in ("sausages", "herbs"):  # hanging from the hook (origin) down, nobody walks into them
        assert hi[1] <= 1e-6 and not m.main.collision
    elif kind in ("tool_board", "weapon_board"):  # on the wall, above the floor
        assert lo[1] > 0.8 and hi[2] <= 0.07 and not m.main.collision
    else:  # standing on the floor, against the wall at -Z
        assert lo[1] == pytest.approx(0.0) and m.main.collision
        assert lo[2] >= -0.5  # no deeper than half a metre behind the origin
