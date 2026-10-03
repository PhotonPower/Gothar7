"""Command line interface: ``gothar-worldgen <command> <site>``."""

from __future__ import annotations

import argparse
import json
import sys
import webbrowser
from collections.abc import Sequence
from pathlib import Path
from typing import TextIO

from gothar_worldgen import __version__
from gothar_worldgen.assemble.world import (
    AssembleError,
    VobIds,
    assemble,
    load_world,
    write_world,
)
from gothar_worldgen.buildings.batch import generate, write_index
from gothar_worldgen.buildings.medieval import StreetIndex, load_rules
from gothar_worldgen.buildings.rueckbau import Protection, split_building
from gothar_worldgen.buildings.rueckbau import apply as apply_rueckbau
from gothar_worldgen.buildings.rueckbau import select as select_rueckbau
from gothar_worldgen.config import (
    ConfigError,
    DataPaths,
    SiteConfig,
    default_config_dir,
    load_local,
    load_site,
)
from gothar_worldgen.download import ALL_SOURCES, download_site, lgl_tiles
from gothar_worldgen.export.splat import SplatPaths, composite, coverage, layer_masks, write_splat
from gothar_worldgen.export.starts import DEFAULT_STARTS, load_starts
from gothar_worldgen.export.terrain import ExportError, Grid, crop, export_terrain, load_grid
from gothar_worldgen.export.water import carve_and_place
from gothar_worldgen.facade.capture import (
    CaptureError,
    CaptureOptions,
    process_capture,
    terrain_ground,
)
from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.frames import ToolError, find_ffmpeg
from gothar_worldgen.facade.overrides import OverrideError, load_all
from gothar_worldgen.facade.poses import DEFAULT_CAMERA_HEIGHT_M, Track, TrackError, load_gpx
from gothar_worldgen.facade.preview import load_buildings, load_equirect, preview_building
from gothar_worldgen.facade.rectify import FacadeError
from gothar_worldgen.facade.sync import parse_utc
from gothar_worldgen.facade.webui.api import ApiError, Workspace
from gothar_worldgen.facade.webui.server import UiServer
from gothar_worldgen.geo.bbox import BBox, tiles_covering
from gothar_worldgen.geo.dgm1 import DgmError
from gothar_worldgen.geo.frame import LocalFrame
from gothar_worldgen.geo.lod2 import Lod2Error
from gothar_worldgen.geo.osm import OsmError
from gothar_worldgen.handmade import (
    HandmadeError,
    build_schloss,
    find_blender,
    put_item,
    schloss_item,
    splat_areas,
)
from gothar_worldgen.handmade import footprints as handmade_footprints
from gothar_worldgen.handmade import load as load_handmade
from gothar_worldgen.handmade import save as save_handmade
from gothar_worldgen.importer import run_import
from gothar_worldgen.qa.begehung import LIMITS, Character, game_grid, load_bodies
from gothar_worldgen.qa.begehung import run as walkthrough
from gothar_worldgen.qa.checks import FAIL
from gothar_worldgen.qa.run import run_qa
from gothar_worldgen.qa.walk import evaluate, read_log, write_routes
from gothar_worldgen.qa.workdata import QaError, load_work
from gothar_worldgen.walls.citywall import (
    CourseError,
    footprints_of,
    generate_citywall,
    load_course,
    wall_context,
)
from gothar_worldgen.walls.citywall import write_index as write_citywall_index

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


def _cmd_facade_preview(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    try:
        coords = [float(v) for v in args.pose.split(",")]
    except ValueError:
        coords = []
    if len(coords) not in (2, 3):
        print("error: --pose must be x,z or x,z,y (local metres)", file=sys.stderr)
        return EXIT_ERROR
    try:
        building = load_buildings(paths.work / "buildings.json").get(args.building)
        if building is None:
            print(f"error: building {args.building} not in buildings.json", file=sys.stderr)
            return EXIT_ERROR
        image = load_equirect(args.image)
    except FacadeError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    y = coords[2] if len(coords) == 3 else building.get("groundY", 0.0) + DEFAULT_CAMERA_HEIGHT_M
    pose = CameraPose(coords[0], y, coords[1], args.heading)
    out_dir = args.out or paths.work / "facades"
    edges = [args.edge] if args.edge is not None else None
    results = preview_building(building, image, pose, out_dir, edges, args.px_per_m)
    for r in results:
        if r.view is None:
            print(f"  edge {r.edge}: skipped ({r.problem})", file=out)
        else:
            v = r.view
            print(
                f"  edge {r.edge}: {r.path.name}  {v.image.shape[1]}x{v.image.shape[0]} px, "
                f"{v.distance_m:.1f} m, {v.angle_deg:.0f}° off-axis, quality {v.quality:.2f}",
                file=out,
            )
    return EXIT_OK if any(r.view is not None for r in results) else EXIT_ERROR


def _cmd_facade_frames(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    if args.every <= 0:
        print("error: --every must be > 0", file=sys.stderr)
        return EXIT_ERROR
    try:
        start = parse_utc(args.start) if args.start else None
    except ValueError:
        print(f"error: --start '{args.start}' is not an ISO 8601 time", file=sys.stderr)
        return EXIT_ERROR
    try:
        ffmpeg = find_ffmpeg(args.ffmpeg, local.ffmpeg)
        track = Track(load_gpx(args.gpx), LocalFrame.for_site(site, 0.0))
    except (ToolError, TrackError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    ground = None
    try:
        work = load_work(paths.work)
        ground = terrain_ground(lambda x, z: float(work.sample_heights([x], [z])[0]))
    except QaError as e:
        print(f"  warning: no terrain ({e}); camera heights relative to y = 0", file=out)
    name = args.name or args.video.stem
    out_dir = paths.work / "captures" / name
    options = CaptureOptions(args.every, start, args.heading_offset, args.camera_height)
    try:
        doc = process_capture(ffmpeg, args.video, track, out_dir, options, ground, out)
    except (ToolError, CaptureError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    print(f"  {len(doc['frames'])} frames -> {out_dir / 'frames.json'}", file=out)
    return EXIT_OK


def open_workspace(args: argparse.Namespace) -> Workspace:
    """Workspace of ``facade ui``: work data in DATA_ROOT, overrides next to the config dir."""
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    data_dir = (args.config_dir or default_config_dir()).parent / "data"
    overrides = args.overrides_dir or data_dir / site.name / "buildings"
    return Workspace.open(
        site.name,
        DataPaths(local.data_root, site.name).work,
        overrides,
        data_dir / "facade_vocabulary.json",
    )


def _cmd_facade_ui(args: argparse.Namespace, out: TextIO) -> int:
    try:
        ws = open_workspace(args)
    except ApiError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    try:
        server = UiServer(ws, args.port, log=out if args.verbose else None)
    except OSError as e:
        print(f"error: cannot listen on 127.0.0.1:{args.port} ({e})", file=sys.stderr)
        return EXIT_ERROR
    print(f"  {len(ws.buildings)} buildings, {len(ws.frames)} frames", file=out)
    print(f"  overrides: {ws.overrides_dir}", file=out)
    print(f"  facade UI at {server.url}  (Ctrl+C to stop)", file=out)
    if not args.no_browser:
        webbrowser.open(server.url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return EXIT_OK


def _export_splat(
    grid: Grid,
    work: Path,
    core: dict | None,
    folder: Path,
    site: str,
    name: str,
    out: TextIO,
    gardens: Sequence[dict] = (),
) -> dict:
    docs = {}
    for file, key in (("buildings.json", "buildings"), ("streets.json", "streets"),
                      ("features.json", "features")):  # fmt: skip
        try:
            docs[key] = json.loads((work / file).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise ExportError(f"{file}: {e} (run 'import' first or pass --no-splat)") from None
    masks = layer_masks(
        grid,
        docs["buildings"].get("buildings", []),
        docs["streets"].get("streets", []),
        docs["streets"].get("squares", []),
        docs["features"].get("features", []),
        core,
        gardens,
    )
    weights = composite(masks, (grid.height, grid.width))
    block = write_splat(
        weights,
        SplatPaths(folder / "generated", f"worlds/{site}/generated", folder / "layers",
                   f"worlds/{site}/layers", name),
    )  # fmt: skip
    shares = ", ".join(f"{k} {v:.0%}" for k, v in coverage(weights).items())
    print(f"  splat: {len(block['maps'])} maps, {len(block['layers'])} layers ({shares})", file=out)
    return block


def _site_dirs(args: argparse.Namespace, site_name: str) -> tuple[Path, Path]:
    """(assets/source/worlds/<site>, tools/worldgen/data/<site>) for this checkout."""
    config = args.config_dir or default_config_dir()
    assets = getattr(args, "assets_dir", None) or config.parents[2] / "assets" / "source"
    return assets / "worlds" / site_name, config.parent / "data" / site_name


def _overrides(data_dir: Path) -> tuple[frozenset[str], frozenset[str]]:
    """(locked ids, ids with keep = false) from the site's override files."""
    all_ = load_all(data_dir / "buildings")
    return (frozenset(i for i, o in all_.items() if o.locked),
            frozenset(i for i, o in all_.items() if not o.keep))  # fmt: skip


def _cmd_buildings(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    folder, data_dir = _site_dirs(args, site.name)
    rules = streets = None
    try:
        buildings = json.loads((paths.work / "buildings.json").read_text(encoding="utf-8"))
        overrides = load_all(data_dir / "buildings")
        if args.mode == "medieval" or args.rueckbau:
            rules = load_rules(data_dir.parent / "building_rules.json")
            street_doc = json.loads((paths.work / "streets.json").read_text(encoding="utf-8"))
            feature_doc = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
            streets = StreetIndex(
                street_doc.get("streets", []), street_doc.get("squares", []),
                feature_doc.get("features", []), rules.get("assignment", "mainStreetHighways"),
                rules.get("assignment", "representativeSquares"),
            )  # fmt: skip
    except (OSError, json.JSONDecodeError, OverrideError, ValueError, KeyError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    locked = frozenset(i for i, o in overrides.items() if o.locked)
    dropped = frozenset(i for i, o in overrides.items() if not o.keep)
    try:
        grid = load_grid(paths.work)
    except ExportError as e:
        print(f"  warning: no heightmap ({e}); base heights from LoD2 only", file=out)
        grid = None
    entries = [b for b in buildings.get("buildings", []) if b.get("id") not in dropped]
    replace = report = None
    rb = rules.data.get("rueckbau") if rules else None
    if rb and rb.get("enabled") and (args.mode == "medieval" or args.rueckbau):
        features = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
        protection = Protection(features.get("features", []))
        selection = select_rueckbau(entries, rb, overrides, protection)
        entries, report = apply_rueckbau(entries, selection, rb, streets)
        protected = frozenset(selection.guarded)

        def replace(b: dict) -> list[dict]:
            return [] if b["id"] in protected else split_building(b, rb, streets).houses

    wall = None
    course_path = data_dir / "city_wall.json"
    if args.mode == "medieval" and rules is not None and grid is not None and course_path.is_file():
        try:  # wall houses (W6): the same detection as `citywall`, on the houses after rueckbau
            features = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
            course = load_course(json.loads(course_path.read_text(encoding="utf-8")),
                                 features.get("features", []))  # fmt: skip
            wall = wall_context(course, entries, grid.height_at, rules)
        except (OSError, json.JSONDecodeError, CourseError) as e:
            print(f"  warning: no wall houses ({e})", file=out)
    res = generate(entries, grid, folder / "generated" / "buildings",
                   f"worlds/{site.name}/generated/buildings", args.area, locked,
                   args.mode, rules, streets, overrides, replace if rb else None,
                   wall)  # fmt: skip
    if report is not None:
        report["overBudget"] = [{"id": i, "newHouses": n} for i, n in res.replaced]
        report["stats"]["replaced"] += len(res.replaced)
        report["stats"]["newHouses"] += sum(n for _, n in res.replaced)
        res.index["stats"]["rueckbau"] = report["stats"]
        write_index(folder / "generated" / "rueckbau_report.json", report)
    write_index(folder / "generated" / "buildings_index.json", res.index)
    st = res.index["stats"]
    per = st["trianglesPerBuilding"]
    print(f"  {args.mode}: {st['buildings']} buildings, {st['cells']} cells; {res.written} files, "
          f"{res.shared} shared, {res.kept_locked} locked kept, "
          f"{len(dropped)} dropped (keep: false)", file=out)  # fmt: skip
    print(f"  triangles: {st['triangles']} total; per building median {per['median']}, "
          f"p90 {per['p90']}, max {per['max']}", file=out)  # fmt: skip
    col = st["collision"]
    print(f"  collision: {col['hulls']} COL_HULL_ bodies, {col['decomposed']} footprints cut, "
          f"{col['fallbacks']} triangle-mesh fallbacks; per building median {col['median']}, "
          f"max {col['max']}, {col['over']} over {col['budget']}", file=out)  # fmt: skip
    if "budget" in st:
        b = st["budget"]
        print(f"  budget {b['trianglesPerBuilding']}/building: {b['over']} over; timber levels "
              f"(0 full .. 3 none, 4 also no dormers): {b['timberLevels']}", file=out)  # fmt: skip
    if "style" in st:
        sty = st["style"]
        print("  styles: " + ", ".join(f"{k} {v}" for k, v in sty["style"].items()), file=out)
        print("  patterns: " + ", ".join(f"{k} {v}" for k, v in sty["pattern"].items())
              + "; roofs: " + ", ".join(f"{k} {v}" for k, v in sty["roof"].items())
              + f"; steepened roofs {sty['roofSteepened'].get('masses', 0)}", file=out)  # fmt: skip
        if "wallHouse" in sty:
            print(f"  wall houses: {sty['wallHouse'].get('True', 0)}", file=out)
        if "chimneys" in sty:
            print("  chimneys: " + ", ".join(f"{k} {v}" for k, v in sty["chimneys"].items())
                  + "; dormers: " + ", ".join(f"{k} {v}" for k, v in sty["dormers"].items()),
                  file=out)  # fmt: skip
    if report is not None:
        r = report["stats"]
        print(f"  rueckbau: {r['replaced']} buildings replaced by {r['newHouses']} houses "
              f"({len(res.replaced)} of them over budget), {r['protected']} protected, "
              f"yards {r['yardM2']:.0f} m2 (rueckbau_report.json)", file=out)  # fmt: skip
    if st["fallbacks"]:
        print("  notes: " + ", ".join(f"{k} {v}" for k, v in st["fallbacks"].items()), file=out)
    if res.steps:
        worst = sorted(res.steps, key=lambda s: -s[1])[:5]
        listed = ", ".join(f"{i} {d} m" for i, d in worst)
        print(f"  {len(res.steps)} buildings > 0.5 m above the lowest DGM point (base lowered): "
              f"{listed}", file=out)  # fmt: skip
    return EXIT_OK


def _cmd_begehung(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    folder, data_dir = _site_dirs(args, site.name)
    assets = folder.parents[1]
    world_path = folder / f"{site.name}.g7world"
    try:
        rules = json.loads((data_dir.parent / "building_rules.json").read_text(encoding="utf-8"))
        movement = assets / "data" / "movement.toml"
        report = walkthrough(
            world_path, assets, paths.work, rules, movement, site.core_half_extent_m
        )
    except (OSError, json.JSONDecodeError, KeyError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    target = folder / "generated" / "begehung.json"
    target.write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    lanes, slopes, doors = report["lanes"], report["slopes"], report["doors"]
    print(f"  {report['bodies']} collision bodies, {lanes['samples']} way samples", file=out)
    print(f"  lanes: {lanes['runs']['blocked']} blocked (< {LIMITS['laneBlockedM']} m), "
          f"{lanes['runs']['tight']} tight (< {LIMITS['laneTightM']} m), "
          f"{lanes['runs']['throughBody']} ways through bodies {lanes['throughByOwner']}",
          file=out)  # fmt: skip
    print(f"  slots narrower than the character: {report['slots']['count']} "
          f"({report['slots']['areaM2']} m²)", file=out)  # fmt: skip
    print(f"  slopes: {slopes['runs']['tooSteep']} too steep, {slopes['runs']['steep']} steep; "
          f"steps ways {slopes['steps']['count']} ({slopes['steps']['tooSteep']} too steep)",
          file=out)  # fmt: skip
    print(f"  doors: {doors['checked']} checked, {doors['high']} high, {doors['buried']} buried "
          f"({doors['fixableByOtherEdge']} fixable by another edge)", file=out)  # fmt: skip
    bad = [c["name"] for c in report["citywall"] if not c["ok"]]
    print(f"  city wall: {'all measures fit' if not bad else ', '.join(bad)}", file=out)
    print(f"  {target}", file=out)
    return EXIT_OK


def _cmd_walk_routes(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    folder, data_dir = _site_dirs(args, site.name)
    assets = folder.parents[1]
    try:
        world = json.loads((folder / f"{site.name}.g7world").read_text(encoding="utf-8"))
        character = Character.load(assets / "data" / "movement.toml")
        grid = game_grid(world, assets)
        bodies = load_bodies(world, assets, grid, character)
        streets = json.loads((paths.work / "streets.json").read_text(encoding="utf-8"))["streets"]
        stations = json.loads((data_dir / "starts.json").read_text(encoding="utf-8"))["starts"]
        features = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
        wall_doc = json.loads((data_dir / "city_wall.json").read_text(encoding="utf-8"))
        course = load_course(wall_doc, features.get("features", []))
    except (OSError, json.JSONDecodeError, KeyError, ValueError, CourseError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    target = folder / "generated" / "walk"
    gates = [{"key": g.key, "at": list(g.at), "street": g.street} for g in course.gates]
    ring = [list(p) for p in course.ring]
    counts = write_routes(target, stations, streets, bodies, ring, gates, site.core_half_extent_m)
    for name, n in counts.items():
        print(f"  {name}: {n} points -> {target / (name + '.json')}", file=out)
    world_vfs = f"worlds/{site.name}/{site.name}.g7world"
    print(
        f"  run: gothar --world={world_vfs} --walk=<route> --walk-out=<dir> --no-render", file=out
    )
    return EXIT_OK


def _cmd_walk_report(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    folder, _ = _site_dirs(args, site.name)
    try:
        route = json.loads(args.route.read_text(encoding="utf-8"))
        events, summary = read_log(args.run)
        static_path = folder / "generated" / "begehung.json"
        static = (json.loads(static_path.read_text(encoding="utf-8"))
                  if static_path.is_file() else None)  # fmt: skip
    except (OSError, json.JSONDecodeError, KeyError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    report = evaluate(route, events, summary, static)
    target = args.run / "walk_report.json"
    target.write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"  {report['points']} points, summary: {json.dumps(summary, ensure_ascii=False)}",
          file=out)  # fmt: skip
    print(
        f"  problems {report['count']}, stuck by {report['stuckBy']}; "
        f"{report['confirmed']} confirm the static walkthrough, {report['new']} new",
        file=out,
    )
    print(f"  {target}", file=out)
    return EXIT_OK


def _cmd_citywall(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    folder, data_dir = _site_dirs(args, site.name)
    try:
        course_doc = json.loads((data_dir / "city_wall.json").read_text(encoding="utf-8"))
        features = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
        buildings = json.loads((paths.work / "buildings.json").read_text(encoding="utf-8"))
        street_doc = json.loads((paths.work / "streets.json").read_text(encoding="utf-8"))
        rules = load_rules(data_dir.parent / "building_rules.json")
        overrides = load_all(data_dir / "buildings")
        course = load_course(course_doc, features.get("features", []))
        grid = load_grid(paths.work)
    except (OSError, json.JSONDecodeError, OverrideError, ValueError, KeyError, CourseError,
            ExportError) as e:  # fmt: skip
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    # The houses as generated (after the rueckbau): the wall is left out where they stand.
    entries = [b for b in buildings.get("buildings", [])
               if b.get("id") not in {i for i, o in overrides.items() if not o.keep}]  # fmt: skip
    rb = rules.data.get("rueckbau")
    if rb and rb.get("enabled"):
        streets = StreetIndex(
            street_doc.get("streets", []), street_doc.get("squares", []),
            features.get("features", []), rules.get("assignment", "mainStreetHighways"),
            rules.get("assignment", "representativeSquares"),
        )  # fmt: skip
        selection = select_rueckbau(
            entries, rb, overrides, Protection(features.get("features", []))
        )
        entries, _ = apply_rueckbau(entries, selection, rb, streets)
    footprints = [poly for _, poly in footprints_of(entries)]
    try:  # hand-made objects (castle) stand on the line like houses
        from shapely.geometry import Polygon

        hm = load_handmade(data_dir / "handmade.json")
        footprints += [Polygon(fp) for fp in handmade_footprints(hm)]
    except (OSError, json.JSONDecodeError, HandmadeError) as e:
        print(f"  warning: handmade.json ignored ({e})", file=out)
    index = generate_citywall(course, footprints, grid.height_at, rules,
                              folder / "generated" / "citywall",
                              f"worlds/{site.name}/generated/citywall")  # fmt: skip
    write_citywall_index(folder / "generated" / "citywall_index.json", index)
    st = index["stats"]
    print(f"  ring {st['ringM']} m: wall {st['wallM']} m, {st['onHousesM']} m on houses; "
          f"Zwinger {st['zwingerM']} m", file=out)  # fmt: skip
    print(f"  {st['towers']} towers, {st['gateTowers']} gate towers, {st['pfortes']} posterns, "
          f"{st['stairs']} stairs, {st['merlons']} merlons; {st['files']} files",
          file=out)  # fmt: skip
    print(f"  triangles {st['triangles']} (budget {st['budgetTriangles']}); collision max "
          f"{st['collisionMax']} per file, {st['collisionOver']} over", file=out)  # fmt: skip
    for note in st["notes"]:
        print(f"  note: {note}", file=out)
    if st["openEnds"]:
        print(f"  warning: {len(st['openEnds'])} wall ends in the open: "
              + ", ".join(st["openEnds"][:5]), file=out)  # fmt: skip
    return EXIT_OK


def _cmd_schloss(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    folder, data_dir = _site_dirs(args, site.name)
    blender = args.blender or find_blender()
    if blender is None:
        print("error: Blender not found (set G7_BLENDER or --blender)", file=sys.stderr)
        return EXIT_ERROR
    spec_path = data_dir / "schloss.json"
    out_glb = folder / "handmade" / "schloss" / "schloss.glb"
    out_blend = folder / "generated" / "schloss" / "schloss.blend"
    try:
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        line = build_schloss(Path(blender), spec_path, data_dir.parent / "building_rules.json",
                             out_glb, out_blend)  # fmt: skip
        mesh = f"worlds/{site.name}/handmade/schloss/schloss.glb"
        doc = put_item(load_handmade(data_dir / "handmade.json"), schloss_item(spec, mesh))
        save_handmade(data_dir / "handmade.json", doc)
    except (OSError, json.JSONDecodeError, HandmadeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    print(f"  {line}", file=out)
    print(f"  {out_glb}", file=out)
    print(f"  {out_blend} (not versioned)", file=out)
    print(f"  {data_dir / 'handmade.json'}", file=out)
    return EXIT_OK


def _cmd_assemble(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    folder, data_dir = _site_dirs(args, site.name)
    name = args.name or site.name
    ids_path = data_dir / "vob_ids.json"
    try:
        terrain_world = load_world(folder / f"{site.name}_terrain.g7world")
        if terrain_world is None:
            raise AssembleError(f"{site.name}_terrain.g7world not found (run export-terrain)")
        index_path = folder / "generated" / "buildings_index.json"
        if not index_path.is_file():
            raise AssembleError("buildings_index.json not found (run 'gothar-worldgen buildings')")
        index = json.loads(index_path.read_text(encoding="utf-8"))
        ids = VobIds.load(ids_path)
        locked, _ = _overrides(data_dir)
        try:
            ground = load_grid(paths.work).height_at
        except ExportError:
            ground = None
        handmade = load_handmade(data_dir / "handmade.json")
        water_path = folder / "generated" / "water_index.json"
        water = json.loads(water_path.read_text(encoding="utf-8")) if water_path.is_file() else None
        wall_path = folder / "generated" / "citywall_index.json"
        citywall = (
            json.loads(wall_path.read_text(encoding="utf-8")) if wall_path.is_file() else None
        )
        res = assemble(terrain_world, index, load_world(folder / f"{name}.g7world"), ids, name,
                       locked, ground, citywall, handmade, water,
                       DEFAULT_STARTS + load_starts(data_dir / "starts.json"))  # fmt: skip
    except (AssembleError, OverrideError, OSError, json.JSONDecodeError, HandmadeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    write_world(folder / f"{name}.g7world", res.world)
    ids.save(ids_path)
    print(f"  {len(res.world['vobs'])} vobs: {res.added} added, {res.updated} updated, "
          f"{res.removed} removed, {res.kept_locked} locked kept, "
          f"{res.kept_editor} editor vobs kept; nextVobId {res.world['nextVobId']}",
          file=out)  # fmt: skip
    print(f"  {folder / f'{name}.g7world'}", file=out)
    print(f"  {ids_path}", file=out)
    return EXIT_OK


def _cmd_export_terrain(args: argparse.Namespace, out: TextIO) -> int:
    site = load_site(args.site, args.config_dir)
    local = load_local(args.config_dir)
    paths = DataPaths(local.data_root, site.name)
    assets = (
        args.assets_dir
        or (args.config_dir or default_config_dir()).parents[2] / "assets" / "source"
    )
    name = args.name or f"{site.name}_terrain"
    folder = assets / "worlds" / site.name
    vfs = f"worlds/{site.name}/generated/{name}.r16"
    try:
        meta = json.loads((paths.work / "terrain.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        meta = {}
    core = meta.get("areas", {}).get("core")
    try:
        grid = load_grid(paths.work)
        grid = crop(grid, core if args.area == "core" else None, args.step)
        data_dir = (args.config_dir or default_config_dir()).parent / "data" / site.name
        water_doc = data_dir / "water.json"
        # River beds and lake basins: the heightmap deliberately leaves the DGM there.
        if water_doc.is_file():
            features = json.loads((paths.work / "features.json").read_text(encoding="utf-8"))
            spec = json.loads(water_doc.read_text(encoding="utf-8"))
            grid, water = carve_and_place(grid, features.get("features", []), spec)
            write_index(folder / "generated" / "water_index.json", water)
            for kind in ("rivers", "lakes"):
                for wname, st in water["stats"][kind].items():
                    print(f"  water: {wname}: {st['boxes']} boxes, "
                          f"{st['cellsLowered']} cells lowered", file=out)  # fmt: skip
        splat = None
        if not args.no_splat:
            gardens = splat_areas(load_handmade(data_dir / "handmade.json"))
            splat = _export_splat(grid, paths.work, core, folder, site.name, name, out, gardens)
        starts = (DEFAULT_STARTS if site.name == "leonberg" else ()) + load_starts(
            data_dir / "starts.json"
        )
        r16 = folder / "generated" / f"{name}.r16"
        result = export_terrain(grid, name, folder / f"{name}.g7world", r16, vfs, splat, starts)
    except ExportError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    g = result.grid
    print(f"  {g.width} x {g.height} samples, {g.cell:g} m, x {g.first_x:g} .. "
          f"{g.first_x + (g.width - 1) * g.cell:g}, z {g.first_z:g} .. "
          f"{g.first_z + (g.height - 1) * g.cell:g}", file=out)  # fmt: skip
    print(
        f"  heights {result.min_y:g} .. {result.max_y:g} m, step {result.step_mm:.2f} mm", file=out
    )
    print(f"  {result.world_path}", file=out)
    print(f"  {result.heightmap_path}  (VFS {vfs}, not versioned)", file=out)
    return EXIT_OK


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

    p = sub.add_parser(
        "export-terrain", help="heightmap -> .g7world with terrain block (W2, see world.md)"
    )
    p.add_argument("site")
    p.add_argument("--area", choices=("surroundings", "core"), default="surroundings")
    p.add_argument("--step", type=int, default=1, help="keep every n-th sample (default 1)")
    p.add_argument("--name", default=None, help="world name (default: <site>_terrain)")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.add_argument("--no-splat", action="store_true", help="heightmap only, no splat layers")
    p.set_defaults(func=_cmd_export_terrain)

    p = sub.add_parser("buildings", help="grey building masses as .glb (W3) + index")
    p.add_argument("site")
    p.add_argument("--area", choices=("core", "all"), default="core",
                   help="core: old town, a file per building; all: plus 64 m cells")  # fmt: skip
    p.add_argument("--rueckbau", action="store_true",
                   help="replace large buildings also in massing mode (preview)")  # fmt: skip
    p.add_argument("--mode", choices=("massing", "medieval"), default="medieval",
                   help="massing: grey blocks; medieval: half-timbering (W5 draft)")  # fmt: skip
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_buildings)

    p = sub.add_parser("citywall", help="city wall with towers and gates as .glb (W6) + index")
    p.add_argument("site")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_citywall)

    p = sub.add_parser("begehung", help="static walkthrough of the assembled world (W3)")
    p.add_argument("site")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_begehung)

    p = sub.add_parser("walk-routes", help="routes for gothar --walk (W3 walkthrough part 2)")
    p.add_argument("site")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_walk_routes)

    p = sub.add_parser("walk-report", help="evaluate a gothar --walk run against the static walk")
    p.add_argument("site")
    p.add_argument("--route", type=Path, required=True, help="the route.json that was run")
    p.add_argument("--run", type=Path, required=True, help="the --walk-out folder")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_walk_report)

    p = sub.add_parser("schloss", help="castle model from the Blender script (W6) + handmade.json")
    p.add_argument("site")
    p.add_argument("--blender", type=Path, default=None, help="default: G7_BLENDER, PATH, install")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_schloss)

    p = sub.add_parser("assemble", help="terrain + buildings -> <site>.g7world with stable VobIds")
    p.add_argument("site")
    p.add_argument("--name", default=None, help="world name (default: <site>)")
    p.add_argument("--assets-dir", type=Path, default=None, help="default: <repo>/assets/source")
    p.set_defaults(func=_cmd_assemble)

    facade = sub.add_parser("facade", help="facade reference tool (W4)")
    fsub = facade.add_subparsers(dest="facade_command", required=True)
    p = fsub.add_parser("preview", help="rectified facade views of one building from a 360° image")
    p.add_argument("site")
    p.add_argument("building", help="building id from buildings.json")
    p.add_argument("--image", type=Path, required=True, help="equirectangular 360° image (2:1)")
    p.add_argument("--pose", required=True, help="camera position x,z[,y] in local metres")
    p.add_argument("--heading", type=float, default=0.0, help="compass heading of the image centre")
    p.add_argument("--edge", type=int, default=None, help="only this footprint edge")
    p.add_argument("--px-per-m", type=float, default=50.0)
    p.add_argument("--out", type=Path, default=None, help="default: <work>/<site>/facades")
    p.set_defaults(func=_cmd_facade_preview)

    p = fsub.add_parser(
        "frames", help="frames + camera poses from an exported 360° video and its GPX track"
    )
    p.add_argument("site")
    p.add_argument("video", type=Path, help="equirectangular MP4 exported from Insta360 Studio")
    p.add_argument("--gpx", type=Path, required=True, help="GPS track of the recording")
    p.add_argument("--every", type=float, default=2.0, help="seconds between frames (default 2)")
    p.add_argument("--start", default=None, help="UTC time of video second 0 (skips auto sync)")
    p.add_argument("--heading-offset", type=float, default=0.0,
                   help="image centre vs. walking direction, degrees clockwise")  # fmt: skip
    p.add_argument("--camera-height", type=float, default=DEFAULT_CAMERA_HEIGHT_M)
    p.add_argument("--name", default=None, help="capture name (default: video file name)")
    p.add_argument("--ffmpeg", type=Path, default=None, help="path to ffmpeg")
    p.set_defaults(func=_cmd_facade_frames)

    p = fsub.add_parser("ui", help="local web UI for annotating facades (overrides)")
    p.add_argument("site")
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--no-browser", action="store_true", help="do not open a browser")
    p.add_argument("--overrides-dir", type=Path, default=None,
                   help="default: tools/worldgen/data/<site>/buildings")  # fmt: skip
    p.add_argument("--verbose", action="store_true", help="log every request")
    p.set_defaults(func=_cmd_facade_ui)

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
