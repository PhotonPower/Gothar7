from __future__ import annotations

from pathlib import Path

import pytest

from gothar_chargen.events import events_path_for, load_events, parse_events
from gothar_chargen.naming import is_clip_name, is_event_name


@pytest.mark.parametrize(
    "name",
    [
        # examples from characters-pipeline.md §3 and animation-list.md
        "none/s_walk",
        "1h/s_run",
        "1h/t_attack_combo1_t2",
        "none/t_stand_2_sit",
        "mob/anvil/s_work",
        "dlg/a_gesture_shrug",
        "amb/s_guard_arms_crossed",
        "swim/t_2_dive",
        "mob/chest/s_picklock",
        "cbow/s_aim",
    ],
)
def test_valid_clip_names(name):
    assert is_clip_name(name)


@pytest.mark.parametrize(
    "name",
    [
        "s_walk",
        "Walk",
        "none/walk",
        "none/x_walk",
        "none/s_Walk",
        "sword/s_run",
        "mob/s_work",
        "mob/anvil/work",
        "none/s_walk_",
        "none/s__walk",
        "none/s_walk.001",
    ],
)
def test_invalid_clip_names(name):
    assert not is_clip_name(name)


@pytest.mark.parametrize(
    "name", ["footstep_l", "hit_start", "combo_window", "sound:sword_swing_01"]
)
def test_valid_event_names(name):
    assert is_event_name(name)


@pytest.mark.parametrize("name", ["", "Footstep", "1hit", "sound:", "hit start"])
def test_invalid_event_names(name):
    assert not is_event_name(name)


def test_events_path():
    assert events_path_for(Path("anims/human/1h.glb")) == Path("anims/human/1h.events.toml")


def test_parse_valid_events():
    ev, errors = parse_events(
        """
version = 1
fps = 25
[clips."none/s_walk"]
events = [ { frame = 0, event = "footstep_l" }, { frame = 12, event = "footstep_r" } ]
[clips."none/s_idle"]
"""
    )
    assert errors == []
    assert ev is not None
    assert ev.fps == 25
    assert [e.name for e in ev.clips["none/s_walk"]] == ["footstep_l", "footstep_r"]
    assert ev.clips["none/s_idle"] == ()


def test_default_fps():
    ev, errors = parse_events("version = 1\n")
    assert errors == [] and ev is not None and ev.fps == 30


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("version = 1\n[clips\n", "invalid TOML"),
        ("version = 3\n", "version must be 1"),
        ("version = 1\nfps = 0\n", "fps"),
        ("version = 1\nspeed = 2\n", "unknown top-level"),
        ("version = 1\nclips = 5\n", "clips must be a table"),
        ('version = 1\n[clips."Walk"]\n', "naming convention"),
        ('version = 1\n[clips."none/s_walk"]\nloop = true\n', "only 'events'"),
        ('version = 1\n[clips."none/s_walk"]\nevents = [ { frame = 1 } ]\n', "expected"),
        (
            'version = 1\n[clips."none/s_walk"]\nevents = [ { frame = -1, event = "x" } ]\n',
            "frame must",
        ),
        (
            'version = 1\n[clips."none/s_walk"]\nevents = [ { frame = 1, event = "Bad" } ]\n',
            "invalid event",
        ),
        (
            'version = 1\n[clips."none/s_walk"]\n'
            'events = [ { frame = 9, event = "a" }, { frame = 1, event = "b" } ]\n',
            "sorted",
        ),
    ],
)
def test_invalid_events(text, message):
    _, errors = parse_events(text)
    assert any(message in e for e in errors), errors


def test_load_events_missing_file(tmp_path):
    ev, errors = load_events(tmp_path / "x.events.toml")
    assert ev is None and errors
