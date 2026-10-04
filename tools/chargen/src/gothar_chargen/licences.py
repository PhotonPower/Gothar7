"""Licence check of the MPFB assets a human recipe uses (public repo: CC0 only, F3v).

MakeHuman packs listed as CC0 contain single items whose file header says AGPL3 or CC BY, so the
pack page alone is not enough. For every asset of a recipe (``.mhclo``, ``.mhmat`` and the material
an ``.mhclo`` names, textures) the check reads the ``license`` line of the file header:

* a licence other than CC0 → rejected;
* CC0 (``CC0``, ``CC-0``, ``CC0 1.0`` …) → accepted;
* no licence line → accepted only when the asset folder is listed in ``data/asset_licences.toml``
  with its CC0 source (the matching row of ``assets/LICENSES.md``).
"""

from __future__ import annotations

import os
import re
import tomllib
from importlib import resources
from pathlib import Path
from typing import Any

HEADER_LINES = 40  # the licence line sits in the comment block at the top
_LICENCE = re.compile(r"^\s*#?\s*licen[cs]e\b\s*:?\s*(.*)$", re.IGNORECASE)
_MATERIAL = re.compile(r"^\s*material\s+(\S+)")
_ASSET = re.compile(r"\.(mhclo|mhmat|png|jpe?g)$", re.IGNORECASE)


class LicenceError(Exception):
    """An asset whose licence is not CC0 or not documented."""


def default_data_root() -> Path:
    """The MPFB user data folder (``MPFB_DATA`` overrides the Blender 4.5 extension folder)."""
    if env := os.environ.get("MPFB_DATA"):
        return Path(env)
    appdata = Path(os.environ.get("APPDATA", Path.home()))
    return appdata / "Blender Foundation/Blender/4.5/extensions/.user/blender_org/mpfb/data"


def header_licence(text: str) -> str | None:
    """The licence named in the file header, or None without a licence line."""
    for line in text.splitlines()[:HEADER_LINES]:
        if m := _LICENCE.match(line):
            return m.group(1).strip() or None
    return None


def is_cc0(licence: str) -> bool:
    """CC0 in its usual spellings (``CC0``, ``CC-0``, ``cc0 1.0``, ``CC0 1.0 Universal``)."""
    return re.sub(r"[^a-z0-9]", "", licence.lower()).startswith("cc0")


def headerless_sources() -> dict[str, str]:
    """Asset folder (below the MPFB data folder) -> CC0 source, for files without licence line."""
    text = resources.files("gothar_chargen.data").joinpath("asset_licences.toml").read_text("utf-8")
    data = tomllib.loads(text)
    return {k.rstrip("/"): str(v) for k, v in data.get("headerless", {}).items()}


def recipe_assets(data: dict[str, Any]) -> list[str]:
    """Every asset path a parsed recipe names (strings ending in .mhclo/.mhmat/.png/.jpg)."""
    out: list[str] = []

    def walk(value: object) -> None:
        if isinstance(value, str) and _ASSET.search(value):
            out.append(value)
        elif isinstance(value, list):
            for v in value:
                walk(v)
        elif isinstance(value, dict):
            for v in value.values():
                walk(v)

    walk(data)
    return sorted(set(out))


def _check_file(path: Path, rel: str, allowed: dict[str, str]) -> str | None:
    """Error text for one file, None when it may be used."""
    if not path.is_file():
        return f"{rel}: not installed (expected below the MPFB data folder)"
    licence = None
    if path.suffix.lower() in (".mhclo", ".mhmat"):
        licence = header_licence(path.read_text(encoding="utf-8", errors="replace"))
    if licence is not None:
        return None if is_cc0(licence) else f"{rel}: licence '{licence}' (only CC0 may be used)"
    if str(Path(rel).parent.as_posix()) in allowed:
        return None
    return (
        f"{rel}: no licence line in the file header; check the source and list the folder in "
        "data/asset_licences.toml with its CC0 source"
    )


def check_recipe(recipe: Path, data_root: Path | None = None) -> list[str]:
    """Licence errors of all assets a recipe uses (empty: all CC0)."""
    root = data_root or default_data_root()
    data = tomllib.loads(recipe.read_text(encoding="utf-8"))
    allowed = headerless_sources()
    errors: list[str] = []
    for rel in recipe_assets(data):
        path = root / rel
        if error := _check_file(path, rel, allowed):
            errors.append(error)
            continue
        if path.suffix.lower() != ".mhclo":
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            # the material the asset names counts too; without a licence line it shares the
            # verdict of its asset
            if (m := _MATERIAL.match(line)) and (mat := path.parent / m.group(1)).is_file():
                licence = header_licence(mat.read_text(encoding="utf-8", errors="replace"))
                if licence is not None and not is_cc0(licence):
                    errors.append(f"{rel}: material {m.group(1)} has licence '{licence}'")
    return [f"{recipe.name}: {e}" for e in errors]
