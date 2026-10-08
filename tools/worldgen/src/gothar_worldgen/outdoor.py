"""Lanes and yards come alive (W6 streets): props before and behind the houses, trees, bushes and
grass, as plain mesh vobs (``data/<site>/outdoor.json``, design doc "Gassen beleben").

Everything stands on the ground beside the houses' collision bodies and keeps clear of what the
player and the NPCs need:

- a corridor along every walkable street axis (at least ``minCorridorM`` to each side, wider streets
  up to ``streetMarginM`` short of their edge; steps, paths and tracks (``pathKinds``) over their
  whole width),
- before every door its swing and a lane out (``doorLane`` wide x deep) plus the way from there to
  the street axis, and around the routine places of the uses and the waynet's freepoints a disc of
  ``placeRadiusM``,
- along the waynet's edges ``waynetM`` to each side (the waynet of the world without these things;
  built again afterwards, it stays the same),
- the hand-made landmarks (castle garden, church) by ``handmadeM``.

Props stand against a house wall facing away from it (``frontM``: a wall within that distance of a
street is the front, else the back yard). Trees come from OSM (``natural=tree``), moved a little if
needed, fruit trees and bushes from a grid over the yards, grass and weeds at the feet of the walls.
All choices are seeded per house or place: one house changing does not move the others' things.
"""

from __future__ import annotations

import json
import math
import random
from collections import Counter, defaultdict
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from functools import cache
from pathlib import Path
from typing import Any

from shapely.geometry import LineString, Point, Polygon
from shapely.geometry.polygon import orient
from shapely.ops import unary_union
from shapely.strtree import STRtree

FORMAT_VERSION = 1
GROUP_NAME = "WORLDGEN_GASSEN"
OUTLINE_M = 0.08  # house outlines simplified by this much for the walls props stand at
WALL_GAP_M = 0.12  # props this far off the (simplified) wall: never into the house
Pt = tuple[float, float]


class OutdoorError(ValueError):
    pass


@dataclass(frozen=True)
class Rules:
    data: dict[str, Any]

    @classmethod
    def load(cls, path: Path) -> Rules:
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise OutdoorError(f"{path.name}: {e}") from e
        if doc.get("version") != FORMAT_VERSION:
            raise OutdoorError(f"{path.name}: version {doc.get('version')} != {FORMAT_VERSION}")
        for kind in _all_kinds(doc):
            if _builder(kind) is None:
                raise OutdoorError(f"{path.name}: unknown model {kind!r}")
        return cls(doc)

    @property
    def keep(self) -> dict[str, Any]:
        return dict(self.data["keep"])


def _all_kinds(doc: dict[str, Any]) -> set[str]:
    kinds: set[str] = set()
    groups = doc.get("groups", {})
    names: set[str] = set()
    for spec in doc.get("uses", {}).values():
        for side in ("front", "back"):
            names.update(spec.get(side, []))
    for side in ("front", "back"):
        names.update(doc.get("houses", {}).get(side, {}).get("pick", {}))
    names.update(doc.get("kerb", {}).get("pick", {}))
    for name in names:
        kinds.update(groups.get(name, [name]))
    for members in groups.values():
        kinds.update(members)
    kinds.update(doc.get("market", {}).get("pick", {}))
    kinds.update(doc.get("trees", {}).get("pick", {}))
    kinds.update(doc.get("trees", {}).get("garden", {}))
    kinds.update(doc.get("yards", {}).get("bush", {}).get("pick", {}))
    kinds.update(doc.get("grass", {}).get("pick", {}))
    if doc.get("yards", {}).get("fruitTree"):
        kinds.add("tree_fruit")
    return kinds


def _builder(kind: str) -> Callable[[], Any] | None:
    from gothar_worldgen.mobs import PROPS
    from gothar_worldgen.vegetation import VEGETATION

    return PROPS.get(kind) or VEGETATION.get(kind)


def mesh_path(kind: str) -> str:
    from gothar_worldgen.vegetation import VEGETATION

    return f"{'vegetation' if kind in VEGETATION else 'props'}/{kind}.glb"


@cache
def footprint(kind: str) -> tuple[float, float]:
    """Width (x) and depth (z) of the model's finest level."""
    builder = _builder(kind)
    if builder is None:
        raise OutdoorError(f"unknown model {kind!r}")
    model = builder()
    xs, zs = [], []
    for b in model.main.builders.values():
        xs += [p[0] for p in b.pos]
        zs += [p[2] for p in b.pos]
    return (max(xs) - min(xs), max(zs) - min(zs))


def yaw_quat(fx: float, fz: float) -> list[float]:
    """Rotation about +Y turning the model's front (+Z) into (fx, fz) (as ``uses.inside``)."""
    a = math.atan2(fx, fz)
    return [0.0, round(math.sin(a / 2), 5) + 0.0, 0.0, round(math.cos(a / 2), 5) + 0.0]


def _rect(c: Pt, u: Pt, w: float, d: float) -> Polygon:
    """Rectangle around ``c``: ``w`` along ``u``, ``d`` across."""
    ux, uz = u
    nx, nz = -uz, ux
    hw, hd = w / 2, d / 2
    return Polygon([(c[0] + sx * hw * ux + sy * hd * nx, c[1] + sx * hw * uz + sy * hd * nz)
                    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])  # fmt: skip


class _Shapes:
    """Shapes with a spatial index (built once)."""

    def __init__(self, shapes: Sequence[Any], owners: Sequence[str] | None = None) -> None:
        self.shapes = list(shapes)
        self.owners = list(owners or [""] * len(self.shapes))
        self.tree = STRtree(self.shapes) if self.shapes else None

    def hits(self, shape: Any, ignore: str = "") -> bool:  # noqa: ANN401
        if self.tree is None:
            return False
        for i in self.tree.query(shape, predicate="intersects"):
            if not ignore or self.owners[int(i)] != ignore:
                return True
        return False

    def distance(self, shape: Any, limit: float) -> float:  # noqa: ANN401
        """Distance to the nearest shape, ``limit`` if none is closer."""
        if self.tree is None:
            return limit
        best = limit
        for i in self.tree.query(shape, predicate="dwithin", distance=limit):
            best = min(best, float(self.shapes[int(i)].distance(shape)))
        return best


class _Placed:
    """What was placed so far, in 4 m cells (things keep ``gap`` apart)."""

    def __init__(self) -> None:
        self.cells: dict[tuple[int, int], list[Polygon]] = defaultdict(list)

    def _keys(self, shape: Polygon) -> list[tuple[int, int]]:
        x0, z0, x1, z1 = shape.bounds
        return [(i, j) for i in range(math.floor(x0 / 4), math.floor(x1 / 4) + 1)
                for j in range(math.floor(z0 / 4), math.floor(z1 / 4) + 1)]  # fmt: skip

    def clear(self, shape: Polygon, gap: float) -> bool:
        grown = shape.buffer(gap)
        return not any(q.intersects(grown) for k in self._keys(grown) for q in self.cells[k])

    def add(self, shape: Polygon) -> None:
        for k in self._keys(shape):
            self.cells[k].append(shape)


@dataclass
class Plan:
    vobs: list[dict[str, Any]] = field(default_factory=list)
    failed: list[dict[str, str]] = field(default_factory=list)
    counts: Counter[str] = field(default_factory=Counter)

    def json(self) -> dict[str, Any]:
        return {"version": FORMAT_VERSION, "counts": dict(sorted(self.counts.items())),
                "vobs": len(self.vobs), "failed": self.failed}  # fmt: skip


@dataclass
class Site:
    """What the planner needs of the assembled world."""

    entries: Sequence[dict[str, Any]]  # buildings_index entries (id, doors)
    uses: dict[str, str]  # house id -> use
    places: Sequence[Pt]  # routine places, mobs, freepoints (x, z)
    bodies: Sequence[tuple[str, Polygon]]  # (vob name, section) of every collision body
    streets: Sequence[dict[str, Any]]  # streets.json entries
    features: Sequence[dict[str, Any]]  # features.json entries
    height_at: Callable[[float, float], float]
    ways: Sequence[tuple[Pt, Pt]] = ()  # waynet edges of the world without these things
    squares: Sequence[dict[str, Any]] = ()  # streets.json squares (the market)
    spots: Sequence[Pt] = ()  # its freepoints


def _steps(lo: float, hi: float, step: float) -> list[float]:
    out = []
    t = lo
    while t <= hi + 1e-9:
        out.append(t)
        t += step
    return out


def _pick(rng: random.Random, weights: dict[str, float]) -> str:
    kinds = sorted(weights)
    return rng.choices(kinds, [weights[k] for k in kinds])[0]


def _label(prefix: str, house: str, key: str) -> str:
    """``GASSE_<house>_<group>_<part>`` from a key ``<house>:<side>:<group>:<part>`` (B: yard)."""
    _, side, group, part = key.split(":")
    return f"{prefix}_{house.replace('-', 'M')}_{'B' if side == 'back' else ''}{group}_{part}"


def _name(prefix: str, house: str, *parts: str) -> str:
    """Vob name; the house id keeps its case like ``BLD_<id>`` (ids differ in case only)."""
    return "_".join(
        [prefix, house.replace("-", "M"), *(p.upper().replace("-", "M") for p in parts)]
    )


class _Planner:
    def __init__(self, rules: Rules, site: Site) -> None:
        from gothar_worldgen.waynet.generate import SKIP_HIGHWAYS, STAIRS

        self.rules = rules.data
        self.site = site
        keep = rules.keep
        self.gap = float(keep["propGapM"])
        self.max_slope = float(keep["maxSlopeM"])
        self.plan = Plan()
        self.placed = _Placed()
        axes, corridors, widths, surfaces = [], [], [], []
        self.streets: list[tuple[LineString, float, float, str]] = []  # axis, width, half, id
        for s in site.streets:
            pts = s.get("points") or []
            if s.get("highway") in SKIP_HIGHWAYS or len(pts) < 2:
                continue
            if s.get("tunnel") and int(s.get("layer", 0) or 0) < 0:
                continue
            w = float(s.get("widthM") or 3.0)
            line = LineString(pts)
            if s.get("highway") in STAIRS:
                half = w / 2 + float(keep["stepsExtraM"])
            elif s.get("highway") in set(keep.get("pathKinds", [])):  # paths: wholly free
                half = w / 2 + float(keep.get("pathExtraM", 0.2))
            else:
                half = max(float(keep["minCorridorM"]), w / 2 - float(keep["streetMarginM"]))
            axes.append(line)
            widths.append(w)
            corridors.append(line.buffer(half))
            self.streets.append((line, w, half, str(s.get("osmId", ""))))
            # as it looks: the splat map blurs the street's edge by 1.5 m (export/splat.py)
            surfaces.append(
                line.buffer(w / 2 + float(rules.data.get("kerb", {}).get("edgeGapM", 0.0)))
            )
        self.axes = _Shapes(axes)
        self.surfaces = _Shapes(surfaces)  # the streets' whole width: free-standing things stay off
        self.widths = widths
        lane_w, lane_d = (float(v) for v in keep["doorLane"])
        lanes = []
        for e in site.entries:
            for d in e.get("doors", []):
                x, z, nx, nz = float(d[0]), float(d[1]), float(d[4]), float(d[5])
                c = (x + nx * lane_d / 2, z + nz * lane_d / 2)
                lanes.append(_rect(c, (-nz, nx), lane_w, lane_d))
                out = Point(x + nx * lane_d, z + nz * lane_d)
                near = self._nearest_axis(out)
                if near is not None and near.distance(out) < 25.0:
                    to = near.interpolate(near.project(out))
                    lanes.append(LineString([out, to]).buffer(float(keep["approachM"])))
        r = float(keep["placeRadiusM"])
        discs = [Point(p).buffer(r) for p in [*site.places, *site.spots]]
        ways = [LineString([a, b]).buffer(float(keep["waynetM"])) for a, b in site.ways]
        self.keep = _Shapes([*corridors, *lanes, *discs, *ways])
        self.bodies = _Shapes([poly for _, poly in site.bodies], [o for o, _ in site.bodies])
        houses: dict[str, list[Polygon]] = defaultdict(list)
        landmarks: dict[str, list[Polygon]] = defaultdict(list)
        walls: list[Polygon] = []
        for owner, poly in site.bodies:
            if owner.startswith("BLD_") and "_RAUM_" not in owner:
                houses[owner[4:]].append(poly)
            elif owner.startswith("HANDMADE_"):
                landmarks[owner].append(poly)
            elif owner.startswith("CITYWALL_"):
                walls.append(poly)
        self.houses = {k: unary_union(v) for k, v in houses.items()}
        # the outlines of the collision pieces are ragged (many short edges): walls to stand at
        self.outlines = {k: v.simplify(OUTLINE_M) for k, v in self.houses.items()}
        self.house_shapes = _Shapes(list(self.houses.values()), list(self.houses))
        margin = float(keep["handmadeM"])
        self.landmarks = _Shapes([unary_union(v).convex_hull.buffer(margin)
                                  for v in landmarks.values()])  # fmt: skip
        self.walls = walls

    def _nearest_axis(self, p: Point) -> LineString | None:
        if self.axes.tree is None:
            return None
        return self.axes.shapes[int(self.axes.tree.nearest(p))]

    def street_gap(self, p: Point) -> float:
        """Distance from the nearest street's edge (negative on it)."""
        if self.axes.tree is None:
            return math.inf
        best = math.inf
        for i in self.axes.tree.query(p.buffer(30.0)):
            best = min(best, float(self.axes.shapes[int(i)].distance(p)) - self.widths[int(i)] / 2)
        return best

    def ground(self, shape: Polygon) -> float | None:
        """Lowest ground under the corners, None if they differ by more than the slope limit."""
        hs = [self.site.height_at(x, z) for x, z in list(shape.exterior.coords)[:-1]]
        if max(hs) - min(hs) > self.max_slope:
            return None
        return min(hs)

    def add(self, key: str, name: str, kind: str, pos: tuple[float, float, float],
            rot: list[float]) -> None:  # fmt: skip
        self.plan.vobs.append({"key": f"outdoor:{key}", "name": name, "type": "mesh",
                               "pos": [round(c, 4) for c in pos], "rot": rot,
                               "mesh": mesh_path(kind)})  # fmt: skip
        self.plan.counts[kind] += 1

    # --- props at the walls, in groups ---------------------------------------------------------

    def group(self, name: str) -> list[str]:
        """The kinds of a group recipe (``groups``), or the single kind ``name``."""
        return list(self.rules.get("groups", {}).get(name, [name]))

    def _row(self, kinds: Sequence[str], start: Pt, u: Pt, n: Pt,
             back: float) -> list[tuple[str, Pt, Polygon]] | None:  # fmt: skip
        """The group side by side from ``start`` along ``u``, backs ``back`` off the line, facing
        ``n``; None if any of them is in the way of something."""
        gap = float(self.rules.get("groupGapM", 0.15))
        out = []
        t = 0.0
        for kind in kinds:
            w, d = footprint(kind)
            c = (start[0] + u[0] * (t + w / 2) + n[0] * (back + d / 2),
                 start[1] + u[1] * (t + w / 2) + n[1] * (back + d / 2))  # fmt: skip
            t += w + gap
            rect = _rect(c, u, w, d)
            if self.keep.hits(rect) or self.bodies.hits(rect) or self.landmarks.hits(rect):
                return None
            if not self.placed.clear(rect, self.gap):
                return None
            out.append((kind, c, rect))
        return out

    def _length(self, kinds: Sequence[str]) -> float:
        gap = float(self.rules.get("groupGapM", 0.15))
        return sum(footprint(k)[0] for k in kinds) + gap * (len(kinds) - 1)

    def _put(self, row: list[tuple[str, Pt, Polygon]], n: Pt, key: str, label: str) -> bool:
        """Places a row if the ground allows; ``key`` and ``label`` + index name its things."""
        ys = [self.ground(rect) for _, _, rect in row]
        if any(y is None for y in ys):
            return False
        for k, ((kind, c, rect), y) in enumerate(zip(row, ys, strict=True), start=1):
            self.placed.add(rect)
            assert y is not None
            self.add(f"{key}:{k}", f"{label}_{k}_{kind.upper()}", kind, (c[0], y, c[1]),
                     yaw_quat(*n))  # fmt: skip
        return True

    def wall_edges(self, hid: str, side: str) -> list[tuple[Pt, Pt, Pt, float]]:
        """(start, along, out, length) of the house's walls on ``side`` (front: within ``frontM``
        of a street)."""
        poly = self.outlines.get(hid)
        if poly is None:
            return []
        front_m = float(self.rules["frontM"])
        out = []
        for part in getattr(poly, "geoms", [poly]):
            ring = list(orient(part, 1.0).exterior.coords)  # counter-clockwise: outside right
            for a, b in zip(ring, ring[1:], strict=False):
                length = math.dist(a, b)
                if length < 0.8:
                    continue
                u = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
                n = (u[1], -u[0])
                mid = Point((a[0] + b[0]) / 2 + n[0] * 1.5, (a[1] + b[1]) / 2 + n[1] * 1.5)
                if (self.street_gap(mid) <= front_m) == (side == "front"):
                    out.append(((a[0], a[1]), u, n, length))
        return out

    def wall_group(self, hid: str, kinds: list[str], side: str, rng: random.Random,
                   key: str) -> int:  # fmt: skip
        """Places the group at one of the house's walls; drops things from its end until it
        fits. Returns how many were placed."""
        edges = self.wall_edges(hid, side)
        while kinds:
            need = self._length(kinds)
            spots = [(e, t) for e in edges for t in _steps(0.15, e[3] - need - 0.15, 0.4)]
            rng.shuffle(spots)
            for (a, u, n, _), t in spots:
                row = self._row(kinds, (a[0] + u[0] * t, a[1] + u[1] * t), u, n, WALL_GAP_M)
                if row is not None and self._put(row, n, key, _label("GASSE", hid, key)):
                    return len(kinds)
            kinds = kinds[:-1]
        return 0

    def props(self) -> None:
        houses = self.rules.get("houses", {})
        for e in sorted(self.site.entries, key=lambda e: e["id"]):
            hid = e["id"]
            rng = random.Random(f"{self.rules['seed']}:props:{hid}")
            use = self.site.uses.get(hid)
            wanted: list[tuple[str, str]] = []
            if use is not None:
                spec = self.rules["uses"].get(use, {})
                wanted += [("front", g) for g in spec.get("front", [])]
                wanted += [("back", g) for g in spec.get("back", [])]
            else:
                for side in ("front", "back"):
                    s = houses.get(side)
                    if s and rng.random() < float(s["chance"]):
                        more = s.get("more", [])  # chances of a second, third ... group
                        n = 1 + sum(1 for c in more if rng.random() < float(c))
                        wanted += [(side, _pick(rng, s["pick"])) for _ in range(n)]
            for k, (side, name) in enumerate(wanted, start=1):
                kinds = self.group(name)
                # a group that does not fit in one piece goes in parts (doors split the fronts),
                # what finds no room at the front goes into the yard
                sides = [side, "back"] if side == "front" else [side]
                left, part = kinds, 0
                for where in sides:
                    while left:
                        part += 1
                        got = self.wall_group(hid, left, where, rng, f"{hid}:{side}:{k}:{part}")
                        if got == 0:
                            break
                        left = left[got:]
                if len(left) == len(kinds):
                    if use is not None:
                        self.plan.failed.append({"what": f"{side} {name}", "house": hid})
                    else:
                        self.plan.counts[f"{side} (no room)"] += 1
                elif left:
                    self.plan.counts["group (shortened)"] += 1

    def kerb(self) -> None:
        """Groups at the edge of wide streets where no wall stands, facing the street, outside
        its corridor."""
        spec = self.rules.get("kerb")
        if not spec:
            return
        step, chance = float(spec["stepM"]), float(spec["chance"])
        for i, (axis, w, half, sid) in enumerate(self.streets):
            if w < float(spec["minWidthM"]):
                continue
            rng = random.Random(f"{self.rules['seed']}:kerb:{sid or i}")
            t = step / 2
            while t < axis.length - step / 2:
                here, t = t, t + step
                if rng.random() >= chance:
                    continue
                name = _pick(rng, spec["pick"])
                kinds = self.group(name)
                need = self._length(kinds)
                p0, p1 = axis.interpolate(here), axis.interpolate(min(here + 0.5, axis.length))
                length = math.dist((p0.x, p0.y), (p1.x, p1.y))
                if length < 1e-6:
                    continue
                u = ((p1.x - p0.x) / length, (p1.y - p0.y) / length)
                side = rng.choice((1.0, -1.0))
                n = (-u[1] * side, u[0] * side)  # from the axis outwards
                depth = max(footprint(k)[1] for k in kinds)
                # beside the street's surface (its edge plus a little), never on it
                off = max(half + 0.15, w / 2 + float(spec.get("edgeGapM", 0.1)) + 0.05)
                if off + depth > w / 2 + float(spec.get("beyondEdgeM", 1.5)):
                    continue
                start = (p0.x + n[0] * off - u[0] * need / 2,
                         p0.y + n[1] * off - u[1] * need / 2)  # fmt: skip
                row = self._row(kinds, start, u, n, 0.0)
                if row is not None and any(self.surfaces.hits(r) for _, _, r in row):
                    row = None  # on another street's surface (a crossing, a lane close by)
                face = (-n[0], -n[1])  # the things look onto the street
                if row is not None:
                    self._put(
                        row,
                        face,
                        f"kerb:{sid or i}:{round(here)}",
                        f"RAND_{(sid or str(i)).upper()}_{round(here)}",
                    )

    # --- the market ------------------------------------------------------------------------------

    def market(self) -> None:
        """Market stalls on the market square: near its edge, the counter towards the middle."""
        spec = self.rules.get("market")
        if not spec:
            return
        name = spec["square"]
        squares = [Polygon(q["polygon"]) for q in self.site.squares
                   if q.get("name") == name and len(q.get("polygon", [])) >= 3]  # fmt: skip
        if not squares:
            self.plan.failed.append({"what": "market square", "house": spec["square"]})
            return
        square = squares[0]
        rng = random.Random(f"{self.rules['seed']}:market")
        lo, hi = (float(v) for v in spec["edgeM"])
        gap = float(spec["gapM"])
        ring = list(orient(square, 1.0).exterior.coords)
        cells = []
        x0, z0, x1, z1 = square.bounds
        for i in range(math.floor(x0), math.ceil(x1)):
            for j in range(math.floor(z0), math.ceil(z1)):
                q = Point(i + 0.5, j + 0.5)
                if square.contains(q) and lo <= square.exterior.distance(q) <= hi:
                    cells.append((i + 0.5, j + 0.5))
        rng.shuffle(cells)
        placed = 0
        for c in cells:
            if placed >= int(spec["stalls"]):
                break
            # the nearest edge of the square: the stall's back towards it, its front inwards
            best = min(zip(ring, ring[1:], strict=False),
                       key=lambda e: LineString(e).distance(Point(c)))  # fmt: skip
            (ax, az), (bx, bz) = best
            length = math.dist((ax, az), (bx, bz))
            u = ((bx - ax) / length, (bz - az) / length)
            inward = (-u[1], u[0])  # counter-clockwise ring: the inside is on the left
            kind = _pick(rng, spec["pick"])
            w, d = footprint(kind)
            rect = _rect(c, u, w, d)
            if not square.contains(rect):
                continue
            if self.keep.hits(rect) or self.bodies.hits(rect) or self.landmarks.hits(rect):
                continue
            if not self.placed.clear(rect, gap):
                continue
            hs = [self.site.height_at(x, z) for x, z in list(rect.exterior.coords)[:-1]]
            if max(hs) - min(hs) > float(spec["maxSlopeM"]):
                continue
            self.placed.add(rect)
            placed += 1
            self.add(f"market:{placed}", f"MARKT_STAND_{placed}", kind, (c[0], min(hs), c[1]),
                     yaw_quat(*inward))  # fmt: skip
        if placed < int(spec["stalls"]):
            self.plan.counts["market stall (no room)"] += int(spec["stalls"]) - placed

    # --- trees, bushes, grass ------------------------------------------------------------------

    def tree_free(self, p: Pt, kind: str) -> bool:
        from gothar_worldgen.vegetation import crown_radius

        trees = self.rules["trees"]
        trunk = Point(p).buffer(float(trees["trunkClearM"]))
        if self.keep.hits(trunk) or self.bodies.hits(trunk) or self.landmarks.hits(trunk):
            return False
        crown = crown_radius(kind) * float(trees["crownClear"])
        if self.house_shapes.distance(Point(p), crown) < crown:
            return False
        return self.placed.clear(Point(p).buffer(crown * 0.6), 0.0)

    def tree(self, key: str, p: Pt, kind: str, rng: random.Random) -> bool:
        from gothar_worldgen.vegetation import crown_radius

        shift = float(self.rules["trees"]["shiftM"])
        angles = [k * math.pi / 4 for k in range(8)]
        tries = [p] + [(p[0] + r * math.cos(a), p[1] + r * math.sin(a))
                       for r in (shift / 2, shift) for a in angles]  # fmt: skip
        for q in tries:
            if not self.tree_free(q, kind):
                continue
            base = Point(q).buffer(0.4)
            y = self.ground(Polygon(base.exterior.coords))
            if y is None:
                continue
            self.placed.add(Point(q).buffer(crown_radius(kind) * 0.6))
            a = rng.uniform(0, 2 * math.pi)
            self.add(key, _name("BAUM", key.split(":")[-1].upper()), kind, (q[0], y, q[1]),
                     yaw_quat(math.sin(a), math.cos(a)))  # fmt: skip
            return True
        return False

    def in_area(self, p: Pt) -> bool:
        return self.house_shapes.distance(Point(p), 40.0) < 40.0

    def trees(self) -> None:
        trees = self.rules["trees"]
        if not trees.get("osm"):
            return
        kinds = set(trees["gardenKinds"])
        gardens = _Shapes([Polygon(f["polygon"]) for f in self.site.features
                           if f.get("kind") in kinds and f.get("geometry") == "polygon"
                           and len(f["polygon"]) >= 3])  # fmt: skip
        for f in sorted(self.site.features, key=lambda f: str(f.get("osmId"))):
            if f.get("kind") != "tree" or f.get("geometry") != "point":
                continue
            p = (float(f["position"][0]), float(f["position"][1]))
            if not self.in_area(p):
                continue
            rng = random.Random(f"{self.rules['seed']}:tree:{f['osmId']}")
            kind = _pick(rng, trees["garden"] if gardens.hits(Point(p)) else trees["pick"])
            if not self.tree(f"tree:{f['osmId']}", p, kind, rng):
                self.plan.counts["tree (no room)"] += 1

    def yards(self) -> None:
        from gothar_worldgen.vegetation import crown_radius

        yards = self.rules["yards"]
        step = float(yards["gridM"])
        fruit, bush = yards["fruitTree"], yards["bush"]
        x0, z0, x1, z1 = unary_union(list(self.houses.values())).bounds
        trees: list[Pt] = []
        for i in range(math.floor(x0 / step), math.ceil(x1 / step) + 1):
            for j in range(math.floor(z0 / step), math.ceil(z1 / step) + 1):
                rng = random.Random(f"{self.rules['seed']}:yard:{i}:{j}")
                p = (
                    i * step + rng.uniform(-0.4, 0.4) * step,
                    j * step + rng.uniform(-0.4, 0.4) * step,
                )
                q = Point(p)
                to_house = self.house_shapes.distance(q, 8.0)
                if to_house >= 8.0:  # not a yard: fields, squares, outside
                    continue
                gap = self.street_gap(q)
                roll = rng.random()
                apart = float(fruit["apartM"])
                if (roll < float(fruit["chance"]) and to_house >= float(fruit["houseM"])
                        and gap >= float(fruit["streetM"])
                        and all(math.dist(p, t) >= apart for t in trees)):  # fmt: skip
                    if self.tree(f"yard:{i}_{j}", p, "tree_fruit", rng):
                        trees.append(p)
                    continue
                lo, hi = (float(v) for v in bush["houseM"])
                if (
                    roll < float(bush["chance"])
                    and lo <= to_house <= hi
                    and gap >= float(bush["streetM"])
                ):
                    kind = _pick(rng, bush["pick"])
                    r = crown_radius(kind)
                    disc = Point(p).buffer(r * 0.8, quad_segs=4)
                    if self.keep.hits(disc) or self.bodies.hits(disc) or self.landmarks.hits(disc):
                        continue
                    if not self.placed.clear(disc, 0.1):
                        continue
                    y = self.ground(Polygon(disc.exterior.coords))
                    if y is None:
                        continue
                    self.placed.add(disc)
                    a = rng.uniform(0, 2 * math.pi)
                    self.add(
                        f"bush:{i}_{j}",
                        _name("BUSCH", f"{i}_{j}"),
                        kind,
                        (p[0], y - 0.05, p[1]),
                        yaw_quat(math.sin(a), math.cos(a)),
                    )

    def grass(self) -> None:
        g = self.rules["grass"]
        step, off = float(g["stepM"]), float(g["wallM"])
        walls = [(hid, poly) for hid, poly in sorted(self.houses.items())]
        walls += [(f"wall{k}", poly) for k, poly in enumerate(self.walls)]
        for hid, poly in walls:
            parts = getattr(poly, "geoms", [poly])
            for n, part in enumerate(parts):
                ring = list(orient(part, 1.0).exterior.coords)
                for e0, (a, b) in enumerate(zip(ring, ring[1:], strict=False)):
                    e = f"{n}_{e0}" if len(parts) > 1 else str(e0)
                    length = math.dist(a, b)
                    if length < 0.5:
                        continue
                    ux, uz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
                    nx, nz = uz, -ux
                    rng = random.Random(f"{self.rules['seed']}:grass:{hid}:{e}")
                    t = rng.uniform(0.2, step)
                    k = 0
                    while t < length - 0.2:
                        p = (a[0] + ux * t + nx * off, a[1] + uz * t + nz * off)
                        t += step * rng.uniform(0.6, 1.4)
                        k += 1
                        if rng.random() >= float(g["chance"]):
                            continue
                        spot = Point(p).buffer(0.12, quad_segs=2)
                        if (
                            self.keep.hits(spot)
                            or self.bodies.hits(spot)
                            or self.landmarks.hits(spot)
                        ):
                            continue
                        if not self.placed.clear(spot, 0.05):
                            continue
                        kind = _pick(rng, g["pick"])
                        a2 = rng.uniform(0, 2 * math.pi)
                        y = self.site.height_at(*p) - 0.02
                        self.add(f"grass:{hid}:{e}:{k}", _name("GRAS", hid, e, str(k)), kind,
                                 (p[0], y, p[1]), yaw_quat(math.sin(a2), math.cos(a2)))  # fmt: skip


def plan_outdoor(rules: Rules, site: Site) -> Plan:
    """The market stalls first (they need the most room), the props at the walls and the
    street edges, then trees, yards and the grass between them."""
    p = _Planner(rules, site)
    p.market()
    p.props()
    p.kerb()
    p.trees()
    p.yards()
    p.grass()
    return p.plan
