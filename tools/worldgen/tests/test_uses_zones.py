"""Indoor zones of the enterable houses (world.md "Zonen", type indoor; W7)."""

import json
import math

import pytest
from shapely.geometry import Point, Polygon
from shapely.ops import unary_union

from gothar_worldgen.export.terrain import world_text
from gothar_worldgen.uses.places import House
from gothar_worldgen.uses.zones import (
    GRID_M,
    SLANT_M,
    indoor_zones,
    room_boxes,
    with_room_zones,
    zones_text,
)


def corners(z: dict) -> Polygon:
    """The box's footprint (local +X = (cos yaw, -sin yaw), local +Z = (sin yaw, cos yaw))."""
    b = z["box"]
    a = math.radians(b["yaw"])
    (cx, _, cz), (hx, _, hz) = b["center"], b["halfExtents"]
    ux, uz, vx, vz = math.cos(a), -math.sin(a), math.sin(a), math.cos(a)
    return Polygon([(cx + sx * hx * ux + sz * hz * vx, cz + sx * hx * uz + sz * hz * vz)
                    for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))])  # fmt: skip


def test_a_turned_rectangle_is_one_box_at_its_walls():
    a = math.radians(30.0)
    ux, uz, vx, vz = math.cos(a), math.sin(a), -math.sin(a), math.cos(a)
    ring = [(10 + p * 6 * ux + q * 4 * vx, 20 + p * 6 * uz + q * 4 * vz)
            for p, q in ((0, 0), (1, 0), (1, 1), (0, 1))]  # fmt: skip
    (z,) = room_boxes("LEO_X_INNEN", ring, 1.0, 3.9)
    assert z["type"] == "indoor" and z["value"] == "LEO_X_INNEN"
    assert z["box"]["center"][1] == pytest.approx(2.45) and z["box"]["halfExtents"][1] == 1.45
    assert sorted(z["box"]["halfExtents"][::2]) == pytest.approx([2.0, 3.0], abs=1e-4)
    assert Polygon(ring).buffer(1e-3).contains(corners(z))
    assert corners(z).area > Polygon(ring).area - 20 * GRID_M  # within a grid cell of the walls


SMITHY = [
    [-90.02, -159.73],
    [-89.33, -161.04],
    [-83.77, -158.5],
    [-88.37, -147.4],
    [-93.74, -149.49],
]


def test_a_pentagon_is_covered_inside_its_walls():
    zones = room_boxes("LEO_SCHMIEDE_ZNP_INNEN", SMITHY, -3.73, -0.814)
    room = Polygon(SMITHY)
    assert 1 < len(zones) <= 6
    boxes = [corners(z) for z in zones]
    assert all(room.buffer(1e-3).contains(b) for b in boxes)  # never beyond the inner wall faces
    union = unary_union(boxes)
    inner = room.buffer(-SLANT_M - 0.01)  # everything but a band along slanted walls
    assert union.buffer(1e-3).contains(inner)
    assert union.area > 0.9 * room.area


def test_lines_sorted_as_the_engine_writes_them():
    rooms = room_boxes("LEO_B_INNEN", SMITHY, 0.0, 2.5) + room_boxes(
        "LEO_A_INNEN", [[0, 0], [4, 0], [4, 3], [0, 3]], 0.0, 2.5
    )
    music = {"value": "CAMP", "type": "music", "bounds": [[0, 0, 0], [1, 1, 1]]}
    text = zones_text([*rooms, music])
    lines = text.splitlines()
    assert lines[0] == "[" and lines[-1] == "  ]"
    body = [json.loads(line.strip().rstrip(",")) for line in lines[1:-1]]
    assert [z["value"] for z in body][:2] == ["CAMP", "LEO_A_INNEN"]
    assert lines[1].strip().startswith('{"bounds":')  # other types: keys sorted, as the engine
    assert lines[2].strip().startswith('{"type":"indoor","value":"LEO_A_INNEN","box":{"center":')
    smithy = [z["box"]["center"] for z in body if z["value"] == "LEO_B_INNEN"]
    assert smithy == sorted(smithy, key=lambda c: (c[0], c[2]))  # then centre x, z


def test_assemble_replaces_only_its_room_zones():
    world = {"vobs": [], "zones": [{"type": "music", "value": "CAMP"},
                                   {"type": "indoor", "value": "LEO_ALT_INNEN", "box": {}},
                                   {"type": "indoor", "value": "KELLER", "box": {}}]}  # fmt: skip
    new = room_boxes("LEO_A_INNEN", [[0, 0], [4, 0], [4, 3], [0, 3]], 0.0, 2.5)
    with_room_zones(world, new)
    assert [z["value"] for z in world["zones"]] == ["CAMP", "KELLER", "LEO_A_INNEN"]
    empty = {"vobs": [], "zones": [{"type": "indoor", "value": "LEO_ALT_INNEN", "box": {}}]}
    with_room_zones(empty, [])
    assert "zones" not in empty
    text = world_text({"version": 1, "vobs": [], "zones": new})
    assert text.endswith('  "zones": [\n    ' + zones_text(new)[6:] + "\n}\n")


def test_zones_of_the_enterable_houses():
    index = {"entries": [{"id": "DEBW_00100061ZjV",
                          "interior": {"ring": [[0, 0], [4, 0], [4, 3], [0, 3]], "floor": 1.0,
                                       "ceiling": 3.5}},
                         {"id": "DEBW_00100061Zk9"}]}  # fmt: skip
    houses = [House("DEBW_00100061ZjV", "wohnhaus", inside=True),
              House("DEBW_00100061Zk9", "wohnhaus", inside=True),
              House("DEBW_00100061Zzz", "wohnhaus")]  # fmt: skip
    (z,) = indoor_zones(houses, index)
    assert z["value"] == "LEO_WOHNHAUS_ZJV_INNEN"  # as WP_LEO_WOHNHAUS_ZJV_INNEN
    assert corners(z).contains(Point(2.0, 1.5))
