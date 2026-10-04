"""Inside the enterable houses (W7 C2): furniture, hearth, light, freepoints, waypoints, trigger."""

import math

import pytest
from shapely.geometry import LineString, Point, Polygon

from gothar_worldgen.uses.inside import BENCH_OFF_M, InsideSpec, inside_spec, plan_inside
from gothar_worldgen.uses.places import House, UsesError

# a room 9.4 x 6.4 m (a house 10 x 7 with 0.3 m walls), door in the south wall, facing out (+z)
ROOM = {"floor": 1.0, "ceiling": 3.9,
        "ring": [[0.3, -0.3], [0.3, -6.7], [9.7, -6.7], [9.7, -0.3]],
        "door": {"from": [5.5, -0.3], "to": [6.5, -0.3], "axis": [1.0, 0.0], "normal": [0.0, 1.0],
                 "floor": 1.0, "w": 1.0, "h": 2.1}}  # fmt: skip
INDEX = {"entries": [{"id": "DEBW_00100061ZjV", "interior": ROOM}]}
SPEC = {"wohnhaus": inside_spec({"mobs": ["bed:R", "chest:1", "table:1"], "freepoints": ["LEAN:1"],
                                 "hearth": True}, "uses.wohnhaus.inside")}  # fmt: skip


def plan(owner: str = "", residents: int = 2, specs=SPEC):  # noqa: ANN001, ANN201
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=residents, inside=True, owner=owner)
    return plan_inside([house], specs, INDEX, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})


def by_kind(p, kind: str) -> list[dict]:  # noqa: ANN001
    return [v for v in p.vobs if v["type"] == kind]


def test_inside_spec_is_checked():
    assert inside_spec({"mobs": ["bed:R"]}, "x").mobs == [("bed", "R")]
    for bad in ({"mobs": ["throne:1"]}, {"mobs": ["bed:many"]}, {"freepoints": ["NAP:1"]}):
        with pytest.raises(UsesError):
            inside_spec(bad, "x")


def test_furniture_hearth_and_light():
    p = plan()
    assert p.failed == []
    mobs = by_kind(p, "mob")
    kinds = sorted(v["components"]["mob"]["definition"] for v in mobs)
    assert kinds == ["bed", "bed", "bench", "bench", "chest", "table"]  # 2 residents
    room = Polygon(ROOM["ring"])
    for v in mobs:
        assert room.contains(Point(v["pos"][0], v["pos"][2])) and v["pos"][1] == 1.0
        assert v["name"].startswith("MOB_LEO_WOHNHAUS_ZJV_INNEN_")
    table = next(v for v in mobs if v["mesh"] == "mobs/table.glb")
    benches = [v for v in mobs if v["mesh"] == "mobs/bench.glb"]
    for b in benches:  # beside the table, fronts facing away from it
        d = math.dist((b["pos"][0], b["pos"][2]), (table["pos"][0], table["pos"][2]))
        assert d == pytest.approx(BENCH_OFF_M, abs=1e-6)
    (prop,) = by_kind(p, "mesh")
    assert prop["mesh"] == "props/hearth.glb"
    lights = by_kind(p, "light")
    assert lights and lights[0]["components"]["light"]["flicker"] > 0  # the hearth's fire
    assert all(v["pos"][1] > 1.0 for v in lights)
    assert by_kind(p, "trigger") == []  # no owner: no private area yet


def test_way_from_the_door_stays_clear_and_places_are_reachable():
    p = plan(residents=3)
    entry = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_INNEN")
    mid = (6.0, -0.3)
    path = LineString([mid, Polygon(ROOM["ring"]).centroid.coords[0]]).buffer(0.5)
    for v in by_kind(p, "mob") + by_kind(p, "mesh"):
        assert not path.contains(Point(v["pos"][0], v["pos"][2])), v["name"]
    fps = [f for f in p.places if f["kind"] == "fp"]
    assert {f["name"].split("_")[1] for f in fps} >= {"CAMPFIRE", "LEAN"}
    assert all(f["y"] == 1.0 for f in fps)
    assert (
        entry["pos"] == pytest.approx([6.0, -1.5]) and entry["link"] == "WP_LEO_WOHNHAUS_ZJV_TUER"
    )
    door = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_TUER")
    assert door["pos"] == pytest.approx([6.0, -0.15]) and door["link"] == "WP_LEO_WOHNHAUS_ZJV_VOR"
    front = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_VOR")
    assert front["pos"] == pytest.approx([6.0, 0.6]) and front["link"] == "WP_LEO_WOHNHAUS_ZJV"
    assert "y" not in front  # outside: on the terrain


def test_private_area_with_an_owner():
    p = plan(owner="npc_leo_bauer")
    (t,) = by_kind(p, "trigger")
    trig = t["components"]["trigger"]
    assert trig["shape"] == "box" and trig["owner"] == "npc_leo_bauer"
    assert sorted(trig["halfExtents"][::2]) == pytest.approx([3.2, 4.7])  # the room's half sizes
    assert trig["halfExtents"][1] == pytest.approx(1.45) and t["pos"][1] == pytest.approx(2.45)


def test_houses_without_room_or_spec():
    assert plan(specs={}).vobs and plan(specs={}).places  # waypoints even without furniture
    empty = plan_inside([House("X", "wohnhaus", inside=True)], SPEC, INDEX, {})
    assert empty.vobs == [] and empty.places == []  # no interior in the index
    assert plan(specs={"wohnhaus": InsideSpec()}).failed == []


def test_hearth_comes_first_and_the_way_to_it_stays_clear():
    p = plan(residents=3)
    (prop,) = by_kind(p, "mesh")
    entry = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_INNEN")
    fire = next(f for f in p.places if f["name"].startswith("FP_CAMPFIRE"))
    lane = LineString([entry["pos"], fire["pos"]]).buffer(0.5)
    for v in by_kind(p, "mob"):
        assert not lane.contains(Point(v["pos"][0], v["pos"][2])), v["name"]
    (light,) = [v for v in by_kind(p, "light") if v["name"].endswith("_HERD")]
    assert light["pos"][1] == pytest.approx(1.9)  # above the embers, below the hood
    assert math.dist((light["pos"][0], light["pos"][2]), (prop["pos"][0], prop["pos"][2])) < 1e-6
    # on the wall facing the door: seen on coming in, its front (+z of the model) towards it
    assert prop["pos"][2] == pytest.approx(-6.7 + 0.05 + 0.45)
    assert prop["rot"] == pytest.approx([0.0, 0.0, 0.0, 1.0])


def test_door_swing_stays_free_and_the_open_blade_is_walked_around():
    p = plan(residents=3)
    sweep = Point(5.5, -0.3).buffer(1.0)  # hinge at "from": the blade turns in towards -z
    for v in by_kind(p, "mob") + by_kind(p, "mesh"):
        assert sweep.distance(Point(v["pos"][0], v["pos"][2])) > 0.4, v["name"]
    blade = LineString([(5.5, -0.3), (5.5, -1.3)])
    entry = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_INNEN")
    for f in (f for f in p.places if f["kind"] == "fp"):
        assert LineString([entry["pos"], f["pos"]]).distance(blade) >= 0.3, f["name"]
