import json
import math
from pathlib import Path

import pytest
from shapely.geometry import LineString, Point, Polygon, box

from gothar_worldgen.outdoor import OutdoorError, Rules, Site, footprint, plan_outdoor

REPO = Path(__file__).resolve().parents[3]
RULES = Path(__file__).resolve().parents[1] / "data" / "leonberg" / "outdoor.json"

# two houses on the north side of an east-west street (axis z = -5, 6 m wide: edge at z = -2)
HOUSE_A = box(0.0, 0.0, 10.0, 8.0)  # the tavern, door in the middle of its street wall
HOUSE_B = box(20.0, 0.0, 28.0, 8.0)  # a plain house
DOOR_A = [5.0, 0.0, 0.0, "ground", 0.0, -1.0]
STREET = {"highway": "residential", "widthM": 6.0, "points": [[-20.0, -5.0], [40.0, -5.0]]}


def _rules(**changes) -> Rules:
    data = json.loads(RULES.read_text(encoding="utf-8"))
    for path, value in changes.items():
        node = data
        keys = path.split("__")
        for k in keys[:-1]:
            node = node[k]
        node[keys[-1]] = value
    return Rules(data)


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
    )
    spec.update(over)
    return Site(**spec)


def _foot(v) -> Polygon:
    """The vob's footprint (model width along its x, depth along its z)."""
    kind = Path(v["mesh"]).stem
    w, d = footprint(kind)
    qy, qw = v["rot"][1], v["rot"][3]
    a = 2 * math.atan2(qy, qw)
    fx, fz = math.sin(a), math.cos(a)  # the model's +Z
    ux, uz = fz, -fx  # its +X
    x, _, z = v["pos"]
    return Polygon([(x + sx * w / 2 * ux + sy * d / 2 * fx, z + sx * w / 2 * uz + sy * d / 2 * fz)
                    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])  # fmt: skip


def _props(plan):
    return [v for v in plan.vobs if v["mesh"].startswith("props/")]


def test_props_stand_beside_the_houses_clear_of_street_and_door():
    plan = plan_outdoor(_rules(), _site())
    props = _props(plan)
    assert props
    corridor = LineString(STREET["points"]).buffer(max(1.2, 6.0 / 2 - 0.9))
    lane = box(5.0 - 0.9, -2.2, 5.0 + 0.9, 0.0)  # doorLane 1.8 x 2.2 before the door
    for v in props:
        foot = _foot(v)
        assert not foot.intersects(corridor), v["name"]
        assert not foot.intersects(lane), v["name"]
        assert not foot.intersects(HOUSE_A) and not foot.intersects(HOUSE_B), v["name"]
        near = min(HOUSE_A.distance(foot), HOUSE_B.distance(foot))
        assert near < 0.1, v["name"]  # against a wall


def test_the_tavern_gets_its_things_at_the_street_facing_away_from_the_wall():
    plan = plan_outdoor(_rules(), _site())
    mine = [v for v in _props(plan) if v["key"].startswith("outdoor:A:")]
    want = _rules().data["uses"]["gasthaus"]["front"]
    assert sorted(Path(v["mesh"]).stem for v in mine) == sorted(want)
    assert not plan.failed
    for v in mine:
        x, _, z = v["pos"]
        if z < 0:  # at the street wall: the front looks south, onto the street
            qy, qw = v["rot"][1], v["rot"][3]
            a = 2 * math.atan2(qy, qw)
            assert (math.sin(a), math.cos(a)) == pytest.approx((0.0, -1.0), abs=1e-3)


def test_a_group_stands_together():
    # without the door's lane splitting the wall the tavern's four things stand side by side
    plan = plan_outdoor(_rules(), _site(entries=[{"id": "A", "doors": []}, {"id": "B"}]))
    mine = [v for v in _props(plan) if v["key"].startswith("outdoor:A:front")]
    xs = sorted(v["pos"][0] for v in mine)
    gaps = [b - a for a, b in zip(xs, xs[1:], strict=False)]
    assert gaps and max(gaps) <= 3.5  # groupM 3 plus the widths


def test_plan_is_deterministic():
    a = plan_outdoor(_rules(), _site())
    b = plan_outdoor(_rules(), _site())
    assert a.vobs == b.vobs and a.counts == b.counts


def test_the_waynet_and_routine_places_stay_free():
    ways = [((-20.0, -1.0), (40.0, -1.0))]  # an edge right along the house fronts
    places = [(24.0, 9.0)]  # a routine place behind house B
    plan = plan_outdoor(_rules(), _site(ways=ways, places=places))
    edge = LineString(ways[0]).buffer(1.2)
    disc = Point(places[0]).buffer(1.3)
    for v in plan.vobs:
        foot = _foot(v) if v["mesh"].startswith("props/") else Point(v["pos"][0], v["pos"][2])
        assert not foot.intersects(edge) and not foot.intersects(disc), v["name"]
    # the tavern's things went to its other walls instead (yard and sides)
    tavern = [v for v in _props(plan) if v["key"].startswith("outdoor:A:")]
    assert tavern and all(v["key"].startswith("outdoor:A:back") for v in tavern)
    assert all(v["pos"][2] > 0.0 for v in tavern)


def test_trees_keep_off_the_street_and_the_walls():
    plan = plan_outdoor(_rules(), _site())
    trees = [v for v in plan.vobs if Path(v["mesh"]).stem.startswith("tree_")]
    corridor = LineString(STREET["points"]).buffer(2.1)
    for v in trees:
        trunk = Point(v["pos"][0], v["pos"][2])
        assert not trunk.buffer(0.6).intersects(corridor)
        assert min(HOUSE_A.distance(trunk), HOUSE_B.distance(trunk)) >= 2.0
    keys = {v["key"] for v in trees}
    assert "outdoor:tree:n2" in keys  # the one in the yard
    assert "outdoor:tree:n1" not in keys or plan.counts["tree (no room)"] == 0


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
    data = json.loads(RULES.read_text(encoding="utf-8"))
    data["uses"]["gasthaus"]["front"].append("statue")
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
    plan = plan_outdoor(_rules(grass__chance=1.0), site)
    keys = [v["key"] for v in plan.vobs]
    names = [v["name"] for v in plan.vobs]
    assert len(set(keys)) == len(keys) and len(set(names)) == len(names)
    assert any(n.startswith("GASSE_Zh_") for n in names) and any(
        n.startswith("GASSE_ZH_") for n in names
    )
    assert any(n.startswith("GRAS_T_1_") for n in names)  # the second part's edges
