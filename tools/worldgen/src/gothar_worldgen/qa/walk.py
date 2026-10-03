"""Routes for the autopilot (``gothar --walk``, tools.md "Autopilot") and their evaluation (W3).

Part 2 of the walkthrough (part 1: :mod:`gothar_worldgen.qa.begehung`). Three routes:

- ``stations``: each walkthrough station (``data/<site>/starts.json``) from its start through its
  target and a little beyond, with a screenshot at the target.
- ``ways``: every walkable OSM way of the core, run by run outside the collision bodies; each run
  starts with a teleport, then points every ``stepM`` metres.
- ``gates``: through every gate and pforte of the city wall and back.

``evaluate`` reads ``walk.jsonl`` and ``walk_summary.json`` of a run and matches every stuck, fall
and slide against the static report: confirmed findings, and new ones the static walk missed.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import shapely
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

from gothar_worldgen.qa.begehung import WALKABLE, Body

ROUTE_VERSION = 1
FORMAT = "gothar-walk-report"
STEP_M = 8.0  # point spacing along a way
MIN_RUN_M = 5.0  # shorter free runs of a way are left out
RUN_SPEED = 4.0  # m/s, generous estimate for the time limit
TELEPORT_S = 3.0
MATCH_M = 3.0  # an event this close to a static finding confirms it
SLIDE_S = 0.5  # shorter slides are grazes along a wall, not a slope


def _r(v: float) -> float:
    return round(float(v), 2) + 0.0


def _route(points: list[dict[str, Any]], metres: float) -> dict[str, Any]:
    teleports = sum(1 for p in points if p.get("teleport"))
    limit = int(math.ceil((metres / RUN_SPEED + teleports * TELEPORT_S + 60) * 1.5))
    return {
        "version": ROUTE_VERSION,
        "gait": "run",
        "time": "12:00",
        "timeLimit": limit,
        "points": points,
    }


def station_route(stations: list[dict[str, Any]], beyond: float = 6.0) -> dict[str, Any]:
    """Teleport to each station, run to its target and ``beyond`` metres further (screenshot)."""
    points, metres = [], 0.0
    for s in stations:
        if "target" not in s:
            continue
        x, z = float(s["x"]), float(s["z"])
        tx, tz = map(float, s["target"])
        d = math.hypot(tx - x, tz - z) or 1.0
        key = s["name"].removeprefix("START_BG_")
        points.append({"name": f"{key}_START", "pos": [_r(x), _r(z)], "teleport": True})
        points.append(
            {"name": f"{key}_ZIEL", "pos": [_r(tx), _r(tz)], "radius": 1.0, "screenshot": True}
        )
        points.append(
            {
                "name": f"{key}_DAHINTER",
                "pos": [_r(tx + (tx - x) / d * beyond), _r(tz + (tz - z) / d * beyond)],
            }
        )
        metres += d + beyond
    return _route(points, metres)


def way_runs(
    streets: list[dict[str, Any]], bodies: list[Body], area: Polygon
) -> list[tuple[dict[str, Any], LineString]]:
    """Free stretches (outside every body, grown by the character radius) of the walkable ways."""
    solid = unary_union([b.poly for b in bodies]).buffer(0.35)
    runs = []
    for w in streets:
        if w["highway"] not in WALKABLE or w.get("tunnel") or w.get("bridge") or w.get("layer"):
            continue
        line = LineString(w["points"])
        if line.length < MIN_RUN_M:
            continue
        free = line.intersection(area).difference(solid)
        for part in getattr(free, "geoms", [free]):
            if isinstance(part, LineString) and part.length >= MIN_RUN_M:
                runs.append((w, part))
    runs.sort(key=lambda r: (r[0]["osmId"], r[1].coords[0]))
    return runs


def ways_route(
    runs: list[tuple[dict[str, Any], LineString]], step: float = STEP_M
) -> dict[str, Any]:
    points, metres = [], 0.0
    for k, (w, run) in enumerate(runs):
        n = max(1, int(math.ceil(run.length / step)))
        for j in range(n + 1):
            p = run.interpolate(run.length * j / n)
            name = f"W{k:04d}_{w['osmId']}_{j:02d}"
            pt: dict[str, Any] = {"name": name, "pos": [_r(p.x), _r(p.y)]}
            if j == 0:
                pt["teleport"] = True
            else:
                pt["radius"] = 1.0
            points.append(pt)
        metres += run.length
    return _route(points, metres)


def _off_bodies(p: Point, solid: Any, clearance: float = 0.6) -> Point:  # noqa: ANN401
    """``p``, or the nearest point outside the bodies (grown by ``clearance``)."""
    grown = solid.buffer(clearance)
    if not grown.contains(p):
        return p
    edge = grown.boundary
    q = edge.interpolate(edge.project(p))
    return Point(q.x, q.y)


def _street_sides(
    street: str, on: Point, streets: list[dict[str, Any]], reach: float
) -> list[Point] | None:
    """Two points ``reach`` metres either way from ``on`` along the named street through it."""
    best = None
    for w in streets:
        if street and w.get("name") == street:
            line = LineString(w["points"])
            if best is None or line.distance(on) < best.distance(on):
                best = line
    if best is None or best.distance(on) > 6.0:
        return None
    d = best.project(on)
    return [best.interpolate(max(0.0, d - reach)), best.interpolate(min(best.length, d + reach))]


def gates_route(
    course: list[list[float]],
    gates: list[dict[str, Any]],
    bodies: list[Body],
    streets: list[dict[str, Any]] = (),  # type: ignore[assignment]
    reach: float = 8.0,
) -> dict[str, Any]:
    """Through each gate/pforte: teleport inside the ring, run out, run back in.

    Inside and outside lie across the ring (inside = nearer the origin, the market fountain), so the
    character runs straight through the passage; if a house blocks that line in, the inside point
    moves onto the gate's street (``city_wall.json``).
    """
    ring = LineString(course + [course[0]])
    solid = unary_union([b.poly for b in bodies])
    points, metres = [], 0.0
    for g in gates:
        at = Point(g["at"])
        d = ring.project(at)
        a, b = ring.interpolate(max(0.0, d - 1.0)), ring.interpolate(min(ring.length, d + 1.0))
        tx, tz = b.x - a.x, b.y - a.y
        n = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / n, tx / n
        on = ring.interpolate(d)
        across = sorted(
            (Point(on.x + nx * reach * s, on.y + nz * reach * s) for s in (1.0, -1.0)),
            key=lambda p: p.distance(Point(0.0, 0.0)),
        )
        inside, outside = (_off_bodies(p, solid) for p in across)
        # A house on the straight line in: come along the gate's street instead.
        if LineString([inside, on]).intersects(solid.buffer(0.3).difference(on.buffer(2.0))):
            street = _street_sides(g.get("street", ""), on, list(streets), reach)
            if street:
                inside = _off_bodies(min(street, key=lambda p: p.distance(Point(0.0, 0.0))), solid)
        key = g["key"].upper()
        points += [
            {"name": f"{key}_INNEN", "pos": [_r(inside.x), _r(inside.y)], "teleport": True},
            {
                "name": f"{key}_DURCH",
                "pos": [_r(on.x), _r(on.y)],
                "radius": 1.0,
                "screenshot": True,
            },
            {"name": f"{key}_AUSSEN", "pos": [_r(outside.x), _r(outside.y)], "radius": 1.0},
            {"name": f"{key}_ZURUECK", "pos": [_r(inside.x), _r(inside.y)], "radius": 1.0},
        ]
        metres += 4 * reach
    return _route(points, metres)


def read_log(out_dir: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    events = [
        json.loads(line)
        for line in (out_dir / "walk.jsonl").read_text("utf-8").splitlines()
        if line.strip()
    ]
    summary_path = out_dir / "walk_summary.json"
    summary = json.loads(summary_path.read_text("utf-8")) if summary_path.is_file() else {}
    return events, summary


def _static_points(report: dict[str, Any]) -> list[tuple[str, float, float, dict[str, Any]]]:
    pts = []
    for f in report.get("lanes", {}).get("list", []):
        pts.append((f"lane:{f['kind']}", f["at"][0], f["at"][1], f))
    for f in report.get("slopes", {}).get("list", []):
        pts.append((f"slope:{f['kind']}", f["at"][0], f["at"][1], f))
    for f in report.get("slopes", {}).get("steps", {}).get("list", []):
        pts.append(("steps", f["at"][0], f["at"][1], f))
    return pts


def evaluate(
    route: dict[str, Any],
    events: list[dict[str, Any]],
    summary: dict[str, Any],
    report: dict[str, Any] | None,
) -> dict[str, Any]:
    """Problems of one run, each matched against the static report where possible."""
    pos_of = {p["name"]: p["pos"] for p in route["points"]}
    static = _static_points(report or {})
    tree = shapely.STRtree([Point(x, z) for _, x, z, _ in static]) if static else None
    problems = []
    slide_from: dict[str, Any] | None = None
    for e in events:
        kind = e.get("event")
        if kind == "slide_start":
            slide_from = e
            continue
        if kind == "slide_end" and slide_from is not None:
            took = float(e["t"]) - float(slide_from["t"])
            if took < SLIDE_S:
                slide_from = None
                continue
            e = {**slide_from, "event": "slide", "seconds": round(took, 2)}
            kind, slide_from = "slide", None
        if kind not in ("stuck", "fall", "slide", "teleport_failed"):
            continue
        if kind == "fall" and e.get("water"):
            continue
        where = e.get("pos") or pos_of.get(e.get("name", ""), [0.0, 0.0, 0.0])
        x, z = (where[0], where[2]) if len(where) == 3 else (where[0], where[1])
        item = {"event": kind, "t": e.get("t"), "at": [_r(x), _r(z)]}
        for key in ("name", "vob", "state", "height", "damage", "seconds"):
            if key in e:
                item[key] = e[key]
        if kind == "stuck" and e.get("name") in pos_of:  # the point it did not reach
            item["target"] = pos_of[e["name"]]
        osm = str(e.get("name", "")).split("_")[1] if str(e.get("name", "")).startswith("W") else ""
        same_way = [label for label, _, _, f in static if osm and f.get("osmId") == osm]
        if same_way:  # the static walk flagged this way (anywhere along it)
            item["static"] = same_way[0]
        elif tree is not None:
            k = int(tree.nearest(Point(x, z)))
            label, sx, sz, f = static[k]
            if math.hypot(sx - x, sz - z) <= MATCH_M:
                item["static"] = label
        problems.append(item)
    count: dict[str, int] = {}
    for p in problems:
        count[p["event"]] = count.get(p["event"], 0) + 1
    vobs: dict[str, int] = {}
    for p in problems:
        if p["event"] == "stuck":
            v = str(p.get("vob", "unknown")).split("_")[0]
            vobs[v] = vobs.get(v, 0) + 1
    return {
        "format": FORMAT,
        "points": len(route["points"]),
        "summary": summary,
        "count": count,
        "stuckBy": vobs,
        "confirmed": sum(1 for p in problems if "static" in p),
        "new": sum(1 for p in problems if "static" not in p),
        "problems": problems,
    }


def write_routes(
    target: Path,
    stations: list[dict[str, Any]],
    streets: list[dict[str, Any]],
    bodies: list[Body],
    course: list[list[float]],
    gates: list[dict[str, Any]],
    half_extent: float,
) -> dict[str, int]:
    """``stations.json``, ``ways.json``, ``gates.json`` in ``target``; returns points per route."""
    target.mkdir(parents=True, exist_ok=True)
    area = box(-half_extent, -half_extent, half_extent, half_extent)
    routes = {
        "stations": station_route(stations),
        "ways": ways_route(way_runs(streets, bodies, area)),
        "gates": gates_route(course, gates, bodies, streets),
    }
    for name, r in routes.items():
        (target / f"{name}.json").write_text(
            json.dumps(r, ensure_ascii=False, indent=1) + "\n", encoding="utf-8"
        )
    return {name: len(r["points"]) for name, r in routes.items()}
