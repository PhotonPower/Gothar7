"""Figure manifests ``figures/<name>.figure.toml``: which parts make up a figure (F3 kit).

Format (version 1)::

    version = 1
    lods = [1.0, 0.5, 0.2]                 # triangle share per LOD level (lod0 = 1.0)

    [parts]                                # .glb files on the reference rig, relative to
    body = "parts/test/body_test.glb"      # assets/source/characters; body = base body or an
    head = "parts/test/head_test.glb"      # outfit/armour that replaces it (mesh swap, §6)
    hair = "parts/test/hair_test.glb"      # optional; beard likewise

    [palette]                              # material name -> colour (sRGB hex), tints the
    skin = "#c8a07a"                       # base colour of materials with that name
    cloth_a = "#6b5a3e"

The assembled figure is written next to the manifest as ``<name>.glb`` with mesh nodes named by
role and LOD level (``body_lod0``, ``head_lod1`` ...), see characters-pipeline.md §2.2 and fit.py.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from gothar_chargen.fit import ROLES

FORMAT_VERSION = 1
SUFFIX = ".figure.toml"
_COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")
_NAME = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")
REQUIRED_ROLES = ("body", "head")


class FigureError(Exception):
    """The figure manifest is malformed."""


@dataclass(frozen=True)
class Figure:
    name: str
    parts: dict[str, str]  # role -> part path (relative to the characters folder)
    lods: tuple[float, ...] = (1.0, 0.5, 0.2)
    palette: dict[str, tuple[float, float, float]] = field(default_factory=dict)  # linear RGB

    def part_paths(self, characters_dir: Path) -> dict[str, Path]:
        return {role: characters_dir / rel for role, rel in self.parts.items()}


def srgb_to_linear(hex_color: str) -> tuple[float, float, float]:
    """'#rrggbb' (sRGB) -> linear RGB as glTF base colours expect."""

    def channel(c: int) -> float:
        s = c / 255.0
        return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4

    r, g, b = (int(hex_color[i : i + 2], 16) for i in (1, 3, 5))
    return (channel(r), channel(g), channel(b))


def parse_figure(data: dict, name: str) -> Figure:
    if not _NAME.match(name):
        raise FigureError(f"figure name '{name}' must be lower_snake_case")
    if data.get("version") != FORMAT_VERSION:
        raise FigureError(f"version must be {FORMAT_VERSION}, got {data.get('version')!r}")
    unknown = set(data) - {"version", "lods", "parts", "palette"}
    if unknown:
        raise FigureError(f"unknown keys: {sorted(unknown)}")

    parts = data.get("parts")
    if not isinstance(parts, dict):
        raise FigureError("missing [parts] table")
    for role, path in parts.items():
        if role not in ROLES:
            raise FigureError(f"unknown part role '{role}' (allowed: {', '.join(ROLES)})")
        if not isinstance(path, str) or not path.endswith(".glb") or Path(path).is_absolute():
            raise FigureError(f"part '{role}': expected a relative .glb path, got {path!r}")
    missing = [r for r in REQUIRED_ROLES if r not in parts]
    if missing:
        raise FigureError(f"missing parts: {missing}")

    lods = data.get("lods", [1.0, 0.5, 0.2])
    if (
        not isinstance(lods, list)
        or not 1 <= len(lods) <= 3
        or lods[0] != 1.0
        or not all(isinstance(x, int | float) and 0 < x <= 1 for x in lods)
        or any(b >= a for a, b in zip(lods, lods[1:], strict=False))
    ):
        raise FigureError("lods must be 1-3 decreasing shares in (0, 1], starting with 1.0")

    palette_raw = data.get("palette", {})
    if not isinstance(palette_raw, dict):
        raise FigureError("[palette] must be a table")
    palette = {}
    for material, color in palette_raw.items():
        if not isinstance(color, str) or not _COLOR.match(color):
            raise FigureError(f"palette '{material}': expected '#rrggbb', got {color!r}")
        palette[material] = srgb_to_linear(color)
    return Figure(name, dict(parts), tuple(float(x) for x in lods), palette)


def load_figure(path: Path) -> Figure:
    if not path.name.endswith(SUFFIX):
        raise FigureError(f"{path.name}: figure manifests end in {SUFFIX}")
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as e:
        raise FigureError(str(e)) from e
    return parse_figure(data, path.name[: -len(SUFFIX)])
