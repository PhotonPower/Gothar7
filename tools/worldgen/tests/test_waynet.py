import json
from dataclasses import dataclass

import pytest
from shapely.geometry import box

from gothar_worldgen.waynet.generate import build_waynet, building_short, name_part
from gothar_worldgen.waynet.write import normalized, tidy, waynet_text


@dataclass
class Body:
    owner: str
    poly: object
    bottom: float = 0.0
    top: float = 2.0


@dataclass
class Ch:
    radius: float = 0.3
    height: float = 1.8
    step: float = 0.4
    max_slope: float = 50.0
    eye: float = 1.62


def flat(x: float, z: float) -> float:
    return 1.0


STREETS = [
    {"osmId": "w1", "highway": "residential", "name": "Marktstraße", "points": [[-60, 0], [60, 0]]},
    {"osmId": "w2", "highway": "footway", "points": [[0, 0], [0, 40]]},
    {
        "osmId": "w3",
        "highway": "service",
        "tunnel": True,
        "layer": -1,
        "points": [[-60, 5], [60, 5]],
    },
]
HOUSE = Body("BLD_DEBW_00100061ZnA", box(10, -20, 20, -6))
DOOR_BUILDING = {"id": "DEBW_00100061ZnA", "doors": [[15.0, -5.4, 1.0, "ground", 0.0, 1.0]]}


def run(**kw):
    args = dict(
        site="leonberg",
        streets=STREETS,
        buildings=[DOOR_BUILDING],
        bodies=[HOUSE],
        height=flat,
        character=Ch(),
        half_extent=100.0,
    )
    args.update(kw)
    return build_waynet(**args)


def test_names_follow_the_contract():
    assert name_part("Graf-Ulrich-Straße") == "GRAF_ULRICH_STRASSE"
    assert name_part("Hintere Gärten") == "HINTERE_GAERTEN"
    assert building_short("DEBW_00100061ZnA") == "ZNA"
    assert building_short("DEBW_00100061ZnA-T1") == "ZNA_T1"


def test_points_along_the_axes_doors_and_edges():
    res = run()
    names = {p.name for p in res.points}
    assert all(n.startswith("WP_LEO_") for n in names)
    street = [n for n in names if n.startswith("WP_LEO_MARKTSTRASSE_") and n[-3:].isdigit()]
    assert len(street) >= 120 // 12  # about every 12 m
    assert not any("W3" in n for n in names)  # the underground way is left out
    door = [n for n in names if n.endswith("_ZNA")]
    assert door == ["WP_LEO_MARKTSTRASSE_ZNA"]
    (d,) = [p for p in res.points if p.name == door[0]]
    assert d.pos[2] == pytest.approx(-5.4 + 0.6) and d.dir == (-0.0, 0.0, -1.0)  # facing the door
    edges = {(a, b) for a, b, _ in res.edges}
    assert any(door[0] in e for e in edges)  # tied to the net
    assert all(gen for _, _, gen in res.edges)
    assert res.report["components"] == 1 and res.report["mainShare"] == 1.0


def test_blocked_points_move_or_drop_and_blocked_lines_detour():
    wall = Body("CITYWALL_X", box(-1, -0.4, 1, 0.4))  # right on the axis at x = 0
    res = run(bodies=[HOUSE, wall])
    for p in res.points:
        assert not wall.poly.buffer(0.5 - 1e-6).contains(
            __import__("shapely").Point(p.pos[0], p.pos[2])
        )
    assert res.report["components"] == 1  # the net goes round the obstacle


def test_freepoints_from_landmarks():
    lm = {
        "fountains": {"marktbrunnen": (0.0, 20.0, 2.0)},
        "square": ((0.0, 20.0), 6.0),
        "gates": {"oberes_tor": (0.0, 60.0)},
        "centre": (0.0, 20.0),
        "beds": [(30.0, 30.0)],
    }
    res = run(landmarks=lm)
    kinds = {p.name.split("_")[1] for p in res.freepoints}
    assert {"SIT", "DRINK", "SMALLTALK", "ROAM", "STAND", "WATER"} <= kinds
    assert not any(
        p.name.startswith("FP_WATER_LEO_MARKT") for p in res.freepoints
    )  # not at fountains
    assert all(p.owner == "worldgen" and p.pos[1] == 1.0 for p in res.freepoints)


def test_hand_made_entries_stay_and_annotations_apply():
    first = run()
    gen_name = sorted(p.name for p in first.points if p.name.startswith("WP_LEO_MARKTSTRASSE_0"))[0]
    existing = {
        "points": [
            {"name": gen_name, "pos": [1.0, 2.0, 3.0]},  # moved by hand: no owner
            {"name": "WP_HAND_A", "pos": [50.0, 1.0, 30.0]},
        ],
        "edges": [["WP_HAND_A", gen_name]],
        "freepoints": [{"name": "FP_SIT_HAND", "pos": [0, 1, 0]}],
    }
    remove = sorted(p.name for p in first.points if p.name.startswith("WP_LEO_WEG_W2_"))[:1]
    ann = {
        "remove": remove,
        "add": {
            "points": [{"name": "WP_LEO_EXTRA", "pos": [0, 1, 39]}],
            "edges": [["WP_LEO_EXTRA", remove[0]]],
        },
    }
    res = run(existing=existing, annotations=ann)
    by = {p.name: p for p in res.points}
    assert by[gen_name].pos == (1.0, 2.0, 3.0) and by[gen_name].owner is None
    assert "WP_HAND_A" in by and any(p.name == "FP_SIT_HAND" for p in res.freepoints)
    assert ("WP_HAND_A", gen_name, False) in res.edges
    assert remove[0] not in by and "WP_LEO_EXTRA" in by
    assert not any(remove[0] in e[:2] for e in res.edges)  # no edge to a removed point


def test_writer_matches_the_engine_layout():
    block = {
        "points": [
            {"name": "WP_MARKT_02", "pos": [20, 3, -41.5], "owner": "worldgen"},
            {
                "name": "WP_MARKT_01",
                "pos": [12.5, 3.1, -40],
                "dir": [0, 0.5, 2],
                "owner": "worldgen",
            },
            {"name": "WP_KIRCHE_TUER", "pos": [30.2, 4, -38]},
        ],
        "edges": [
            ["WP_MARKT_02", "WP_MARKT_01", "worldgen"],
            ["WP_MARKT_02", "WP_KIRCHE_TUER"],
            ["WP_MARKT_01", "WP_MARKT_02"],
        ],
        "freepoints": [
            {
                "name": "FP_SIT_BRUNNEN_01",
                "pos": [1.2, 3, 0.8],
                "dir": [0, 0, -1],
                "owner": "worldgen",
            }
        ],
    }
    text = waynet_text(block)
    assert (
        '      {"name":"WP_MARKT_01","pos":[12.5,3.1,-40.0],"dir":[0.0,0.0,1.0],"owner":"worldgen"}'
    ) in text
    assert '      ["WP_KIRCHE_TUER","WP_MARKT_02"]' in text
    assert '      ["WP_MARKT_01","WP_MARKT_02"]' in text  # merged: hand-made wins
    assert text.startswith('{\n    "points": [\n      {"name":"WP_KIRCHE_TUER"')
    assert text.endswith('"owner":"worldgen"}\n    ]\n  }')
    assert waynet_text(normalized(json.loads(json.dumps(block)))) == text
    assert waynet_text({}) == '{\n    "points": [],\n    "edges": [],\n    "freepoints": []\n  }'
    assert tidy(3.1) == 3.1 and tidy(-0.0000000437) == 0.0 and tidy(-276.01001) == -276.01
    assert tidy(2.000005) == 2.000005 and tidy(0.70710677) == 0.70711  # noise: 1e-5


def test_short_steep_pieces_block_and_are_listed():
    def kink(x: float, z: float) -> float:  # a 1 m step across the street at x = 20
        return 1.0 + (1.0 if x > 20.0 else 0.0)

    res = run(height=kink)
    steep = res.report["steep"]
    assert steep and all(s["maxDeg"] > 35.0 for s in steep)
    assert any(s["street"] == "MARKTSTRASSE" for s in steep)
    for a, b, _ in res.edges:  # no edge of the net crosses the step
        pa = next(p for p in res.points if p.name == a).pos
        pb = next(p for p in res.points if p.name == b).pos
        assert (pa[0] - 20.0) * (pb[0] - 20.0) > 0 or abs(pa[0] - pb[0]) < 1e-9


def test_islands_are_tied_to_each_other_and_links_join_without_checks():
    fence = Body("HANDMADE_ZAUN", box(-60, 18, 60, 18.4))  # cuts the footway at z = 18
    res = run(bodies=[HOUSE, fence])
    assert res.report["components"] >= 2  # the far end of the footway is cut off
    gate = (("TOR", (0.0, 15.0), (0.0, 21.5)),)  # an explicit link through the fence
    linked = run(bodies=[HOUSE, fence], links=gate)
    assert linked.report["components"] == 1
    assert {"WP_LEO_TOR_1", "WP_LEO_TOR_2"} <= {p.name for p in linked.points}
    assert ("WP_LEO_TOR_1", "WP_LEO_TOR_2", True) in linked.edges


def test_writing_a_written_block_again_changes_nothing():
    # assemble reads the waynet block and writes it again: a direction that is unit length within
    # the engine's tolerance stays as written (-0.90449 must not become -0.904488)
    block = {"points": [{"name": "WP_A", "pos": [1, 2, 3], "dir": [-0.4265, 0.0, -0.90449],
                         "owner": "worldgen"}]}  # fmt: skip
    once = waynet_text(block)
    assert '"dir":[-0.4265,0.0,-0.90449]' in once
    again = json.loads("{" + '"w": ' + once + "}")["w"]
    assert waynet_text(again) == once


def test_routine_places_join_the_net():
    places = [
        {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA", "house": "DEBW_00100061ZnA",
         "pos": [15.0, -3.0], "dir": [0.0, -1.0]},
        {"kind": "fp", "name": "FP_REPAIR_LEO_SCHMIEDE_ZNA_01", "house": "DEBW_00100061ZnA",
         "pos": [12.0, -3.5], "dir": [0.0, -1.0]},
        {"kind": "mob", "name": "MOB_X", "house": "DEBW_00100061ZnA", "pos": [0, 0], "dir": [0, 1]},
    ]  # fmt: skip
    res = run(places=places)
    names = {p.name for p in res.points}
    assert "WP_LEO_SCHMIEDE_ZNA" in names and "MOB_X" not in names
    assert any("WP_LEO_SCHMIEDE_ZNA" in e[:2] for e in res.edges)
    fp = next(p for p in res.freepoints if p.name == "FP_REPAIR_LEO_SCHMIEDE_ZNA_01")
    assert fp.pos == (12.0, 1.0, -3.5) and fp.owner == "worldgen"
    assert res.report["usePoints"] == 1 and res.report["usesUnconnected"] == []


def test_inside_places_hang_on_their_door_and_stand_on_the_floor():
    places = [
        {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA", "house": "H", "pos": [15.0, -3.0],
         "dir": [0.0, -1.0]},
        {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA_TUER", "house": "H", "pos": [15.0, -6.15],
         "dir": [0.0, -1.0], "y": 0.4, "link": "WP_LEO_SCHMIEDE_ZNA"},
        {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA_INNEN", "house": "H", "pos": [15.0, -7.5],
         "dir": [0.0, -1.0], "y": 0.4, "link": "WP_LEO_SCHMIEDE_ZNA_TUER"},
        {"kind": "fp", "name": "FP_LEAN_LEO_SCHMIEDE_ZNA_INNEN_01", "house": "H",
         "pos": [12.0, -9.0], "dir": [1.0, 0.0], "y": 0.4},
    ]  # fmt: skip
    res = run(places=places)  # the house body covers the inside points: no line checks there
    edges = {(a, b) for a, b, _ in res.edges}
    assert ("WP_LEO_SCHMIEDE_ZNA", "WP_LEO_SCHMIEDE_ZNA_TUER") in edges
    assert ("WP_LEO_SCHMIEDE_ZNA_INNEN", "WP_LEO_SCHMIEDE_ZNA_TUER") in edges
    by = {p.name: p for p in res.points}
    assert by["WP_LEO_SCHMIEDE_ZNA_INNEN"].pos[1] == 0.4
    fp = next(f for f in res.freepoints if f.name.endswith("_INNEN_01"))
    assert fp.pos == (12.0, 0.4, -9.0)


def test_a_link_may_come_before_its_point_and_no_bridge_climbs_a_storey():
    door = {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA", "house": "H", "pos": [15.0, -3.0],
            "dir": [0.0, -1.0]}  # fmt: skip
    inside = {"kind": "wp", "name": "WP_LEO_SCHMIEDE_ZNA_INNEN", "house": "H", "pos": [15.0, -7.5],
              "dir": [0.0, -1.0], "y": 0.4, "link": "WP_LEO_SCHMIEDE_ZNA"}  # fmt: skip
    round_about = {
        "kind": "wp",
        "name": "WP_LEO_SCHMIEDE_ZNA_OBEN_UM",
        "house": "H",
        "pos": [14.0, -7.5],
        "dir": [0.0, 1.0],
        "y": 3.4,
        "link": "WP_LEO_SCHMIEDE_ZNA_OBEN",
    }  # a detour, written before its point
    upstairs = {
        "kind": "wp",
        "name": "WP_LEO_SCHMIEDE_ZNA_OBEN",
        "house": "H",
        "pos": [16.0, -7.5],
        "dir": [0.0, 1.0],
        "y": 3.4,
        "link": "WP_LEO_SCHMIEDE_ZNA_INNEN",
    }
    res = run(places=[door, inside, round_about, upstairs])  # fmt: skip
    edges = {frozenset((a, b)) for a, b, _ in res.edges}
    assert frozenset(("WP_LEO_SCHMIEDE_ZNA_OBEN_UM", "WP_LEO_SCHMIEDE_ZNA_OBEN")) in edges
    assert res.report["usesUnconnected"] == []
    # a point upstairs right above one downstairs, its link lost: not tied through the ceiling
    lost = dict(upstairs, name="WP_LEO_SCHMIEDE_ZNA_OBEN_X", pos=[15.0, -7.5], link="WP_GONE")
    cut = run(places=[door, inside, lost])
    assert not any("WP_LEO_SCHMIEDE_ZNA_OBEN_X" in e[:2] for e in cut.edges)
    assert cut.report["usesUnconnected"][0]["point"] == "WP_LEO_SCHMIEDE_ZNA_OBEN_X"


def test_an_island_reaches_an_edge_between_its_points():
    # two linked points above a fence along the street; only a gap at x = 31..33 lets them down,
    # where no street point is: the bridge splits the street's edge at the foot (W6)
    fence = [Body("STRASSE_WALL_A", box(10, 9.8, 31, 10.2)),
             Body("STRASSE_WALL_B", box(33, 9.8, 60, 10.2))]  # fmt: skip
    places = [
        {"kind": "wp", "name": "WP_LEO_INSEL_A", "house": "H", "pos": [32.0, 20.0],
         "dir": [0.0, 1.0], "link": "WP_LEO_INSEL_B"},
        {"kind": "wp", "name": "WP_LEO_INSEL_B", "house": "H", "pos": [32.5, 24.0],
         "dir": [0.0, 1.0], "link": "WP_LEO_INSEL_A"},
    ]  # fmt: skip
    res = run(bodies=[HOUSE, *fence], places=places)
    assert res.report["components"] == 1
    split = [b for a, b, _ in res.edges if a == "WP_LEO_INSEL_A" and "_S" in b]
    split += [a for a, b, _ in res.edges if b == "WP_LEO_INSEL_A" and "_S" in a]
    assert split and split[0].startswith("WP_LEO_MARKTSTRASSE_")
    foot = next(p for p in res.points if p.name == split[0])
    assert foot.pos[0] == pytest.approx(32.0) and foot.pos[2] == pytest.approx(0.0)
