"""Axis-aligned bounding boxes in projected coordinates and the tile grids that cover them."""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class BBox:
    """Rectangle in projected metres (e.g. EPSG:25832). Min is inclusive, max exclusive."""

    min_e: float
    min_n: float
    max_e: float
    max_n: float

    def __post_init__(self) -> None:
        if not (self.max_e > self.min_e and self.max_n > self.min_n):
            raise ValueError(f"degenerate bounding box: {self}")

    @classmethod
    def around(cls, easting: float, northing: float, half_extent_m: float) -> BBox:
        """Square box centred on a point."""
        return cls(
            easting - half_extent_m,
            northing - half_extent_m,
            easting + half_extent_m,
            northing + half_extent_m,
        )

    @property
    def width(self) -> float:
        return self.max_e - self.min_e

    @property
    def height(self) -> float:
        return self.max_n - self.min_n

    def contains(self, easting: float, northing: float) -> bool:
        return self.min_e <= easting < self.max_e and self.min_n <= northing < self.max_n


@dataclass(frozen=True)
class Tile:
    """A square tile of a regular grid, identified by its lower-left corner in metres."""

    min_e: int
    min_n: int
    size_m: int

    @property
    def label(self) -> str:
        """Lower-left corner in km, as used in LGL tile names (e.g. ``500_5405``)."""
        return f"{self.min_e // 1000}_{self.min_n // 1000}"

    @property
    def bbox(self) -> BBox:
        return BBox(self.min_e, self.min_n, self.min_e + self.size_m, self.min_n + self.size_m)


def tiles_covering(bbox: BBox, tile_size_m: int) -> list[Tile]:
    """All grid tiles (aligned to multiples of ``tile_size_m``) that overlap ``bbox``.

    Sorted by northing, then easting. A box edge lying exactly on a tile border does not
    pull in the neighbouring tile.
    """
    if tile_size_m <= 0:
        raise ValueError(f"tile size must be positive, got {tile_size_m}")
    first_e = math.floor(bbox.min_e / tile_size_m)
    last_e = math.ceil(bbox.max_e / tile_size_m) - 1
    first_n = math.floor(bbox.min_n / tile_size_m)
    last_n = math.ceil(bbox.max_n / tile_size_m) - 1
    return [
        Tile(e * tile_size_m, n * tile_size_m, tile_size_m)
        for n in range(first_n, last_n + 1)
        for e in range(first_e, last_e + 1)
    ]
