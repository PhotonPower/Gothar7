"""Inventory cell views (M14 preparation): which way every item model faces in its cell.

Contract with engine (docs/modules/ui.md "Inventar-Ansichten", proposal): the data file
``assets/source/data/inventory_views.toml`` holds a view per item category (the categories of the
Lua items, ``category = "melee_1h"`` …) and overrides per model (the Lua ``mesh`` path). A view is
yaw/pitch/roll in degrees plus ``fill``, the share of the cell the bounding box takes. Resolution:
``[default]`` <- ``[category.<c>]`` <- ``[model."<mesh>"]``, field by field; ``<c>`` is the model
entry's own ``category`` if it has one, else the category of the item.

Cell geometry (item axes as in the glTF: +Y up = grip axis, +Z = edge/front; view axes: +X right,
+Y up, the camera on +Z looking down -Z, square cell, perspective with ``[camera] fov``):

1. rotate the model by ``R = Rz(roll) * Rx(pitch) * Ry(yaw)`` (yaw spins it about its own +Y, pitch
   tilts its top towards the viewer - seen from above -, roll turns it in the image plane, positive
   counter-clockwise),
2. take the 8 corners of its local bounding box (LOD 0), rotated; their box has centre ``c`` and
   half sizes ``h`` - the model is moved by ``-c`` (centred on the box),
3. camera distance ``d = h.z + max(h.x, h.y) / (fill * tan(fov / 2))`` - the front of the box fills
   ``fill`` of the cell in its larger direction.

This module is the reference implementation of that rule (the review sheet uses it) and reads the
Lua item instances (``Item "<id>" { mesh = ..., category = ... }``) for the category of a model.
"""

from __future__ import annotations

import math
import re
import tomllib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from gothar_chargen.gltf import Gltf

VIEWS_FILE = Path("assets/source/data/inventory_views.toml")
SCRIPTS_ITEMS_DIR = Path("game/scripts/items")
ITEMS_DIR = Path("assets/source/items")
VERSION = 1
FIELDS = ("yaw", "pitch", "roll", "fill")
# the Lua item categories (engine: gameplay/Character.hpp kItemCategories)
CATEGORIES = (
    "melee_1h", "melee_2h", "bow", "crossbow", "ammo", "armor", "helmet", "ring", "amulet",
    "belt", "rune", "scroll", "potion", "food", "document", "key", "torch", "misc",
)  # fmt: skip


class ViewError(Exception):
    """The views file breaks the format."""


@dataclass(frozen=True)
class View:
    yaw: float
    pitch: float
    roll: float
    fill: float


@dataclass(frozen=True)
class Views:
    fov: float
    default: dict[str, float]
    categories: dict[str, dict[str, float]]
    models: dict[str, dict[str, float | str]]

    def category_of(self, mesh: str, item_category: str | None) -> str | None:
        own = self.models.get(mesh, {}).get("category")
        return str(own) if own is not None else item_category

    def resolve(self, mesh: str, item_category: str | None = None) -> View:
        """The view of a model: default <- category <- model, field by field."""
        values = dict(self.default)
        category = self.category_of(mesh, item_category)
        if category is not None:
            values.update(self.categories.get(category, {}))
        values.update({k: v for k, v in self.models.get(mesh, {}).items() if k in FIELDS})
        return View(**{k: float(values[k]) for k in FIELDS})  # type: ignore[arg-type]


def _fields(table: object, where: str, allow_category: bool = False) -> dict:
    if not isinstance(table, dict):
        raise ViewError(f"{where}: expected a table")
    out: dict = {}
    for key, value in table.items():
        if key == "category" and allow_category:
            if value not in CATEGORIES:
                raise ViewError(f"{where}.category: unknown category {value!r}")
            out[key] = value
        elif key in FIELDS:
            if not isinstance(value, int | float) or isinstance(value, bool):
                raise ViewError(f"{where}.{key}: expected a number")
            if key == "fill" and not 0.1 <= value <= 1.0:
                raise ViewError(f"{where}.fill: {value} outside 0.1..1")
            if key != "fill" and not -360.0 <= value <= 360.0:
                raise ViewError(f"{where}.{key}: {value} outside -360..360 degrees")
            out[key] = float(value)
        else:
            raise ViewError(f"{where}: unknown key {key!r}")
    return out


def parse_views(text: str) -> Views:
    data = tomllib.loads(text)
    if data.get("version") != VERSION:
        raise ViewError(f"version must be {VERSION}")
    unknown = set(data) - {"version", "camera", "default", "category", "model"}
    if unknown:
        raise ViewError(f"unknown tables {sorted(unknown)}")
    fov = data.get("camera", {}).get("fov")
    if not isinstance(fov, int | float) or not 5.0 <= fov <= 90.0:
        raise ViewError("[camera] fov: expected 5..90 degrees")
    default = _fields(data.get("default"), "default")
    if set(default) != set(FIELDS):
        raise ViewError(f"[default] needs all of {FIELDS}")
    categories = {}
    for name, table in data.get("category", {}).items():
        if name not in CATEGORIES:
            raise ViewError(f"[category.{name}]: unknown category")
        categories[name] = _fields(table, f"category.{name}")
    models = {}
    for mesh, table in data.get("model", {}).items():
        if not re.fullmatch(r"items/[a-z0-9_]+\.glb", mesh):
            raise ViewError(f'[model."{mesh}"]: expected "items/<id>.glb"')
        models[mesh] = _fields(table, f'model."{mesh}"', allow_category=True)
    return Views(float(fov), default, categories, models)


def load_views(path: Path) -> Views:
    return parse_views(path.read_text(encoding="utf-8"))


# --- Lua items ------------------------------------------------------------------------------------

_ITEM = re.compile(r'Item\s*(?:"([a-z0-9_]+)"|\(\s*(\w+)\s*\))\s*\{(.*?)\n\s*\}', re.S)
_FIELD = re.compile(r'\b(mesh|category)\s*=\s*("[^"]*"(?:\s*\.\.\s*\w+\s*\.\.\s*"[^"]*")?)')
_CALL = re.compile(r'^(\w+)\("([a-z0-9_]+)"', re.M)


@dataclass(frozen=True)
class LuaItem:
    id: str
    mesh: str | None
    category: str


def _lua_value(expr: str, id_: str, param: str | None) -> str:
    """A string literal, or ``"a" .. id .. "b"`` inside a helper function (param = the id)."""
    parts = [p.strip() for p in expr.split("..")]
    out = ""
    for p in parts:
        if p.startswith('"'):
            out += p.strip('"')
        elif p == param:
            out += id_
        else:
            raise ValueError(f"unsupported Lua expression {expr!r}")
    return out


def lua_items(scripts_dir: Path) -> list[LuaItem]:
    """Item instances of the Lua scripts: literal ``Item "id" {…}`` and helpers ``Item(id) {…}``
    that are called as ``helper("id", ...)`` (runes, scrolls); default category ``misc``."""
    items: list[LuaItem] = []
    for path in sorted(scripts_dir.glob("*.lua")):
        text = path.read_text(encoding="utf-8")
        helpers: dict[str, tuple[str, dict[str, str]]] = {}
        for m in _ITEM.finditer(text):
            literal, param, body = m.groups()
            fields = dict((k, v) for k, v in _FIELD.findall(body))
            if literal:
                values = {k: _lua_value(v, literal, None) for k, v in fields.items()}
                items.append(LuaItem(literal, values.get("mesh"), values.get("category", "misc")))
            else:
                fn = re.findall(r"local\s+function\s+(\w+)\s*\(([^)]*)\)", text[: m.start()])
                if fn:
                    helpers[fn[-1][0]] = (param, fields)
        for name, id_ in _CALL.findall(text):
            if name in helpers:
                param, fields = helpers[name]
                values = {k: _lua_value(v, id_, param) for k, v in fields.items()}
                items.append(LuaItem(id_, values.get("mesh"), values.get("category", "misc")))
    return items


def model_categories(items: list[LuaItem], models: list[str]) -> dict[str, set[str]]:
    """Per model path the categories of the items that show it: by ``mesh``, else (no mesh in the
    script yet) by the convention ``items/<id>.glb``."""
    out: dict[str, set[str]] = {m: set() for m in models}
    for it in items:
        mesh = it.mesh or f"items/{it.id}.glb"
        if mesh in out and (it.mesh or not any(o.mesh == mesh for o in items)):
            out[mesh].add(it.category)
    return out


# --- cell geometry --------------------------------------------------------------------------------


def rotation(view: View) -> np.ndarray:
    """R = Rz(roll) @ Rx(pitch) @ Ry(yaw), degrees, right-handed, applied to column vectors."""
    y, p, r = (math.radians(a) for a in (view.yaw, view.pitch, view.roll))
    ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
    rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
    return rz @ rx @ ry


@dataclass(frozen=True)
class Cell:
    matrix: np.ndarray  # 4x4 model -> view space (rotation, then centring)
    distance: float  # camera on +Z at this distance, looking down -Z
    fov: float


def cell(view: View, lo: np.ndarray, hi: np.ndarray, fov: float) -> Cell:
    """Model matrix and camera distance for a model with local bounding box (lo, hi)."""
    rot = rotation(view)
    corners = np.array(
        [[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    )
    turned = corners @ rot.T
    a, b = turned.min(axis=0), turned.max(axis=0)
    centre, half = (a + b) / 2, (b - a) / 2
    matrix = np.eye(4)
    matrix[:3, :3] = rot
    matrix[:3, 3] = -centre
    distance = half[2] + max(half[0], half[1]) / (view.fill * math.tan(math.radians(fov) / 2))
    return Cell(matrix, float(distance), fov)


def model_bounds(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Local bounding box of LOD 0 (node ``<id>_lod0``; nodes of items carry no transform)."""
    g = Gltf.load(path)
    lod0 = next(n for n in g.list("nodes") if n.get("name") == f"{path.stem}_lod0")
    prims = g.doc["meshes"][lod0["mesh"]]["primitives"]
    pos = np.concatenate([np.asarray(g.accessor(p["attributes"]["POSITION"])) for p in prims])
    return pos.min(axis=0), pos.max(axis=0)
