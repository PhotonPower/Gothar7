from collections import Counter

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.buildings.gltf import MeshData
from gothar_worldgen.buildings.massing import Mass, build_mesh, masses_for_building

RECT = ((0.0, 0.0), (10.0, 0.0), (10.0, -6.0), (0.0, -6.0))  # 10 m east, 6 m north
L_SHAPE = ((0.0, 0.0), (12.0, 0.0), (12.0, -5.0), (5.0, -5.0), (5.0, -12.0), (0.0, -12.0))


def triangles(m: MeshData) -> np.ndarray:
    return m.positions[m.indices.reshape(-1, 3)].astype(np.float64)


def boundary_edges(m: MeshData) -> list[tuple]:
    """Edges used by only one triangle (positions welded to mm)."""
    count: Counter = Counter()
    for tri in triangles(m):
        keys = [tuple(np.round(p, 3)) for p in tri]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            count[tuple(sorted((keys[a], keys[b])))] += 1
    return [e for e, n in count.items() if n == 1]


def check_basics(m: MeshData) -> None:
    tris = triangles(m)
    face_n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    face_n /= np.linalg.norm(face_n, axis=1, keepdims=True)
    stored = m.normals[m.indices.reshape(-1, 3)[:, 0]]
    # Counter-clockwise winding agrees with the stored flat normal.
    assert np.all(np.einsum("ij,ij->i", face_n, stored) > 0.999)
    np.testing.assert_allclose(np.linalg.norm(m.normals, axis=1), 1.0, atol=1e-5)
    # Closed except for the open bottom: every boundary edge lies at y = 0 (the base).
    for a, b in boundary_edges(m):
        assert a[1] == 0.0 and b[1] == 0.0


def test_saddle_roof_on_a_rectangle():
    mass = Mass(RECT, eave_y=5.0, ridge_y=8.0, roof="saddle", ridge_dir=(1.0, 0.0))
    m = build_mesh([mass], 0.0, (5.0, -3.0))
    check_basics(m)
    ys = m.positions[:, 1]
    assert ys.min() == 0.0 and ys.max() == pytest.approx(8.0)
    # Ridge along x in the middle (z = -3 -> local 0), eaves at 5 m on the long sides.
    top = m.positions[np.isclose(ys, 8.0)]
    np.testing.assert_allclose(top[:, 2], 0.0, atol=1e-5)
    roof_y = {round(float(y), 3) for y in ys}
    assert {0.0, 5.0, 8.0} <= roof_y
    # Gable walls: the short east/west walls reach the ridge.
    assert np.any(np.isclose(m.positions[:, 0], 5.0) & np.isclose(ys, 8.0))


def test_roof_covers_exactly_the_footprint():
    for footprint in (RECT, L_SHAPE):
        for roof in ("flat", "saddle", "shed"):
            m = build_mesh([Mass(footprint, 4.0, 7.0, roof, (1.0, 0.0))], 0.0, (0.0, 0.0))
            check_basics(m)
            tris = triangles(m)
            up = m.normals[m.indices.reshape(-1, 3)[:, 0]][:, 1] > 0.1
            xz = tris[up][:, :, [0, 2]]
            e1, e2 = xz[:, 1] - xz[:, 0], xz[:, 2] - xz[:, 0]
            area = np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]).sum() / 2
            assert area == pytest.approx(Polygon(footprint).area, rel=1e-6)


def test_walls_face_outward_for_both_ring_orientations():
    for ring in (RECT, tuple(reversed(RECT))):
        m = build_mesh([Mass(ring, 3.0, 3.0, "flat")], 0.0, (5.0, -3.0))
        check_basics(m)
        tris = triangles(m)
        n = m.normals[m.indices.reshape(-1, 3)[:, 0]]
        walls = np.abs(n[:, 1]) < 0.01
        centre = tris[walls].mean(axis=1)
        assert np.all(np.einsum("ij,ij->i", n[walls][:, [0, 2]], centre[:, [0, 2]]) > 0)


def test_shed_rises_across_the_ridge_direction():
    m = build_mesh([Mass(RECT, 3.0, 5.0, "shed", (1.0, 0.0))], 0.0, (0.0, 0.0))
    check_basics(m)
    roof = m.positions[m.normals[:, 1] > 0.1]
    # Rises along v = (-0, 1) * ... : across x; constant along the ridge (x).
    for z in np.unique(np.round(roof[:, 2], 3)):
        assert np.ptp(roof[np.isclose(roof[:, 2], z), 1]) < 1e-5


def test_masses_and_fallbacks():
    roof = {"type": "mixed", "eaveY": 6.0, "ridgeY": 10.0, "ridgeDir": [0.0, 2.0]}
    b = {"id": "B", "groundY": 1.0, "footprint": list(RECT), "heightM": 9.0, "roof": roof}
    r = masses_for_building(b)
    assert r.notes == ["mixed->saddle"] and r.masses[0].roof == "saddle"
    assert r.masses[0].ridge_dir == (0.0, 1.0)
    flat = masses_for_building({**b, "roof": {"type": "other", "eaveY": 6.0, "ridgeY": 10.0}})
    assert flat.notes == ["other->flat"] and flat.masses[0].ridge_y == 8.0
    low = masses_for_building({**b, "roof": {"type": "saddle", "eaveY": 6.0, "ridgeY": 6.1}})
    assert low.masses[0].roof == "flat"
    hip = masses_for_building({**b, "roof": {"type": "hip", "eaveY": 6.0, "ridgeY": 9.0}})
    assert hip.notes == ["hip->saddle"]
    no_roof = masses_for_building(
        {"id": "C", "groundY": 2.0, "heightM": 5.0, "footprint": list(RECT)}
    )
    assert (no_roof.masses[0].roof, no_roof.masses[0].ridge_y) == ("flat", 7.0)


def test_parts_are_built_separately():
    part_a = {
        "footprint": [[0, 0], [5, 0], [5, -5], [0, -5]],
        "groundY": 0.0,
        "roof": {"type": "saddle", "eaveY": 4.0, "ridgeY": 7.0, "ridgeDir": [1, 0]},
    }
    part_b = {"footprint": [[5, 0], [9, 0], [9, -5], [5, -5]], "groundY": 0.5,
              "roof": {"type": "flat", "eaveY": 3.0, "ridgeY": 3.0}}  # fmt: skip
    b = {"id": "P", "groundY": 0.0, "footprint": [[0, 0], [9, 0], [9, -5], [0, -5]],
         "parts": [part_a, part_b]}  # fmt: skip
    r = masses_for_building(b)
    assert [m.roof for m in r.masses] == ["saddle", "flat"]
    m = build_mesh(r.masses, 0.0, (4.5, -2.5))
    assert m.positions[:, 1].max() == pytest.approx(7.0)


def test_unusable_geometry():
    with pytest.raises(ValueError):
        build_mesh([Mass(((0, 0), (1, 0), (2, 0)), 3.0, 3.0, "flat")], 0.0, (0.0, 0.0))
    # A self-intersecting ring is repaired instead of failing.
    bow = ((0.0, 0.0), (4.0, -4.0), (4.0, 0.0), (0.0, -4.0))
    assert build_mesh([Mass(bow, 3.0, 3.0, "flat")], 0.0, (0.0, 0.0)).triangle_count > 0
