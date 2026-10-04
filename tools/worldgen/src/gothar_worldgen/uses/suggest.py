"""Suggested building uses for gameplay (W7 "Gebäudenutzungen", plan approved 2026-10-04).

Today's shops, pubs and crafts of the old town (OpenStreetMap) and the ALKIS building function
are mapped to medieval uses (tavern, baker, merchant, smithy ...); the houses are ranked by how
sure the source is and how close they stand to the market, the church and the gates. The result
is a suggestion for the koordinator to choose from (``uses_suggest.json`` and a labelled map); the
choice becomes ``data/<site>/uses.json`` (PR B). Names shown in the game are our own, never the
real business names; the real names appear only in the (unversioned) suggestion as the reason.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any

from shapely.geometry import Point, Polygon
from shapely.strtree import STRtree

# use -> (German label, how many at most, candidate for an enterable house)
USES: dict[str, tuple[str, int, bool]] = {
    "gasthaus": ("Gasthaus", 2, True),
    "baecker": ("Bäcker", 1, False),
    "metzger": ("Metzger", 1, False),
    "haendler": ("Händler", 5, True),
    "schmiede": ("Schmiede", 1, True),
    "werkstatt": ("Werkstatt", 3, False),
    "bader": ("Bader", 1, False),
    "kraeuter": ("Kräuterhändler", 1, False),
    "amtshaus": ("Amtshaus", 1, False),
    "wache": ("Wache", 2, False),
    "pfarrhaus": ("Pfarrhaus", 1, False),
    "bauer": ("Bauernhof", 2, False),
    "wohnhaus": ("Wohnhaus", 10, True),
}
TOTAL = 30  # houses in the suggestion

# OSM tag -> use, with the strength of the hint (3 = OSM says so)
_OSM: list[tuple[str, str | None, str]] = [
    ("craft", "blacksmith", "schmiede"),
    ("craft", "metal_construction", "schmiede"),
    ("shop", "bakery", "baecker"),  # before the cafe: a bakery with a cafe is a baker
    ("shop", "pastry", "baecker"),
    ("shop", "butcher", "metzger"),
    ("amenity", "restaurant", "gasthaus"),
    ("amenity", "pub", "gasthaus"),
    ("amenity", "bar", "gasthaus"),
    ("amenity", "biergarten", "gasthaus"),
    ("amenity", "cafe", "gasthaus"),
    ("tourism", "hotel", "gasthaus"),
    ("tourism", "guest_house", "gasthaus"),
    ("shop", "hairdresser", "bader"),
    ("shop", "beauty", "bader"),
    ("amenity", "pharmacy", "kraeuter"),
    ("amenity", "townhall", "amtshaus"),
    ("office", "government", "amtshaus"),
    ("amenity", "police", "wache"),
    ("office", "religion", "pfarrhaus"),
    ("craft", None, "werkstatt"),
    ("shop", None, "haendler"),
]
# ALKIS building function (code after the underscore) -> use, strength 2
_ALKIS: dict[str, str] = {
    "2081": "gasthaus",  # Gaststätte, Restaurant
    "2071": "gasthaus",  # Hotel, Motel, Pension
    "2120": "werkstatt",  # Werkstatt
    "2050": "haendler",  # Geschäftsgebäude
    "1123": "haendler",  # Wohngebäude mit Handel und Dienstleistungen
    "2721": "bauer",  # Scheune
    "2724": "bauer",  # Stall
    "3012": "amtshaus",  # Rathaus
}
# poor medieval hints (services, cafes): weaker than real goods, pubs and restaurants
_WEAK_SHOPS = {"tattoo", "massage", "hearing_aids", "mobile_phone", "optician", "travel_agency",
               "copyshop", "lottery", "e-cigarette", "nutrition_supplements", "beauty",
               "funeral_directors", "mall", "department_store", "car", "car_repair",
               "cafe", "fast_food", "ice_cream"}  # fmt: skip
NEAR_M = 4.0  # an OSM point this close to a footprint belongs to the house
RANK_REACH_M = 250.0  # the closeness bonus fades out over this distance
HEART_REACH_M = 250.0  # trades and shops only this near the market or the church (not farms)


@dataclass
class Suggestion:
    id: str
    short: str
    use: str
    score: float
    reasons: list[str] = field(default_factory=list)
    at: tuple[float, float] = (0.0, 0.0)
    residents: int = 0

    def json(self) -> dict[str, Any]:
        out: dict[str, Any] = {"id": self.id, "short": self.short, "use": self.use,
                               "label": USES[self.use][0], "score": round(self.score, 2),
                               "at": [round(self.at[0], 1), round(self.at[1], 1)],
                               "reasons": self.reasons}  # fmt: skip
        if self.residents:
            out["residents"] = self.residents
        if USES[self.use][2]:
            out["insideCandidate"] = True
        return out


def short_id(bid: str) -> str:
    """Readable short form with case (``ZhK`` and ``Zhk`` are different houses): the part
    after the common LoD2 prefix."""
    tail = bid.split("_")[-1]
    return tail[8:] if len(tail) > 8 and tail.startswith("001000") else tail


def osm_use(tags: dict[str, str]) -> tuple[str, str] | None:
    """The medieval use an OSM object hints at and the tag that says so, or None."""
    for key, value, use in _OSM:
        if key in tags and (value is None or tags[key] == value):
            return use, f"{key}={tags[key]}"
    return None


def alkis_use(function: str | None) -> str | None:
    code = str(function or "").split("_")[-1]
    return _ALKIS.get(code)


def _closeness(at: tuple[float, float], anchors: Sequence[tuple[float, float]]) -> float:
    """1 next to the market, the church or a gate, 0 from ``RANK_REACH_M`` on."""
    if not anchors:
        return 0.0
    d = min(math.dist(at, a) for a in anchors)
    return max(0.0, 1.0 - d / RANK_REACH_M)


def suggest(
    houses: dict[str, Polygon],
    functions: dict[str, str | None],
    pois: Sequence[tuple[dict[str, str], tuple[float, float]]],
    anchors: Sequence[tuple[float, float]],
    gates: Sequence[tuple[float, float]] = (),
    heart: Sequence[tuple[float, float]] = (),
    church: tuple[float, float] | None = None,
    storeys: dict[str, int] | None = None,
    short: Any = None,  # noqa: ANN401  callable id -> short name
    total: int = TOTAL,
) -> list[Suggestion]:
    """Ranked suggestions: OSM hints (strength 3), ALKIS functions (2), closeness to the anchors
    (up to 1); at most ``USES[use][1]`` houses per use. A smithy and the guards by the gates
    (OSM police elsewhere is today's station, not a medieval guard); the rest homes near
    ``heart`` (market, church)."""
    ids = sorted(houses)
    tree = STRtree([houses[i] for i in ids])
    hints: dict[str, dict[str, list[str]]] = {i: {} for i in ids}
    for tags, (x, z) in pois:
        hit = osm_use(tags)
        if hit is None:
            continue
        use, what = hit
        if use == "wache":
            continue  # guards go to the gates (below)
        p = Point(x, z)
        near = [ids[int(k)] for k in tree.query(p.buffer(NEAR_M))
                if houses[ids[int(k)]].distance(p) <= NEAR_M]  # fmt: skip
        if not near:
            continue
        hid = min(near, key=lambda i: houses[i].distance(p))
        name = tags.get("name")
        hints[hid].setdefault(use, []).append(f"OSM {what}" + (f" ({name})" if name else ""))
    for hid in ids:
        use = alkis_use(functions.get(hid))
        if use is not None:
            hints[hid].setdefault(use, []).append(f"ALKIS {functions[hid]}")

    def centre(hid: str) -> tuple[float, float]:
        c = houses[hid].centroid
        return (c.x, c.y)

    candidates: list[Suggestion] = []
    heart_pts = list(heart) or list(anchors)
    for hid, by_use in hints.items():
        far = bool(heart_pts) and min(math.dist(centre(hid), a) for a in heart_pts) > HEART_REACH_M
        for use, reasons in by_use.items():
            if far and use != "bauer":
                continue

            def weight(r: str) -> float:
                if not r.startswith("OSM"):
                    return 2.0
                weak = r.split()[1].split("=")[-1] in _WEAK_SHOPS
                return 2.5 if weak else 3.0

            strength = max(weight(r) for r in reasons)
            score = strength + 0.2 * (len(reasons) - 1) + _closeness(centre(hid), anchors)
            candidates.append(Suggestion(hid, short(hid) if short else hid, use, score,
                                         sorted(reasons), centre(hid)))  # fmt: skip
    candidates.sort(key=lambda s: (-s.score, s.id, s.use))
    taken: dict[str, Suggestion] = {}
    count: dict[str, int] = {}
    for s in candidates:
        if s.id in taken or count.get(s.use, 0) >= USES[s.use][1] or s.use == "wohnhaus":
            continue
        taken[s.id] = s
        count[s.use] = count.get(s.use, 0) + 1
    if not count.get("schmiede") and gates:  # a town needs its smith: by a gate, with room
        free = [h for h in ids if h not in taken]
        best = min(
            free, key=lambda h: min(math.dist(centre(h), g) for g in gates) - 0.02 * houses[h].area
        )
        why = ["Lage: am Tor (kein Hinweis in OSM/ALKIS)"]
        taken[best] = Suggestion(best, short(best) if short else best, "schmiede", 1.0, why,
                                 centre(best))  # fmt: skip
    if not count.get("pfarrhaus") and church is not None:  # the parish house by the church
        free = [h for h in ids if h not in taken and houses[h].distance(Point(church)) > 1.0]
        if free:
            best = min(free, key=lambda h: houses[h].distance(Point(church)))
            taken[best] = Suggestion(best, short(best) if short else best, "pfarrhaus", 1.0,
                                     ["Lage: an der Kirche"], centre(best))  # fmt: skip
            count["pfarrhaus"] = 1
    for g in gates:  # a guard house at each gate (the nearest free house), up to the limit
        if count.get("wache", 0) >= USES["wache"][1]:
            break
        free = [h for h in ids if h not in taken]
        if not free:
            break
        best = min(free, key=lambda h: houses[h].distance(Point(g)))
        taken[best] = Suggestion(best, short(best) if short else best, "wache", 1.0,
                                 ["Lage: am Tor"], centre(best))  # fmt: skip
        count["wache"] = count.get("wache", 0) + 1
    home_anchors = heart_pts
    homes = sorted(
        (h for h in ids if h not in taken), key=lambda h: (-_closeness(centre(h), home_anchors), h)
    )
    for h in homes[: max(0, min(USES["wohnhaus"][1], total - len(taken)))]:
        near = _closeness(centre(h), home_anchors)
        taken[h] = Suggestion(h, short(h) if short else h, "wohnhaus", near,
                              ["Lage: nahe Markt/Kirche"], centre(h))  # fmt: skip
    for s in taken.values():  # residents: about one per 60 m² of floor over all storeys
        floors = (storeys or {}).get(s.id, 2)
        s.residents = max(1, min(8, round(houses[s.id].area * floors / 60.0)))
    return sorted(taken.values(), key=lambda s: (list(USES).index(s.use), -s.score, s.id))
