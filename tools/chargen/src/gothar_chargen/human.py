"""Human recipes ``humans/<name>.human.toml``: an MPFB2 character as data (F3b, ADR 0018).

Format (version 1)::

    version = 1
    triangles = 15000                   # lod0 target for the exported parts (budget §2.2: <= 20000)
    parts = ["body", "head", "hair"]    # which parts to export (default: all); e.g. a base body
                                        # recipe exports ["body"], a head recipe ["head", "hair"]

A clothing kit recipe exports every garment as its own part, fitted to one base body::

    version = 1
    fit_to = "body_m_average"           # macros, skin and eyes from humans/body_m_average
    parts = ["cloth"]                   # -> parts/<name>/<garment>.glb (figure: [parts] cloth)
    [assets]
    clothes = ["clothes/toigo_fisherman_sweater/toigo_fisherman_sweater.mhclo"]

Armour kits (F3g) keep the colour textures of their pieces and may rename and derive pieces::

    neutral = false                     # keep colour textures (default: neutral grey + palette)
    [names]                             # source asset stem -> our part name (neutral names)
    rehmanpolanski_viking_tunic = "mail_tunic"
    [budget]                            # triangles of lod0 per piece (default: triangles / pieces)
    mail_tunic = 3000
    [derive.leather_vest]               # own simple piece derived from a fitted CC0 garment
    from = "clothes/elvs_crude_t-shirt_male/elvs_crude_t-shirt_male.mhclo"
    cut = ["upperarm"]                  # drop vertices bound mostly to bones with these prefixes
    offset = 0.008                      # push outwards along the normals (metres)
    texture = "gothar/ambientcg/Leather033A/Leather033A_1K-JPG_Color.jpg"  # tiling, CC0
    normal = "gothar/ambientcg/Leather033A/Leather033A_1K-JPG_NormalGL.jpg"  # optional
    uv_scale = 2.0                      # texture repeats on the source UVs

A base or character recipe::

    [macro]                             # MPFB macro values, 0..1
    gender = 1.0
    age = 0.75
    race = { caucasian = 1.0 }

    [assets]                            # paths below the MPFB user data folder; CC0 packs only
    skin = "skins/middleage_caucasian_male/middleage_caucasian_male.mhmat"
    eyes = "eyes/low-poly/low-poly.mhclo"
    eyebrows = "eyebrows/eyebrow001/eyebrow001.mhclo"
    eyelashes = "eyelashes/eyelashes01/eyelashes01.mhclo"
    teeth = "teeth/teeth_base/teeth_base.mhclo"     # needed by the face morphs (open mouth)
    tongue = "tongue/tongue01/tongue01.mhclo"
    hair = "hair/cortu_short_messy_hair/cortu_short_messy_hair.mhclo"
    beard = "clothes/rehmanpolanski_beard_viking/rehmanpolanski_beard_viking.mhclo"  # in head.glb
    clothes = ["clothes/toigo_wool_pants/toigo_wool_pants.mhclo"]

    [shape]                             # MPFB2 core targets (face/body shape), value 0..1
    nose-hump-incr = 0.6

    [tint]                              # multiply the base colour texture (asset stem or "skin")
    toigo_wool_pants = "#bf8559"

``gothar-chargen human`` builds the character with MPFB (local Blender only), conforms it to the
reference rig and writes the parts ``parts/<name>/{body,head,hair}.glb`` with external textures in
``textures/`` plus a figure manifest ``figures/<name>.figure.toml``.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

FORMAT_VERSION = 1
SUFFIX = ".human.toml"
MACROS = ("gender", "age", "muscle", "weight", "proportions", "height", "cupsize", "firmness")
RACES = ("african", "asian", "caucasian")
PARTS = ("body", "head", "hair", "cloth")
DEFAULT_PARTS = ("body", "head", "hair")
_TARGET = re.compile(r"^[a-z0-9]+(?:[-_][a-z0-9]+)*$")
_COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")
_NAME = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")
TRIANGLES_MAX = 20_000


class HumanError(Exception):
    """The human recipe is malformed."""


@dataclass(frozen=True)
class Derive:
    """An own simple garment derived from a fitted CC0 garment (cut, offset, own texture)."""

    name: str
    source: str  # mhclo path below the MPFB data folder
    texture: str  # tiling colour texture below the MPFB data folder
    normal: str | None = None
    cut: tuple[str, ...] = ()  # bone name prefixes of the reference rig
    offset: float = 0.0  # metres along the vertex normals
    uv_scale: float = 1.0


@dataclass(frozen=True)
class Human:
    name: str
    macro: dict[str, float]
    race: dict[str, float]
    skin: str
    eyes: str
    eyebrows: str | None = None
    eyelashes: str | None = None
    teeth: str | None = None
    tongue: str | None = None
    hair: str | None = None
    beard: str | None = None
    clothes: tuple[str, ...] = ()
    tints: dict[str, str] = field(default_factory=dict)
    triangles: int = 15_000
    parts: tuple[str, ...] = DEFAULT_PARTS
    shape: dict[str, float] = field(default_factory=dict)  # MPFB target -> value
    fit_to: str | None = None  # clothing kits: the base body recipe they are fitted to
    neutral: bool = True  # kits: neutral grey textures (palette colours them); armour: False
    names: dict[str, str] = field(default_factory=dict)  # asset stem -> part name
    budget: dict[str, int] = field(default_factory=dict)  # part name -> lod0 triangles
    derive: tuple[Derive, ...] = ()

    def part_name(self, stem: str) -> str:
        return self.names.get(stem, stem)

    def assets(self) -> list[tuple[str, str]]:
        """(MPFB asset type, path) for every mhclo asset, in loading order."""
        out = [("Eyes", self.eyes)]
        if self.eyebrows:
            out.append(("Eyebrows", self.eyebrows))
        if self.eyelashes:
            out.append(("Eyelashes", self.eyelashes))
        if self.teeth:
            out.append(("Teeth", self.teeth))
        if self.tongue:
            out.append(("Tongue", self.tongue))
        if self.beard:
            out.append(("Beard", self.beard))  # MPFB loads beards as clothes
        out += [("Clothes", c) for c in self.clothes]
        if self.hair:
            out.append(("Hair", self.hair))
        return out


def asset_stem(path: str) -> str:
    return PurePosixPath(path).stem


def _asset_path(value: object, where: str, ext: str) -> str:
    if not isinstance(value, str) or not value.endswith(ext):
        raise HumanError(f"{where}: expected a path ending in {ext}, got {value!r}")
    p = PurePosixPath(value)
    if p.is_absolute() or ".." in p.parts or "\\" in value:
        raise HumanError(f"{where}: path must be relative to the MPFB data folder (with /)")
    return value


def parse_human(data: dict, name: str, base: Human | None = None) -> Human:
    """`base` is the recipe named by ``fit_to`` (clothing kits inherit its macros, skin, eyes)."""
    if not _NAME.match(name):
        raise HumanError(f"name '{name}' must be lower_snake_case")
    if data.get("version") != FORMAT_VERSION:
        raise HumanError(f"version must be {FORMAT_VERSION}, got {data.get('version')!r}")
    allowed_keys = {
        "version", "triangles", "parts", "macro", "assets", "tint", "shape", "fit_to",
        "neutral", "names", "budget", "derive",
    }  # fmt: skip
    unknown = set(data) - allowed_keys
    if unknown:
        raise HumanError(f"unknown keys: {sorted(unknown)}")
    fit_to = data.get("fit_to")
    if fit_to is not None:
        if base is None or base.name != fit_to:
            raise HumanError(f"fit_to '{fit_to}': base recipe not given")
        if "macro" in data or "shape" in data:
            raise HumanError("a recipe with fit_to takes [macro] and [shape] from its base")
        data = {
            **data,
            "macro": {**base.macro, "race": base.race},
            "shape": base.shape,
            "assets": {"skin": base.skin, "eyes": base.eyes, **data.get("assets", {})},
        }

    macro_raw = dict(data.get("macro", {}))
    race_raw = macro_raw.pop("race", {"caucasian": 1.0})
    macro: dict[str, float] = {}
    for key, value in macro_raw.items():
        if key not in MACROS:
            raise HumanError(f"unknown macro '{key}' (allowed: {', '.join(MACROS)})")
        if not isinstance(value, int | float) or not 0.0 <= value <= 1.0:
            raise HumanError(f"macro '{key}' must be between 0 and 1")
        macro[key] = float(value)
    if not isinstance(race_raw, dict) or not race_raw or set(race_raw) - set(RACES):
        raise HumanError(f"race must be a table with keys from {RACES}")
    total = sum(float(v) for v in race_raw.values())
    if total <= 0:
        raise HumanError("race weights must not all be 0")
    race = {r: float(race_raw.get(r, 0.0)) / total for r in RACES}

    assets = data.get("assets")
    if not isinstance(assets, dict):
        raise HumanError("missing [assets] table")
    allowed = {
        "skin", "eyes", "eyebrows", "eyelashes", "teeth", "tongue", "hair", "beard", "clothes",
    }  # fmt: skip
    if set(assets) - allowed:
        raise HumanError(f"unknown assets: {sorted(set(assets) - allowed)}")
    if "skin" not in assets or "eyes" not in assets:
        raise HumanError("assets need at least 'skin' and 'eyes'")
    clothes = assets.get("clothes", [])
    if not isinstance(clothes, list):
        raise HumanError("clothes must be a list")

    tints_raw = data.get("tint", {})
    if not isinstance(tints_raw, dict):
        raise HumanError("[tint] must be a table")
    for key, color in tints_raw.items():
        if not isinstance(color, str) or not _COLOR.match(color):
            raise HumanError(f"tint '{key}': expected '#rrggbb', got {color!r}")

    parts = data.get("parts", list(DEFAULT_PARTS))
    if not isinstance(parts, list) or not parts or set(parts) - set(PARTS):
        raise HumanError(f"parts must be a non-empty list from {PARTS}")
    if "cloth" in parts and "body" in parts:
        raise HumanError("parts: 'cloth' exports garments on their own, not together with 'body'")
    if "cloth" in parts and not assets_clothes(data) and not data.get("derive"):
        raise HumanError("parts: 'cloth' needs clothes in [assets] or [derive]")
    shape = data.get("shape", {})
    if not isinstance(shape, dict):
        raise HumanError("[shape] must be a table")
    for target, value in shape.items():
        if not _TARGET.match(target):
            raise HumanError(f"shape: bad MPFB target name '{target}'")
        if not isinstance(value, int | float) or not 0.0 < value <= 1.0:
            raise HumanError(f"shape '{target}' must be in (0, 1]")

    kit_keys = {"neutral", "names", "budget", "derive"} & set(data)
    if kit_keys and parts != ["cloth"]:
        raise HumanError(f'{sorted(kit_keys)}: only for garment kits (parts = ["cloth"])')
    neutral = data.get("neutral", True)
    if not isinstance(neutral, bool):
        raise HumanError("neutral must be true or false")
    derive = _parse_derive(data.get("derive", {}))
    names = data.get("names", {})
    stems = {asset_stem(c) for c in clothes}
    if not isinstance(names, dict) or not all(
        isinstance(v, str) and _NAME.match(v) for v in names.values()
    ):
        raise HumanError("[names] maps asset stems to lower_snake_case part names")
    if set(names) - stems:
        raise HumanError(f"[names] for assets not in this recipe: {sorted(set(names) - stems)}")
    pieces = [names.get(s, s) for s in sorted(stems)] + [d.name for d in derive]
    if len(set(pieces)) != len(pieces):
        raise HumanError(f"part names must be unique: {sorted(pieces)}")
    budget = data.get("budget", {})
    if not isinstance(budget, dict) or not all(
        isinstance(v, int) and 100 <= v <= TRIANGLES_MAX for v in budget.values()
    ):
        raise HumanError(f"[budget] maps part names to triangles (100..{TRIANGLES_MAX})")
    if set(budget) - set(pieces):
        raise HumanError(f"[budget] for unknown pieces: {sorted(set(budget) - set(pieces))}")

    triangles = data.get("triangles", 15_000)
    if not isinstance(triangles, int) or not 1_000 <= triangles <= TRIANGLES_MAX:
        raise HumanError(f"triangles must be an integer between 1000 and {TRIANGLES_MAX}")

    human = Human(
        name=name,
        macro=macro,
        race=race,
        skin=_asset_path(assets["skin"], "skin", ".mhmat"),
        eyes=_asset_path(assets["eyes"], "eyes", ".mhclo"),
        eyebrows=_asset_path(assets["eyebrows"], "eyebrows", ".mhclo")
        if "eyebrows" in assets
        else None,
        eyelashes=_asset_path(assets["eyelashes"], "eyelashes", ".mhclo")
        if "eyelashes" in assets
        else None,
        teeth=_asset_path(assets["teeth"], "teeth", ".mhclo") if "teeth" in assets else None,
        tongue=_asset_path(assets["tongue"], "tongue", ".mhclo") if "tongue" in assets else None,
        hair=_asset_path(assets["hair"], "hair", ".mhclo") if "hair" in assets else None,
        beard=_asset_path(assets["beard"], "beard", ".mhclo") if "beard" in assets else None,
        clothes=tuple(_asset_path(c, "clothes", ".mhclo") for c in clothes),
        tints=dict(tints_raw),
        triangles=triangles,
        parts=tuple(p for p in PARTS if p in parts),
        shape={k: float(v) for k, v in shape.items()},
        fit_to=fit_to,
        neutral=neutral,
        names=dict(names),
        budget=dict(budget),
        derive=derive,
    )
    known = {"skin"} | {asset_stem(p) for _, p in human.assets()}
    unknown_tints = set(human.tints) - known
    if unknown_tints:
        raise HumanError(f"tint for assets not in this recipe: {sorted(unknown_tints)}")
    return human


def _parse_derive(raw: object) -> tuple[Derive, ...]:
    if not isinstance(raw, dict):
        raise HumanError("[derive] must be a table of [derive.<name>] tables")
    out = []
    for name, d in sorted(raw.items()):
        where = f"derive.{name}"
        if not _NAME.match(name):
            raise HumanError(f"{where}: name must be lower_snake_case")
        if not isinstance(d, dict):
            raise HumanError(f"{where}: must be a table")
        unknown = set(d) - {"from", "texture", "normal", "cut", "offset", "uv_scale"}
        if unknown:
            raise HumanError(f"{where}: unknown keys {sorted(unknown)}")
        if "from" not in d or "texture" not in d:
            raise HumanError(f"{where}: needs 'from' and 'texture'")
        cut = d.get("cut", [])
        if not isinstance(cut, list) or not all(isinstance(c, str) and c for c in cut):
            raise HumanError(f"{where}: cut must be a list of bone name prefixes")
        offset = d.get("offset", 0.0)
        if not isinstance(offset, int | float) or not 0.0 <= offset <= 0.03:
            raise HumanError(f"{where}: offset must be between 0 and 0.03 m")
        uv_scale = d.get("uv_scale", 1.0)
        if not isinstance(uv_scale, int | float) or not 0.1 <= uv_scale <= 16:
            raise HumanError(f"{where}: uv_scale must be between 0.1 and 16")
        texture = d["texture"]
        if not isinstance(texture, str) or not texture.endswith((".jpg", ".png")):
            raise HumanError(f"{where}: texture must be a .jpg or .png path")
        normal = d.get("normal")
        if normal is not None and (
            not isinstance(normal, str) or not normal.endswith((".jpg", ".png"))
        ):
            raise HumanError(f"{where}: normal must be a .jpg or .png path")
        out.append(
            Derive(
                name=name,
                source=_asset_path(d["from"], f"{where}.from", ".mhclo"),
                texture=_asset_path(texture, f"{where}.texture", texture[-4:]),
                normal=_asset_path(normal, f"{where}.normal", normal[-4:]) if normal else None,
                cut=tuple(cut),
                offset=float(offset),
                uv_scale=float(uv_scale),
            )
        )
    return tuple(out)


def assets_clothes(data: dict) -> list:
    assets = data.get("assets", {})
    return assets.get("clothes", []) if isinstance(assets, dict) else []


def load_human(path: Path) -> Human:
    if not path.name.endswith(SUFFIX):
        raise HumanError(f"{path.name}: human recipes end in {SUFFIX}")
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as e:
        raise HumanError(str(e)) from e
    base = None
    if isinstance(data.get("fit_to"), str):
        base_path = path.with_name(data["fit_to"] + SUFFIX)
        if not base_path.is_file():
            raise HumanError(f"fit_to: {base_path.name} not found next to {path.name}")
        base = load_human(base_path)
    return parse_human(data, path.name[: -len(SUFFIX)], base)
