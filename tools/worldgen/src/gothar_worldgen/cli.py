"""Command line interface: ``gothar-worldgen <command> <site>``."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import TextIO

from gothar_worldgen import __version__
from gothar_worldgen.config import (
    ConfigError,
    DataPaths,
    SiteConfig,
    load_local,
    load_site,
)
from gothar_worldgen.download import ALL_SOURCES, download_site, lgl_tiles
from gothar_worldgen.geo.bbox import BBox, tiles_covering
from gothar_worldgen.geo.dgm1 import DgmError
from gothar_worldgen.geo.lod2 import Lod2Error
from gothar_worldgen.geo.osm import OsmError
from gothar_worldgen.importer import run_import
from gothar_worldgen.qa.checks import FAIL
from gothar_worldgen.qa.run import run_qa
from gothar_worldgen.qa.workdata import QaError

EXIT_OK = 0
EXIT_ERROR = 1


def _fmt_bbox(b: BBox) -> str:
    return f"E {b.min_e:.0f} - {b.max_e:.0f}, N {b.min_n:.0f} - {b.max_n:.0f}"


def _cmd_info(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    _print_site(site, out)

    try:
        local = load_local(args.config_dir)
    except ConfigError as e:
        print(f"\nlocal config: not available ({e})", file=out)
        return EXIT_OK

    paths = DataPaths(local.data_root, site.name)
    print(f"\ndata root: {local.data_root}", file=out)
    for key, path in paths.raw_inputs().items():
        state = "ok" if path.is_dir() else "MISSING"
        print(f"  {key:<5} {state:<8} {path}", file=out)
    print(f"  work  -> {paths.work}", file=out)
    if local.blender:
        state = "ok" if local.blender.is_file() else "MISSING"
        print(f"blender:   {state:<8} {local.blender}", file=out)
    return EXIT_OK


def _print_site(site: SiteConfig, out: TextIO) -> None:
    o = site.origin
    s = site.game_scale
    print(f"site:         {site.name} ({site.crs})", file=out)
    print(
        f"origin:       E {o.easting:.1f}, N {o.northing:.1f} (height: {o.height_reference})",
        file=out,
    )
    print(f"core:         {_fmt_bbox(site.bbox('core'))}", file=out)
    print(f"surroundings: {_fmt_bbox(site.bbox('surroundings'))}", file=out)
    print(
        f"game scale:   horizontal {s.horizontal}, vertical {s.vertical}, "
        f"alley widen {s.alley_widen_factor}",
        file=out,
    )


def _cmd_tiles(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    bbox = site.bbox(args.area)
    if args.size is None:
        tiles = lgl_tiles(bbox)
        grid = "LGL Open GeoData grid, 2000 m"
    else:
        tiles = tiles_covering(bbox, args.size)
        grid = f"{args.size} m grid"
    print(f"{site.name} {args.area}: {_fmt_bbox(bbox)} ({site.crs})", file=out)
    print(f"{len(tiles)} tiles, {grid} (label = lower-left corner in km):", file=out)
    for t in tiles:
        print(f"  {t.label}   {_fmt_bbox(t.bbox)}", file=out)
    return EXIT_OK


def _cmd_download(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    sources = ALL_SOURCES if args.only is None else tuple(args.only.split(","))
    unknown = [s for s in sources if s not in ALL_SOURCES]
    if unknown:
        print(
            f"error: unknown source(s) {', '.join(unknown)} (known: {', '.join(ALL_SOURCES)})",
            file=sys.stderr,
        )
        return EXIT_ERROR
    paths = DataPaths(local.data_root, site.name)
    print(
        f"downloading {', '.join(sources)} for {site.name} {args.area} -> {paths.data_root}",
        file=out,
    )
    try:
        results = download_site(site, paths, sources, area=args.area, force=args.force, out=out)
    except (OSError, ValueError) as e:
        print(f"error: download failed: {e}", file=sys.stderr)
        return EXIT_ERROR
    fresh = sum(not r.skipped for r in results)
    print(f"done: {fresh} downloaded, {len(results) - fresh} already present", file=out)
    return EXIT_OK


def _cmd_import(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    try:
        run_import(site, paths, out)
        status = run_qa(site.name, paths.work, out)
    except (DgmError, Lod2Error, OsmError, QaError, OSError) as e:
        print(f"error: import failed: {e}", file=sys.stderr)
        return EXIT_ERROR
    return EXIT_ERROR if status == FAIL else EXIT_OK


def _cmd_check(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    work = DataPaths(local.data_root, site.name).work
    print(f"checking {site.name} -> {work}", file=out)
    try:
        status = run_qa(site.name, work, out)
    except (QaError, OSError) as e:
        print(f"error: check failed: {e}", file=sys.stderr)
        return EXIT_ERROR
    return EXIT_ERROR if status == FAIL else EXIT_OK


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gothar-worldgen",
        description="Gothar world-track tools (geodata -> game world data).",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument(
        "--config-dir",
        type=Path,
        default=None,
        help="directory with <site>.toml and local.toml (default: tools/worldgen/config)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="show site configuration and raw-data directories")
    p.add_argument("site")
    p.set_defaults(func=_cmd_info)

    p = sub.add_parser("tiles", help="list the data tiles needed to cover a site area")
    p.add_argument("site")
    p.add_argument("--area", choices=("core", "surroundings"), default="surroundings")
    p.add_argument(
        "--size",
        type=int,
        default=None,
        help="plain grid with this tile size in metres (default: LGL download grid)",
    )
    p.set_defaults(func=_cmd_tiles)

    p = sub.add_parser("download", help="download LGL tiles and the OSM extract into DATA_ROOT")
    p.add_argument("site")
    p.add_argument("--area", choices=("core", "surroundings"), default="surroundings")
    p.add_argument(
        "--only", default=None, help=f"comma-separated subset of: {','.join(ALL_SOURCES)}"
    )
    p.add_argument("--force", action="store_true", help="download again even if present")
    p.set_defaults(func=_cmd_download)

    p = sub.add_parser(
        "import", help="convert raw geodata into intermediate world data, then run 'check'"
    )
    p.add_argument("site")
    p.set_defaults(func=_cmd_import)

    p = sub.add_parser("check", help="plausibility report and preview images of the work data")
    p.add_argument("site")
    p.set_defaults(func=_cmd_check)

    return parser


def main(argv: Sequence[str] | None = None, out: TextIO | None = None) -> int:
    args = build_parser().parse_args(argv)
    size = getattr(args, "size", None)
    if size is not None and size <= 0:
        print("error: --size must be > 0", file=sys.stderr)
        return EXIT_ERROR
    try:
        return int(args.func(args, out or sys.stdout))
    except ConfigError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
