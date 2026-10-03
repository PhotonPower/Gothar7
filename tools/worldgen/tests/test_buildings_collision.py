from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon
from shapely.ops import unary_union

from gothar_worldgen.buildings.batch import generate
from gothar_worldgen.buildings.collision import (
    BUDGET,
    MERGE_TOLERANCE,
    body_is_closed,
    collision_for,
    convex_pieces,
    merge_collision,
)
from gothar_worldgen.buildings.gltf import CollisionPart, Primitive, glb_bytes_multi, read_glb
from gothar_worldgen.buildings.massing import Mass, MeshData, _Roof
from gothar_worldgen.buildings.medieval import StreetIndex, build_house, load_rules
from gothar_worldgen.facade.overrides import from_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
RECT = [(0.0, 0.0), (12.0, 0.0), (12.0, -8.0), (0.0, -8.0)]
L_SHAPE = [(0.0, 0.0), (12.0, 0.0), (12.0, -8.0), (6.0, -8.0), (6.0, -4.0), (0.0, -4.0)]
U_SHAPE = [(0.0, 0.0), (20.0, 0.0), (20.0, -12.0), (14.0, -12.0), (14.0, -4.0), (6.0, -4.0),
           (6.0, -12.0), (0.0, -12.0)]  # fmt: skip


def mass(ring, roof="saddle", direction=(1.0, 0.0)) -> Mass:
    return Mass(tuple(ring), 8.0, 13.0, roof, direction)


def outward(part: CollisionPart) -> bool:
    pos = part.positions.astype(np.float64)
    centre = pos.mean(axis=0)
    for t in part.indices.reshape(-1, 3):
        a, b, c = pos[t]
        n = np.cross(b - a, c - a)
        if np.linalg.norm(n) > 1e-9 and np.dot(n, (a + b + c) / 3 - centre) < 0:
            return False
    return True


def test_writer_adds_col_nodes_without_material_and_stays_deterministic():
    mesh = MeshData(np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], np.float32),
                    np.array([[0, 0, 1]] * 3, np.float32), np.zeros((3, 2), np.float32),
                    np.array([0, 1, 2], np.uint32))  # fmt: skip
    col = collision_for([mass(RECT)], 0.0, (0.0, 0.0)).parts
    a = glb_bytes_multi([Primitive("stone", (0.2, 0.2, 0.2, 1.0), mesh)], "H", col)
    assert a == glb_bytes_multi([Primitive("stone", (0.2, 0.2, 0.2, 1.0), mesh)], "H", col)
    doc, _ = read_glb(a)
    assert doc["scenes"][0]["nodes"] == list(range(len(doc["nodes"])))  # all at the root
    names = [n["name"] for n in doc["nodes"]]
    assert names == ["H", "COL_HULL_0"]
    prim = doc["meshes"][doc["nodes"][1]["mesh"]]["primitives"][0]
    assert "material" not in prim and set(prim["attributes"]) == {"POSITION"}
    with pytest.raises(ValueError, match="COL_"):
        glb_bytes_multi([Primitive("stone", (0, 0, 0, 1), mesh)], "H",
                        [CollisionPart("HULL", col[0].positions, col[0].indices)])  # fmt: skip


@pytest.mark.parametrize("ring,pieces", [(RECT, 1), (L_SHAPE, 2), (U_SHAPE, 3)])
def test_pieces_are_convex_cover_the_footprint_and_keep_yards_free(ring, pieces):
    poly = Polygon(ring)
    parts = convex_pieces(poly)
    assert len(parts) == pieces
    for p in parts:
        assert p.convex_hull.area <= p.area * (1 + MERGE_TOLERANCE) + 1e-9
    union = unary_union(parts)
    assert union.symmetric_difference(poly).area < 0.01 * poly.area
    assert sum(p.area for p in parts) < 1.01 * poly.area  # no real overlap
    if ring is U_SHAPE:
        assert not union.contains(Polygon([(7, -6), (13, -6), (13, -11), (7, -11)]))


@pytest.mark.parametrize("ring", [RECT, L_SHAPE, U_SHAPE])
@pytest.mark.parametrize("roof", ["saddle", "shed", "flat"])
def test_bodies_are_closed_outward_and_follow_the_roof(ring, roof):
    m = mass(ring, roof)
    result = collision_for([m], -0.3, (0.0, 0.0))
    assert not result.fallback and result.triangles <= BUDGET
    plan = _Roof(m, Polygon(ring))
    for part in result.parts:
        assert part.name.startswith("COL_HULL_")
        assert body_is_closed(part) and outward(part)
        pos = part.positions
        assert pos[:, 1].min() == pytest.approx(-0.3 + 0.3, abs=1e-4)  # base, relative to origin
        for x, y, z in pos[pos[:, 1] > 0.5]:
            assert y - 0.3 == pytest.approx(plan.height(x, z), abs=1e-3)


def test_collision_stays_under_the_visible_roof_of_a_house():
    roof = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.4, "ridgeDir": [1.0, 0.0]}
    house = {"id": "H", "inCore": True, "groundY": 0.0, "footprint": [list(p) for p in RECT],
             "roof": roof}  # fmt: skip
    street = StreetIndex([{"points": [[-20.0, 6.0], [40.0, 6.0]]}])
    r = build_house(house, 0.0, (6.0, -4.0), RULES, street, from_json({"id": "H", "age": 0.0}))
    assert r.collision is not None and len(r.collision.parts) == 1
    col = r.collision.parts[0].positions
    roof = np.concatenate([p.mesh.positions for p in r.primitives if p.material.startswith("roof")])
    assert col[:, 1].max() <= roof[:, 1].max()
    assert col[:, 1].max() >= roof[:, 1].max() - RULES.data["roof"]["thicknessM"] - 0.3
    # Ground footprint, no jetty and no overhang.
    assert col[:, 0].min() == pytest.approx(-6.0, abs=1e-3)
    assert col[:, 2].max() == pytest.approx(4.0, abs=1e-3)


def test_too_many_pieces_fall_back_to_a_triangle_mesh():
    comb = [(0.0, 0.0), (40.0, 0.0), (40.0, -10.0)]
    for k in range(10, 0, -1):  # ten teeth: far more than 8 convex pieces
        comb += [(4.0 * k - 1.0, -10.0), (4.0 * k - 1.0, -3.0), (4.0 * k - 3.0, -3.0),
                 (4.0 * k - 3.0, -10.0)]  # fmt: skip
    comb += [(0.0, -10.0)]
    result = collision_for([mass(comb, "flat", None)], 0.0, (0.0, 0.0))
    assert result.fallback and [p.name for p in result.parts] == ["COL_0"]


def test_cells_shift_and_renumber_the_parts():
    a = collision_for([mass(RECT)], 0.0, (6.0, -4.0)).parts
    b = collision_for([mass(L_SHAPE)], 0.0, (6.0, -4.0)).parts
    merged = merge_collision([(a, (100.0, 0.0, 0.0)), (b, (110.0, 1.0, 0.0))], (105.0, 0.0, 0.0))
    assert [p.name for p in merged] == ["COL_HULL_0", "COL_HULL_1", "COL_HULL_2"]
    assert merged[0].positions[:, 0].min() == pytest.approx(a[0].positions[:, 0].min() - 5.0)
    assert merged[1].positions[:, 1].min() == pytest.approx(b[0].positions[:, 1].min() + 1.0)


def test_batch_writes_col_nodes_and_reports_them(tmp_path: Path):
    b = {"id": "B1", "inCore": True, "footprint": [list(p) for p in L_SHAPE], "groundY": 0.0,
         "roof": {"type": "saddle", "eaveY": 7.0, "ridgeY": 10.0, "ridgeDir": [1, 0]}}  # fmt: skip
    res = generate([b], None, tmp_path, "worlds/t/generated/buildings")
    entry = res.index["entries"][0]
    assert entry["collisionTriangles"] > 0
    doc, _ = read_glb((tmp_path / entry["mesh"].rsplit("/", 1)[-1]).read_bytes())
    assert sum(n["name"].startswith("COL_HULL_") for n in doc["nodes"]) == 2
    col = res.index["stats"]["collision"]
    assert col["hulls"] == 2 and col["decomposed"] == 1 and col["fallbacks"] == 0
