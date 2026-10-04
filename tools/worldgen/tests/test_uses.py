from pathlib import Path

import pytest
from shapely.geometry import Polygon, box

from gothar_worldgen.uses.sheet import draw_map, table_md
from gothar_worldgen.uses.suggest import USES, alkis_use, osm_use, short_id, suggest


def test_osm_and_alkis_hints():
    assert osm_use({"amenity": "pub", "name": "X"}) == ("gasthaus", "amenity=pub")
    assert osm_use({"shop": "bakery", "amenity": "cafe"})[0] == "baecker"  # a baker with a cafe
    assert osm_use({"craft": "blacksmith"})[0] == "schmiede"
    assert osm_use({"craft": "tailor"})[0] == "werkstatt"
    assert osm_use({"shop": "clothes"})[0] == "haendler"
    assert osm_use({"amenity": "bench"}) is None
    assert alkis_use("31001_2081") == "gasthaus" and alkis_use("31001_1010") is None
    assert short_id("DEBW_00100061ZhK") == "ZhK" and short_id("DEBW_00100061Zhk-T1") == "Zhk-T1"


def row(n: int, y: float = 0.0) -> dict[str, Polygon]:
    return {f"H{k:02d}": box(k * 12.0, y, k * 12.0 + 10.0, y + 8.0) for k in range(n)}


def test_limits_reasons_and_closeness():
    houses = row(12)
    pois = [({"amenity": "pub", "name": f"P{k}"}, (k * 12.0 + 5.0, -1.0)) for k in range(4)]
    pois.append(({"shop": "tattoo"}, (5 * 12.0 + 5.0, -1.0)))
    pois.append(({"shop": "jewelry"}, (6 * 12.0 + 5.0, -1.0)))
    picks = suggest(houses, {}, pois, anchors=[(130.0, 4.0)], heart=[(130.0, 4.0)], total=8)
    by_use: dict[str, list] = {}
    for p in picks:
        by_use.setdefault(p.use, []).append(p)
    assert len(by_use["gasthaus"]) == USES["gasthaus"][1] == 2
    assert {p.id for p in by_use["gasthaus"]} == {"H02", "H03"}  # the two nearest the anchor
    assert all("OSM amenity=pub" in p.reasons[0] for p in by_use["gasthaus"])
    shops = sorted(by_use["haendler"], key=lambda p: -p.score)
    assert shops[0].id == "H06"  # real goods before a tattoo studio
    assert len(picks) == 8 and len({p.id for p in picks}) == 8  # one use per house
    assert all(p.residents >= 1 for p in picks)


def test_far_trades_are_left_out_but_farms_stay():
    houses = {"NEAR": box(0, 0, 10, 8), "FAR": box(400, 0, 410, 8), "BARN": box(500, 0, 510, 8)}
    pois = [({"amenity": "pub"}, (405.0, -1.0))]
    picks = suggest(houses, {"BARN": "31001_2721"}, pois, anchors=[(0.0, 0.0)], heart=[(0.0, 0.0)])
    uses = {p.id: p.use for p in picks}
    assert uses.get("FAR") != "gasthaus" and uses["BARN"] == "bauer"


def test_smithy_guards_and_parish_by_location():
    houses = row(14)
    gates = [(-5.0, 4.0), (173.0, 4.0)]
    picks = suggest(houses, {}, [], anchors=[(84.0, 4.0)], gates=gates, heart=[(84.0, 4.0)],
                    church=(84.0, 20.0), total=8)  # fmt: skip
    uses = {p.id: p.use for p in picks}
    assert list(uses.values()).count("schmiede") == 1
    assert sum(u == "wache" for u in uses.values()) == 2
    assert uses.get("H13") == "wache"  # the house at the second gate
    assert list(uses.values()).count("pfarrhaus") == 1
    homes = [p for p in picks if p.use == "wohnhaus"]
    left = [h for h in houses if h not in uses]
    assert homes and left
    far_home = max(abs(p.at[0] - 84.0) for p in homes)
    assert all(abs(houses[h].centroid.x - 84.0) >= far_home for h in left)  # homes near the heart


def test_sheet_outputs(tmp_path: Path):
    houses = row(4)
    picks = suggest(houses, {}, [({"shop": "bakery", "name": "B"}, (5.0, -1.0))],
                    anchors=[(20.0, 4.0)], heart=[(20.0, 4.0)], total=3)  # fmt: skip
    md = table_md(picks)
    assert "| 1 |" in md and "OSM shop=bakery (B)" in md and "Bäcker" in md
    out = tmp_path / "map.png"
    draw_map(out, houses, picks, (20.0, 4.0), 40.0, px=400)
    assert out.stat().st_size > 1000


def test_json_marks_inside_candidates():
    picks = suggest(row(2), {}, [({"amenity": "pub"}, (5.0, -1.0))], anchors=[(0.0, 0.0)],
                    heart=[(0.0, 0.0)], total=2)  # fmt: skip
    j = {p.id: p.json() for p in picks}
    assert j["H00"]["use"] == "gasthaus" and j["H00"]["insideCandidate"] is True
    assert j["H00"]["label"] == "Gasthaus" and j["H00"]["reasons"] == ["OSM amenity=pub"]
    with pytest.raises(KeyError):
        USES["unknown"]
