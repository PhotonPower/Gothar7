"""``gothar-worldgen import <site>``: raw geodata -> intermediate data in ``work/<site>/``."""

from __future__ import annotations

from typing import Any, TextIO

from gothar_worldgen.config import DataPaths, SiteConfig
from gothar_worldgen.geo.dgm1 import build_height_grid
from gothar_worldgen.geo.terrain import write_terrain

# Steps of the W1 import that are not implemented yet (reported, not an error).
PENDING_STEPS = ("buildings (LoD2)", "streets/features (OSM)", "preview")


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


def run_import(site: SiteConfig, paths: DataPaths, out: TextIO) -> None:
    print(f"importing {site.name} -> {paths.work}", file=out)
    import_terrain(site, paths, out)
    for step in PENDING_STEPS:
        print(f"  {step}: not implemented yet", file=out)
