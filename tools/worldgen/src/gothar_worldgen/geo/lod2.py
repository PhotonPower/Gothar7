"""LGL LoD2 (CityGML 1.0) -> buildings with their thematic surfaces, in source coordinates.

An LGL LoD2 file covers 1 km x 1 km and is named ``LoD2_32_<E km>_<N km>_1_BW.gml``.
A ``bldg:Building`` either carries roof type, height and surfaces itself, or consists of
``bldg:BuildingPart`` elements that do (the parent then only has the function code).
"""

from __future__ import annotations

import re
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt
from lxml import etree

from gothar_worldgen.geo.bbox import BBox, Tile, tiles_covering

FILE_TILE_SIZE_M = 1000

NS = {
    "bldg": "http://www.opengis.net/citygml/building/1.0",
    "core": "http://www.opengis.net/citygml/1.0",
    "gml": "http://www.opengis.net/gml",
}
_BUILDING_TAG = f"{{{NS['bldg']}}}Building"
_GML_ID = f"{{{NS['gml']}}}id"
_SURFACE_KINDS = {
    f"{{{NS['bldg']}}}RoofSurface": "roof",
    f"{{{NS['bldg']}}}WallSurface": "wall",
    f"{{{NS['bldg']}}}GroundSurface": "ground",
    f"{{{NS['bldg']}}}ClosureSurface": "closure",
}
_NAME_RE = re.compile(r"^LoD2_32_(\d+)_(\d+)_1_BW\.gml$", re.IGNORECASE)

Ring = npt.NDArray[np.float64]  # (n, 3): easting, northing, height NHN; closed (first == last)


class Lod2Error(Exception):
    """Missing or broken LoD2 data; the message is meant for the user."""


@dataclass(frozen=True)
class Polygon3:
    exterior: Ring
    interiors: tuple[Ring, ...] = ()


@dataclass(frozen=True)
class Surface:
    kind: str  # "roof", "wall", "ground" or "closure"
    polygons: tuple[Polygon3, ...]


@dataclass(frozen=True)
class Body:
    """A building without parts, or one building part: roof type, height and surfaces."""

    id: str
    roof_type: str | None
    measured_height: float | None
    surfaces: tuple[Surface, ...]

    def polygons(self, kind: str) -> list[Polygon3]:
        return [p for s in self.surfaces if s.kind == kind for p in s.polygons]


@dataclass(frozen=True)
class Lod2Building:
    id: str
    function: str | None
    bodies: tuple[Body, ...]
    has_parts: bool
    source_file: str


def find_lod2_files(lod2_dir: Path) -> dict[Tile, Path]:
    """All LoD2 GML files below ``lod2_dir`` by 1 km tile."""
    found: dict[Tile, Path] = {}
    for path in sorted(lod2_dir.rglob("*.gml")):
        m = _NAME_RE.match(path.name)
        if m:
            found[Tile(int(m[1]) * 1000, int(m[2]) * 1000, FILE_TILE_SIZE_M)] = path
    return found


def files_for_area(lod2_dir: Path, bbox: BBox) -> list[Path]:
    available = find_lod2_files(lod2_dir)
    needed = tiles_covering(bbox, FILE_TILE_SIZE_M)
    missing = [t.label for t in needed if t not in available]
    if missing:
        raise Lod2Error(
            f"LoD2 tiles missing in {lod2_dir}: {', '.join(missing)} "
            "(run 'gothar-worldgen download <site> --only lod2')"
        )
    return [available[t] for t in needed]


def _ring(pos_list: etree._Element) -> Ring:
    dim = int(pos_list.get("srsDimension", "3"))
    if dim != 3:
        raise Lod2Error(f"unsupported srsDimension {dim} (line {pos_list.sourceline})")
    values = np.array((pos_list.text or "").split(), dtype=np.float64)
    if values.size < 9 or values.size % 3:
        raise Lod2Error(f"broken gml:posList (line {pos_list.sourceline})")
    return values.reshape(-1, 3)


def _polygons(surface: etree._Element) -> tuple[Polygon3, ...]:
    result = []
    for poly in surface.iterfind(".//gml:Polygon", NS):
        ext = poly.find("gml:exterior//gml:posList", NS)
        if ext is None:
            continue
        interiors = tuple(_ring(p) for p in poly.iterfind("gml:interior//gml:posList", NS))
        result.append(Polygon3(_ring(ext), interiors))
    return tuple(result)


def _float(elem: etree._Element, path: str) -> float | None:
    text = elem.findtext(path, namespaces=NS)
    return float(text) if text else None


def _body(elem: etree._Element) -> Body:
    surfaces = []
    for bounded in elem.iterfind("bldg:boundedBy", NS):
        for surface in bounded:
            kind = _SURFACE_KINDS.get(surface.tag)
            if kind:
                surfaces.append(Surface(kind, _polygons(surface)))
    return Body(
        id=elem.get(_GML_ID, ""),
        roof_type=elem.findtext("bldg:roofType", namespaces=NS),
        measured_height=_float(elem, "bldg:measuredHeight"),
        surfaces=tuple(surfaces),
    )


def _building(elem: etree._Element, source_file: str) -> Lod2Building:
    parts = elem.findall("bldg:consistsOfBuildingPart/bldg:BuildingPart", NS)
    bodies = tuple(_body(p) for p in parts) if parts else (_body(elem),)
    return Lod2Building(
        id=elem.get(_GML_ID, ""),
        function=elem.findtext("bldg:function", namespaces=NS),
        bodies=bodies,
        has_parts=bool(parts),
        source_file=source_file,
    )


def read_lod2(path: Path) -> Iterator[Lod2Building]:
    """Stream all buildings of a CityGML file (memory stays small for large files)."""
    try:
        for _, elem in etree.iterparse(str(path), events=("end",), tag=_BUILDING_TAG):
            yield _building(elem, path.name)
            elem.clear(keep_tail=True)
            parent = elem.getparent()
            if parent is not None:  # drop the finished cityObjectMember from its parent
                while parent.getprevious() is not None:
                    del parent.getparent()[0]
    except etree.XMLSyntaxError as e:
        raise Lod2Error(f"{path.name}: invalid XML: {e}") from None
