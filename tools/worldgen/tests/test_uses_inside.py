"""Inside the enterable houses (W7 C2): furniture, hearth, light, freepoints, waypoints, trigger."""

import math

import pytest
from shapely.geometry import LineString, Point, Polygon

from gothar_worldgen.uses.inside import (
    BENCH_OFF_M,
    GROUP_D,
    PROP_SIZE,
    REACH_R_M,
    InsideSpec,
    inside_spec,
    plan_inside,
)
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
    (prop,) = [v for v in by_kind(p, "mesh") if v["mesh"] == "props/hearth.glb"]
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
    for f in fps:  # a straight walk from the room's waypoint, clear of every piece of furniture
        lane = LineString([entry["pos"], f["pos"]])
        for v in by_kind(p, "mob") + by_kind(p, "mesh"):
            assert lane.distance(Point(v["pos"][0], v["pos"][2])) >= REACH_R_M, (
                f["name"],
                v["name"],
            )
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
    (prop,) = [v for v in by_kind(p, "mesh") if v["mesh"] == "props/hearth.glb"]
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


def test_a_town_wall_through_the_room_is_its_back_wall():
    wall = Polygon([(-1.0, -5.5), (11.0, -5.5), (11.0, -7.5), (-1.0, -7.5)])
    bodies = [("CITYWALL_WALL_01", wall), ("BLD_DEBW_00100061ZjV", Polygon(ROOM["ring"])),
              ("MOB_LEO_TUER_ZJV", Point(6.0, -1.0).buffer(0.3))]  # fmt: skip
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=3, inside=True)
    p = plan_inside([house], SPEC, INDEX, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"}, bodies)
    assert p.failed == []
    for v in by_kind(p, "mob") + by_kind(p, "mesh"):
        assert v["pos"][2] > -5.5, v["name"]  # nothing in or behind the wall
    for f in (f for f in p.places if f["kind"] == "fp"):
        assert not wall.buffer(0.2).contains(Point(f["pos"])), f["name"]
    (prop,) = [
        v for v in by_kind(p, "mesh") if v["mesh"] == "props/hearth.glb"
    ]  # the hearth against the town wall, facing the door
    assert prop["pos"][2] == pytest.approx(-5.5 + 0.05 + 0.05 + 0.45)  # gap to the wall, half depth


HOUSEHOLD = {"mobs": ["bed:1"], "freepoints": ["STAND:1", "LEAN:1"], "hearth": True,
             "props": ["counter:1", "bellows:1", "quench_trough:1", "barrel:2", "shelf:1",
                       "weapon_board:1", "sausages:1", "herbs:1"]}  # fmt: skip


ON_WALL_OR_TABLE = ("hearth.glb", "_board.glb", "lantern.glb", "candlestick.glb")


def household(spec: dict = HOUSEHOLD):  # noqa: ANN201
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=1, inside=True)
    specs = {"wohnhaus": inside_spec(spec, "x")}
    return plan_inside([house], specs, INDEX, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})


def props_of(p, kind: str) -> list[dict]:  # noqa: ANN001
    return [v for v in by_kind(p, "mesh") if v["mesh"] == f"props/{kind}.glb"]


def test_props_are_checked():
    assert inside_spec({"props": ["barrel:2", "counter:1"]}, "x").props == [
        ("barrel", 2),
        ("counter", 1),
    ]
    for bad in ({"props": ["throne:1"]}, {"props": ["barrel:many"]}):
        with pytest.raises(UsesError):
            inside_spec(bad, "x")


def test_household_props_leave_the_ways_free():
    p = household()
    assert p.failed == []
    entry = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_INNEN")
    floor_props = [v for v in by_kind(p, "mesh")
                   if v["mesh"].startswith("props/") and v["pos"][1] == 1.0
                   and not v["mesh"].endswith(ON_WALL_OR_TABLE)]  # fmt: skip
    assert len(floor_props) >= 6  # counter, shelves, bellows, trough, barrels
    room = Polygon(ROOM["ring"])
    for v in floor_props:
        assert room.contains(Point(v["pos"][0], v["pos"][2])), v["name"]
    for f in (f for f in p.places if f["kind"] == "fp" and "STAND" not in f["name"]):
        lane = LineString([entry["pos"], f["pos"]])
        for v in floor_props:
            assert lane.distance(Point(v["pos"][0], v["pos"][2])) >= REACH_R_M, (
                f["name"],
                v["name"],
            )


def test_counter_with_shelf_aisle_and_waypoint():
    p = household()
    (counter,) = props_of(p, "counter")
    shelf = min(props_of(p, "shelf"), key=lambda v: math.dist(v["pos"], counter["pos"]))
    theke = next(w for w in p.places if w["name"] == "WP_LEO_WOHNHAUS_ZJV_INNEN_THEKE")
    assert theke["link"] == "WP_LEO_WOHNHAUS_ZJV_INNEN" and theke["y"] == 1.0
    stand = next(f for f in p.places if f["name"].startswith("FP_STAND"))
    c, s, t = (
        (counter["pos"][0], counter["pos"][2]),
        (shelf["pos"][0], shelf["pos"][2]),
        stand["pos"],
    )
    assert math.dist(c, s) == pytest.approx(GROUP_D - 0.32 - PROP_SIZE["shelf"][1] / 2, abs=1e-6)
    # the trader stands between shelf and counter, facing the customers
    assert math.dist(t, s) < math.dist(c, s) and math.dist(t, c) < math.dist(c, s)
    assert LineString([theke["pos"], t]).distance(Point(c)) > 0.3  # along the aisle
    # the aisle's open end lies behind the counter (towards the shelf), not among the customers
    behind = (s[0] - c[0], s[1] - c[1])
    to_wp = (theke["pos"][0] - c[0], theke["pos"][1] - c[1])
    assert behind[0] * to_wp[0] + behind[1] * to_wp[1] > 0


def test_smithy_things_beside_the_hearth_and_blades_on_the_wall():
    p = household()
    (hearth,) = props_of(p, "hearth")
    (bellows,) = props_of(p, "bellows")
    hx, hz = hearth["pos"][0], hearth["pos"][2]
    a = 2 * math.atan2(bellows["rot"][1], bellows["rot"][3])
    nozzle = (-math.cos(a), math.sin(a))  # the model's -X in the world
    to_fire = (hx - bellows["pos"][0], hz - bellows["pos"][2])
    assert nozzle[0] * to_fire[0] + nozzle[1] * to_fire[1] > 0.9 * math.hypot(*to_fire)
    assert math.dist((hx, hz), (bellows["pos"][0], bellows["pos"][2])) < 1.5
    blades = [v for v in by_kind(p, "mesh") if v["mesh"].startswith("items/")]
    assert [v["mesh"] for v in blades] == [
        "items/it_sword_old.glb",
        "items/it_sword_crude.glb",
        "items/it_axe.glb",
    ]
    x, y, z, w = blades[0]["rot"]  # item +Y points down
    assert 1 - 2 * (x * x + z * z) == pytest.approx(-1.0, abs=1e-4)


def test_hanging_things_above_heads_off_the_hearth():
    p = household()
    (hearth,) = props_of(p, "hearth")
    for kind in ("sausages", "herbs"):
        (v,) = props_of(p, kind)
        assert v["pos"][1] == pytest.approx(ROOM["ceiling"] - 0.02)
        assert math.dist((v["pos"][0], v["pos"][2]), (hearth["pos"][0], hearth["pos"][2])) > 1.0


# the same room divided at x = 4.0: the chamber on the west, the room with the door on the east
DIVIDED = {"entries": [{"id": "DEBW_00100061ZjV", "interior": {**ROOM, "rooms": [
    {"name": "INNEN", "ring": [[4.075, -0.3], [4.075, -6.7], [9.7, -6.7], [9.7, -0.3]]},
    {"name": "KAMMER", "ring": [[0.3, -0.3], [0.3, -6.7], [3.925, -6.7], [3.925, -0.3]]}],
    "passages": [{"rooms": ["KAMMER", "INNEN"], "mid": [4.0, -3.5], "axis": [1.0, 0.0],
                  "w": 0.9, "h": 2.0}]}}]}  # fmt: skip


def test_a_divided_storey_puts_beds_in_the_chamber():
    spec = {"wohnhaus": inside_spec({"mobs": ["bed:R", "chest:1", "table:1"], "hearth": True,
                                     "freepoints": ["LEAN:1"],
                                     "props": ["shelf:1", "sacks:1"]}, "x")}  # fmt: skip
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=2, inside=True)
    p = plan_inside([house], spec, DIVIDED, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})
    assert p.failed == []
    kammer = Polygon(DIVIDED["entries"][0]["interior"]["rooms"][1]["ring"])
    where = {v["name"]: kammer.contains(Point(v["pos"][0], v["pos"][2])) for v in p.vobs
             if v["type"] in ("mob", "mesh")}  # fmt: skip
    beds = [n for n in where if "_BED_" in n]
    assert len(beds) == 2 and all(where[n] for n in beds)
    assert all(n.startswith("MOB_LEO_WOHNHAUS_ZJV_KAMMER_") for n in beds)
    assert where["MOB_LEO_WOHNHAUS_ZJV_KAMMER_CHEST_1"]
    assert where["PROP_LEO_WOHNHAUS_ZJV_KAMMER_SACKS_1"]  # stores go to the chamber
    assert not where["PROP_LEO_WOHNHAUS_ZJV_INNEN_HERD"]
    assert not where["MOB_LEO_WOHNHAUS_ZJV_INNEN_TABLE_1"]
    names = {w["name"]: w for w in p.places if w["kind"] == "wp"}
    through = names["WP_LEO_WOHNHAUS_ZJV_KAMMER_DURCHGANG"]
    assert (
        through["pos"] == pytest.approx([4.0, -3.5])
        and through["link"] == "WP_LEO_WOHNHAUS_ZJV_INNEN"
    )
    chamber = names["WP_LEO_WOHNHAUS_ZJV_KAMMER"]
    assert chamber["link"] == through["name"] and chamber["pos"] == pytest.approx([2.8, -3.5])
    lights = [v["name"] for v in by_kind(p, "light")]
    assert "LIGHT_LEO_WOHNHAUS_ZJV_KAMMER_LATERNE_1" in lights  # every room has its light


def test_a_divided_storey_has_a_zone_per_room():
    from gothar_worldgen.uses.zones import indoor_zones

    house = House("DEBW_00100061ZjV", "wohnhaus", inside=True)
    values = {z["value"] for z in indoor_zones([house], DIVIDED)}
    assert values == {"LEO_WOHNHAUS_ZJV_INNEN", "LEO_WOHNHAUS_ZJV_KAMMER"}


def test_windows_keep_tall_things_off_and_let_the_day_in():
    # a window in the north wall, just where the hearth faces the door
    window = {
        "from": [5.6, -6.7],
        "to": [6.4, -6.7],
        "sill": 1.9,
        "top": 2.9,
        "normal": [0.0, -1.0],
    }
    index = {"entries": [{"id": "DEBW_00100061ZjV", "interior": {**ROOM, "windows": [window]}}]}
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=2, inside=True)
    p = plan_inside([house], SPEC, index, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})
    (hearth,) = [v for v in by_kind(p, "mesh") if v["mesh"] == "props/hearth.glb"]
    assert abs(hearth["pos"][0] - 6.0) > 0.4 + 0.6 + 0.3 or hearth["pos"][2] > -6.7 + 0.8 + 0.3
    (light,) = [v for v in by_kind(p, "light") if "_FENSTER_" in v["name"]]
    assert light["pos"] == pytest.approx([6.0, 2.4, -6.7 + 0.8])
    comp = light["components"]["light"]
    assert list(comp) == ["color", "range", "intensity", "daylight"] and comp["daylight"] is True


def test_candles_on_tables_lanterns_on_walls_within_the_budget():
    from gothar_worldgen.uses.inside import LANTERN_APART_M, LANTERN_CLEAR_M, NIGHT_M2, ROOM_LIGHTS

    window = {
        "from": [2.0, -6.7],
        "to": [2.8, -6.7],
        "sill": 1.9,
        "top": 2.9,
        "normal": [0.0, -1.0],
    }
    index = {"entries": [{"id": "DEBW_00100061ZjV", "interior": {**ROOM, "windows": [window]}}]}
    spec = {"wohnhaus": inside_spec({"mobs": ["table:1"]}, "x")}  # no hearth: lanterns light it
    house = House("DEBW_00100061ZjV", "wohnhaus", inside=True)
    p = plan_inside([house], spec, index, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})
    (table,) = [v for v in by_kind(p, "mob") if v["mesh"] == "mobs/table.glb"]
    (stick,) = [v for v in by_kind(p, "mesh") if v["mesh"] == "props/candlestick.glb"]
    assert stick["pos"] == pytest.approx([table["pos"][0], 1.75, table["pos"][2]])
    lights = by_kind(p, "light")
    assert len(lights) <= ROOM_LIGHTS
    night = [v for v in lights if not v["components"]["light"].get("daylight")]
    assert len(night) >= round(Polygon(ROOM["ring"]).area / NIGHT_M2)
    lanterns = [v for v in lights if "_LATERNE_" in v["name"]]
    assert lanterns and all(v["components"]["light"]["flicker"] > 0 for v in lanterns)
    for v in lanterns:
        x, z = v["pos"][0], v["pos"][2]
        assert math.dist((x, z), (6.0, -0.3)) >= LANTERN_CLEAR_M - 0.2  # the door
        assert math.dist((x, z), (2.4, -6.7)) >= LANTERN_CLEAR_M - 0.2  # the window
        others = [w for w in night if w is not v]
        assert min(math.dist((x, z), (w["pos"][0], w["pos"][2])) for w in others) >= (
            LANTERN_APART_M - 0.2
        )


def test_tables_set_hearth_kept_and_small_things():
    spec = {"wohnhaus": inside_spec({"mobs": ["bed:R", "table:1"], "hearth": True,
                                     "props": ["stool:2", "fur:1", "broom:1", "wall_hanging:1",
                                               "basket:1"]}, "x")}  # fmt: skip
    house = House("DEBW_00100061ZjV", "wohnhaus", residents=1, inside=True)
    p = plan_inside([house], spec, INDEX, {"DEBW_00100061ZjV": "WP_LEO_WOHNHAUS_ZJV"})
    assert p.failed == []
    meshes = by_kind(p, "mesh")
    (table,) = [v for v in by_kind(p, "mob") if v["mesh"] == "mobs/table.glb"]
    tx, tz = table["pos"][0], table["pos"][2]
    (rug,) = props_of(p, "rug")
    assert rug["pos"] == pytest.approx([tx, 1.0, tz])  # under the table
    (ware,) = props_of(p, "tableware")
    assert ware["pos"] == pytest.approx([tx, 1.75, tz])
    items = sorted(
        v["mesh"] for v in meshes if v["mesh"].startswith("items/it_") and "broom" not in v["mesh"]
    )
    assert items == [
        "items/it_apple.glb",
        "items/it_bread.glb",
        "items/it_mug.glb",
        "items/it_mug.glb",
    ]
    for v in meshes:
        if v["mesh"] in ("items/it_mug.glb", "items/it_apple.glb"):
            assert v["pos"][1] in (pytest.approx(1.8), pytest.approx(1.75))  # on the top
            assert math.dist((v["pos"][0], v["pos"][2]), (tx, tz)) < 0.6
    stools = props_of(p, "stool")
    assert len(stools) == 2
    # at the table's ends where there is room, else at a wall
    assert any(abs(math.dist((v["pos"][0], v["pos"][2]), (tx, tz)) - 1.15) < 1e-6 for v in stools)
    (hearth,) = props_of(p, "hearth")
    (pot,) = props_of(p, "pot")
    assert pot["pos"][::2] == pytest.approx(hearth["pos"][::2]) and pot["pos"][1] > 1.4
    (wood,) = props_of(p, "firewood")
    assert math.dist(wood["pos"][::2], hearth["pos"][::2]) < 1.5
    (broom,) = [v for v in meshes if v["mesh"] == "items/it_broom.glb"]
    assert broom["pos"][1] == pytest.approx(1.0 + 1.2) and broom["rot"] == [0.0, 0.0, 1.0, 0.0]
    (fur,) = props_of(p, "fur")
    (bed,) = [v for v in by_kind(p, "mob") if v["mesh"] == "mobs/bed.glb"]
    assert math.dist(fur["pos"][::2], bed["pos"][::2]) == pytest.approx(0.45 + 0.48 + 0.05)
    (hanging,) = props_of(p, "wall_hanging")
    room = Polygon(ROOM["ring"])
    assert room.exterior.distance(Point(hanging["pos"][0], hanging["pos"][2])) < 0.2  # on a wall
