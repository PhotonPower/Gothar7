from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.gltf import MeshData, Primitive, glb_bytes_multi, read_glb
from gothar_worldgen.buildings.medieval import build_house, load_rules
from gothar_worldgen.facade.overrides import from_json
from gothar_worldgen.textures.apply import kind_of, textured, write_textures
from gothar_worldgen.textures.procedural import ALBEDO_SCALE, KINDS, TILE_M, make, periodic_noise

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
# geometry and palette tests without the W5 textures (those: test_textures.py)
RULES.data["textures"] = {}


def _seam(img: np.ndarray, axis: int) -> float:
    """Step across the wrap seam relative to the largest step between neighbouring lines inside
    the image (joints and seams are allowed, a jump that never occurs inside is not)."""
    steps = np.abs(np.diff(img, axis=axis)).mean(axis=1 - axis)
    first = np.take(img, 0, axis=axis)
    last = np.take(img, -1, axis=axis)
    return float(np.abs(first - last).mean() / max(float(steps.max()), 1e-9))


@pytest.mark.parametrize("kind", sorted(KINDS))
def test_textures_tile_seamlessly_and_keep_the_palette_brightness(kind):
    tex = make(kind, 256)
    gray = tex.albedo if tex.albedo.ndim == 2 else tex.albedo.mean(axis=2)
    assert _seam(tex.height, 1) <= 1.05  # across u: always seamless
    if kind not in ("plaster_low", "plaster_streak"):  # these do not repeat upwards
        assert _seam(tex.height, 0) <= 1.05
        # the palette colour stays; the rubble stone is darker on purpose (owner 2026-10-04)
        assert gray.mean() == pytest.approx(0.45 if kind == "stone" else 1.0, abs=0.02)
    n = tex.normal() * 2 - 1
    assert np.allclose(np.linalg.norm(n, axis=-1), 1.0, atol=1e-6)
    assert (n[..., 2] > 0.2).all()  # no normal lies flat
    again = make(kind, 256)
    assert np.array_equal(again.albedo, tex.albedo)  # reproducible


def test_periodic_noise_wraps():
    img = periodic_noise((64, 128), np.random.default_rng(1))
    assert img.min() == 0.0 and img.max() == 1.0
    assert _seam(img, 0) <= 1.05 and _seam(img, 1) <= 1.05


def test_the_foot_band_is_dirty_at_the_bottom_and_plain_at_the_top():
    tex = make("plaster_low", 256)
    gray = tex.albedo.mean(axis=2)
    assert gray[-8:].mean() < gray[:8].mean() - 0.1  # image row 0 is the top


def test_palette_entries_choose_their_texture():
    assert kind_of("plaster_white") == "plaster" and kind_of("plaster_ochre~low") == "plaster_low"
    assert kind_of("stone") == kind_of("brick") == "stone"
    assert kind_of("timber_oxblood") == "timber" and kind_of("roof_old") == "roof"
    assert kind_of("roof_old_moss") == kind_of("roof_red~moss") == "roof_moss"
    assert kind_of("plaster_grey~streak") == "plaster_streak"
    assert kind_of("frame") == "boards" and kind_of("hedge") is None
    assert kind_of("stone_slab") == "slab" and kind_of("stone_dry") == "stone"  # W6


def _quad(points, normal):
    pos = np.asarray(points, dtype=np.float32)
    normals = np.tile(np.asarray(normal, np.float32), (4, 1))
    uvs = np.zeros((4, 2), np.float32)
    return MeshData(pos, normals, uvs, np.array([0, 1, 2, 0, 2, 3], np.uint32))


def test_roof_rows_run_along_the_eave_and_colours_are_lifted():
    # a roof plane falling to the south (+z), eave along x
    s = np.sqrt(0.5)
    mesh = _quad([[0, 0, 2], [4, 0, 2], [4, 2, 0], [0, 2, 0]], [0, s, s])
    prim = textured(Primitive("roof_red", (0.2, 0.08, 0.06, 1.0), mesh), "worlds/x/textures")
    uv = prim.mesh.uvs
    assert prim.textures == (
        "worlds/x/textures/roof_albedo.png",
        "worlds/x/textures/roof_normal.png",
    )
    assert uv[0, 1] == pytest.approx(uv[1, 1]) and uv[2, 1] == pytest.approx(uv[3, 1])  # eave rows
    assert abs(uv[1, 0] - uv[0, 0]) == pytest.approx(4.0 / TILE_M["roof"][0])
    assert uv[3, 1] < uv[0, 1]  # up the slope is up in the image (glTF v grows downwards)
    assert prim.color[0] == pytest.approx(0.2 / ALBEDO_SCALE)


def test_glb_references_each_image_once():
    mesh = _quad([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], [0, 0, 1])
    a = textured(Primitive("plaster_white", (0.6, 0.55, 0.45, 1.0), mesh), "t")
    b = textured(Primitive("plaster_white~low", (0.6, 0.55, 0.45, 1.0), mesh), "t")
    c = Primitive("hedge", (0.05, 0.08, 0.03, 1.0), mesh)
    doc, _ = read_glb(glb_bytes_multi([a, a, b, c], "h"))
    uris = [i["uri"] for i in doc["images"]]
    assert uris == [
        f"t/{k}_{m}.png" for k in ("plaster", "plaster_low") for m in ("albedo", "normal")
    ]
    assert len(doc["materials"]) == 3 and "normalTexture" in doc["materials"][0]
    assert "baseColorTexture" not in doc["materials"][2]["pbrMetallicRoughness"]
    assert doc["samplers"][0]["wrapS"] == 10497


def test_textured_houses_get_a_dirty_foot_band_only_when_listed():
    ring = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
    roof = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
    house = {"id": "H1", "groundY": 0.0, "footprint": ring, "roof": roof}
    style = from_json({"id": "H1", "style": "handwerkerhaus", "seed": 1})  # plaster to the socle
    plain = build_house(house, -0.3, (5.0, -3.5), RULES, None, style)
    assert not any(p.material.endswith("~low") for p in plain.primitives)
    probe = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
    probe.data["textures"] = {"probe": ["H1"], "dirtM": 0.8}
    r = build_house(house, -0.3, (5.0, -3.5), probe, None, style)
    low = [p for p in r.primitives if p.material.endswith("~low")]
    assert low  # plaster on the ground storey: the band is there and reaches 0.8 m
    if low:
        ys = np.concatenate([p.mesh.positions[:, 1] for p in low]) + -0.3
        assert ys.max() <= 0.8 + 1e-3
        uv = np.concatenate([p.mesh.uvs for p in low])
        assert uv[:, 1].max() <= 1.0 + 1e-6
    total = sum(p.mesh.triangle_count for p in r.primitives)
    assert total <= sum(p.mesh.triangle_count for p in plain.primitives) + 300  # wall splits


def test_write_textures(tmp_path: Path):
    paths = write_textures(tmp_path, 64)
    assert len(paths) == 2 * len(KINDS) and all(p.is_file() for p in paths)


def test_rain_streaks_are_part_of_the_plaster_under_some_windows():
    ring = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
    roof = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
    house = {"id": "H1", "groundY": 0.0, "footprint": ring, "roof": roof}
    probe = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
    probe.data["textures"] = {"probe": ["H1"], "dirtM": 0.8}
    style = from_json({"id": "H1", "style": "handwerkerhaus", "seed": 1})
    r = build_house(house, -0.3, (5.0, -3.5), probe, None, style)
    streaks = [p for p in r.primitives if p.material.endswith("~streak")]
    assert streaks and all(
        p.mesh.uvs.min() >= -1e-6 and p.mesh.uvs.max() <= 1 + 1e-6 for p in streaks
    )
    assert kind_of(streaks[0].material) == "plaster_streak"
    moss = [p for p in r.primitives if p.material.endswith("~moss")]
    assert moss  # the shady roof side
    doc, _ = read_glb(glb_bytes_multi([textured(p, "t") for p in streaks], "s"))
    assert "alphaMode" not in doc["materials"][0]  # opaque: soft edges are in the texture
    # the walls are still closed: plain plaster + streaks cover what plain plaster covered alone
    plain = build_house(house, -0.3, (5.0, -3.5), RULES, None, style)

    def area(prims):
        total = 0.0
        for p in prims:
            if p.material.split("~")[0].startswith(("plaster", "lehm")):
                t = p.mesh.positions[p.mesh.indices.reshape(-1, 3)].astype(float)
                total += (
                    0.5
                    * np.linalg.norm(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]), axis=1).sum()
                )
        return total

    assert area(r.primitives) == pytest.approx(area(plain.primitives), rel=0.01)


def test_cobbles_tile_and_have_round_stones_in_sand():
    tex = make("cobbles", 256)
    assert _seam(tex.height, 0) <= 1.05 and _seam(tex.height, 1) <= 1.05
    assert tex.albedo.mean() == pytest.approx(1.0, abs=0.02)
    gaps = (tex.height <= 0.0).mean()
    assert 0.05 < gaps < 0.45  # stones touch, sand shows between them


def test_neighbouring_houses_get_different_offsets_of_the_repeating_textures():
    from gothar_worldgen.textures.apply import texture_house

    mesh = _quad([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], [0, 0, 1])
    prims = [Primitive("stone", (0.45, 0.4, 0.33, 1.0), mesh), Primitive("plaster_white~streak",
             (0.6, 0.55, 0.45, 1.0), mesh)]  # fmt: skip
    a, b = texture_house(prims, "t", "A"), texture_house(prims, "t", "B")
    assert not np.allclose(a[0].mesh.uvs, b[0].mesh.uvs)  # the stones move
    assert np.allclose(a[1].mesh.uvs, b[1].mesh.uvs)  # streak variants stay where they are
    assert np.allclose(texture_house(prims, "t", "A")[0].mesh.uvs, a[0].mesh.uvs)  # stable


def test_gutter_slabs_have_joints_across_and_none_along_the_edges():
    tex = make("slab", 256, 3)
    lum = tex.albedo.mean(axis=2)
    rows = lum.mean(axis=1)  # along v: joints between slabs are darker rows
    cols = lum.mean(axis=0)  # across u: no joint along the gutter's edges
    assert rows.min() < 0.8 * np.median(rows)
    assert cols.max() - cols.min() < 0.1 * np.median(cols)
    assert lum.min() > 0.4 * lum.mean()  # dirty sand, not black lines
