"""Country roads and field ways outside the wall (geo/rural.py) and the evened ground
(export/harmonise.py)."""

from __future__ import annotations

import numpy as np
from shapely.geometry import LineString, Polygon

from gothar_worldgen.export.harmonise import harmonise, laplace_fill, rasterise, rural_areas
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.geo.rural import rural_streets

RING = [(-50.0, -50.0), (50.0, -50.0), (50.0, 50.0), (-50.0, 50.0)]
GATE = (50.0, 0.0)
SPEC = {
    "insideM": 5.0,
    "reachM": 600.0,
    "landstrasse": {"highways": ["secondary"], "gateM": 12.0, "widthM": 4.5, "surface": "gravel"},
    "feldweg": {
        "highways": ["track", "path"],
        "named": ["residential"],
        "minNamedM": 400.0,
        "widthM": 2.5,
        "surface": "mud",
    },  # fmt: skip
    "harmonise": {
        "buildingM": 3.0,
        "streetMarginM": 3.0,
        "streetMarginShare": 0.5,
        "railWidthM": 8.0,
        "wallM": 30.0,
        "keepM": 5.0,
    },  # fmt: skip
}


def street(hw: str, pts: list[tuple[float, float]], **kw: object) -> dict:
    return {"osmId": 1, "highway": hw, "class": "road", "widthM": 8.0,
            "points": [list(p) for p in pts], **kw}  # fmt: skip


def test_inside_streets_stay_unchanged() -> None:
    s = street("service", [(-20.0, 0.0), (20.0, 0.0)])
    out, stats = rural_streets([s], RING, [GATE], SPEC)
    assert out == [s]
    assert stats["dropped"]["pieces"] == 0


def test_gate_street_is_cut_at_the_ring_and_becomes_a_country_road() -> None:
    s = street("residential", [(0.0, 0.0), (200.0, 0.0)], surface="asphalt")
    out, stats = rural_streets([s], RING, [GATE], SPEC)
    inside = [e for e in out if "rural" not in e]
    rural = [e for e in out if e.get("rural")]
    assert len(inside) == 1 and inside[0]["highway"] == "residential"
    assert inside[0]["points"][-1] == [55.0, 0.0]  # the ring plus insideM
    assert len(rural) == 1
    r = rural[0]
    assert r["rural"] == "landstrasse" and r["highway"] == "track" and r["class"] == "track"
    assert r["osmHighway"] == "residential" and r["widthM"] == 4.5 and r["surface"] == "gravel"
    assert r["points"][0] == [55.0, 0.0] and r["points"][-1] == [200.0, 0.0]
    assert stats["landstrasse"]["m"] == 145.0


def test_country_highway_tracks_and_long_named_streets_outside() -> None:
    streets = [
        street("residential", [(0.0, 0.0), (100.0, 0.0), (100.0, 100.0)], name="Torweg"),
        street("secondary", [(100.0, 100.0), (100.0, 150.0), (100.0, 300.0)]),
        street("track", [(100.0, 150.0), (150.0, 150.0)]),
        street("service", [(100.0, 300.0), (200.0, 300.0)]),
        street("steps", [(200.0, 300.0), (250.0, 300.0)]),
        street("residential", [(150.0, 150.0), (300.0, 150.0), (300.0, 350.0)], name="Lange Gasse"),
        street("residential", [(300.0, 350.0), (300.0, 520.0)], name="Lange Gasse"),
        street("residential", [(300.0, 520.0), (500.0, 520.0)], name="Kurze Gasse"),
    ]
    out, stats = rural_streets(streets, RING, [GATE], SPEC)
    kinds = [(e.get("osmHighway", e["highway"]), e.get("name"), e.get("rural")) for e in out]
    assert kinds == [
        ("residential", "Torweg", None),  # inside the ring
        ("residential", "Torweg", "landstrasse"),  # starts at the gate tower
        ("secondary", None, "landstrasse"),
        ("track", None, "feldweg"),
        ("residential", "Lange Gasse", "feldweg"),
        ("residential", "Lange Gasse", "feldweg"),
    ]
    assert all(e["widthM"] == 2.5 and e["surface"] == "mud" for e in out[3:])
    assert stats["namedKept"] == ["Lange Gasse"]
    assert stats["dropped"]["pieces"] == 3
    assert stats["access"] == {"links": 0, "linkM": 0.0, "isolated": 0, "targets": 0}


def test_isolated_ways_and_doors_get_the_shortest_link() -> None:
    streets = [
        street("residential", [(0.0, 0.0), (100.0, 0.0)], name="Torweg"),
        street("service", [(100.0, 0.0), (100.0, 40.0), (100.0, 80.0)]),
        street("track", [(100.0, 80.0), (100.0, 200.0)]),
        street("service", [(100.0, 0.0), (200.0, 0.0), (300.0, 0.0)]),
        street("track", [(600.0, 0.0), (700.0, 0.0)]),  # far off: left out
    ]
    out, stats = rural_streets(streets, RING, [GATE], SPEC, targets=[(195.0, 8.0)])
    access = [e["points"] for e in out if e.get("access")]
    assert [[100.0, 0.0], [100.0, 40.0], [100.0, 80.0]] in access  # joins the track
    assert [[100.0, 0.0], [200.0, 0.0]] in access  # the door at (195, 8)
    assert stats["access"]["links"] == 1 and stats["access"]["targets"] == 1
    assert stats["access"]["isolated"] == 1 and stats["isolatedM"] == 100.0
    assert not any(e["points"][0] == [600.0, 0.0] for e in out)


def test_laplace_fill_closes_a_pit_and_keeps_a_plane() -> None:
    rows, cols = np.mgrid[0:40, 0:40]
    plane = 0.1 * cols + 0.05 * rows
    h = plane.copy()
    mask = np.zeros(h.shape, dtype=bool)
    mask[10:30, 12:25] = True
    h[mask] -= 3.0  # a cellar pit
    laplace_fill(h, mask)
    assert np.abs(h - plane).max() < 0.01  # a plane is harmonic: the pit is gone


def test_harmonise_fills_minus_keep_only() -> None:
    heights = np.zeros((30, 30))
    heights[10:20, 5:25] = -2.0  # a cut
    grid = Grid(heights, 0.0, 0.0, 1.0)
    fill = Polygon([(4.5, 9.5), (24.5, 9.5), (24.5, 19.5), (4.5, 19.5)])
    keep = Polygon([(14.5, 9.5), (24.5, 9.5), (24.5, 19.5), (14.5, 19.5)])
    out, stats = harmonise(grid, fill, keep)
    assert stats["cells"] == 100
    assert np.all(out.heights[10:20, 15:25] == -2.0)  # kept as it was
    assert out.heights[15, 6] > -1.0  # filled towards the surroundings
    assert stats["raisedM"] > 1.0 and stats["loweredM"] == 0.0


def test_rasterise_leaves_the_border() -> None:
    grid = Grid(np.zeros((10, 10)), 0.0, 0.0, 1.0)
    mask = rasterise(grid, Polygon([(-5, -5), (20, -5), (20, 20), (-5, 20)]))
    assert mask.sum() == 64 and not mask[0].any() and not mask[:, -1].any()


def test_rural_areas_fill_outside_and_keep_the_town_and_kept_houses() -> None:
    raw = [street("service", [(0.0, 0.0), (300.0, 0.0)]),
           street("secondary", [(0.0, 200.0), (300.0, 200.0)], bridge=True)]  # fmt: skip
    dropped = [[[200.0, 50.0], [210.0, 50.0], [210.0, 60.0], [200.0, 60.0]]]
    kept = [[[150.0, -3.0], [160.0, -3.0], [160.0, 3.0], [150.0, 3.0]]]
    fill, keep = rural_areas(raw, RING, dropped, kept, [], SPEC)
    assert fill.contains(LineString([(120.0, 0.0), (140.0, 0.0)]))
    assert not fill.intersects(Polygon(RING))  # nothing inside the wall
    assert fill.contains(Polygon(dropped[0]).buffer(2.9))
    assert keep.contains(Polygon(kept[0]).buffer(4.9))
    assert keep.contains(LineString([(100.0, 200.0), (200.0, 200.0)]))  # the bridge
    assert keep.contains(Polygon(RING).buffer(29.0))
