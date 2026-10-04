"""Licence check of MPFB assets: CC0 by file header or by a documented CC0 source."""

from __future__ import annotations

from pathlib import Path

import pytest

from gothar_chargen.licences import (
    check_recipe,
    header_licence,
    headerless_sources,
    is_cc0,
    recipe_assets,
)

CHARACTERS = Path(__file__).resolve().parents[3] / "assets" / "source" / "characters"


@pytest.mark.parametrize(
    ("text", "licence"),
    [
        ("# Exported from MakeClothes (TM)\n# author x\n# license CC0\nname a\n", "CC0"),
        ("# license CC-0\n", "CC-0"),
        ("# license: CC0\n", "CC0"),
        ("license CC BY 4.0\n", "CC BY 4.0"),
        ("# license AGPL3 (see also http://www.makehuman.org/node/320)\n", "AGPL3 (see also"),
        ("# author x\nname a\n", None),
    ],
)
def test_header_licence(text: str, licence: str | None) -> None:
    found = header_licence(text)
    assert found == licence if licence is None else found is not None and found.startswith(licence)


@pytest.mark.parametrize(
    ("licence", "ok"),
    [("CC0", True), ("CC-0", True), ("cc0 1.0", True), ("CC0 1.0 Universal", True),
     ("CC BY 4.0", False), ("CC_by", False), ("AGPL3", False), ("CC-BY-SA", False)],
)  # fmt: skip
def test_is_cc0(licence: str, ok: bool) -> None:
    assert is_cc0(licence) is ok


def _asset(root: Path, rel: str, header: str | None, material: str | None = None) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Exported from MakeClothes (TM)"]
    if header:
        lines.append(f"# license {header}")
    lines.append("name x")
    if material:
        lines.append(f"material {material}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _recipe(tmp_path: Path, clothes: list[str]) -> Path:
    recipe = tmp_path / "kit.human.toml"
    listed = ", ".join(f'"{c}"' for c in clothes)
    recipe.write_text(f"version = 1\n[assets]\nclothes = [{listed}]\n", encoding="utf-8")
    return recipe


def test_check_recipe(tmp_path: Path) -> None:
    root = tmp_path / "mpfb"
    _asset(root, "clothes/good/good.mhclo", "CC0", material="good.mhmat")
    _asset(root, "clothes/good/good.mhmat", None)  # headerless material shares the asset's verdict
    _asset(root, "clothes/agpl/agpl.mhclo", "AGPL3")
    _asset(root, "clothes/ccby/ccby.mhclo", "CC0", material="ccby.mhmat")
    _asset(root, "clothes/ccby/ccby.mhmat", "CC BY 4.0")
    _asset(root, "clothes/bare/bare.mhclo", None)  # no header, not listed
    _asset(root, "hair/short01/short01.mhclo", None)  # no header, listed (system assets)
    rel = [
        "clothes/good/good.mhclo",
        "clothes/agpl/agpl.mhclo",
        "clothes/ccby/ccby.mhclo",
        "clothes/bare/bare.mhclo",
        "hair/short01/short01.mhclo",
        "clothes/gone/gone.mhclo",
    ]
    errors = check_recipe(_recipe(tmp_path, rel), root)
    text = "\n".join(errors)
    assert len(errors) == 4, text
    assert "agpl.mhclo: licence 'AGPL3'" in text
    assert "material ccby.mhmat has licence 'CC BY 4.0'" in text
    assert "bare.mhclo: no licence line" in text
    assert "gone.mhclo: not installed" in text
    assert "good" not in text and "short01" not in text


def test_recipe_assets_walks_all_keys() -> None:
    data = {
        "assets": {"skin": "skins/a/a.mhmat", "clothes": ["c/b/b.mhclo"], "hairs": ["h/c/c.mhclo"]},
        "derive": [{"source": "c/d/d.mhclo", "texture": "gothar/x/x.jpg"}],
        "macro": {"age": 0.5},
        "names": {"b": "shirt"},
    }
    assert recipe_assets(data) == [
        "c/b/b.mhclo", "c/d/d.mhclo", "gothar/x/x.jpg", "h/c/c.mhclo", "skins/a/a.mhmat",
    ]  # fmt: skip


def test_headerless_sources_are_documented() -> None:
    """Every listed folder names its CC0 source; each source pack has a row in LICENSES.md."""
    sources = headerless_sources()
    assert sources
    licences = (CHARACTERS.parents[2] / "assets" / "LICENSES.md").read_text(encoding="utf-8")
    for folder, source in sources.items():
        assert "CC0" in source, folder
        assert not folder.endswith("/") and folder.count("/") >= 1, folder
    for pack in ("makehuman_system_assets_cc0.zip", "hair01_cc0.zip", "Leather 014", "Metal 021"):
        assert pack in licences, pack
