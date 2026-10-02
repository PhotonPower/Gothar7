from pathlib import Path

import pytest

from gothar_worldgen.config import (
    CONFIG_DIR_ENV,
    DATA_ROOT_ENV,
    ConfigError,
    DataPaths,
    default_config_dir,
    load_local,
    load_site,
)

from .conftest import SITE_TOML


def _write_site(config_dir: Path, text: str, name: str = "testsite") -> None:
    (config_dir / f"{name}.toml").write_text(text, encoding="utf-8")


def _write_local(config_dir: Path, text: str) -> None:
    (config_dir / "local.toml").write_text(text, encoding="utf-8")


def test_load_valid_site(config_dir: Path):
    site = load_site("testsite", config_dir)
    assert site.name == "testsite"
    assert site.crs == "EPSG:25832"
    assert site.origin.easting == 500933.0
    assert site.origin.height_reference == "marktplatz"
    assert site.core_half_extent_m == 350
    assert site.surroundings_half_extent_m == 1000
    assert site.game_scale.alley_widen_factor == 1.0
    assert site.bbox("core").width == 700
    assert site.bbox().width == 2000


def test_shipped_leonberg_config_is_valid():
    site = load_site("leonberg")
    assert site.crs == "EPSG:25832"
    assert site.bbox("core").contains(site.origin.easting, site.origin.northing)


def test_missing_site_file(config_dir: Path):
    with pytest.raises(ConfigError, match="not found"):
        load_site("nowhere", config_dir)


@pytest.mark.parametrize("name", ["", "../leonberg", "a/b", "x.y"])
def test_invalid_site_name(config_dir: Path, name: str):
    with pytest.raises(ConfigError, match="invalid site name"):
        load_site(name, config_dir)


def test_invalid_toml(config_dir: Path):
    _write_site(config_dir, "[source\ncrs = 1")
    with pytest.raises(ConfigError, match="invalid TOML"):
        load_site("testsite", config_dir)


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ('crs = "EPSG:25832"', 'crs = "EPSG:4326"', "unsupported crs"),
        ("easting  = 500933.0", "", "missing key 'easting'"),
        ("easting  = 500933.0", 'easting = "500933"', "must be a number"),
        ("easting  = 500933.0", "easting = true", "must be a number"),
        ("half_extent_m = 350", "half_extent_m = 0", "must be > 0"),
        ("half_extent_m = 1000", "half_extent_m = 100", "must not be smaller"),
        ("vertical   = 1.0", "vertical = -1.0", "must be > 0"),
        ('height_reference = "marktplatz"', 'height_reference = ""', "non-empty string"),
        ('[source]\ncrs = "EPSG:25832"', 'source = "EPSG:25832"', "must be a table"),
    ],
)
def test_invalid_site_values(config_dir: Path, old: str, new: str, message: str):
    assert old in SITE_TOML
    _write_site(config_dir, SITE_TOML.replace(old, new, 1))
    with pytest.raises(ConfigError, match=message):
        load_site("testsite", config_dir)


def test_local_config(config_dir: Path):
    _write_local(config_dir, '[paths]\ndata_root = "D:/GotharData"\nblender = "C:/blender.exe"\n')
    local = load_local(config_dir)
    assert local.data_root == Path("D:/GotharData")
    assert local.blender == Path("C:/blender.exe")


def test_local_config_blender_is_optional(config_dir: Path):
    _write_local(config_dir, '[paths]\ndata_root = "/data"\n')
    assert load_local(config_dir).blender is None


def test_local_config_missing(config_dir: Path):
    with pytest.raises(ConfigError, match="local.example.toml"):
        load_local(config_dir)


def test_local_config_missing_data_root(config_dir: Path):
    _write_local(config_dir, "[paths]\n")
    with pytest.raises(ConfigError, match="data_root"):
        load_local(config_dir)


def test_local_config_invalid_blender(config_dir: Path):
    _write_local(config_dir, '[paths]\ndata_root = "/data"\nblender = 3\n')
    with pytest.raises(ConfigError, match="blender"):
        load_local(config_dir)


def test_env_data_root_overrides_file(config_dir: Path, monkeypatch: pytest.MonkeyPatch):
    _write_local(config_dir, '[paths]\ndata_root = "/file"\n')
    monkeypatch.setenv(DATA_ROOT_ENV, "/env")
    assert load_local(config_dir).data_root == Path("/env")


def test_env_data_root_without_local_file(config_dir: Path, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv(DATA_ROOT_ENV, "/env")
    local = load_local(config_dir)
    assert local.data_root == Path("/env")
    assert local.blender is None


def test_default_config_dir(monkeypatch: pytest.MonkeyPatch, tmp_path: Path):
    assert (default_config_dir() / "leonberg.toml").is_file()
    monkeypatch.setenv(CONFIG_DIR_ENV, str(tmp_path))
    assert default_config_dir() == tmp_path


def test_data_paths_follow_design_layout():
    p = DataPaths(Path("/data"), "leonberg")
    assert p.dgm1 == Path("/data/geo/lgl/dgm1")
    assert p.lod2 == Path("/data/geo/lgl/lod2")
    assert p.dop == Path("/data/geo/lgl/dop")
    assert p.osm == Path("/data/geo/osm")
    assert p.capture == Path("/data/capture")
    assert p.work == Path("/data/work/leonberg")
    assert set(p.raw_inputs()) == {"dgm1", "lod2", "dop", "osm"}
