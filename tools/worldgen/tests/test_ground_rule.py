"""Ground rule of the hand-made models (W6): terraces, pads, measured ground, grounding check."""

import json
import math
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Point, Polygon

from gothar_worldgen.export.pads import apply_pads
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.garden_terraces import (
    LEDGE_IN,
    LEDGE_W,
    SIDE_W,
    STEP_M,
    STRIP_M,
    WALL_T,
    level_at,
    plan,
)
from gothar_worldgen.handmade import WING_F, garden_plan, with_wing_terrain
from gothar_worldgen.owner_models import garden_placements
from gothar_worldgen.qa.begehung import game_grid
from gothar_worldgen.qa.grounding import FOOT_M, check_vob, check_world, placed

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT.parents[1] / "assets" / "source"
SCHLOSS = json.loads((ROOT / "data" / "leonberg" / "schloss.json").read_text(encoding="utf-8"))
EXTENT = {"halfS": 30.0, "halfT": 8.0, "innerS": 5.0}


class Frame:
    """Garden frame = world (x = s, z = t)."""

    centre = (0.0, 0.0)

    def p(self, s: float, t: float, y: float) -> tuple[float, float, float]:
        return (s, y, t)


def slope(x: float, z: float) -> float:  # rises 6 % to the east and 5 % to the north (-z)
    return 0.06 * x - 0.05 * z


def test_terraces_are_level_and_follow_the_slope():
    p = plan(Frame(), slope, EXTENT)
    west, mitte, ost = p["terraces"]
    assert [tr["key"] for tr in p["terraces"]] == ["west", "mitte", "ost"]
    assert west["y"] < mitte["y"] < ost["y"]
    assert mitte["y"] == pytest.approx(0.0, abs=0.05)  # median of the middle field
    assert west["s"][1] == mitte["s"][0] == -5.0 and level_at(p["terraces"], 20.0) == ost["y"]
    assert level_at(p["terraces"], 99.0) is None
    for tr, pad in zip(p["terraces"], p["pads"], strict=False):  # the heightmap gets each level
        assert pad["y"] == tr["y"] and pad["exact"]
        assert Polygon(pad["polygon"]).area == pytest.approx(
            (tr["s"][1] - tr["s"][0] + 0.02) * (tr["t"][1] - tr["t"][0])
        )


def test_walls_reach_below_the_lower_side_and_leave_gates():
    p = plan(Frame(), slope, EXTENT)
    levels = [tr["y"] for tr in p["terraces"]]
    assert all(w["y0"] < min(levels) + 2.0 for w in p["walls"])
    assert all(w["y1"] > w["y0"] for w in p["walls"] + p["bodies"])
    assert len(p["bodies"]) < len(p["walls"])  # one body per run of pieces
    north = [st for st in p["stairs"] if st["dir"] == [0.0, 1.0] and not st.get("side")]
    assert north, "the ground outside the north edge rises: flights down into the terraces"
    for st in p["stairs"]:  # every flight opens a wall: no piece crosses its top point
        top = Point(st["top"])
        for w in p["walls"]:
            if w.get("t", p["wallT"]) == p["wallT"] and st["w"] > SIDE_W:
                seg = Polygon([w["a"], w["b"], [w["b"][0] + 1e-3, w["b"][1]], w["a"]])
                assert seg.distance(top) > 0.5 or abs(st["y1"] - st["y0"]) < STEP_M


def test_side_flights_between_the_terraces():
    p = plan(Frame(), slope, EXTENT)
    side = [st for st in p["stairs"] if st.get("side")]
    assert len(side) == 2 and all(st["w"] == SIDE_W for st in side)
    west = next(st for st in side if st["top"][0] < 0)
    assert west.get("cut") is True  # the west half is lower: a pit sunk into the middle field
    # the east half is higher: a raised landing at its gate, and the gate through its ledge
    land, gate = p["landings"]
    assert gate["y1"] == land["y1"] and gate["s"][0] == land["s"][1]
    assert gate["s"][1] - gate["s"][0] == pytest.approx(WALL_T + LEDGE_IN)
    assert land["y1"] == p["terraces"][2]["y"]
    (cut_pad,) = p["pads"][3:4]  # after the terraces: the pit, then the strips of the edges
    assert cut_pad["y"] < p["terraces"][1]["y"] and "clampBelow" not in cut_pad


def test_ledges_on_the_higher_side_hide_the_heightmap_slope():
    p = plan(Frame(), slope, EXTENT)
    west, _, ost = p["terraces"]
    ledges = [w for w in p["walls"] if w["t"] == LEDGE_W]
    assert ledges and all(w["y1"] > w["y0"] for w in ledges)
    # the west end: the ground falls away outside, so the ledge lies inside, flush with the terrace
    west_end = [w for w in ledges if w["a"][0] == w["b"][0] == west["s"][0]]
    assert west_end and all(w["in"] == [1.0, 0.0] and w["y1"] == west["y"] for w in west_end)
    # the east end: the ground rises outside, the ledge lies outside and holds it back
    east_end = [w for w in ledges if w["a"][0] == w["b"][0] == ost["s"][1]]
    assert east_end and all(w["in"] == [1.0, 0.0] and w["y1"] > ost["y"] for w in east_end)
    strips = p["pads"][4:]
    lowered = [st for st in strips if st.get("clampBelow")]
    assert lowered and all(st["y"] < ost["y"] for st in lowered)
    for st in strips[:-2]:  # STRIP_M wide on the outer edges
        poly = Polygon(st["polygon"])
        assert poly.area == pytest.approx(STRIP_M * poly.length / 2 - STRIP_M**2, rel=0.01)
    for st in strips[-2:]:  # between the terraces, lowered to the lower terrace
        assert st["clampBelow"] and st["y"] in (west["y"], p["terraces"][1]["y"])


def test_inner_ledges_leave_room_for_the_fountain_and_the_beds():
    p = plan(Frame(), slope, EXTENT)
    inner = [w for w in p["walls"] if w["t"] in (LEDGE_IN, WALL_T + LEDGE_IN)]
    assert inner
    for w in inner:  # from the face into the higher side, never past 3.5 m (fountain) / 6.5 m
        reach = w["a"][0] + w["in"][0] * w["t"]
        assert 3.5 - 1e-6 <= abs(reach) <= 6.5 + 1e-6


def test_pads_level_the_heightmap():
    grid = Grid(np.zeros((21, 21)), -10.0, -10.0, 1.0)
    square = [[-3.0, -3.0], [3.0, -3.0], [3.0, 3.0], [-3.0, 3.0]]
    out, n = apply_pads(grid, [{"polygon": square, "y": 2.0}])
    assert out.height_at(0.0, 0.0) == 2.0 and out.height_at(3.0, 0.0) == 2.0
    assert out.height_at(6.0, 0.0) == 0.0 and grid.height_at(0.0, 0.0) == 0.0  # copy
    assert n == 49
    exact, n = apply_pads(grid, [{"polygon": square, "y": 2.0, "exact": True}])
    assert n == 25 and exact.height_at(2.0, 0.0) == 2.0 and exact.height_at(3.0, 0.0) == 0.0
    faded, _ = apply_pads(grid, [{"polygon": square, "y": 2.0, "fadeM": 4.0}])
    assert faded.height_at(5.0, 0.0) == pytest.approx(1.0)  # half way out of the fade
    assert faded.height_at(8.0, 0.0) == 0.0
    hill = Grid(np.fromfunction(lambda r, c: c * 0.5, (21, 21)), -10.0, -10.0, 1.0)
    cut, _ = apply_pads(hill, [{"polygon": square, "y": 5.5, "clampBelow": True}])
    assert cut.height_at(-3.0, 0.0) == 3.5 and cut.height_at(3.0, 0.0) == 5.5  # only lowered


def test_grounding_check_allows_only_the_foot_in_the_ground():
    grid = Grid(np.full((11, 11), 1.0), -5.0, -5.0, 1.0)
    foot = [[0.0, 0.8, 0.0], [1.0, 0.8 + FOOT_M - 0.05, 0.0]]  # foundation band: may be buried
    wall = [[0.0, 2.0, 0.0], [1.0, 3.0, 1.0]]
    assert check_vob("ok", np.array(foot + wall), grid).ok
    # base 0: up to FOOT_M in the ground is fine, 0.05 m is within the tolerance, 0.4 m is not
    sunk = np.array([[0, 0, 0], [0, 0.3, 0], [1, 0.6, 1], [2, 0.95, 2], [3, 2, 3]], dtype=float)
    r = check_vob("sunk", sunk, grid)
    assert not r.ok and r.buried == 1 and r.worst == (1.0, 1.0)
    assert r.deepest == pytest.approx(0.4)
    quarter = [0.0, math.sin(math.pi / 4), 0.0, math.cos(math.pi / 4)]
    turned = placed(np.array([[1.0, 0.0, 0.0]]), [10.0, 1.0, 0.0], quarter)
    assert turned[0] == pytest.approx([10.0, 1.0, -1.0])  # 90 degrees about +Y


def test_castle_wings_get_measured_ground_at_their_ends():
    spec = with_wing_terrain(SCHLOSS, lambda x, z: 2.5)
    for w, raw in zip(spec["wings"], SCHLOSS["wings"], strict=True):
        assert w["terrain"]["f"] == list(WING_F) and WING_F[0] == 0.0 and WING_F[-1] == 1.0
        assert w["terrain"]["court"] == w["terrain"]["garden"] == [2.5] * len(WING_F)
        assert {k: v for k, v in w.items() if k != "terrain"} == {
            k: v for k, v in raw.items() if k != "terrain"
        }


def test_garden_models_stand_on_their_terraces():
    garden = garden_plan(SCHLOSS, lambda x, z: -0.05 * x)
    terraces = [{"key": tr["key"], "s": tr["s"], "y": tr["y"]} for tr in garden["terraces"]]
    radii = {"obeliskbrunnen": 3.55, "brunnen_garten": 1.88}
    ps = {p.key: p for p in garden_placements(SCHLOSS, radii, terraces=terraces)}
    level = {tr["key"]: tr["y"] for tr in terraces}
    fence = ps["garten_gelaender"]
    assert fence.pos[1] == level["mitte"] and fence.shear == (0.0, 0.0)
    assert fence.lifts == {
        "parterre_west": pytest.approx(level["west"] - level["mitte"]),
        "parterre_east": pytest.approx(level["ost"] - level["mitte"]),
    }
    assert ps["gartenbrunnen_w"].pos[1] == pytest.approx(level["west"] - 0.02)
    assert ps["gartenbrunnen_o"].pos[1] == pytest.approx(level["ost"] - 0.02)
    assert ps["obeliskbrunnen"].pos[1] == pytest.approx(level["mitte"] - 0.02)


WORLD = ASSETS / "worlds" / "leonberg" / "leonberg.g7world"
HEIGHTMAP = ASSETS / "worlds" / "leonberg" / "generated" / "leonberg_terrain.r16"


@pytest.mark.skipif(not HEIGHTMAP.is_file(), reason="heightmap not exported (export-terrain)")
def test_no_hand_made_model_is_sunk_in_the_world():
    world = json.loads(WORLD.read_text(encoding="utf-8"))
    reports = check_world(world, ASSETS, game_grid(world, ASSETS))
    names = {r.name for r in reports}
    assert {"HANDMADE_SCHLOSS", "HANDMADE_KIRCHE", "HANDMADE_MARKTBRUNNEN"} <= names
    assert {"HANDMADE_GARTEN_GELAENDER", "HANDMADE_OBELISKBRUNNEN"} <= names
    assert [(r.name, r.buried, r.deepest, r.worst) for r in reports if not r.ok] == []
