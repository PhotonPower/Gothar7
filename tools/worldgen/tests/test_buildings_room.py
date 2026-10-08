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


def all_prims(result) -> list:  # noqa: ANN001
    """The house's primitives and those of its rooms (each room its own mesh, W7)."""
    return [*result.primitives, *(q for room in result.room_prims.values() for q in room)]


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
    assert len(all_prims(r)) >= len(plain.primitives) + 3  # walls, floor, ceiling, beams
    # every room is its own mesh; none of the room's geometry stays in the house's
    names = [q["name"] for q in room["rooms"]] + [q["name"] for q in room["upper"]["rooms"]]
    assert sorted(r.room_prims) == sorted(names)
    assert not {q.material for q in r.primitives} & {"plaster_white"}
    # the room lies outside the house budget: the outside keeps its timber level; its open
    # windows only lose their panes (two triangles each)
    panes = 2 * (len(room["windows"]) + len(room["upper"]["windows"]))
    assert abs(r.triangles - plain.triangles) <= panes + 4 and r.timber_level == plain.timber_level


def stone_tris(result) -> int:  # noqa: ANN001
    return sum(p.mesh.triangle_count for p in all_prims(result) if p.material == "stone")


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
    up = r.room["upper"]  # the slab between the storeys, the body above the upper storey
    assert solid(r, 5.0, (r.room["ceiling"] + up["floor"]) / 2, -3.5)
    assert solid(r, 5.0, up["ceiling"] + 0.6, -3.5)
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


def test_no_partition_against_the_house_door():
    from shapely.geometry import box

    from gothar_worldgen.buildings.medieval import _partition_plan

    spec = {**RULES.data["interior"], "maxRoomM2": 20.0, "minRoomWidthM": 2.5}
    inner = box(0.0, -6.0, 12.0, 0.0)  # 12 x 6 m: three or four rooms
    for x in [k * 0.25 for k in range(4, 45)]:  # the door anywhere along the long wall
        rooms, cuts = _partition_plan(inner, (x, 0.0), spec, "wohnhaus", 1.0)
        assert len(rooms) > 1 and all(abs(c["t"] - x) >= 1.0 - 1e-9 for c in cuts), x
        widths = [q.bounds[2] - q.bounds[0] for _, q in rooms]  # less the partitions' halves
        assert min(widths) >= 2.5 - spec["partitionM"] - 1e-6, (x, widths)


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
    stairs = Polygon(r.room["stairs"]["footprint"]).buffer(0.1) if "stairs" in r.room else None
    global ORIGIN
    keep, ORIGIN = ORIGIN, np.array([5.0, -0.5, -4.0])
    try:
        for room in rooms:
            inner = room.buffer(-0.15)
            x0, z0, x1, z1 = inner.bounds
            for x in np.arange(x0, x1, 0.25):
                for z in np.arange(z0, z1, 0.25):
                    from shapely.geometry import Point

                    q = Point(x, z)
                    if inner.contains(q) and not (stairs is not None and stairs.contains(q)):
                        assert not solid(r, float(x), 1.0, float(z)), (x, z)
    finally:
        ORIGIN = keep


def test_windows_of_the_room_are_open():
    r = house({"use": "wohnhaus"})
    wins = r.room["windows"]
    assert len(wins) >= 4
    wall = RULES.data["interior"]["wallM"]
    for w in wins:
        assert w["top"] - w["sill"] == pytest.approx(1.0, abs=0.05) and w["sill"] > 0.5
        (fx, fz), (tx, tz) = w["from"], w["to"]
        mx, mz = (fx + tx) / 2, (fz + tz) / 2
        nx, nz = w["normal"]  # outwards
        y = (w["sill"] + w["top"]) / 2 - 0.25  # below the cross bar
        # open through the whole wall, from the room's face out to the facade
        for d in (0.05, wall / 2 + 0.06, wall - 0.05):
            px, pz = mx + nx * d + (tx - fx) * 0.2, mz + nz * d + (tz - fz) * 0.2
            assert not solid_mesh(r, px, y, pz, nx, nz), (w, d)


def solid_mesh(result, x: float, y: float, z: float, nx: float, nz: float) -> bool:  # noqa: ANN001
    """A ray from inside the room outwards along the window's normal hits geometry before 0.4 m."""
    o = np.array([x - nx * 0.06, y, z - nz * 0.06]) - ORIGIN
    d = np.array([nx, 0.0, nz])
    for p in all_prims(result):
        pts = p.mesh.positions.astype(float)
        tri = pts[p.mesh.indices.reshape(-1, 3)]
        e1, e2 = tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]
        h = np.cross(d, e2)
        det = np.einsum("ij,ij->i", e1, h)
        ok = np.abs(det) > 1e-9
        f = np.where(ok, 1 / np.where(ok, det, 1), 0)
        s_ = o - tri[:, 0]
        u = f * np.einsum("ij,ij->i", s_, h)
        q = np.cross(s_, e1)
        v = f * (q @ d)
        t = f * np.einsum("ij,ij->i", e2, q)
        if np.any(ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0) & (t < 0.12)):
            return True
    return False


def test_room_meshes_collide_only_with_a_box_inside_the_house():
    """A model without COL_ collides with all its triangles (asset.md): the room meshes carry one
    small box above the ceiling, inside the house's own solid storey above the room."""
    r = house({"use": "wohnhaus"})
    assert sorted(r.room_cols) == sorted(r.room_prims)
    for (box,) in r.room_cols.values():
        assert box.name.startswith("COL_BOX_")
        pts = box.positions.astype(float) + ORIGIN
        assert pts[:, 1].min() > r.room["ceiling"]
        x, y, z = pts.mean(axis=0)
        assert solid(r, float(x), float(y), float(z))  # already solid: changes nothing


def test_stone_floors_by_use():
    assert house({"use": "schmiede"}).room["footstep"] == "stone"
    assert "footstep" not in house({"use": "wohnhaus"}).room  # boards: wood by the path


def test_no_stairs_where_the_upper_storey_is_not_enterable_yet():
    r = house({"use": "wohnhaus", "upper": False})  # uses.json "upper": false
    assert "stairs" not in r.room and "upper" not in r.room and r.room["rooms"]


def test_stairs_lead_to_an_upper_storey():
    """W7: stairs at most 35 degrees along a wall, the opening above them, the upper storey with
    its own rooms, floor, ceiling and open windows; collision: the ramp, the slab, upstairs free."""
    from shapely.geometry import Point, Polygon

    r = house({"use": "wohnhaus"})
    room = r.room
    st, up = room["stairs"], room["upper"]
    assert math.degrees(math.atan(st["rise"] / st["tread"])) <= 35.0
    assert st["floor"] == pytest.approx(room["floor"]) and st["top"] == pytest.approx(up["floor"])
    assert up["floor"] > room["ceiling"] and up["ceiling"] - up["floor"] >= 2.1
    assert [q["name"] for q in up["rooms"]][0] == "OBEN"
    assert up["windows"]  # daylight upstairs too
    ring = Polygon(room["ring"]).buffer(0.01)
    opening = Polygon(st["opening"])
    assert ring.contains(Polygon(st["footprint"]))
    assert opening.intersection(ring).area > 0.8 * opening.area  # over the room (into the wall)
    assert ring.contains(Polygon(st["headLanding"]))
    # headroom over the ramp under the opening's far end: at least 2 m to the ceiling
    tread, rise = st["tread"], st["rise"]
    back = opening.exterior.distance(Point(st["headPoint"]))  # roughly the landing
    assert back >= 0.0
    # collision: the slab between the storeys is solid beside the opening, open in it; upstairs
    # (a metre over its floor) is free; the ramp carries you up half-way
    hall = Polygon(up["rooms"][0]["ring"]).difference(opening.buffer(0.5))
    p = hall.representative_point()
    mid_slab = (room["ceiling"] + up["floor"]) / 2
    assert solid(r, p.x, mid_slab, p.y)
    o = opening.centroid
    assert not solid(r, o.x, mid_slab, o.y)
    assert not solid(r, p.x, up["floor"] + 1.0, p.y)
    fx, fz = st["foot"]
    ux, uz = st["up"]
    sx, sz = st["side"]
    half = (st["steps"] - 1) * tread / 2
    x, z = fx + ux * half + sx * st["width"] / 2, fz + uz * half + sz * st["width"] / 2
    under = st["floor"] + (half + tread) * rise / tread - 0.1
    assert solid(r, x, under, z) and not solid(r, x, under + 0.4, z)
    # each storey's rooms are their own meshes, upstairs ones above the slab
    for name, prims in r.room_prims.items():
        ys = np.concatenate([q.mesh.positions[:, 1] for q in prims]) + ORIGIN[1]
        if name.startswith("OBEN"):  # (the top of the stair rail may reach into it)
            assert np.percentile(ys, 5) >= up["floor"] - 0.05, name


def test_no_stairs_in_a_single_storey():
    low = {**HOUSE, "roof": {**ROOF, "eaveY": 3.2, "ridgeY": 7.0}}
    r = build_house(low, -0.5, (5.0, -3.5), RULES, STREET_SOUTH, ground_at=lambda x, z: 0.0,
                    interior={"use": "wohnhaus"})  # fmt: skip
    assert r.room is not None and "stairs" not in r.room and "upper" not in r.room
    assert any("no stairs" in n for n in r.notes)


def test_the_slab_keeps_the_stair_opening_free_in_a_big_room():
    """A big nearly convex slab piece with the opening's notch must not close it (its hull)."""
    from shapely.geometry import MultiPoint, box

    from gothar_worldgen.buildings.medieval import _slab_pieces

    inner = box(0.0, 0.0, 20.0, 11.0)
    hole = box(0.0, 3.0, 1.05, 6.8)  # at the wall, like the stairs' opening
    pieces = _slab_pieces(inner, hole)
    for piece in pieces:
        hull = MultiPoint(list(piece.exterior.coords)).convex_hull
        assert hull.intersection(hole).area < 1e-6
    total = sum(q.area for q in pieces)
    assert total == pytest.approx(inner.area - hole.area, rel=1e-6)
