from __future__ import annotations

import copy
import os
import struct
import zlib
from dataclasses import replace
from pathlib import Path

import pytest

from conftest import REPO_ROOT
from gothar_chargen.gltf import Gltf
from gothar_chargen.human import HumanError, load_human, parse_human
from gothar_chargen.images import ImageError, image_info, is_power_of_two
from gothar_chargen.mapping import load_mapping
from gothar_chargen.postprocess import apply_alpha_mask, externalize_images, material_role
from gothar_chargen.skeleton import load_rig
from gothar_chargen.validate import Tolerances, validate_gltf

CHARACTERS = REPO_ROOT / "assets/source/characters"
FARMER = CHARACTERS / "figures/farmer.glb"


def png(width: int, height: int, alpha: bool = True) -> bytes:
    """Minimal valid PNG (one colour)."""
    color_type, channels = (6, 4) if alpha else (2, 3)
    raw = b"".join(b"\0" + b"\x80" * (width * channels) for _ in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def jpeg_header(width: int, height: int) -> bytes:
    app0 = b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\0\x01\x01\0\0\x01\0\x01\0\0"
    sof = b"\xff\xc0" + struct.pack(">HBHHB", 11, 8, height, width, 1) + b"\x01\x11\x00"
    return b"\xff\xd8" + app0 + sof + b"\xff\xd9"


# --- recipe --------------------------------------------------------------------------------------

RECIPE = {
    "version": 1,
    "triangles": 12000,
    "macro": {"gender": 1.0, "age": 0.7, "race": {"caucasian": 2.0, "african": 2.0}},
    "assets": {
        "skin": "skins/a/a.mhmat",
        "eyes": "eyes/low-poly/low-poly.mhclo",
        "hair": "hair/h/h.mhclo",
        "clothes": ["clothes/shirt/shirt.mhclo"],
    },
    "tint": {"shirt": "#ffeedd", "skin": "#cc9988"},
}


def test_parse_recipe():
    h = parse_human(RECIPE, "npc_01")
    assert h.race == {"african": 0.5, "asian": 0.0, "caucasian": 0.5}
    assert [t for t, _ in h.assets()] == ["Eyes", "Clothes", "Hair"]
    assert h.triangles == 12000


def test_committed_recipes():
    recipes = sorted((CHARACTERS / "humans").glob("*.human.toml"))
    assert recipes
    for r in recipes:
        h = load_human(r)
        for part in h.parts:  # base bodies export only "body", heads "head" and "hair"
            if part == "cloth":  # clothing kits: one part per garment
                for garment in h.clothes:
                    stem = Path(garment).stem
                    assert (CHARACTERS / "parts" / h.name / f"{stem}.glb").is_file(), garment
                continue
            assert (CHARACTERS / "parts" / h.name / f"{part}.glb").is_file(), (h.name, part)


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"version": 2}, "version"),
        ({"x": 1}, "unknown keys"),
        ({"macro": {"strength": 0.5}}, "unknown macro"),
        ({"macro": {"age": 1.5}}, "between 0 and 1"),
        ({"macro": {"race": {"elf": 1.0}}}, "race"),
        ({"macro": {"race": {"asian": 0.0}}}, "all be 0"),
        ({"assets": None}, "missing \\[assets\\]"),
        ({"assets": {"skin": "s/s.mhmat"}}, "at least"),
        ({"assets": {"skin": "s/s.png", "eyes": "e/e.mhclo"}}, ".mhmat"),
        ({"assets": {"skin": "../s.mhmat", "eyes": "e/e.mhclo"}}, "relative"),
        (
            {"assets": {"skin": "s/s.mhmat", "eyes": "e/e.mhclo", "cape": "c.mhclo"}},
            "unknown assets",
        ),
        ({"assets": {"skin": "s/s.mhmat", "eyes": "e/e.mhclo", "clothes": "c.mhclo"}}, "list"),
        ({"tint": {"shirt": "beige"}}, "#rrggbb"),
        ({"tint": {"cape": "#ffffff"}}, "not in this recipe"),
        ({"triangles": 50000}, "triangles"),
        ({"parts": ["body", "legs"]}, "parts"),
        ({"parts": []}, "parts"),
        ({"shape": {"nose hump": 0.5}}, "bad MPFB target"),
        ({"shape": {"nose-hump-incr": 1.5}}, "shape"),
    ],
)
def test_invalid_recipes(change, message):
    with pytest.raises(HumanError, match=message):
        parse_human({**RECIPE, **change}, "npc")


def test_parts_shape_and_beard():
    data = {
        **RECIPE,
        "parts": ["hair", "head"],
        "shape": {"nose-hump-incr": 0.6},
        "assets": {**RECIPE["assets"], "beard": "clothes/b/b.mhclo"},
    }
    h = parse_human(data, "head_x")
    assert h.parts == ("head", "hair")  # canonical order
    assert h.shape == {"nose-hump-incr": 0.6}
    assert ("Beard", "clothes/b/b.mhclo") in h.assets()
    assert parse_human(RECIPE, "x").parts == ("body", "head", "hair")


def test_clothing_kit_recipe(tmp_path):
    from gothar_chargen.human import HumanError, load_human

    body = """version = 1
parts = ["body"]
[macro]
gender = 1.0
weight = 0.2
[assets]
skin = "s/s.mhmat"
eyes = "e/e.mhclo"
"""
    kit_text = """version = 1
fit_to = "body_x"
parts = ["cloth"]
[assets]
clothes = ["clothes/a/a.mhclo"]
"""
    (tmp_path / "body_x.human.toml").write_text(body, encoding="utf-8")
    kit = tmp_path / "cloth_x.human.toml"
    kit.write_text(kit_text, encoding="utf-8")
    h = load_human(kit)
    assert h.fit_to == "body_x" and h.parts == ("cloth",)
    assert h.macro["weight"] == 0.2 and h.skin == "s/s.mhmat"  # inherited from the body
    kit.write_text(kit_text.replace("[assets]", "[macro]\nage = 0.5\n[assets]"), encoding="utf-8")
    with pytest.raises(HumanError, match="from its base"):
        load_human(kit)
    with pytest.raises(HumanError, match="not together"):
        parse_human({**RECIPE, "parts": ["body", "cloth"]}, "x")
    with pytest.raises(HumanError, match="fit_to"):
        parse_human({**RECIPE, "fit_to": "body_y"}, "x")


def test_recipe_name_and_suffix(tmp_path):
    with pytest.raises(HumanError, match="lower_snake_case"):
        parse_human(RECIPE, "Npc 1")
    with pytest.raises(HumanError, match=".human.toml"):
        load_human(tmp_path / "x.toml")


def test_mpfb_mapping_covers_body():
    m = load_mapping("mpfb_game_engine")
    rig = load_rig()
    body = {b.name for b in rig.bones if not b.socket}
    mpfb = ["Root", "neck_01"] + sorted(body - {"root", "neck"})
    assert set(m.resolve(mpfb, set(rig.names)).values()) == body


# --- images --------------------------------------------------------------------------------------


def test_image_info():
    info = image_info(png(64, 32))
    assert (info.format, info.width, info.height, info.alpha) == ("png", 64, 32, True)
    assert image_info(png(8, 8, alpha=False)).alpha is False
    info = image_info(jpeg_header(1024, 512))
    assert (info.format, info.width, info.height, info.alpha) == ("jpeg", 1024, 512, False)
    for bad in (b"GIF89a", b"\x89PNG\r\n\x1a\n\0\0\0\0XXXX", b"\xff\xd8\x00"):
        with pytest.raises(ImageError):
            image_info(bad)
    assert is_power_of_two(1024) and not is_power_of_two(1000) and not is_power_of_two(0)


def test_material_roles():
    assert material_role("skin") == "skin"
    assert material_role("hair.001") == "hair"
    assert material_role("eyelashes") == "eyelashes"
    assert material_role("cloth_toigo_wool_pants") == "cloth"
    assert material_role("mannequin") == "cloth"
    assert material_role("fur") == "fur"


# --- texture post-processing and rules -----------------------------------------------------------


@pytest.fixture
def farmer(tmp_path) -> Gltf:
    """The farmer figure in a temp folder whose textures/ mirrors the committed files."""
    g = Gltf.load(FARMER)
    target = tmp_path / "figures" / "farmer.glb"
    target.parent.mkdir()
    for image in g.doc["images"]:
        src = (FARMER.parent / image["uri"]).resolve()
        rel = Path(image["uri"])
        dst = (target.parent / rel).resolve()
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read_bytes())
    return Gltf(doc=copy.deepcopy(g.doc), bin=g.bin, path=target)


def codes(report, level="error"):
    return {i.code for i in report.issues if i.level == level}


def test_farmer_textures_pass(rig):
    report = validate_gltf(Gltf.load(FARMER), rig, None, path=FARMER)
    assert report.ok(strict=True), report.issues
    assert report.stats["textures"] >= 6
    roles = {material_role(m["name"]): m for m in Gltf.load(FARMER).doc["materials"]}
    assert roles["hair"]["alphaMode"] == "MASK"


def _image_of(g: Gltf, role: str) -> dict:
    mat = next(m for m in g.doc["materials"] if material_role(m["name"]) == role)
    tex = g.doc["textures"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
    return g.doc["images"][tex["source"]]


def test_texture_rules(farmer, rig, tmp_path):
    hair = next(m for m in farmer.doc["materials"] if material_role(m["name"]) == "hair")
    hair["alphaMode"] = "BLEND"
    assert "tex.alpha_mode" in codes(validate_gltf(farmer, rig, None, path=farmer.path))
    hair["alphaMode"] = "MASK"

    odd = tmp_path / "odd.png"
    odd.write_bytes(png(100, 64))
    _image_of(farmer, "skin")["uri"] = os.path.relpath(odd, farmer.path.parent).replace("\\", "/")
    assert "tex.pow2" in codes(validate_gltf(farmer, rig, None, path=farmer.path))

    _image_of(farmer, "skin")["uri"] = "../textures/nope.jpg"
    assert "tex.missing" in codes(validate_gltf(farmer, rig, None, path=farmer.path))

    jpg = tmp_path / "hair.jpg"
    jpg.write_bytes(jpeg_header(512, 512))
    _image_of(farmer, "hair")["uri"] = os.path.relpath(jpg, farmer.path.parent).replace("\\", "/")
    assert "tex.alpha" in codes(validate_gltf(farmer, rig, None, path=farmer.path))


def test_texture_size_limit(farmer, rig):
    small = replace(Tolerances(), texture_max=(("skin", 256), ("cloth", 1024)))
    assert "tex.size" in codes(validate_gltf(farmer, rig, None, small, path=farmer.path))


def test_externalize_and_mask(tmp_path):
    g = Gltf(
        doc={
            "asset": {"version": "2.0"},
            "buffers": [{"byteLength": 0}],
            "bufferViews": [],
            "accessors": [],
            "materials": [{"name": "hair"}, {"name": "cloth_x"}],
            "images": [],
        }
    )
    data = png(16, 16)
    g.bin = data
    g.doc["buffers"][0]["byteLength"] = len(data)
    g.doc["bufferViews"].append({"buffer": 0, "byteOffset": 0, "byteLength": len(data)})
    g.doc["images"].append({"name": "hair__test_hair", "bufferView": 0, "mimeType": "image/png"})
    glb = tmp_path / "parts" / "x" / "hair.glb"
    textures = tmp_path / "textures"
    written = externalize_images(g, glb, textures)
    assert written == [textures / "hair" / "test_hair.png"]
    assert g.doc["images"][0] == {
        "name": "hair__test_hair",
        "uri": "../../textures/hair/test_hair.png",
    }
    assert g.bin == b"" and g.doc["bufferViews"] == []
    assert apply_alpha_mask(g) == 1
    assert g.doc["materials"][0]["alphaMode"] == "MASK"
    assert "alphaMode" not in g.doc["materials"][1]
    # an existing file is kept (shared texture), nothing written again
    g2 = Gltf(doc=copy.deepcopy(g.doc))
    g2.doc["images"][0] = {"name": "hair/test_hair", "bufferView": 0, "mimeType": "image/png"}
    g2.doc["bufferViews"] = [{"buffer": 0, "byteOffset": 0, "byteLength": len(data)}]
    g2.bin = data
    assert externalize_images(g2, glb, textures) == []


def test_reimported_image_reuses_shared_file(tmp_path):
    textures = tmp_path / "textures"
    (textures / "skin").mkdir(parents=True)
    data = png(8, 8, alpha=False)
    (textures / "skin" / "face_skin.png").write_bytes(data)
    g = Gltf(
        doc={
            "asset": {"version": "2.0"},
            "buffers": [{"byteLength": len(data)}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(data)}],
            "images": [{"name": "face_skin.png", "bufferView": 0, "mimeType": "image/png"}],
        },
        bin=data,
    )
    glb = tmp_path / "figures" / "x.glb"
    assert externalize_images(g, glb, textures) == []
    assert g.doc["images"][0]["uri"] == "../textures/skin/face_skin.png"
    assert not (textures / "misc").exists()
