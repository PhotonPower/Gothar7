"""Hit windows of the human strikes against the blade path (gameplay.md "Trefferprüfung")."""

from __future__ import annotations

import pytest

from conftest import REPO_ROOT
from gothar_chargen.clipspec import load_set_spec
from gothar_chargen.events import load_events
from gothar_chargen.hitreach import (
    FIST_LENGTH,
    NO_REACH,
    ZONE_FRONT,
    ZONE_HIGH,
    ZONE_LOW,
    blade_paths,
    item_length,
    weapon_length,
)

ANIMS = REPO_ROOT / "assets/source/characters/anims/human"
ITEMS = REPO_ROOT / "assets/source/items"
MODES = ("fist", "1h", "2h")


def _strikes() -> list[tuple[str, str, dict[str, int]]]:
    return [
        (mode, clip.name, dict(clip.markers))
        for mode in MODES
        for clip in load_set_spec(mode).clips
        if "hit_start" in dict(clip.markers)
    ]


STRIKES = _strikes()


@pytest.fixture(scope="module")
def paths():
    return {
        mode: blade_paths(ANIMS / f"{mode}.glb", mode, weapon_length(mode, ITEMS)) for mode in MODES
    }


def test_weapon_lengths():
    assert item_length(ITEMS / "it_sword_old.glb") == pytest.approx(0.9, abs=0.02)
    assert item_length(ITEMS / "it_sword_2h.glb") == pytest.approx(1.1, abs=0.02)
    assert weapon_length("fist", ITEMS) == FIST_LENGTH


def test_all_melee_strikes_covered():
    names = {name for _, name, _ in STRIKES}
    assert len(names) == 14
    assert names >= NO_REACH


@pytest.mark.parametrize(("mode", "name", "markers"), STRIKES, ids=[s[1] for s in STRIKES])
def test_window_order(paths, mode, name, markers):
    """Window inside the clip, the swing sound 4 frames before it, the combo window after it."""
    frames = paths[mode][name].frames
    start, end = markers["hit_start"], markers["hit_end"]
    assert 0 < start < end < frames - 1
    assert markers["sound:swing"] == start - 4 >= 0
    if "combo_start" in markers:
        assert markers["combo_start"] == end
        assert end + 5 <= markers["combo_end"] < frames - 1
    parsed, errors = load_events(ANIMS / f"{mode}.events.toml")
    assert parsed is not None, errors
    assert {e.name: e.frame for e in parsed.clips[name]} == markers  # build-set --events-only


@pytest.mark.parametrize(("mode", "name", "markers"), STRIKES, ids=[s[1] for s in STRIKES])
def test_window_on_the_strike(paths, mode, name, markers):
    """The blade swings forward into the space in front of the body during the window: at
    hit_start it is well behind its reach in the window, and the window holds the farthest reach
    of the strike (a few frames around it)."""
    path = paths[mode][name]
    start, end = markers["hit_start"], markers["hit_end"]
    forward, low, high = path.window_reach(start, end)
    if name in NO_REACH:  # blade upright during the swing: reported, needs new key poses
        assert forward < ZONE_FRONT + 0.15, f"{name} reaches now: drop it from NO_REACH"
        return
    assert forward >= ZONE_FRONT + 0.15
    assert ZONE_LOW <= low <= high <= ZONE_HIGH
    ahead = path.ahead()
    reach = ahead[start : end + 1].max()
    assert ahead[start] <= reach - 0.3
    assert reach >= ahead[max(start - 3, 0) : end + 4].max() - 0.05
