"""Waynet proposal from the street axes (W6, contract docs/modules/world.md "Wegnetz", #135).

``build_waynet`` turns the OSM street axes of the core, the doors of the generated houses and a few
landmarks into the ``waynet`` block of the world:

- **points** (``WP_``) along every walkable axis, about every ``SPACING_M`` and at bends, junctions
  and ends, each moved onto walkable ground clear of collision bodies (or left out);
- **door points** 0.6 m beyond the door probe of the generator (about 1.2 m from the wall),
  each tied to the nearest street point it can reach;
- **edges** between neighbours that pass the walk checks: the line at hip height is free of
  collision bodies and the ground along it is not steeper than the character can walk (stairs
  ways excepted); a blocked edge gets a detour point beside it or is left out (reported);
- **freepoints** (``FP_``) the site data gives: sitting (on the rim) and drinking at the fountains,
  small talk and roaming on the market square, standing at the gates, watering the garden beds
  (``FP_WATER`` is watering plants, world.md); praying (kneeling, facing the portal) and preaching
  in front of the church, preaching on the market fountain's step, begging (sitting, back to a
  wall) beside the church portal, at the market fountain and inside the town gates.

Everything carries ``owner: "worldgen"``. Hand-made entries of the existing block (without owner)
stay; the generator never creates a name a hand-made point already has, but connects that point.
``data/<site>/waynet.json`` removes generated entries for good (``remove``) or adds some (``add``).
"""

from __future__ import annotations

import math
import re
import unicodedata
from collections import defaultdict
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from typing import Any

import numpy as np
import shapely
from shapely.geometry import LineString, MultiLineString, Point, Polygon, box
from shapely.ops import nearest_points
from shapely.strtree import STRtree

OWNER = "worldgen"
SPACING_M = 12.0  # points along an axis at most this far apart
BEND_DEG = 30.0  # an axis vertex turning more than this becomes a point
SNAP_M = 1.0  # axis ends closer than this are one junction
CLEAR_M = 0.2  # points keep the character radius plus this from collision bodies
SHIFT_M = 3.0  # a blocked point may move this far to find free ground
DOOR_OUT_M = 0.6  # door points: this much beyond the generator's door probe
DOOR_REACH_M = 30.0  # a door point connects to a street point at most this far away
DOOR_CANDIDATES = 16
GRADE_SAMPLE_M = 0.5  # the slope check looks at pieces this long
BRIDGE_STEP_M = 0.4  # a bridge between parts climbs at most this much more than its slope allows
BEG_GAP_M = 0.4  # a beggar's foot point this far off the wall behind it (sitting, back to it)
PREACH_GAP_M = 0.4  # the preacher on the fountain's step: this far off the basin
PRAY_FREE_M = 0.6  # a kneeling figure keeps this much free in front of it
PRAY_N = 4  # praying places in a row in front of the church portal
PRAY_OUT_M = 3.4  # ... this far out from the portal, ``PRAY_STEP_M`` apart
PRAY_STEP_M = 1.1
PREACH_OUT_M = 0.9  # the preacher this far in front of the portal, facing the praying row
NPC_SLOPE_DEG = 35.0  # steeper pieces stop a running character (autopilot, W6; begehung "steep")
SKIP_HIGHWAYS = {"motorway", "motorway_link", "trunk", "trunk_link"}
STAIRS = {"steps"}
SITE_PREFIX = {"leonberg": "LEO"}


@dataclass
class Wp:
    name: str
    pos: tuple[float, float, float]
    dir: tuple[float, float, float] | None = None
    owner: str | None = OWNER

    def json(self) -> dict[str, Any]:
        out: dict[str, Any] = {"name": self.name, "pos": list(self.pos)}
        if self.dir is not None:
            out["dir"] = list(self.dir)
        if self.owner:
            out["owner"] = self.owner
        return out


@dataclass
class Result:
    points: list[Wp]
    edges: list[tuple[str, str, bool]]  # a, b, generated
    freepoints: list[Wp]
    report: dict[str, Any] = field(default_factory=dict)

    def block(self) -> dict[str, Any]:
        return {
            "points": [p.json() for p in sorted(self.points, key=lambda p: p.name)],
            "edges": [[a, b, OWNER] if gen else [a, b] for a, b, gen in sorted(self.edges)],
            "freepoints": [p.json() for p in sorted(self.freepoints, key=lambda p: p.name)],
        }


def name_part(text: str) -> str:
    """Upper-case name part from place data: umlauts written out, other characters to ``_``."""
    t = text.upper()
    for a, b in (("Ä", "AE"), ("Ö", "OE"), ("Ü", "UE"), ("ß", "SS"), ("ẞ", "SS")):
        t = t.replace(a, b)
    t = unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode()
    t = re.sub(r"[^A-Z0-9]+", "_", t).strip("_")
    return t or "X"


def building_short(bid: str) -> str:
    """Short form of a LoD2 id for names: the part after the common prefix (``ZnA``, ``ZnA-T1``)."""
    tail = bid.split("_")[-1]
    tail = tail[8:] if len(tail) > 8 and tail.startswith("001000") else tail
    return name_part(tail)


class _Checks:
    """Clearance and walkability against the collision bodies and the heightmap."""

    def __init__(
        self,
        bodies: Sequence[Any],
        height: Callable[[float, float], float],
        radius: float,
        max_slope_deg: float,
    ) -> None:
        self.polys = [b.poly for b in bodies]
        self.owners = [b.owner for b in bodies]
        self.tree = STRtree(self.polys) if self.polys else None
        self.height = height
        self.clear = radius + CLEAR_M
        self.max_grade = math.tan(math.radians(max_slope_deg))

    def free(self, x: float, z: float) -> bool:
        if self.tree is None:
            return True
        p = Point(x, z)
        return not any(
            self.polys[i].distance(p) < self.clear for i in self.tree.query(p.buffer(self.clear))
        )

    def place(
        self, x: float, z: float, across: tuple[float, float] | None
    ) -> tuple[float, float] | None:
        """(x, z) or the nearest free spot within ``SHIFT_M`` (across the axis if given)."""
        if self.free(x, z):
            return (x, z)
        dirs = (
            [across, (-across[0], -across[1])]
            if across
            else [
                (math.cos(a), math.sin(a)) for a in np.linspace(0, 2 * math.pi, 8, endpoint=False)
            ]
        )
        for d in np.arange(0.5, SHIFT_M + 1e-6, 0.5):
            for dx, dz in dirs:  # type: ignore[misc]
                if self.free(x + dx * d, z + dz * d):
                    return (x + dx * d, z + dz * d)
        return None

    def against_wall(
        self,
        spot: tuple[float, float],
        gap: float,
        owner: str = "",
        facing: tuple[tuple[float, float], float] | None = None,
    ) -> tuple[tuple[float, float], tuple[float, float]] | None:
        """The place ``gap`` in front of the body nearest to ``spot`` (whose owner starts with
        ``owner``), facing away from it: nothing else nearer, the ground ahead free. ``facing``
        (direction, least cosine) limits which way the place may face."""
        if self.tree is None:
            return None
        p = Point(spot)
        near = [int(i) for i in self.tree.query(p.buffer(1.5)) if self.owners[i].startswith(owner)]
        if not near:
            return None
        k = min(near, key=lambda i: self.polys[i].distance(p))
        if self.polys[k].contains(p):
            return None
        q = nearest_points(self.polys[k].exterior, p)[0]
        dx, dz = spot[0] - q.x, spot[1] - q.y
        ln = math.hypot(dx, dz)
        if ln < 1e-6:
            return None
        n = (dx / ln, dz / ln)
        if facing is not None and n[0] * facing[0][0] + n[1] * facing[0][1] < facing[1]:
            return None
        at = (q.x + n[0] * gap, q.y + n[1] * gap)
        pa = Point(at)
        if any(self.polys[i].distance(pa) < gap - 0.02 for i in self.tree.query(pa.buffer(gap))):
            return None
        if not self.free(at[0] + n[0] * 1.0, at[1] + n[1] * 1.0):
            return None
        return at, n

    def line_free(self, a: tuple[float, float], b: tuple[float, float]) -> str | None:
        """None if the walk a -> b is fine, else the reason."""
        line = LineString([a, b])
        if self.tree is not None:
            for i in self.tree.query(line):
                if self.polys[i].intersects(line):
                    return f"blocked by {self.owners[i]}"
        return None

    def grade_ok(self, a: tuple[float, float], b: tuple[float, float]) -> bool:
        """No piece of ``GRADE_SAMPLE_M`` along the line steeper than ``self.max_grade``: the
        character gets stuck on short steep kinks long before the controller's slope limit."""
        length = math.dist(a, b)
        n = max(1, math.ceil(length / GRADE_SAMPLE_M))
        hs = [
            self.height(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
            for k in range(n + 1)
        ]
        step = length / n
        return all(abs(hs[k + 1] - hs[k]) <= self.max_grade * step for k in range(n))

    def steepest(self, a: tuple[float, float], b: tuple[float, float]) -> float:
        """Steepest piece of ``GRADE_SAMPLE_M`` along the line, in degrees."""
        length = math.dist(a, b)
        n = max(1, math.ceil(length / GRADE_SAMPLE_M))
        hs = [
            self.height(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
            for k in range(n + 1)
        ]
        rise = max(abs(hs[k + 1] - hs[k]) for k in range(n))
        return math.degrees(math.atan2(rise, length / n))


@dataclass
class _Node:
    key: int
    x: float
    z: float
    street: str
    across: tuple[float, float] | None
    junction: bool = False
    placed: tuple[float, float] | None = None
    name: str = ""


def _streets(
    streets: Sequence[dict[str, Any]], area: Polygon
) -> list[tuple[LineString, str, bool]]:
    out = []
    for s in streets:
        if s.get("highway") in SKIP_HIGHWAYS:
            continue
        if s.get("tunnel") and int(s.get("layer", 0) or 0) < 0:
            continue  # underground garages and passages below houses
        line = LineString(s["points"]).intersection(area)
        for g in getattr(line, "geoms", [line]):
            if isinstance(g, LineString) and g.length > 1.0:
                name = (
                    name_part(s["name"])
                    if s.get("name")
                    else f"WEG_{name_part(str(s.get('osmId', '')))}"
                )
                out.append((g, name, s.get("highway") in STAIRS))
    return out


def build_waynet(
    site: str,
    streets: Sequence[dict[str, Any]],
    buildings: Sequence[dict[str, Any]],
    bodies: Sequence[Any],
    height: Callable[[float, float], float],
    character: Any,  # noqa: ANN401  qa.begehung.Character
    half_extent: float,
    landmarks: dict[str, Any] | None = None,
    existing: dict[str, Any] | None = None,
    annotations: dict[str, Any] | None = None,
    links: Sequence[tuple[str, tuple[float, float], tuple[float, float]]] = (),
    places: Sequence[dict[str, Any]] = (),
) -> Result:
    prefix = f"WP_{SITE_PREFIX.get(site, name_part(site))}"
    fprefix = SITE_PREFIX.get(site, name_part(site))
    checks = _Checks(
        bodies, height, float(character.radius), min(NPC_SLOPE_DEG, float(character.max_slope))
    )
    report: dict[str, Any] = {
        "droppedPoints": [],
        "droppedEdges": [],
        "doorsUnconnected": [],
        "doorsUnusable": [],
        "doorsWithoutAccess": [],
        "usesUnconnected": [],
        "steep": [],
    }

    # --- axes: noded at junctions, sampled -------------------------------------------------------
    area = box(-half_extent, -half_extent, half_extent, half_extent)
    axes = _streets(streets, area)
    noded = shapely.node(MultiLineString([a for a, _, _ in axes]))
    parts = [
        g for g in getattr(noded, "geoms", [noded]) if isinstance(g, LineString) and g.length > 0.2
    ]
    src_tree = STRtree([a for a, _, _ in axes])

    nodes: list[_Node] = []
    by_cell: dict[tuple[int, int], list[int]] = defaultdict(list)

    def node_at(
        x: float, z: float, street: str, across: tuple[float, float] | None, junction: bool
    ) -> int:
        cx, cz = int(math.floor(x / SNAP_M)), int(math.floor(z / SNAP_M))
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for k in by_cell[(cx + dx, cz + dz)]:
                    n = nodes[k]
                    if math.hypot(n.x - x, n.z - z) < SNAP_M:
                        n.junction = n.junction or junction
                        return k
        nodes.append(_Node(len(nodes), x, z, street, across, junction))
        by_cell[(cx, cz)].append(len(nodes) - 1)
        return len(nodes) - 1

    runs: list[tuple[list[int], bool]] = []  # node chain along one part, stairs?
    for part in sorted(parts, key=lambda g: (round(g.coords[0][0], 2), round(g.coords[0][1], 2))):
        mid = part.interpolate(0.5, normalized=True)
        i = int(src_tree.nearest(mid))
        _, street, stairs = axes[i]
        coords = list(part.coords)
        marks = [0.0]
        dist = 0.0
        for k in range(1, len(coords) - 1):  # bends
            dist += math.dist(coords[k - 1], coords[k])
            a = np.subtract(coords[k], coords[k - 1])
            b = np.subtract(coords[k + 1], coords[k])
            na, nb = np.linalg.norm(a), np.linalg.norm(b)
            if na > 1e-6 and nb > 1e-6:
                turn = math.degrees(math.acos(max(-1.0, min(1.0, float(a @ b / (na * nb))))))
                if turn > BEND_DEG:
                    marks.append(dist)
        marks.append(part.length)
        stations = [0.0]
        for s0, s1 in zip(marks, marks[1:], strict=False):
            n = max(1, math.ceil((s1 - s0) / SPACING_M - 1e-9))
            stations += [s0 + (s1 - s0) * k / n for k in range(1, n + 1)]
        chain = []
        for j, s in enumerate(stations):
            p = part.interpolate(s)
            q = part.interpolate(min(part.length, s + 0.5))
            r = part.interpolate(max(0.0, s - 0.5))
            d = np.subtract(q.coords[0], r.coords[0])
            ln = float(np.linalg.norm(d)) or 1.0
            across = (-d[1] / ln, d[0] / ln)
            end = j in (0, len(stations) - 1)
            k = node_at(p.x, p.y, street, None if end else across, end)
            if not chain or chain[-1] != k:
                chain.append(k)
        runs.append((chain, stairs))

    for n in nodes:
        n.placed = checks.place(n.x, n.z, None if n.junction else n.across)
        if n.placed is None:
            report["droppedPoints"].append(
                {
                    "at": [round(n.x, 2), round(n.z, 2)],
                    "street": n.street,
                    "reason": "no free ground within 3 m",
                }
            )

    # names: per street, ordered along a stable key
    per_street: dict[str, list[_Node]] = defaultdict(list)
    for n in nodes:
        if n.placed is not None:
            per_street[n.street].append(n)
    for street, ns in per_street.items():
        ns.sort(key=lambda n: (round(n.x, 1), round(n.z, 1)))
        for k, n in enumerate(ns, start=1):
            n.name = f"{prefix}_{street}_{k:03d}"

    points: dict[str, Wp] = {}

    def wp(name: str, x: float, z: float, d: tuple[float, float] | None = None) -> Wp:
        y = height(x, z)
        dir3 = None
        if d is not None:  # unit length here: the writer keeps near-unit directions as they are
            ln = math.hypot(d[0], d[1]) or 1.0
            dir3 = (d[0] / ln, 0.0, d[1] / ln)
        return Wp(name, (x, y, z), dir3)

    for n in nodes:
        if n.placed is not None:
            points[n.name] = wp(n.name, *n.placed)

    edges: dict[tuple[str, str], bool] = {}
    detours = 0

    def add_edge(a: str, b: str) -> None:
        if a != b:
            edges[(min(a, b), max(a, b))] = True

    def try_edge(na: _Node, nb: _Node, stairs: bool, street: str) -> None:
        nonlocal detours
        a, b = na.placed, nb.placed
        assert a is not None and b is not None
        why = checks.line_free(a, b)
        if why is None and checks.grade_ok(a, b):
            add_edge(na.name, nb.name)
            return
        if why is None:
            why = "too steep"
        # a detour point beside the middle of the line
        mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        dx, dz = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dz) or 1.0
        for off in (1.0, -1.0, 2.0, -2.0):
            c = (mx - dz / ln * off, mz + dx / ln * off)
            if (
                checks.free(*c)
                and checks.line_free(a, c) is None
                and checks.line_free(c, b) is None
                and checks.grade_ok(a, c)
                and checks.grade_ok(c, b)
            ):
                detours += 1
                name = f"{na.name}_U{detours}"
                points[name] = wp(name, *c)
                add_edge(na.name, name)
                add_edge(name, nb.name)
                return
        report["droppedEdges"].append({"a": na.name, "b": nb.name, "reason": why})
        if why == "too steep":
            mid = [round((a[0] + b[0]) / 2, 1), round((a[1] + b[1]) / 2, 1)]
            report["steep"].append({"a": na.name, "b": nb.name, "at": mid,
                                    "maxDeg": round(checks.steepest(a, b), 1),
                                    "street": street, "steps": stairs})  # fmt: skip

    for chain, stairs in runs:
        alive = [nodes[k] for k in chain if nodes[k].placed is not None]
        for na, nb in zip(alive, alive[1:], strict=False):
            try_edge(na, nb, stairs, na.street)

    # --- explicit links (stairs, posterns): both ends placed, joined without the line checks -----
    for hint, a, b in links:
        ends = []
        for j, q in enumerate((a, b), start=1):
            spot = checks.place(q[0], q[1], None)
            if spot is None:
                report["droppedPoints"].append(
                    {
                        "at": [round(q[0], 2), round(q[1], 2)],
                        "link": hint,
                        "reason": "no free ground",
                    }
                )
                break
            name = f"{prefix}_{name_part(hint)}_{j}"
            points[name] = wp(name, *spot)
            ends.append(name)
        if len(ends) == 2:
            add_edge(*ends)

    # --- door points -----------------------------------------------------------------------------
    street_names = [n.name for n in nodes if n.placed is not None]
    doors = 0
    splits = 0

    def street_of(name: str) -> str:
        return name[len(prefix) + 1 :].rsplit("_", 1)[0]

    def connect(name: str, spot: tuple[float, float]) -> bool:
        """Tie a new point to the net: the nearest reachable point, else the nearest reachable
        spot on an edge (the edge is split there)."""
        nonlocal splits
        names = [n for n in points if n != name]
        if not names:
            return False
        xy = np.array([points[n].pos[::2] for n in names])
        dist = np.hypot(xy[:, 0] - spot[0], xy[:, 1] - spot[1])
        for k in np.argsort(dist)[:DOOR_CANDIDATES]:
            if dist[k] > DOOR_REACH_M:
                break
            t = (float(xy[k, 0]), float(xy[k, 1]))
            if checks.line_free(spot, t) is None and checks.grade_ok(spot, t):
                add_edge(name, names[int(k)])
                return True
        best = None
        sp = Point(spot)
        for a, b in list(edges):
            pa, pb = points[a].pos[::2], points[b].pos[::2]
            line = LineString([pa, pb])
            d = line.distance(sp)
            if d > DOOR_REACH_M or (best is not None and d >= best[0]):
                continue
            q = line.interpolate(line.project(sp))
            t = (q.x, q.y)
            if min(math.dist(t, pa), math.dist(t, pb)) < 1.0:
                continue
            if checks.free(*t) and checks.line_free(spot, t) is None and checks.grade_ok(spot, t):
                best = (d, a, b, t)
        if best is None:
            return False
        _, a, b, t = best
        splits += 1
        mid = f"{a}_S{splits}"
        points[mid] = wp(mid, *t)
        del edges[(a, b)]
        add_edge(a, mid)
        add_edge(mid, b)
        add_edge(name, mid)
        return True

    for bld in sorted(buildings, key=lambda b: b["id"]):
        for j, d in enumerate(bld.get("doors", [])):
            x, z, _, kind, nx, nz = d[:6]
            if kind == "blocked":  # no free wall: the house has no usable door (W6)
                report["doorsWithoutAccess"].append(
                    {"building": bld["id"], "at": [round(x, 2), round(z, 2)]}
                )
                continue
            out = (
                DOOR_OUT_M
                + (1.2 if kind == "stairs" else 0.0)
                + (1.0 if kind == "descent" else 0.0)
            )
            spot = None
            for extra in np.arange(0.0, 2.01, 0.5):
                cx, cz = x + nx * (out + extra), z + nz * (out + extra)
                if checks.free(cx, cz):
                    spot = (cx, cz)
                    break
            if spot is None:  # a neighbour or a wall right in front: no usable door
                report["doorsUnusable"].append(
                    {"building": bld["id"], "at": [round(x, 2), round(z, 2)]}
                )
                continue
            near = street_names
            street = "HAUS"
            if near:
                xy = np.array([points[n].pos[::2] for n in near])
                street = street_of(
                    near[int(np.argmin(np.hypot(xy[:, 0] - spot[0], xy[:, 1] - spot[1])))]
                )
            name = f"{prefix}_{street}_{building_short(bld['id'])}" + (f"_{j + 1}" if j else "")
            while name in points:
                name += "_X"
            points[name] = wp(name, spot[0], spot[1], (-nx, -nz))
            doors += 1
            if not connect(name, spot):
                report["doorsUnconnected"].append(
                    {
                        "building": bld["id"],
                        "point": name,
                        "reason": "no reachable point or way within 30 m",
                    }
                )

    # --- routine places of the houses with a use (W7, uses_places.json) ---------------------------
    use_points = 0
    links: list[tuple[str, dict[str, Any]]] = []
    for pl in places:
        if pl.get("kind") != "wp":
            continue
        name = pl["name"]
        while name in points:
            name += "_U"
        spot = (float(pl["pos"][0]), float(pl["pos"][1]))
        points[name] = wp(name, spot[0], spot[1], tuple(pl["dir"]))  # type: ignore[arg-type]
        use_points += 1
        if "y" in pl:  # inside a house: on its floor, not on the terrain under it
            p = points[name]
            points[name] = Wp(name, (p.pos[0], float(pl["y"]), p.pos[2]), p.dir)
        if pl.get("link"):  # through a door: tied to its routine waypoint as it is, once all
            links.append((name, pl))  # are there (a detour comes before the point it leads to)
            continue
        if not connect(name, spot):
            why = "no reachable point or way within 30 m"
            report["usesUnconnected"].append({"point": name, "house": pl.get("house"),
                                              "reason": why})  # fmt: skip
    for name, pl in links:
        if pl["link"] in points:
            add_edge(name, pl["link"])
        else:
            why = f"link {pl['link']} missing"
            report["usesUnconnected"].append({"point": name, "house": pl.get("house"),
                                              "reason": why})  # fmt: skip

    # --- bridges: each island tied to the nearest other part a free, walkable line reaches -----
    # (a point of it, else a spot on one of its edges: doors above a cut street reach the street
    # between its points, where a retaining wall leaves room - W6)
    bridges = 0
    names_all = sorted(points)
    xy_all = np.array([points[n].pos[::2] for n in names_all])
    hopeless: set[frozenset[str]] = set()  # islands no edge of another part is reachable from

    def onto_edge(comp: set[str], cid: dict[str, int], i: int) -> tuple[Any, ...] | None:
        keys = list(edges)
        lines = [LineString([points[a].pos[::2], points[b].pos[::2]]) for a, b in keys]
        tree = STRtree(lines)
        best: tuple[Any, ...] | None = None
        for n in comp:
            q = points[n].pos[::2]
            sp = Point(q)
            for k in tree.query(sp, predicate="dwithin", distance=DOOR_REACH_M):
                a, b = keys[int(k)]
                if cid.get(a, i) == i:
                    continue
                line = lines[int(k)]
                d = line.distance(sp)
                if best is not None and d >= best[0]:
                    continue
                f = line.interpolate(line.project(sp))
                t = (f.x, f.y)
                if min(math.dist(t, points[a].pos[::2]), math.dist(t, points[b].pos[::2])) < 1.0:
                    continue
                if abs(points[n].pos[1] - height(*t)) > BRIDGE_STEP_M + checks.max_grade * d:
                    continue  # another storey: no way up there
                if checks.free(*t) and checks.line_free(q, t) is None and checks.grade_ok(q, t):
                    best = (d, n, a, b, t)
        return best

    for _ in range(200):
        comps = _groups(points, [(a, b, True) for a, b in edges])
        if len(comps) < 2:
            break
        cid = {n: i for i, c in enumerate(comps) for n in c}
        added = False
        for i, comp in enumerate(comps[1:], start=1):
            best = None
            for n in comp:
                q = points[n].pos[::2]
                dist = np.hypot(xy_all[:, 0] - q[0], xy_all[:, 1] - q[1])
                tried: dict[int, int] = defaultdict(int)  # attempts per other part
                for k in np.argsort(dist):
                    if dist[k] > DOOR_REACH_M or (best is not None and dist[k] >= best[0]):
                        break
                    other = names_all[int(k)]
                    j = cid.get(other, i)
                    if j == i or tried[j] >= DOOR_CANDIDATES:
                        continue
                    tried[j] += 1
                    t = (float(xy_all[k, 0]), float(xy_all[k, 1]))
                    rise = abs(points[n].pos[1] - points[other].pos[1])
                    if rise > BRIDGE_STEP_M + checks.max_grade * float(dist[k]):
                        continue  # another storey right above or below: no way up there
                    if checks.line_free(q, t) is None and checks.grade_ok(q, t):
                        best = (float(dist[k]), n, other)
                        break
            if best is not None:
                add_edge(best[1], best[2])
                bridges += 1
                added = True
                break  # the parts changed: group again
            key = frozenset(comp)
            if key in hopeless:
                continue
            split = onto_edge(comp, cid, i)
            if split is None:
                hopeless.add(key)
                continue
            _, n, a, b, t = split
            splits += 1
            mid = f"{a}_S{splits}"
            points[mid] = wp(mid, *t)
            del edges[(a, b)]
            add_edge(a, mid)
            add_edge(mid, b)
            add_edge(n, mid)
            bridges += 1
            added = True
            break
        if not added:
            break

    # --- freepoints ------------------------------------------------------------------------------
    freepoints: dict[str, Wp] = {}
    for fp in _freepoints(fprefix, landmarks or {}):
        x, z = fp.pos[0], fp.pos[2]
        ahead = fp.free_ahead
        if checks.free(x, z) and (
            ahead == 0.0
            or fp.dir is None
            or checks.free(x + fp.dir[0] * ahead, z + fp.dir[2] * ahead)
        ):
            freepoints[fp.name] = Wp(fp.name, (x, height(x, z), z), fp.dir)
        else:
            report["droppedPoints"].append(
                {"at": [round(x, 2), round(z, 2)], "freepoint": fp.name, "reason": "blocked"}
            )
    wp_names = sorted(points)
    wp_xy = np.array([points[n].pos[::2] for n in wp_names]) if wp_names else np.zeros((0, 2))

    def reachable(at: tuple[float, float]) -> bool:
        """A waypoint within reach that a free, walkable line leads to (the way onto the place)."""
        dist = np.hypot(wp_xy[:, 0] - at[0], wp_xy[:, 1] - at[1])
        for k in np.argsort(dist)[:DOOR_CANDIDATES]:
            t = (float(wp_xy[k, 0]), float(wp_xy[k, 1]))
            if (
                dist[k] <= DOOR_REACH_M
                and checks.line_free(at, t) is None
                and checks.grade_ok(at, t)
            ):
                return True
        return False

    for wall in _wall_freepoints(fprefix, landmarks or {}):
        found = None
        for spot in wall.spots:
            got = checks.against_wall(spot, wall.gap, wall.owner, wall.facing)
            if got is not None and reachable(got[0]):
                found = got
                break
        if found is None:
            at = [round(c, 2) for c in wall.spots[0]]
            why = {"at": at, "freepoint": wall.name, "reason": "no free wall"}
            report["droppedPoints"].append(why)
            continue
        (x, z), (nx, nz) = found
        y = height(x, z) if wall.y is None else wall.y
        freepoints[wall.name] = Wp(wall.name, (x, y, z), (nx, 0.0, nz))

    for pl in places:  # their freepoints (no edges, as every freepoint)
        if pl.get("kind") == "fp":
            x, z = float(pl["pos"][0]), float(pl["pos"][1])
            d = (float(pl["dir"][0]), 0.0, float(pl["dir"][1]))
            y = float(pl["y"]) if "y" in pl else height(x, z)  # inside: the room's floor
            freepoints[pl["name"]] = Wp(pl["name"], (x, y, z), d)

    # --- annotations and hand-made entries -------------------------------------------------------
    ann = annotations or {}
    removed = set(ann.get("remove", []))
    for name in removed:
        points.pop(name, None)
        freepoints.pop(name, None)
    edges = {k: v for k, v in edges.items() if k[0] not in removed and k[1] not in removed}
    add = ann.get("add", {})
    for p in add.get("points", []):
        points[p["name"]] = Wp(
            p["name"], tuple(p["pos"]), tuple(p["dir"]) if p.get("dir") else None
        )  # type: ignore[arg-type]
    for p in add.get("freepoints", []):
        freepoints[p["name"]] = Wp(
            p["name"], tuple(p["pos"]), tuple(p["dir"]) if p.get("dir") else None
        )  # type: ignore[arg-type]
    for a, b in add.get("edges", []):
        add_edge(a, b)

    hand_points = [
        Wp(p["name"], tuple(p["pos"]), tuple(p["dir"]) if p.get("dir") else None, None)  # type: ignore[arg-type]
        for p in (existing or {}).get("points", [])
        if p.get("owner") != OWNER
    ]
    hand_free = [
        Wp(p["name"], tuple(p["pos"]), tuple(p["dir"]) if p.get("dir") else None, None)  # type: ignore[arg-type]
        for p in (existing or {}).get("freepoints", [])
        if p.get("owner") != OWNER
    ]
    hand_edges = [
        (min(e[0], e[1]), max(e[0], e[1]))
        for e in (existing or {}).get("edges", [])
        if len(e) < 3 or e[2] != OWNER
    ]
    for p in hand_points:  # the hand-made point wins; generated edges to its name stay
        points[p.name] = p
    for p in hand_free:
        freepoints[p.name] = p
    all_edges: list[tuple[str, str, bool]] = [
        (a, b, True) for (a, b) in edges if (a, b) not in set(hand_edges)
    ]
    all_edges += [(a, b, False) for a, b in hand_edges]
    all_edges = [e for e in all_edges if e[0] in points and e[1] in points]

    report.update(_components(points, all_edges, report))
    report.update(
        {
            "points": len(points),
            "doorPoints": doors,
            "edges": len(all_edges),
            "freepoints": len(freepoints),
            "detours": detours,
            "splits": splits,
            "usePoints": use_points,
            "bridges": bridges,
            "handPoints": len(hand_points),
            "removed": sorted(removed),
        }
    )
    return Result(list(points.values()), all_edges, list(freepoints.values()), report)


def _groups(points: dict[str, Wp], edges: Sequence[tuple[str, str, bool]]) -> list[list[str]]:
    """Connected groups of points, biggest first."""
    parent = {n: n for n in points}

    def find(n: str) -> str:
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n

    for a, b, _ in edges:
        if a in parent and b in parent:
            ra, rb = find(a), find(b)
            if ra != rb:
                parent[ra] = rb
    groups: dict[str, list[str]] = defaultdict(list)
    for n in points:
        groups[find(n)].append(n)
    return sorted(groups.values(), key=lambda g: (-len(g), min(g)))


def _components(
    points: dict[str, Wp], edges: Sequence[tuple[str, str, bool]], report: dict[str, Any]
) -> dict[str, Any]:
    parent = {n: n for n in points}

    def find(n: str) -> str:
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n

    for a, b, _ in edges:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    groups: dict[str, list[str]] = defaultdict(list)
    for n in points:
        groups[find(n)].append(n)
    comps = sorted(groups.values(), key=len, reverse=True)
    main = len(comps[0]) if comps else 0
    dropped = defaultdict(list)
    for e in report.get("droppedEdges", []):
        dropped[e["a"]].append(e["reason"])
        dropped[e["b"]].append(e["reason"])
    unconnected = {
        d.get("point"): d["reason"] for d in report.get("doorsUnconnected", []) if d.get("point")
    }
    islands = []
    for c in comps[1:]:
        reasons = sorted(
            {r for n in c for r in dropped.get(n, [])}
            | {unconnected[n] for n in c if n in unconnected}
        )
        islands.append(
            {
                "size": len(c),
                "points": sorted(c)[:6],
                "reasons": reasons or ["axis ends without a connection"],
            }
        )
    return {
        "components": len(comps),
        "mainShare": round(main / max(1, len(points)), 4),
        "islands": islands,
    }


@dataclass
class _Fp:
    name: str
    pos: tuple[float, float, float]
    dir: tuple[float, float, float] | None
    free_ahead: float = 0.0  # this much ground in front of it stays free too (kneeling)


@dataclass
class _WallFp:
    """A freepoint with a wall right behind it: the first of ``spots`` that finds one."""

    name: str
    spots: list[tuple[float, float]]
    gap: float
    owner: str = ""  # only bodies whose owner starts with this
    facing: tuple[tuple[float, float], float] | None = None
    y: float | None = None  # its height where not the terrain's (the fountain's step)


def _ring(
    name: str, centre: tuple[float, float], r: float, n: int, outward: bool, start: float = 0.0
) -> list[_Fp]:
    out = []
    for k in range(n):
        a = start + 2 * math.pi * k / n
        ux, uz = math.cos(a), math.sin(a)
        d = (ux, 0.0, uz) if outward else (-ux, 0.0, -uz)
        out.append(_Fp(f"{name}_{k + 1:02d}", (centre[0] + ux * r, 0.0, centre[1] + uz * r), d))
    return out


def _freepoints(site: str, lm: dict[str, Any]) -> list[_Fp]:
    """Freepoints from the landmarks: fountains (sit on the rim facing out, drink facing in), the
    market square (small talk in pairs, roaming), the gates (stand, facing out of town)."""
    out: list[_Fp] = []
    for key, (x, z, r) in sorted(lm.get("fountains", {}).items()):
        tag = name_part(key)
        out += _ring(f"FP_SIT_{site}_{tag}", (x, z), r + 0.35, 6, True, 0.3)
        out += _ring(f"FP_DRINK_{site}_{tag}", (x, z), r + 0.5, 1, False, math.pi / 2)
    if "square" in lm:
        (x, z), r = lm["square"]
        for k in range(3):  # pairs facing each other
            a = 2 * math.pi * k / 3 + 0.5
            cx, cz = x + math.cos(a) * r, z + math.sin(a) * r
            tx, tz = -math.sin(a), math.cos(a)  # across the ring
            for j, sgn in enumerate((1.0, -1.0), start=1):
                out.append(
                    _Fp(
                        f"FP_SMALLTALK_{site}_MARKT_{k + 1:02d}_{j}",
                        (cx + tx * 0.7 * sgn, 0.0, cz + tz * 0.7 * sgn),
                        (-tx * sgn, 0.0, -tz * sgn),
                    )
                )
        out += _ring(f"FP_ROAM_{site}_MARKT", (x, z), r * 1.5, 4, True, 0.2)
    for k, (x, z) in enumerate(lm.get("beds", []), start=1):  # watering the garden beds
        out.append(_Fp(f"FP_WATER_{site}_GARTEN_{k:02d}", (x, 0.0, z), None))
    for key, (x, z) in sorted(lm.get("gates", {}).items()):
        cx, cz = lm.get("centre", (0.0, 0.0))
        d = np.array([x - cx, z - cz])
        d = d / (np.linalg.norm(d) or 1.0)
        p = (x - d[0] * 4.0, 0.0, z - d[1] * 4.0)  # inside the gate, facing out of town
        out.append(_Fp(f"FP_STAND_{site}_{name_part(key)}", p, (float(d[0]), 0.0, float(d[1]))))
    if "church" in lm:  # praying in a row facing the portal, the preacher before it facing them
        (px, pz), (nx, nz) = lm["church"]["portal"], lm["church"]["normal"]
        tx, tz = -nz, nx
        at = (px + nx * PREACH_OUT_M, 0.0, pz + nz * PREACH_OUT_M)
        out.append(_Fp(f"FP_PREACH_{site}_KIRCHE_01", at, (nx, 0.0, nz)))
        for k in range(PRAY_N):
            s = (k - (PRAY_N - 1) / 2) * PRAY_STEP_M
            p = (px + nx * PRAY_OUT_M + tx * s, 0.0, pz + nz * PRAY_OUT_M + tz * s)
            out.append(_Fp(f"FP_PRAY_{site}_KIRCHE_{k + 1:02d}", p, (-nx, 0.0, -nz), PRAY_FREE_M))
    return out


FOUNTAIN_BEG_A = 0.3 + math.pi / 6 + math.pi  # between two rim seats, across from the preacher
FOUNTAIN_PREACH_A = 0.3 + math.pi / 6


def _wall_freepoints(site: str, lm: dict[str, Any]) -> list[_WallFp]:
    """Places with a wall right behind them: beggars (sitting, back to the wall) beside the church
    portal, on the market fountain's step and inside the town gates; the preacher on the
    fountain's step, the basin behind him."""
    out: list[_WallFp] = []
    if "church" in lm:
        out.append(_WallFp(f"FP_BEG_{site}_KIRCHE_01", [lm["church"]["beg"]], BEG_GAP_M))
    if "marktbrunnen" in lm.get("fountains", {}) and "fountainStep" in lm:
        x, z, _ = lm["fountains"]["marktbrunnen"]
        y, r = lm["fountainStep"]
        for kind, a in (("BEG", FOUNTAIN_BEG_A), ("PREACH", FOUNTAIN_PREACH_A)):
            spot = (x + math.cos(a) * r, z + math.sin(a) * r)
            gap = BEG_GAP_M if kind == "BEG" else PREACH_GAP_M
            out.append(_WallFp(f"FP_{kind}_{site}_MARKTBRUNNEN_01", [spot], gap,
                               "HANDMADE_MARKTBRUNNEN", y=y))  # fmt: skip
    cx, cz = lm.get("centre", (0.0, 0.0))
    for key in sorted(lm.get("towers", [])):
        x, z = lm["gates"][key]
        d = np.array([cx - x, cz - z])
        d = d / (np.linalg.norm(d) or 1.0)  # into the town
        t = (-d[1], d[0])
        spots = [(x + d[0] * s + t[0] * a, z + d[1] * s + t[1] * a)
                 for a in (2.5, -2.5, 3.5, -3.5, 4.5, -4.5) for s in (4.5, 5.5, 6.5)]  # fmt: skip
        out.append(_WallFp(f"FP_BEG_{site}_{name_part(key)}_01",
                           [(float(p), float(q)) for p, q in spots], BEG_GAP_M, "CITYWALL_",
                           ((float(d[0]), float(d[1])), 0.5)))  # fmt: skip
    return out
