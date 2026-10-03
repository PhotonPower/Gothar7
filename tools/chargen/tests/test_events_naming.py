from __future__ import annotations

from pathlib import Path

import pytest

from conftest import REPO_ROOT
from gothar_chargen.events import events_path_for, load_events, parse_events
from gothar_chargen.naming import is_clip_name, is_event_name, is_loop_clip


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


def test_loop_clips():
    assert is_loop_clip("none/s_walk") and is_loop_clip("mob/anvil/s_work")
    assert not is_loop_clip("none/t_jump_land") and not is_loop_clip("dlg/a_gesture_shrug")


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
        ('version = 1\n[clips."none/s_walk"]\nloop = true\n', "'events' and/or 'speed'"),
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


# --- natural speed of locomotion clips (§3 `speed`) ----------------------------------------------


def test_parse_and_format_speed():
    from gothar_chargen.events import Event, format_events

    text = format_events(
        30, {"none/s_walk": [Event(0, "footstep_l")]}, {"none/s_walk": 0.98, "none/s_run": 5.9}
    )
    assert "speed = 5.90" in text and '[clips."none/s_run"]' in text  # speed only: still written
    parsed, errors = parse_events(text)
    assert not errors
    assert parsed.speeds == {"none/s_walk": 0.98, "none/s_run": 5.9}
    assert parsed.clips["none/s_walk"] == (Event(0, "footstep_l"),)


@pytest.mark.parametrize("speed", ["0", "-1.0", '"fast"', "true"])
def test_invalid_speed(speed):
    _, errors = parse_events(f'version = 1\n[clips."none/s_walk"]\nspeed = {speed}\n')
    assert any("speed must be a number > 0" in e for e in errors)


def test_clip_speeds_of_the_sets():
    """In-place human clips: planted foot speed; monster clips with root motion: root speed."""
    from gothar_chargen.clipspeed import clip_speeds
    from gothar_chargen.gltf import Gltf

    anims = REPO_ROOT / "assets/source/characters"
    human = clip_speeds(Gltf.load(anims / "anims/human/none.glb"))
    assert human["none/s_walk"] == pytest.approx(0.98, abs=0.05)
    assert human["none/s_run"] > human["none/s_walk"] > human["none/s_sneak"] > 0.5
    assert "none/s_idle" not in human and "none/s_ladder_up" not in human
    from gothar_chargen.clipfix import foot_slide

    wolf_glb = Gltf.load(anims / "monsters/wolf/anims/wolf.glb")
    wolf = clip_speeds(wolf_glb)
    run = next(a for a in wolf_glb.doc["animations"] if a["name"] == "wolf/s_run")
    root_speed, stride = foot_slide(wolf_glb, run)
    assert wolf["wolf/s_run"] == pytest.approx(root_speed, abs=0.01)  # root motion: root speed
    assert root_speed == pytest.approx(stride, rel=0.02)  # ... = stride (clipfix)
    assert not clip_speeds(Gltf.load(anims / "anims/human/swim.glb"))


def test_update_speeds_keeps_events(tmp_path):
    import shutil

    from gothar_chargen.events import update_speeds

    src = REPO_ROOT / "assets/source/characters/anims/human/none"
    shutil.copy(src.with_suffix(".glb"), tmp_path / "none.glb")
    events = tmp_path / "none.events.toml"
    text = src.with_name("none.events.toml").read_text(encoding="utf-8")
    events.write_text(text.replace("speed = 0.98", "speed = 3.00"), encoding="utf-8")
    update_speeds(tmp_path / "none.glb")
    assert events.read_text(encoding="utf-8") == text  # speeds measured again, events unchanged


def test_stale_speed_is_an_error(tmp_path):
    import shutil

    from gothar_chargen.skeleton import load_rig
    from gothar_chargen.validate import validate_file

    src = REPO_ROOT / "assets/source/characters/anims/human/none"
    shutil.copy(src.with_suffix(".glb"), tmp_path / "none.glb")
    text = src.with_name("none.events.toml").read_text(encoding="utf-8")
    (tmp_path / "none.events.toml").write_text(text.replace("speed = 0.98", "speed = 1.60"))
    report = validate_file(tmp_path / "none.glb", load_rig())
    assert "events.speed" in {i.code for i in report.issues if i.level == "error"}
