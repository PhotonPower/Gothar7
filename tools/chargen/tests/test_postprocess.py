from __future__ import annotations

import numpy as np

from conftest import REPO_ROOT, add_clip, append_accessor, node_index
from gothar_chargen.events import Event, detect_contacts, format_events, parse_events
from gothar_chargen.gltf import Gltf
from gothar_chargen.postprocess import compact, strip_animation_channels
from gothar_chargen.validate import validate_gltf

ANIM_SET = REPO_ROOT / "assets/source/characters/anims/human/none.glb"


def _add_channel(g: Gltf, clip: str, bone: str, path: str) -> None:
    anim = next(a for a in g.doc["animations"] if a["name"] == clip)
    times = append_accessor(g, np.array([0.0, 1.0]), "SCALAR")
    values = append_accessor(g, np.zeros((2, 3)), "VEC3")
    anim["samplers"].append({"input": times, "output": values})
    anim["channels"].append(
        {
            "sampler": len(anim["samplers"]) - 1,
            "target": {"node": node_index(g, bone), "path": path},
        }
    )


def _codes(report) -> set[str]:
    return {i.code for i in report.errors}


def test_translation_and_scale_channels_are_errors(figure, rig, reference):
    add_clip(figure, "none/s_walk", ["pelvis"])
    _add_channel(figure, "none/s_walk", "pelvis", "translation")  # allowed
    _add_channel(figure, "none/s_walk", "root", "translation")  # allowed
    assert validate_gltf(figure, rig, reference).ok(strict=True)
    _add_channel(figure, "none/s_walk", "calf_l", "translation")
    _add_channel(figure, "none/s_walk", "hand_r", "scale")
    report = validate_gltf(figure, rig, reference)
    assert _codes(report) == {"anim.channels"}
    assert len(report.errors) == 2


def test_strip_keeps_root_and_pelvis_translation(figure, rig, reference):
    add_clip(figure, "none/s_walk", ["pelvis", "thigh_l"])
    _add_channel(figure, "none/s_walk", "pelvis", "translation")
    _add_channel(figure, "none/s_walk", "calf_l", "translation")
    _add_channel(figure, "none/s_walk", "hand_r", "scale")
    size_before = len(figure.bin)
    assert strip_animation_channels(figure) == 2
    anim = figure.doc["animations"][0]
    paths = {
        (figure.doc["nodes"][c["target"]["node"]]["name"], c["target"]["path"])
        for c in anim["channels"]
    }
    assert paths == {("pelvis", "rotation"), ("thigh_l", "rotation"), ("pelvis", "translation")}
    assert len(anim["samplers"]) == 3
    assert all(c["sampler"] < 3 for c in anim["channels"])
    assert len(figure.bin) < size_before
    # still a valid, readable file
    again = Gltf.from_bytes(figure.to_bytes())
    report = validate_gltf(again, rig, reference)
    assert report.ok(strict=True), report.issues


def test_strip_without_changes_keeps_file(figure):
    before = figure.to_bytes()
    assert strip_animation_channels(figure) == 0
    assert figure.to_bytes() == before


def test_compact_preserves_mesh_and_skin_data(figure):
    prim = figure.doc["meshes"][0]["primitives"][0]
    weights = figure.accessor(prim["attributes"]["WEIGHTS_0"]).copy()
    ibm = figure.accessor(figure.doc["skins"][0]["inverseBindMatrices"]).copy()
    append_accessor(figure, np.ones((100, 3)), "VEC3")  # unused -> dropped
    count = len(figure.doc["accessors"])
    compact(figure)
    assert len(figure.doc["accessors"]) == count - 1
    prim = figure.doc["meshes"][0]["primitives"][0]
    np.testing.assert_array_equal(figure.accessor(prim["attributes"]["WEIGHTS_0"]), weights)
    np.testing.assert_array_equal(
        figure.accessor(figure.doc["skins"][0]["inverseBindMatrices"]), ibm
    )
    assert figure.doc["buffers"][0]["byteLength"] == len(figure.bin)


def test_weapon_layers_have_no_seam_jumps():
    """Layered clips: arms/head (from the stance) and the spine (from the base) move smoothly."""
    seam = {"spine_01", "spine_02", "spine_03", "clavicle_l", "clavicle_r", "neck", "head"}
    for mode in ("fist", "1h", "2h", "bow", "cbow", "mag"):
        g = Gltf.load(ANIM_SET.parent / f"{mode}.glb")
        for anim in g.doc["animations"]:
            for ch in anim["channels"]:
                name = g.doc["nodes"][ch["target"]["node"]]["name"]
                if ch["target"]["path"] != "rotation" or name not in seam:
                    continue
                v = g.accessor(anim["samplers"][ch["sampler"]]["output"]).astype(float)
                dots = np.abs(np.sum(v[1:] * v[:-1], axis=1)).clip(0, 1)
                step = float(np.degrees(2 * np.arccos(dots)).max())
                assert step < 15.0, f"{anim['name']} {name}: {step:.1f}° per frame"


def test_committed_anim_sets_are_clean(rig, reference):
    for glb in sorted(ANIM_SET.parent.glob("*.glb")):
        g = Gltf.load(glb)
        assert strip_animation_channels(g) == 0, glb.name
        report = validate_gltf(g, rig, reference, path=glb)
        assert report.ok(strict=True), report.issues
    none = validate_gltf(Gltf.load(ANIM_SET), rig, reference, path=ANIM_SET)
    assert none.stats["clips"] == 27
    assert none.stats["events"] >= 15


# --- footstep detection and events writer --------------------------------------------------------


def test_detect_contacts_walk_cycle():
    # foot down at frames 3-5, lifted otherwise; last frame repeats the first
    h = [0.10, 0.08, 0.03, 0.0, 0.0, 0.01, 0.05, 0.09, 0.10]
    assert detect_contacts(h) == [3]


def test_detect_contacts_wraps_around_loop_end():
    h = [0.0, 0.05, 0.10, 0.05, 0.0, 0.0]  # contact at 4..0 (wrapping), last == first
    assert detect_contacts(h) == [4]


def test_detect_contacts_idle_and_short():
    assert detect_contacts([0.0, 0.001, 0.0]) == []
    assert detect_contacts([0.0]) == []


def test_detect_contacts_non_cyclic():
    assert detect_contacts([0.0, 0.1, 0.1, 0.0], cyclic=False) == [0, 3]


def test_format_events_roundtrip():
    text = format_events(
        30,
        {
            "none/s_walk": [Event(20, "footstep_r"), Event(0, "footstep_l")],
            "none/s_idle": [],
        },
    )
    ev, errors = parse_events(text)
    assert errors == [] and ev is not None
    assert ev.fps == 30
    assert list(ev.clips) == ["none/s_walk"]
    assert [(e.frame, e.name) for e in ev.clips["none/s_walk"]] == [
        (0, "footstep_l"),
        (20, "footstep_r"),
    ]
