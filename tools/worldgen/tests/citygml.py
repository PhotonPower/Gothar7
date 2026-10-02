"""Build small synthetic CityGML 1.0 documents like the LGL LoD2 files for tests."""

from collections.abc import Sequence
from pathlib import Path

Point = tuple[float, float, float]

HEADER = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<core:CityModel xmlns:core="http://www.opengis.net/citygml/1.0" '
    'xmlns:bldg="http://www.opengis.net/citygml/building/1.0" '
    'xmlns:gml="http://www.opengis.net/gml">\n'
)


def pos_list(points: Sequence[Point]) -> str:
    ring = list(points) + [points[0]]
    coords = " ".join(f"{e} {n} {h}" for e, n, h in ring)
    return f'<gml:posList srsDimension="3">{coords}</gml:posList>'


def surface(kind: str, *polygons: Sequence[Point]) -> str:
    members = "".join(
        "<gml:surfaceMember><gml:Polygon><gml:exterior><gml:LinearRing>"
        f"{pos_list(p)}</gml:LinearRing></gml:exterior></gml:Polygon></gml:surfaceMember>"
        for p in polygons
    )
    return (
        f"<bldg:boundedBy><bldg:{kind}><bldg:lod2MultiSurface><gml:MultiSurface>{members}"
        f"</gml:MultiSurface></bldg:lod2MultiSurface></bldg:{kind}></bldg:boundedBy>"
    )


def saddle_surfaces(
    e0: float, n0: float, w: float, d: float, ground: float, eave: float, ridge: float
) -> str:
    """Box of w (east) x d (north) with a saddle roof whose ridge runs east-west."""
    e1, n1, nm = e0 + w, n0 + d, n0 + d / 2
    return (
        surface("GroundSurface", [(e0, n0, ground), (e0, n1, ground), (e1, n1, ground),
                                  (e1, n0, ground)])
        + surface("WallSurface", [(e0, n0, ground), (e1, n0, ground), (e1, n0, eave),
                                  (e0, n0, eave)])
        + surface(
            "RoofSurface",
            [(e0, n0, eave), (e1, n0, eave), (e1, nm, ridge), (e0, nm, ridge)],
            [(e0, nm, ridge), (e1, nm, ridge), (e1, n1, eave), (e0, n1, eave)],
        )
    )  # fmt: skip


def ground_surface(e0: float, n0: float, w: float, d: float, ground: float) -> str:
    e1, n1 = e0 + w, n0 + d
    return surface(
        "GroundSurface", [(e0, n0, ground), (e0, n1, ground), (e1, n1, ground), (e1, n0, ground)]
    )


def flat_roof(e0: float, n0: float, w: float, d: float, top: float) -> str:
    e1, n1 = e0 + w, n0 + d
    return surface("RoofSurface", [(e0, n0, top), (e1, n0, top), (e1, n1, top), (e0, n1, top)])


def flat_surfaces(e0: float, n0: float, w: float, d: float, ground: float, top: float) -> str:
    return ground_surface(e0, n0, w, d, ground) + flat_roof(e0, n0, w, d, top)


def building(
    gml_id: str,
    surfaces: str = "",
    *,
    roof_type: str | None = "3100",
    height: float | None = None,
    function: str = "31001_1010",
    parts: Sequence[str] = (),
) -> str:
    attrs = f"<bldg:function>{function}</bldg:function>"
    if not parts:
        if roof_type:
            attrs += f"<bldg:roofType>{roof_type}</bldg:roofType>"
        if height is not None:
            attrs += f'<bldg:measuredHeight uom="urn:adv:uom:m">{height}</bldg:measuredHeight>'
    body = "".join(f"<bldg:consistsOfBuildingPart>{p}</bldg:consistsOfBuildingPart>" for p in parts)
    return (
        f'<core:cityObjectMember><bldg:Building gml:id="{gml_id}">{attrs}{surfaces}{body}'
        "</bldg:Building></core:cityObjectMember>\n"
    )


def part(gml_id: str, surfaces: str, roof_type: str = "3100", height: float | None = None) -> str:
    h = (
        f'<bldg:measuredHeight uom="urn:adv:uom:m">{height}</bldg:measuredHeight>'
        if height is not None
        else ""
    )
    return (
        f'<bldg:BuildingPart gml:id="{gml_id}"><bldg:roofType>{roof_type}</bldg:roofType>'
        f"{h}{surfaces}</bldg:BuildingPart>"
    )


def write_citygml(path: Path, *buildings: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(HEADER + "".join(buildings) + "</core:CityModel>\n", encoding="utf-8")
    return path
