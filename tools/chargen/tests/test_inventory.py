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
    model_categories,
    model_vertices,
    parse_views,
    project,
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
        "version = 2\n[camera]\nfov = 30\n"
        "[default]\nyaw = 1\npitch = 2\nroll = 3\nfill = 0.8\n"
        "[category.food]\npitch = 20\n[category.misc]\nroll = -45\n"
        '[model."items/it_x.glb"]\ncategory = "misc"\nfill = 0.5\n'
    )
    assert views.resolve("items/it_y.glb", "food") == View(1, 20, 3, 0.8)
    assert views.resolve("items/it_y.glb", None) == View(1, 2, 3, 0.8)
    assert views.resolve("items/it_x.glb", "food") == View(1, 2, -45, 0.5)  # own category wins


HEAD = "version = 2\n[camera]\nfov = 30\n[default]\nyaw = 0\npitch = 0\nroll = 0\n"


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("version = 1", "version"),
        ("version = 2\n[default]\nyaw = 0\npitch = 0\nroll = 0\nfill = 0.8", "fov"),
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


TAN15 = math.tan(math.radians(15))


def box(lo: tuple[float, float, float], hi: tuple[float, float, float]) -> np.ndarray:
    return np.array(
        [[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    )


def test_cell_distance_of_simple_shapes():
    """d = max over the vertices of p.z + max(|p.x|, |p.y|) / tan(fov / 2), after centring."""
    # a flat segment across the view: both ends on the edge of the full view
    c = cell(View(0, 0, 0, 0.8), np.array([[0.0, 0, 0], [2, 0, 0]]), 30.0)
    np.testing.assert_allclose(c.matrix[:3, 3], [-1, 0, 0])
    assert math.isclose(c.distance, 1 / TAN15)
    np.testing.assert_allclose(c.window, [0, 0], atol=1e-12)
    # a point in the middle sticking out towards the camera does not matter, it is not outermost
    c = cell(View(0, 0, 0, 0.8), np.array([[-1.0, 0, 0], [1, 0, 0], [0, 0, 0.5]]), 30.0)
    assert math.isclose(c.distance, 1 / TAN15 - 0.25)
    # the nearer end sets the distance; it looks larger, so the window moves towards it
    c = cell(View(0, 0, 0, 1.0), np.array([[1.0, 0, 0.5], [-1, 0, -0.5]]), 30.0)
    assert math.isclose(c.distance, 0.5 + 1 / TAN15)
    near, far = 1.0, -1 / ((c.distance + 0.5) * TAN15)
    np.testing.assert_allclose(c.window, [(near + far) / 2, 0])
    assert math.isclose(c.half, (near - far) / 2)


@pytest.mark.parametrize(
    "view",
    [View(0, 0, 0, 0.8), View(25, 15, 0, 0.85), View(90, 0, -45, 0.95), View(37, -12, 81, 0.6)],
)
def test_cell_centres_the_contour_and_fits_fill(view: View):
    shape = np.r_[box((-0.1, -0.2, 0.0), (0.3, 0.6, 0.1)), [[0.5, 0.7, 0.4], [-0.3, 0.1, -0.2]]]
    c = cell(view, shape, 30.0)
    view_z = (np.c_[shape, np.ones(len(shape))] @ c.matrix.T)[:, 2]
    assert (c.distance - view_z > 0).all()  # all in front of the camera
    u = project(c, shape)
    lo, hi = u.min(axis=0), u.max(axis=0)
    np.testing.assert_allclose(lo, -hi, atol=1e-12)  # centred on the projected contour
    assert math.isclose(float(np.max(hi - lo)) / 2, view.fill)  # fills `fill` of the cell


def diagonal_share(u: np.ndarray) -> float:
    """Length of the image along the cell diagonal (lower left - upper right) / the diagonal."""
    along = u @ np.array([1.0, 1.0]) / math.sqrt(2)
    return float(along.max() - along.min()) / (2 * math.sqrt(2))


def test_diagonal_blade_fills_the_diagonal():
    """A sword at roll -45 reaches along the cell diagonal: its crossguard widens the local
    bounding box (the version 1 rule shrank this one to 79 %), not its contour."""
    sword = np.r_[
        box((-0.004, 0.0, -0.02), (0.004, 0.9, 0.02)),  # grip and blade along +Y
        box((-0.01, 0.15, -0.09), (0.01, 0.17, 0.09)),  # crossguard across, along Z
    ]
    view = View(90, 0, -45, 0.95)
    assert diagonal_share(project(cell(view, sword, 30.0), sword)) >= 0.85
    views = load_views(VIEWS)
    categories = model_categories(lua_items(REPO_ROOT / SCRIPTS_ITEMS_DIR), MODELS)
    for name in ("it_sword_old", "it_sword_crude", "it_sword_2h", "it_bow_short", "it_torch"):
        mesh = f"items/{name}.glb"
        vertices = model_vertices(REPO_ROOT / "assets/source" / mesh)
        c = cell(views.resolve(mesh, min(categories[mesh])), vertices, views.fov)
        assert diagonal_share(project(c, vertices)) >= 0.85, mesh


def test_cell_of_every_model_is_finite_centred_and_fills_its_view():
    views = load_views(VIEWS)
    categories = model_categories(lua_items(REPO_ROOT / SCRIPTS_ITEMS_DIR), MODELS)
    for mesh in MODELS:
        own = sorted(categories[mesh])
        view = views.resolve(mesh, views.category_of(mesh, own[0] if own else None))
        vertices = model_vertices(REPO_ROOT / "assets/source" / mesh)
        c = cell(view, vertices, views.fov)
        assert np.isfinite(c.matrix).all() and c.distance > 0 and c.half > 0, mesh
        u = project(c, vertices)
        lo, hi = u.min(axis=0), u.max(axis=0)
        np.testing.assert_allclose(lo, -hi, atol=1e-9, err_msg=mesh)
        assert math.isclose(float(np.max(hi - lo)) / 2, view.fill), mesh
