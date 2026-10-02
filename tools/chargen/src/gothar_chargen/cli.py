"""Command line interface: ``gothar-chargen <command>``."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import TextIO

from gothar_chargen import __version__
from gothar_chargen.blender_run import (
    BlenderError,
    build_placeholder,
    build_reference_rig,
    export_glb,
    find_blender,
)
from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.skeleton import SkeletonError, load_rig
from gothar_chargen.validate import Report, reference_pose, validate_file

EXIT_OK = 0
EXIT_ERROR = 1

CHARACTERS_DIR = Path("assets/source/characters")
REFERENCE_GLB = CHARACTERS_DIR / "rig/human_reference.glb"


def find_repo_root(start: Path | None = None) -> Path | None:
    """First parent of this package (then of ``start``/cwd) containing assets/source/characters."""
    for base in (Path(__file__).resolve(), (start or Path.cwd()).resolve()):
        for p in (base, *base.parents):
            if (p / CHARACTERS_DIR).is_dir():
                return p
    return None


def _collect(paths: Sequence[Path]) -> list[Path]:
    files: list[Path] = []
    for p in paths:
        if p.is_dir():
            files.extend(sorted(p.rglob("*.glb")))
        else:
            files.append(p)
    return files


def _print_report(report: Report, out: TextIO) -> None:
    s = report.stats
    parts = [f"{s['bones']} bones"] if "bones" in s else []
    if s.get("skinned_meshes"):
        parts.append(f"{s['skinned_meshes']} skinned mesh(es), {s.get('height', '?')} m")
    if s.get("morph_targets"):
        parts.append(f"{len(s['morph_targets'])} morph targets")
    if s.get("clips"):
        parts.append(f"{s['clips']} clips")
    if "events" in s:
        parts.append(f"{s['events']} events")
    state = "OK" if report.ok() else "FAILED"
    extra = f" [{len(report.warnings)} warning(s)]" if report.warnings else ""
    print(f"{state:<6} {report.path}  ({', '.join(parts) or 'no skeleton'}){extra}", file=out)
    for issue in report.issues:
        print(f"  {issue.level.upper():<7} {issue.code}: {issue.message}", file=out)


def _cmd_validate(args: argparse.Namespace, out: TextIO) -> int:
    rig = load_rig(args.rig)
    root = find_repo_root()
    reference = None
    if not args.no_reference:
        ref_path = args.reference or (root / REFERENCE_GLB if root else None)
        if ref_path is None or not ref_path.is_file():
            print(f"error: reference rig not found ({ref_path}); use --reference", file=out)
            return EXIT_ERROR
        reference = reference_pose(Gltf.load(ref_path))

    paths = args.paths or ([root / CHARACTERS_DIR] if root else [])
    files = _collect(paths)
    if not files:
        print("error: no .glb/.blend files to validate", file=out)
        return EXIT_ERROR

    reports: list[Report] = []
    with tempfile.TemporaryDirectory(prefix="gothar-chargen-") as tmp:
        for f in files:
            if f.suffix.lower() == ".blend":
                blender = find_blender(args.blender)
                glb = Path(tmp) / (f.stem + ".glb")
                export_glb(blender, f, glb)
                report = validate_file(glb, rig, reference)
                report.path = f
            else:
                report = validate_file(f, rig, reference)
            reports.append(report)

    if args.json:
        json.dump([r.to_dict() for r in reports], out, indent=2)
        print(file=out)
    else:
        for r in reports:
            _print_report(r, out)
        failed = sum(not r.ok(args.strict) for r in reports)
        print(f"{len(reports)} file(s), {failed} failed", file=out)
    return EXIT_OK if all(r.ok(args.strict) for r in reports) else EXIT_ERROR


def _cmd_build_rig(args: argparse.Namespace, out: TextIO) -> int:
    root = find_repo_root()
    out_dir = args.out_dir or (root / CHARACTERS_DIR / "rig" if root else None)
    if out_dir is None:
        print("error: repository not found; pass --out-dir", file=out)
        return EXIT_ERROR
    blender = find_blender(args.blender)
    blend = out_dir / "human_reference.blend"
    glb = out_dir / "human_reference.glb"
    build_reference_rig(blender, blend, args.rig)
    export_glb(blender, blend, glb)
    print(f"wrote {blend}\nwrote {glb}", file=out)
    rig = load_rig(args.rig)
    report = validate_file(glb, rig, reference_pose(Gltf.load(glb)))
    _print_report(report, out)
    return EXIT_OK if report.ok() else EXIT_ERROR


def _cmd_export(args: argparse.Namespace, out: TextIO) -> int:
    glb = args.out or args.blend.with_suffix(".glb")
    removed = export_glb(find_blender(args.blender), args.blend, glb)
    note = f" ({removed} translation/scale channels removed)" if removed else ""
    print(f"wrote {glb}{note}", file=out)
    return EXIT_OK


def _find_one(folder: Path, pattern: str) -> Path:
    found = sorted(folder.rglob(pattern))
    if len(found) != 1:
        raise FileNotFoundError(f"expected one '{pattern}' below {folder}, found {len(found)}")
    return found[0]


def _cmd_build_placeholder(args: argparse.Namespace, out: TextIO) -> int:
    root = find_repo_root()
    out_dir = args.out_dir or (root / CHARACTERS_DIR if root else None)
    if out_dir is None:
        print("error: repository not found; pass --out-dir", file=out)
        return EXIT_ERROR
    ual1 = _find_one(args.quaternius, "AnimationLibrary_Godot_Standard.glb")
    ual2 = _find_one(args.quaternius, "UAL2_Standard.glb")
    blender = find_blender(args.blender)
    build_placeholder(blender, ual1, ual2, out_dir, args.clips)
    rig = load_rig(args.rig)
    reference = reference_pose(Gltf.load(out_dir / REFERENCE_GLB.relative_to(CHARACTERS_DIR)))
    ok = True
    for blend in (
        out_dir / "figures/placeholder_mannequin.blend",
        out_dir / "anims/human/none.blend",
    ):
        glb = blend.with_suffix(".glb")
        export_glb(blender, blend, glb)
        blend.with_suffix(".blend1").unlink(missing_ok=True)
        report = validate_file(glb, rig, reference)
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gothar-chargen", description="Character-track tools for Gothar"
    )
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument("--blender", type=Path, help="path to blender(.exe), Blender 4.5 LTS")
    parser.add_argument("--rig", type=Path, help="skeleton definition .toml (default: packaged)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("validate", help="check .glb/.blend files against the reference rig")
    p.add_argument(
        "paths", nargs="*", type=Path, help="files or folders (default: assets/source/characters)"
    )
    p.add_argument("--reference", type=Path, help="reference .glb (default: human_reference.glb)")
    p.add_argument("--no-reference", action="store_true", help="skip the bind-pose comparison")
    p.add_argument("--strict", action="store_true", help="treat warnings as errors")
    p.add_argument("--json", action="store_true", help="machine-readable output")
    p.set_defaults(func=_cmd_validate)

    p = sub.add_parser("build-rig", help="generate human_reference.blend/.glb with Blender")
    p.add_argument("--out-dir", type=Path, help="default: assets/source/characters/rig")
    p.set_defaults(func=_cmd_build_rig)

    p = sub.add_parser("export", help="export a .blend to .glb with the project glTF settings")
    p.add_argument("blend", type=Path)
    p.add_argument("--out", type=Path, help="default: next to the .blend")
    p.set_defaults(func=_cmd_export)

    p = sub.add_parser(
        "build-placeholder",
        help="F1 placeholder figure + test clips from the Quaternius libraries (CC0)",
    )
    p.add_argument(
        "--quaternius",
        type=Path,
        required=True,
        help="folder with the unpacked UAL1/UAL2 [Standard] zips (opengameart.org)",
    )
    p.add_argument("--clips", default="f1_placeholder", help="clip list in data/clips/")
    p.add_argument("--out-dir", type=Path, help="default: assets/source/characters")
    p.set_defaults(func=_cmd_build_placeholder)
    return parser


def main(argv: Sequence[str] | None = None, out: TextIO | None = None) -> int:
    out = out or sys.stdout
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args, out))
    except (BlenderError, GltfError, SkeletonError, OSError) as e:
        print(f"error: {e}", file=out)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
