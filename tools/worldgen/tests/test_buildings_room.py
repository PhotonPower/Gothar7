"""Enterable houses (W7 C1): ground storey as one room, open door, hollow collision, door mob."""

import math
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.medieval import StreetIndex, build_house, load_rules
from gothar_worldgen.uses.places import door_mobs

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
RING = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
HOUSE = {"id": "H1", "groundY": 0.0, "footprint": RING, "roof": ROOF}
STREET_SOUTH = StreetIndex([{"points": [[-20.0, 5.0], [30.0, 5.0]]}])
ORIGIN = np.array([5.0, -0.5, -3.5])


def house(interior=None, lod=0):  # noqa: ANN001, ANN201
    return build_house(HOUSE, -0.5, (5.0, -3.5), RULES, STREET_SOUTH,
                       ground_at=lambda x, z: 0.0, interior=interior, lod=lod)  # fmt: skip


def solid(result, x: float, y: float, z: float) -> bool:  # noqa: ANN001
    """Inside one of the convex collision bodies."""
    q = np.array([x, y, z])
    for p in result.collision.parts:
        pts = p.positions.astype(float) + ORIGIN
        tri = pts[p.indices.reshape(-1, 3)]
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        n *= np.sign(np.einsum("ij,ij->i", n, tri[:, 0] - pts.mean(axis=0)))[:, None]
        if np.all(n @ q - np.einsum("ij,ij->i", n, tri[:, 0]) <= 1e-6):
            return True
    return False


def test_room_geometry_and_record():
    plain, r = house(), house({"use": "wohnhaus"})
    assert plain.room is None and r.room is not None
    room = r.room
    wall = RULES.data["interior"]["wallM"]
    assert room["floor"] == 0.0 and room["ceiling"] - room["floor"] >= 2.4
    xs = [p[0] for p in room["ring"]]
    assert min(xs) == pytest.approx(wall) and max(xs) == pytest.approx(10.0 - wall)
    d = room["door"]
    assert d["normal"] == pytest.approx([0.0, 1.0]) and d["w"] == pytest.approx(1.0)
    assert d["from"][1] == pytest.approx(-wall) and d["to"][1] == pytest.approx(-wall)
    assert len(r.primitives) >= len(plain.primitives) + 3  # walls, floor, ceiling, beams
    # the room lies outside the house budget: the outside keeps its timber level
    assert abs(r.triangles - plain.triangles) <= 4 and r.timber_level == plain.timber_level


def stone_tris(result) -> int:  # noqa: ANN001
    return sum(p.mesh.triangle_count for p in result.primitives if p.material == "stone")


def test_stone_floor_for_smithy_and_tavern():
    assert stone_tris(house({"use": "schmiede"})) > stone_tris(house({"use": "wohnhaus"}))


def test_collision_has_a_hollow_room_and_an_open_door():
    r = house({"use": "wohnhaus"})
    d = r.room["door"]
    mid = ((d["from"][0] + d["to"][0]) / 2, d["from"][1])
    for z in np.arange(1.5, -3.5, -0.25):  # from the street through the door into the room
        for h in (0.3, 1.0, 1.7):
            assert not solid(r, mid[0], h, z), (mid[0], h, z)
    assert solid(r, 2.0, 1.0, -0.15)  # inside the wall beside the door
    assert solid(r, 5.0, r.room["ceiling"] + 0.6, -3.5)  # above the ceiling
    assert solid(r, 5.0, -0.2, -3.5)  # the floor slab
    assert not solid(r, 5.0, 1.0, -3.5)  # the room
    assert solid(house(), 5.0, 1.0, -3.5)  # without a room: solid as before


def test_distance_levels_stay_closed():
    r1 = house({"use": "wohnhaus"}, lod=1)
    plain1 = house(lod=1)
    assert r1.room is None and r1.triangles == plain1.triangles
    assert len(r1.primitives) == len(plain1.primitives)


def test_door_mob_in_the_opening():
    room = house({"use": "wohnhaus"}).room
    index = {"entries": [{"id": "DEBW_00100061ZjV", "interior": room}]}
    (closed,) = door_mobs(index, opened=False)
    (opened,) = door_mobs(index, opened=True)
    assert closed["name"] == "MOB_LEO_TUER_ZJV" and closed["definition"] == "door"
    assert closed["components"] == {"mob": {"definition": "door"}}  # closed: no "open" key
    assert list(opened["components"]["mob"]) == ["definition", "open"]  # order (world.md)
    assert opened["components"]["mob"]["open"] is True and opened["rot"] == closed["rot"]
    assert closed["mesh"] == "mobs/door.glb" and closed["pos"][1] == room["door"]["floor"]

    def turned(v: dict, local: tuple[float, float]) -> tuple[float, float]:
        a = 2 * math.atan2(v["rot"][1], v["rot"][3])
        return (local[0] * math.cos(a) + local[1] * math.sin(a),
                -local[0] * math.sin(a) + local[1] * math.cos(a))  # fmt: skip

    assert turned(closed, (0.0, 1.0)) == pytest.approx((0.0, 1.0), abs=1e-4)  # front faces out
    bx, bz = turned(closed, (1.0, 0.0))  # the blade runs across the opening
    hx, hz = closed["pos"][0], closed["pos"][2]
    far = (hx + bx * room["door"]["w"], hz + bz * room["door"]["w"])
    ends = [room["door"]["from"], room["door"]["to"]]
    assert min(math.dist(far, e) for e in ends) == pytest.approx(0.06, abs=0.02)
    assert door_mobs({"entries": [{"id": "X"}]}, True) == []


def test_a_big_ground_storey_is_divided_with_an_open_passage():
    r = house({"use": "wohnhaus"})  # 9.4 x 6.4 m inside: above maxRoomM2 (45)
    rooms = r.room["rooms"]
    assert [q["name"] for q in rooms] == ["INNEN", "KAMMER"]
    from shapely.geometry import Point, Polygon

    first = Polygon(rooms[0]["ring"])
    d = r.room["door"]
    door = ((d["from"][0] + d["to"][0]) / 2, (d["from"][1] + d["to"][1]) / 2)
    assert first.buffer(0.05).contains(Point(door))  # the house door opens into the first
    whole = Polygon(r.room["ring"]).area
    part = RULES.data["interior"]["partitionM"]
    assert sum(Polygon(q["ring"]).area for q in rooms) == pytest.approx(whole - 6.4 * part, abs=0.1)
    (p,) = r.room["passages"]
    assert p["rooms"] in (["INNEN", "KAMMER"], ["KAMMER", "INNEN"]) and p["w"] == 0.9
    mx, mz = p["mid"]
    ax, az = p["axis"]
    # the partition is solid beside the passage; the passage is open (up to the ceiling, like
    # the house door: the engine probes the ground from above)
    sx, sz = -az, ax
    assert solid(r, mx + sx * 1.5, 1.0, mz + sz * 1.5)
    assert not solid(r, mx, 2.4, mz)
    assert not solid(r, mx, 1.0, mz) and not solid(r, mx + ax * 0.5, 1.0, mz + az * 0.5)


def test_rooms_stay_whole_under_the_limit():
    rules = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
    rules.data["interior"]["maxRoomM2"] = 80.0
    r = build_house(HOUSE, -0.5, (5.0, -3.5), rules, STREET_SOUTH, ground_at=lambda x, z: 0.0,
                    interior={"use": "wohnhaus"})  # fmt: skip
    assert "rooms" not in r.room and "passages" not in r.room


def test_no_collision_reaches_into_a_skewed_room():
    """The walls left round a carved room stay walls: no convex piece reaches into the room (a
    five-cornered house, like the smithy at the town wall)."""
    from shapely.geometry import Polygon

    ring = [[0.0, 0.0], [11.0, 0.0], [11.0, -6.0], [4.0, -9.5], [0.0, -7.0]]
    h = {**HOUSE, "footprint": ring}
    r = build_house(h, -0.5, (5.0, -4.0), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0,
                    interior={"use": "schmiede"})  # fmt: skip
    assert r.room is not None
    rooms = [Polygon(q["ring"]) for q in r.room.get("rooms", [])] or [Polygon(r.room["ring"])]
    global ORIGIN
    keep, ORIGIN = ORIGIN, np.array([5.0, -0.5, -4.0])
    try:
        for room in rooms:
            inner = room.buffer(-0.15)
            x0, z0, x1, z1 = inner.bounds
            for x in np.arange(x0, x1, 0.25):
                for z in np.arange(z0, z1, 0.25):
                    from shapely.geometry import Point

                    if inner.contains(Point(x, z)):
                        assert not solid(r, float(x), 1.0, float(z)), (x, z)
    finally:
        ORIGIN = keep
