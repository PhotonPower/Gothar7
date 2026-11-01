"""UI frames (M14): every file present, sizes and slices as specified, centres tileable."""

from __future__ import annotations

import io
import tomllib
from pathlib import Path

import numpy as np
import pytest

from gothar_chargen.cli import main
from gothar_chargen.images import image_info
from gothar_chargen.ui_frames import (
    BAR_OPENING,
    FRAMES_DIR,
    SCALE,
    SPECS,
    decode_png,
    png_bytes,
    render_frame,
    spec,
    style_toml,
)

REPO = Path(__file__).resolve().parents[3]
FRAMES = REPO / FRAMES_DIR
SIZE_BUDGET = 400_000  # bytes for all frames together


@pytest.fixture(scope="module")
def rendered() -> dict[str, np.ndarray]:
    return {s.name: render_frame(s) for s in SPECS}


def _seam_ratio(tile: np.ndarray, axis: int) -> float:
    """Mean step across the wrap-around seam relative to the mean step between neighbours."""
    a = tile.astype(np.float64)
    inner = np.abs(np.diff(a, axis=axis)).mean()
    first = np.take(a, 0, axis=axis)
    last = np.take(a, -1, axis=axis)
    seam = np.abs(first - last).mean()
    return (seam + 0.5) / (inner + 0.5)


def test_specs_are_consistent():
    names = [s.name for s in SPECS]
    assert len(names) == len(set(names))
    for s in SPECS:
        left, top, right, bottom = s.slice
        x0, y0, x1, y1 = s.center_box
        assert min(s.slice) >= 0
        if s.background:
            assert s.slice == (0, 0, 0, 0)
        else:
            assert x1 - x0 >= 8 and y1 - y0 >= 8, s.name  # something to stretch
        assert s.center in ("tile", "tile_x", "clear", "fixed")
        # even sizes: 2 image pixels per UI unit, borders whole UI units
        assert s.width % 2 == 0 and s.height % 2 == 0
        assert all(v % 2 == 0 for v in s.slice), s.name


@pytest.mark.parametrize("s", SPECS, ids=lambda s: s.name)
def test_repository_file_matches_spec(s, rendered):
    path = FRAMES / s.file
    assert path.is_file(), f"missing {path}; run gothar-chargen ui-frames"
    data = path.read_bytes()
    info = image_info(data)
    assert (info.format, info.width, info.height, info.alpha) == ("png", s.width, s.height, True)
    pixels = decode_png(data).astype(int)
    fresh = rendered[s.name].astype(int)
    # the repository holds the generator's output (tolerance for floating point differences)
    assert np.abs(pixels - fresh).max() <= 2, f"{s.file} out of date; run gothar-chargen ui-frames"


def test_total_size_is_small():
    total = sum((FRAMES / s.file).stat().st_size for s in SPECS)
    assert total <= SIZE_BUDGET, total


def test_no_stray_frames():
    known = {s.file for s in SPECS}
    present = {p.name for p in FRAMES.glob("*.png")}
    assert present <= known, present - known


def test_deterministic_and_seeded():
    s = spec("panel")
    assert np.array_equal(render_frame(s, 7), render_frame(s, 7))
    assert not np.array_equal(render_frame(s, 7), render_frame(s, 8))


@pytest.mark.parametrize("s", [s for s in SPECS if s.center in ("tile", "tile_x")], ids=str)
def test_centre_tiles(s, rendered):
    x0, y0, x1, y1 = s.center_box
    centre = rendered[s.name][y0:y1, x0:x1]
    assert _seam_ratio(centre, axis=1) < 1.6, "left/right seam"
    if s.center == "tile":
        assert _seam_ratio(centre, axis=0) < 1.6, "top/bottom seam"
    else:  # bar fills: the same column everywhere (gradient only across)
        assert np.abs(np.diff(centre.astype(int), axis=1)).max() <= 1


def test_bar_frame_opening_is_clear(rendered):
    s = spec("bar")
    x0, y0, x1, y1 = s.center_box
    img = rendered["bar"]
    assert (img[y0:y1, x0:x1, 3] == 0).all()
    ox0, oy0, ox1, oy1 = BAR_OPENING
    assert ox0 < x0 and oy0 < y0 and ox1 > x1 and oy1 > y1
    assert (img[oy0 + 1 : oy1 - 1, ox0 + 1 : ox1 - 1, 3] == 0).all()
    assert (img[2, 20:-20, 3] == 255).all()  # the iron band


@pytest.mark.parametrize("name", ["panel", "leather", "tooltip", "cell", "button", "tab_active"])
def test_opaque_frames_cover_their_border(name, rendered):
    img = rendered[name]
    s = spec(name)
    mid = s.width // 2
    assert img[s.height // 2, 3, 3] > 200  # left edge band, half height
    assert img[4, mid, 3] > 200  # top edge band


def test_edges_stretch_cleanly(rendered):
    """Edge bands vary much less along their length than across (stretching keeps them)."""
    for name in ("panel", "leather", "cell", "tooltip", "subtitle"):
        s = spec(name)
        x0, y0, x1, y1 = s.center_box
        top = rendered[name][:y0, x0:x1, :].astype(float)
        along = np.abs(np.diff(top, axis=1)).mean()
        across = np.abs(np.diff(top, axis=0)).mean()
        assert along < across, name


def test_png_round_trip():
    rng = np.random.default_rng(1)
    img = rng.integers(0, 256, (17, 13, 4), dtype=np.uint8)
    img[5:9] = 40  # flat rows pick other filters
    assert np.array_equal(decode_png(png_bytes(img)), img)


def test_style_entries_match_spec():
    parsed = tomllib.loads(style_toml())["frames"]
    assert set(parsed) == {s.name for s in SPECS}
    for s in SPECS:
        entry = parsed[s.name]
        assert entry["image"] == f"ui/frames/{s.file}"
        assert tuple(entry["slice"]) == s.slice
        assert entry["scale"] == SCALE


def test_engine_style_uses_these_frames():
    style = tomllib.loads((REPO / "assets/source/ui/style.toml").read_text(encoding="utf-8"))
    for name, entry in style.get("frames", {}).items():
        s = spec(name)
        assert entry["image"] == f"ui/frames/{s.file}"
        assert tuple(entry["slice"]) == s.slice, name
        assert entry.get("scale", 1.0) == SCALE, name


def test_cli_writes_frames(tmp_path):
    out = io.StringIO()
    code = main(["ui-frames", "--out", str(tmp_path), "--only", "cell", "bar_hp"], out=out)
    assert code == 0
    assert sorted(p.name for p in tmp_path.iterdir()) == ["bar_hp.png", "cell.png"]
    assert "2 frame(s)" in out.getvalue()
    out = io.StringIO()
    assert main(["ui-frames", "--style"], out=out) == 0
    assert "[frames.panel]" in out.getvalue()
    assert main(["ui-frames", "--out", str(tmp_path), "--only", "nope"], out=io.StringIO()) == 1
