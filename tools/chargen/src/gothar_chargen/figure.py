"""Figure manifests ``figures/<name>.figure.toml``: which parts make up a figure (F3 kit).

Format (version 1)::

    version = 1

    [parts]                                # .glb files on the reference rig, relative to
    body = "parts/test/body_test.glb"      # assets/source/characters; body = base body or an
    head = "parts/test/head_test.glb"      # outfit/armour that replaces it (mesh swap, §6)
    hair = "parts/test/hair_test.glb"      # optional; beard likewise
    cloth = ["parts/cloth_m_average/toigo_fisherman_sweater.glb"]  # optional garments (kit):
                                           # role cloth_<garment>; the body under them is hidden

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
CLOTH_PREFIX = "cloth_"


def _absolute(path: str) -> bool:
    """Absolute on any platform (a leading slash counts on Windows, too)."""
    return Path(path).is_absolute() or path.startswith(("/", "\\"))


def cloth_role(path: str) -> str:
    """'parts/cloth_m_average/elvs_crude_t-shirt_male.glb' -> 'cloth_elvs_crude_t_shirt_male'."""
    stem = Path(path).stem.lower()
    return CLOTH_PREFIX + re.sub(r"[^a-z0-9]+", "_", stem).strip("_")


# walk-style variants (contract with engine 2026-10-08): [anim] variant = "<v>" makes the engine
# play none/X_<v> (anims/human/gait.glb) instead of none/X when that clip exists
VARIANTS = ("woman", "military", "old", "relaxed")


class FigureError(Exception):
    """The figure manifest is malformed."""


@dataclass(frozen=True)
class Figure:
    name: str
    parts: dict[str, str]  # role -> part path (relative to the characters folder)
    palette: dict[str, tuple[float, float, float]] = field(default_factory=dict)  # linear RGB
    variant: str | None = None  # [anim] variant: walk-style variant (VARIANTS)

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
    if "lods" in data:
        raise FigureError("lods: LOD levels come from the parts now (§6.2), remove the key")
    unknown = set(data) - {"version", "parts", "palette", "anim"}
    if unknown:
        raise FigureError(f"unknown keys: {sorted(unknown)}")

    parts = data.get("parts")
    if not isinstance(parts, dict):
        raise FigureError("missing [parts] table")
    parts = dict(parts)
    cloth = parts.pop("cloth", []) if isinstance(parts, dict) else []
    if not isinstance(cloth, list) or not all(isinstance(c, str) for c in cloth):
        raise FigureError("parts.cloth must be a list of .glb paths")
    for path in cloth:
        role = cloth_role(path)
        if role in parts:
            raise FigureError(f"garment '{path}' listed twice")
        parts[role] = path
    for role, path in parts.items():
        if role.startswith(CLOTH_PREFIX):
            if not path.endswith(".glb") or _absolute(path):
                raise FigureError(f"garment {path!r}: expected a relative .glb path")
            continue
        if role not in ROLES:
            raise FigureError(f"unknown part role '{role}' (allowed: {', '.join(ROLES)})")
        if not isinstance(path, str) or not path.endswith(".glb") or _absolute(path):
            raise FigureError(f"part '{role}': expected a relative .glb path, got {path!r}")
    missing = [r for r in REQUIRED_ROLES if r not in parts]
    if missing:
        raise FigureError(f"missing parts: {missing}")

    palette_raw = data.get("palette", {})
    if not isinstance(palette_raw, dict):
        raise FigureError("[palette] must be a table")
    palette = {}
    for material, color in palette_raw.items():
        if not isinstance(color, str) or not _COLOR.match(color):
            raise FigureError(f"palette '{material}': expected '#rrggbb', got {color!r}")
        palette[material] = srgb_to_linear(color)

    anim = data.get("anim", {})
    if not isinstance(anim, dict) or set(anim) - {"variant"}:
        raise FigureError("[anim] may only hold 'variant'")
    variant = anim.get("variant")
    if variant is not None and variant not in VARIANTS:
        raise FigureError(f"[anim] variant {variant!r}: expected one of {', '.join(VARIANTS)}")
    return Figure(name, dict(parts), palette, variant)


def load_figure(path: Path) -> Figure:
    if not path.name.endswith(SUFFIX):
        raise FigureError(f"{path.name}: figure manifests end in {SUFFIX}")
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as e:
        raise FigureError(str(e)) from e
    return parse_figure(data, path.name[: -len(SUFFIX)])
