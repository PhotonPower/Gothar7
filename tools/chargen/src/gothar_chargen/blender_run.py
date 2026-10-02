"""Runs the Blender scripts in gothar_chargen/blender/ headless."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

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


def run_script(blender: Path, script: str, args: list[str], blend_file: Path | None = None) -> str:
    """Runs ``blender --background [file] --python <script> -- <args>``; returns its output."""
    cmd = [str(blender), "--background"]
    if blend_file is None:
        cmd.append("--factory-startup")
    else:
        cmd.append(str(blend_file))
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


def export_glb(blender: Path, blend_file: Path, glb_out: Path) -> None:
    run_script(blender, "export_glb.py", ["--out", str(glb_out)], blend_file=blend_file)
