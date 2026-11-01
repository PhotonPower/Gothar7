"""Hit windows of the human strikes against the blade path (gameplay.md "Trefferprüfung")."""

from __future__ import annotations

import pytest

from conftest import REPO_ROOT
from gothar_chargen.clipspec import load_set_spec
from gothar_chargen.events import load_events
from gothar_chargen.hitreach import (
    FIST_LENGTH,
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
    assert {"1h/t_attack_l", "1h/t_attack_r", "2h/t_attack_l", "2h/t_attack_r"} <= names


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
    assert forward >= ZONE_FRONT + 0.15
    assert ZONE_LOW <= low <= high <= ZONE_HIGH
    ahead = path.ahead()
    reach = ahead[start : end + 1].max()
    assert ahead[start] <= reach - 0.3
    assert reach >= ahead[max(start - 3, 0) : end + 4].max() - 0.05


@pytest.mark.parametrize("mode", ["1h", "2h"])
def test_directional_swings_cross_the_front(paths, mode):
    """Gothic 1 side swings: the blade sweeps across in front of the body, t_attack_l from the
    right (-X) to the left (+X), t_attack_r the other way, slanting down (high wind-up, the tip
    below 1 m on the way out) so that animals are hit too."""
    for clip, sign in (("t_attack_l", 1.0), ("t_attack_r", -1.0)):
        markers = dict(
            next(c for c in load_set_spec(mode).clips if c.name == f"{mode}/{clip}").markers
        )
        start, end = markers["hit_start"], markers["hit_end"]
        tips = paths[mode][f"{mode}/{clip}"].tips[start : end + 1, 0]
        assert sign * (tips[-1, 0] - tips[0, 0]) >= 1.2, clip  # sideways across the front
        assert tips[-1, 1] < tips[0, 1] - 0.4, clip  # slanting down
        assert tips[:, 1].min() <= 1.0 and tips[:, 1].max() <= 1.8, clip


ANIMAL_LOW, ANIMAL_HIGH = 0.5, 1.0  # animals are capsules up to 1.0 m (engine animal_height)


def _animal_points(path, markers) -> int:
    p = path.points[markers["hit_start"] : markers["hit_end"] + 1].reshape(-1, 3)
    low = (p[:, 2] >= ZONE_FRONT) & (p[:, 1] >= ANIMAL_LOW) & (p[:, 1] <= ANIMAL_HIGH)
    return int(low.sum())


@pytest.mark.parametrize(
    ("mode", "name", "markers"),
    [s for s in STRIKES if s[0] != "fist"],
    ids=[s[1] for s in STRIKES if s[0] != "fist"],
)
def test_weapon_strikes_reach_animals(paths, mode, name, markers):
    """Every weapon strike passes the space in front of the body at animal height within its
    window (wolves, boars: capsules up to 1.0 m; engine2 2026-11-01)."""
    assert _animal_points(paths[mode][name], markers) >= 5, name


def test_fists_reach_animals(paths):
    """Fists: at least one combo hit is a low blow at animal height (owner 2026-11-01: the low
    cross, combo 2), so an unarmed hero can fight a wolf with two hits."""
    low = {
        name
        for mode, name, m in STRIKES
        if mode == "fist" and _animal_points(paths[mode][name], m) >= 5
    }
    assert "fist/t_attack_combo2" in low


SIDE_REACH = {"1h": 1.5, "2h": 1.8}  # m from the body centre: the AI fights at 1.9 / 2.3 m


@pytest.mark.parametrize("mode", ["1h", "2h"])
def test_side_swings_reach_the_fighting_distance(paths, mode):
    """The side swings lunge (root forward) so that the tip reaches the opponent at the combat
    AI's distance (engine2 2026-11-01, centre to centre 1.9 m with 1h, 2.3 m with 2h, minus
    body and blade radius)."""
    for clip in ("t_attack_l", "t_attack_r"):
        name = f"{mode}/{clip}"
        markers = dict(next(c for c in load_set_spec(mode).clips if c.name == name).markers)
        tips = paths[mode][name].tips[markers["hit_start"] : markers["hit_end"] + 1, 0]
        assert tips[:, 2].max() >= SIDE_REACH[mode], name
