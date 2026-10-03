from __future__ import annotations

import io
import json
from pathlib import Path

import pytest

from conftest import REFERENCE_GLB, REPO_ROOT, node
from gothar_chargen import blender_run
from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS
from gothar_chargen.blender_run import BlenderError, find_blender
from gothar_chargen.cli import EXIT_ERROR, EXIT_OK, find_repo_root, main


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    code = main(list(argv), out)
    return code, out.getvalue()


def test_validate_default_folder():
    code, text = run("validate")
    assert code == EXIT_OK, text
    assert "human_reference.glb" in text
    assert "0 failed" in text


def test_validate_broken_file(tmp_path, figure):
    node(figure, "calf_l")["name"] = "shin_l"
    bad = tmp_path / "bad.glb"
    bad.write_bytes(figure.to_bytes())
    code, text = run("validate", str(bad), str(REFERENCE_GLB))
    assert code == EXIT_ERROR
    assert "FAILED" in text and "skeleton.names" in text
    assert "2 file(s), 1 failed" in text


def test_validate_json(tmp_path):
    code, text = run("validate", "--json", str(REFERENCE_GLB))
    assert code == EXIT_OK
    data = json.loads(text)
    assert data[0]["ok"] is True
    assert data[0]["stats"]["bones"] == 60


def test_strict_turns_warnings_into_failure(tmp_path, figure):
    node(figure, "mannequin").pop("skin")
    f = tmp_path / "w.glb"
    f.write_bytes(figure.to_bytes())
    assert run("validate", str(f))[0] == EXIT_OK
    assert run("validate", "--strict", str(f))[0] == EXIT_ERROR


def test_validate_without_reference(tmp_path):
    code, _ = run("validate", "--no-reference", str(REFERENCE_GLB))
    assert code == EXIT_OK
    code, text = run("validate", "--reference", str(tmp_path / "missing.glb"), str(REFERENCE_GLB))
    assert code == EXIT_ERROR and "reference rig not found" in text


def test_validate_empty_folder(tmp_path):
    code, text = run("validate", str(tmp_path))
    assert code == EXIT_ERROR and "no .glb" in text


def test_find_repo_root():
    assert find_repo_root() == REPO_ROOT


def test_missing_blender(monkeypatch, tmp_path):
    monkeypatch.delenv(blender_run.BLENDER_ENV, raising=False)
    monkeypatch.setenv("PROGRAMFILES", str(tmp_path))
    monkeypatch.setattr(blender_run.shutil, "which", lambda _: None)
    with pytest.raises(BlenderError, match="Blender not found"):
        find_blender()
    code, text = run("export", str(tmp_path / "x.blend"))
    assert code == EXIT_ERROR and "Blender not found" in text


def test_blender_from_env(monkeypatch, tmp_path):
    exe = tmp_path / "blender.exe"
    exe.write_text("")
    monkeypatch.setenv(blender_run.BLENDER_ENV, str(exe))
    assert find_blender() == exe
    assert find_blender(Path(exe)) == exe


def test_export_settings_keep_contract():
    s = GLTF_EXPORT_SETTINGS
    assert s["export_format"] == "GLB" and s["export_yup"] is True
    assert s["export_def_bones"] is False  # sockets are non-deform bones
    assert s["export_influence_nb"] == 4
    assert s["export_apply"] is False
    assert s["export_rest_position_armature"] is True


def _blender_or_skip() -> Path:
    try:
        return find_blender()
    except BlenderError:
        pytest.skip("Blender not installed")


@pytest.mark.blender
def test_blend_source_matches_committed_glb():
    """Exporting the committed .blend again must reproduce a valid rig (needs Blender)."""
    _blender_or_skip()
    blend = REFERENCE_GLB.with_suffix(".blend")
    code, text = run("validate", "--strict", str(blend))
    assert code == EXIT_OK, text


def test_build_placeholder_needs_sources(tmp_path):
    code, text = run("build-placeholder", "--sources", str(tmp_path))
    assert code == EXIT_ERROR
    assert "UAL2_Standard.glb" in text


def test_report_command():
    code, text = run("report")
    assert code == EXIT_OK
    assert "Prio A:" in text and "/36 clips present" in text
    code, _ = run("report", "--fail-missing")
    assert code == EXIT_OK  # F2 DoD: no Prio-A clip missing
    code, text = run("report", "--json")
    assert json.loads(text)["prio_a"]["listed"] == 36


def test_build_set_rejects_unknown_set(tmp_path):
    code, text = run("build-set", "nope", "--sources", str(tmp_path))
    assert code == EXIT_ERROR and "unknown clip list" in text


@pytest.mark.blender
def test_assemble_test_figure(tmp_path):
    """Assembling the committed test manifest again yields a valid figure (needs Blender)."""
    import shutil

    _blender_or_skip()
    characters = REPO_ROOT / "assets/source/characters"
    out = tmp_path / "characters"
    shutil.copytree(characters / "rig", out / "rig")
    shutil.copytree(characters / "parts", out / "parts")
    (out / "figures").mkdir()
    manifest = shutil.copy(characters / "figures/test_plain.figure.toml", out / "figures")
    code, text = run("assemble", str(manifest), "--out-dir", str(out))
    assert code == EXIT_OK, text
    assert (out / "figures/test_plain.glb").is_file()
