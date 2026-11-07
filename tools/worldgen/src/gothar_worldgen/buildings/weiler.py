"""Only hamlets outside the town wall (W6, decision of the project owner 2026-11-07).

Outside the wall nearly all LoD2 buildings are left out; what stays is historically plausible and
sparse, chosen by rules (``data/building_rules.json`` "weiler"):

- the houses inside the wall (centroid in the ring plus ``insideM``, or a good share of the
  footprint in it: wall houses);
- the houses of ``uses.json`` before the gates (the "Torvorstadt": guards, merchants with
  routines); of a building cut by the rueckbau only the parts named there;
- mills: by the river (``mill.river``), the nearest to the town first (of a farmstead's size,
  ``minM2``..``maxM2``; ALKIS buildings, ``functions``), ``mill.spacingM`` apart;
- single farms: by the roads out of the gates (streets starting at a gate tower, and the country
  roads ``farm.highways``), the largest first, ``farm.spacingM`` from any other farm or mill.

Mills and farms stand at least ``minWallM`` before the wall and within ``reachM`` of it (the
surroundings, as in vegetation.json). The land they leave becomes meadow, orchard meadow or field
(``vegetation_place.umland``). Selection works on the LoD2 entries before the rueckbau, so a kept
farm may be cut into a house and a barn (``maxParts``, the largest).
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
from typing import Any

import shapely
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import unary_union


@dataclass
class Weiler:
    dropped: set[str] = field(default_factory=set)  # LoD2 ids left out
    gate_parents: set[str] = field(default_factory=set)  # kept only for parts named in uses.json
    use_ids: set[str] = field(default_factory=set)
    mills: list[str] = field(default_factory=list)
    farms: list[str] = field(default_factory=list)
    inside: int = 0
    max_parts: int = 2

    def keeps(self, house_id: str) -> bool:
        """Whether a house (a LoD2 entry or a rueckbau part ``<id>-T<n>``) stays."""
        parent = parent_id(house_id)
        if parent in self.dropped:
            return False
        return parent not in self.gate_parents or house_id in self.use_ids

    def filter(self, entries: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
        """The entries that stay; of a mill or farm cut by the rueckbau only its ``max_parts``
        largest parts (a house and a barn, not a row)."""
        kept = [e for e in entries if self.keeps(str(e.get("id")))]
        hamlets = set(self.mills) | set(self.farms)
        parts: dict[str, list[tuple[float, str]]] = {}
        for e in kept:
            bid = str(e.get("id"))
            if bid != parent_id(bid) and parent_id(bid) in hamlets:
                fp = _footprint(e)
                parts.setdefault(parent_id(bid), []).append((fp.area if fp else 0.0, bid))
        drop = {
            b
            for v in parts.values()
            for _, b in sorted(v, key=lambda t: (-t[0], t[1]))[self.max_parts :]
        }
        return [e for e in kept if str(e.get("id")) not in drop]


def parent_id(house_id: str) -> str:
    """The LoD2 id of a rueckbau part (``<id>-T<n>``/``-H<n>``) or of the entry itself."""
    return house_id.split("-", 1)[0]


def _footprint(entry: dict[str, Any]) -> Polygon | None:
    pts = entry.get("footprint") or []
    if len(pts) < 3:
        return None
    poly = shapely.make_valid(Polygon(pts))
    return poly if not poly.is_empty and poly.area > 0 else None


def _sized(fp: Polygon, spec: dict[str, Any]) -> bool:
    """A farmstead's or mill's size: no hall, school or factory (those the rueckbau would cut
    into a row of houses)."""
    return float(spec["minM2"]) <= fp.area <= float(spec["maxM2"])


def _building(entry: dict[str, Any], prefixes: Sequence[str]) -> bool:
    """A building by its ALKIS function (not a bridge or other structure); without one, yes."""
    fn = entry.get("function")
    return not fn or any(str(fn).startswith(p) for p in prefixes)


def _pick(cands: list[tuple[str, Polygon]], spacing: float, taken: list[Polygon],
          town: Polygon | None = None) -> list[str]:  # fmt: skip
    """The largest first (or, with ``town``, the nearest to it), each ``spacing`` from all taken
    before (deterministic)."""

    def key(t: tuple[str, Polygon]) -> tuple[float, float, str]:
        near = round(town.distance(t[1].centroid), 1) if town is not None else 0.0
        return (near, -round(t[1].area, 3), t[0])

    out = []
    for bid, fp in sorted(cands, key=key):
        c = fp.centroid
        if all(c.distance(t.centroid) >= spacing for t in taken):
            taken.append(fp)
            out.append(bid)
    return out


def select_weiler(
    entries: Sequence[dict[str, Any]],
    ring: Sequence[tuple[float, float]],
    gates: Sequence[tuple[float, float]],
    streets: Sequence[dict[str, Any]],
    features: Sequence[dict[str, Any]],
    use_ids: Iterable[str],
    spec: dict[str, Any],
) -> Weiler:
    """The selection for the LoD2 ``entries``; ``ring`` the town wall, ``gates`` its gate towers."""
    res = Weiler(use_ids=set(use_ids), max_parts=int(spec.get("maxParts", 2)))
    town = Polygon(ring).buffer(0)
    inner = town.buffer(float(spec.get("insideM", 3.0)))
    share = float(spec.get("insideShare", 0.25))
    reach = town.convex_hull.buffer(float(spec["reachM"]))
    near = town.buffer(float(spec["minWallM"]))
    use_parents = {parent_id(u) for u in res.use_ids}
    mill, farm = spec["mill"], spec["farm"]
    river = unary_union([LineString(f["points"]) for f in features
                         if f.get("geometry") == "line" and f.get("name") == mill["river"]
                         and len(f.get("points") or []) >= 2])  # fmt: skip
    gate_pts = [Point(g) for g in gates]
    gate_m = float(farm.get("gateM", 12.0))
    highways = set(farm["highways"])
    roads = []
    for s in streets:
        pts = s.get("points") or []
        if len(pts) < 2:
            continue
        line = LineString(pts)
        if s.get("highway") in highways or any(line.distance(g) < gate_m for g in gate_pts):
            roads.append(line)
    road = unary_union(roads) if roads else None
    outside: list[tuple[str, Polygon]] = []
    free: list[tuple[str, Polygon]] = []  # candidates for mills and farms
    prefixes = tuple(spec.get("functions", ("31001",)))
    for e in entries:
        bid = str(e["id"])
        fp = _footprint(e)
        if fp is None:
            continue
        if inner.contains(fp.centroid) or fp.intersection(town).area >= share * fp.area:
            res.inside += 1
            continue
        if bid in use_parents:
            if bid not in res.use_ids:
                res.gate_parents.add(bid)
            continue
        outside.append((bid, fp))
        if (
            _building(e, prefixes)
            and not near.contains(fp.centroid)
            and reach.contains(fp.centroid)
        ):
            free.append((bid, fp))
    taken: list[Polygon] = []
    if not river.is_empty:
        res.mills = _pick([(b, fp) for b, fp in free if _sized(fp, mill)
                           and river.distance(fp) <= float(mill["maxRiverM"])],
                          float(mill["spacingM"]), taken, town)  # fmt: skip
    if road is not None:
        res.farms = _pick([(b, fp) for b, fp in free if b not in res.mills
                           and _sized(fp, farm)
                           and road.distance(fp) <= float(farm["maxRoadM"])],
                          float(farm["spacingM"]), taken)  # fmt: skip
    kept = set(res.mills) | set(res.farms)
    res.dropped = {b for b, _ in outside if b not in kept}
    return res
