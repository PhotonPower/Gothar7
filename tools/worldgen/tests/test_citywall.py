import json
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import MultiPoint, Polygon, box

from gothar_worldgen.assemble.world import VobIds, assemble
from gothar_worldgen.buildings.collision import body_is_closed
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.buildings.medieval import Rules, load_rules
from gothar_worldgen.walls.citywall import (
    CourseError,
    _Frame3,
    build_wall,
    generate_citywall,
    load_course,
    open_wall_ends,
    plan_wall,
    walk_height,
)

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
CW = RULES.data["cityWall"]
# A 120 x 80 m town; the OSM piece is the south side, the rest are hand-set points.
FEATURES = [{"osmId": "w1", "kind": "city_wall", "geometry": "line",
             "points": [[0.0, 0.0], [120.0, 0.0]]}]  # fmt: skip
HOUSE = Polygon([(40.0, -84.0), (52.0, -84.0), (52.0, -74.0), (40.0, -74.0)])  # on the north side


def course_doc(**extra) -> dict:
    doc = {"format": "gothar-city-wall", "version": 1,
           "ring": [{"osm": "w1"}, {"points": [[120, -80], [0, -80]]}],
           "gates": [{"key": "ost", "name": "Osttor", "kind": "tower", "at": [120, -40]},
                     {"key": "sued", "kind": "pforte", "at": [60, 0]}]}  # fmt: skip
    doc.update(extra)
    return doc


def flat(x: float, z: float) -> float:
    return 0.0


def slope(x: float, z: float) -> float:
    return 0.05 * x


def plan_for(height=flat, houses=(HOUSE,)):
    course = load_course(course_doc(), FEATURES)
    return course, plan_wall(course, list(houses), height, RULES)


def test_course_is_a_closed_counter_clockwise_ring_from_osm_and_points():
    course = load_course(course_doc(), FEATURES)
    ring = Polygon(course.ring)
    assert ring.is_valid and ring.area == pytest.approx(120 * 80)
    shoelace = sum(course.ring[i - 1][0] * course.ring[i][1] - course.ring[i][0]
                   * course.ring[i - 1][1] for i in range(len(course.ring)))  # fmt: skip
    assert shoelace > 0
    reversed_osm = [{**FEATURES[0], "points": [[120.0, 0.0], [0.0, 0.0]]}]
    assert Polygon(load_course(course_doc(), reversed_osm).ring).area == pytest.approx(9600)
    with pytest.raises(CourseError, match="w9"):
        load_course(course_doc(ring=[{"osm": "w9"}]), FEATURES)
    with pytest.raises(CourseError, match="kind"):
        load_course(course_doc(gates=[{"key": "x", "kind": "door", "at": [0, 0]}]), FEATURES)


def test_wall_is_left_out_under_houses_and_its_ends_reach_into_them():
    course, (plan, prof, spec) = plan_for()
    path = plan.path
    for a, b in plan.free:
        for s in np.linspace(a, b, 40):
            p = path.point(s)
            if HOUSE.buffer(-1.3).contains(_pt(p)):
                pytest.fail("wall inside the house")
    assert open_wall_ends(plan, [HOUSE], RULES) == []
    built = sum(b - a for a, b in plan.free)
    gate_span = CW["gate"]["alongM"] - 2 * CW["stossM"]
    assert built == pytest.approx(path.length - gate_span - (12.0 - 2 * 1.2), abs=0.6)


def _pt(p):
    from shapely.geometry import Point

    return Point(float(p[0]), float(p[1]))


def test_towers_gates_posterns_and_stairs_are_placed():
    course, (plan, prof, spec) = plan_for()
    assert [g.key for g, _ in plan.gates] == ["ost"]
    assert [g.key for g, _ in plan.pfortes] == ["sued"]
    assert len(plan.stairs) == 1
    s_all = sorted(plan.towers + [s for _, s in plan.gates])
    gaps = np.diff(s_all + [s_all[0] + plan.path.length])
    assert len(plan.towers) >= 4 and gaps.max() < 2 * CW["tower"]["everyM"]
    for s in plan.towers:  # never on the gate, the postern or the house
        assert all(abs(s - sp) > 3 for _, sp in plan.pfortes)
        f = _Frame3(plan.path, s)
        assert not HOUSE.intersects(Polygon([f.p(-2.5, 0.9), f.p(2.5, 0.9), f.p(2.5, 3.4),
                                             f.p(-2.5, 3.4)]))  # fmt: skip


@pytest.mark.parametrize("height", [flat, slope])
def test_crown_follows_the_ground_and_the_foot_is_buried(height):
    course, (plan, prof, spec) = plan_for(height)
    for a, b in plan.free:
        for s in np.linspace(a + 6, b - 6, 10) if b - a > 12 else []:
            g = prof.ground(s)
            walk = walk_height(prof, spec, s)
            assert walk == pytest.approx(g + CW["heightM"] - CW["parapetHighM"], abs=0.35)
    pieces, _ = build_wall(course, [HOUSE], height, RULES)
    for piece in pieces:
        for part in piece.collision:
            pos = part.positions + np.asarray(piece.origin, np.float32)
            bottom = pos[pos[:, 1] <= pos[:, 1].min() + 1e-3]
            for x, y, z in bottom:
                ground = height(float(x), float(z))
                assert y <= ground + 1e-3 or y > ground + 1.0  # buried, or a lintel/vault block


def _bodies(piece):
    out = []
    for part in piece.collision:
        pos = part.positions.astype(float) + np.asarray(piece.origin)
        out.append((MultiPoint([(x, z) for x, _, z in pos]).convex_hull, pos[:, 1].min(),
                    pos[:, 1].max()))  # fmt: skip
    return out


def test_gate_and_postern_passages_are_free_of_collision():
    course, (plan, prof, spec) = plan_for()
    pieces, _ = build_wall(course, [HOUSE], flat, RULES)
    bodies = [b for p in pieces for b in _bodies(p)]
    g = CW["gate"]
    for _, s in plan.gates:
        f = _Frame3(plan.path, s)
        w = g["passageW"] / 2 - 0.1
        lane = Polygon([f.p(-w, -6), f.p(w, -6), f.p(w, 6), f.p(-w, 6)])
        for hull, y0, y1 in bodies:
            assert not (hull.intersects(lane) and y0 < g["springH"] - 0.1 and y1 > 0.1)
    for _, s in plan.pfortes:
        f = _Frame3(plan.path, s)
        w = CW["pforte"]["w"] / 2 - 0.1
        lane = Polygon([f.p(-w, -3), f.p(w, -3), f.p(w, 3), f.p(-w, 3)])
        for hull, y0, y1 in bodies:
            assert not (hull.intersects(lane) and y0 < CW["pforte"]["h"] - 0.1 and y1 > 0.1)


def test_collision_bodies_are_closed_and_within_the_budget():
    course, _ = plan_for(slope)
    pieces, _ = build_wall(course, [HOUSE], slope, RULES)
    kinds = {p.kind for p in pieces}
    assert kinds == {"wall", "gate"}
    for piece in pieces:
        assert piece.collision and piece.collision_triangles <= CW["collisionPerFile"]
        for part in piece.collision:
            assert part.name.startswith("COL_HULL_") and body_is_closed(part)


def test_stairs_climb_gently_to_the_walk_and_the_ramp_is_flush():
    course, (plan, prof, spec) = plan_for(slope)
    ((_, st),) = plan.stairs
    assert st.rise <= CW["stairs"]["rise"] + 1e-9
    assert st.top == pytest.approx(walk_height(prof, spec, st.s_top))
    assert np.degrees(np.arctan2(st.rise, st.run)) <= 50.0
    pieces, _ = build_wall(course, [HOUSE], slope, RULES)
    gate = next(p for p in pieces if p.kind == "gate")
    ramp = gate.collision[-1].positions + np.asarray(gate.origin, np.float32)
    assert ramp[:, 1].max() == pytest.approx(st.top, abs=1e-3)  # flush with the walk
    assert ramp[ramp[:, 1] > ramp[:, 1].min() + 0.01][:, 1].min() - st.bottom <= 0.3 + 1e-3


def test_parapet_stays_climbable_and_merlons_are_partly_gone():
    course, _ = plan_for()
    pieces, plan = build_wall(course, [HOUSE], flat, RULES)
    walk = CW["heightM"] - CW["parapetHighM"]
    tops = [
        b[2]
        for p in pieces
        if p.kind == "wall"
        for b in _bodies(p)
        if b[2] < CW["tower"]["heightM"] - 1.0
    ]  # wall bodies, not the towers
    assert max(tops) <= walk + CW["parapetHighM"] + 0.15  # no collision on the merlons
    merlons = sum(p.merlons for p in pieces)
    built = sum(b - a for a, b in plan.free)
    possible = built / (CW["merlon"]["w"] + CW["merlon"]["gap"])
    assert 0.6 * possible < merlons < possible


def test_generate_writes_files_and_assemble_places_gameplay_vobs(tmp_path: Path):
    course = load_course(course_doc(), FEATURES)
    index = generate_citywall(course, [HOUSE], flat, RULES, tmp_path, "worlds/t/generated/cw")
    again = generate_citywall(course, [HOUSE], flat, RULES, tmp_path, "worlds/t/generated/cw")
    assert index == again
    st = index["stats"]
    assert st["openEnds"] == [] and st["gateTowers"] == 1 and st["pfortes"] == 1
    assert st["triangles"] <= CW["budgetTriangles"]
    for e in index["entries"]:
        doc, _ = read_glb((tmp_path / f"{e['id']}.glb").read_bytes())
        assert any(n["name"].startswith("COL_HULL_") for n in doc["nodes"])
    terrain = {"version": 1, "name": "t", "terrain": {"version": 1}}
    ids = VobIds({}, 1)
    res = assemble(terrain, {"entries": []}, None, ids, "t", citywall=index)
    walls = [v for v in res.world["vobs"] if v["name"].startswith("CITYWALL_")]
    assert len(walls) == len(index["entries"])
    assert all(v["category"] == "gameplay" and v["type"] == "mesh" for v in walls)
    res2 = assemble(terrain, {"entries": []}, res.world, ids, "t", citywall=index)
    assert res2.added == 0 and res2.updated == 0
    assert json.dumps(res2.world) == json.dumps(res.world)


def test_zwinger_walls_are_lower_without_a_walk():
    course = load_course(course_doc(zwinger=[{"points": [[20, -20], [100, -20]]}]), FEATURES)
    pieces, _ = build_wall(course, [], flat, RULES)
    z = [p for p in pieces if p.kind == "zwinger"]
    assert len(z) == 1 and z[0].length == pytest.approx(80.0)
    top = max(b[2] for b in _bodies(z[0]))
    assert top == pytest.approx(CW["zwinger"]["heightM"], abs=0.05)


def test_rules_check_the_arch_and_the_cross_section():
    bad = Rules({**RULES.data, "cityWall": {**CW, "gate": {**CW["gate"], "apexH": 3.0}}})
    with pytest.raises(CourseError, match="pointed arch"):
        plan_wall(load_course(course_doc(), FEATURES), [], flat, bad)
    bad = Rules({**RULES.data, "cityWall": {**CW, "walkM": 1.0}})
    with pytest.raises(CourseError, match="walkM"):
        plan_wall(load_course(course_doc(), FEATURES), [], flat, bad)
    assert box(0, 0, 1, 1).area == 1.0
