import json
import math
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.assemble.world import VobIds, assemble
from gothar_worldgen.buildings.batch import generate
from gothar_worldgen.buildings.medieval import StreetIndex, load_rules
from gothar_worldgen.buildings.rueckbau import Protection, apply, select, split_building
from gothar_worldgen.facade.overrides import OverrideError, from_json, to_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
RB = RULES.data["rueckbau"]
STREET = StreetIndex([{"points": [[-100.0, 6.0], [200.0, 6.0]]}])  # south of z = 0


def block(bid: str, x: float, width: float, depth: float, eave: float = 9.0) -> dict:
    """Rectangle with its south side (z = 0) on the street, counter-clockwise north-up."""
    fp = [[x, 0.0], [x + width, 0.0], [x + width, -depth], [x, -depth]]
    return {
        "id": bid,
        "inCore": True,
        "footprint": fp,
        "groundY": 0.0,
        "roof": {"type": "flat", "eaveY": eave, "ridgeY": eave},
    }


def test_rules_section_is_marked_as_decided():
    assert RB["status"].startswith("festgelegt") and "2026-10-03" in RB["status"]
    assert (RB["select"]["areaM2"], RB["select"]["lengthM"], RB["select"]["eaveM"]) == (
        200.0,
        22.0,
        12.0,
    )
    assert RB["ridge"]["gableToStreet"] == 0.7 and RB["pitchDeg"] == 50.0


def test_select_thresholds_reasons_and_protection():
    blds = [
        block("SMALL", 0, 8, 9),
        block("AREA", 20, 18, 14),
        block("LONG", 50, 24, 6),
        block("TALL", 80, 8, 9, eave=13.0),
        block("CHURCH", 100, 30, 15),
        block("WALL", 140, 30, 10),
        block("LOCK", 180, 30, 10),
        block("NONE", 220, 30, 10),
        block("FORCE", 260, 6, 6),
        block("TOUCH", 300, 30, 10),
        {**block("HISTWALL", 340, 60, 2), "function": "51007_1510"},
    ]
    features = [
        {
            "kind": "place_of_worship",
            "geometry": "polygon",
            "name": "Kirche",
            "polygon": [[101, -1], [129, -1], [129, -14], [101, -14]],
        },
        {"kind": "city_wall", "geometry": "line", "points": [[140, -5], [170, -5]]},
        {"kind": "city_wall", "geometry": "line", "points": [[300, -10], [330, -10]]},
        {
            "kind": "memorial",
            "geometry": "point",
            "position": [4, -4],
            "tags": {"historic": "memorial"},
        },
    ]
    ov = {
        "LOCK": from_json({"id": "LOCK", "locked": True}),
        "NONE": from_json({"id": "NONE", "rueckbau": "none"}),
        "FORCE": from_json({"id": "FORCE", "rueckbau": "split"}),
    }
    sel = select(blds, RB, ov, Protection(features), over_budget=frozenset({"SMALL"}))
    assert set(sel.selected) == {
        "AREA",
        "LONG",
        "TALL",
        "FORCE",
        "TOUCH",
    }  # touching: not protected
    assert sel.selected["AREA"] == ["area 252 m2"] and sel.selected["TALL"] == ["eave 13.0 m"]
    assert sel.protected == {
        "CHURCH": "OSM place_of_worship Kirche",
        "WALL": "OSM city_wall",
        "LOCK": "locked",
        "NONE": "override rueckbau: none",
        "SMALL": "OSM memorial",
        "HISTWALL": "ALKIS historic structure 51007_1510",
    }
    assert {"SMALL", "CHURCH"} <= sel.guarded  # protected also against the budget split


def test_split_into_parcels_along_the_street():
    b = block("B", 0.0, 35.0, 10.0)
    res = split_building(b, RB, STREET)
    houses = res.houses
    assert [h["id"] for h in houses] == [f"B-T{i}" for i in range(1, len(houses) + 1)]
    widths = []
    for h in houses:
        xs = [p[0] for p in h["footprint"]]
        widths.append(max(xs) - min(xs))
    assert 4 <= len(houses) <= 6
    assert all(RB["parcels"]["widthM"] - 2.5 <= w <= RB["parcels"]["widthM"] + 2.5 for w in widths)
    polys = [Polygon(h["footprint"]) for h in houses]
    assert sum(p.area for p in polys) == pytest.approx(350.0, abs=0.5)
    assert all(a.intersection(c).area < 1e-3 for i, a in enumerate(polys) for c in polys[i + 1 :])
    lefts = [min(p[0] for p in h["footprint"]) for h in houses]
    assert lefts == sorted(lefts)  # T1 is on the left seen from the street (west)
    for h in houses:
        roof, st = h["roof"], RB["storeys"]
        assert roof["type"] == "saddle" and h["derivedFrom"] == "B" and h["rueckbau"] == "front"
        assert roof["eaveY"] <= st["groundM"] + (RB["storeysMax"] - 1) * st["upperM"] + 1e-6
        assert (
            RB["pitchDeg"] - RB["pitchSpreadDeg"]
            <= roof["pitchDeg"]
            <= RB["pitchDeg"] + RB["pitchSpreadDeg"]
        )
        assert roof["ridgeY"] > roof["eaveY"]
    assert split_building(b, RB, STREET).houses == houses  # deterministic


def test_deep_parcels_get_a_rear_house_or_a_yard():
    rear = split_building(block("D", 0.0, 7.0, 24.0), RB, STREET)  # 10 m behind the 14 m front
    assert [h["id"] for h in rear.houses] == ["D-T1", "D-H1"]
    front = Polygon(rear.houses[0]["footprint"])
    assert max(-p[1] for p in front.exterior.coords) == pytest.approx(RB["parcels"]["maxDepthM"])
    assert rear.houses[1]["roof"]["eaveY"] <= 3.0 + 2.7 + 1e-6
    yard = split_building(block("Y", 0.0, 7.0, 18.0), RB, STREET)  # 4 m rest: yard
    assert [h["id"] for h in yard.houses] == ["Y-T1"] and yard.yard_m2 == pytest.approx(
        28.0, abs=0.5
    )


def test_ridge_mix_is_mostly_gable_to_street():
    gable = total = 0
    for i in range(60):
        for h in split_building(block(f"M{i}", 0.0, 21.0, 9.0), RB, STREET).houses:
            total += 1
            gable += (
                abs(h["roof"]["ridgeDir"][1]) > 0.9
            )  # ridge perpendicular to the street (along z)
    assert 0.55 < gable / total < 0.85


def test_apply_report():
    blds = [block("KEEP", 0, 8, 8), block("BIG", 20, 30, 10)]
    sel = select(blds, RB, {}, Protection([]))
    out, report = apply(blds, sel, RB, STREET)
    assert [b["id"] for b in out][0] == "KEEP" and all(b["id"].startswith("BIG-T") for b in out[1:])
    assert report["stats"]["replaced"] == 1 and report["stats"]["newHouses"] == len(out) - 1
    assert report["selected"][0]["reasons"] == ["area 300 m2", "length 30.0 m"]


def test_budget_replacement_in_the_batch(tmp_path: Path):
    from gothar_worldgen.buildings.medieval import Rules

    tight = Rules({**RULES.data, "budget": {"trianglesPerBuilding": 900}})
    big = block("WIDE", 0.0, 20.0, 9.0)
    big["roof"] = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
    calls = []

    def replace(b: dict) -> list[dict]:
        calls.append(b["id"])
        return split_building(b, RB, STREET).houses

    res = generate(
        [big], None, tmp_path, "v", mode="medieval", rules=tight, streets=STREET, replace=replace
    )
    ids = [e["id"] for e in res.index["entries"]]
    assert calls == ["WIDE"] and res.replaced == [("WIDE", len(ids))]
    assert all(i.startswith("WIDE-T") for i in ids)  # derived houses are not split again


def test_override_key_rueckbau():
    o = from_json({"id": "A", "rueckbau": "split"})
    assert o.rueckbau == "split" and to_json(o) == {"id": "A", "keep": True, "rueckbau": "split"}
    with pytest.raises(OverrideError, match="rueckbau"):
        from_json({"id": "A", "rueckbau": "maybe"})


def test_assembler_reserves_the_original_id():
    terrain = {
        "version": 1,
        "terrain": {
            "version": 1,
            "heightmap": "t.r16",
            "width": 2,
            "height": 2,
            "cellSize": 1.0,
            "firstSample": [0.0, 0.0],
            "minY": 0.0,
            "maxY": 1.0,
        },
    }
    ids = VobIds({}, 1)
    w1 = assemble(
        terrain,
        {"entries": [{"id": "BIG", "kind": "building", "mesh": "m", "pos": [0, 0, 0]}]},
        None,
        ids,
        "t",
    ).world
    big_id = next(v["id"] for v in w1["vobs"] if v["name"] == "BLD_BIG")
    idx = {
        "entries": [
            {"id": f"BIG-T{i}", "kind": "building", "mesh": "m", "pos": [i * 7.0, 0, 0]}
            for i in (1, 2)
        ]
    }
    w2 = assemble(terrain, idx, w1, ids, "t").world
    names = {v["name"]: v["id"] for v in w2["vobs"]}
    assert "BLD_BIG" not in names and ids.ids["building:BIG"] == big_id
    assert min(names["BLD_BIG-T1"], names["BLD_BIG-T2"]) > big_id
    assert json.dumps(w2)  # serialisable
    assert math.isfinite(np.float64(w2["nextVobId"]))


def test_deep_plots_get_several_rear_houses_and_no_giant_roofs():
    rb = RULES.data["rueckbau"]
    # 10 m along the street, 50 m deep: front house plus rear strips of at most maxDepthM
    deep = {"id": "D1", "groundY": 0.0, "footprint": [[0, 0], [10, 0], [10, -50], [0, -50]],
            "roof": {"type": "flat", "eaveY": 12.0, "ridgeY": 12.0}}  # fmt: skip
    street = StreetIndex([{"points": [[-20.0, 5.0], [30.0, 5.0]]}])  # the short side faces it
    res = split_building(deep, rb, street)
    rear = [h for h in res.houses if h["rueckbau"] == "rear"]
    assert len(rear) >= 2
    for h in res.houses:
        ys = np.asarray(h["footprint"])[:, 1]
        assert np.ptp(ys) <= rb["parcels"]["maxDepthM"] + 1e-6 or h["rueckbau"] == "front"
        assert h["roof"]["ridgeY"] - h["roof"]["eaveY"] <= rb["maxRiseM"] + 1e-6


def test_wide_original_roofs_are_capped_but_not_flatter_than_the_minimum():
    from gothar_worldgen.buildings.massing import Mass
    from gothar_worldgen.buildings.medieval import cap_rise

    wide = Mass(((0.0, 0.0), (30.0, 0.0), (30.0, -24.0), (0.0, -24.0)), 10.0, 40.0, "saddle",
                (1.0, 0.0))  # fmt: skip
    capped = cap_rise(wide, RULES)
    p = RULES.data["roofPitch"]
    rise = capped.ridge_y - capped.eave_y
    assert rise == pytest.approx(max(p["maxRiseM"], math.tan(math.radians(p["minDeg"])) * 12.0))
    normal = Mass(((0.0, 0.0), (10.0, 0.0), (10.0, -7.0), (0.0, -7.0)), 8.0, 12.0, "saddle",
                  (1.0, 0.0))  # fmt: skip
    assert cap_rise(normal, RULES) is None
