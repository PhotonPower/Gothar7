"""``gothar-worldgen import <site>``: raw geodata -> intermediate data in ``work/<site>/``."""

from __future__ import annotations

from typing import Any, TextIO

import shapely

from gothar_worldgen.config import DataPaths, SiteConfig
from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.buildings import (
    LocalFrame,
    buildings_document,
    select_and_convert,
    summarize,
    write_buildings_json,
)
from gothar_worldgen.geo.dgm1 import build_height_grid
from gothar_worldgen.geo.lod2 import files_for_area, read_lod2
from gothar_worldgen.geo.terrain import write_terrain

# Steps of the W1 import that are not implemented yet (reported, not an error).
PENDING_STEPS = ("streets/features (OSM)", "preview")


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


def run_import(site: SiteConfig, paths: DataPaths, out: TextIO) -> None:
    print(f"importing {site.name} -> {paths.work}", file=out)
    terrain = import_terrain(site, paths, out)
    import_buildings(site, paths, terrain["origin"]["heightNHN"], out)
    for step in PENDING_STEPS:
        print(f"  {step}: not implemented yet", file=out)
