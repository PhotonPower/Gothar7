import math
import random
from collections import Counter
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.buildings.massing import Mass, _Builder
from gothar_worldgen.buildings.medieval import (
    ROLES,
    Dormer,
    Rules,
    StreetIndex,
    _chimney,
    _Context,
    _dormer,
    _plan,
    _plan_rect,
    _RoofPart,
    _SagRoof,
    assign_style,
    barn_hearths,
    build_house,
    chimney_count,
    chimney_rect,
    dormer_choice,
    dormer_depth,
    load_rules,
    place_chimneys,
    place_dormers,
    preferred_side,
    roof_takes_dormers,
)
from gothar_worldgen.facade.overrides import OverrideError, from_json, to_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
D = RULES.data["dormers"]
C = RULES.data["chimneys"]
THICK = RULES.data["roof"]["thicknessM"]
RING = [(0.0, 0.0), (14.0, 0.0), (14.0, -9.0), (0.0, -9.0)]  # ridge along x, sides south/north
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 14.2, "ridgeDir": [1.0, 0.0], "pitchDeg": 52.0}
STREET_SOUTH = StreetIndex([{"points": [[-20.0, 6.0], [40.0, 6.0]]}])


def house(bid: str = "H", ring=RING, roof=ROOF) -> dict:
    return {"id": bid, "inCore": True, "groundY": 0.0, "footprint": [list(p) for p in ring],
            "roof": roof}  # fmt: skip


def part(ring=RING, eave=8.4, ridge=14.2, sag=0.0, direction=(1.0, 0.0)) -> _RoofPart:
    mass = Mass(tuple(ring), eave, ridge, "saddle", direction)
    return _RoofPart(_SagRoof(mass, Polygon(ring), sag), Polygon(ring), Polygon(ring))


def style(name: str):
    return assign_style(house(), from_json({"id": "x", "style": name}), STREET_SOUTH, RULES)


def triangles(positions: np.ndarray, indices: np.ndarray) -> list:
    tri = positions[indices.reshape(-1, 3)]
    return [tuple(sorted(tuple(np.round(v, 4)) for v in t)) for t in tri]


# --- chimneys ---------------------------------------------------------------------------------


def test_chimney_count_per_style_ridge_and_barn_hearth():
    assert chimney_count(style("handwerkerhaus"), 10.0, RULES, None, False) == 1
    assert chimney_count(style("buergerhaus"), 15.0, RULES, None, False) == 2
    assert chimney_count(style("steinhaus"), 20.0, RULES, None, False) == 1
    assert chimney_count(style("kirche"), 20.0, RULES, None, False) == 0
    assert chimney_count(style("handwerkerhaus"), 3.0, RULES, None, False) == 0  # tiny shed
    assert chimney_count(style("scheune"), 10.0, RULES, None, False) == 0  # free-standing barn
    assert chimney_count(style("scheune"), 20.0, RULES, None, True) == 1  # hearth: only one
    override = from_json({"id": "x", "chimneys": 3})
    assert chimney_count(style("kirche"), 20.0, RULES, override, False) == 3


def test_barns_get_a_hearth_only_next_to_a_dwelling_or_by_function():
    dwelling = {**house("W"), "function": "31001_1010"}
    attached = {**house("A", [(14.0, 0.0), (24.0, 0.0), (24.0, -9.0), (14.0, -9.0)]),
                "function": "31001_2721"}  # fmt: skip
    alone = {**house("F", [(60.0, 0.0), (70.0, 0.0), (70.0, -9.0), (60.0, -9.0)]),
             "function": "31001_2721"}  # fmt: skip
    assert barn_hearths([dwelling, attached, alone], None, None, RULES) == {"A"}
    rules = Rules({**RULES.data, "chimneys": {**C, "hearthFunctions": ["31001_2721"]}})
    assert barn_hearths([dwelling, attached, alone], None, None, rules) == {"A", "F"}


@pytest.mark.parametrize("sag", [0.0, 0.25])
def test_chimney_stands_near_the_ridge_rises_above_it_and_has_no_gap(sag):
    p = part(sag=sag)
    roof = p.roof
    for seed in range(20):
        spots = place_chimneys(p, 1, [], RULES, random.Random(seed))
        assert len(spots) == 1
        u, v = spots[0]
        t = (u - roof.umin) / roof.length
        assert C["ridgeT"][0] - 1e-9 <= t <= C["ridgeT"][1] + 1e-9
        assert abs(v - roof.mid) <= C["offRidgeMaxM"] + 1e-9
        assert u - C["sizeM"][0] / 2 - roof.umin >= C["gableClearM"] - 1e-9
        b = _Builder((0.0, 0.0, 0.0))
        _chimney(b, roof, u, v, RULES, random.Random(seed))
        pos = np.asarray(b.pos)
        ridge_top = roof.height(*_plan(roof, u, roof.mid)) + THICK
        assert pos[:, 1].max() >= ridge_top + C["riseM"][0]
        foot = pos[:, 1].min()
        for x, z in chimney_rect(roof, u, v, RULES).exterior.coords:
            assert foot < roof.height(x, z) + THICK - 0.2  # starts below the roof surface
        assert len(b.idx) // 3 <= 24


def test_two_chimneys_one_per_half_and_clear_of_other_parts():
    p = part()
    spots = place_chimneys(p, 2, [], RULES, random.Random(1))
    assert len(spots) == 2
    mid = p.roof.umin + p.roof.length / 2
    assert sorted(u < mid for u, _ in spots) == [False, True]
    annex = Polygon([(5.0, -3.0), (9.0, -3.0), (9.0, -6.0), (5.0, -6.0)])  # over the ridge middle
    for u, v in place_chimneys(p, 1, [annex], RULES, random.Random(2)):
        assert chimney_rect(p.roof, u, v, RULES).distance(annex) >= C["partsClearM"]


# --- dormers ----------------------------------------------------------------------------------


def test_dormer_share_per_style():
    for name, share in D["shareByStyle"].items():
        n = 2000
        got = sum(dormer_choice(f"{name}{i}", None, name, RULES).wanted for i in range(n)) / n
        assert got == pytest.approx(share, abs=0.05), name
    kinds = Counter(dormer_choice(f"k{i}", None, "buergerhaus", RULES).kind for i in range(2000))
    assert kinds["schlepp"] / 2000 == pytest.approx(D["types"]["schlepp"], abs=0.05)
    hatch = [dormer_choice(f"s{i}", None, "scheune", RULES).hatch for i in range(50)]
    assert all(hatch)
    assert dormer_choice("a", None, "buergerhaus", RULES) == dormer_choice(
        "a", None, "buergerhaus", RULES
    )


def test_only_big_steep_saddle_roofs_take_dormers():
    assert roof_takes_dormers(part().roof, RULES)
    small = [(0.0, 0.0), (12.0, 0.0), (12.0, -7.0), (0.0, -7.0)]  # 3.5 m eave to ridge
    assert not roof_takes_dormers(part(small).roof, RULES)
    assert not roof_takes_dormers(part(ridge=11.0).roof, RULES)  # ridge only 2.6 m above eave
    r = build_house(house(ring=small, roof={**ROOF, "ridgeY": 13.0}), 0.0, (0.0, 0.0), RULES,
                    STREET_SOUTH, from_json({"id": "H", "dormers": 2}))  # fmt: skip
    assert r.dormers == 0 and any("dormers override ignored" in n for n in r.notes)
    stone = build_house(house(), 0.0, (0.0, 0.0), RULES, STREET_SOUTH,
                        from_json({"id": "H", "style": "steinhaus"}))  # fmt: skip
    assert stone.dormers == 0
    rare = [build_house(house(f"K{i}"), 0.0, (0.0, 0.0), RULES, STREET_SOUTH,
                        from_json({"id": f"K{i}", "style": "kirche"})).dormers
            for i in range(5)]  # fmt: skip
    assert rare == [0] * 5


@pytest.mark.parametrize("kind,pitch", [("schlepp", 17.0), ("giebel", 50.0)])
@pytest.mark.parametrize("sag", [0.0, 0.25])
def test_dormers_keep_every_clearance(kind, pitch, sag):
    p = part(sag=sag)
    roof = p.roof
    chimney = chimney_rect(roof, 7.0, roof.mid, RULES)
    dormers = place_dormers(p, {1: 3, -1: 3}, kind, 1.8, pitch, False, [chimney], [], RULES)
    assert dormers
    beta = math.atan2(14.2 - 8.4, roof.half)
    ov = D["overhangM"]
    for side in (1, -1):
        us = sorted(dm.u for dm in dormers if dm.side == side)
        assert all(
            b - a - 1.8 >= D["clearEachOtherM"] - 1e-9 for a, b in zip(us, us[1:], strict=False)
        )
    for dm in dormers:
        assert dm.kind == kind and dm.depth > 0
        assert roof.half - dm.d_front == pytest.approx(D["clearEaveM"] * math.cos(beta))
        assert dm.d_front - dm.depth >= D["clearRidgeM"] * math.cos(beta) - 1e-9
        assert dm.u - dm.w / 2 - roof.umin >= D["clearGableM"] - 1e-9
        assert roof.umax - dm.u - dm.w / 2 >= D["clearGableM"] - 1e-9
        v0, v1 = sorted((roof.mid + dm.side * (dm.d_front + ov),
                         roof.mid + dm.side * (dm.d_front - dm.depth)))  # fmt: skip
        body = _plan_rect(roof, dm.u - dm.w / 2 - ov, dm.u + dm.w / 2 + ov, v0, v1)
        assert body.distance(chimney) >= D["clearChimneyM"] - 1e-9
        assert Polygon(RING).buffer(1e-6).contains(body)


def test_no_dormers_next_to_other_parts():
    p = part()
    annex = Polygon([(3.0, 0.0), (11.0, 0.0), (11.0, 4.0), (3.0, 4.0)])  # along the south eave
    dormers = place_dormers(p, {1: 3, -1: 3}, "schlepp", 1.6, 17.0, False, [], [annex], RULES)
    assert dormers and all(dm.side == -1 for dm in dormers)  # roof.v = +z: +1 is the south side


def test_preferred_side_street_or_sun():
    p = part()  # ridge along x: side +1 faces south (+z), -1 north
    north_street = StreetIndex([{"points": [[-20.0, -15.0], [40.0, -15.0]]}])
    assert preferred_side(p.roof, north_street, 12.0) == -1  # eaves to the street: that side
    assert preferred_side(p.roof, STREET_SOUTH, 12.0) == 1
    gable_street = StreetIndex([{"points": [[20.0, -30.0], [20.0, 30.0]]}])  # beyond the gable
    assert preferred_side(p.roof, gable_street, 12.0) == 1  # sunny south side
    assert preferred_side(p.roof, None, 12.0) == 1
    ns = [(0.0, 0.0), (9.0, 0.0), (9.0, -14.0), (0.0, -14.0)]
    q = part(ns, direction=(0.0, 1.0))  # ridge north-south: v = (-1, 0), side +1 faces west
    street_north = StreetIndex([{"points": [[-30.0, -20.0], [30.0, -20.0]]}])  # gable to it
    assert preferred_side(q.roof, street_north, 12.0) == 1  # west before east


def test_gable_to_the_street_house_gets_its_dormer_on_the_sunny_side():
    ns = [(0.0, 0.0), (9.0, 0.0), (9.0, -14.0), (0.0, -14.0)]
    roof = {**ROOF, "ridgeDir": [0.0, 1.0]}
    gable_street = StreetIndex([{"points": [[-30.0, 6.0], [30.0, 6.0]]}])  # south of the gable
    r = build_house(house(ring=ns, roof=roof), 0.0, (0.0, 0.0), RULES, gable_street,
                    from_json({"id": "H", "dormers": 1, "chimneys": 0}))  # fmt: skip
    assert r.dormers == 1
    frame = next(p for p in r.primitives if p.material == "frame").mesh.positions
    high = frame[frame[:, 1] > 9.0]  # the dormer window, above the eave
    assert len(high) and high[:, 0].max() < 4.5  # west of the ridge (x = 4.5)


def ctx_for(style_name: str = "buergerhaus") -> _Context:
    builders = {role: _Builder((0.0, 0.0, 0.0)) for role in ROLES}
    return _Context(RULES, random.Random(0), None, style(style_name), 0, builders, 0.0)


@pytest.mark.parametrize("kind,pitch", [("schlepp", 17.0), ("giebel", 50.0)])
def test_dormer_roof_dives_under_the_main_roof_and_front_stands_on_it(kind, pitch):
    p = part(sag=0.2)
    roof = p.roof
    dm = Dormer(1, 5.0, 1.8, kind, False, pitch, roof.half - 0.4)
    dm = Dormer(1, 5.0, 1.8, kind, False, pitch, dm.d_front, dormer_depth(roof, dm, RULES))
    ctx = ctx_for()
    _dormer(ctx, roof, dm)
    tops = np.asarray(ctx.builders["roof"].pos + ctx.builders["roof_north"].pos)
    back = roof.mid + dm.d_front - dm.depth
    rear = tops[np.abs(tops[:, 2] - back) < 1e-6]  # rear edge of the dormer roof (v = z here)
    assert len(rear)
    for x, y, z in rear:
        assert y < roof.height(x, z) + THICK - 1e-3  # hidden under the main roof
    walls = np.asarray(ctx.builders["infill"].pos)
    front = walls[np.abs(walls[:, 2] - (roof.mid + dm.d_front)) < 1e-6]
    surface = min(roof.height(x, roof.mid + dm.d_front) + THICK for x in (4.1, 5.9))
    assert front[:, 1].min() < surface - THICK  # front wall starts inside the roof: no gap
    assert front[:, 1].max() >= surface + D["frontM"] - 1e-6
    tris = sum(len(b.idx) // 3 for b in ctx.builders.values())
    assert tris <= (36 if kind == "schlepp" else 45)
    frame = np.asarray(ctx.builders["frame"].pos)
    assert frame[:, 1].min() > surface + 0.1  # the window sits above the roof surface


def test_whole_house_no_duplicate_triangles_deterministic_and_counted():
    o = from_json({"id": "H", "dormers": 4, "chimneys": 2})
    a = build_house(house(), 0.0, (0.0, 0.0), RULES, STREET_SOUTH, o)
    b = build_house(house(), 0.0, (0.0, 0.0), RULES, STREET_SOUTH, o)
    assert a.dormers == 4 and a.chimneys == 2
    for pa, pb in zip(a.primitives, b.primitives, strict=True):
        assert np.array_equal(pa.mesh.positions, pb.mesh.positions)
    tris = [t for p in a.primitives for t in triangles(p.mesh.positions, p.mesh.indices)]
    assert len(tris) == len(set(tris))
    chimney = [p for p in a.primitives if p.material in C["material"]]
    assert chimney


def test_budget_level_4_drops_dormers_but_keeps_chimneys():
    o = from_json({"id": "H", "dormers": 2, "chimneys": 1})
    none = Rules({**RULES.data, "budget": {"trianglesPerBuilding": 1}})
    r = build_house(house(), 0.0, (0.0, 0.0), none, STREET_SOUTH, o)
    assert r.timber_level == 4 and r.dormers == 0 and r.chimneys == 1
    assert any("timber and dormers reduced" in n for n in r.notes)


def test_override_keys_dormers_and_chimneys():
    o = from_json({"id": "A", "dormers": 2, "chimneys": 0})
    assert (o.dormers, o.chimneys) == (2, 0)
    assert to_json(o) == {"id": "A", "keep": True, "dormers": 2, "chimneys": 0}
    for bad in ({"dormers": 7}, {"chimneys": -1}, {"dormers": 1.5}, {"chimneys": True}):
        with pytest.raises(OverrideError):
            from_json({"id": "A", **bad})
