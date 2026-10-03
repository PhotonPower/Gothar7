import random
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.buildings.massing import Mass
from gothar_worldgen.buildings.medieval import (
    Opening,
    StreetIndex,
    _lean_posts,
    _SagRoof,
    assign_style,
    build_house,
    load_rules,
    timber_segments,
)
from gothar_worldgen.facade.overrides import OverrideError, from_json, to_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
AGING = RULES.data["aging"]
RING = [[0.0, 0.0], [12.0, 0.0], [12.0, -7.0], [0.0, -7.0]]
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0], "pitchDeg": 53.0}
STREET = StreetIndex([{"points": [[-20.0, 5.0], [40.0, 5.0]]}])


def house(bid: str = "H", x: float = 0.0) -> dict:
    fp = [[px + x, pz] for px, pz in RING]
    return {"id": bid, "inCore": True, "groundY": 0.0, "footprint": fp, "roof": ROOF}


def aged(age: float, style: str = "handwerkerhaus", bid: str = "H"):
    return from_json({"id": bid, "style": style, "age": age})


def test_age_follows_the_style_ranges_and_the_override():
    for style, (lo, hi) in AGING["ageByStyle"].items():
        ages = [
            assign_style(
                house(f"{style}{i}"), from_json({"id": "x", "style": style}), STREET, RULES
            ).age
            for i in range(60)
        ]
        assert lo <= min(ages) and max(ages) <= hi and max(ages) - min(ages) > 0.1 * (hi - lo)
    assert assign_style(house(), aged(0.42), STREET, RULES).age == 0.42
    a = assign_style(house("A"), None, STREET, RULES).age
    assert a == assign_style(house("A"), None, STREET, RULES).age  # deterministic


def test_override_key_age():
    assert to_json(from_json({"id": "A", "age": 0.5})) == {"id": "A", "keep": True, "age": 0.5}
    with pytest.raises(OverrideError, match="age"):
        from_json({"id": "A", "age": 1.5})


def test_sag_only_in_the_ridge():
    mass = Mass(tuple(map(tuple, RING)), 8.4, 13.0, "saddle", (1.0, 0.0))
    roof = _SagRoof(mass, Polygon(RING), 0.2)
    assert roof.sag_at(6.0, -3.5) == pytest.approx(0.2)  # ridge middle
    for x, z in ((0.0, -3.5), (12.0, -3.5), (6.0, 0.0), (6.0, -7.0), (6.0, 1.0), (3.0, 0.4)):
        assert roof.sag_at(x, z) == 0.0  # gable ends, eaves and the overhang beyond them
    flat = _SagRoof(Mass(mass.footprint, 8.4, 8.4, "flat", None), Polygon(RING), 0.2)
    assert flat.sag == 0.0


def roof_points(result) -> dict:
    pts = {}
    for p in result.primitives:
        if p.material.startswith("roof"):
            for x, y, z in p.mesh.positions:
                pts[(round(float(x), 2), round(float(z), 2))] = float(y)
    return pts


def test_old_house_ridge_sags_eaves_stay():
    young = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, aged(0.0))
    old = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, aged(1.0))
    expected = AGING["ridgeSagMaxRatio"] * 12.0
    assert young.sag_m == 0.0 and old.sag_m == pytest.approx(expected, rel=0.05)
    yp, op = roof_points(young), roof_points(old)
    ridge_old = [y for (x, z), y in op.items() if abs(z) < 0.05 and abs(x) < 2.0]
    ridge_young = max(y for (x, z), y in yp.items() if abs(z) < 0.05)
    assert ridge_old and min(ridge_old) < ridge_young - 0.5 * expected  # lower in the middle
    # Every roof point of the old house at or beyond the eave lines (local |z| >= 3.5) keeps the
    # young house's height: the eaves stay straight.
    shared = [k for k in op if abs(k[1]) >= 3.5 - 1e-6 and k in yp]
    assert shared and all(op[k] == pytest.approx(yp[k], abs=1e-3) for k in shared)
    assert old.triangles <= RULES.data["budget"]["trianglesPerBuilding"]


def test_neighbours_share_the_eave_line():
    """Party wall at x = 12: the eaves of a young and an old neighbour meet without a gap."""
    a = build_house(house("A"), -0.3, (0.0, 0.0), RULES, STREET, aged(1.0, bid="A"))
    b = build_house(house("B", 12.0), -0.3, (0.0, 0.0), RULES, STREET, aged(0.0, bid="B"))
    for z in (0.0, -7.0):  # both eave lines at the party wall
        ya = [
            y
            for (x, zz), y in roof_points(a).items()
            if abs(x - 12.0) < 1e-6 and abs(zz - z) < 1e-6
        ]
        yb = [
            y
            for (x, zz), y in roof_points(b).items()
            if abs(x - 12.0) < 1e-6 and abs(zz - z) < 1e-6
        ]
        if ya and yb:
            assert min(ya) == pytest.approx(min(yb), abs=1e-3)
    sag_a = [y for (x, z), y in roof_points(a).items() if abs(x - 6.0) < 0.6 and abs(z + 3.5) < 0.6]
    assert sag_a  # the old one does sag in its middle
    walls_a = [p for p in a.primitives if not p.material.startswith("roof")]
    assert walls_a


def test_posts_lean_but_never_into_openings_or_outside():
    window = Opening("window", 4.0, 1.0, 0.8, 1.0)
    segs = timber_segments(12.0, 2.7, [window], RULES, RULES.pattern("einfach"))
    st = assign_style(house(), aged(1.0), STREET, RULES)
    leaned = _lean_posts(segs, 12.0, [window], RULES, random.Random(3), st)
    b = RULES.data["timber"]["beamM"]
    moved = 0
    for (kind, _, p0, p1), (_, _, q0, q1) in zip(segs, leaned, strict=True):
        assert np.allclose(p0, q0)
        if kind != "post":
            assert np.allclose(p1, q1)
            continue
        if not np.allclose(p1, q1):
            moved += 1
            assert b / 2 <= q1[0] <= 12.0 - b / 2
            inside_x = window.u - b / 2 < q1[0] < window.u + window.w + b / 2
            inside_y = window.v - b / 2 < q1[1] < window.v + window.h + b / 2
            assert not (inside_x and inside_y)
        if p0[0] < b or p0[0] > 12.0 - b:
            assert np.allclose(p1, q1)  # corner posts stay upright
    assert moved > 0
    stone = assign_style(house(), aged(1.0, style="steinhaus"), STREET, RULES)
    assert _lean_posts(segs, 12.0, [window], RULES, random.Random(3), stone) == segs


def test_windows_irregular_only_when_old_and_never_for_annotations():
    front = {
        "edge": 0,
        "openings": [{"storey": 1, "type": "window", "x": 1.0, "w": 0.7, "h": 0.9, "y": 0.8}],
    }
    young = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, aged(0.0))
    old = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, aged(1.0))
    fy = next(p for p in young.primitives if p.material == "frame").mesh.positions
    fo = next(p for p in old.primitives if p.material == "frame").mesh.positions
    assert fy.shape == fo.shape and not np.allclose(fy, fo)  # same windows, a little irregular
    ann = from_json({"id": "H", "style": "handwerkerhaus", "age": 1.0, "frontFacade": front})
    r = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, ann)
    frame = next(p for p in r.primitives if p.material == "frame").mesh.positions
    south = frame[np.abs(frame[:, 2] - (3.5 - RULES.data["openings"]["revealM"])) < 1e-3]
    assert south[:, 0].min() == pytest.approx(1.0 - 6.0, abs=1e-3)  # annotated x exactly


def test_stone_buildings_barely_sag():
    church = build_house(house(), -0.3, (6.0, -3.5), RULES, STREET, aged(1.0, style="kirche"))
    expected = AGING["ridgeSagMaxRatio"] * 12.0 * AGING["stoneSagFactor"]
    assert church.sag_m == pytest.approx(expected, rel=0.05)
