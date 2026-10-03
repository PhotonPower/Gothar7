"""Command line interface: ``gothar-chargen <command>``."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import tomllib
from collections.abc import Sequence
from pathlib import Path
from typing import TextIO

from gothar_chargen import __version__
from gothar_chargen.blender_run import (
    BlenderError,
    assemble_figure,
    build_mpfb_human,
    build_placeholder,
    build_reference_rig,
    build_set,
    build_test_parts,
    conform_human,
    export_glb,
    find_blender,
    finish_textures,
    prepare_monster,
)
from gothar_chargen.clipspec import ClipSpecError, load_set_spec, packaged_sets
from gothar_chargen.collision import CollisionError, derive_collision, write_collision
from gothar_chargen.figure import SUFFIX as FIGURE_SUFFIX
from gothar_chargen.figure import FigureError, load_figure
from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.human import SUFFIX as HUMAN_SUFFIX
from gothar_chargen.human import HumanError, load_human
from gothar_chargen.report import ReportError, progress
from gothar_chargen.skeleton import (
    RigSpec,
    SkeletonError,
    load_rig,
    packaged_species,
    species_of,
)
from gothar_chargen.validate import ReferencePose, Report, reference_pose, validate_file

EXIT_OK = 0
EXIT_ERROR = 1

CHARACTERS_DIR = Path("assets/source/characters")
REFERENCE_GLB = CHARACTERS_DIR / "rig/human_reference.glb"
ANIMATION_LIST = Path("docs/design/animation-list.md")
MONSTER_DATA = Path(__file__).resolve().parent / "data" / "monsters"


def monster_reference(characters: Path, species: str) -> Path:
    return characters / "monsters" / species / "rig" / f"{species}_reference.glb"


class _Rigs:
    """Rig and reference pose per file: monsters/<species>/... use the species rig (§7)."""

    def __init__(self, args: argparse.Namespace, characters: Path | None) -> None:
        self.explicit = args.rig
        self.characters = characters
        self.no_reference = getattr(args, "no_reference", False)
        self.reference_path: Path | None = getattr(args, "reference", None)
        self._cache: dict[str, tuple[RigSpec, ReferencePose | None]] = {}

    def for_file(self, path: Path) -> tuple[RigSpec, ReferencePose | None]:
        species = None if self.explicit else species_of(path)
        key = species or ""
        if key not in self._cache:
            rig = load_rig(self.explicit, species=species)
            ref = None
            if not self.no_reference:
                if self.reference_path is not None:
                    ref_path = self.reference_path
                elif self.characters is None:
                    raise FileNotFoundError("repository not found; use --reference")
                elif species:
                    ref_path = monster_reference(self.characters, species)
                else:
                    ref_path = self.characters / REFERENCE_GLB.relative_to(CHARACTERS_DIR)
                if not ref_path.is_file():
                    raise FileNotFoundError(
                        f"reference rig not found ({ref_path}); use --reference"
                    )
                ref = reference_pose(Gltf.load(ref_path))
            self._cache[key] = (rig, ref)
        return self._cache[key]


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
        size = f"{s['height']} m" if "height" in s else "part"
        parts.append(f"{s['skinned_meshes']} skinned mesh(es), {size}")
    if s.get("triangles"):
        lods = f", {s['lods']} LOD levels" if s.get("lods") else ""
        parts.append(f"{s['triangles']} tris{lods}")
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
    root = find_repo_root()
    rigs = _Rigs(args, root / CHARACTERS_DIR if root else None)

    paths = args.paths or ([root / CHARACTERS_DIR] if root else [])
    files = _collect(paths)
    if not files:
        print("error: no .glb/.blend files to validate", file=out)
        return EXIT_ERROR

    reports: list[Report] = []
    with tempfile.TemporaryDirectory(prefix="gothar-chargen-") as tmp:
        for f in files:
            rig, reference = rigs.for_file(f)
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


def _characters_dir(args: argparse.Namespace) -> Path:
    root = find_repo_root()
    out_dir = args.out_dir or (root / CHARACTERS_DIR if root else None)
    if out_dir is None:
        raise FileNotFoundError("repository not found; pass --out-dir")
    return out_dir


def _export_and_check(
    blender: Path, blends: list[Path], out_dir: Path, args: argparse.Namespace, out: TextIO
) -> int:
    rigs = _Rigs(args, out_dir)
    ok = True
    for blend in blends:
        glb = blend.with_suffix(".glb")
        export_glb(blender, blend, glb)
        blend.with_suffix(".blend1").unlink(missing_ok=True)
        rig, reference = rigs.for_file(glb)
        report = validate_file(glb, rig, reference)
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_build_placeholder(args: argparse.Namespace, out: TextIO) -> int:
    out_dir = _characters_dir(args)
    ual2 = _find_one(args.sources, "UAL2_Standard.glb")
    blender = find_blender(args.blender)
    build_placeholder(blender, ual2, out_dir)
    blends = [out_dir / "figures/placeholder_mannequin.blend"]
    return _export_and_check(blender, blends, out_dir, args, out)


def _cmd_build_set(args: argparse.Namespace, out: TextIO) -> int:
    out_dir = _characters_dir(args)
    names = packaged_sets(monsters=False) if args.set == ["all"] else args.set
    specs = [load_set_spec(n) for n in names]  # fail early on a bad list
    blender = find_blender(args.blender)
    blends = []
    for spec in specs:
        log = build_set(blender, spec.set, args.sources, out_dir)
        for line in log.splitlines():
            if line.startswith("[chargen] clip"):
                print(line[10:], file=out)
        blends.append(spec.blend_path(out_dir))
    return _export_and_check(blender, blends, out_dir, args, out)


def _cmd_monster(args: argparse.Namespace, out: TextIO) -> int:
    """CC0 source animal -> contract rig, reference mesh and clip source (contract §7)."""
    characters = _characters_dir(args)
    blender = find_blender(args.blender)
    ok = True
    for species in args.species:
        config = MONSTER_DATA / f"{species}.build.toml"
        if not config.is_file():
            print(f"error: no build configuration {config.name} in data/monsters/", file=out)
            return EXIT_ERROR
        cfg = tomllib.loads(config.read_text(encoding="utf-8"))
        source = args.sources / cfg["source"]
        if not source.is_file():
            print(f"error: source not found: {source}", file=out)
            return EXIT_ERROR
        clips = args.sources / f"{species}_clips.blend"
        log = prepare_monster(
            blender, source, config, characters, clips, MONSTER_DATA / f"{species}.toml"
        )
        for line in log.splitlines():
            if line.startswith("[chargen]"):
                print(line[10:], file=out)
        glb = monster_reference(characters, species)
        write_collision(MONSTER_DATA / f"{species}.toml", derive_collision(Gltf.load(glb)))
        rig = load_rig(MONSTER_DATA / f"{species}.toml")
        report = validate_file(glb, rig, reference_pose(Gltf.load(glb)))
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_collision(args: argparse.Namespace, out: TextIO) -> int:
    """[rig.collision] of monster rigs from their reference mesh (no Blender needed)."""
    characters = _characters_dir(args)
    for species in args.species or packaged_species():
        rig_toml = MONSTER_DATA / f"{species}.toml"
        collision = derive_collision(Gltf.load(monster_reference(characters, species)))
        write_collision(rig_toml, collision)
        print(f"{species}: {collision}", file=out)
    return EXIT_OK


def _cmd_assemble(args: argparse.Namespace, out: TextIO) -> int:
    characters = _characters_dir(args)
    manifests = args.manifests or sorted((characters / "figures").glob("*" + FIGURE_SUFFIX))
    if not manifests:
        print("error: no figure manifests found", file=out)
        return EXIT_ERROR
    figures = [(m, load_figure(m)) for m in manifests]  # fail early on a bad manifest
    blender = find_blender(args.blender)
    rig = load_rig(args.rig)
    reference = reference_pose(Gltf.load(characters / REFERENCE_GLB.relative_to(CHARACTERS_DIR)))
    ok = True
    for manifest, figure in figures:
        glb = manifest.with_name(figure.name + ".glb")
        assemble_figure(blender, manifest, characters, glb)
        finish_textures(glb, characters / "textures")
        report = validate_file(glb, rig, reference)
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_human(args: argparse.Namespace, out: TextIO) -> int:
    characters = _characters_dir(args)
    recipes = args.recipes or sorted((characters / "humans").glob("*" + HUMAN_SUFFIX))
    humans = [(r, load_human(r)) for r in recipes]  # fail early on a bad recipe
    if not humans:
        print("error: no human recipes found", file=out)
        return EXIT_ERROR
    blender = find_blender(args.blender)
    rig = load_rig(args.rig)
    ok = True
    for recipe, human in humans:
        parts_dir = characters / "parts" / human.name
        with tempfile.TemporaryDirectory(prefix="gothar-human-") as tmp:
            blend = Path(tmp) / f"{human.name}.blend"
            build_mpfb_human(blender, recipe, blend)
            log = conform_human(blender, blend, recipe, parts_dir)
        for line in log.splitlines():
            if line.startswith("[chargen] wrote"):
                print(line[10:], file=out)
        textures = []
        for glb in sorted(parts_dir.glob("*.glb")):
            textures += finish_textures(glb, characters / "textures")
            report = validate_file(glb, rig, None)
            _print_report(report, out)
            ok = ok and report.ok(strict=True)
        for t in textures:
            print(f"texture {t.relative_to(characters)}", file=out)
        manifest = characters / "figures" / f"{human.name}{FIGURE_SUFFIX}"
        if not manifest.exists():
            parts = "\n".join(
                f'{role} = "parts/{human.name}/{role}.glb"'
                for role in ("body", "head", "hair")
                if (parts_dir / f"{role}.glb").is_file()
            )
            manifest.write_text(
                f"# {human.name}: assembled from parts built by gothar-chargen human\n"
                f"version = 1\nlods = [1.0, 0.5, 0.2]\n\n[parts]\n{parts}\n",
                encoding="utf-8",
                newline="\n",
            )
            print(f"wrote {manifest.relative_to(characters)}", file=out)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_build_test_parts(args: argparse.Namespace, out: TextIO) -> int:
    characters = _characters_dir(args)
    folder = characters / "parts" / "test"
    build_test_parts(find_blender(args.blender), folder)
    reports = [validate_file(p, load_rig(args.rig), None) for p in sorted(folder.glob("*.glb"))]
    for r in reports:
        _print_report(r, out)
    return EXIT_OK if all(r.ok(strict=True) for r in reports) else EXIT_ERROR


def _cmd_report(args: argparse.Namespace, out: TextIO) -> int:
    root = find_repo_root()
    list_path = args.list or (root / ANIMATION_LIST if root else None)
    anims = args.anims or (root / CHARACTERS_DIR / "anims" if root else None)
    if list_path is None or anims is None:
        print("error: repository not found; pass --list and --anims", file=out)
        return EXIT_ERROR
    monsters = [anims.parent / "monsters"] if (anims.parent / "monsters").is_dir() else []
    result = progress(list_path.read_text(encoding="utf-8"), anims, *monsters)
    if args.json:
        json.dump(result.to_dict(), out, indent=2)
        print(file=out)
    else:
        listed = len(result.listed)
        print(f"Prio A: {listed - len(result.missing)}/{listed} clips present", file=out)
        if result.missing:
            print(f"  missing ({len(result.missing)}): {', '.join(result.missing)}", file=out)
        for heading, (present, total) in result.section_counts().items():
            print(f"{heading}: {present}/{total} clips present", file=out)
        for s in result.stale:
            print(f"  list out of date: {s}", file=out)
        if result.extra:
            print(f"Other clips ({len(result.extra)}): {', '.join(result.extra)}", file=out)
    failed = (args.fail_missing and result.missing) or (args.fail_stale and result.stale)
    return EXIT_ERROR if failed else EXIT_OK


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

    sources_help = "folder with the unpacked Quaternius UAL1/UAL2 [Standard] zips (CC0)"
    p = sub.add_parser("build-placeholder", help="placeholder figure from the Quaternius mannequin")
    p.add_argument("--sources", type=Path, required=True, help=sources_help)
    p.add_argument("--out-dir", type=Path, help="default: assets/source/characters")
    p.set_defaults(func=_cmd_build_placeholder)

    p = sub.add_parser("assemble", help="assemble figures from figures/<name>.figure.toml")
    p.add_argument("manifests", nargs="*", type=Path, help="default: all in figures/")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_assemble)

    p = sub.add_parser("human", help="MPFB2 human from humans/<name>.human.toml -> parts (local)")
    p.add_argument("recipes", nargs="*", type=Path, help="default: all in humans/")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_human)

    p = sub.add_parser("build-test-parts", help="own simple test parts for the figure kit")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_build_test_parts)

    p = sub.add_parser("report", help="animation-list.md vs. clips in anims/ and monsters/")
    p.add_argument("--list", type=Path, help="default: docs/design/animation-list.md")
    p.add_argument("--anims", type=Path, help="default: assets/source/characters/anims")
    p.add_argument("--json", action="store_true", help="machine-readable output")
    p.add_argument("--fail-missing", action="store_true", help="exit 1 if Prio-A clips are missing")
    p.add_argument("--fail-stale", action="store_true", help="exit 1 if a list status is outdated")
    p.set_defaults(func=_cmd_report)

    p = sub.add_parser("monster", help="monster rig + clip source from a CC0 animal (§7, local)")
    p.add_argument("species", nargs="+", help="species with data/monsters/<species>.build.toml")
    p.add_argument(
        "--sources", type=Path, required=True, help="DATA_ROOT/characters/monsters (unpacked packs)"
    )
    p.add_argument("--out-dir", type=Path, help="default: assets/source/characters")
    p.set_defaults(func=_cmd_monster)

    p = sub.add_parser("collision", help="monster collision capsules from the reference meshes")
    p.add_argument("species", nargs="*", help="default: all packaged monster rigs")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_collision)

    p = sub.add_parser("build-set", help="animation sets from data/clips/<set>.toml")
    p.add_argument("set", nargs="+", help="set names (none, swim, wolf, ...) or 'all' (humans)")
    p.add_argument(
        "--sources",
        type=Path,
        required=True,
        help=sources_help + "; monster sets: the folder with <species>_clips.blend",
    )
    p.add_argument("--out-dir", type=Path, help="default: assets/source/characters")
    p.set_defaults(func=_cmd_build_set)
    return parser


def main(argv: Sequence[str] | None = None, out: TextIO | None = None) -> int:
    if out is None:
        out = sys.stdout
        if hasattr(out, "reconfigure"):  # Windows consoles default to a legacy code page
            out.reconfigure(encoding="utf-8", errors="replace")
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args, out))
    except (
        BlenderError,
        ClipSpecError,
        CollisionError,
        FigureError,
        GltfError,
        HumanError,
        ReportError,
        SkeletonError,
        OSError,
    ) as e:
        print(f"error: {e}", file=out)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
