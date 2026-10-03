"""Static walkthrough of an assembled world (W3): what the character controller will meet.

Reads the world as the engine does: the exported heightmap (with carved water beds) and the
``COL_`` bodies of every mesh vob. Checks, before the engine's ``--walk`` autopilot exists:

a. Lane widths along the OSM ways and narrow slots between collision bodies.
b. Slopes along the ways (``steps`` ways separately, they are ramps in the heightmap) and door
   steps of the generated houses (door bottom = floor height against the ground at the door edge).
c. City wall: walk, stairs, gates and passages against the controller (from the rules).

Character values come from ``game/config/movement.toml`` (radius, height, step, max slope);
the thresholds are in :data:`LIMITS`. Everything is 2D in XZ, with each body cut to the
character's height band above the ground at its centre, so it is an estimate, not physics.
"""

from __future__ import annotations

import math
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, MultiPoint, Point, Polygon, box
from shapely.ops import unary_union

from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.export.terrain import Grid

FORMAT, VERSION = "gothar-begehung", 1
LIMITS = {
    "laneTightM": 1.2,  # narrower: two people cannot pass, the camera gets close
    "laneBlockedM": 0.7,  # narrower: the cylinder (0.6 m) does not fit with a margin
    "slopeSteepDeg": 35.0,  # steeper: tiring in Gothic terms, slow and sliding near the limit
    "doorStepM": 0.4,  # door bottom higher above the ground than one step (controller step)
    "doorBuriedM": 0.15,  # door bottom deeper under the ground than this: looks buried
    "sampleM": 1.0,
    "rayM": 12.0,
}
WALKABLE = {
    "footway",
    "path",
    "service",
    "residential",
    "steps",
    "secondary",
    "tertiary",
    "track",
    "living_street",
    "unclassified",
    "pedestrian",
    "cycleway",
    "secondary_link",
    "tertiary_link",
}


@dataclass
class Character:
    radius: float
    height: float
    step: float
    max_slope: float
    eye: float

    @staticmethod
    def load(path: Path) -> Character:
        """Step, slope and eye height from movement.toml; the cylinder is fixed in physics
        (``Character.hpp``: radius 0.3 m, height 1.8 m)."""
        doc = tomllib.loads(path.read_text(encoding="utf-8"))
        flat = {k: v for sec in doc.values() if isinstance(sec, dict) for k, v in sec.items()}
        return Character(
            0.3,
            1.8,
            float(flat.get("step_height", 0.4)),
            float(flat.get("max_slope_degrees", 50.0)),
            float(flat.get("eye_height", 1.62)),
        )


@dataclass
class Body:
    owner: str  # vob name
    poly: Polygon  # XZ section inside the character's height band
    bottom: float
    top: float


def game_grid(world: dict[str, Any], assets: Path) -> Grid:
    """The heightmap the engine loads (``terrain`` block of the world, ``.r16`` next to it)."""
    t = world["terrain"]
    raw = np.fromfile(assets / t["heightmap"], dtype="<u2").reshape(t["height"], t["width"])
    h = t["minY"] + raw.astype(np.float64) / 65535.0 * (t["maxY"] - t["minY"])
    return Grid(h, float(t["firstSample"][0]), float(t["firstSample"][1]), float(t["cellSize"]))


def _accessor(doc: dict[str, Any], binary: bytes, index: int) -> np.ndarray:
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    dtype = {5126: "<f4", 5125: "<u4", 5123: "<u2"}[acc["componentType"]]
    width = {"SCALAR": 1, "VEC3": 3}[acc["type"]]
    a = np.frombuffer(binary, dtype=dtype, count=acc["count"] * width, offset=start)
    return a.reshape(-1, width) if width > 1 else a


def collision_parts(data: bytes) -> list[tuple[str, np.ndarray, np.ndarray]]:
    """(node name, positions, triangle indices) of the ``COL_`` nodes of a .glb (node-local)."""
    doc, binary = read_glb(data)
    out = []
    for node in doc.get("nodes", []):
        name = node.get("name", "")
        if not name.startswith("COL_") or "mesh" not in node:
            continue
        prim = doc["meshes"][node["mesh"]]["primitives"][0]
        pos = _accessor(doc, binary, prim["attributes"]["POSITION"]).astype(np.float64)
        pos = pos + np.asarray(node.get("translation", [0.0, 0.0, 0.0]))
        idx = _accessor(doc, binary, prim["indices"]).reshape(-1, 3)
        out.append((name, pos, idx))
    return out


def slab_section(pos: np.ndarray, tris: np.ndarray, lo: float, hi: float) -> Polygon | None:
    """XZ convex hull of a convex body cut to ``lo <= y <= hi`` (vertices plus edge crossings)."""
    pts = [p[[0, 2]] for p in pos if lo <= p[1] <= hi]
    edges = {tuple(sorted((int(t[i]), int(t[(i + 1) % 3])))) for t in tris for i in range(3)}
    for a, b in edges:
        pa, pb = pos[a], pos[b]
        for y in (lo, hi):
            if (pa[1] - y) * (pb[1] - y) < 0:
                f = (y - pa[1]) / (pb[1] - pa[1])
                pts.append((pa + (pb - pa) * f)[[0, 2]])
    if len(pts) < 3:
        return None
    hull = MultiPoint([tuple(p) for p in pts]).convex_hull
    return hull if isinstance(hull, Polygon) and hull.area > 1e-4 else None


def _quat_yaw(q: list[float]) -> float:
    return 2.0 * math.atan2(q[1], q[3])


def load_bodies(world: dict[str, Any], assets: Path, grid: Grid, ch: Character) -> list[Body]:
    """Every ``COL_`` body of the world's mesh vobs, cut to the band step .. height above ground."""
    by_id = {v["id"]: v for v in world["vobs"]}

    def offset(v: dict[str, Any]) -> np.ndarray:
        p = np.asarray(v["pos"], dtype=np.float64)
        while "parent" in v:
            v = by_id[v["parent"]]
            p = p + np.asarray(v["pos"])  # groups carry no rotation (assembler)
        return p

    cache: dict[str, list[tuple[str, np.ndarray, np.ndarray]]] = {}
    bodies = []
    for v in world["vobs"]:
        if v["type"] != "mesh" or not v.get("mesh"):
            continue
        if v["mesh"] not in cache:
            path = assets / v["mesh"]
            cache[v["mesh"]] = collision_parts(path.read_bytes()) if path.is_file() else []
        yaw = _quat_yaw(v["rot"])
        c, s = math.cos(yaw), math.sin(yaw)
        o = offset(v)
        for _, pos, tris in cache[v["mesh"]]:
            p = pos.copy()
            p[:, 0], p[:, 2] = c * pos[:, 0] + s * pos[:, 2], -s * pos[:, 0] + c * pos[:, 2]
            p += o
            cx, cz = p[:, 0].mean(), p[:, 2].mean()
            ground = grid.height_at(float(cx), float(cz))
            poly = slab_section(p, tris, ground + ch.step + 0.05, ground + ch.height)
            if poly is not None:
                bodies.append(Body(v["name"], poly, float(p[:, 1].min()), float(p[:, 1].max())))
    return bodies


def _runs(flags: np.ndarray) -> list[tuple[int, int]]:
    """[start, end) index ranges where ``flags`` is true."""
    out, start = [], None
    for i, f in enumerate(flags):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, len(flags)))
    return out


def _way_samples(points: list[list[float]], step: float) -> tuple[np.ndarray, np.ndarray]:
    line = LineString(points)
    n = max(2, int(line.length / step) + 1)
    d = np.linspace(0.0, line.length, n)
    pts = np.array([line.interpolate(x).coords[0] for x in d])
    tangent = np.gradient(pts, axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-9)
    return pts, tangent


def lanes(
    streets: list[dict[str, Any]], bodies: list[Body], area: Polygon, limits: dict[str, float]
) -> dict[str, Any]:
    """Free width across each way (rays to both sides) and narrow runs along it."""
    polys = [b.poly for b in bodies]
    tree = shapely.STRtree(polys)
    ray = limits["rayM"]
    found, sampled, inside_total = [], 0, 0
    for w in streets:
        if w["highway"] not in WALKABLE or w.get("tunnel") or w.get("bridge") or w.get("layer"):
            continue
        line = LineString(w["points"])
        if not line.intersects(area) or line.length < 2.0:
            continue
        pts, tan = _way_samples(w["points"], limits["sampleM"])
        keep = shapely.contains(area, shapely.points(pts))
        if not keep.any():
            continue
        pts, tan = pts[keep], tan[keep]
        sampled += len(pts)
        normal = np.stack([-tan[:, 1], tan[:, 0]], axis=1)
        width = np.empty(len(pts))
        blocked = np.zeros(len(pts), dtype=bool)
        for i, (p, n) in enumerate(zip(pts, normal, strict=True)):
            here = Point(p)
            hit = tree.query(here, predicate="intersects")
            if len(hit):
                blocked[i] = True
                width[i] = 0.0
                continue
            side = []
            for sgn in (1.0, -1.0):
                r = LineString([p, p + sgn * n * ray])
                cand = tree.query(r, predicate="intersects")
                side.append(
                    min((here.distance(polys[k].intersection(r)) for k in cand), default=ray)
                )
            width[i] = sum(side)
        inside_total += int(blocked.sum())
        for flag, kind in (
            (width < limits["laneBlockedM"], "blocked"),
            ((width >= limits["laneBlockedM"]) & (width < limits["laneTightM"]), "tight"),
        ):
            for a, b in _runs(flag):
                seg = width[a:b]
                mid = pts[(a + b - 1) // 2]
                through = bool(blocked[a:b].all())
                owners = sorted({bodies[k].owner for k in tree.query(Point(mid), "intersects")})
                found.append(
                    {
                        "kind": "throughBody" if through else kind,
                        "osmId": w["osmId"],
                        "highway": w["highway"],
                        "name": w.get("name", ""),
                        "lengthM": round(float(b - a), 1),
                        "minWidthM": round(float(seg.min()), 2),
                        "at": [round(float(mid[0]), 1), round(float(mid[1]), 1)],
                        "bodies": owners,
                    }
                )
    found.sort(key=lambda f: (f["kind"] != "blocked", f["minWidthM"], -f["lengthM"]))
    count = {k: sum(f["kind"] == k for f in found) for k in ("blocked", "tight", "throughBody")}
    owners: dict[str, int] = {}
    for f in found:
        if f["kind"] == "throughBody":
            for o in {o.split("_")[0] for o in f["bodies"]}:
                owners[o] = owners.get(o, 0) + 1
    return {"samples": sampled, "runs": count, "throughByOwner": owners, "list": found}


def slots(bodies: list[Body], area: Polygon, ch: Character) -> dict[str, Any]:
    """Free space too narrow for the cylinder (between houses), by morphological opening."""
    solid = unary_union([b.poly for b in bodies])
    free = area.difference(solid)
    r = ch.radius + 0.05
    passable = free.buffer(-r, join_style="mitre").buffer(r, join_style="mitre")
    narrow = free.difference(passable.buffer(0.01))
    parts = [g for g in getattr(narrow, "geoms", [narrow]) if g.area >= 1.0]
    parts.sort(key=lambda g: -g.area)
    return {
        "count": len(parts),
        "areaM2": round(sum(g.area for g in parts), 1),
        "largest": [
            {"areaM2": round(g.area, 1), "at": [round(g.centroid.x, 1), round(g.centroid.y, 1)]}
            for g in parts[:10]
        ],
    }


def slopes(
    streets: list[dict[str, Any]],
    grid: Grid,
    area: Polygon,
    ch: Character,
    limits: dict[str, float],
) -> dict[str, Any]:
    """Slope along every way over 2 m; runs steeper than the limits, ``steps`` ways apart."""
    found, stairs = [], []
    for w in streets:
        if w["highway"] not in WALKABLE or w.get("tunnel") or w.get("bridge") or w.get("layer"):
            continue
        if not LineString(w["points"]).intersects(area):
            continue
        pts, _ = _way_samples(w["points"], limits["sampleM"])
        keep = shapely.contains(area, shapely.points(pts))
        if keep.sum() < 3:
            continue
        pts = pts[keep]
        h = np.array([grid.height_at(float(x), float(z)) for x, z in pts])
        run = np.linalg.norm(pts[2:] - pts[:-2], axis=1)
        ang = np.degrees(np.arctan2(np.abs(h[2:] - h[:-2]), np.maximum(run, 1e-6)))
        if w["highway"] == "steps":
            stairs.append(
                {
                    "osmId": w["osmId"],
                    "maxDeg": round(float(ang.max()), 1),
                    "riseM": round(float(h.max() - h.min()), 1),
                    "at": [
                        round(float(pts[len(pts) // 2][0]), 1),
                        round(float(pts[len(pts) // 2][1]), 1),
                    ],
                }
            )
            continue
        for lim, kind in ((ch.max_slope, "tooSteep"), (limits["slopeSteepDeg"], "steep")):
            flag = ang > lim if kind == "tooSteep" else (ang > lim) & (ang <= ch.max_slope)
            for a, b in _runs(flag):
                mid = pts[1 + (a + b - 1) // 2]
                found.append(
                    {
                        "kind": kind,
                        "osmId": w["osmId"],
                        "highway": w["highway"],
                        "name": w.get("name", ""),
                        "maxDeg": round(float(ang[a:b].max()), 1),
                        "lengthM": round(float(b - a), 1),
                        "at": [round(float(mid[0]), 1), round(float(mid[1]), 1)],
                    }
                )
    found.sort(key=lambda f: -f["maxDeg"])
    stairs.sort(key=lambda s: -s["maxDeg"])
    return {
        "runs": {k: sum(f["kind"] == k for f in found) for k in ("tooSteep", "steep")},
        "list": found,
        "steps": {
            "count": len(stairs),
            "tooSteep": sum(s["maxDeg"] > ch.max_slope for s in stairs),
            "steep": sum(limits["slopeSteepDeg"] < s["maxDeg"] <= ch.max_slope for s in stairs),
            "list": stairs,
        },
    }


def door_steps(
    buildings: list[dict[str, Any]],
    index: dict[str, Any],
    streets: list[dict[str, Any]],
    grid: Grid,
    area: Polygon,
    limits: dict[str, float],
) -> dict[str, Any]:
    """Door bottom (floor of the house) against the ground in front of the door.

    Index entries with ``doors`` (written by the generator since E1) are exact: the point in front
    of the main door and the floor. Older entries: the street test of the generator is
    approximated by the edge whose outer midpoint lies nearest to a way.
    """
    floor = {e["id"]: e["groundY"] for e in index.get("entries", []) if "groundY" in e}
    exact = {e["id"]: e["doors"] for e in index.get("entries", []) if e.get("doors")}
    ways = unary_union(
        [
            LineString(w["points"])
            for w in streets
            if w["highway"] in WALKABLE and not w.get("tunnel") and len(w["points"]) >= 2
        ]
    )
    high, buried, checked, fixable = [], [], 0, 0
    kinds: dict[str, int] = {}
    for bid, doors in exact.items():  # the generator wrote its doors (E1): no guessing
        x, z, y = doors[0][:3]
        kind = doors[0][3] if len(doors[0]) > 3 else "ground"
        kinds[kind] = kinds.get(kind, 0) + 1
        if not area.contains(Point(x, z)):
            continue
        checked += 1
        step = y - grid.height_at(x, z)
        item = {"id": bid, "stepM": round(step, 2), "at": [x, z]}
        if step > limits["doorStepM"]:
            high.append(item)
        elif step < -limits["doorBuriedM"]:
            buried.append(item)
    for b in buildings:
        bid = b.get("id")
        if bid not in floor or bid in exact:
            continue
        ring = b.get("footprint") or (b.get("masses") or [{}])[0].get("footprint") or []
        if len(ring) < 3 or not area.contains(Point(ring[0])):
            continue
        poly = Polygon(ring)
        if not poly.is_valid or poly.area < 4.0:
            continue
        coords = list(poly.exterior.coords)[:-1]
        best, edge_steps = None, []
        for i, a in enumerate(coords):
            c = coords[(i + 1) % len(coords)]
            length = math.dist(a, c)
            if length < 2.0:
                continue
            mx, mz = (a[0] + c[0]) / 2, (a[1] + c[1]) / 2
            nx, nz = (c[1] - a[1]) / length, -(c[0] - a[0]) / length
            out = Point(mx + nx * 0.6, mz + nz * 0.6)
            if poly.contains(out):
                out = Point(mx - nx * 0.6, mz - nz * 0.6)
            edge_steps.append(floor[bid] - grid.height_at(out.x, out.y))
            score = (round(ways.distance(out) / 4.0), -length)  # nearest street, then longest
            if best is None or score < best[0]:
                best = (score, out)
        if best is None:
            continue
        checked += 1
        step = floor[bid] - grid.height_at(best[1].x, best[1].y)
        item = {
            "id": bid,
            "stepM": round(step, 2),
            "at": [round(best[1].x, 1), round(best[1].y, 1)],
        }
        if step > limits["doorStepM"]:
            high.append(item)
        elif step < -limits["doorBuriedM"]:
            buried.append(item)
        else:
            continue
        # Would another edge (of at least 2 m) suit the floor height?
        fixable += any(-limits["doorBuriedM"] <= e <= limits["doorStepM"] for e in edge_steps)
    high.sort(key=lambda d: -d["stepM"])
    buried.sort(key=lambda d: d["stepM"])
    return {
        "checked": checked,
        "high": len(high),
        "buried": len(buried),
        "fixableByOtherEdge": fixable,
        "kinds": dict(sorted(kinds.items())),
        "buriedMedianM": round(float(np.median([d["stepM"] for d in buried])), 2)
        if buried
        else 0.0,
        "highList": high[:20],
        "buriedList": buried[:20],
    }


def citywall(rules: dict[str, Any], stats: dict[str, Any], ch: Character) -> list[dict[str, Any]]:
    """Wall measures against the controller (values from building_rules.json)."""
    w = rules["cityWall"]
    st, tower, gate, pforte = w["stairs"], w["tower"], w["gate"], w["pforte"]
    door = rules.get("openings", {}).get("door", {"w": 1.0, "h": 2.1})
    d = 2 * ch.radius
    rows = [
        ("Wehrgang Breite", w["walkM"], d + 0.2, "m", "Zylinder + 0,2 m"),
        ("Brustwehr", w["parapetM"], 0.5, "m", "hält den Spieler, ohne die Sicht zu sperren"),
        ("Treppenstufe Höhe", st["rise"], ch.step, "m", "Stufenhöhe des Controllers (höchstens)"),
        ("Treppe Breite", st["width"], d + 0.2, "m", "Zylinder + 0,2 m"),
        (
            "Treppe Steigung",
            round(math.degrees(math.atan2(st["rise"], st["run"])), 1),
            ch.max_slope,
            "°",
            "Grenzwinkel (höchstens)",
        ),
        ("Turmdurchgang Höhe", tower["passageH"], ch.height + 0.2, "m", "Figur + 0,2 m"),
        ("Stadttor Breite", gate["passageW"], d + 0.2, "m", "Zylinder + 0,2 m"),
        ("Stadttor Höhe (Kämpfer)", gate["springH"], ch.height + 0.2, "m", "Figur + 0,2 m"),
        ("Pforte Breite", pforte["w"], d + 0.2, "m", "Zylinder + 0,2 m"),
        ("Pforte Höhe", pforte["h"], ch.height + 0.2, "m", "Figur + 0,2 m"),
        ("Haustür Breite", door["w"], d + 0.2, "m", "Zylinder + 0,2 m"),
        ("Haustür Höhe", door["h"], ch.height + 0.2, "m", "Figur + 0,2 m"),
    ]
    out = []
    for name, value, need, unit, why in rows:
        upper = "höchstens" in why
        ok = value <= need if upper else value >= need
        out.append(
            {
                "name": name,
                "value": value,
                "limit": round(need, 2),
                "unit": unit,
                "rule": why,
                "ok": ok,
            }
        )
    out.append(
        {
            "name": "offene Mauerenden",
            "value": len(stats.get("openEnds", [])),
            "limit": 0,
            "unit": "",
            "rule": "keine",
            "ok": not stats.get("openEnds"),
        }
    )
    return out


def run(
    world_path: Path,
    assets: Path,
    work: Path,
    rules: dict[str, Any],
    movement: Path,
    half_extent: float,
) -> dict[str, Any]:
    """The whole walkthrough as one report (``generated/begehung.json``)."""
    import json

    world = json.loads(world_path.read_text(encoding="utf-8"))
    ch = Character.load(movement)
    grid = game_grid(world, assets)
    area = box(-half_extent, -half_extent, half_extent, half_extent)
    bodies = load_bodies(world, assets, grid, ch)
    streets = json.loads((work / "streets.json").read_text(encoding="utf-8"))["streets"]
    buildings = json.loads((work / "buildings.json").read_text(encoding="utf-8"))
    buildings = buildings.get("buildings", buildings.get("entries", []))
    gen = world_path.parent / "generated"
    index = json.loads((gen / "buildings_index.json").read_text(encoding="utf-8"))
    wall_path = gen / "citywall_index.json"
    wall = json.loads(wall_path.read_text(encoding="utf-8")) if wall_path.is_file() else {}
    return {
        "format": FORMAT,
        "version": VERSION,
        "world": world_path.name,
        "character": vars(ch),
        "limits": LIMITS,
        "areaHalfExtentM": half_extent,
        "bodies": len(bodies),
        "lanes": lanes(streets, bodies, area, LIMITS),
        "slots": slots(bodies, area, ch),
        "slopes": slopes(streets, grid, area, ch, LIMITS),
        "doors": door_steps(buildings, index, streets, grid, area, LIMITS),
        "citywall": citywall(rules, wall.get("stats", {}), ch),
    }
