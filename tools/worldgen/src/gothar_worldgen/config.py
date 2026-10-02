"""Loading and validating worldgen configuration.

Two files in ``tools/worldgen/config/``:

* ``<site>.toml`` (versioned): area, origin and game scale of a site, e.g. ``leonberg.toml``.
* ``local.toml`` (not versioned): machine-specific paths such as ``DATA_ROOT``.
"""

from __future__ import annotations

import os
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

from gothar_worldgen.geo.bbox import BBox

CONFIG_DIR_ENV = "GOTHAR_WORLDGEN_CONFIG"
DATA_ROOT_ENV = "GOTHAR_DATA_ROOT"
SUPPORTED_CRS = ("EPSG:25832",)

AreaName = Literal["core", "surroundings"]


class ConfigError(Exception):
    """Missing or invalid configuration; the message is meant for the user."""


def default_config_dir() -> Path:
    """``$GOTHAR_WORLDGEN_CONFIG`` if set, else ``tools/worldgen/config`` of this checkout."""
    env = os.environ.get(CONFIG_DIR_ENV)
    if env:
        return Path(env)
    # src/gothar_worldgen/config.py -> tools/worldgen/config
    return Path(__file__).resolve().parents[2] / "config"


@dataclass(frozen=True)
class Origin:
    """Origin of the local engine coordinate system, in source CRS metres."""

    easting: float
    northing: float
    height_reference: str


@dataclass(frozen=True)
class GameScale:
    horizontal: float
    vertical: float
    alley_widen_factor: float


@dataclass(frozen=True)
class SiteConfig:
    name: str
    crs: str
    origin: Origin
    core_half_extent_m: float
    surroundings_half_extent_m: float
    game_scale: GameScale

    def bbox(self, area: AreaName = "surroundings") -> BBox:
        """Square area around the origin in source CRS metres."""
        half = self.core_half_extent_m if area == "core" else self.surroundings_half_extent_m
        return BBox.around(self.origin.easting, self.origin.northing, half)


@dataclass(frozen=True)
class LocalConfig:
    data_root: Path
    blender: Path | None


@dataclass(frozen=True)
class DataPaths:
    """Raw-data and work directories below ``DATA_ROOT`` (see docs/design/leonberg-pipeline.md)."""

    data_root: Path
    site: str

    @property
    def dgm1(self) -> Path:
        return self.data_root / "geo" / "lgl" / "dgm1"

    @property
    def lod2(self) -> Path:
        return self.data_root / "geo" / "lgl" / "lod2"

    @property
    def dop(self) -> Path:
        return self.data_root / "geo" / "lgl" / "dop"

    @property
    def osm(self) -> Path:
        return self.data_root / "geo" / "osm"

    @property
    def capture(self) -> Path:
        return self.data_root / "capture"

    @property
    def work(self) -> Path:
        return self.data_root / "work" / self.site

    def raw_inputs(self) -> dict[str, Path]:
        """Input directories the geo import reads from, by short name."""
        return {"dgm1": self.dgm1, "lod2": self.lod2, "dop": self.dop, "osm": self.osm}


def _read_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as f:
            return tomllib.load(f)
    except FileNotFoundError:
        raise ConfigError(f"configuration file not found: {path}") from None
    except tomllib.TOMLDecodeError as e:
        raise ConfigError(f"{path}: invalid TOML: {e}") from None


def _get(table: Mapping[str, Any], key: str, where: str) -> Any:  # noqa: ANN401
    if key not in table:
        raise ConfigError(f"{where}: missing key '{key}'")
    return table[key]


def _table(table: Mapping[str, Any], key: str, where: str) -> Mapping[str, Any]:
    value = _get(table, key, where)
    if not isinstance(value, Mapping):
        raise ConfigError(f"{where}: '{key}' must be a table")
    return value


def _number(table: Mapping[str, Any], key: str, where: str, *, positive: bool = False) -> float:
    value = _get(table, key, where)
    # bool is an int subclass; reject it explicitly.
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ConfigError(f"{where}: '{key}' must be a number, got {value!r}")
    if positive and value <= 0:
        raise ConfigError(f"{where}: '{key}' must be > 0, got {value}")
    return float(value)


def _string(table: Mapping[str, Any], key: str, where: str) -> str:
    value = _get(table, key, where)
    if not isinstance(value, str) or not value:
        raise ConfigError(f"{where}: '{key}' must be a non-empty string")
    return value


def parse_site(name: str, data: Mapping[str, Any], where: str = "<site>") -> SiteConfig:
    """Validate a parsed site TOML document."""
    source = _table(data, "source", where)
    crs = _string(source, "crs", f"{where} [source]")
    if crs not in SUPPORTED_CRS:
        raise ConfigError(
            f"{where} [source]: unsupported crs '{crs}' (supported: {', '.join(SUPPORTED_CRS)})"
        )

    origin_t = _table(data, "origin", where)
    w = f"{where} [origin]"
    origin = Origin(
        easting=_number(origin_t, "easting", w),
        northing=_number(origin_t, "northing", w),
        height_reference=_string(origin_t, "height_reference", w),
    )

    area = _table(data, "area", where)
    core = _number(
        _table(area, "core", where), "half_extent_m", f"{where} [area.core]", positive=True
    )
    surroundings = _number(
        _table(area, "surroundings", where),
        "half_extent_m",
        f"{where} [area.surroundings]",
        positive=True,
    )
    if surroundings < core:
        raise ConfigError(
            f"{where}: area.surroundings ({surroundings} m) must not be smaller than "
            f"area.core ({core} m)"
        )

    scale_t = _table(data, "game_scale", where)
    w = f"{where} [game_scale]"
    scale = GameScale(
        horizontal=_number(scale_t, "horizontal", w, positive=True),
        vertical=_number(scale_t, "vertical", w, positive=True),
        alley_widen_factor=_number(scale_t, "alley_widen_factor", w, positive=True),
    )

    return SiteConfig(
        name=name,
        crs=crs,
        origin=origin,
        core_half_extent_m=core,
        surroundings_half_extent_m=surroundings,
        game_scale=scale,
    )


def load_site(name: str, config_dir: Path | None = None) -> SiteConfig:
    """Load ``<config_dir>/<name>.toml``."""
    if not name or not name.replace("_", "").replace("-", "").isalnum():
        raise ConfigError(f"invalid site name '{name}'")
    path = (config_dir or default_config_dir()) / f"{name}.toml"
    return parse_site(name, _read_toml(path), where=str(path))


def load_local(config_dir: Path | None = None) -> LocalConfig:
    """Load ``local.toml``; ``$GOTHAR_DATA_ROOT`` overrides ``paths.data_root``.

    Without ``local.toml`` the environment variable alone is sufficient (CI).
    """
    path = (config_dir or default_config_dir()) / "local.toml"
    env_root = os.environ.get(DATA_ROOT_ENV)

    paths: Mapping[str, Any] = {}
    if path.is_file():
        data = _read_toml(path)
        paths = _table(data, "paths", str(path)) if "paths" in data else {}
    elif not env_root:
        raise ConfigError(
            f"{path} not found. Copy local.example.toml to local.toml and set paths.data_root "
            f"(or set ${DATA_ROOT_ENV})."
        )

    data_root = Path(env_root or _string(paths, "data_root", f"{path} [paths]"))

    blender_value = paths.get("blender")
    if blender_value is not None and not isinstance(blender_value, str):
        raise ConfigError(f"{path} [paths]: 'blender' must be a string")
    return LocalConfig(data_root=data_root, blender=Path(blender_value) if blender_value else None)
