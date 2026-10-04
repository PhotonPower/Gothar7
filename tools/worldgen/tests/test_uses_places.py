import json
import math
import re
from pathlib import Path

import pytest
from shapely.geometry import Point, box

from gothar_worldgen.uses.places import (
    DOOR_KEEP_M,
    MOB_SLOT,
    SPACING_M,
    UsesError,
    load_uses,
    mob_vobs,
    plan_places,
)

HOUSE = box(0.0, -7.0, 10.0, 0.0)  # door on the south side (z = 0), outward +z
DOOR = [5.0, 0.6, 0.0, "ground", 0.0, 1.0]
NAME = re.compile(r"^(WP|FP|MOB)_[A-Z0-9_]+$")


def write(tmp_path: Path, houses, uses) -> Path:  # noqa: ANN001
    p = tmp_path / "uses.json"
    p.write_text(json.dumps({"version": 1, "houses": houses, "uses": uses}), encoding="utf-8")
    return p


def free(x: float, z: float, r: float) -> bool:
    return HOUSE.distance(Point(x, z)) >= r


def flat(x: float, z: float) -> float:
    return 2.0


def test_uses_file_is_checked(tmp_path: Path):
    ok = write(
        tmp_path, [{"id": "H1", "use": "schmiede"}], {"schmiede": {"freepoints": ["REPAIR:1"]}}
    )
    assert load_uses(ok).houses[0].use == "schmiede"
    for houses, uses, msg in (
        ([{"id": "H1", "use": "burg"}], {}, "unknown use"),
        ([{"id": "H1", "use": "bader"}, {"id": "H1", "use": "bader"}], {}, "listed twice"),
        ([{"id": "H1", "use": "bader"}], {"bader": {"freepoints": ["NAP:1"]}}, "TYPE:N"),
        ([{"id": "H1", "use": "bader"}], {"bader": {"mobs": ["throne:1"]}}, "TYPE:N"),
        ([{"use": "bader"}], {}, "needs id"),
    ):
        with pytest.raises(UsesError, match=msg):
            load_uses(write(tmp_path, houses, uses))


def test_smithy_gets_its_waypoint_anvil_and_places(tmp_path: Path):
    spec = {"schmiede": {"freepoints": ["REPAIR:1", "CHOP:1"], "mobs": ["anvil:1"]}}
    doc = load_uses(write(tmp_path, [{"id": "DEBW_00100061ZnP", "use": "schmiede"}], spec))
    plan = plan_places(doc, {"DEBW_00100061ZnP": DOOR}, free, flat)
    assert plan.failed == []
    by = {p.kind: [q for q in plan.places if q.kind == p.kind] for p in plan.places}
    (wp,) = by["wp"]
    assert wp.name == "WP_LEO_SCHMIEDE_ZNP" and wp.pos == pytest.approx((5.0, 1.8))
    (anvil,) = by["mob"]
    assert anvil.mob == "anvil" and abs(anvil.pos[0] - 5.0) >= DOOR_KEEP_M  # beside the door
    assert anvil.pos[1] < 1.0 and anvil.dir == (0.0, 1.0)  # back to the wall, facing out
    repair = next(p for p in by["fp"] if p.name.startswith("FP_REPAIR_"))
    assert repair.pos == pytest.approx((anvil.pos[0], anvil.pos[1] + MOB_SLOT["anvil"]))
    assert repair.dir == (-0.0, -1.0)  # facing the anvil
    for p in plan.places:
        assert NAME.match(p.name), p.name
        assert HOUSE.distance(Point(*p.pos)) > 0.0  # nothing inside the house
    fps = [p.pos for p in by["fp"]] + [wp.pos]
    assert all(math.dist(a, b) >= SPACING_M - 1e-9 for i, a in enumerate(fps) for b in fps[i + 1 :])


def test_pairs_houses_without_door_and_crowded_fronts(tmp_path: Path):
    doc = load_uses(write(tmp_path, [{"id": "A", "use": "amtshaus"}, {"id": "B", "use": "bader"}],
                          {"amtshaus": {"freepoints": ["SMALLTALK:1"]},
                           "bader": {"freepoints": ["STAND:1"]}}))  # fmt: skip
    plan = plan_places(doc, {"A": DOOR, "B": [5.0, 0.6, 0.0, "blocked", 0.0, 1.0]}, free, flat)
    talk = [p for p in plan.places if p.name.startswith("FP_SMALLTALK_LEO_AMTSHAUS_A_")]
    assert len(talk) == 2 and talk[0].dir == (-talk[1].dir[0], -talk[1].dir[1])  # face each other
    assert {"house": "B", "what": "house", "reason": "no usable door"} in plan.failed
    wall = box(0.0, 0.0, 10.0, 3.0)  # something right in front: nothing fits

    def crowded(x: float, z: float, r: float) -> bool:
        return free(x, z, r) and wall.distance(Point(x, z)) >= r

    plan = plan_places(doc, {"A": DOOR}, crowded, flat)
    assert plan.places == [] and plan.failed[0]["what"] == "waypoint"


def test_mob_vobs_face_out_of_the_house(tmp_path: Path):
    doc = load_uses(write(tmp_path, [{"id": "H1", "use": "gasthaus"}],
                          {"gasthaus": {"mobs": ["bench:2"]}}))  # fmt: skip
    west = box(-10, 0, 0, 10)  # the house; its door on the east side

    def open_east(x: float, z: float, r: float) -> bool:
        return west.distance(Point(x, z)) >= r

    plan = plan_places(doc, {"H1": [0.6, 5.0, 0.0, "ground", 1.0, 0.0]}, open_east, flat)
    vobs = mob_vobs(plan, flat)
    assert len(vobs) == 2 and {v["definition"] for v in vobs} == {"bench"}
    for v in vobs:
        _, qy, _, qw = v["rot"]
        ang = 2 * math.atan2(qy, qw)  # +Z turned by ang about Y
        assert (math.sin(ang), math.cos(ang)) == pytest.approx((1.0, 0.0), abs=1e-4)
        assert v["pos"][1] == 2.0 and v["mesh"] == "mobs/bench.glb"
        assert v["key"] == f"use:{v['name']}" and v["name"].startswith("MOB_LEO_GASTHAUS_H1_BENCH_")
