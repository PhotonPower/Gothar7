import json
import math
from pathlib import Path

import pytest
from shapely.geometry import LineString, Point, Polygon, box

from gothar_worldgen.outdoor import OutdoorError, Rules, Site, footprint, plan_outdoor

RULES = Path(__file__).resolve().parents[1] / "data" / "leonberg" / "outdoor.json"

# two houses on the north side of an east-west street (axis z = -5, 6 m wide: edge at z = -2)
HOUSE_A = box(0.0, 0.0, 10.0, 8.0)  # the tavern, door in the middle of its street wall
HOUSE_B = box(20.0, 0.0, 28.0, 8.0)  # a plain house
DOOR_A = [5.0, 0.0, 0.0, "ground", 0.0, -1.0]
STREET = {"highway": "residential", "widthM": 6.0, "points": [[-20.0, -5.0], [40.0, -5.0]],
          "osmId": "w1"}  # fmt: skip
SQUARE = {"name": "Marktplatz", "polygon": [[-60.0, 20.0], [-20.0, 20.0], [-20.0, 50.0],
                                             [-60.0, 50.0]]}  # fmt: skip


def _data() -> dict:
    return json.loads(RULES.read_text(encoding="utf-8"))


def _rules(kerb: bool = False, market: bool = False, **changes) -> Rules:
    """The site's rules; street edge and market off unless a test is about them."""
    data = _data()
    if not kerb:
        data.pop("kerb", None)
    if not market:
        data.pop("market", None)
    for path, value in changes.items():
        node = data
        keys = path.split("__")
        for k in keys[:-1]:
            node = node[k]
        node[keys[-1]] = value
    return Rules(data)


def _corridor() -> Polygon:
    keep = _data()["keep"]
    half = max(keep["minCorridorM"], STREET["widthM"] / 2 - keep["streetMarginM"])
    return LineString(STREET["points"]).buffer(half)


def _site(**over) -> Site:
    spec = dict(
        entries=[{"id": "A", "doors": [DOOR_A]}, {"id": "B", "doors": []}],
        uses={"A": "gasthaus"},
        places=[],
        bodies=[("BLD_A", HOUSE_A), ("BLD_B", HOUSE_B)],
        streets=[STREET],
        features=[
            {"osmId": "n1", "kind": "tree", "geometry": "point", "position": [14.0, -4.5]},
            {"osmId": "n2", "kind": "tree", "geometry": "point", "position": [15.0, 20.0]},
        ],
        height_at=lambda x, z: 0.0,
        squares=[SQUARE],
    )
    spec.update(over)
    return Site(**spec)


def _front(v) -> tuple[float, float]:
    qy, qw = v["rot"][1], v["rot"][3]
    a = 2 * math.atan2(qy, qw)
    return math.sin(a), math.cos(a)  # the model's +Z


def _foot(v) -> Polygon:
    """The vob's footprint (model width along its x, depth along its z)."""
    w, d = footprint(Path(v["mesh"]).stem)
    fx, fz = _front(v)
    ux, uz = fz, -fx  # its +X
    x, _, z = v["pos"]
    return Polygon([(x + sx * w / 2 * ux + sy * d / 2 * fx, z + sx * w / 2 * uz + sy * d / 2 * fz)
                    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])  # fmt: skip


def _props(plan, prefix: str = "outdoor:"):
    return [v for v in plan.vobs if v["mesh"].startswith("props/") and v["key"].startswith(prefix)]


def _expanded(names: list[str]) -> list[str]:
    groups = _data()["groups"]
    return [k for n in names for k in groups.get(n, [n])]


def test_props_stand_at_the_walls_clear_of_street_and_door():
    plan = plan_outdoor(_rules(), _site())
    props = _props(plan)
    assert props
    corridor = _corridor()
    lane = box(5.0 - 0.9, -2.2, 5.0 + 0.9, 0.0)  # doorLane 1.8 x 2.2 before the door
    for v in props:
        foot = _foot(v)
        assert not foot.intersects(corridor), v["name"]
        assert not foot.intersects(lane), v["name"]
        assert not foot.intersects(HOUSE_A) and not foot.intersects(HOUSE_B), v["name"]
        near = min(HOUSE_A.distance(foot), HOUSE_B.distance(foot))
        assert near < 0.25, v["name"]  # against a wall (12 cm off the simplified outline)


def test_no_props_inside_an_enterable_house():
    """Its collision: pieces of wall round the room, open at the door and between them (W7)."""
    walls = [box(20.0, 7.7, 25.0, 8.0), box(20.0, 0.0, 20.3, 3.0), box(27.7, 4.0, 28.0, 8.0),
             box(20.0, 0.0, 23.5, 0.3), box(24.5, 0.0, 28.0, 0.3)]  # gaps up to 3 m  # fmt: skip
    room = box(20.3, 0.3, 27.7, 7.7)
    town_wall = box(26.0, -1.0, 26.6, 12.0)  # through the room: no grass at its foot inside
    bodies = [("BLD_A", HOUSE_A), *(("BLD_B", w) for w in walls), ("CITYWALL_1", town_wall)]
    door_b = [24.0, 0.0, 0.0, "ground", 0.0, -1.0]
    entries = [
        {"id": "A", "doors": [DOOR_A]},
        {"id": "B", "doors": [door_b], "interior": {"ring": list(room.exterior.coords)[:-1]}},
    ]
    site = _site(bodies=bodies, entries=entries, uses={"A": "gasthaus", "B": "gasthaus"})
    plan = plan_outdoor(_rules(grass__chance=1.0), site)
    props = _props(plan, "outdoor:B:")
    assert props  # its groups stand outside
    for v in props:
        assert not _foot(v).intersects(room), v["name"]
    grass = [v for v in plan.vobs if v["name"].startswith("GRAS_")]
    assert grass and not any(room.contains(Point(v["pos"][0], v["pos"][2])) for v in grass)


def test_the_tavern_gets_its_groups_facing_away_from_the_wall():
    plan = plan_outdoor(_rules(), _site())
    mine = _props(plan, "outdoor:A:")
    want = _expanded(_data()["uses"]["gasthaus"]["front"])
    assert sorted(Path(v["mesh"]).stem for v in mine) == sorted(want)
    assert not plan.failed
    for v in mine:
        if v["pos"][2] < 0:  # at the street wall: the front looks south, onto the street
            assert _front(v) == pytest.approx((0.0, -1.0), abs=1e-3)


def test_a_group_stands_side_by_side():
    # without the door's lane splitting the wall the first group stands in one row
    plan = plan_outdoor(_rules(), _site(entries=[{"id": "A", "doors": []}, {"id": "B"}]))
    first = _props(plan, "outdoor:A:front:1:1:")
    assert [Path(v["mesh"]).stem for v in first] == _expanded(["faesser"])
    feet = [_foot(v) for v in first]
    gap = _data()["groupGapM"]
    for a, b in zip(feet, feet[1:], strict=False):
        assert a.distance(b) == pytest.approx(gap, abs=0.02)  # neighbours, no overlap


def test_plan_is_deterministic():
    a = plan_outdoor(_rules(kerb=True, market=True), _site())
    b = plan_outdoor(_rules(kerb=True, market=True), _site())
    assert a.vobs == b.vobs and a.counts == b.counts


def test_the_waynet_and_routine_places_stay_free():
    ways = [((-20.0, -1.0), (40.0, -1.0))]  # an edge right along the house fronts
    places = [(24.0, 9.0)]  # a routine place behind house B
    plan = plan_outdoor(_rules(kerb=True), _site(ways=ways, places=places))
    edge = LineString(ways[0]).buffer(_data()["keep"]["waynetM"])
    disc = Point(places[0]).buffer(1.3)
    for v in plan.vobs:
        foot = _foot(v) if v["mesh"].startswith("props/") else Point(v["pos"][0], v["pos"][2])
        assert not foot.intersects(edge) and not foot.intersects(disc), v["name"]
    # the tavern's things went to its other walls instead (yard and sides)
    tavern = _props(plan, "outdoor:A:")
    assert tavern and all(v["pos"][2] > 0.0 for v in tavern)


def test_street_edge_groups_face_the_street_outside_its_corridor():
    plan = plan_outdoor(_rules(kerb=True, kerb__chance=1.0), _site())
    kerb = _props(plan, "outdoor:kerb:")
    assert kerb
    surface = LineString(STREET["points"]).buffer(STREET["widthM"] / 2)
    for v in kerb:
        foot = _foot(v)
        assert not foot.intersects(surface), v["name"]  # beside the street, never on it
        assert not foot.intersects(HOUSE_A) and not foot.intersects(HOUSE_B), v["name"]
        x, _, z = v["pos"]
        fx, fz = _front(v)
        assert fz * (-5.0 - z) > 0, v["name"]  # looking onto the street
        assert v["name"].startswith("RAND_W1_")


def test_market_stalls_stand_on_the_square_facing_inwards():
    plan = plan_outdoor(_rules(market=True), _site())
    stalls = [v for v in plan.vobs if v["key"].startswith("outdoor:market:")]
    want = _data()["market"]["stalls"]
    square = Polygon(SQUARE["polygon"])
    assert 0 < len(stalls) <= want
    feet = [_foot(v) for v in stalls]
    c = square.centroid
    for v, foot in zip(stalls, feet, strict=True):
        assert Path(v["mesh"]).stem.startswith("market_stall_")
        assert square.contains(foot)
        x, _, z = v["pos"]
        fx, fz = _front(v)
        assert fx * (c.x - x) + fz * (c.y - z) > -0.5, v["name"]  # not with its back inwards
    for i, a in enumerate(feet):
        for b in feet[i + 1 :]:
            assert a.distance(b) >= _data()["market"]["gapM"] - 1e-6


def test_no_market_without_its_square():
    plan = plan_outdoor(_rules(market=True), _site(squares=[]))
    assert {"what": "market square", "house": "Marktplatz"} in plan.failed


def test_trees_keep_off_the_street_and_the_walls():
    plan = plan_outdoor(_rules(), _site())
    trees = [v for v in plan.vobs if Path(v["mesh"]).stem.startswith("tree_")]
    corridor = _corridor()
    for v in trees:
        trunk = Point(v["pos"][0], v["pos"][2])
        assert not trunk.buffer(0.6).intersects(corridor)
        assert min(HOUSE_A.distance(trunk), HOUSE_B.distance(trunk)) >= 2.0
    assert "outdoor:tree:n2" in {v["key"] for v in trees}  # the one in the yard


def test_grass_grows_at_the_feet_of_the_walls():
    plan = plan_outdoor(_rules(grass__chance=1.0), _site())
    grass = [v for v in plan.vobs if Path(v["mesh"]).stem in ("grass_tuft", "weeds")]
    assert grass
    for v in grass:
        p = Point(v["pos"][0], v["pos"][2])
        d = min(HOUSE_A.exterior.distance(p), HOUSE_B.exterior.distance(p))
        assert d == pytest.approx(0.18, abs=0.02)
        assert not HOUSE_A.contains(p) and not HOUSE_B.contains(p)


def test_unknown_models_are_refused(tmp_path: Path):
    data = _data()
    data["groups"]["faesser"].append("statue")
    path = tmp_path / "outdoor.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(OutdoorError, match="statue"):
        Rules.load(path)


def test_site_rules_load():
    rules = Rules.load(RULES)
    assert rules.keep["waynetM"] >= 1.0 and rules.keep["minCorridorM"] >= 1.0


def test_keys_and_names_are_unique():
    # a house of two parts (its edges are counted per part) and ids that differ in case only
    two = box(30.0, 0.0, 34.0, 6.0).union(box(36.0, 0.0, 40.0, 6.0))
    site = _site(
        entries=[{"id": "Zh", "doors": []}, {"id": "ZH", "doors": []}, {"id": "T", "doors": []}],
        uses={"Zh": "gasthaus", "ZH": "gasthaus"},
        bodies=[("BLD_Zh", HOUSE_A), ("BLD_ZH", HOUSE_B), ("BLD_T", two)],
    )
    plan = plan_outdoor(_rules(kerb=True, market=True, grass__chance=1.0), site)
    keys = [v["key"] for v in plan.vobs]
    names = [v["name"] for v in plan.vobs]
    assert len(set(keys)) == len(keys) and len(set(names)) == len(names)
    assert any(n.startswith("GASSE_Zh_") for n in names)
    assert any(n.startswith("GASSE_ZH_") for n in names)
    assert any(n.startswith("GRAS_T_1_") for n in names)  # the second part's edges


def test_paths_and_tracks_stay_free_over_their_whole_width():
    # a field track (3 m) passing right along house B's yard wall
    track = {"highway": "track", "widthM": 3.0, "points": [[15.0, 9.5], [35.0, 9.5]], "osmId": "w2"}
    plan = plan_outdoor(_rules(kerb=True, kerb__chance=1.0), _site(streets=[STREET, track]))
    surface = LineString(track["points"]).buffer(1.5)
    for v in _props(plan):
        assert not _foot(v).intersects(surface), v["name"]
    assert any(v["key"].startswith("outdoor:kerb:w2:") for v in plan.vobs)  # at its edge instead
