import json
import math
from pathlib import Path

import pytest
from shapely.geometry import Point, Polygon
from shapely.ops import unary_union

from gothar_worldgen.uses.sound import sound_zones, with_sound_zones
from gothar_worldgen.uses.zones import zones_text

SPEC = json.loads(
    (Path(__file__).parents[1] / "data" / "leonberg" / "sound_zones.json").read_text("utf-8")
)
TOWN = [(-100.0, -80.0), (100.0, -80.0), (100.0, 80.0), (-100.0, 80.0)]
ROOM = {"type": "indoor", "value": "LEO_SCHMIEDE_ZNP_INNEN",
        "box": {"center": [5.0, 1.5, 5.0], "halfExtents": [3.0, 1.5, 2.0], "yaw": 0.0}}  # fmt: skip


def footprint(z: dict) -> Polygon:
    b = z["box"]
    (cx, _, cz), (hx, _, hz), yaw = b["center"], b["halfExtents"], math.radians(b["yaw"])
    ux, uz, vx, vz = math.cos(yaw), -math.sin(yaw), math.sin(yaw), math.cos(yaw)
    return Polygon([(cx + sx * hx * ux + sz * hz * vx, cz + sx * hx * uz + sz * hz * vz)
                    for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))])  # fmt: skip


def plan() -> list[dict]:
    square = [[-20.0, -10.0], [20.0, -10.0], [20.0, 10.0], [-20.0, 10.0]]
    garden = Polygon([(120, 0), (160, 0), (160, 30), (120, 30)])
    water = [
        {"pos": [300.0, -5.0, 0.0], "rot": [0.0, 0.0, 0.0, 1.0], "halfExtents": [20.0, 1.0, 4.0]}
    ]
    forest = Polygon([(400, 300), (520, 300), (520, 420), (400, 420)])
    return sound_zones(SPEC, 1000.0, TOWN, [square], [garden], water, [forest], [(0.0, 2.0, 40.0)],
                       lambda x, z: 10.0, [ROOM])  # fmt: skip


def test_the_old_town_has_its_music_and_lanes_and_the_world_its_defaults():
    zones = plan()
    world = [z for z in zones if z["box"]["halfExtents"][0] == 1000.0]
    assert {(z["type"], z["value"]) for z in world} == {("music", "LAND"), ("ambient", "feld")}
    town = Polygon(TOWN)
    lanes = unary_union([footprint(z) for z in zones if z["value"] == "stadt_gasse"])
    assert lanes.difference(town).area < 1.0 and lanes.intersection(town).area > 0.85 * town.area
    music = unary_union([footprint(z) for z in zones if z["value"] == "STADT"])
    assert music.contains(town.buffer(-0.5)) and music.contains(Point(140, 15))  # and the garden
    hy = SPEC["heightM"] / 2
    assert all(
        z["box"]["halfExtents"][1] == hy for z in zones if z["value"] in ("stadt_gasse", "wald")
    )


def test_squares_water_forests_fountains_the_wall_walk_and_rooms():
    zones = plan()
    by = {}
    for z in zones:
        by.setdefault(z["value"], []).append(z)
    (sq,) = by["stadt_markt"]
    assert footprint(sq).contains(Polygon([(-20, -10), (20, -10), (20, 10), (-20, 10)]))
    (w,) = by["ufer"]
    assert w["box"]["halfExtents"][0] == 20.0 + SPEC["waterMarginM"]  # the banks along it
    assert by["wald"] and all(Polygon([(400, 300), (520, 300), (520, 420), (400, 420)])
                              .buffer(0.1).contains(footprint(z)) for z in by["wald"])  # fmt: skip
    (f,) = by["brunnen"]
    assert f["box"]["halfExtents"][0] == SPEC["fountainHalfM"] and f["box"]["center"][1] > 2.0
    walk = by["stadtmauer"]
    assert walk and all(z["box"]["center"][1] == pytest.approx(10.0 + SPEC["wallBandM"] / 2)
                        for z in walk)  # fmt: skip
    ring = Polygon(TOWN).exterior
    assert all(
        ring.distance(Point(z["box"]["center"][0], z["box"]["center"][2])) < 0.5 for z in walk
    )
    (room,) = by["schmiede_esse"]
    assert room["type"] == "ambient" and room["box"] == ROOM["box"]  # the forge in its room


def test_only_worldgens_sound_zones_are_replaced_and_written_like_indoor_ones():
    hand = {"type": "ambient", "value": "hoehle", "box": ROOM["box"]}
    world = {"zones": [ROOM, hand, {"type": "music", "value": "STADT", "box": ROOM["box"]}]}
    zones = plan()
    with_sound_zones(world, zones, SPEC)
    assert ROOM in world["zones"] and hand in world["zones"]
    assert sum(1 for z in world["zones"] if z["value"] == "STADT") == sum(
        1 for z in zones if z["value"] == "STADT"
    )  # the old one gone
    text = zones_text(world["zones"])
    lines = text.strip("[]\n ").split(",\n    ")
    assert lines[0].startswith('{"type":"music","value":"LAND","box":{"center"')
    values = [json.loads(line)["value"] for line in lines]
    assert values == sorted(values)
