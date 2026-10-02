"""``gothar-worldgen import <site>``: raw geodata -> intermediate data in ``work/<site>/``."""

from __future__ import annotations

from typing import Any, TextIO

import shapely

from gothar_worldgen.config import DataPaths, SiteConfig
from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.buildings import (
    buildings_document,
    select_and_convert,
    summarize,
    write_buildings_json,
)
from gothar_worldgen.geo.dgm1 import build_height_grid
from gothar_worldgen.geo.features import build_features, summarize_features
from gothar_worldgen.geo.frame import LocalFrame
from gothar_worldgen.geo.jsonio import write_json_records
from gothar_worldgen.geo.lod2 import files_for_area, read_lod2
from gothar_worldgen.geo.osm import find_osm_file, read_osm
from gothar_worldgen.geo.streets import build_streets, summarize_streets
from gothar_worldgen.geo.terrain import write_terrain

OSM_CREDIT = "© OpenStreetMap-Mitwirkende"
OSM_LICENSE = "ODbL 1.0"


def _box(b: BBox) -> shapely.Geometry:
    return shapely.box(b.min_e, b.min_n, b.max_e, b.max_n)


def import_terrain(site: SiteConfig, paths: DataPaths, out: TextIO) -> dict[str, Any]:
    """DGM1 tiles -> ``terrain.r16`` / ``terrain.png`` / ``terrain.json``."""
    grid = build_height_grid(paths.dgm1, site.bbox("surroundings"))
    meta = write_terrain(grid, site, paths.work)
    hr = meta["heightRange"]
    o = meta["origin"]
    print(
        f"  terrain: {meta['width']} x {meta['height']} samples, "
        f"{meta['cellSize']} m cells, {len(grid.files)} DGM1 files",
        file=out,
    )
    print(
        f"           origin height {o['heightNHN']:.2f} m NHN, local y {hr['minY']:.2f} .. "
        f"{hr['maxY']:.2f} m (step {hr['stepM'] * 1000:.2f} mm)",
        file=out,
    )
    return meta


def import_buildings(
    site: SiteConfig, paths: DataPaths, origin_nhn: float, out: TextIO
) -> dict[str, Any]:
    """LoD2 tiles -> ``buildings.json`` (all buildings of the surroundings, flagged inCore)."""
    files = files_for_area(paths.lod2, site.bbox("surroundings"))
    frame = LocalFrame.for_site(site, origin_nhn)
    entries = select_and_convert(
        (b for f in files for b in read_lod2(f)),
        frame,
        area=_box(site.bbox("surroundings")),
        core=_box(site.bbox("core")),
    )
    doc = buildings_document(entries, site, frame, [f.name for f in files])
    write_buildings_json(paths.work / "buildings.json", doc)
    stats = summarize(entries)
    roofs = ", ".join(f"{k} {v}" for k, v in list(stats["roofTypes"].items())[:6])
    print(
        f"  buildings: {stats['buildings']} ({stats['inCore']} in core, "
        f"{stats['withParts']} with parts, {stats['withWarnings']} with warnings)",
        file=out,
    )
    print(f"           roofs: {roofs}", file=out)
    return doc


def import_osm(site: SiteConfig, paths: DataPaths, origin_nhn: float, out: TextIO) -> None:
    """OSM extract -> ``streets.json`` (streets, squares) and ``features.json``."""
    extract = read_osm(find_osm_file(paths.osm), site.bbox("surroundings"), site.crs)
    frame = LocalFrame.for_site(site, origin_nhn)

    def header(fmt: str) -> dict[str, Any]:
        return {
            "format": fmt,
            "version": 1,
            "origin": frame.origin_json(site.crs),
            "gameScale": frame.scale_json(),
            "source": {
                "product": "OpenStreetMap",
                "file": extract.source_file,
                "timestamp": extract.timestamp,
                "credit": OSM_CREDIT,
                "license": OSM_LICENSE,
            },
        }

    layers = build_streets(extract.objects, frame)
    write_json_records(paths.work / "streets.json", header("gothar-streets"), layers)
    features = build_features(extract.objects, frame)
    write_json_records(
        paths.work / "features.json", header("gothar-features"), {"features": features}
    )

    s = summarize_streets(layers)
    classes = ", ".join(f"{k} {v}" for k, v in s["classes"].items())
    print(
        f"  streets: {s['streets']} lines ({classes}), {s['squares']} squares, "
        f"{s['widthFromTags']} widths from tags (OSM {extract.timestamp or 'unknown date'})",
        file=out,
    )
    types = ", ".join(f"{k} {v}" for k, v in summarize_features(features).items())
    print(f"  features: {len(features)} ({types})", file=out)


def run_import(site: SiteConfig, paths: DataPaths, out: TextIO) -> None:
    print(f"importing {site.name} -> {paths.work}", file=out)
    terrain = import_terrain(site, paths, out)
    origin_nhn = terrain["origin"]["heightNHN"]
    import_buildings(site, paths, origin_nhn, out)
    import_osm(site, paths, origin_nhn, out)
