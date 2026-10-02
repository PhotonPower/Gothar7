import datetime as dt
import io
import zipfile
from pathlib import Path

import pytest

from gothar_worldgen.config import DataPaths, load_site
from gothar_worldgen.download import (
    LGL_PRODUCTS,
    OSM_URL,
    lgl_tile_name,
    lgl_tile_url,
    lgl_tiles,
    safe_extract,
)
from gothar_worldgen.download import download_site as _download_site
from gothar_worldgen.geo.bbox import BBox, Tile

DAY = dt.date(2026, 10, 2)


def download_site(*args, **kwargs):
    kwargs.setdefault("today", DAY)
    return _download_site(*args, **kwargs)


def _zip_bytes(members: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return buf.getvalue()


class FakeFetcher:
    """Serves an LGL-like ZIP for every tile URL and a dummy file for OSM; records calls."""

    def __init__(self) -> None:
        self.urls: list[str] = []

    def __call__(self, url: str, dest: Path) -> None:
        self.urls.append(url)
        if url == OSM_URL:
            dest.write_bytes(b"pbf")
            return
        name = url.rsplit("/", 1)[1].split(".zip", 1)[0]
        dest.write_bytes(_zip_bytes({f"{name}/data.txt": b"x", f"{name}/LICENSE.pdf": b"l"}))


def test_leonberg_needs_four_lgl_tiles():
    tiles = lgl_tiles(BBox.around(500_933, 5_405_056, 1000))
    assert [t.label for t in tiles] == ["499_5404", "501_5404", "499_5406", "501_5406"]


def test_leonberg_core_spans_two_lgl_tiles():
    core = lgl_tiles(BBox.around(500_933, 5_405_056, 350))
    assert [t.label for t in core] == ["499_5404", "501_5404"]


@pytest.mark.parametrize(
    ("key", "url"),
    [
        ("dgm1", "https://opengeodata.lgl-bw.de/data/dgm/dgm1_32_501_5404_2_bw.zip"),
        ("lod2", "https://opengeodata.lgl-bw.de/data/lod2/LoD2_32_501_5404_2_bw.zip"),
        ("dop", "https://opengeodata.lgl-bw.de/data/dop20/dop20rgb_32_501_5404_2_bw.zip"),
    ],
)
def test_lgl_tile_urls_match_portal_pattern(key: str, url: str):
    tile = Tile(501_000, 5_404_000, 2000)
    assert lgl_tile_url(LGL_PRODUCTS[key], tile) == f"{url}?customerGroup=keine-angabe"
    assert lgl_tile_name(LGL_PRODUCTS[key], tile) == url.rsplit("/", 1)[1].removesuffix(".zip")


def test_download_extracts_tiles_and_records_sources(config_dir: Path, tmp_path: Path):
    site = load_site("testsite", config_dir)
    paths = DataPaths(tmp_path / "data", "testsite")
    fetch = FakeFetcher()
    out = io.StringIO()

    results = download_site(site, paths, fetch=fetch, out=out)

    assert len(results) == 3 * 4 + 1
    assert not any(r.skipped for r in results)
    assert (paths.dgm1 / "dgm1_32_499_5404_2_bw" / "data.txt").is_file()
    assert (paths.lod2 / "LoD2_32_501_5406_2_bw" / "data.txt").is_file()
    assert (paths.dop / "dop20rgb_32_499_5406_2_bw").is_dir()
    assert (paths.osm / "stuttgart-regbez-latest.osm.pbf").read_bytes() == b"pbf"
    assert not list(paths.dgm1.glob("*.zip")), "archives are removed after extraction"
    assert not list(paths.data_root.rglob("*.part"))

    sources = (paths.data_root / "geo" / "SOURCES.md").read_text(encoding="utf-8")
    assert sources.count("| 2026-10-02 |") == 13
    assert "customerGroup" not in sources
    assert "downloaded" in out.getvalue()


def test_download_skips_present_items(config_dir: Path, tmp_path: Path):
    site = load_site("testsite", config_dir)
    paths = DataPaths(tmp_path / "data", "testsite")
    download_site(site, paths, fetch=FakeFetcher())

    again = FakeFetcher()
    results = download_site(site, paths, fetch=again)
    assert again.urls == []
    assert all(r.skipped for r in results)
    sources = (paths.data_root / "geo" / "SOURCES.md").read_text(encoding="utf-8")
    assert sources.count("| 2026-10-02 |") == 13, "skipped items are not recorded twice"


def test_download_force_and_subset(config_dir: Path, tmp_path: Path):
    site = load_site("testsite", config_dir)
    paths = DataPaths(tmp_path / "data", "testsite")
    download_site(site, paths, ["dgm1"], area="core", fetch=FakeFetcher())
    fetch = FakeFetcher()
    results = download_site(site, paths, ["dgm1"], area="core", fetch=fetch, force=True)
    assert len(fetch.urls) == 2
    assert not results[0].skipped
    assert not paths.lod2.exists()


def test_download_rejects_archive_without_expected_folder(config_dir: Path, tmp_path: Path):
    site = load_site("testsite", config_dir)
    paths = DataPaths(tmp_path / "data", "testsite")

    def fetch(url: str, dest: Path) -> None:
        dest.write_bytes(_zip_bytes({"other/data.txt": b"x"}))

    with pytest.raises(ValueError, match="expected folder"):
        download_site(site, paths, ["dgm1"], area="core", fetch=fetch)


@pytest.mark.parametrize("member", ["../evil.txt", "/abs.txt", "C:/win.txt"])
def test_safe_extract_rejects_escaping_paths(tmp_path: Path, member: str):
    archive = tmp_path / "a.zip"
    archive.write_bytes(_zip_bytes({member: b"x"}))
    with pytest.raises(ValueError, match="unsafe"):
        safe_extract(archive, tmp_path / "out")
