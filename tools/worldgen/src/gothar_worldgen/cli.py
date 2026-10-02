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
from gothar_worldgen.geo.bbox import BBox, tiles_covering

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_NOT_IMPLEMENTED = 2


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
        print(f"blender:   {local.blender}", file=out)
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
    tiles = tiles_covering(bbox, args.size)
    print(f"{site.name} {args.area}: {_fmt_bbox(bbox)} ({site.crs})", file=out)
    print(f"{len(tiles)} tiles of {args.size} m (label = lower-left corner in km):", file=out)
    for t in tiles:
        print(f"  {t.label}   {_fmt_bbox(t.bbox)}", file=out)
    return EXIT_OK


def _cmd_import(args: argparse.Namespace, out: TextIO) -> int:
    load_site(args.site, args.config_dir)
    print("import: not implemented yet (W1: DGM1, LoD2, OSM follow)", file=sys.stderr)
    return EXIT_NOT_IMPLEMENTED


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
    p.add_argument("--size", type=int, default=1000, help="tile edge length in metres")
    p.set_defaults(func=_cmd_tiles)

    p = sub.add_parser("import", help="convert raw geodata into intermediate world data")
    p.add_argument("site")
    p.set_defaults(func=_cmd_import)

    return parser


def main(argv: Sequence[str] | None = None, out: TextIO | None = None) -> int:
    args = build_parser().parse_args(argv)
    if getattr(args, "size", 1) <= 0:
        print("error: --size must be > 0", file=sys.stderr)
        return EXIT_ERROR
    try:
        return int(args.func(args, out or sys.stdout))
    except ConfigError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
