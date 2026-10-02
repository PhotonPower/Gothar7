import io
import json
from pathlib import Path

import pytest

from gothar_worldgen.cli import EXIT_ERROR, EXIT_OK, main

from .citygml import building, flat_surfaces, write_citygml
from .conftest import SITE_TOML
from .osmxml import OsmXml

LGL_EXCERPT_DIR = Path(__file__).parent / "data" / "lgl_dgm1"


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    code = main(list(argv), out=out)
    return code, out.getvalue()


def test_tiles_defaults_to_lgl_grid(config_dir: Path):
    code, out = run("--config-dir", str(config_dir), "tiles", "testsite")
    assert code == EXIT_OK
    assert "4 tiles, LGL Open GeoData grid" in out
    assert "499_5404" in out
    assert "501_5406" in out


def test_tiles_plain_grid(config_dir: Path):
    code, out = run(
        "--config-dir", str(config_dir), "tiles", "testsite", "--area", "core", "--size", "1000"
    )
    assert code == EXIT_OK
    assert "4 tiles, 1000 m grid" in out


def test_download_requires_local_config(config_dir: Path, capsys: pytest.CaptureFixture[str]):
    code, _ = run("--config-dir", str(config_dir), "download", "testsite")
    assert code == EXIT_ERROR
    assert "local.toml" in capsys.readouterr().err


def test_download_rejects_unknown_source(
    config_dir: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
):
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(config_dir))
    code, _ = run("--config-dir", str(config_dir), "download", "testsite", "--only", "dgm1,x")
    assert code == EXIT_ERROR
    assert "unknown source" in capsys.readouterr().err


def test_download_command(config_dir: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    calls = []

    def fake_download_site(site, paths, sources, **kwargs):
        calls.append((site.name, paths.data_root, sources, kwargs["area"], kwargs["force"]))
        return []

    monkeypatch.setattr("gothar_worldgen.cli.download_site", fake_download_site)
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path))
    code, out = run(
        "--config-dir", str(config_dir), "download", "testsite", "--only", "dgm1,osm", "--force"
    )
    assert code == EXIT_OK
    assert calls == [("testsite", tmp_path, ("dgm1", "osm"), "surroundings", True)]
    assert "done: 0 downloaded" in out


def test_download_failure_is_reported(
    config_dir: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
):
    def failing(*args, **kwargs):
        raise OSError("network down")

    monkeypatch.setattr("gothar_worldgen.cli.download_site", failing)
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path))
    code, _ = run("--config-dir", str(config_dir), "download", "testsite")
    assert code == EXIT_ERROR
    assert "network down" in capsys.readouterr().err


def test_tiles_rejects_non_positive_size(config_dir: Path, capsys: pytest.CaptureFixture[str]):
    code, _ = run("--config-dir", str(config_dir), "tiles", "testsite", "--size", "0")
    assert code == EXIT_ERROR
    assert "--size" in capsys.readouterr().err


def test_info_without_local_config(config_dir: Path):
    code, out = run("--config-dir", str(config_dir), "info", "testsite")
    assert code == EXIT_OK
    assert "site:         testsite (EPSG:25832)" in out
    assert "local config: not available" in out


def test_info_reports_raw_data_dirs(config_dir: Path, tmp_path: Path):
    data_root = tmp_path / "data"
    (data_root / "geo" / "lgl" / "dgm1").mkdir(parents=True)
    (config_dir / "local.toml").write_text(
        f"[paths]\ndata_root = '{data_root.as_posix()}'\n", encoding="utf-8"
    )
    code, out = run("--config-dir", str(config_dir), "info", "testsite")
    assert code == EXIT_OK
    rows = {line.split()[0]: line for line in out.splitlines() if line.startswith("  ")}
    assert " ok " in rows["dgm1"]
    assert "MISSING" in rows["lod2"]
    assert "MISSING" in rows["osm"]


def test_info_reports_blender(config_dir: Path, tmp_path: Path):
    blender = tmp_path / "blender.exe"
    blender.write_bytes(b"")
    (config_dir / "local.toml").write_text(
        f"[paths]\ndata_root = '{tmp_path.as_posix()}'\nblender = '{blender.as_posix()}'\n",
        encoding="utf-8",
    )
    _, out = run("--config-dir", str(config_dir), "info", "testsite")
    assert "blender:   ok" in out
    blender.unlink()
    _, out = run("--config-dir", str(config_dir), "info", "testsite")
    assert "blender:   MISSING" in out


def test_unknown_site_is_an_error(config_dir: Path, capsys: pytest.CaptureFixture[str]):
    code, _ = run("--config-dir", str(config_dir), "info", "nowhere")
    assert code == EXIT_ERROR
    assert "not found" in capsys.readouterr().err


def _small_site_with_data(config_dir: Path, data_root: Path) -> None:
    """Shrink the test site to the real DGM1 excerpt and put the excerpt under DATA_ROOT."""
    small = SITE_TOML.replace("half_extent_m = 350", "half_extent_m = 10").replace(
        "half_extent_m = 1000", "half_extent_m = 20"
    )
    (config_dir / "testsite.toml").write_text(small, encoding="utf-8")
    dgm = data_root / "geo" / "lgl" / "dgm1" / "excerpt"
    dgm.mkdir(parents=True)
    for f in LGL_EXCERPT_DIR.glob("*.xyz"):
        (dgm / f.name).write_bytes(f.read_bytes())
    # One synthetic 6 m x 6 m flat-roofed building next to the origin (E 500933, N 5405056).
    write_citygml(
        data_root / "geo" / "lgl" / "lod2" / "LoD2_32_500_5405_1_BW.gml",
        building("B1", flat_surfaces(500930, 5405053, 6, 6, 371.0, 374.0), roof_type="1000"),
    )
    osm = OsmXml()
    osm.way(1, [(500920, 5405050), (500950, 5405050)], {"highway": "residential"})
    osm.node(500940, 5405060, {"natural": "tree"})
    osm.write(data_root / "geo" / "osm" / "test.osm")


def test_import_writes_all_layers(
    config_dir: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    data_root = tmp_path / "data"
    _small_site_with_data(config_dir, data_root)
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(data_root))
    code, out = run("--config-dir", str(config_dir), "import", "testsite")
    assert code == EXIT_OK
    assert "terrain: 40 x 40 samples" in out
    assert "origin height 371.59 m NHN" in out
    work = data_root / "work" / "testsite"
    assert (work / "terrain.r16").stat().st_size == 40 * 40 * 2
    assert (work / "terrain.png").is_file()
    assert (work / "terrain.json").is_file()
    assert "buildings: 1 (1 in core" in out
    doc = json.loads((work / "buildings.json").read_text(encoding="utf-8"))
    assert doc["origin"]["heightNHN"] == 371.59  # same reference height as the terrain
    assert doc["buildings"][0]["groundY"] == pytest.approx(371.0 - 371.59)
    assert "streets: 1 lines (road 1)" in out
    assert "features: 1 (tree 1)" in out
    streets = json.loads((work / "streets.json").read_text(encoding="utf-8"))
    assert streets["source"]["credit"] == "© OpenStreetMap-Mitwirkende"
    assert streets["streets"][0]["points"][0] == [-13.0, 6.0]  # E 500920, N 5405050 local
    assert json.loads((work / "features.json").read_text(encoding="utf-8"))["features"]
    assert "checks:" in out
    report = json.loads((work / "report.json").read_text(encoding="utf-8"))
    assert report["status"] in ("ok", "warn")
    assert (work / "preview.png").is_file()
    assert (work / "preview_core.png").is_file()

    code, out = run("--config-dir", str(config_dir), "check", "testsite")
    assert code == EXIT_OK
    assert "alignment.roadsInBuildings" in out


def test_check_without_work_data(
    config_dir: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
):
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path))
    code, _ = run("--config-dir", str(config_dir), "check", "testsite")
    assert code == EXIT_ERROR
    assert "run 'gothar-worldgen import" in capsys.readouterr().err


def test_import_without_dgm_data_fails(
    config_dir: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
):
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path))
    code, _ = run("--config-dir", str(config_dir), "import", "testsite")
    assert code == EXIT_ERROR
    assert "DGM1 tiles missing" in capsys.readouterr().err


def test_command_is_required():
    with pytest.raises(SystemExit):
        main([])
