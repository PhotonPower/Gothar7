"""Engelberg arms and banner: repository files match the generator, colours keep the style sheet."""

from __future__ import annotations

import io
from pathlib import Path

import numpy as np
import pytest

from gothar_chargen.cli import main
from gothar_chargen.heraldry import (
    ARMS_FILE,
    ARMS_SIZE,
    BANNER_FILE,
    BANNER_SIZE,
    DRAFTS,
    ENGELBERG,
    HERALDRY_DIR,
    render_arms,
    render_banner,
    saturation,
    shield_sdf,
)
from gothar_chargen.images import image_info, is_power_of_two
from gothar_chargen.ui_frames import decode_png

REPO = Path(__file__).resolve().parents[3]
FOLDER = REPO / HERALDRY_DIR


@pytest.fixture(scope="module")
def fresh() -> dict[str, np.ndarray]:
    return {ARMS_FILE: render_arms(ENGELBERG), BANNER_FILE: render_banner(ENGELBERG)}


@pytest.mark.parametrize("name", [ARMS_FILE, BANNER_FILE])
def test_repository_file_matches_generator(name, fresh):
    data = (FOLDER / name).read_bytes()
    info = image_info(data)
    assert info.format == "png" and info.alpha
    assert is_power_of_two(info.width) and is_power_of_two(info.height)
    pixels = decode_png(data).astype(int)
    assert pixels.shape == fresh[name].shape
    # tolerance for floating point differences between platforms
    assert np.abs(pixels - fresh[name]).max() <= 2, (
        f"{name} out of date; run gothar-chargen heraldry"
    )


def test_no_stray_files():
    assert {p.name for p in FOLDER.iterdir()} == {ARMS_FILE, BANNER_FILE}


def test_sizes(fresh):
    assert fresh[ARMS_FILE].shape == (ARMS_SIZE, ARMS_SIZE, 4)
    assert fresh[BANNER_FILE].shape == (BANNER_SIZE[1], BANNER_SIZE[0], 4)
    assert sum((FOLDER / n).stat().st_size for n in (ARMS_FILE, BANNER_FILE)) < 700_000


def test_colours_keep_the_style_sheet():
    for arms in DRAFTS:
        for colour in (arms.field, arms.metal, arms.wing, arms.line):
            assert saturation(colour) <= 0.6, (arms.name, colour)
    assert len({a.name for a in DRAFTS}) == len(DRAFTS)
    assert all(a.figure in ("angel", "head", "none") for a in DRAFTS)


def test_arms_alpha_is_the_shield(fresh):
    img = fresh[ARMS_FILE]
    a = img[..., 3]
    assert a[0, 0] == 0 and a[-1, 0] == 0 and a[-1, -1] == 0  # corners outside
    assert a[ARMS_SIZE // 2, ARMS_SIZE // 2] == 255
    # no pure white or black where the shield is opaque
    rgb = img[..., :3][a == 255]
    assert rgb.max() < 250 and rgb.min() > 5


def test_shield_shape():
    assert shield_sdf(np.array(0.0), np.array(0.0)) < 0
    assert shield_sdf(np.array(0.0), np.array(-0.94)) < 0
    assert shield_sdf(np.array(0.7), np.array(-0.8)) > 0  # outside the point
    assert abs(float(shield_sdf(np.array(0.8), np.array(0.5)))) < 1e-9


def test_banner_swallowtail(fresh):
    img = fresh[BANNER_FILE]
    h, w = img.shape[:2]
    a = img[..., 3]
    assert a[: h * 3 // 4].min() == 255  # opaque above the cut
    assert a[-2, w // 2] == 0  # notch in the middle
    assert a[-4, 0] == 255 and a[-4, w - 1] == 255  # both tails reach the bottom


def test_cli_writes_into_out(tmp_path):
    buf = io.StringIO()
    assert main(["heraldry", "--out", str(tmp_path)], out=buf) == 0
    assert {p.name for p in tmp_path.iterdir()} == {ARMS_FILE, BANNER_FILE}
