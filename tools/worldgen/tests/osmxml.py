"""Build small OSM XML files for tests; node positions are given in EPSG:25832 metres."""

from collections.abc import Sequence
from pathlib import Path
from xml.sax.saxutils import quoteattr

from pyproj import Transformer

_TO_WGS84 = Transformer.from_crs("EPSG:25832", "EPSG:4326", always_xy=True)


class OsmXml:
    def __init__(self) -> None:
        self._nodes: list[str] = []
        self._ways: list[str] = []
        self._relations: list[str] = []
        self._next_node = 1

    @staticmethod
    def _tags(tags: dict[str, str]) -> str:
        return "".join(f"<tag k={quoteattr(k)} v={quoteattr(v)}/>" for k, v in tags.items())

    def node(self, e: float, n: float, tags: dict[str, str] | None = None) -> int:
        nid = self._next_node
        self._next_node += 1
        lon, lat = _TO_WGS84.transform(e, n)
        self._nodes.append(
            f'<node id="{nid}" version="1" lat="{lat:.9f}" lon="{lon:.9f}">'
            f"{self._tags(tags or {})}</node>"
        )
        return nid

    def way(
        self, wid: int, points: Sequence[tuple[float, float]], tags: dict[str, str], closed=False
    ) -> list[int]:
        refs = [self.node(e, n) for e, n in points]
        if closed:
            refs.append(refs[0])
        nds = "".join(f'<nd ref="{r}"/>' for r in refs)
        self._ways.append(f'<way id="{wid}" version="1">{nds}{self._tags(tags)}</way>')
        return refs

    def multipolygon(
        self, rid: int, outer: tuple[int, Sequence], inner: tuple[int, Sequence], tags: dict
    ) -> None:
        """Relation with one outer and one inner ring, each given as (way id, points)."""
        self.way(outer[0], outer[1], {}, closed=True)
        self.way(inner[0], inner[1], {}, closed=True)
        members = (
            f'<member type="way" ref="{outer[0]}" role="outer"/>'
            f'<member type="way" ref="{inner[0]}" role="inner"/>'
        )
        tags = {"type": "multipolygon", **tags}
        self._relations.append(
            f'<relation id="{rid}" version="1">{members}{self._tags(tags)}</relation>'
        )

    def write(self, path: Path) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        body = "\n".join(self._nodes + self._ways + self._relations)
        path.write_text(
            f'<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6">\n{body}\n</osm>\n',
            encoding="utf-8",
        )
        return path


def square(e: float, n: float, size: float) -> list[tuple[float, float]]:
    """Counter-clockwise square with lower-left corner (e, n)."""
    return [(e, n), (e + size, n), (e + size, n + size), (e, n + size)]
