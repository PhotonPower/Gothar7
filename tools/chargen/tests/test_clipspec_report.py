from __future__ import annotations

from pathlib import Path

import pytest

from conftest import REPO_ROOT, add_clip
from gothar_chargen.clipspec import (
    ClipSpecError,
    load_set_spec,
    packaged_sets,
    parse_set_spec,
    parse_source_ref,
)
from gothar_chargen.gltf import Gltf
from gothar_chargen.mapping import load_mapping
from gothar_chargen.report import ReportError, expand_names, parse_prio_a, progress

ANIMATION_LIST = REPO_ROOT / "docs/design/animation-list.md"
ANIMS = REPO_ROOT / "assets/source/characters/anims"
SOURCES = {"ual1": {"file": "a.glb", "mapping": "quaternius_ual1"}}


# --- clip lists ----------------------------------------------------------------------------------


def test_packaged_sets_are_valid():
    humans = ["1h", "2h", "amb", "bow", "cbow", "dive", "dlg", "fist", "mag", "mob", "none", "swim"]
    assert packaged_sets(monsters=False) == humans
    assert packaged_sets(monsters=True) == [
        "keiler",
        "laufvogel",
        "quaderbuckel",
        "schinder",
        "wolf",
    ]
    assert packaged_sets() == sorted(
        humans + ["keiler", "laufvogel", "quaderbuckel", "schinder", "wolf"]
    )
    for name in packaged_sets():
        spec = load_set_spec(name)
        assert spec.set == name
        assert all(n.startswith(f"{name}/") for n in spec.names)  # helpers may differ
        for source in spec.sources.values():
            load_mapping(source.mapping)


def test_set_files_match_specs():
    """Every packaged set has its .glb with exactly the listed clips."""
    for name in packaged_sets():
        spec = load_set_spec(name)
        g = Gltf.load(spec.blend_path(ANIMS.parent).with_suffix(".glb"))
        assert sorted(a["name"] for a in g.doc["animations"]) == sorted(spec.names)


def test_source_refs():
    libs = parse_set_spec(
        {"set": "x", "sources": SOURCES, "clip": [{"name": "none/s_a", "from": "ual1:A"}]}
    ).sources
    ref = parse_source_ref("ual1:Jump_Loop[0:20]", libs)
    assert (ref.library, ref.action, ref.start, ref.end) == ("ual1", "Jump_Loop", 0, 20)
    assert parse_source_ref("ual1:Idle Loop", libs).start is None
    for bad, msg in (
        ("Jump", "expected"),
        ("ual9:Jump", "unknown source"),
        ("ual1:J[5:5]", "empty"),
    ):
        with pytest.raises(ClipSpecError, match=msg):
            parse_source_ref(bad, libs)


def _spec(*clips: dict, **extra) -> dict:
    return {"set": "none", "sources": SOURCES, "clip": list(clips), **extra}


def test_all_operations():
    spec = parse_set_spec(
        _spec(
            {"name": "none/s_walk", "from": "ual1:Walk", "events": "footsteps"},
            {"name": "none/s_run", "from": "ual1:Run"},
            {"name": "none/s_walk_back", "reverse": "none/s_walk"},
            {"name": "none/t_walk_2_run", "blend": ["none/s_walk", "none/s_run"], "frames": 8},
            {
                "name": "none/t_jump_run",
                "concat": ["ual1:Jump[0:20]", "ual1:Land"],
                "events": "land",
            },
        )
    )
    ops = {c.name: c for c in spec.clips}
    assert ops["none/s_walk"].events == "footsteps"
    assert ops["none/s_walk_back"].clips == ("none/s_walk",)
    assert ops["none/t_walk_2_run"].frames == 8
    assert len(ops["none/t_jump_run"].sources) == 2
    assert spec.names[0] == "none/s_walk"


def test_keyframe_and_helper():
    spec = parse_set_spec(
        _spec(
            {"name": "none/s_walk", "from": "ual1:Walk", "helper": True},
            {
                "name": "none/s_strafe_l",
                "keyframe": "strafe",
                "params": {"base": "none/s_walk", "side": "l", "yaw": 70},
                "events": "footsteps",
            },
            {"name": "none/s_ladder_up", "keyframe": "ladder"},
        )
    )
    assert spec.names == ["none/s_strafe_l", "none/s_ladder_up"]  # helper not exported
    strafe = spec.clips[1]
    assert (strafe.op, strafe.recipe, strafe.clips) == ("keyframe", "strafe", ("none/s_walk",))
    assert strafe.param == {"base": "none/s_walk", "side": "l", "yaw": 70}
    assert spec.clips[0].helper and not strafe.helper


def test_layer_and_depends():
    spec = parse_set_spec(
        _spec(
            {"name": "1h/s_idle", "from": "ual1:Sword_Idle"},
            {"name": "1h/s_walk", "layer": ["none/s_walk", "1h/s_idle"]},
            {
                "name": "1h/s_run",
                "layer": ["none/s_run", "1h/s_idle"],
                "from_bone": ["clavicle_l", "clavicle_r", "neck"],
            },
            depends=["none"],
        ),
        external=frozenset({"none/s_walk", "none/s_run"}),
    )
    assert spec.depends == ("none",)
    walk, run = spec.clips[1], spec.clips[2]
    assert walk.op == "layer" and walk.bones == ("spine_02",)
    assert run.bones == ("clavicle_l", "clavicle_r", "neck")
    with pytest.raises(ClipSpecError, match="depends"):
        parse_set_spec(_spec({"name": "1h/s_walk", "layer": ["none/s_walk", "none/s_walk"]}))
    with pytest.raises(ClipSpecError, match="from_bone"):
        parse_set_spec(
            _spec({"name": "1h/s_a", "layer": ["none/s_x", "none/s_x"], "from_bone": []}),
            external=frozenset({"none/s_x"}),
        )


def test_weapon_sets_depend_on_none():
    for mode in ("fist", "1h", "2h", "bow", "cbow", "mag"):
        spec = load_set_spec(mode)
        assert spec.depends == ("none",)
        # 8 locomotion clips per mode, plus drawing and sheathing for 1h and fist (M9)
        # + combat clips of M11 (fist 3, 1h 8, 2h 8, bow 3, cbow 3), magic of M12 (mag 12)
        assert len(spec.names) == {
            "fist": 13,
            "1h": 18,
            "2h": 16,
            "bow": 11,
            "cbow": 11,
            "mag": 20,
        }.get(mode, 8)
        layered = [c for c in spec.clips if c.op == "layer" and not c.name.endswith("/s_idle")]
        assert len(layered) == 7
        assert all(c.bones == ("clavicle_l", "clavicle_r", "neck") for c in layered)


def test_circular_depends(tmp_path, monkeypatch):
    import gothar_chargen.clipspec as cs

    data = {
        "a": {"set": "a", "depends": ["b"], "clip": [{"name": "none/s_a", "keyframe": "slide"}]},
        "b": {"set": "b", "depends": ["a"], "clip": [{"name": "none/s_b", "keyframe": "slide"}]},
    }
    monkeypatch.setattr(cs, "_read_spec_data", lambda name: data[str(name)])
    with pytest.raises(ClipSpecError, match="circular"):
        cs.load_set_spec("a")


def test_recipes_match_blender_module():
    """Recipe names in clipspec.RECIPES and blender/keyframes.py agree (read without bpy)."""
    import re

    from gothar_chargen.clipspec import RECIPES

    source = (REPO_ROOT / "tools/chargen/src/gothar_chargen/blender/keyframes.py").read_text(
        "utf-8"
    )
    table = source[source.index("RECIPES: dict") :]
    assert set(re.findall(r'"([a-z_]+)": [a-z_]+,', table)) == set(RECIPES)


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ({"clip": []}, "missing 'set'"),
        (_spec({"name": "none/s_a", "keyframe": "dance"}), "unknown keyframe recipe"),
        (
            _spec({"name": "none/s_a", "keyframe": "strafe", "params": {"base": "none/s_x"}}),
            "earlier",
        ),
        (_spec({"name": "none/s_a", "keyframe": "ladder", "params": 3}), "params must"),
        (_spec({"name": "none/s_a", "keyframe": "ladder", "helper": "yes"}), "helper must"),
        ({"set": "none", "clip": []}, "no clips"),
        ({"set": "none", "sources": {"x": {"file": "a"}}, "clip": []}, "needs 'file'"),
        (_spec({"name": "Walk", "from": "ual1:A"}), "invalid clip name"),
        (
            _spec({"name": "none/s_a", "from": "ual1:A"}, {"name": "none/s_a", "from": "ual1:B"}),
            "duplicate",
        ),
        (_spec({"name": "none/s_a"}), "exactly one"),
        (_spec({"name": "none/s_a", "from": "ual1:A", "reverse": "x"}), "exactly one"),
        (_spec({"name": "none/s_a", "from": "ual1:A", "events": "steps"}), "events must"),
        (_spec({"name": "none/s_a", "reverse": "none/s_b"}), "earlier clip"),
        (_spec({"name": "none/s_a", "concat": ["ual1:A"]}), "at least two"),
        (
            _spec(
                {"name": "none/s_a", "from": "ual1:A"},
                {"name": "none/t_b", "blend": ["none/s_a"], "frames": 5},
            ),
            "blend needs",
        ),
        (
            _spec(
                {"name": "none/s_a", "from": "ual1:A"},
                {"name": "none/t_b", "blend": ["none/s_a", "none/s_a"]},
            ),
            "frames",
        ),
    ],
)
def test_invalid_specs(data, message):
    with pytest.raises(ClipSpecError, match=message):
        parse_set_spec(data)


def test_load_set_spec_errors(tmp_path: Path):
    with pytest.raises(ClipSpecError, match="unknown clip list"):
        load_set_spec("does_not_exist")
    bad = tmp_path / "x.toml"
    bad.write_text("set = [", encoding="utf-8")
    with pytest.raises(ClipSpecError):
        load_set_spec(bad)
    bad.write_text(
        'set = "none"\n[[clip]]\nname = "none/s_a"\nreverse = "none/s_a"\n', encoding="utf-8"
    )
    with pytest.raises(ClipSpecError, match="earlier"):
        load_set_spec(bad)


# --- report --------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("cell", "names"),
    [
        ("`none/s_idle`", ["none/s_idle"]),
        ("`none/s_strafe_l` / `s_strafe_r`", ["none/s_strafe_l", "none/s_strafe_r"]),
        ("`none/t_walk_2_run`, `t_run_2_walk`", ["none/t_walk_2_run", "none/t_run_2_walk"]),
        ("`swim/s_idle`, `swim/t_turn_l/r`", ["swim/s_idle", "swim/t_turn_l", "swim/t_turn_r"]),
        (
            "`dive/s_idle`, `swim/t_2_dive`, `dive/t_2_swim`",
            ["dive/s_idle", "swim/t_2_dive", "dive/t_2_swim"],
        ),
        ("`mob/anvil/s_work`, `t_stop`", ["mob/anvil/s_work", "mob/anvil/t_stop"]),
    ],
)
def test_expand_names(cell, names):
    assert expand_names(cell) == names


def test_expand_names_needs_mode():
    with pytest.raises(ReportError, match="cannot read"):
        expand_names("`s_walk`")


def test_real_list_has_36_prio_a_clips():
    rows = parse_prio_a(ANIMATION_LIST.read_text(encoding="utf-8"))
    names = [n for r in rows for n in r.names]
    assert len(names) == len(set(names)) == 36
    assert "none/s_idle" in names and "dive/t_2_swim" in names


def test_real_list_is_consistent_with_files():
    text = ANIMATION_LIST.read_text(encoding="utf-8")
    result = progress(text, ANIMS, ANIMS.parent / "monsters")
    assert result.stale == []
    assert result.extra == []
    assert result.missing == []
    counts = list(result.section_counts().values())
    # Prio-B item use + mobs (M8), per mode, Prio-C routines (M9), dialogue (M10), combat (M11),
    # 5 monsters (3 placeholders, schinder 15, quaderbuckel 17)
    expected = [
        (17, 17),
        (48, 48),
        (43, 43),
        (22, 22),
        (31, 31),
        (14, 14),
        (13, 13),
        (12, 12),
        (12, 12),
        (15, 15),
        (17, 17),
    ]
    assert counts == expected
    # without the monsters folder the wolf rows are reported as out of date
    assert progress(text, ANIMS).stale


LIST = """# Liste
## Prio A – x

| Name | Zweck | Status |
|---|---|---|
| `none/s_walk` | Gehen | platzhalter |
| `none/s_run`, `t_run_stop` | Rennen | offen |
| `none/s_idle` | Stehen | offen |

## Prio B
| Bereich | Muster |
|---|---|
| Kampf | `1h/t_attack…` |

### Prio B – Fortbewegung
| Name | Status |
|---|---|
| `1h/s_idle`, `s_walk` | platzhalter |
"""


def test_progress_with_synthetic_files(tmp_path, figure):
    add_clip(figure, "none/s_run", ["pelvis"])
    add_clip(figure, "none/t_run_stop", ["pelvis"])
    add_clip(figure, "none/s_extra", ["pelvis"])
    add_clip(figure, "1h/s_idle", ["pelvis"])
    (tmp_path / "human").mkdir()
    (tmp_path / "human" / "none.glb").write_bytes(figure.to_bytes())
    (tmp_path / "human" / "broken.glb").write_bytes(b"nope")
    result = progress(LIST, tmp_path)
    assert result.missing == ["none/s_walk", "none/s_idle"]
    # stale: walk (placeholder, missing), run row (offen, present), 1h row (s_walk missing)
    assert len(result.stale) == 3
    assert result.extra == ["none/s_extra"]
    assert result.section_counts() == {"Prio B – Fortbewegung": (1, 2)}
    d = result.to_dict()
    assert d["prio_a"] == {"listed": 4, "present": 2}
    assert d["sections"]["Prio B – Fortbewegung"] == {"present": 1, "listed": 2}


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("# nothing\n", "no 'Prio A'"),
        ("## Prio A\n| Name | Zweck |\n|---|---|\n| `none/s_a` | x |\n", "columns"),
    ],
)
def test_bad_lists(text, message):
    with pytest.raises(ReportError, match=message):
        parse_prio_a(text)


def test_close_and_in_place_options():
    clip = {"name": "amb/s_train_sword", "from": "ual1:Sword", "close": 12, "in_place": True}
    spec = parse_set_spec({"set": "amb", "sources": SOURCES, "clip": [clip]})
    assert spec.clips[0].close == 12 and spec.clips[0].in_place
    plain = parse_set_spec(
        {"set": "amb", "sources": SOURCES, "clip": [{"name": "amb/s_x", "from": "ual1:A"}]}
    )
    assert plain.clips[0].close == 0 and not plain.clips[0].in_place
    for bad, message in (
        ({"name": "amb/t_x", "from": "ual1:A", "close": 8}, "loops"),
        ({"name": "amb/s_x", "from": "ual1:A", "close": 1}, "close"),
        ({"name": "amb/s_x", "from": "ual1:A", "in_place": "yes"}, "in_place"),
    ):
        with pytest.raises(ClipSpecError, match=message):
            parse_set_spec({"set": "amb", "sources": SOURCES, "clip": [bad]})


def test_additive_option_only_for_a_clips():
    ok = parse_set_spec(
        {"set": "dlg", "sources": SOURCES,
         "clip": [{"name": "dlg/a_nod", "from": "ual1:A", "additive": True}]}
    )  # fmt: skip
    assert ok.clips[0].additive
    for bad in (
        {"name": "dlg/a_nod", "from": "ual1:A"},
        {"name": "dlg/s_x", "from": "ual1:A", "additive": True},
    ):
        with pytest.raises(ClipSpecError, match="additive"):
            parse_set_spec({"set": "dlg", "sources": SOURCES, "clip": [bad]})


def test_chain_option():
    spec = parse_set_spec(
        {"set": "1h", "sources": SOURCES, "clip": [
            {"name": "1h/t_a", "from": "ual1:A"},
            {"name": "1h/t_b", "from": "ual1:B"},
            {"name": "1h/t_draw", "chain": ["1h/t_a", "1h/t_b"], "markers": {"draw": 10}},
        ]}
    )  # fmt: skip
    assert spec.clips[2].op == "chain" and spec.clips[2].clips == ("1h/t_a", "1h/t_b")
    for bad, message in (
        (["1h/t_a"], "at least two"),
        (["1h/t_a", "1h/t_x"], "neither an earlier clip"),
    ):
        with pytest.raises(ClipSpecError, match=message):
            parse_set_spec(
                {"set": "1h", "sources": SOURCES, "clip": [
                    {"name": "1h/t_a", "from": "ual1:A"},
                    {"name": "1h/t_draw", "chain": bad},
                ]}
            )  # fmt: skip
