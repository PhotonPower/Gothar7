"""Exports the open .blend to .glb with the project's glTF settings (settings.py).

Run inside Blender 4.5:
    blender --background <file.blend> --python export_glb.py -- --out <file.glb>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="export_glb")
    parser.add_argument("--out", type=Path, required=True, help="output .glb")
    return parser.parse_args(argv)


def main() -> None:
    args = _parse_args()
    if bpy.context.object is not None and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    result = bpy.ops.export_scene.gltf(filepath=str(args.out.resolve()), **GLTF_EXPORT_SETTINGS)
    if "FINISHED" not in result:
        print(f"[chargen] export failed: {result}", file=sys.stderr)
        sys.exit(1)
    print(f"[chargen] wrote {args.out}")


if __name__ == "__main__":
    main()
