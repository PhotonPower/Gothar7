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
from gothar_chargen.assemble import AssembleError, assemble_all
from gothar_chargen.blender_run import (
    BlenderError,
    bake_fabrics,
    bake_item_textures,
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
from gothar_chargen.clipfix import repair_set
from gothar_chargen.clipspec import ClipSpecError, load_set_spec, packaged_sets
from gothar_chargen.collision import CollisionError, derive_collision, write_collision
from gothar_chargen.events import update_speeds
from gothar_chargen.fabrics import apply_fray_materials, load_fabrics
from gothar_chargen.figure import SUFFIX as FIGURE_SUFFIX
from gothar_chargen.figure import FigureError
from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.human import SUFFIX as HUMAN_SUFFIX
from gothar_chargen.human import HumanError, load_human
from gothar_chargen.items import build_items, validate_item
from gothar_chargen.meshdata import split_lod
from gothar_chargen.partdata import (
    PartDataError,
    body_or_head_data,
    garment_data,
    hide_own_skin,
    name_meshes,
    write_part,
)
from gothar_chargen.poke import CLIPS as POKE_CLIPS
from gothar_chargen.poke import SAMPLES as POKE_SAMPLES
from gothar_chargen.poke import check_figure, poke_report
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
ITEMS_DIR = Path("assets/source/items")
POKE_FIGURES = ("farmer", "peasant_woman", "laborer", "guard", "old_man")  # the test NPCs
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

    defaults = [root / CHARACTERS_DIR, root / ITEMS_DIR] if root else []
    paths = args.paths or [d for d in defaults if d.is_dir()]
    files = _collect(paths)
    if not files:
        print("error: no .glb/.blend files to validate", file=out)
        return EXIT_ERROR

    reports: list[Report] = []
    with tempfile.TemporaryDirectory(prefix="gothar-chargen-") as tmp:
        for f in files:
            if "items" in f.parts:
                reports.append(validate_item(f))
                continue
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
    """Figures from manifests and parts, pure Python (no Blender): figures/<name>.glb, then the
    strict validator. Without arguments all manifests; outputs without a manifest are removed."""
    characters = _characters_dir(args)
    figures_dir = characters / "figures"
    manifests = list(args.manifests) or None
    if not (manifests or list(figures_dir.glob("*" + FIGURE_SUFFIX))):
        print("error: no figure manifests found", file=out)
        return EXIT_ERROR
    written = assemble_all(figures_dir, characters, manifests)
    rig = load_rig(args.rig)
    reference = reference_pose(Gltf.load(characters / REFERENCE_GLB.relative_to(CHARACTERS_DIR)))
    ok = True
    for glb in written:
        report = validate_file(glb, rig, reference)
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def _part_role(g: Gltf) -> str | None:
    for node in g.list("nodes"):
        base, level = split_lod(str(node.get("name", "")))
        if "mesh" in node and level is not None:
            return base
    return None


def update_part_data(characters: Path, dirs: list[Path] | None, out: TextIO) -> None:
    """(Re)computes the assembly data of parts (§6.2): neck rings of bodies and heads, masks of
    garments on the body their kit is fitted to. `dirs`: part folders (default: all)."""
    parts_root = characters / "parts"
    folders = dirs or sorted(p for p in parts_root.iterdir() if p.is_dir())
    garments: list[tuple[Path, Gltf]] = []
    for folder in folders:
        for path in sorted(folder.glob("*.glb")):
            g = Gltf.load(path)
            role = _part_role(g)
            if role in ("body", "head"):
                removed = hide_own_skin(g) if role == "body" else 0
                write_part(path, g, body_or_head_data(g, role))
                extra = (
                    f", {removed} skin triangles under its own clothing removed" if removed else ""
                )
                print(f"part data {path.relative_to(characters)} ({role}{extra})", file=out)
            elif role == "cloth":
                garments.append((path, g))
            elif role in ("hair", "beard"):  # no assembly data; only stable mesh names
                name_meshes(g)
                path.write_bytes(g.to_bytes())
                print(f"part data {path.relative_to(characters)} ({role}, names)", file=out)
    humans = characters / "humans"
    for path, g in garments:
        recipe = load_human(humans / (path.parent.name + HUMAN_SUFFIX))
        if recipe.fit_to is None:
            raise HumanError(f"{path.parent.name}: garment parts need a recipe with fit_to")
        body_rel = f"parts/{recipe.fit_to}/body.glb"
        body = Gltf.load(characters / body_rel)
        write_part(path, g, garment_data(g, body, body_rel, recipe.hides.get(path.stem, ())))
        print(f"part data {path.relative_to(characters)} (covers {body_rel})", file=out)


def _cmd_part_data(args: argparse.Namespace, out: TextIO) -> int:
    characters = _characters_dir(args)
    update_part_data(characters, [Path(d).resolve() for d in args.dirs] or None, out)
    return EXIT_OK


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
        # whole humans get a figure; base bodies and heads are combined in hand-written manifests
        if {"body", "head"} <= set(human.parts) and not manifest.exists():
            parts = "\n".join(
                f'{role} = "parts/{human.name}/{role}.glb"'
                for role in ("body", "head", "hair")
                if (parts_dir / f"{role}.glb").is_file()
            )
            manifest.write_text(
                f"# {human.name}: assembled from parts built by gothar-chargen human\n"
                f"version = 1\n\n[parts]\n{parts}\n",
                encoding="utf-8",
                newline="\n",
            )
            print(f"wrote {manifest.relative_to(characters)}", file=out)
    # assembly data (§6.2): the rebuilt parts, and the kits fitted to a rebuilt base body
    built = {h.name for _, h in humans}
    dirs = {characters / "parts" / name for name in built}
    for kit in sorted((characters / "humans").glob("*" + HUMAN_SUFFIX)):
        recipe = load_human(kit)
        if recipe.fit_to in built:
            dirs.add(characters / "parts" / recipe.name)
    update_part_data(characters, sorted(d for d in dirs if d.is_dir()), out)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_build_test_parts(args: argparse.Namespace, out: TextIO) -> int:
    characters = _characters_dir(args)
    folder = characters / "parts" / "test"
    build_test_parts(find_blender(args.blender), folder)
    reports = [validate_file(p, load_rig(args.rig), None) for p in sorted(folder.glob("*.glb"))]
    for r in reports:
        _print_report(r, out)
    return EXIT_OK if all(r.ok(strict=True) for r in reports) else EXIT_ERROR


def _cmd_repair_clips(args: argparse.Namespace, out: TextIO) -> int:
    """Root speed of root motion locomotion clips = stride; lying poses above the ground."""
    characters = _characters_dir(args)
    sets = [Path(s) for s in args.sets] or sorted(
        [*characters.glob("anims/*/*.glb"), *characters.glob("monsters/*/anims/*.glb")]
    )
    for glb in sets:
        done = repair_set(glb)
        name = glb.relative_to(characters) if glb.is_relative_to(characters) else glb
        print(f"{name}: {'; '.join(done) if done else 'ok'}", file=out)
        if done:
            update_speeds(glb)  # the root speed is the natural speed of root motion clips
    return EXIT_OK


def _cmd_poke(args: argparse.Namespace, out: TextIO) -> int:
    """Skin showing through the clothes in motion (rule fit.poke_motion, F3o)."""
    characters = _characters_dir(args)
    figures = args.figures or [characters / "figures" / f"{n}.glb" for n in POKE_FIGURES]
    clips = tuple(args.clips) if args.clips else POKE_CLIPS
    ok = True
    for fig in figures:
        if not fig.is_file():
            print(f"error: {fig} not found (run gothar-chargen assemble)", file=out)
            return EXIT_ERROR
        results = check_figure(fig, characters / "anims" / "human", clips, args.samples)
        report = poke_report(fig, results)
        state = "OK" if report.ok(args.strict) else "FAILED"
        print(f"{state:<6} {fig.name}  (worst {report.stats['poke_cm2']} cm²)", file=out)
        for r in results:
            print(f"         {r.clip:<24} {r.area:6.1f} cm²  {', '.join(r.bones)}", file=out)
        for issue in report.issues:
            print(f"  {issue.level.upper():<7} {issue.code}: {issue.message}", file=out)
        ok = ok and report.ok(args.strict)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_build_items(args: argparse.Namespace, out: TextIO) -> int:
    """Weapons and hand items (F6): textures in Blender, geometry and glTF in Python."""
    root = find_repo_root()
    items_dir = args.out_dir or (root / ITEMS_DIR if root else None)
    if items_dir is None:
        print("error: repository not found; pass --out-dir", file=out)
        return EXIT_ERROR
    if not args.skip_textures:
        log = bake_item_textures(find_blender(args.blender), items_dir / "textures", args.sources)
        for line in log.splitlines():
            if line.startswith("[chargen] texture"):
                print(line[10:], file=out)
    ok = True
    for path in build_items(items_dir, args.only or None):
        report = validate_item(path)
        _print_report(report, out)
        ok = ok and report.ok(strict=True)
    return EXIT_OK if ok else EXIT_ERROR


def _cmd_fabrics(args: argparse.Namespace, out: TextIO) -> int:
    """Worn cloth textures from data/fabrics.toml (F3n; fabric sources in DATA_ROOT, Blender)."""
    characters = _characters_dir(args)
    log = bake_fabrics(find_blender(args.blender), characters, args.sources, args.only)
    for line in log.splitlines():
        if line.startswith("[chargen] baked"):
            print(line[10:], file=out)
    # frayed hems: the parts use the .png with MASK; the old texture goes, the masks follow
    for target in load_fabrics().targets:
        if target.fray <= 0 or (args.only and target.file not in args.only):
            continue
        for part in apply_fray_materials(characters, target):
            print(f"material MASK {part.relative_to(characters)} -> {target.file}", file=out)
        for old in (characters / "textures" / "cloth").glob(Path(target.file).stem + ".*"):
            if old.name != target.file:
                old.unlink()
                print(f"removed {old.relative_to(characters)}", file=out)
    print("next: gothar-chargen part-data (masks keep the body under frayed hems)", file=out)
    return EXIT_OK


def _cmd_speeds(args: argparse.Namespace, out: TextIO) -> int:
    """Natural speed of the locomotion clips into the events files (§3, no Blender)."""
    characters = _characters_dir(args)
    sets = [Path(s) for s in args.sets] or sorted(
        [*characters.glob("anims/*/*.glb"), *characters.glob("monsters/*/anims/*.glb")]
    )
    for glb in sets:
        speeds = update_speeds(glb)
        listed = ", ".join(f"{c.split('/')[-1]} {v:.2f}" for c, v in sorted(speeds.items()))
        name = glb.relative_to(characters) if glb.is_relative_to(characters) else glb
        print(f"{name}: {listed or 'no locomotion clips'}", file=out)
    return EXIT_OK


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

    p = sub.add_parser("assemble", help="figures from figures/<name>.figure.toml (no Blender)")
    p.add_argument("manifests", nargs="*", type=Path, help="default: all in figures/")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_assemble)

    p = sub.add_parser("part-data", help="assembly data of the parts: neck rings, garment masks")
    p.add_argument("dirs", nargs="*", help="part folders (default: all under parts/)")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_part_data)

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

    p = sub.add_parser("poke", help="skin showing through clothes in motion (fit.poke_motion)")
    p.add_argument("figures", nargs="*", type=Path, help="built figures (default: test NPCs)")
    p.add_argument("--clips", nargs="*", default=[], help="clip names (default: a bending set)")
    p.add_argument("--samples", type=int, default=POKE_SAMPLES, help="frames per clip")
    p.add_argument("--strict", action="store_true", help="warnings fail too")
    p.add_argument(
        "--out-dir", type=Path, help="characters folder (default: assets/source/characters)"
    )
    p.set_defaults(func=_cmd_poke)

    p = sub.add_parser("build-items", help="weapons and hand items -> assets/source/items (F6)")
    p.add_argument("--sources", type=Path, help="DATA_ROOT/characters/ambientcg/items")
    p.add_argument("--skip-textures", action="store_true", help="geometry only (no Blender)")
    p.add_argument("--only", nargs="*", default=[], help="item ids (default: all)")
    p.add_argument("--out-dir", type=Path, help="items folder (default: repository)")
    p.set_defaults(func=_cmd_build_items)

    p = sub.add_parser("fabrics", help="worn cloth textures from data/fabrics.toml (local, F3n)")
    p.add_argument(
        "--sources", type=Path, required=True, help="DATA_ROOT/characters/ambientcg/fabric"
    )
    p.add_argument("--only", nargs="*", default=[], help="texture file names (default: all)")
    p.add_argument("--out-dir", type=Path, help="characters folder (default: repository)")
    p.set_defaults(func=_cmd_fabrics)

    p = sub.add_parser("repair-clips", help="root speed = stride, lying poses above the ground")
    p.add_argument("sets", nargs="*", help="set .glb files (default: all in anims/ and monsters/)")
    p.add_argument("--out-dir", type=Path, help="characters folder (default: repository)")
    p.set_defaults(func=_cmd_repair_clips)

    p = sub.add_parser("speeds", help="natural speed of locomotion clips -> events.toml (§3)")
    p.add_argument("sets", nargs="*", help="set .glb files (default: all in anims/ and monsters/)")
    p.add_argument("--out-dir", type=Path, help="characters folder (default: repository)")
    p.set_defaults(func=_cmd_speeds)

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
        AssembleError,
        BlenderError,
        ClipSpecError,
        PartDataError,
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
