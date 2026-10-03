import math

import numpy as np
import pytest

from gothar_worldgen.assemble.world import VobIds, assemble
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.export.water import carve_and_place, river_lines

# 200 m x 120 m at 1 m, falling 0.02 m per metre towards +x (east).
XS = np.arange(200) - 99.5
HEIGHTS = np.tile(10.0 - 0.02 * XS, (120, 1))
GRID = Grid(HEIGHTS.copy(), -99.5, -59.5, 1.0)
RIVER = {"osmName": "Bach", "widthM": 4.0, "depthM": 1.2, "bankM": 2.0, "levelBelowM": 0.3,
         "segmentM": 15.0, "smoothM": 10.0}  # fmt: skip
LAKE = {"osmName": "Teich", "depthM": 1.5, "levelBelowM": 0.2, "shoreM": 3.0}
DOC = {"rivers": [RIVER], "lakes": [LAKE], "boxBelowBedM": 0.3, "overlapM": 0.5}
FEATURES = [
    # The river comes in two pieces (joined) and has a culvert that is left out.
    {
        "kind": "stream",
        "geometry": "line",
        "name": "Bach",
        "points": [[-90.0, -20.0], [0.0, -20.0]],
    },
    {"kind": "stream", "geometry": "line", "name": "Bach", "points": [[0.0, -20.0], [90.0, -30.0]]},
    {
        "kind": "stream",
        "geometry": "line",
        "name": "Bach",
        "tags": {"tunnel": "culvert"},
        "points": [[90.0, -30.0], [95.0, -31.0]],
    },  # fmt: skip
    {
        "kind": "water",
        "geometry": "polygon",
        "name": "Teich",
        "polygon": [[-40.0, 20.0], [0.0, 20.0], [0.0, 45.0], [-40.0, 45.0]],
    },  # fmt: skip
]


def run():
    return carve_and_place(GRID, FEATURES, DOC)


def cell(grid: Grid, x: float, z: float) -> float:
    return float(grid.heights[round(z - grid.first_z), round(x - grid.first_x)])


def surface_at(entries, x: float, z: float) -> float | None:
    """Highest box top whose rotated footprint contains (x, z)."""
    best = None
    for e in entries:
        cx, cy, cz = e["pos"]
        hx, hy, hz = e["halfExtents"]
        qy, qw = e["rot"][1], e["rot"][3]
        yaw = 2 * math.atan2(qy, qw)
        ux, uz = math.cos(yaw), -math.sin(yaw)  # local +X
        dx, dz = x - cx, z - cz
        a = dx * ux + dz * uz
        b = -dx * uz + dz * ux
        if abs(a) <= hx and abs(b) <= hz:
            top = cy + hy
            best = top if best is None else max(best, top)
    return best


def test_lines_are_joined_without_culverts():
    lines = river_lines(FEATURES, "Bach")
    assert len(lines) == 1 and lines[0][0] == (-90.0, -20.0) and lines[0][-1] == (90.0, -30.0)


def test_bed_is_carved_banks_stay_dry_and_nothing_rises():
    carved, index = run()
    assert (carved.heights <= HEIGHTS + 1e-9).all()
    for x in (-60.0, -20.0, 40.0):
        z = -20.0 if x < 0 else -20.0 - x / 9.0
        ground = 10.0 - 0.02 * x
        assert cell(carved, x, z) == pytest.approx(ground - RIVER["depthM"], abs=0.15)
        level = surface_at(index["entries"], x, z)
        assert level is not None and level - cell(carved, x, z) > 0.9  # deep enough to swim
        assert cell(carved, x, z + 6.0) == pytest.approx(ground, abs=0.05)  # beyond the bank
        assert cell(carved, x, z + 5.0) >= level - 0.05  # bank top above the water: dry


def test_level_never_rises_downstream_and_boxes_cover_the_axis():
    _, index = run()
    river = [e for e in index["entries"] if e["id"].startswith("bach_")]
    assert len(river) >= 180 / RIVER["segmentM"]
    tops = [e["pos"][1] + e["halfExtents"][1] for e in river]
    assert all(b <= a + 1e-6 for a, b in zip(tops, tops[1:], strict=False))
    for x in np.arange(-88.0, 88.0, 1.0):
        z = -20.0 if x < 0 else -20.0 - x / 9.0
        assert surface_at(river, float(x), z) is not None


def test_lake_basin_and_boxes():
    carved, index = run()
    lake = [e for e in index["entries"] if e["id"].startswith("teich_")]
    assert len(lake) == 1
    level = surface_at(lake, -20.0, 32.0)
    ground = float(np.median([10.0 - 0.02 * x for x in range(-40, 1)]))
    assert level == pytest.approx(ground - LAKE["levelBelowM"], abs=0.1)
    assert cell(carved, -20.0, 32.0) == pytest.approx(ground - LAKE["depthM"], abs=0.2)
    assert cell(carved, -20.0, 50.0) == cell(GRID, -20.0, 50.0)  # outside the lake: unchanged


def test_format_and_assembler():
    _, index = run()
    for e in index["entries"]:
        assert all(h > 0 for h in e["halfExtents"])
        assert e["rot"][0] == 0.0 and e["rot"][2] == 0.0  # only about Y
        assert abs(e["rot"][1] ** 2 + e["rot"][3] ** 2 - 1.0) < 1e-5
    terrain = {"version": 1, "name": "t", "terrain": {"version": 1}}
    ids = VobIds({}, 1)
    res = assemble(terrain, {"entries": []}, None, ids, "t", water=index)
    vobs = [v for v in res.world["vobs"] if v["type"] == "water"]
    assert len(vobs) == len(index["entries"])
    v = vobs[0]
    assert list(v) == ["id", "type", "name", "parent", "pos", "rot", "components"]
    assert set(v["components"]) == {"water"} and len(v["components"]["water"]["halfExtents"]) == 3
    again = assemble(terrain, {"entries": []}, res.world, ids, "t", water=index)
    assert again.added == 0 and again.updated == 0
