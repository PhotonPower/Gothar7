"""Inventory cell views (M14): data/inventory_views.toml, the cell rule, the Lua item scan."""

from __future__ import annotations

import math
import re
from pathlib import Path

import numpy as np
import pytest

from conftest import REPO_ROOT
from gothar_chargen.inventory import (
    CATEGORIES,
    ITEMS_DIR,
    SCRIPTS_ITEMS_DIR,
    VIEWS_FILE,
    View,
    ViewError,
    cell,
    load_views,
    lua_items,
    model_bounds,
    model_categories,
    parse_views,
    rotation,
)
from gothar_chargen.items import ITEMS

VIEWS = REPO_ROOT / VIEWS_FILE
MODELS = sorted(f"items/{p.name}" for p in (REPO_ROOT / ITEMS_DIR).glob("*.glb"))


def test_categories_are_the_engine_item_categories():
    header = (REPO_ROOT / "engine/gameplay/include/g7/gameplay/Character.hpp").read_text("utf-8")
    block = re.search(r"kItemCategories = \{(.*?)\};", header, re.S)
    assert block is not None
    assert tuple(re.findall(r'"(\w+)"', block.group(1))) == CATEGORIES


def test_views_file_has_a_view_per_category_and_only_existing_models():
    views = load_views(VIEWS)
    assert set(views.categories) == set(CATEGORIES)
    for mesh in views.models:
        assert (REPO_ROOT / "assets/source" / mesh).is_file(), mesh


def test_every_item_model_has_a_view():
    """Each model in assets/source/items gets its view from a category (its Lua items' or its own
    `category`) or from a complete model entry - none falls back to [default] alone."""
    assert {Path(m).stem for m in MODELS} == set(ITEMS)
    views = load_views(VIEWS)
    categories = model_categories(lua_items(REPO_ROOT / SCRIPTS_ITEMS_DIR), MODELS)
    for mesh in MODELS:
        own = views.models.get(mesh, {}).get("category")
        found = {str(own)} if own is not None else categories[mesh]
        assert found, f"{mesh}: no Lua item and no category in {VIEWS_FILE}"
        for category in found:
            assert category in views.categories, (mesh, category)
            view = views.resolve(mesh, category)
            assert 0.1 <= view.fill <= 1.0


def test_lua_items_cover_literals_helpers_and_default_category(tmp_path: Path):
    (tmp_path / "a.lua").write_text(
        'Item "it_a" {\n    name = "A",\n    mesh = "items/it_a.glb",\n    category = "food",\n}\n'
        'Item "it_b" {\n    name = "B",\n}\n',
        encoding="utf-8",
    )
    (tmp_path / "b.lua").write_text(
        "local function rune(id, spell)\n    Item(id) {\n"
        '        mesh = "items/" .. id .. ".glb",\n        category = "rune",\n    }\nend\n'
        'rune("it_rune_x", "spl_x")\n',
        encoding="utf-8",
    )
    items = {i.id: i for i in lua_items(tmp_path)}
    assert items["it_a"].mesh == "items/it_a.glb" and items["it_a"].category == "food"
    assert items["it_b"].mesh is None and items["it_b"].category == "misc"
    assert (
        items["it_rune_x"].mesh == "items/it_rune_x.glb" and items["it_rune_x"].category == "rune"
    )
    models = ["items/it_a.glb", "items/it_b.glb", "items/it_rune_x.glb", "items/it_c.glb"]
    found = model_categories(list(items.values()), models)
    assert found == {
        "items/it_a.glb": {"food"},
        "items/it_b.glb": {"misc"},  # no mesh yet: by the id
        "items/it_rune_x.glb": {"rune"},
        "items/it_c.glb": set(),
    }


def test_repository_lua_items_are_found():
    items = {i.id: i for i in lua_items(REPO_ROOT / SCRIPTS_ITEMS_DIR)}
    assert items["it_sword_old"].category == "melee_1h"
    assert items["it_rune_heal"].mesh == "items/it_rune_heal.glb"
    assert items["it_scroll_heal"].mesh == "items/it_scroll.glb"


def test_resolve_merges_default_category_and_model():
    views = parse_views(
        "version = 1\n[camera]\nfov = 30\n"
        "[default]\nyaw = 1\npitch = 2\nroll = 3\nfill = 0.8\n"
        "[category.food]\npitch = 20\n[category.misc]\nroll = -45\n"
        '[model."items/it_x.glb"]\ncategory = "misc"\nfill = 0.5\n'
    )
    assert views.resolve("items/it_y.glb", "food") == View(1, 20, 3, 0.8)
    assert views.resolve("items/it_y.glb", None) == View(1, 2, 3, 0.8)
    assert views.resolve("items/it_x.glb", "food") == View(1, 2, -45, 0.5)  # own category wins


HEAD = "version = 1\n[camera]\nfov = 30\n[default]\nyaw = 0\npitch = 0\nroll = 0\n"


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("version = 2", "version"),
        ("version = 1\n[default]\nyaw = 0\npitch = 0\nroll = 0\nfill = 0.8", "fov"),
        (HEAD, "default"),
        (HEAD + "fill = 2", "fill"),
        (HEAD + "fill = 0.8\n[category.food]\nzoom = 1", "zoom"),
        (HEAD + "fill = 0.8\n[category.shield]\nyaw = 1", "shield"),
        (HEAD + 'fill = 0.8\n[model."it_x"]\nyaw = 1', "items/"),
        (HEAD + 'fill = 0.8\n[model."items/it_x.glb"]\ncategory = "shield"', "shield"),
        (HEAD + "fill = 0.8\n[extra]\na = 1", "unknown tables"),
    ],
)
def test_parse_views_rejects_broken_files(text: str, message: str):
    with pytest.raises(ViewError, match=message):
        parse_views(text)


def test_rotation_axes():
    """yaw 90: the item's +Z (edge) to the right; pitch > 0: the top towards the viewer (+Z);
    roll -45: the tip (+Y) to the upper right."""
    np.testing.assert_allclose(rotation(View(90, 0, 0, 1)) @ [0, 0, 1], [1, 0, 0], atol=1e-12)
    np.testing.assert_allclose(rotation(View(90, 0, 0, 1)) @ [1, 0, 0], [0, 0, -1], atol=1e-12)
    up = rotation(View(0, 30, 0, 1)) @ [0, 1, 0]
    assert up[1] > 0 and up[2] > 0
    s = math.sqrt(0.5)
    np.testing.assert_allclose(rotation(View(0, 0, -45, 1)) @ [0, 1, 0], [s, s, 0], atol=1e-12)
    r = rotation(View(37, -12, 81, 1))
    np.testing.assert_allclose(r @ r.T, np.eye(3), atol=1e-12)


def test_cell_centres_the_box_and_fits_its_front():
    lo, hi = np.array([-0.1, -0.2, 0.0]), np.array([0.3, 0.6, 0.1])
    c = cell(View(0, 0, 0, 0.8), lo, hi, 30.0)
    corners = np.array(
        [[x, y, z, 1] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (0, 0.1)]
    )
    moved = (corners @ c.matrix.T)[:, :3]
    np.testing.assert_allclose(moved.min(axis=0), -moved.max(axis=0), atol=1e-12)
    # the front face (z = +0.05) seen from the camera: half height 0.4 fills 0.8 of the half cell
    half_cell = (c.distance - 0.05) * math.tan(math.radians(15))
    assert math.isclose(0.4 / half_cell, 0.8)


def test_cell_of_every_model_is_finite_and_in_front_of_the_camera():
    views = load_views(VIEWS)
    categories = model_categories(lua_items(REPO_ROOT / SCRIPTS_ITEMS_DIR), MODELS)
    for mesh in MODELS:
        own = sorted(categories[mesh])
        view = views.resolve(mesh, views.category_of(mesh, own[0] if own else None))
        lo, hi = model_bounds(REPO_ROOT / "assets/source" / mesh)
        c = cell(view, lo, hi, views.fov)
        assert np.isfinite(c.matrix).all() and c.distance > 0, mesh
