"""Vegetation on areas (W6 vegetation): gardens and other green land of OSM filled after rules,
the formal garden (Pomeranzengarten) with clipped hedges along its paths. Rules are content:
``data/<site>/vegetation.json``; the masks (lanes, paths, door lanes, waynet edges, freepoints,
houses, handmade models) are those of the lanes' planner (``outdoor._Planner``).

A layer fills a polygon on a jittered grid: each cell tries one thing of ``pick`` with
``chance``; trees keep their crowns off houses (as the lanes' trees), bushes and grass patches
keep their disc clear of every mask and of what stands already. Keys and names are stable
(``veg:<area>:<layer>:<i>:<j>``), the seed fixed: the world stays byte-equal across runs.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

from shapely.geometry import LineString, Point, Polygon
from shapely.ops import unary_union

from gothar_worldgen.outdoor import OutdoorError

if TYPE_CHECKING:
    from gothar_worldgen.outdoor import _Planner

FORMAT_VERSION = 1
TREE_KINDS = ("tree_",)


class VegetationError(OutdoorError):
    pass


@dataclass(frozen=True)
class VegRules:
    data: dict[str, Any]

    @classmethod
    def load(cls, path: Path) -> VegRules:
        from gothar_worldgen.vegetation import VEGETATION

        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            raise VegetationError(f"{path.name}: {e}") from e
        if doc.get("version") != FORMAT_VERSION:
            raise VegetationError(f"{path.name}: version {doc.get('version')} != {FORMAT_VERSION}")
        for kind in _kinds(doc):
            if kind not in VEGETATION:
                raise VegetationError(f"{path.name}: unknown model {kind!r}")
        return cls(doc)


def _kinds(doc: dict[str, Any]) -> set[str]:
    out: set[str] = set()
    for area in doc.get("areas", []):
        for layer in area.get("layers", []):
            out |= set(layer.get("pick", {}))
    for key in ("slopes", "wall", "banks"):
        for layer in (doc.get(key) or {}).get("layers", []):
            out |= set(layer.get("pick", {}))
    for cls in (doc.get("umland") or {}).get("classes", []):
        for layer in [*cls.get("layers", []), *(cls.get("farLayers") or [])]:
            out |= set(layer.get("pick", {}))
    formal = doc.get("formal")
    if formal:
        out.add(formal["hedge"]["kind"])
        for layer in formal.get("beds", []):
            out |= set(layer.get("pick", {}))
    return out


def _pick(rng: random.Random, weights: dict[str, float]) -> str:
    kinds = sorted(weights)
    return rng.choices(kinds, [weights[k] for k in kinds])[0]


def _yaw(rng: random.Random) -> list[float]:
    from gothar_worldgen.outdoor import yaw_quat

    a = rng.uniform(0, 2 * math.pi)
    return yaw_quat(math.sin(a), math.cos(a))


class _Areas:
    def __init__(self, planner: _Planner, rules: VegRules) -> None:
        self.p = planner
        self.rules = rules.data
        self.seed = str(self.rules.get("seed", 7))
        self.reach = float(self.rules.get("reachM", 100.0))
        # the old town: inside the town wall (its bodies' hull), and ``reachM`` round it
        core = planner.walls or list(planner.houses.values())
        self.town = unary_union(core).convex_hull.buffer(self.reach)

    def free(self, disc: Polygon, landmarks: bool = True) -> bool:
        p = self.p
        if p.keep.hits(disc) or p.bodies.hits(disc) or p.surfaces.hits(disc):
            return False
        if landmarks and p.landmarks.hits(disc):
            return False
        return p.house_shapes.distance(disc, 1.0) >= 1.0 and p.placed.clear(disc, 0.1)

    def put(self, key: str, name: str, kind: str, at: tuple[float, float], rng: random.Random,
            landmarks: bool = True, slope: float | None = None) -> bool:  # fmt: skip
        from gothar_worldgen.vegetation import crown_radius

        r = crown_radius(kind)
        tree = kind.startswith(TREE_KINDS)
        disc = Point(at).buffer((0.6 if tree else 0.8) * r, quad_segs=4)
        trunk = Point(at).buffer(0.5, quad_segs=4)
        if not self.free(trunk if tree else disc, landmarks):
            return False
        if tree:
            if self.p.house_shapes.distance(Point(at), r) < r * 0.75:
                return False
            if not self.p.placed.clear(disc, 0.0):
                return False
        foot = Polygon(Point(at).buffer(0.4 if tree else min(r, 1.0), quad_segs=2).exterior.coords)
        hs = [self.p.site.height_at(x, z) for x, z in list(foot.exterior.coords)[:-1]]
        limit = self.p.max_slope if slope is None else slope
        if max(hs) - min(hs) > limit:
            return False
        self.p.placed.add(disc)
        self.p.add(key, name, kind, (at[0], min(hs) - 0.03, at[1]), _yaw(rng))
        return True

    def fill(self, area_id: str, poly: Any, layers: list[dict[str, Any]],  # noqa: ANN401
             landmarks: bool = True) -> int:  # fmt: skip
        """Each layer on its own jittered grid inside ``poly``."""
        n = 0
        x0, z0, x1, z1 = poly.bounds
        for li, layer in enumerate(layers):
            step = float(layer["stepM"])
            inner = poly.buffer(-float(layer.get("edgeM", 0.5)))
            if inner.is_empty:
                continue
            for i in range(math.floor(x0 / step), math.ceil(x1 / step) + 1):
                for j in range(math.floor(z0 / step), math.ceil(z1 / step) + 1):
                    rng = random.Random(f"{self.seed}:{area_id}:{li}:{i}:{j}")
                    at = (i * step + rng.uniform(-0.45, 0.45) * step,
                          j * step + rng.uniform(-0.45, 0.45) * step)  # fmt: skip
                    if rng.random() >= float(layer["chance"]) or not inner.contains(Point(at)):
                        continue
                    if not self.slope_fits(at, layer):
                        continue
                    kind = _pick(rng, layer["pick"])
                    key = f"veg:{area_id}:{li}:{i}:{j}"
                    name = f"VEG_{area_id.upper()}_{li}_{i}_{j}".replace("-", "M")
                    if self.put(key, name, kind, at, rng, landmarks, layer.get("slopeM")):
                        n += 1
        return n

    def slope_deg(self, at: tuple[float, float]) -> float:
        """The ground's slope at ``at`` (degrees), over a metre each way."""
        h = self.p.site.height_at
        x, z = at
        gx = (h(x + 1.0, z) - h(x - 1.0, z)) / 2.0
        gz = (h(x, z + 1.0) - h(x, z - 1.0)) / 2.0
        return math.degrees(math.atan(math.hypot(gx, gz)))

    def slope_fits(self, at: tuple[float, float], layer: dict[str, Any]) -> bool:
        if "minSlopeDeg" not in layer and "maxSlopeDeg" not in layer:
            return True
        d = self.slope_deg(at)
        return float(layer.get("minSlopeDeg", -1.0)) <= d < float(layer.get("maxSlopeDeg", 91.0))

    def slopes(self) -> None:
        """Scrub and grass on the steep ground round the old town (by the heightmap's slope)."""
        spec = self.rules.get("slopes")
        if spec:
            self.p.plan.counts["slopes"] += self.fill("hang", self.town, spec["layers"])

    def wall_band(self) -> None:
        """A band outside the town wall, ``fromM``..``toM`` off it: grass and scrub (the wall
        path along its foot stays free)."""
        spec = self.rules.get("wall")
        if not spec or not self.p.walls:
            return
        hull = unary_union(self.p.walls).convex_hull
        band = hull.buffer(float(spec["toM"])).difference(hull.buffer(float(spec["fromM"])))
        self.p.plan.counts["wall band"] += self.fill("mauer", band, spec["layers"])

    def banks(self) -> None:
        """Reed and bushes along a river, ``fromM``..``toM`` off its axis, within ``reachM`` of
        the town wall."""
        spec = self.rules.get("banks")
        if not spec:
            return
        names = set(spec["rivers"])
        lines = [LineString(f["points"]) for f in sorted(self.p.site.features,
                                                         key=lambda f: str(f.get("osmId")))
                 if f.get("geometry") == "line" and f.get("name") in names
                 and len(f.get("points") or []) >= 2]  # fmt: skip
        if not lines:
            return
        axis = unary_union(lines)
        core = self.p.walls or list(self.p.houses.values())
        near = unary_union(core).convex_hull.buffer(float(spec["reachM"]))
        band = axis.buffer(float(spec["toM"])).difference(axis.buffer(float(spec["fromM"])))
        self.p.plan.counts["banks"] += self.fill("ufer", band.intersection(near), spec["layers"])

    def areas(self) -> None:
        feats = sorted(self.p.site.features, key=lambda f: str(f.get("osmId")))
        for area in self.rules.get("areas", []):
            kinds = set(area["kinds"])
            for f in feats:
                if f.get("kind") not in kinds or f.get("geometry") != "polygon":
                    continue
                if f.get("name") and f.get("name") == (self.rules.get("formal") or {}).get("name"):
                    continue  # the formal garden has its own rules
                poly = Polygon(f["polygon"], f.get("holes") or []).buffer(0)
                poly = poly.intersection(self.town)
                if poly.is_empty or poly.area < float(area.get("minM2", 20.0)):
                    continue
                self.p.plan.counts[f"area {area['name']}"] += self.fill(
                    f"{area['name']}_{f['osmId']}", poly, area["layers"]
                )

    def umland(self) -> None:
        """The surroundings up to ``reachM`` round the town wall (decision of the project owner
        2026-11-07: medieval): each class of OSM land between ``innerM`` and ``reachM`` after its
        layers, beyond ``farM`` after its far layers (a wood seen from the town as clumps of
        trees). Built-up land (housing, trade) becomes orchard meadow or meadow."""
        spec = self.rules.get("umland")
        if not spec:
            return
        core = self.p.walls or list(self.p.houses.values())
        hull = unary_union(core).convex_hull
        reach = hull.buffer(float(spec["reachM"]))
        feats = sorted(self.p.site.features, key=lambda f: str(f.get("osmId")))
        for cls in spec["classes"]:
            kinds = set(cls["kinds"])
            ring = reach.difference(hull.buffer(float(cls["innerM"])))
            far_m = cls.get("farM")
            near = ring.intersection(hull.buffer(float(far_m))) if far_m else ring
            far = ring.difference(hull.buffer(float(far_m))) if far_m else None
            for f in feats:
                if f.get("kind") not in kinds or f.get("geometry") != "polygon":
                    continue
                poly = Polygon(f["polygon"], f.get("holes") or []).buffer(0)
                for part, layers, tag in ((near, cls["layers"], ""),
                                          (far, cls.get("farLayers"), "F")):  # fmt: skip
                    if part is None or not layers:
                        continue
                    piece = poly.intersection(part)
                    if piece.is_empty or piece.area < float(cls.get("minM2", 50.0)):
                        continue
                    self.p.plan.counts[f"umland {cls['name']}"] += self.fill(
                        f"{cls['name']}{tag}_{f['osmId']}", piece, layers
                    )

    def formal(self) -> None:
        """Hedges along both sides of the paths inside the formal garden, end to end with gaps
        where paths cross; the beds between them after their layers."""
        spec = self.rules.get("formal")
        if not spec:
            return
        area = next((f for f in self.p.site.features if f.get("name") == spec["name"]
                     and f.get("geometry") == "polygon"), None)  # fmt: skip
        if area is None:
            self.p.plan.failed.append({"what": "formal garden", "reason": "not in features"})
            return
        garden = Polygon(area["polygon"]).buffer(0)
        hedge = spec["hedge"]
        from gothar_worldgen.vegetation import HEDGE_L

        paths = []
        for line, width, _, sid in self.p.streets:
            piece = line.intersection(garden)
            for g in getattr(piece, "geoms", [piece]):
                if isinstance(g, LineString) and g.length > 2.0:
                    paths.append((g, width, sid))
        lanes = unary_union([g.buffer(w / 2) for g, w, _ in paths]) if paths else Point(0, 0)
        crossing = float(hedge["crossingM"])
        n = 0
        for g, width, sid in sorted(paths, key=lambda q: q[2]):
            off = width / 2 + float(hedge["offsetM"])
            for side in (1.0, -1.0):
                rows = g.parallel_offset(off, "left" if side > 0 else "right", join_style=2)
                for ri, r in enumerate(getattr(rows, "geoms", [rows])):
                    if not isinstance(r, LineString) or r.length < HEDGE_L:
                        continue
                    run: list[tuple[int, Any, Any, Any, Polygon, list[float]]] = []
                    t, m = HEDGE_L / 2, 0
                    while t <= r.length - HEDGE_L / 2 + 1e-9:
                        a, b = r.interpolate(t - HEDGE_L / 2), r.interpolate(t + HEDGE_L / 2)
                        c = r.interpolate(t)
                        t += HEDGE_L + float(hedge["gapM"])
                        m += 1
                        piece = LineString([a, b])
                        ok = not piece.buffer(crossing).intersects(
                            lanes.difference(g.buffer(width / 2 + 0.05))
                        )  # a crossing path: an opening in the hedge
                        ok = ok and garden.buffer(-0.3).contains(piece)
                        disc = piece.buffer(0.35, cap_style=2)
                        ok = ok and not self.p.bodies.hits(disc)
                        ok = ok and not self.p.keep.hits(disc.buffer(-0.05))
                        h = [self.p.site.height_at(q.x, q.y) for q in (a, b)]
                        if ok and abs(h[0] - h[1]) <= self.p.max_slope:
                            run.append((m, a, b, c, disc, h))
                            continue
                        n += self._hedges(run, sid, side, ri, hedge)
                        run = []
                    n += self._hedges(run, sid, side, ri, hedge)
        self.p.plan.counts["formal hedges"] += n
        beds = garden.difference(lanes.buffer(float(hedge["offsetM"]) + 0.6))
        self.p.plan.counts["formal beds"] += self.fill(
            "formal", beds, spec.get("beds", []), landmarks=False
        )

    def _hedges(self, run: list[Any], sid: str, side: float, ri: int,
                hedge: dict[str, Any]) -> int:  # fmt: skip
        """A run of hedge pieces end to end; shorter runs than ``minRun`` are left out (a lone
        piece of hedge beside a path looks lost)."""
        from gothar_worldgen.outdoor import yaw_quat
        from gothar_worldgen.vegetation import HEDGE_L

        if len(run) < int(hedge.get("minRun", 3)):
            return 0
        tag = f"{'L' if side > 0 else 'R'}{ri}"
        for m, a, b, c, disc, h in run:
            ux, uz = (b.x - a.x) / HEDGE_L, (b.y - a.y) / HEDGE_L
            self.p.placed.add(disc)
            self.p.add(f"veg:formal:{sid}:{tag}:{m}", f"HECKE_{sid}_{tag}_{m}".upper(),
                       hedge["kind"], (c.x, min(h) - 0.02, c.y), yaw_quat(-uz, ux))  # fmt: skip
        return len(run)


def plan_vegetation(planner: _Planner, rules: VegRules) -> None:
    """After the lanes (props, trees, yards, grass): the formal garden, the areas, then the
    river banks, the band outside the town wall and the steep slopes."""
    a = _Areas(planner, rules)
    a.formal()
    a.areas()
    a.banks()
    a.wall_band()
    a.slopes()
    a.umland()
