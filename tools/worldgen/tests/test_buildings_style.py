from collections import Counter
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.medieval import (
    Opening,
    StreetIndex,
    assign_style,
    build_house,
    load_rules,
    timber_segments,
)
from gothar_worldgen.buildings.rueckbau import Protection, select
from gothar_worldgen.facade.overrides import from_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
FP = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
MAIN = {"highway": "secondary", "points": [[-50.0, 6.0], [60.0, 6.0]]}
LANE = {"highway": "residential", "points": [[-50.0, 30.0], [60.0, 30.0]]}
MARKT = {"name": "Marktplatz", "polygon": [[0, 2], [10, 2], [10, 12], [0, 12]]}
CHURCH = {
    "kind": "place_of_worship",
    "geometry": "polygon",
    "polygon": [[100, 0], [120, 0], [120, -20], [100, -20]],
}
CASTLE = {
    "kind": "castle",
    "geometry": "polygon",
    "polygon": [[200, 0], [220, 0], [220, -20], [200, -20]],
}
SITE = StreetIndex([MAIN, LANE], [], [CHURCH, CASTLE])
SITE_MARKT = StreetIndex([LANE], [MARKT], [])


def house(bid: str = "H", fp=FP, **extra) -> dict:
    roof = {
        "type": "saddle",
        "eaveY": 8.4,
        "ridgeY": 13.0,
        "ridgeDir": [1.0, 0.0],
        "pitchDeg": 53.0,
    }
    return {"id": bid, "inCore": True, "groundY": 0.0, "footprint": fp, "roof": roof, **extra}


def moved(dx: float, dz: float) -> list:
    return [[x + dx, z + dz] for x, z in FP]


@pytest.mark.parametrize(
    ("building", "site", "style"),
    [
        (house(function="51007_1510"), SITE, "mauer"),
        (house(fp=moved(105, -5)), SITE, "kirche"),
        (house(function="31001_3041", fp=moved(0, -200)), SITE, "kirche"),
        (house(fp=moved(205, -5)), SITE, "steinhaus"),
        (house(function="31001_3012"), SITE, "amtshaus"),
        (house(function="31001_2463"), SITE, "scheune"),
        (house(function="31001_2721"), SITE, "scheune"),
        (house(function="31001_1010"), SITE, "buergerhaus"),  # 70 m2 on the main street
        (house(function="31001_1010", fp=moved(0, 60)), SITE, "handwerkerhaus"),  # back lane
        (house(function="31001_1010", fp=moved(240, 60)), SITE, "ackerbuergerhaus"),  # town edge
        (house(derivedFrom="X"), SITE, "buergerhaus"),
        (house(derivedFrom="X", fp=moved(0, 60)), SITE, "handwerkerhaus"),
    ],
)
def test_style_assignment(building, site, style):
    assert assign_style(building, None, site, RULES).style == style


def test_market_square_houses_are_representative():
    st = assign_style(house(function="31001_1010"), None, SITE_MARKT, RULES)
    assert st.style == "buergerhaus" and st.massive_ground and st.jetty
    derived = assign_style(house(derivedFrom="X"), None, SITE_MARKT, RULES)
    assert derived.style == "buergerhaus" and derived.massive_ground
    townhall = assign_style(house(function="31001_3012"), None, SITE_MARKT, RULES)
    assert townhall.style == "amtshaus" and townhall.brustung == "raute"


def test_frequencies_follow_the_rules():
    prof = RULES.data["styles"]["handwerkerhaus"]
    n = 1500
    patterns, infill, roofs = Counter(), Counter(), Counter()
    for i in range(n):
        st = assign_style(house(f"H{i}", function="31001_1010", fp=moved(0, 60)), None, SITE, RULES)
        patterns[st.pattern] += 1
        infill[st.infill] += 1
        roofs[st.roof] += 1
    for name, share in prof["patterns"].items():
        assert patterns[name] / n == pytest.approx(share, abs=0.04)
    for name, share in prof["infill"].items():
        assert infill[name] / n == pytest.approx(share, abs=0.04)
    for name, share in prof["roof"].items():
        assert roofs[name] / n == pytest.approx(share, abs=0.04)


def test_raute_is_only_an_ornament_and_overrides_win():
    seen = Counter()
    for i in range(400):
        st = assign_style(house(f"B{i}"), None, SITE, RULES)  # Bürgerhaus: 5 % raute
        seen[(st.pattern, st.brustung)] += 1
    assert ("raute", None) not in seen and seen[("einfach", "raute")] > 0
    o = from_json(
        {
            "id": "H",
            "roofCover": "tiles_red",
            "frontFacade": {"edge": 0, "openings": [], "timber": "andreaskreuz", "infill": "lehm"},
        }
    )
    st = assign_style(house(), o, SITE, RULES)
    assert (st.pattern, st.infill, st.roof) == ("andreaskreuz", "lehm", "roof_red")
    assert assign_style(house(), from_json({"id": "H", "style": "scheune"}), SITE, RULES).gate


def test_no_thatch_inside_the_wall():
    o = from_json({"id": "S", "style": "scheune", "roofCover": "thatch"})
    assert assign_style(house("S"), o, SITE, RULES).roof == "roof_old"
    assert assign_style({**house("S"), "inCore": False}, o, SITE, RULES).roof == "roof_thatch"


def materials(result) -> Counter:
    out = Counter()
    for p in result.primitives:
        out[p.material] += p.mesh.triangle_count
    return out


def test_moss_only_on_north_roof_faces():
    o = from_json({"id": "H", "style": "handwerkerhaus", "roofCover": "tiles_old"})
    r = build_house(house(), -0.3, (5.0, -3.5), RULES, SITE, o)
    m = {p.material: p for p in r.primitives}
    north, south = m["roof_old_moss"].mesh, m["roof_old"].mesh
    up = north.normals[:, 1] > 0.1
    assert np.all(north.normals[up][:, 2] < 0)  # mossy slope faces north (-Z)
    assert np.any(south.normals[south.normals[:, 1] > 0.1][:, 2] > 0)  # the other slope south


def test_socle_and_massive_ground_storey():
    timber = build_house(
        house(), -0.3, (5.0, -3.5), RULES, SITE, from_json({"id": "H", "style": "handwerkerhaus"})
    )
    stone = next(p for p in timber.primitives if p.material == "stone").mesh.positions
    assert stone[:, 1].max() <= 0.3 + RULES.data["socleM"] + 1e-3  # only the socle band is stone
    beams = np.concatenate(
        [p.mesh.positions for p in timber.primitives if p.material.startswith("timber")]
    )
    assert beams[:, 1].min() < 3.0  # timber already in the ground storey
    mauer = build_house(house(function="51007_1510"), -0.3, (5.0, -3.5), RULES, SITE)
    assert set(materials(mauer)) <= {"stone", "roof_old", "roof_old_moss"}  # no openings, no timber


def test_low_roofs_become_steep_saddles():
    flat = {**house(), "roof": {"type": "flat", "eaveY": 8.4, "ridgeY": 8.4}}
    r = build_house(
        flat, -0.3, (5.0, -3.5), RULES, SITE, from_json({"id": "H", "style": "handwerkerhaus"})
    )
    assert r.steepened == 1
    roofs = np.concatenate(
        [p.mesh.positions for p in r.primitives if p.material.startswith("roof")]
    )
    rise = roofs[:, 1].max() - (8.4 + 0.3)  # local y = world y - base (-0.3)
    half = 3.5 + RULES.data["roof"]["overhangM"]
    pitch = np.degrees(np.arctan(rise / 3.5))
    assert 49.0 < pitch < 57.0 and half > 0  # 50-55 deg plus the roof thickness
    steep = build_house(house(), -0.3, (5.0, -3.5), RULES, SITE)
    assert steep.steepened == 0


def test_brustung_ornament_stays_below_the_window():
    window = Opening("window", 4.0, 1.0, 0.8, 1.0)
    segs = timber_segments(
        10.0, 2.7, [window], RULES, RULES.pattern("einfach"), brustung=RULES.pattern("raute")
    )
    ornament = [(p0, p1) for kind, k, p0, p1 in segs if kind == "brace" and k >= 1]
    assert ornament
    assert all(max(p0[1], p1[1]) <= window.v + 1e-6 for p0, p1 in ornament)


def test_barn_gets_a_gate_on_the_street():
    r = build_house(house(function="31001_2721"), -0.3, (5.0, -3.5), RULES, SITE)
    frame = next(p for p in r.primitives if p.material == "frame").mesh.positions
    panel = frame[np.abs(frame[:, 2] - (3.5 - RULES.data["openings"]["revealM"])) < 1e-3]
    assert panel[:, 0].max() - panel[:, 0].min() >= 2.0  # a wide gate panel on the south side


def test_rueckbau_spares_steep_historic_roofs_and_the_old_town_hall():
    rb = RULES.data["rueckbau"]
    big = [[0, 0], [20, 0], [20, -12], [0, -12]]  # 240 m2: over the area threshold
    steep = {**house("STEEP", fp=big)}
    low = {
        **house("LOW", fp=big),
        "roof": {"type": "saddle", "eaveY": 8.4, "ridgeY": 10.0, "pitchDeg": 25.0},
    }
    old_hall = {**house("OLDHALL", fp=big), "function": "31001_3012"}
    new_hall = {
        **house("NEWHALL", fp=big),
        "function": "31001_3012",
        "roof": {"type": "flat", "eaveY": 20.0, "ridgeY": 20.0},
    }
    sel = select([steep, low, old_hall, new_hall], rb, {}, Protection([]), frozenset({"STEEP"}))
    assert set(sel.selected) == {"LOW", "NEWHALL"}
    assert sel.protected["STEEP"].startswith("historic steep roof 53")
    assert sel.protected["OLDHALL"] == "ALKIS 31001_3012 (town hall)"
    assert "STEEP" in sel.guarded  # not split for the triangle budget either
