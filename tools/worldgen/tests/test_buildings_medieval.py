import random
from collections import Counter
from pathlib import Path

import numpy as np
import pytest
from shapely.geometry import Polygon

from gothar_worldgen.buildings.gltf import glb_bytes_multi, read_glb
from gothar_worldgen.buildings.medieval import (
    ROLES,
    Opening,
    Rules,
    StreetIndex,
    build_house,
    load_rules,
    make_frame,
    offset_ring,
    procedural_openings,
    storey_heights,
    timber_segments,
)
from gothar_worldgen.facade.overrides import from_json

RULES = load_rules(Path(__file__).resolve().parents[1] / "data" / "building_rules.json")
# geometry and palette tests without the W5 textures (those: test_textures.py)
RULES.data["textures"] = {}
# 10 m x 7 m, counter-clockwise north-up like buildings.json; edge 0 is the south side (z = 0).
RING = [[0.0, 0.0], [10.0, 0.0], [10.0, -7.0], [0.0, -7.0]]
ROOF = {"type": "saddle", "eaveY": 8.4, "ridgeY": 13.0, "ridgeDir": [1.0, 0.0]}
HOUSE = {"id": "H1", "groundY": 0.0, "footprint": RING, "roof": ROOF}
OZ = -3.5  # origin z used below: world z = local z + OZ
STREET_SOUTH = StreetIndex([{"points": [[-20.0, 5.0], [30.0, 5.0]]}])
# A timber-framed house with jetties for the geometry tests (style decided per override).
TIMBERED = from_json({"id": "H1", "style": "handwerkerhaus", "jettyM": 0.25})


def kind(material: str) -> str:
    """Palette entry -> role group."""
    if material.startswith("timber"):
        return "timber"
    if material.startswith("roof"):
        return "roof"
    return {"stone": "stone", "frame": "frame"}.get(material, "infill")


def rng() -> random.Random:
    return random.Random(1)


def test_rules_file_is_decided():
    assert RULES.data["status"].startswith("festgelegt") and "2026-10-03" in RULES.data["status"]
    palette = {k for k in RULES.data["palette"] if k != "note"}
    # a budget, no engine limit: it batches per mesh and material value, each value costs a few
    # draw batches per visible cell (engine 2026-10-08)
    assert len(palette) <= 20
    patterns = set(RULES.data["timber"]["patterns"]) - {"note"}
    assert {"mann", "halber_mann", "einfach", "andreaskreuz", "feuerbock", "raute"} <= patterns
    assert "roof_slate" not in palette  # slate is untypical in Württemberg


def test_storey_heights():
    hs = storey_heights(8.4, RULES, None, rng())
    assert len(hs) == 3 and sum(hs) == pytest.approx(8.4)
    assert storey_heights(3.0, RULES, None, rng()) == [3.0]
    assert len(storey_heights(30.0, RULES, None, rng())) == RULES.data["storeys"]["max"]
    assert storey_heights(8.0, RULES, [3.0, 3.0], rng()) == [3.0, 5.0]  # last one stretched
    assert sum(storey_heights(6.0, RULES, [3.0, 3.0, 3.0], rng())) == pytest.approx(6.0)  # scaled
    assert storey_heights(0.0, RULES, None, rng()) == []


def test_offset_ring():
    ring = [tuple(p) for p in RING]
    all_out = offset_ring(ring, [0.5] * 4)
    assert Polygon([p for p, _ in all_out]).area == pytest.approx(11 * 8)
    south = offset_ring(ring, [0.5, 0.0, 0.0, 0.0])
    pts = [p for p, _ in south]
    assert [e for _, e in south] == [0, 1, 2, 3]
    assert pts[0] == pytest.approx((0.0, 0.5)) and pts[1] == pytest.approx((10.0, 0.5))
    # Collinear neighbours with different offsets get a short connecting edge (index -1).
    straight = [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (10.0, -5.0), (0.0, -5.0)]
    stepped = offset_ring(straight, [0.5, 0.0, 0.0, 0.0, 0.0])
    assert -1 in [e for _, e in stepped]


def test_frame_left_is_seen_from_outside():
    f = make_frame((0.0, 0.0), (10.0, 0.0), (0.0, 1.0), 0.0)  # south facade, looking north
    assert (f.lx, f.lz) == (0.0, 0.0) and f.width == 10.0  # west is on the left
    g = make_frame((10.0, 0.0), (0.0, 0.0), (0.0, 1.0), 0.0)
    assert (g.lx, g.lz) == (0.0, 0.0)


def test_procedural_openings():
    plan = procedural_openings(10.0, [3.0, 2.7, 2.7], RULES, rng(), True, [3.0, 2.7, 2.7])
    assert set(plan) == {0, 1, 2}
    assert [o.kind for o in plan[0]].count("door") == 1
    for row in plan.values():
        spans = sorted((o.u, o.u + o.w) for o in row)
        assert all(a1 <= b0 for (_, a1), (b0, _) in zip(spans, spans[1:], strict=False))
        assert all(u0 > 0 and u1 < 10.0 for u0, u1 in spans)
    assert procedural_openings(2.0, [3.0], RULES, rng(), True, [3.0]) == {}


def test_timber_is_cut_at_openings():
    door = Opening("door", 4.0, 0.0, 1.0, 2.0)
    window = Opening("window", 7.0, 0.9, 0.8, 1.0)
    segs = timber_segments(10.0, 2.7, [door, window], RULES, RULES.pattern())
    for kind, _k, p0, p1 in segs:
        mid = (p0 + p1) / 2
        for op in (door, window):
            assert not (op.u < mid[0] < op.u + op.w and op.v < mid[1] < op.v + op.h), kind
    posts = sorted(round(float(p0[0]), 3) for k, _, p0, _ in segs if k == "post")
    b = RULES.data["timber"]["beamM"]
    assert round(4.0 - b / 2, 3) in posts and round(5.0 + b / 2, 3) in posts  # posts flank the door
    fewer = timber_segments(10.0, 2.7, [], RULES, [], bays=False)
    assert len(fewer) < len(timber_segments(10.0, 2.7, [], RULES, RULES.pattern()))


def roles(result) -> dict[str, int]:
    out: Counter = Counter()
    for p in result.primitives:
        out[kind(p.material)] += p.mesh.triangle_count
    return dict(out)


def test_house_uses_palette_materials():
    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, TIMBERED)
    assert {"stone", "infill", "timber", "roof", "frame"} <= set(roles(r))
    assert r.triangles == sum(roles(r).values()) <= RULES.data["budget"]["trianglesPerBuilding"]
    palette = RULES.data["palette"]
    for p in r.primitives:  # material name = palette entry, colour = its exact value
        assert list(p.color) == palette[p.material]
    doc, _ = read_glb(glb_bytes_multi(r.primitives, "H1"))
    names = [m["name"] for m in doc["materials"]]
    assert len(names) == len(set(names)) and set(names) <= set(palette)
    assert ROLES[0] == "wall_ground"  # internal roles are mapped, never exported


def walls(result) -> np.ndarray:
    return np.concatenate(
        [p.mesh.positions for p in result.primitives if kind(p.material) in ("infill", "timber")]
    )


def test_jetty_only_on_the_street_side():
    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, TIMBERED)
    walls_ = walls(r)
    # Local z: the street is south (+z). Upper storeys reach beyond the footprint only there.
    jetty = RULES.data["jetty"]["defaultM"]
    assert walls_[:, 2].max() + OZ > jetty * 1.5  # south face pushed out (2 storeys of jetty)
    assert walls_[:, 2].min() + OZ >= -7.0 - RULES.data["timber"]["depthM"] - 1e-3  # north stays
    plain = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, None, TIMBERED)  # no streets: no jetty
    infill = np.concatenate(
        [p.mesh.positions for p in plain.primitives if kind(p.material) == "infill"]
    )
    assert infill[:, 2].max() + OZ <= 1e-3


def test_no_coplanar_duplicate_faces():
    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, TIMBERED)
    keys = Counter()
    for p in r.primitives:
        tris = p.mesh.positions[p.mesh.indices.reshape(-1, 3)]
        for t in tris:
            keys[tuple(sorted(tuple(np.round(v, 3)) for v in t))] += 1
    assert max(keys.values()) == 1


def test_front_facade_override_is_used():
    gate = {"storey": 0, "type": "gate", "x": 3.0, "w": 3.0, "h": 2.6}
    window = {"storey": 1, "type": "window", "x": 1.0, "w": 0.7, "h": 0.9, "y": 0.8}
    front = {"edge": 0, "openings": [gate, window]}
    override = from_json(
        {
            "id": "H1",
            "style": "handwerkerhaus",
            "storeys": [3.2, 2.6, 2.6],
            "jettyM": 0.4,
            "frontFacade": front,
        }
    )
    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, None, override)
    frame = next(p for p in r.primitives if p.material == "frame").mesh.positions
    # The 3 m gate panel: frame vertices spanning x 3..6 (local -2..1) at the south face.
    south = frame[np.abs(frame[:, 2] - 3.5 + RULES.data["openings"]["revealM"]) < 1e-3]
    assert south[:, 0].min() == pytest.approx(-2.0, abs=1e-3) and south[:, 0].max() >= 1.0 - 1e-3
    infill = np.concatenate(
        [p.mesh.positions for p in r.primitives if kind(p.material) == "infill"]
    )
    assert infill[:, 2].max() + OZ == pytest.approx(0.8, abs=1e-3)  # jettyM 0.4 x 2 storeys


def test_deterministic_and_seeded():
    a = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH)
    b = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH)
    assert glb_bytes_multi(a.primitives, "x") == glb_bytes_multi(b.primitives, "x")
    seeded = build_house(
        HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, from_json({"id": "H1", "seed": 7})
    )
    assert glb_bytes_multi(seeded.primitives, "x") != glb_bytes_multi(a.primitives, "x")


def test_budget_reduces_timber_step_by_step():
    full = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, TIMBERED)
    assert full.timber_level == 0
    tight = Rules({**RULES.data, "budget": {"trianglesPerBuilding": full.triangles - 1}})
    r = build_house(HOUSE, -0.3, (5.0, -3.5), tight, STREET_SOUTH, TIMBERED)
    assert r.timber_level > 0 and r.triangles < full.triangles
    assert any("timber reduced" in n for n in r.notes)
    none = Rules({**RULES.data, "budget": {"trianglesPerBuilding": 1}})
    r3 = build_house(HOUSE, -0.3, (5.0, -3.5), none, STREET_SOUTH, TIMBERED)
    assert r3.timber_level == 4 and r3.dormers == 0  # level 4: no timber, no dormers either
    # Only the jetty undersides stay in the timber role.
    assert roles(r3).get("timber", 0) < roles(r)["timber"] < roles(full)["timber"]


def test_parts_ignore_the_front_facade():
    part = {"footprint": RING, "groundY": 0.0, "roof": HOUSE["roof"]}
    override = from_json({"id": "P", "frontFacade": {"edge": 0, "openings": []}})
    r = build_house({**HOUSE, "id": "P", "parts": [part]}, -0.3, (5.0, -3.5), RULES, None, override)
    assert any("frontFacade ignored" in n for n in r.notes)


def _passage_house(h: float = 3.0):
    passage = {"axis": [[5.0, 2.0], [5.0, -9.0]], "w": 2.5, "h": h}
    return from_json(
        {"id": "H1", "style": "handwerkerhaus", "storeys": [4.0, 2.2, 2.2], "passages": [passage]}
    )


def test_passage_leaves_a_free_corridor_through_the_ground_storey():
    from shapely.geometry import MultiPoint, box

    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, _passage_house())
    origin = np.asarray([5.0, -0.3, -3.5])
    lane = box(5.0 - 1.15, -7.5, 5.0 + 1.15, 0.5)  # the corridor, a little narrower
    below, above = [], []
    for part in r.collision.parts:
        pos = part.positions.astype(float) + origin
        hull = MultiPoint([(x, z) for x, _, z in pos]).convex_hull
        if hull.intersects(lane):
            (above if pos[:, 1].min() >= 3.0 - 1e-3 else below).append(part.name)
    assert not below and above  # free up to the clear height, the house above it
    assert not r.collision.fallback
    # no wall face crosses the corridor below its top: the facades have the opening
    for p in r.primitives:
        pos = p.mesh.positions.astype(float) + origin
        tris = pos[np.asarray(p.mesh.indices).reshape(-1, 3)]
        for t in tris:
            if t[:, 1].max() < 2.9 and np.ptp(t[:, 2]) < 1e-3:  # a facade-parallel face (z const)
                xs = t[:, 0]
                assert xs.max() <= 5.0 - 1.2 or xs.min() >= 5.0 + 1.2, p.material


def test_passage_is_lowered_under_a_low_ground_storey_and_takes_over_the_door():
    low = from_json({"id": "H1", "style": "handwerkerhaus", "storeys": [2.6, 2.9, 2.9],
                     "passages": [{"axis": [[5.0, 2.0], [5.0, -9.0]]}]})  # fmt: skip
    r = build_house(HOUSE, -0.3, (5.0, -3.5), RULES, STREET_SOUTH, low)
    assert any(n.startswith("passage lowered to") for n in r.notes)
    assert all(abs(d[0] - 5.0) > 1.75 for d in r.doors)  # the street door went into the passage


def test_houses_far_from_the_origin_keep_their_roof_slopes():
    # the ridge line used to be centred at the point nearest the origin: far houses lost it
    far = {"id": "F1", "groundY": 0.0, "roof": ROOF,
           "footprint": [[x + 300.0, z - 200.0] for x, z in RING]}  # fmt: skip
    for house in (HOUSE, far):
        r = build_house(house, -0.3, (5.0, -3.5), RULES, None, TIMBERED)
        slopes = 0
        for p in r.primitives:
            if kind(p.material) != "roof":
                continue
            t = p.mesh.positions[p.mesh.indices.reshape(-1, 3)].astype(float)
            n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
            n /= np.linalg.norm(n, axis=1, keepdims=True)
            slopes += int(((n[:, 1] > 0.3) & (n[:, 1] < 0.95)).sum())  # pitched, facing up
        assert slopes >= 4, house["id"]
