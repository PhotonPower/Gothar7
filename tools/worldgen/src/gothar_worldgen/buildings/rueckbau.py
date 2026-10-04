"""Replacing large modern buildings by narrow half-timbered houses (leonberg-pipeline.md section 7).

Decision of the owner (2026-10-03): large newer buildings are made smaller and replaced by
half-timbered houses, neither removed nor kept large. Thresholds and sizes are in
``data/building_rules.json`` ("rueckbau"; values set by the koordinator on the owner's behalf).

A selected building is cut into parcels perpendicular to its main street side; parcels deeper than
the maximum get a front house and either a small rear house or a yard (no building). Replacements
are ``buildings.json``-like entries with ids ``<lod2-id>-T<n>`` (front, left to right seen from the
street) and ``<lod2-id>-H<n>`` (rear); the original id is no longer used (its VobId stays reserved).
"""

from __future__ import annotations

import hashlib
import math
import random
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, Point, Polygon
from shapely.geometry.polygon import orient
from shapely.ops import split

PROTECTED_POLYGON_KINDS = {"place_of_worship", "castle", "town_hall", "monument"}
PROTECTED_LINE_KINDS = {"city_wall"}
# ALKIS function prefixes that are never replaced: 51007 = historic structure (e.g. 51007_1510 city
# wall, mapped in LoD2 as long thin bodies a little off the OSM wall line).
PROTECTED_FUNCTION_PREFIXES = ("51007",)
PROTECTED_LINE_MIN_M = 2.0  # the wall must run at least this far through the footprint


@dataclass
class Selection:
    selected: dict[str, list[str]] = field(default_factory=dict)  # id -> reasons
    protected: dict[str, str] = field(
        default_factory=dict
    )  # would be replaced, but stays: id -> why
    guarded: set[str] = field(default_factory=set)  # never split (also not for the triangle budget)


def _rng(building_id: str, salt: str) -> random.Random:
    digest = hashlib.sha256(f"{building_id}:{salt}".encode()).hexdigest()
    return random.Random(int(digest[:16], 16))


def _dims(footprint: Sequence[Sequence[float]]) -> tuple[float, float]:
    poly = Polygon(footprint)
    rect = list(poly.minimum_rotated_rectangle.exterior.coords)
    a, b = math.dist(rect[0], rect[1]), math.dist(rect[1], rect[2])
    return poly.area, max(a, b)


class Protection:
    """OSM areas whose buildings must never be replaced (church, castle, wall, monuments)."""

    def __init__(self, features: Sequence[dict[str, Any]]) -> None:
        geoms = []
        for f in features:
            kind, geom = f.get("kind"), f.get("geometry")
            tags = f.get("tags") or {}
            historic = isinstance(tags, dict) and "historic" in tags
            if geom == "polygon" and (kind in PROTECTED_POLYGON_KINDS or historic):
                geoms.append((Polygon(f["polygon"]), f"{kind} {f.get('name') or ''}".strip()))
            elif geom == "line" and kind in PROTECTED_LINE_KINDS:
                geoms.append((LineString(f["points"]), kind))
            elif geom == "point" and historic:
                geoms.append(
                    (Point(f["position"]).buffer(0.5), f"{kind} {f.get('name') or ''}".strip())
                )
        self.geoms = [(g, why) for g, why in geoms if g.is_valid and not g.is_empty]

    def reason(self, footprint: Sequence[Sequence[float]]) -> str | None:
        poly = Polygon(footprint)
        for geom, why in self.geoms:
            if not poly.intersects(geom):
                continue
            if geom.geom_type == "LineString":
                # Runs through, not just along a wall: measured inside, 0.25 m from the outline.
                if poly.buffer(-0.25).intersection(geom).length >= PROTECTED_LINE_MIN_M:
                    return why
                continue
            cut = poly.intersection(geom)
            if cut.area > 0.5:
                return why
        return None


def select(
    buildings: Sequence[dict[str, Any]],
    rules: dict[str, Any],
    overrides: dict[str, Any],
    protection: Protection,
    over_budget: frozenset[str] = frozenset(),
) -> Selection:
    """Old-town buildings to replace (thresholds, budget, override) and those that stay."""
    sel = Selection()
    th = rules["select"]
    for b in buildings:
        if not b.get("inCore") or len(b.get("footprint") or []) < 3:
            continue
        bid = b["id"]
        o = overrides.get(bid)
        mode = getattr(o, "rueckbau", None) or "auto"
        area, length = _dims(b["footprint"])
        roof = b.get("roof") or {}
        eave = float(roof.get("eaveY", b["groundY"] + b.get("heightM", 0.0))) - float(b["groundY"])
        reasons = []
        if area > th["areaM2"]:
            reasons.append(f"area {area:.0f} m2")
        if length > th["lengthM"]:
            reasons.append(f"length {length:.1f} m")
        if eave > th["eaveM"]:
            reasons.append(f"eave {eave:.1f} m")
        if bid in over_budget:
            reasons.append("triangle budget")
        if mode == "split":
            reasons.append("override rueckbau: split")
        why = None
        function = str(b.get("function") or "")
        pitch = float(roof.get("pitchDeg") or 0.0)
        steep = (
            roof.get("type") in ("saddle", "mixed", "hip")
            and pitch >= th.get("steepRoofDeg", 90)
            and eave <= th.get("steepRoofMaxEaveM", 0)
        )
        if o is not None and o.locked:
            why = "locked"
        elif mode == "none":
            why = "override rueckbau: none"
        elif mode != "split":
            if function.startswith(PROTECTED_FUNCTION_PREFIXES):
                why = f"ALKIS historic structure {function}"
            elif function in th.get("alwaysProtectFunctions", ()) and eave <= th.get(
                "steepRoofMaxEaveM", 0
            ):  # old town hall; a large new one (eave above the limit) is replaced
                why = f"ALKIS {function} (town hall)"
            elif steep:
                why = f"historic steep roof {pitch:.0f} deg, eave {eave:.1f} m"
            else:
                osm = protection.reason(b["footprint"])
                why = f"OSM {osm}" if osm else None
        if why:
            sel.guarded.add(bid)
            if reasons:
                sel.protected[bid] = why
        elif reasons:
            sel.selected[bid] = reasons
    return sel


def _front_edge(ring: Sequence[tuple[float, float]], streets: Any, reach: float) -> int:  # noqa: ANN401
    from gothar_worldgen.buildings.medieval import _outward_normals

    normals = _outward_normals(ring)
    n = len(ring)
    lengths = [math.dist(ring[i], ring[(i + 1) % n]) for i in range(n)]
    street = []
    for i in range(n):
        if streets is not None and streets.faces_street(
            ring[i], ring[(i + 1) % n], normals[i], reach
        ):
            street.append(i)
    candidates = street or list(range(n))
    return max(candidates, key=lambda i: lengths[i])


def _cut(poly: Polygon, line: LineString) -> list[Polygon]:
    parts = []
    for g in split(poly, line).geoms:
        parts.extend(
            p for p in getattr(g, "geoms", [g]) if isinstance(p, Polygon) and p.area > 1e-3
        )
    return parts


def _widths(total: float, target: float, spread: float, rng: random.Random) -> list[float]:
    widths = []
    while sum(widths) < total - target / 2:
        widths.append(rng.uniform(target - spread, target + spread))
    if not widths:
        return [total]
    scale = total / sum(widths)
    return [w * scale for w in widths]


def _entry(new_id: str, src: dict[str, Any], poly: Polygon, storeys: int,
           ridge_along: tuple[float, float], rules: dict[str, Any], rng: random.Random,
           kind: str) -> dict[str, Any]:  # fmt: skip
    st = rules["storeys"]
    ground = float(src["groundY"])
    eave = ground + st["groundM"] + (storeys - 1) * st["upperM"]
    ring = list(orient(poly, sign=-1.0).exterior.coords)[:-1]  # counter-clockwise north-up
    pitch = math.radians(rng.uniform(rules["pitchDeg"] - rules["pitchSpreadDeg"],
                                     rules["pitchDeg"] + rules["pitchSpreadDeg"]))  # fmt: skip
    across = np.array([-ridge_along[1], ridge_along[0]])
    proj = np.asarray(ring) @ across
    half = (proj.max() - proj.min()) / 2
    max_rise = float(rules.get("maxRiseM", 0.0))
    if max_rise > 0 and half > 1e-6 and math.tan(pitch) * half > max_rise:  # no giant roofs
        pitch = max(math.atan2(max_rise, half), math.radians(float(rules.get("minPitchDeg", 35.0))))
    ridge = eave + math.tan(pitch) * half
    return {
        "id": new_id,
        "function": src.get("function"),
        "inCore": True,
        "footprint": [[round(x, 2) + 0.0, round(z, 2) + 0.0] for x, z in ring],
        "areaM2": round(poly.area, 1),
        "groundY": ground,
        "heightM": round(ridge - ground, 2),
        "roof": {
            "type": "saddle",
            "eaveY": round(eave, 2),
            "ridgeY": round(ridge, 2),
            "ridgeDir": [round(float(ridge_along[0]), 4), round(float(ridge_along[1]), 4)],
            "pitchDeg": round(math.degrees(pitch), 1),
        },  # fmt: skip
        "derivedFrom": src["id"],
        "rueckbau": kind,
    }


def _rear_strips(rear: Any, a0: np.ndarray, along: np.ndarray, inward: np.ndarray,  # noqa: ANN401
                 reach: float, depth: float) -> list[Polygon]:  # fmt: skip
    """The rear rest cut parallel to the street into strips of at most ``depth``."""
    polys = [g for g in getattr(rear, "geoms", [rear]) if isinstance(g, Polygon) and g.area > 0]
    out: list[Polygon] = []
    for poly in polys:
        t = (np.asarray(poly.exterior.coords) - a0) @ inward
        t0, t1 = float(t.min()), float(t.max())
        n = max(1, math.ceil((t1 - t0) / depth - 1e-6))
        pieces = [poly]
        for j in range(1, n):
            c = a0 + inward * (t0 + (t1 - t0) * j / n)
            line = LineString([tuple(c - along * reach), tuple(c + along * reach)])
            pieces = [q for piece in pieces for q in _cut(piece, line)]
        out += [q for q in pieces if isinstance(q, Polygon) and not q.is_empty]
    return out


@dataclass
class SplitResult:
    houses: list[dict[str, Any]]
    yard_m2: float


def split_building(b: dict[str, Any], rules: dict[str, Any], streets: Any = None) -> SplitResult:  # noqa: ANN401
    """Parcels along the main street side, depth limit, rear house or yard, capped height."""
    ring = [tuple(map(float, p)) for p in b["footprint"]]
    poly = Polygon(ring)
    if not poly.is_valid:
        fixed = shapely.make_valid(poly)
        polys = [g for g in getattr(fixed, "geoms", [fixed]) if isinstance(g, Polygon)]
        poly = max(polys, key=lambda g: g.area)
        ring = list(poly.exterior.coords)[:-1]
    p = rules["parcels"]
    rng = _rng(b["id"], "rueckbau")
    edge = _front_edge(ring, streets, 12.0)
    a0, a1 = np.asarray(ring[edge]), np.asarray(ring[(edge + 1) % len(ring)])
    along = (a1 - a0) / (np.linalg.norm(a1 - a0) or 1.0)
    inward = np.array([-along[1], along[0]])
    if Polygon(ring).contains(Point(*((a0 + a1) / 2 + inward * 0.05))) is False:
        inward = -inward
    pts = np.asarray(ring)
    s = pts @ along
    t = (pts - a0) @ inward
    reach = float(np.ptp(t) + np.ptp(s) + 10)

    # Parcels: cuts perpendicular to the street.
    pieces = [poly]
    pos = float(s.min())
    for w in _widths(float(np.ptp(s)), p["widthM"], p["widthSpreadM"], rng)[:-1]:
        pos += w
        c = a0 + along * (pos - float(a0 @ along))
        line = LineString([tuple(c - inward * reach), tuple(c + inward * reach)])
        pieces = [q for piece in pieces for q in _cut(piece, line)]
    pieces.sort(key=lambda q: float(np.asarray(q.centroid.coords[0]) @ along))
    merged: list[Polygon] = []
    for q in pieces:  # small slivers join their neighbour
        if merged and (q.area < p["minAreaM2"] or merged[-1].area < p["minAreaM2"]):
            u = merged[-1].union(q)
            merged[-1] = u if isinstance(u, Polygon) else max(u.geoms, key=lambda g: g.area)
        else:
            merged.append(q)

    # Front/rear: cut parallel to the street at the maximum depth.
    houses, yard = [], 0.0
    front_dir = (float(along[0]), float(along[1]))
    storeys_max = int(rules["storeysMax"])
    for i, q in enumerate(merged, start=1):
        qt = (np.asarray(q.exterior.coords) - a0) @ inward
        depth = float(qt.max() - max(qt.min(), 0.0))
        front, rear = q, None
        if depth > p["maxDepthM"]:
            c = a0 + inward * (max(float(qt.min()), 0.0) + p["maxDepthM"])
            line = LineString([tuple(c - along * reach), tuple(c + along * reach)])
            cut_t = float((c - a0) @ inward)
            parts = _cut(q, line)
            near = [
                g for g in parts if float((np.asarray(g.centroid.coords[0]) - a0) @ inward) < cut_t
            ]
            far = [g for g in parts if g not in near]
            if near and far:
                front = max(near, key=lambda g: g.area)
                rear = shapely.unary_union(far)
        gable = rng.random() < rules["ridge"]["gableToStreet"]
        ridge = (float(inward[0]), float(inward[1])) if gable else front_dir
        storeys = rng.randint(max(2, storeys_max - 1), storeys_max)
        houses.append(_entry(f"{b['id']}-T{i}", b, front, storeys, ridge, rules, rng, "front"))
        if rear is not None and not rear.is_empty:
            # the rest behind the front house: rear houses in strips of at most maxDepthM, each
            # with its ridge parallel to the street; strips too small become yard
            k = 0
            for rq in _rear_strips(rear, a0, along, inward, reach, float(p["maxDepthM"])):
                rt = (np.asarray(rq.exterior.coords) - a0) @ inward
                if float(np.ptp(rt)) >= p["rearHouseMinDepthM"] and rq.area >= p["minAreaM2"]:
                    k += 1
                    rs = rng.randint(1, int(p["rearStoreysMax"]))
                    rid = f"{b['id']}-H{i}" + (f"{chr(ord('a') + k - 1)}" if k > 1 else "")
                    houses.append(_entry(rid, b, rq, rs, front_dir, rules, rng, "rear"))
                else:
                    yard += rq.area
    return SplitResult(houses, yard)


def apply(
    buildings: Sequence[dict[str, Any]],
    selection: Selection,
    rules: dict[str, Any],
    streets: Any = None,  # noqa: ANN401
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Buildings with the selected ones replaced; plus the report."""
    out, report = [], []
    for b in buildings:
        reasons = selection.selected.get(b["id"])
        if not reasons:
            out.append(b)
            continue
        res = split_building(b, rules, streets)
        out.extend(res.houses)
        report.append({"id": b["id"], "reasons": reasons,
                       "front": sum(1 for h in res.houses if h["rueckbau"] == "front"),
                       "rear": sum(1 for h in res.houses if h["rueckbau"] == "rear"),
                       "yardM2": round(res.yard_m2, 1)})  # fmt: skip
    doc = {
        "selected": report,
        "protected": [{"id": k, "reason": v} for k, v in sorted(selection.protected.items())],
        "stats": {
            "replaced": len(report),
            "newHouses": sum(r["front"] + r["rear"] for r in report),
            "protected": len(selection.protected),
            "yardM2": round(sum(r["yardM2"] for r in report), 1),
        },
    }
    return out, doc
