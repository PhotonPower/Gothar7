"""Blender side of ``gothar-chargen fabrics``: reads the fabric tiles, bakes every texture of
data/fabrics.toml (fabrics.py) and writes it to textures/cloth/ (Blender only for image I/O).

    blender --background --factory-startup --python bake_fabrics.py -- \\
        --characters <assets/source/characters> --sources <DATA_ROOT/characters/ambientcg/fabric>
"""

from __future__ import annotations

import argparse
import sys
import zlib
from pathlib import Path

import bpy  # type: ignore[import-not-found]
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.fabrics import (  # noqa: E402
    SIZE,
    bake,
    fray_alpha,
    garment_of,
    load_fabrics,
)
from gothar_chargen.gltf import Gltf  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="bake_fabrics")
    parser.add_argument("--characters", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=[])
    return parser.parse_args(argv)


def _luminance(path: Path) -> np.ndarray:
    """Tile luminance (h, w), row 0 at the top."""
    img = bpy.data.images.load(str(path), check_existing=False)
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1]
    bpy.data.images.remove(img)
    return px[:, :, :3] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def _save(colour: np.ndarray, path: Path, alpha: np.ndarray | None = None) -> None:
    """colour (size, size, 3) 0..1, row 0 = top (glTF v = 0); alpha (size, size) for .png."""
    size = colour.shape[0]
    img = bpy.data.images.new(path.stem, size, size, alpha=alpha is not None)
    rgba = np.ones((size, size, 4), dtype=np.float32)
    rgba[:, :, :3] = colour[::-1]  # Blender rows start at the bottom
    if alpha is not None:
        rgba[:, :, 3] = alpha[::-1]
    img.pixels.foreach_set(rgba.ravel())
    img.file_format = "JPEG" if path.suffix == ".jpg" else "PNG"
    img.filepath_raw = str(path)
    bpy.context.scene.render.image_settings.quality = 90
    img.save(filepath=str(path), quality=90)
    bpy.data.images.remove(img)


def main() -> None:
    args = _parse_args()
    data = load_fabrics()
    tiles = {name: _luminance(args.sources / t.source) for name, t in data.tiles.items()}
    for target in data.targets:
        if args.only and target.file not in args.only:
            continue
        garment = garment_of(Gltf.load(args.characters / target.part), target.material)
        seed = zlib.crc32(target.file.encode())
        colour = bake(
            tiles[target.tile],
            data.tiles[target.tile].size,
            garment,
            target.wear,
            seed=seed,
            tint=target.tint,
            size=SIZE,
            soil=target.soil,
            soil_amount=target.soil_amount,
        )
        out = args.characters / "textures" / "cloth" / target.file
        alpha = fray_alpha(garment, target.fray, seed, SIZE) if target.fray > 0 else None
        _save(colour, out, alpha)
        fray = f", fray {target.fray}" if target.fray > 0 else ""
        fray += f", soil {target.soil} {target.soil_amount}" if target.soil else ""
        print(f"[chargen] baked {out.name} ({target.tile}, wear {target.wear}{fray})")


if __name__ == "__main__":
    main()
