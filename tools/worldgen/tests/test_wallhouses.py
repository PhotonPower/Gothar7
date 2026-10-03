from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.buildings.medieval import WallContext, build_house, load_rules
from gothar_worldgen.facade.overrides import OverrideError, from_json, to_json
from gothar_worldgen.walls.citywall import load_course, wall_context, wall_houses

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
TOWN = Polygon([(0.0, 0.0), (100.0, 0.0), (100.0, -60.0), (0.0, -60.0)])  # south side at z = 0
CROWN = 7.0
WALL = WallContext(TOWN, lambda x, z: CROWN, frozenset({"W"}))
# Straddles the south wall: the outer facade at z = 2 faces out of the town.
FOOT = [[20.0, 2.0], [32.0, 2.0], [32.0, -6.0], [20.0, -6.0]]


def house(eave: float = 5.0, ridge: float = 9.0, bid: str = "W", foot=FOOT) -> dict:
    roof = {"type": "saddle", "eaveY": eave, "ridgeY": ridge, "ridgeDir": [1.0, 0.0]}
    return {"id": bid, "inCore": True, "groundY": 0.0, "footprint": foot, "roof": roof}


def build(h: dict, wall=WALL, **override):
    o = from_json({"id": h["id"], "style": "handwerkerhaus", "age": 0.0, **override})
    return build_house(h, -0.3, (0.0, 0.0), RULES, None, o, wall=wall)


def role(result, name: str) -> np.ndarray:
    from gothar_worldgen.buildings.medieval import _materials

    mats = _materials(result.style)
    pts = [p.mesh.positions for p in result.primitives if p.material == mats[name]]
    return np.concatenate(pts) if pts else np.zeros((0, 3), np.float32)


def outer(points: np.ndarray, z: float = 2.0, tol: float = 1e-3) -> np.ndarray:
    return points[np.abs(points[:, 2] - z) < tol]


def test_detection_is_shared_and_needs_the_line_through_the_house():
    doc = {"format": "gothar-city-wall", "version": 1,
           "ring": [{"points": [[0, 0], [100, 0], [100, -60], [0, -60]]}]}  # fmt: skip
    course = load_course(doc, [])
    inside = house(bid="I", foot=[[40, -10], [50, -10], [50, -20], [40, -20]])
    touch = house(bid="T", foot=[[60, -0.2], [70, -0.2], [70, -8], [60, -8]])  # 0 m on the line
    houses = wall_houses(course, [house(), inside, touch], RULES)
    assert houses == {"W"}
    ctx = wall_context(course, [house(), inside, touch], lambda x, z: 0.0, RULES)
    assert ctx.houses == frozenset({"W"})
    assert ctx.crown(26.0, 0.0) == pytest.approx(RULES.data["cityWall"]["heightM"], abs=0.3)


def test_outer_side_is_stone_without_timber_door_or_jetty():
    r = build(house(eave=9.0, ridge=13.0), jettyM=0.4)
    assert r.style.wall_house
    stone = outer(role(r, "wall_ground"))
    assert len(stone) and stone[:, 1].max() >= 8.9 - 0.3  # stone up to the eave
    timber = role(r, "timber")
    assert len(timber) == 0 or timber[:, 2].max() <= 2.0 + 1e-3  # no beams in front of it
    infill = outer(role(r, "infill"))
    assert not ((infill[:, 0] > 20.01) & (infill[:, 0] < 31.99)).any()  # no plaster on it
    reveal = RULES.data["openings"]["revealM"]
    panels = outer(role(r, "frame"), 2.0 - reveal)
    wh = RULES.data["cityWall"]["wallHouse"]
    assert len(panels)  # slits / small windows exist
    xs = np.sort(np.unique(np.round(panels[:, 0], 3)))
    gaps = np.diff(xs)
    widths = gaps[gaps < 1.0]  # opening widths (gaps between openings are wider)
    assert widths.max() <= max(wh["window"][0], wh["slit"][0]) + 1e-3
    for pts in (role(r, "wall_ground"), role(r, "infill")):
        assert len(pts) == 0 or pts[:, 2].max() <= 2.0 + 1e-3  # no jetty beyond the wall line


def test_low_house_gets_a_screen_wall_flush_with_the_crown_and_no_overhang():
    r = build(house(eave=5.0, ridge=9.0))
    stone = outer(role(r, "wall_ground"))
    merlon_h = RULES.data["cityWall"]["merlon"]["h"]
    assert stone[:, 1].max() == pytest.approx(CROWN + merlon_h + 0.3, abs=0.06)  # +0.3: base
    roof = np.concatenate([role(r, "roof"), role(r, "roof_north")])
    assert roof[:, 2].max() <= 2.0 + 1e-3  # no roof overhang over the town wall side
    assert r.collision is not None
    assert any(p.positions[:, 1].max() >= CROWN + 0.3 - 1e-3 for p in r.collision.parts)


def test_high_house_has_no_screen_and_dormers_only_on_the_town_side():
    r = build(house(eave=9.0, ridge=14.5), dormers=2)
    stone = outer(role(r, "wall_ground"))
    assert stone[:, 1].max() < 9.0 + 0.3 + 0.05  # the stone wall goes up to the roof, no merlons
    assert r.dormers >= 1
    frames = role(r, "frame")
    high = frames[frames[:, 1] > 9.3]
    assert len(high) and high[:, 2].max() < -1.0  # dormer windows face the town (north)


def test_override_switches_the_wall_house_off_and_without_a_wall_nothing_changes():
    off = build(house(), wallHouse=False)
    assert not off.style.wall_house
    assert len(role(off, "timber")) and role(off, "timber")[:, 2].max() > 1.9
    plain = build(house(), wall=None)
    assert not plain.style.wall_house
    on = build(house(bid="X"), wallHouse=True)  # not detected, but forced on
    assert on.style.wall_house


def test_override_key_wall_house():
    assert to_json(from_json({"id": "A", "wallHouse": True})) == {
        "id": "A",
        "keep": True,
        "wallHouse": True,
    }
    with pytest.raises(OverrideError, match="wallHouse"):
        from_json({"id": "A", "wallHouse": "ja"})


def test_deterministic():
    a, b = build(house()), build(house())
    for pa, pb in zip(a.primitives, b.primitives, strict=True):
        assert np.array_equal(pa.mesh.positions, pb.mesh.positions)
