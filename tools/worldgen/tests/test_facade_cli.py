import io
import json
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from gothar_worldgen.cli import EXIT_ERROR, EXIT_OK, main
from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.rectify import facade_from_footprint

from .facade_synth import render_equirect
from .test_facade_rectify import HOUSE


def setup(config_dir: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> tuple[Path, Path]:
    data_root = tmp_path / "data"
    work = data_root / "work" / "testsite"
    work.mkdir(parents=True)
    (work / "buildings.json").write_text(json.dumps({"buildings": [HOUSE]}), encoding="utf-8")
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(data_root))
    pose = CameraPose(2.0, 1.6, 7.0, 20.0)
    image = render_equirect(2048, pose, facade_from_footprint(HOUSE, 0))
    path = tmp_path / "pano.png"
    Image.fromarray(image).save(path)
    return work, path


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    return main(list(argv), out=out), out.getvalue()


def test_facade_preview_writes_visible_edges(config_dir: Path, tmp_path: Path, monkeypatch):
    work, pano = setup(config_dir, tmp_path, monkeypatch)
    args = ["facade", "preview", "testsite", "B1", "--image", str(pano), "--pose", "2,7,1.6"]
    code, out = run("--config-dir", str(config_dir), *args, "--heading", "20", "--px-per-m", "20")
    assert code == EXIT_OK
    view = work / "facades" / "B1_edge0.png"
    assert Image.open(view).size == (80, 120)  # 4 m x 6 m at 20 px/m
    assert "edge 0: B1_edge0.png" in out
    assert "edge 2: skipped" in out  # the north facade faces away from the camera
    # Rectified checkerboard: the bottom-left cell is black, the next one to the right white.
    pixels = np.asarray(Image.open(view))
    assert pixels[115, 5].mean() < 80
    assert pixels[115, 15].mean() > 180


def test_facade_preview_errors(config_dir: Path, tmp_path: Path, monkeypatch, capsys):
    _, pano = setup(config_dir, tmp_path, monkeypatch)
    base = ["--config-dir", str(config_dir), "facade", "preview", "testsite"]
    assert run(*base, "NOPE", "--image", str(pano), "--pose", "2,7")[0] == EXIT_ERROR
    assert "not in buildings.json" in capsys.readouterr().err
    assert run(*base, "B1", "--image", str(pano), "--pose", "2")[0] == EXIT_ERROR
    assert (
        run(*base, "B1", "--image", str(tmp_path / "missing.png"), "--pose", "2,7")[0] == EXIT_ERROR
    )
    square = tmp_path / "square.png"
    Image.fromarray(np.zeros((10, 10, 3), np.uint8)).save(square)
    assert run(*base, "B1", "--image", str(square), "--pose", "2,7")[0] == EXIT_ERROR
    assert "not equirectangular" in capsys.readouterr().err
    # Camera inside the house: no facade visible.
    assert run(*base, "B1", "--image", str(pano), "--pose", "2,-2")[0] == EXIT_ERROR
