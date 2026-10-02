"""Downloading raw geodata (LGL Open GeoData tiles, OSM extract) into ``DATA_ROOT``.

LGL tile layout (as served by https://opengeodata.lgl-bw.de, checked 2026-10):
2 km x 2 km ZIP archives named ``<prefix>_32_<E km>_<N km>_2_bw.zip``; the grid starts at odd
easting km and even northing km. Each archive contains one folder of the same name.
"""

from __future__ import annotations

import datetime as dt
import shutil
import urllib.request
import zipfile
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import TextIO

from gothar_worldgen.config import AreaName, DataPaths, SiteConfig
from gothar_worldgen.geo.bbox import BBox, Tile, tiles_covering

LGL_BASE_URL = "https://opengeodata.lgl-bw.de/data"
# Optional usage statistic the portal appends to downloads; this is the portal's default.
LGL_CUSTOMER_GROUP = "keine-angabe"
LGL_TILE_SIZE_M = 2000
LGL_GRID_ORIGIN_E = 1000
LGL_GRID_ORIGIN_N = 0

OSM_URL = "https://download.geofabrik.de/europe/germany/baden-wuerttemberg/stuttgart-regbez-latest.osm.pbf"

USER_AGENT = "gothar-worldgen (+https://github.com/PhotonPower/Gothar7)"
TIMEOUT_S = 120

# Downloads ``url`` to the file ``dest``.
Fetcher = Callable[[str, Path], None]


@dataclass(frozen=True)
class LglProduct:
    key: str  # attribute name in DataPaths
    url_dir: str
    file_prefix: str
    title: str


LGL_PRODUCTS: dict[str, LglProduct] = {
    "dgm1": LglProduct("dgm1", "dgm", "dgm1", "LGL DGM1"),
    "lod2": LglProduct("lod2", "lod2", "LoD2", "LGL LoD2"),
    "dop": LglProduct("dop", "dop20", "dop20rgb", "LGL DOP20 RGB"),
}
ALL_SOURCES = (*LGL_PRODUCTS, "osm")


def lgl_tiles(bbox: BBox) -> list[Tile]:
    return tiles_covering(bbox, LGL_TILE_SIZE_M, LGL_GRID_ORIGIN_E, LGL_GRID_ORIGIN_N)


def lgl_tile_name(product: LglProduct, tile: Tile) -> str:
    return f"{product.file_prefix}_32_{tile.label}_2_bw"


def lgl_tile_url(product: LglProduct, tile: Tile) -> str:
    return (
        f"{LGL_BASE_URL}/{product.url_dir}/{lgl_tile_name(product, tile)}.zip"
        f"?customerGroup={LGL_CUSTOMER_GROUP}"
    )


def http_fetch(url: str, dest: Path) -> None:
    """Stream ``url`` into ``dest`` via a temporary ``.part`` file (no partial results)."""
    part = dest.with_name(dest.name + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response, part.open("wb") as f:
            shutil.copyfileobj(response, f, length=1 << 20)
        part.replace(dest)
    finally:
        part.unlink(missing_ok=True)


def safe_extract(archive: Path, target_dir: Path) -> None:
    """Extract a ZIP, refusing members that would land outside ``target_dir``."""
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            p = PurePosixPath(name)
            if p.is_absolute() or ".." in p.parts or ":" in name:
                raise ValueError(f"{archive.name}: unsafe member path '{name}'")
        z.extractall(target_dir)


@dataclass(frozen=True)
class DownloadResult:
    source: str  # product key or "osm"
    item: str  # tile name or file name
    url: str
    skipped: bool


def _download_lgl_tile(
    product: LglProduct, tile: Tile, target_dir: Path, fetch: Fetcher, force: bool
) -> DownloadResult:
    name = lgl_tile_name(product, tile)
    url = lgl_tile_url(product, tile)
    extracted = target_dir / name
    if extracted.is_dir() and not force:
        return DownloadResult(product.key, name, url, skipped=True)

    target_dir.mkdir(parents=True, exist_ok=True)
    archive = target_dir / f"{name}.zip"
    fetch(url, archive)
    try:
        if extracted.is_dir():
            shutil.rmtree(extracted)
        safe_extract(archive, target_dir)
    finally:
        archive.unlink(missing_ok=True)
    if not extracted.is_dir():
        raise ValueError(f"{name}.zip did not contain the expected folder '{name}'")
    return DownloadResult(product.key, name, url, skipped=False)


def _download_osm(osm_dir: Path, fetch: Fetcher, force: bool) -> DownloadResult:
    file_name = OSM_URL.rsplit("/", 1)[1]
    dest = osm_dir / file_name
    if dest.is_file() and not force:
        return DownloadResult("osm", file_name, OSM_URL, skipped=True)
    osm_dir.mkdir(parents=True, exist_ok=True)
    fetch(OSM_URL, dest)
    return DownloadResult("osm", file_name, OSM_URL, skipped=False)


SOURCES_HEADER = (
    "# Herkunft der Rohdaten\n\n"
    "Automatisch ergänzt von `gothar-worldgen download`.\n\n"
    "| Datum | Quelle | Datei / Kachel | URL |\n"
    "|---|---|---|---|\n"
)


def record_sources(sources_md: Path, results: Iterable[DownloadResult], date: dt.date) -> None:
    """Append freshly downloaded items to ``SOURCES.md`` (created with a header if missing)."""
    rows = [
        f"| {date.isoformat()} | {r.source} | {r.item} | {r.url.split('?', 1)[0]} |\n"
        for r in results
        if not r.skipped
    ]
    if not rows:
        return
    sources_md.parent.mkdir(parents=True, exist_ok=True)
    if not sources_md.is_file():
        sources_md.write_text(SOURCES_HEADER, encoding="utf-8")
    with sources_md.open("a", encoding="utf-8") as f:
        f.writelines(rows)


def download_site(
    site: SiteConfig,
    paths: DataPaths,
    sources: Iterable[str] = ALL_SOURCES,
    *,
    area: AreaName = "surroundings",
    fetch: Fetcher = http_fetch,
    force: bool = False,
    out: TextIO | None = None,
    today: dt.date | None = None,
) -> list[DownloadResult]:
    """Download all LGL tiles covering ``area`` and the OSM extract; skip what is present."""
    raw_dirs = paths.raw_inputs()
    results: list[DownloadResult] = []
    tiles = lgl_tiles(site.bbox(area))
    for key in sources:
        if key == "osm":
            results.append(_download_osm(paths.osm, fetch, force))
            _report(results[-1], out)
            continue
        product = LGL_PRODUCTS[key]
        for tile in tiles:
            results.append(_download_lgl_tile(product, tile, raw_dirs[key], fetch, force))
            _report(results[-1], out)
    record_sources(paths.data_root / "geo" / "SOURCES.md", results, today or dt.date.today())
    return results


def _report(result: DownloadResult, out: TextIO | None) -> None:
    if out is not None:
        state = "present, skipped" if result.skipped else "downloaded"
        print(f"  {result.source:<5} {result.item}: {state}", file=out, flush=True)
