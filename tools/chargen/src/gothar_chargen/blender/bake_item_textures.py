"""Blender side of ``gothar-chargen build-items``: item textures (items/textures/) – tiling
ambientCG sources resized, procedural ones from items.py (Blender only for image I/O).

    blender --background --factory-startup --python bake_item_textures.py -- \\
        --out <assets/source/items/textures> --sources <DATA_ROOT/characters/ambientcg/items>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.items import (  # noqa: E402
    PROCEDURAL,
    TEXTURES,
    procedural_texture,
    texture_file,
)


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="bake_item_textures")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    return parser.parse_args(argv)


def _save_rgb(colour: np.ndarray, path: Path) -> None:
    """colour (h, w, 3) 0..1, row 0 = top."""
    h, w, _ = colour.shape
    img = bpy.data.images.new(path.stem, w, h, alpha=False)
    rgba = np.ones((h, w, 4), dtype=np.float32)
    rgba[:, :, :3] = colour[::-1]
    img.pixels.foreach_set(rgba.ravel())
    img.file_format = "JPEG"
    img.filepath_raw = str(path)
    img.save(filepath=str(path), quality=90)
    bpy.data.images.remove(img)


def main() -> None:
    args = _parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for name, (source, size, factor) in TEXTURES.items():
        img = bpy.data.images.load(str(args.sources / source), check_existing=False)
        img.scale(size, size)
        rgba = np.empty(size * size * 4, dtype=np.float32)
        img.pixels.foreach_get(rgba)
        colour = rgba.reshape(size, size, 4)[::-1, :, :3] * np.asarray(factor)
        bpy.data.images.remove(img)
        out = args.out / texture_file(name)
        _save_rgb(np.clip(colour, 0, 1), out)
        print(f"[chargen] texture {out.name} ({size})")
    for name in PROCEDURAL:
        out = args.out / texture_file(name)
        _save_rgb(procedural_texture(name), out)
        print(f"[chargen] texture {out.name} (procedural)")


if __name__ == "__main__":
    main()
