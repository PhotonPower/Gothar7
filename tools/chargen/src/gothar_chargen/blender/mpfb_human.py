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

from gothar_chargen.faces import load_morphs, source_targets  # noqa: E402
from gothar_chargen.human import load_human  # noqa: E402


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="mpfb_human")
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args(argv)


def _mpfb() -> tuple[object, object, object, object]:
    try:
        from bl_ext.blender_org.mpfb.services.faceservice import (  # type: ignore[import-not-found]
            FaceService,
        )
        from bl_ext.blender_org.mpfb.services.humanservice import (  # type: ignore[import-not-found]
            HumanService,
        )
        from bl_ext.blender_org.mpfb.services.locationservice import (  # type: ignore[import-not-found]
            LocationService,
        )
        from bl_ext.blender_org.mpfb.services.targetservice import (  # type: ignore[import-not-found]
            TargetService,
        )
    except ImportError as e:
        raise SystemExit(f"MPFB extension not available ({e}); see docs/05-build.md") from e
    return HumanService, LocationService, TargetService, FaceService


def _load_face_targets(basemesh: object, target_service: object, face_service: object) -> None:
    """Face targets (CC0 packs "Visemes 02", "Faceunits 01") as shape keys at value 0 on the
    basemesh, carried over to brows, lashes, teeth and tongue; conform_human.py mixes them."""
    names = source_targets(load_morphs())
    missing = [n for n in names if not target_service.target_full_path(n)]
    if missing:
        raise SystemExit(
            f"MPFB face targets not installed: {missing} (asset packs visemes02, faceunits01; "
            "docs/05-build.md)"
        )
    target_service.bulk_load_targets(basemesh, [{"target": n, "value": 0.0} for n in names])
    face_service.interpolate_targets(basemesh)


def main() -> None:
    args = _parse_args()
    human = load_human(args.recipe)
    human_service, location_service, target_service, face_service = _mpfb()
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
    if human.shape:
        missing = [n for n in human.shape if not target_service.target_full_path(n)]
        if missing:
            raise SystemExit(f"unknown MPFB targets in [shape]: {missing}")
        target_service.bulk_load_targets(
            basemesh, [{"target": n, "value": v} for n, v in human.shape.items()]
        )
    for asset_type, rel in human.assets():
        mpfb_type = "Clothes" if asset_type == "Beard" else asset_type
        obj = human_service.add_mhclo_asset(
            asset(rel), basemesh, asset_type=mpfb_type, subdiv_levels=0, material_type="MAKESKIN"
        )
        if obj is not None:
            obj["gothar_asset"] = rel
            obj["gothar_type"] = asset_type
    for d in human.derive:  # own pieces start as a fitted copy of a CC0 garment
        obj = human_service.add_mhclo_asset(
            asset(d.source), basemesh, asset_type="Clothes", subdiv_levels=0,
            material_type="MAKESKIN",
        )  # fmt: skip
        obj["gothar_asset"] = d.source
        obj["gothar_type"] = "Clothes"
        obj["gothar_derive"] = d.name
        obj["gothar_texture"] = asset(d.texture)
        if d.normal:
            obj["gothar_normal"] = asset(d.normal)
    _load_face_targets(basemesh, target_service, face_service)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out.resolve()))
    print(f"[chargen] wrote {args.out}")


if __name__ == "__main__":
    main()
