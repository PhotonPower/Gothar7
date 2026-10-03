"""Builds an MPFB2 character from a human recipe and saves it as .blend (ADR 0018).

Needs a local Blender 4.5 with the MPFB extension and its CC0 asset packs (docs/05-build.md);
it is never run in CI. Only this script talks to MPFB; it uses MPFB's public services and copies
no MPFB code. Run (without --factory-startup, so the extension is enabled):
    blender --background --python mpfb_human.py -- --recipe <humans/x.human.toml> --out <x.blend>
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.human import load_human  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="mpfb_human")
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args(argv)


def _mpfb() -> tuple[object, object]:
    try:
        from bl_ext.blender_org.mpfb.services.humanservice import (  # type: ignore[import-not-found]
            HumanService,
        )
        from bl_ext.blender_org.mpfb.services.locationservice import (  # type: ignore[import-not-found]
            LocationService,
        )
    except ImportError as e:
        raise SystemExit(f"MPFB extension not available ({e}); see docs/05-build.md") from e
    return HumanService, LocationService


def main() -> None:
    args = _parse_args()
    human = load_human(args.recipe)
    human_service, location_service = _mpfb()
    data_root = Path(location_service.get_user_data())
    if not data_root.is_dir():
        data_root = (
            Path(os.environ["APPDATA"])
            / r"Blender Foundation\Blender\4.5\extensions\.user\blender_org\mpfb\data"
        )

    def asset(rel: str) -> str:
        path = data_root / rel
        if not path.is_file():
            raise SystemExit(f"MPFB asset not installed: {rel} (expected below {data_root})")
        return str(path)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    macro = {
        "gender": 0.5,
        "age": 0.5,
        "muscle": 0.5,
        "weight": 0.5,
        "proportions": 0.5,
        "height": 0.5,
        "cupsize": 0.5,
        "firmness": 0.5,
    }
    macro.update(human.macro)
    macro["race"] = dict(human.race)
    basemesh = human_service.create_human(macro_detail_dict=macro)
    basemesh.name = "basemesh"
    human_service.add_builtin_rig(basemesh, "game_engine", import_weights=True)
    human_service.set_character_skin(asset(human.skin), basemesh, skin_type="GAMEENGINE")
    for asset_type, rel in human.assets():
        obj = human_service.add_mhclo_asset(
            asset(rel), basemesh, asset_type=asset_type, subdiv_levels=0, material_type="MAKESKIN"
        )
        if obj is not None:
            obj["gothar_asset"] = rel
            obj["gothar_type"] = asset_type
    args.out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out.resolve()))
    print(f"[chargen] wrote {args.out}")


if __name__ == "__main__":
    main()
