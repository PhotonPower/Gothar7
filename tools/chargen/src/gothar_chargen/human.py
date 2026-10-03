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
    [retouch]                           # cover a mark: rectangle <- texture shifted by dx, dy
    mail_tunic = [[0.16, 0.24, 0.26, 0.345, 0.48, 0.0]]
    [hides]                             # roles a piece hides while worn (hoods, helmets)
    hood = ["hair"]
    [derive.leather_vest]               # own simple piece derived from a fitted CC0 garment
    from = "clothes/elvs_crude_t-shirt_male/elvs_crude_t-shirt_male.mhclo"  # or "basemesh"
    cut = ["upperarm"]                  # drop vertices bound mostly to bones with these prefixes
    offset = 0.008                      # along the normals (metres; negative: tighter)
    depth = 0.12                        # optional: cut 12 cm below the top (caps, helmets)
    tilt = 20                           # with depth: the cut rises to the front (degrees)
    nasal = [0.02, 0.07]                # with depth: own nose guard, width and length (metres)
    group = "body"                      # with from = "basemesh": the MPFB vertex group to keep
    keep = ["spine"]                    # keep vertices bound mostly to these bones (prefixes)
    near = ["upperarm_l"]               # ... and within `radius` (m) of these joints
    smooth = 12                         # shrink-free smoothing: cloth folds -> a plate
    brim = 0.04                         # own brim around the rim (kettle helmet)
    bulge = 0.02                        # domed plate: further out towards the middle
    rim = 0.01                          # plate edge: the border folded inwards
    flatten = 0.8                       # neutral plate front (no anatomic shape), 0..1
    bones = ["spine_02", "spine_03"]    # weights only on these bones (stiff plates)
    dome = true                         # with from = "basemesh": smooth dome fitted to the skull
    heads = "head_f_*"                  # these head parts fit under it (dome grows, others bulge)
    texture = "gothar/ambientcg/Leather033A/Leather033A_1K-JPG_Color.jpg"  # optional, tiling
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
_HEADS = re.compile(r"^head_[a-z0-9_*]+$")
TRIANGLES_MAX = 20_000
HIDEABLE = ("hair", "beard")  # roles a worn piece may hide (§6.2)
BASEMESH = "basemesh"  # derive from the character's own skin (caps and helmets)
DERIVE_KEYS = {
    "from",
    "texture",
    "normal",
    "cut",
    "offset",
    "uv_scale",
    "depth",
    "tilt",
    "nasal",
    "group",
    "dome",
    "heads",
    "keep",
    "near",
    "radius",
    "smooth",
    "brim",
    "bones",
    "bulge",
    "rim",
    "flatten",
}


class HumanError(Exception):
    """The human recipe is malformed."""


@dataclass(frozen=True)
class Derive:
    """An own simple garment derived from a fitted CC0 garment (cut, offset, own texture)."""

    name: str
    source: str  # mhclo path below the MPFB data folder
    texture: str | None = None  # tiling colour texture below the MPFB data folder (else: source's)
    normal: str | None = None
    cut: tuple[str, ...] = ()  # bone name prefixes of the reference rig
    offset: float = 0.0  # metres along the vertex normals
    uv_scale: float = 1.0
    depth: float | None = None  # cut plane this far below the piece's top
    tilt: float = 0.0  # degrees the cut plane rises to the front
    nasal: tuple[float, float] | None = None  # own nose guard: width, length (metres)
    group: str | None = None  # from the skin: keep this MPFB vertex group ("body": the skin)
    dome: bool = False  # own geometry: a smooth dome fitted to the skull (caps, helmets)
    keep: tuple[str, ...] = ()  # keep only vertices bound mostly to bones with these prefixes
    near: tuple[str, ...] = ()  # keep only vertices within `radius` of these joints
    radius: float = 0.15
    smooth: int = 0  # shrink-free smoothing passes: cloth folds -> a plate
    brim: float = 0.0  # own geometry: a brim of this width around the rim (metres)
    bulge: float = 0.0  # domed plate: extra offset towards the middle (metres)
    rim: float = 0.0  # own geometry: the border folded inwards by this depth (plate edge)
    flatten: float = 0.0  # neutral plate: the front pulled towards a smooth envelope (0..1)
    bones: tuple[str, ...] = ()  # limit the weights to bones with these prefixes (stiff plates)
    heads: str | None = None  # head parts that must fit under the piece, e.g. "head_f_*"


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
    hides: dict[str, tuple[str, ...]] = field(default_factory=dict)  # part name -> roles
    # part name -> patches [x0, y0, x1, y1, dx, dy] (texture fractions, top left): the rectangle
    # gets the texture shifted by (dx, dy), e.g. to remove a mark of the source
    retouch: dict[str, tuple[tuple[float, ...], ...]] = field(default_factory=dict)

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
        "neutral", "names", "budget", "derive", "hides", "retouch",
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

    kit_keys = {"neutral", "names", "budget", "derive", "hides", "retouch"} & set(data)
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
    hides = data.get("hides", {})
    if not isinstance(hides, dict) or not all(
        isinstance(v, list) and v and set(v) <= set(HIDEABLE) for v in hides.values()
    ):
        raise HumanError(f"[hides] maps part names to lists of roles from {HIDEABLE}")
    if set(hides) - set(pieces):
        raise HumanError(f"[hides] for unknown pieces: {sorted(set(hides) - set(pieces))}")
    retouch = data.get("retouch", {})
    if not isinstance(retouch, dict) or not all(
        isinstance(v, list) and v and all(_patch_ok(r) for r in v) for v in retouch.values()
    ):
        raise HumanError(
            "[retouch] maps part names to lists of [x0, y0, x1, y1, dx, dy] inside the texture"
        )
    if set(retouch) - set(pieces):
        raise HumanError(f"[retouch] for unknown pieces: {sorted(set(retouch) - set(pieces))}")

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
        hides={k: tuple(v) for k, v in hides.items()},
        retouch={k: tuple(tuple(float(x) for x in r) for r in v) for k, v in retouch.items()},
    )
    known = {"skin"} | {asset_stem(p) for _, p in human.assets()}
    unknown_tints = set(human.tints) - known
    if unknown_tints:
        raise HumanError(f"tint for assets not in this recipe: {sorted(unknown_tints)}")
    return human


def _patch_ok(r: object) -> bool:
    """A retouch patch [x0, y0, x1, y1, dx, dy]: rectangle and its shifted source in [0, 1]."""
    if not isinstance(r, list) or len(r) != 6 or not all(isinstance(x, int | float) for x in r):
        return False
    x0, y0, x1, y1, dx, dy = r
    return (
        0 <= x0 < x1 <= 1
        and 0 <= y0 < y1 <= 1
        and x0 + dx >= 0
        and x1 + dx <= 1
        and (y0 + dy >= 0 and y1 + dy <= 1)
    )


def _names(d: dict, key: str, where: str) -> tuple[str, ...]:
    value = d.get(key, [])
    if not isinstance(value, list) or not all(isinstance(x, str) and x for x in value):
        raise HumanError(f"{where}: {key} must be a list of bone names or prefixes")
    return tuple(value)


def _number(d: dict, key: str, where: str, low: float, high: float, default: float) -> float:
    value = d.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int | float) or not low <= value <= high:
        raise HumanError(f"{where}: {key} must be between {low} and {high}")
    return float(value)


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
        unknown = set(d) - DERIVE_KEYS
        if unknown:
            raise HumanError(f"{where}: unknown keys {sorted(unknown)}")
        if "from" not in d:
            raise HumanError(f"{where}: needs 'from'")
        cut = d.get("cut", [])
        if not isinstance(cut, list) or not all(isinstance(c, str) and c for c in cut):
            raise HumanError(f"{where}: cut must be a list of bone name prefixes")
        offset = d.get("offset", 0.0)
        if not isinstance(offset, int | float) or not -0.02 <= offset <= 0.06:
            raise HumanError(f"{where}: offset must be between -0.02 and 0.06 m")
        depth = d.get("depth")
        if depth is not None and (not isinstance(depth, int | float) or not 0.02 <= depth <= 0.5):
            raise HumanError(f"{where}: depth must be between 0.02 and 0.5 m")
        tilt = d.get("tilt", 0.0)
        if not isinstance(tilt, int | float) or not 0.0 <= tilt <= 45.0 or (tilt and depth is None):
            raise HumanError(f"{where}: tilt must be 0..45 degrees and needs depth")
        group = d.get("group")
        if (group is None) != (d["from"] != BASEMESH) or (
            group is not None and not isinstance(group, str)
        ):
            raise HumanError(f'{where}: from = "{BASEMESH}" needs a vertex group, others none')
        dome = d.get("dome", False)
        if not isinstance(dome, bool) or (dome and d["from"] != BASEMESH):
            raise HumanError(f'{where}: dome = true needs from = "{BASEMESH}"')
        heads = d.get("heads")
        if heads is not None and (not isinstance(heads, str) or not _HEADS.match(heads)):
            raise HumanError(f"{where}: heads must be a part folder pattern like head_f_*")
        nasal = d.get("nasal")
        if nasal is not None and (
            depth is None
            or not isinstance(nasal, list)
            or len(nasal) != 2
            or not all(isinstance(x, int | float) for x in nasal)
            or not (0.005 <= nasal[0] <= 0.05 and 0.02 <= nasal[1] <= 0.12)
        ):
            raise HumanError(
                f"{where}: nasal = [width 0.005..0.05, length 0.02..0.12], needs depth"
            )
        uv_scale = d.get("uv_scale", 1.0)
        if not isinstance(uv_scale, int | float) or not 0.1 <= uv_scale <= 16:
            raise HumanError(f"{where}: uv_scale must be between 0.1 and 16")
        texture = d.get("texture")
        if texture is not None and (
            not isinstance(texture, str) or not texture.endswith((".jpg", ".png"))
        ):
            raise HumanError(f"{where}: texture must be a .jpg or .png path")
        normal = d.get("normal")
        if normal is not None and (
            not isinstance(normal, str) or not normal.endswith((".jpg", ".png"))
        ):
            raise HumanError(f"{where}: normal must be a .jpg or .png path")
        out.append(
            Derive(
                name=name,
                source=d["from"]
                if d["from"] == BASEMESH
                else _asset_path(d["from"], f"{where}.from", ".mhclo"),
                texture=_asset_path(texture, f"{where}.texture", texture[-4:]) if texture else None,
                normal=_asset_path(normal, f"{where}.normal", normal[-4:]) if normal else None,
                cut=tuple(cut),
                offset=float(offset),
                uv_scale=float(uv_scale),
                depth=float(depth) if depth is not None else None,
                tilt=float(tilt),
                nasal=(float(nasal[0]), float(nasal[1])) if nasal is not None else None,
                group=group,
                dome=dome,
                heads=heads,
                keep=_names(d, "keep", where),
                near=_names(d, "near", where),
                radius=_number(d, "radius", where, 0.02, 0.5, 0.15),
                smooth=int(_number(d, "smooth", where, 0, 50, 0)),
                brim=_number(d, "brim", where, 0.0, 0.1, 0.0),
                bulge=_number(d, "bulge", where, 0.0, 0.1, 0.0),
                rim=_number(d, "rim", where, 0.0, 0.05, 0.0),
                flatten=_number(d, "flatten", where, 0.0, 1.0, 0.0),
                bones=_names(d, "bones", where),
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
