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
    assert turned(opened, (1.0, 0.0)) == pytest.approx((0.0, -1.0), abs=1e-4)  # swung into the room
    assert door_mobs({"entries": [{"id": "X"}]}, True) == []
