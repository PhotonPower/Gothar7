"""Country roads and field ways outside the town wall (W6, plan approved 2026-11-08).

Outside the wall the modern street network of OSM gives way to a few ways that fit the hamlets
(``data/<site>/rural_ways.json``):

- **Landstraße:** the roads out of the gates: country road highways (``landstrasse.highways``,
  e.g. secondary/tertiary) and the streets (``gateHighways``) that start at a gate tower (within
  ``gateM``). They narrow to ``widthM`` with a gravel surface.
- **Feldweg:** tracks and paths (``feldweg.highways``), and the settlement streets
  (``feldweg.named``, e.g. residential) whose name runs at least ``minNamedM`` outside the wall
  within ``reachM`` of it (the surroundings of the hamlets). ``widthM`` wide, of mud, without
  gutters or retaining walls.
- everything else outside (service roads, footways, steps, cycleways, motorways ...) is left out,
  except the shortest runs that keep things reachable (access field ways, ``"access": true``):
  every kept way joins the town (at most ``maxLinkM`` of dropped way; a kept way that cannot is
  left out), and every target (doors, starts, posterns) without a kept way within ``accessM``
  gets the nearest dropped way within ``accessM`` and its link. Links are made within
  ``linkHalfM`` of the origin (the waynet's area, the core); beyond it the ways stay as sorted.

Inside the ring (plus ``insideM``) every street stays as it is; a street crossing the ring is cut
there. Minor ways (``belt.highways``: footways, paths, steps, lanes) that lie wholly within
``belt.m`` of the ring stay too: the ways about the wall, the castle garden and the Zwinger.
A rural piece carries ``"rural": "landstrasse" | "feldweg"`` and the highway ``track`` (class
``track``), its OSM highway as ``osmHighway``. ``load_street_doc`` is what the readers of
``streets.json`` in the generator use; the rules off (or missing) it returns the file unchanged.
"""

from __future__ import annotations

import heapq
import json
import math
from collections import defaultdict
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import shapely
from shapely import STRtree
from shapely.geometry import LineString, Point, Polygon, box

RULES_FILE = "rural_ways.json"
KINDS = ("landstrasse", "feldweg")
NO_ACCESS = {"motorway", "motorway_link", "trunk", "trunk_link", "steps"}  # never a link

Node = tuple[float, float]


def _lines(geom: shapely.Geometry) -> list[LineString]:
    if isinstance(geom, LineString):
        return [geom] if not geom.is_empty else []
    return [g for g in getattr(geom, "geoms", []) if isinstance(g, LineString) and not g.is_empty]


def _points(line: LineString) -> list[list[float]]:
    return [[round(float(x), 3), round(float(z), 3)] for x, z in line.coords]


def _key(x: float, z: float) -> Node:
    return (round(float(x), 2), round(float(z), 2))


class _Net:
    """The way graph: vertices of all centre lines, joined where OSM ways share a node. Kept
    lines cost nothing, dropped ones their length (the search finds the shortest addition)."""

    def __init__(self) -> None:
        self.adj: dict[Node, list[tuple[Node, float, int]]] = defaultdict(list)
        self.edges: list[tuple[int, int]] = []  # (line, segment) per edge
        self.lines: list[LineString] = []
        self.kept: list[bool] = []
        self.linkable: list[bool] = []
        self.marked: set[tuple[int, int]] = set()  # segments of dropped lines that stay
        self.isolated: set[int] = set()  # kept lines that cannot join the town

    def add(self, line: LineString, kept: bool, linkable: bool = False) -> int:
        n = len(self.lines)
        self.lines.append(line)
        self.kept.append(kept)
        self.linkable.append(linkable)
        if not kept and not linkable:
            return n
        c = list(line.coords)
        for i in range(len(c) - 1):
            a, b = _key(*c[i]), _key(*c[i + 1])
            e = len(self.edges)
            self.edges.append((n, i))
            w = 0.0 if kept else math.dist(c[i], c[i + 1])
            self.adj[a].append((b, w, e))
            self.adj[b].append((a, w, e))
        return n

    def nodes(self, n: int) -> list[Node]:
        return [_key(*p) for p in self.lines[n].coords]

    def search(self, sources: set[Node]) -> tuple[dict[Node, float], dict[Node, tuple[Node, int]]]:
        """Dijkstra from ``sources``: (distance, (previous node, edge))."""
        dist = dict.fromkeys(sources, 0.0)
        prev: dict[Node, tuple[Node, int]] = {}
        heap = [(0.0, k) for k in sorted(sources)]
        heapq.heapify(heap)
        while heap:
            d, u = heapq.heappop(heap)
            if d > dist.get(u, math.inf):
                continue
            for v, w, e in self.adj[u]:
                nd = d + w
                if nd < dist.get(v, math.inf):
                    dist[v] = nd
                    prev[v] = (u, e)
                    heapq.heappush(heap, (nd, v))
        return dist, prev

    def link(
        self, node: Node, dist: dict[Node, float], prev: dict[Node, tuple[Node, int]]
    ) -> set[Node]:
        """Marks the dropped segments on the way back from ``node``; returns the path's nodes."""
        path, u = {node}, node
        while dist.get(u, 0.0) > 0.0 and u in prev:
            v, e = prev[u]
            n, i = self.edges[e]
            if not self.kept[n]:
                self.marked.add((n, i))
            path.add(v)
            u = v
        return path

    def components(self) -> list[tuple[set[Node], set[int]]]:
        """(nodes, lines) per component of the kept lines."""
        owner: dict[Node, list[int]] = defaultdict(list)
        for n in range(len(self.lines)):
            if self.kept[n]:
                for k in self.nodes(n):
                    owner[k].append(n)
        seen: set[int] = set()
        out = []
        for start in range(len(self.lines)):
            if not self.kept[start] or start in seen:
                continue
            nodes, lines, stack = set(), {start}, [start]
            seen.add(start)
            while stack:
                n = stack.pop()
                for k in self.nodes(n):
                    nodes.add(k)
                    for m in owner[k]:
                        if m not in seen:
                            seen.add(m)
                            lines.add(m)
                            stack.append(m)
            out.append((nodes, lines))
        return out


def _connect(
    net: _Net, town_lines: set[int], targets: Sequence[tuple[float, float]], access_m: float,
    max_link_m: float,
) -> dict[str, Any]:  # fmt: skip
    stats: dict[str, Any] = {"links": 0, "linkM": 0.0, "isolated": 0, "targets": 0}
    main: set[Node] = set()
    islands = []
    for nodes, lines in net.components():
        if lines & town_lines:
            main |= nodes
        else:
            islands.append((nodes, lines))
    while islands:  # the nearest island first; each one joined becomes part of the town
        dist, prev = net.search(main)
        k = min(
            range(len(islands)), key=lambda j: min(dist.get(x, math.inf) for x in islands[j][0])
        )
        nodes, lines = islands.pop(k)
        node = min(sorted(nodes), key=lambda x: dist.get(x, math.inf))
        if dist.get(node, math.inf) > max_link_m:  # none nearer: no other one can join either
            for _, rest in [(nodes, lines), *islands]:
                net.isolated |= rest
            stats["isolated"] += 1 + len(islands)
            break
        stats["links"] += 1
        stats["linkM"] += dist[node]
        main |= nodes | net.link(node, dist, prev)
    kept = [n for n in range(len(net.lines)) if net.kept[n] and n not in net.isolated]
    cand = [n for n in range(len(net.lines)) if net.linkable[n]]
    kept_tree = STRtree([net.lines[n] for n in kept]) if kept else None
    cand_tree = STRtree([net.lines[n] for n in cand]) if cand else None
    for tx, tz in targets:
        q = Point(tx, tz)
        if kept_tree is not None and len(kept_tree.query(q, "dwithin", access_m)):
            continue
        if any(net.lines[n].distance(q) <= access_m for n, _ in net.marked) or cand_tree is None:
            continue
        hits = cand_tree.query(q, "dwithin", access_m)
        if not len(hits):
            continue
        n = cand[int(min(hits, key=lambda i: (net.lines[cand[int(i)]].distance(q), int(i))))]
        c = list(net.lines[n].coords)
        seg = min(range(len(c) - 1), key=lambda i: LineString(c[i : i + 2]).distance(q))
        dist, prev = net.search(main)
        end = min((_key(*c[seg]), _key(*c[seg + 1])), key=lambda x: dist.get(x, math.inf))
        if dist.get(end, math.inf) > max_link_m:
            continue
        net.marked.add((n, seg))
        main |= set(net.nodes(n)[seg : seg + 2]) | net.link(end, dist, prev)
        stats["targets"] += 1
        stats["linkM"] += dist[end]
    stats["linkM"] = round(stats["linkM"], 1)
    return stats


def rural_streets(
    streets: Sequence[dict[str, Any]],
    ring: Sequence[tuple[float, float]],
    gates: Sequence[tuple[float, float]],
    spec: dict[str, Any],
    targets: Sequence[tuple[float, float]] = (),
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """(streets with the rural rules applied, stats). ``gates``: the gate towers' positions;
    ``targets``: places that need a way (doors, starts, posterns)."""
    inner = Polygon(ring).buffer(0).buffer(float(spec.get("insideM", 5.0)))
    land, field = spec["landstrasse"], spec["feldweg"]
    land_hw, field_hw = set(land["highways"]), set(field["highways"])
    gate_hw = set(land.get("gateHighways", ["residential", "living_street", "unclassified"]))
    named_hw, min_named = set(field.get("named", [])), float(field.get("minNamedM", 400.0))
    half = spec.get("linkHalfM")  # the waynet's area: links only count there
    link_box = None if half is None else box(-float(half), -float(half), float(half), float(half))
    belt_spec = spec.get("belt", {})
    belt = Polygon(ring).buffer(0).buffer(float(belt_spec.get("m", 0.0)))
    belt_hw = set(belt_spec.get("highways", []))
    gate_m = float(land.get("gateM", 12.0))
    reach = Polygon(ring).buffer(0).convex_hull.buffer(float(spec.get("reachM", 600.0)))
    gate_pts = [Point(g) for g in gates]
    pieces: list[tuple[dict[str, Any], list[LineString], list[LineString], bool]] = []
    named: dict[str, float] = defaultdict(float)
    for s in streets:
        pts = s.get("points") or []
        if len(pts) < 2:
            pieces.append((s, [], [], False))
            continue
        line = LineString(pts)
        if inner.contains(line):
            pieces.append((s, [line], [], False))
            continue
        if s.get("highway") in belt_hw and belt.contains(line):
            pieces.append((s, [line], [], False))  # a minor way about the wall: stays
            continue
        inside = _lines(line.intersection(inner))
        outside = _lines(line.difference(inner))
        at_gate = s.get("highway") in gate_hw and any(line.distance(g) < gate_m for g in gate_pts)
        pieces.append((s, inside, outside, at_gate))
        if s.get("highway") in named_hw and s.get("name"):
            named[s["name"]] += sum(o.intersection(reach).length for o in outside)
    net = _Net()
    town: set[int] = set()
    plan: list[tuple[dict[str, Any], str | None, LineString | None, int]] = []
    stats: dict[str, Any] = {k: {"pieces": 0, "m": 0.0} for k in (*KINDS, "dropped")}
    for s, inside, outside, at_gate in pieces:
        hw = s.get("highway")
        walk = hw not in NO_ACCESS and not s.get("tunnel")
        whole = len(inside) == 1 and not outside and inside[0].equals(LineString(s["points"]))
        if (not inside and not outside) or whole:
            if whole:
                n = net.add(inside[0], True)
                if walk:
                    town.add(n)
            plan.append((s, None, None, -1))
            continue
        for p in inside:
            if len(p.coords) >= 2:
                n = net.add(p, True)
                if walk:
                    town.add(n)
                plan.append((s, "inside", p, n))
        if hw in land_hw or at_gate:
            kind = "landstrasse"
        elif hw in field_hw or (hw in named_hw and named.get(s.get("name", ""), 0.0) >= min_named):
            kind = "feldweg"
        else:
            kind = "dropped"
        for whole_p in outside:
            stats[kind]["pieces"] += 1
            stats[kind]["m"] += whole_p.length
            if link_box is None or link_box.contains(whole_p):
                split = [(whole_p, True)]
            else:  # only the part in the waynet's area takes part in the links
                split = [(q, True) for q in _lines(whole_p.intersection(link_box))]
                split += [(q, False) for q in _lines(whole_p.difference(link_box))]
            for p, linked in split:
                if p.length < 0.01:
                    continue
                if not linked:
                    plan.append((s, kind, p, -1))
                    continue
                n = net.add(p, kind != "dropped", kind == "dropped" and walk and not s.get("layer"))
                plan.append((s, kind, p, n))
    access = _connect(net, town, targets, float(spec.get("accessM", 12.0)),
                      float(spec.get("maxLinkM", 300.0)))  # fmt: skip
    out: list[dict[str, Any]] = []
    isolated = 0.0
    for s, kind, p, n in plan:
        if kind is None or p is None:
            out.append(s)
            continue
        if kind == "inside":
            out.append(s | {"points": _points(p)})
            continue
        base = {k: v for k, v in s.items() if k not in ("surface", "smoothness", "lanes")}
        base |= {"highway": "track", "class": "track", "osmHighway": s.get("highway"),
                 "widthSource": "rural"}  # fmt: skip
        if kind != "dropped":
            if n in net.isolated:
                isolated += p.length
                continue
            rule = land if kind == "landstrasse" else field
            out.append(base | {"rural": kind, "widthM": float(rule["widthM"]),
                               "surface": rule["surface"], "points": _points(p)})  # fmt: skip
            continue
        if n < 0:
            continue
        c = list(p.coords)
        segs = sorted(i for m, i in net.marked if m == n)
        runs: list[list[int]] = []
        for i in segs:
            if runs and i == runs[-1][-1] + 1:
                runs[-1].append(i)
            else:
                runs.append([i])
        for run in runs:
            part = LineString(c[run[0] : run[-1] + 2])
            out.append(base | {"rural": "feldweg", "access": True,
                               "widthM": float(field["widthM"]), "surface": field["surface"],
                               "points": _points(part)})  # fmt: skip
            stats["feldweg"]["m"] += part.length
            stats["dropped"]["m"] -= part.length
    for v in stats.values():
        v["m"] = round(v["m"], 1)
    stats["isolatedM"] = round(isolated, 1)
    stats["access"] = access
    stats["namedKept"] = sorted(n for n, m in named.items() if m >= min_named)
    return out, stats


def load_rules(data_dir: Path) -> dict[str, Any] | None:
    """The site's rural rules, or None when the file is missing or ``enabled`` is false."""
    path = data_dir / RULES_FILE
    if not path.is_file():
        return None
    spec = json.loads(path.read_text(encoding="utf-8"))
    return spec if spec.get("enabled") else None


def _targets(data_dir: Path, gates: Sequence[Any], index: Path) -> list[tuple[float, float]]:
    """Doors of the generated houses, the start points and the gates (towers and posterns)."""
    from gothar_worldgen.export.starts import DEFAULT_STARTS, load_starts

    out = [(float(g.at[0]), float(g.at[1])) for g in gates]
    starts = (*DEFAULT_STARTS, *load_starts(data_dir / "starts.json"))
    out += [(float(s.x), float(s.z)) for s in starts]
    if index.is_file():
        for e in json.loads(index.read_text(encoding="utf-8")).get("entries", []):
            out += [(float(d[0]), float(d[1])) for d in e.get("doors", [])]
    return out


_CACHE: dict[tuple[Any, ...], dict[str, Any]] = {}


def load_street_doc(work: Path, data_dir: Path | None) -> dict[str, Any]:
    """``streets.json`` with the rural rules applied (``data_dir`` None: unchanged).

    The result adds ``"rural"`` (the stats) and ``"raw"`` (the streets as in the file). The doors
    come from the site's ``buildings_index.json`` (``assets/source/worlds/<site>/generated`` of
    the checkout that holds ``data_dir``), written by ``buildings``, which reads the raw streets.
    """
    path = work / "streets.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    spec = load_rules(data_dir) if data_dir is not None else None
    if spec is None or data_dir is None or not (data_dir / "city_wall.json").is_file():
        return doc
    wall = data_dir / "city_wall.json"
    repo = data_dir.resolve().parents[3]
    index = repo / "assets/source/worlds" / data_dir.name / "generated/buildings_index.json"
    inputs = (path, data_dir / RULES_FILE, wall, index, data_dir / "starts.json")
    stamp = tuple(f.stat().st_mtime if f.is_file() else 0.0 for f in inputs)
    key = (str(path), str(data_dir), *stamp)
    if key in _CACHE:
        return _CACHE[key]
    from gothar_worldgen.walls.citywall import load_course

    features = json.loads((work / "features.json").read_text(encoding="utf-8")).get("features", [])
    course = load_course(json.loads(wall.read_text(encoding="utf-8")), features)
    towers = [g.at for g in course.gates if g.kind == "tower"]
    targets = _targets(data_dir, course.gates, index)
    streets, stats = rural_streets(doc.get("streets", []), course.ring, towers, spec, targets)
    res = dict(doc, streets=streets, rural=stats, raw=doc.get("streets", []))
    _CACHE.clear()
    _CACHE[key] = res
    return res
