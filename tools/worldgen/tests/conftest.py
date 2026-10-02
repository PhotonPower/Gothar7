from pathlib import Path

import pytest

from gothar_worldgen.config import CONFIG_DIR_ENV, DATA_ROOT_ENV

SITE_TOML = """
[source]
crs = "EPSG:25832"

[origin]
easting  = 500933.0
northing = 5405056.0
height_reference = "marktplatz"

[area.core]
half_extent_m = 350

[area.surroundings]
half_extent_m = 1000

[game_scale]
horizontal = 1.0
vertical   = 1.0
alley_widen_factor = 1.0
"""


@pytest.fixture(autouse=True)
def _clean_env(monkeypatch: pytest.MonkeyPatch) -> None:
    """Tests must not depend on the developer's environment."""
    monkeypatch.delenv(DATA_ROOT_ENV, raising=False)
    monkeypatch.delenv(CONFIG_DIR_ENV, raising=False)


@pytest.fixture
def config_dir(tmp_path: Path) -> Path:
    """Config directory with a valid ``testsite.toml`` and no ``local.toml``."""
    d = tmp_path / "config"
    d.mkdir()
    (d / "testsite.toml").write_text(SITE_TOML, encoding="utf-8")
    return d
