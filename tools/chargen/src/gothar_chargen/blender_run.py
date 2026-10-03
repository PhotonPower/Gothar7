"""Runs the Blender scripts in gothar_chargen/blender/ headless."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from gothar_chargen.gltf import Gltf
from gothar_chargen.postprocess import (
    apply_alpha_mask,
    externalize_images,
    strip_animation_channels,
)

BLENDER_ENV = "G7_BLENDER"
_SCRIPTS = Path(__file__).resolve().parent / "blender"


class BlenderError(Exception):
    """Blender is not available or a script failed."""


def find_blender(explicit: Path | None = None) -> Path:
    """--blender, then $G7_BLENDER, then PATH, then the default Windows install folders."""
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    if os.environ.get(BLENDER_ENV):
        candidates.append(Path(os.environ[BLENDER_ENV]))
    on_path = shutil.which("blender")
    if on_path:
        candidates.append(Path(on_path))
    foundation = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Blender Foundation"
    if foundation.is_dir():
        candidates.extend(sorted(foundation.glob("Blender */blender.exe"), reverse=True))
    for c in candidates:
        if c.is_file():
            return c
    raise BlenderError(
        f"Blender not found; pass --blender or set {BLENDER_ENV} (Blender 4.5 LTS expected)"
    )


def run_script(
    blender: Path,
    script: str,
    args: list[str],
    blend_file: Path | None = None,
    factory_startup: bool = True,
) -> str:
    """Runs ``blender --background [file] --python <script> -- <args>``; returns its output.

    ``factory_startup=False`` keeps the user's extensions enabled (needed for MPFB).
    """
    cmd = [str(blender), "--background"]
    if blend_file is not None:
        cmd.append(str(blend_file))
    elif factory_startup:
        cmd.append("--factory-startup")
    cmd += ["--python-exit-code", "1", "--python", str(_SCRIPTS / script), "--", *args]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = proc.stdout + proc.stderr
    if proc.returncode != 0:
        raise BlenderError(f"{script} failed (exit {proc.returncode}):\n{output[-4000:]}")
    return output


def build_reference_rig(blender: Path, blend_out: Path, rig_toml: Path | None = None) -> None:
    args = ["--out", str(blend_out)]
    if rig_toml is not None:
        args += ["--rig", str(rig_toml)]
    run_script(blender, "build_reference_rig.py", args)


def export_glb(blender: Path, blend_file: Path, glb_out: Path) -> int:
    """Exports with the project settings, then strips animation channels the contract forbids.

    Returns the number of removed channels (see postprocess.strip_animation_channels).
    """
    run_script(blender, "export_glb.py", ["--out", str(glb_out)], blend_file=blend_file)
    gltf = Gltf.load(glb_out)
    removed = strip_animation_channels(gltf)
    if removed:
        glb_out.write_bytes(gltf.to_bytes())
    return removed


def build_placeholder(blender: Path, ual2: Path, out_dir: Path) -> None:
    run_script(blender, "build_placeholder.py", ["--ual2", str(ual2), "--out", str(out_dir)])


def build_test_parts(blender: Path, out_dir: Path) -> None:
    run_script(blender, "build_test_parts.py", ["--out", str(out_dir)])


def build_mpfb_human(blender: Path, recipe: Path, blend_out: Path) -> None:
    run_script(
        blender,
        "mpfb_human.py",
        ["--recipe", str(recipe), "--out", str(blend_out)],
        factory_startup=False,
    )


def conform_human(blender: Path, blend_file: Path, recipe: Path, out_dir: Path) -> str:
    return run_script(
        blender,
        "conform_human.py",
        ["--recipe", str(recipe), "--out-dir", str(out_dir)],
        blend_file=blend_file,
    )


def finish_textures(glb: Path, textures_root: Path) -> list[Path]:
    """Moves embedded images to textures_root and sets alpha masks (contract §2.3)."""
    gltf = Gltf.load(glb)
    written = externalize_images(gltf, glb, textures_root)
    apply_alpha_mask(gltf)
    glb.write_bytes(gltf.to_bytes())
    return written


def prepare_monster(
    blender: Path, source: Path, config: Path, characters: Path, clips_out: Path, rig_out: Path
) -> str:
    return run_script(
        blender,
        "prepare_monster.py",
        [
            "--config",
            str(config),
            "--characters",
            str(characters),
            "--clips-out",
            str(clips_out),
            "--rig-out",
            str(rig_out),
        ],
        blend_file=source,
    )


def build_set(blender: Path, set_name: str, sources: Path, out_dir: Path) -> str:
    return run_script(
        blender,
        "build_set.py",
        ["--set", set_name, "--sources", str(sources), "--out", str(out_dir)],
    )
