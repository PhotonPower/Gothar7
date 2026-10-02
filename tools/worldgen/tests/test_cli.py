import io
from pathlib import Path

import pytest

from gothar_worldgen.cli import EXIT_ERROR, EXIT_NOT_IMPLEMENTED, EXIT_OK, main


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    code = main(list(argv), out=out)
    return code, out.getvalue()


def test_tiles_lists_leonberg_tiles(config_dir: Path):
    code, out = run("--config-dir", str(config_dir), "tiles", "testsite")
    assert code == EXIT_OK
    assert "9 tiles of 1000 m" in out
    assert "499_5404" in out
    assert "501_5406" in out


def test_tiles_core_two_km(config_dir: Path):
    code, out = run(
        "--config-dir", str(config_dir), "tiles", "testsite", "--area", "core", "--size", "2000"
    )
    assert code == EXIT_OK
    assert "1 tiles of 2000 m" in out


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


def test_unknown_site_is_an_error(config_dir: Path, capsys: pytest.CaptureFixture[str]):
    code, _ = run("--config-dir", str(config_dir), "info", "nowhere")
    assert code == EXIT_ERROR
    assert "not found" in capsys.readouterr().err


def test_import_is_not_implemented_yet(config_dir: Path):
    code, _ = run("--config-dir", str(config_dir), "import", "testsite")
    assert code == EXIT_NOT_IMPLEMENTED


def test_command_is_required():
    with pytest.raises(SystemExit):
        main([])
