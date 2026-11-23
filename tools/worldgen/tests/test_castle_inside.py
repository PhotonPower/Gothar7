"""The castle's inside (W7 D1b): room models, furniture, light, waypoints, the giant cask."""

import json
import math
from pathlib import Path

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.handmade import load, schloss_geometry, schloss_room_items, with_room_items
from gothar_worldgen.mobs import PROPS
from gothar_worldgen.uses.castle import FP_NEAR_M, assemble_vobs, load_castle, plan_castle

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
DIR = REPO / "assets" / "source" / "worlds" / "leonberg" / "handmade" / "schloss"
BUILT = json.loads((DIR / "schloss_built.json").read_text(encoding="utf-8"))
DATA = load_castle(ROOT / "data" / "leonberg" / "castle_inside.json")
geo = schloss_geometry()


def plan():
    return plan_castle(BUILT, DATA, geo)


def test_rooms_are_models_of_their_own_within_budget():
    m = geo.build(BUILT)
    rooms = m.room_triangles()
    assert len(rooms) == 14 and "turm" in rooms and "keller_weinkeller" in rooms
    assert m.triangles() <= 15000 and sum(rooms.values()) <= 25000
    for room in rooms:
        gl, _ = read_glb((DIR / f"schloss_{room}.glb").read_bytes())
        names = [n["name"] for n in gl["nodes"]]
        assert names == [f"schloss_{room}", "COL_HULL_NONE"]  # no collision of its own


def test_room_items_beside_the_castle():
    items = schloss_room_items(BUILT, "worlds/leonberg/handmade/schloss/schloss.glb")
    castle = next(i for i in load(ROOT / "data" / "leonberg" / "handmade.json")["items"]
                  if i["key"] == "schloss")  # fmt: skip
    assert all(i["pos"] == castle["pos"] for i in items)
    wood = {i["key"] for i in items if i.get("components")}
    assert wood == {i["key"] for i in items if i["key"].startswith("schloss_og_")}
    doc = load(ROOT / "data" / "leonberg" / "handmade.json")
    assert with_room_items(doc, items) == doc  # versioned as built
    stale = with_room_items({"items": [{"key": "schloss_x"}, {"key": "schloss"}]}, [])
    assert [i["key"] for i in stale["items"]] == ["schloss"]


def test_data_checks_clean_and_the_plan_is_deterministic():
    p = plan()
    assert p.failed == []
    assert p.vobs == plan().vobs and p.places == plan().places
    names = [v["name"] for v in p.vobs] + [q["name"] for q in p.places]
    assert len(names) == len(set(names))
    assert all("room" not in v for v in assemble_vobs(p))


def test_waypoints_form_one_tree_from_the_court():
    p = plan()
    wps = {q["name"]: q for q in p.places if q["kind"] == "wp"}
    roots = [n for n, q in wps.items() if "link" not in q]
    assert roots == ["WP_LEO_SCHLOSS_PORTAL_AUSSEN"]
    for name in wps:  # every point leads to the court
        seen = set()
        while "link" in wps[name]:
            assert name not in seen
            seen.add(name)
            name = wps[name]["link"]
            assert name in wps
    for room in ("KUECHE", "HALLE", "FESTSAAL", "GEMACH", "WEINKELLER", "VERLIES"):
        assert wps[f"WP_LEO_SCHLOSS_{room}"].get("routine")
    links = [(wps[n], wps[q["link"]]) for n, q in wps.items() if "link" in q]
    for a, b in links:  # short steps; on the spiral at most a storey's share of height
        d = math.dist(a["pos"], b["pos"])
        assert d < 20.0 and abs(a["y"] - b["y"]) <= 0.45 + 1e-6


def test_freepoints_and_new_types_have_a_waypoint_near():
    p = plan()
    wps = [q for q in p.places if q["kind"] == "wp"]
    fps = [q for q in p.places if q["kind"] == "fp"]
    kinds = {q["name"].split("_")[1] for q in fps}
    assert {"COOK", "READ", "STAND", "PRAY", "SMALLTALK"} <= kinds
    for f in fps:
        near = min(math.dist(f["pos"], w["pos"]) for w in wps if abs(w["y"] - f["y"]) < 0.5)
        assert near <= FP_NEAR_M + 1e-6, f["name"]


def test_giant_cask_lever_and_secret_wall_follow_the_contract():
    p = plan()
    by = {v["name"]: v for v in p.vobs}
    lever, wall = by["LEVER_SCHLOSS_FASS"], by["WALL_SCHLOSS_FASS"]
    assert lever["type"] == "mob" and lever["mesh"] == "props/torch_lever.glb"
    mob = lever["components"]["mob"]
    assert list(mob) == ["definition", "targets", "action", "hidden"]  # world.md order
    assert mob == {"definition": "lever", "targets": ["WALL_SCHLOSS_FASS"], "action": "toggle",
                   "hidden": True}  # fmt: skip
    mover = wall["components"]["mover"]
    assert wall["type"] == "mesh" and mover["kind"] == "secret_wall" and mover["open"] is False
    assert list(mover) == ["kind", "open", "seconds", "offset", "axis", "angle", "crush"]
    assert math.dist(lever["pos"], wall["pos"]) < 4.0
    cask = next(v for v in p.vobs if v.get("mesh") == "props/giant_barrel.glb")
    assert math.dist(cask["pos"], wall["pos"]) < 2.0


def test_lights_per_room_and_zones():
    p = plan()
    lights = [v for v in p.vobs if v["type"] == "light"]
    per_room: dict[str, int] = {}
    for v in lights:
        room = v["name"].split("_")[3]
        per_room[room] = per_room.get(room, 0) + 1
    assert max(per_room.values()) <= 6 and per_room["TURM"] == 3
    values = {z["value"] for z in p.zones}
    assert "LEO_SCHLOSS_TURM_INNEN" in values and "LEO_SCHLOSS_WEINKELLER_INNEN" in values
    assert all(z["type"] == "indoor" for z in p.zones)


def test_castle_props_are_built_within_budget():
    for name in ("giant_barrel", "giant_barrel_front", "wall_torch", "torch_lever", "bookshelf",
                 "lectern", "altar", "cell_bars"):  # fmt: skip
        model = PROPS[name]()
        assert model.triangles() <= 1500
        assert (REPO / "assets" / "source" / "props" / f"{name}.glb").read_bytes() == model.glb()
