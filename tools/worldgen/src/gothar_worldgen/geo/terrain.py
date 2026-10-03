"""Height grid -> engine heightmap (``terrain.r16``, ``terrain.png``, ``terrain.json``).

Local engine system (docs/design/leonberg-pipeline.md, section 3): metres, origin at the site
origin, +X = east, +Y = up, -Z = north, ``y = (NHN - origin height) * vertical scale``.
Image layout: row 0 = north (-Z), column 0 = west (-X).
"""

from __future__ import annotations

import json
import math
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from gothar_worldgen.config import SiteConfig
from gothar_worldgen.geo.bbox import BBox
from gothar_worldgen.geo.dgm1 import HeightGrid
from gothar_worldgen.geo.frame import LocalFrame

FORMAT_NAME = "gothar-terrain"
FORMAT_VERSION = 1
U16_MAX = 65535
LGL_CREDIT = "Datengrundlage: LGL, www.lgl-bw.de"


@dataclass(frozen=True)
class Heightmap:
    values: npt.NDArray[np.uint16]  # (rows, cols), row 0 = north
    min_y: float  # local y of value 0
    max_y: float  # local y of value 65535
    origin_nhn: float

    @property
    def step_m(self) -> float:
        """Height resolution of one uint16 step."""
        return (self.max_y - self.min_y) / U16_MAX

    def decode(self) -> npt.NDArray[np.float64]:
        """Local y per sample (inverse of the encoding)."""
        return self.min_y + self.values.astype(np.float64) * self.step_m


def encode_heightmap(grid: HeightGrid, site: SiteConfig) -> Heightmap:
    """Convert NHN heights to local y and quantize to uint16 over the occurring range.

    Origin height and range are rounded to millimetres before encoding, so the values in
    ``terrain.json`` are exactly the ones the encoding used.
    """
    origin_nhn = round(grid.sample(site.origin.easting, site.origin.northing), 3)
    y = (grid.heights.astype(np.float64) - origin_nhn) * site.game_scale.vertical
    values, min_y, max_y = quantize_heights(y)
    return Heightmap(values, min_y, max_y, origin_nhn)


def quantize_heights(
    y: npt.NDArray[np.float64],
) -> tuple[npt.NDArray[np.uint16], float, float]:
    """uint16 over the occurring range, rounded outwards to millimetres (the .r16 encoding)."""
    # The tolerance keeps already rounded ranges (re-encoding decoded heights) unchanged.
    min_y = math.floor(float(y.min()) * 1000 + 1e-6) / 1000
    max_y = math.ceil(float(y.max()) * 1000 - 1e-6) / 1000
    if max_y - min_y < 1e-3:  # flat terrain: any non-zero span works
        max_y = min_y + 1.0
    values = np.rint((y - min_y) / (max_y - min_y) * U16_MAX).astype(np.uint16)
    return values, min_y, max_y


def _local_rect(frame: LocalFrame, bbox: BBox) -> dict[str, float]:
    min_x, max_z = frame.xz(bbox.min_e, bbox.min_n)
    max_x, min_z = frame.xz(bbox.max_e, bbox.max_n)
    return {"minX": min_x, "minZ": min_z, "maxX": max_x, "maxZ": max_z}


def write_png16(path: Path, values: npt.NDArray[np.uint16]) -> None:
    """Write a 16-bit greyscale PNG (no external dependencies)."""
    rows, cols = values.shape
    raw = values.astype(">u2")  # PNG stores samples big-endian
    scanlines = np.zeros((rows, 1 + cols * 2), dtype=np.uint8)  # filter byte 0 per row
    scanlines[:, 1:] = raw.view(np.uint8).reshape(rows, cols * 2)

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", cols, rows, 16, 0, 0, 0, 0)  # 16 bit, greyscale
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(scanlines.tobytes(), 6))
        + chunk(b"IEND", b"")
    )


def terrain_metadata(grid: HeightGrid, hm: Heightmap, site: SiteConfig) -> dict[str, Any]:
    frame = LocalFrame.for_site(site, hm.origin_nhn)
    first_x, first_z = frame.xz(grid.first_e, grid.first_n)
    return {
        "format": FORMAT_NAME,
        "version": FORMAT_VERSION,
        "width": grid.cols,
        "height": grid.rows,
        "cellSize": grid.cell_size * site.game_scale.horizontal,
        "firstSample": {"x": first_x, "z": first_z},
        "heightRange": {"minY": hm.min_y, "maxY": hm.max_y, "stepM": round(hm.step_m, 9)},
        "encoding": (
            "uint16, row-major, row 0 = north (-Z), column 0 = west (-X); "
            "terrain.r16 little-endian, terrain.png 16-bit greyscale; "
            "y = minY + value / 65535 * (maxY - minY)"
        ),
        "areas": {
            "core": _local_rect(frame, site.bbox("core")),
            "surroundings": _local_rect(frame, site.bbox("surroundings")),
        },
        "origin": {
            "crs": site.crs,
            "easting": site.origin.easting,
            "northing": site.origin.northing,
            "heightNHN": hm.origin_nhn,
            "heightReference": site.origin.height_reference,
        },
        "gameScale": {
            "horizontal": site.game_scale.horizontal,
            "vertical": site.game_scale.vertical,
        },
        "source": {
            "product": "LGL DGM1",
            "heightDatum": "DHHN2016",
            "files": [
                {"name": f.path.name, "year": f.year}
                for f in sorted(grid.files, key=lambda f: f.path.name)
            ],
            "credit": LGL_CREDIT,
        },
    }


def write_terrain(grid: HeightGrid, site: SiteConfig, out_dir: Path) -> dict[str, Any]:
    """Write ``terrain.r16``, ``terrain.png`` and ``terrain.json``; returns the metadata."""
    hm = encode_heightmap(grid, site)
    meta = terrain_metadata(grid, hm, site)
    out_dir.mkdir(parents=True, exist_ok=True)
    hm.values.astype("<u2").tofile(out_dir / "terrain.r16")
    write_png16(out_dir / "terrain.png", hm.values)
    (out_dir / "terrain.json").write_text(
        json.dumps(meta, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n"
    )
    return meta
