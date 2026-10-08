"""Steps, gutters and retaining walls in the streets of the old town (W6, ``streetworks.json``).

``export-terrain`` shapes the ground first (the steps' profile rising evenly, see
``export/ways.shape_steps``); ``gothar-worldgen streetworks`` then builds the meshes on the final
heightmap into ``generated/streetworks/`` with ``streetworks_index.json``, and ``assemble`` sets
them as vobs (group ``WORLDGEN_STRASSENBAU``).

- **Steps** along every OSM ``steps`` way of the core: blocks of stone, risers at most ``riseM``,
  each tread as high as the ground at its upper end, so the ground never pokes through it (on
  the evenly rising ground a figure's feet lie at most one riser under the tread). Where the
  ground beside the steps lies lower than ``cheekM`` a cheek wall closes the side. They do not
  collide (the ground carries).
- **Gutters** of flat slabs (a shallow V, ``widthM`` wide, a little lighter than the cobbles, the
  V seen only in its shading) along the paved streets of the core:
  in the middle of streets narrower than ``centreBelowM``, at both sides of wider ones; not under
  houses, on squares or on steps. Decoration only: one file per ``cellM`` cell.
- **Steps in the walls' gaps** before doors, up or down the bank (planned with the walls).
- **Retaining walls** where a street is cut into a slope, as ``export-terrain`` planned them
  (``export/retaining.py``, ``generated/retaining_walls.json``): dry stone, mortared like the
  socles where they meet a house or the town wall; they collide (prisms of about two metres).
"""

from __future__ import annotations

import json
import math
from collections import defaultdict
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from shapely.geometry import LineString, Polygon
from shapely.strtree import STRtree

from gothar_worldgen.buildings.gltf import CollisionPart, Primitive, glb_bytes_multi
from gothar_worldgen.buildings.massing import _Builder
from gothar_worldgen.buildings.stairs import oriented_box

INDEX_FORMAT = "gothar-streetworks-index"
INDEX_VERSION = 1
GROUP_NAME = "WORLDGEN_STRASSENBAU"
FORMAT_VERSION = 1
Pt = tuple[float, float]
Height = Callable[[float, float], float]


class StreetworksError(ValueError):
    pass


def load_rules(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise StreetworksError(f"{path.name}: {e}") from e
    if doc.get("version") != FORMAT_VERSION:
        raise StreetworksError(f"{path.name}: version {doc.get('version')} != {FORMAT_VERSION}")
    return doc


@dataclass
class Piece:
    """One file: builders per palette entry, around ``origin``."""

    key: str
    kind: str
    origin: tuple[float, float, float]
    builders: dict[str, _Builder] = field(default_factory=dict)
    collision: list[CollisionPart] = field(default_factory=list)
    gameplay: bool = False  # walls: never culled for size

    def b(self, material: str) -> _Builder:
        if material not in self.builders:
            self.builders[material] = _Builder(self.origin)
        return self.builders[material]

    def triangles(self) -> int:
        return sum(len(b.idx) // 3 for b in self.builders.values())


def _no_collision(piece: Piece) -> None:
    """A model without COL_ would collide with all its triangles (asset.md): a tiny box a metre
    under the ground instead."""
    from gothar_worldgen.mobs import box_body

    piece.collision.append(box_body("COL_HULL_NONE", (-0.02, -1.04, -0.02), (0.02, -1.0, 0.02)))


# --- steps ------------------------------------------------------------------------------------


def _profile(line: LineString, height: Height, step: float = 0.1) -> tuple[np.ndarray, np.ndarray]:
    n = max(2, int(line.length / step) + 1)
    d = np.linspace(0.0, line.length, n)
    h = np.array([height(*line.interpolate(float(t)).coords[0]) for t in d])
    return d, h


def steps_piece(key: str, line: LineString, width: float, height: Height,
                spec: dict[str, Any]) -> Piece | None:  # fmt: skip
    """Stone steps along ``line`` (from its low end up), ``width`` wide; None if it hardly rises."""
    d, h = _profile(line, height)
    if h[-1] < h[0]:
        line = LineString(list(line.coords)[::-1])
        d, h = _profile(line, height)
    h = np.maximum.accumulate(h)  # the ground is shaped evenly rising; be safe against noise
    drop = float(h[-1] - h[0])
    if drop < float(spec["minDropM"]):
        return None
    n = max(1, math.ceil(drop / float(spec["riseM"]) - 1e-9))
    rise = drop / n
    # tread k (1 .. n) from where the ground reaches the top of tread k-1 to where it reaches its
    # own top: its top is never under the ground
    starts = [float(np.interp(h[0] + (k - 1) * rise, h, d)) for k in range(1, n + 1)]
    starts.append(float(line.length))
    mid = line.interpolate(line.length / 2)
    origin = (mid.x, float(h[0]), mid.y)
    piece = Piece(key, "steps", origin)
    half = width / 2
    cheek_w = float(spec["cheekWidthM"])
    blocks = []  # (t0, t1, top): long treads (landings) in blocks of at most a metre along it
    for k in range(1, n + 1):
        t0, t1 = starts[k - 1], max(starts[k], starts[k - 1] + 0.05)
        parts = max(1, math.ceil((t1 - t0) / 1.0))
        for j in range(parts):
            blocks.append((t0 + (t1 - t0) * j / parts, t0 + (t1 - t0) * (j + 1) / parts,
                           float(h[0] + k * rise)))  # fmt: skip
    for t0, t1, top in blocks:
        a, b = line.interpolate(t0), line.interpolate(t1)
        dx, dz = b.x - a.x, b.y - a.y
        ln = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / ln, dx / ln
        quad = [(a.x + nx * half, a.y + nz * half), (b.x + nx * half, b.y + nz * half),
                (b.x - nx * half, b.y - nz * half), (a.x - nx * half, a.y - nz * half)]  # fmt: skip
        low = min(height(x, z) for x, z in quad) - float(spec["sinkM"])
        oriented_box(piece.b("stone"), quad, low, top)
        for side in (1.0, -1.0):  # a cheek wall where the ground beside lies lower
            ex, ez = (
                (a.x + b.x) / 2 + nx * side * (half + 0.35),
                (a.y + b.y) / 2 + nz * side * (half + 0.35),
            )
            beside = height(ex, ez)
            if top - beside < float(spec["cheekM"]):
                continue
            o, i = half + cheek_w, half
            sx, sz = nx * side, nz * side
            cq = [(a.x + sx * i, a.y + sz * i), (b.x + sx * i, b.y + sz * i),
                  (b.x + sx * o, b.y + sz * o), (a.x + sx * o, a.y + sz * o)]  # fmt: skip
            oriented_box(piece.b("stone"), cq, beside - float(spec["sinkM"]),
                         top + float(spec["cheekAboveM"]))  # fmt: skip
    _no_collision(piece)
    return piece


# --- gutters ----------------------------------------------------------------------------------


def _offset(line: LineString, off: float) -> LineString | None:
    if abs(off) < 1e-6:
        return line
    g = line.offset_curve(off)
    if isinstance(g, LineString) and not g.is_empty and g.length > 1.0:
        return g
    return None


def gutter_strips(streets: Sequence[dict[str, Any]], area: Polygon, blocked: Sequence[Any],
                  spec: dict[str, Any]) -> list[LineString]:  # fmt: skip
    """The gutters' axes: per paved street of the core in its middle or at both sides, cut where
    they would run under a house, over a square or onto steps."""
    kinds = set(spec["highways"])
    tree = STRtree(list(blocked)) if blocked else None
    out: list[LineString] = []
    for s in streets:
        pts = s.get("points") or []
        if s.get("highway") not in kinds or len(pts) < 2:
            continue
        if s.get("tunnel") or s.get("bridge") or s.get("layer"):
            continue
        line = LineString(pts).intersection(area)
        w = float(s.get("widthM") or 4.0)
        offs = [0.0] if w < float(spec["centreBelowM"]) else [
            w / 2 - float(spec["sideOffsetM"]), -(w / 2 - float(spec["sideOffsetM"]))]  # fmt: skip
        for part in getattr(line, "geoms", [line]):
            if not isinstance(part, LineString) or part.length < 2.0:
                continue
            for off in offs:
                g = _offset(part, off)
                if g is None:
                    continue
                if tree is not None:
                    hits = [blocked[int(i)] for i in tree.query(g.buffer(float(spec["clearM"])))]
                    if hits:
                        from shapely.ops import unary_union

                        g = g.difference(unary_union(hits).buffer(float(spec["clearM"])))
                for q in getattr(g, "geoms", [g]):
                    if isinstance(q, LineString) and q.length >= 2.0:
                        out.append(q)
    return out


def gutter_pieces(strips: Sequence[LineString], height: Height,
                  spec: dict[str, Any]) -> list[Piece]:  # fmt: skip
    """The gutters as a shallow V of flat stones following the ground, one file per cell."""
    cell = float(spec["cellM"])
    by_cell: dict[tuple[int, int], list[LineString]] = defaultdict(list)
    for g in strips:
        m = g.interpolate(g.length / 2)
        by_cell[(math.floor(m.x / cell), math.floor(m.y / cell))].append(g)
    half, depth, lift = float(spec["widthM"]) / 2, float(spec["depthM"]), float(spec["liftM"])
    skirt = lift + 0.05
    step = float(spec["sampleM"])
    pieces = []
    for (i, j), lines in sorted(by_cell.items()):
        cx, cz = (i + 0.5) * cell, (j + 0.5) * cell
        piece = Piece(f"gutter_{i}_{j}".replace("-", "m"), "gutter", (cx, height(cx, cz), cz))
        b = piece.b("stone_slab")  # flat slabs a little lighter than the cobbles, the V shaded
        for g in lines:
            n = max(2, int(g.length / step) + 1)
            ts = np.linspace(0.0, g.length, n)
            rows = []
            for t in ts:
                p = g.interpolate(float(t))
                q = g.interpolate(float(min(t + 0.2, g.length))) if t < g.length else p
                r = g.interpolate(float(max(t - 0.2, 0.0)))
                dx, dz = q.x - r.x, q.y - r.y
                ln = math.hypot(dx, dz) or 1.0
                nx, nz = -dz / ln, dx / ln
                # each edge on the ground beside it (a street falling across), the V's bottom
                # above the ground in the middle: no cobbles showing through
                lx, lz, rx, rz = p.x + nx * half, p.y + nz * half, p.x - nx * half, p.y - nz * half
                yl, yr = height(lx, lz) + lift, height(rx, rz) + lift
                yc = max((yl + yr) / 2 - depth, height(p.x, p.y) + lift - depth)
                rows.append([(lx, yl, lz), (p.x, yc, p.y), (rx, yr, rz), float(t), (nx, nz)])
            for (l0, c0, r0, t0, n0), (l1, c1, r1, t1, _) in zip(rows, rows[1:], strict=False):
                for e0, e1, f0, f1, u0, u1 in (
                    (l0, l1, c0, c1, 0.0, half),
                    (c0, c1, r0, r1, half, 2 * half),
                ):
                    b.polygon([e0, e1, f1, f0], [(u0, t0), (u0, t1), (u1, t1), (u1, t0)],
                              (0.0, 1.0, 0.0))  # fmt: skip
                for e0, e1, sx in (
                    (l0, l1, 1.0),
                    (r0, r1, -1.0),
                ):  # the slabs' edges into the ground
                    d0, d1 = (e0[0], e0[1] - skirt, e0[2]), (e1[0], e1[1] - skirt, e1[2])
                    b.polygon([e0, e1, d1, d0], [(0.0, t0), (0.0, t1), (skirt, t1), (skirt, t0)],
                              (n0[0] * sx, 0.0, n0[1] * sx))  # fmt: skip
        _no_collision(piece)
        if piece.triangles():
            pieces.append(piece)
    return pieces


# --- retaining walls ----------------------------------------------------------------------------


def wall_piece(w: dict[str, Any], spec: dict[str, Any]) -> Piece | None:
    """A retaining wall from the plan (``export/retaining.py``): its face along the street, as
    thick as ``thicknessM`` away from it, from its base to its top; dry stone or mortared like
    the socles. It collides in prisms of about two metres."""
    from gothar_worldgen.buildings.collision import prism_body

    pts, base, top, out = w["points"], w["base"], w["top"], w["outward"]
    if len(pts) < 2:
        return None
    thick = float(spec["thicknessM"])
    mid = pts[len(pts) // 2]
    origin = (float(mid[0]), float(min(base)), float(mid[1]))
    piece = Piece(w["id"], "wall", origin, gameplay=True)
    b = piece.b("stone" if w["style"] == "mortared" else "stone_dry")
    front = [(x, z) for x, z in pts]
    back = [(x + ox * thick, z + oz * thick) for (x, z), (ox, oz) in zip(pts, out, strict=True)]
    run = 0.0
    for i in range(len(pts) - 1):
        seg = math.dist(front[i], front[i + 1])
        (fa, fb), (ba, bb) = (front[i], front[i + 1]), (back[i], back[i + 1])
        ya0, yb0, ya1, yb1 = base[i], base[i + 1], top[i], top[i + 1]
        ox, oz = out[i]
        u0, u1 = run, run + seg
        run = u1
        b.polygon(
            [(fa[0], ya0, fa[1]), (fb[0], yb0, fb[1]), (fb[0], yb1, fb[1]), (fa[0], ya1, fa[1])],
            [(u0, ya0), (u1, yb0), (u1, yb1), (u0, ya1)],
            (-ox, 0.0, -oz),
        )  # face
        b.polygon(
            [(ba[0], ya0, ba[1]), (bb[0], yb0, bb[1]), (bb[0], yb1, bb[1]), (ba[0], ya1, ba[1])],
            [(u0, ya0), (u1, yb0), (u1, yb1), (u0, ya1)],
            (ox, 0.0, oz),
        )  # back
        b.polygon(
            [(fa[0], ya1, fa[1]), (fb[0], yb1, fb[1]), (bb[0], yb1, bb[1]), (ba[0], ya1, ba[1])],
            [(u0, 0.0), (u1, 0.0), (u1, thick), (u0, thick)],
            (0.0, 1.0, 0.0),
        )  # cap
    for i in (0, len(pts) - 1):  # the two ends
        f, k = front[i], back[i]
        j = 1 if i == 0 else len(pts) - 2
        ax, az = front[j][0] - f[0], front[j][1] - f[1]
        b.polygon([(f[0], base[i], f[1]), (k[0], base[i], k[1]), (k[0], top[i], k[1]),
                   (f[0], top[i], f[1])], [(0.0, base[i]), (thick, base[i]), (thick, top[i]),
                                          (0.0, top[i])], (-ax, 0.0, -az))  # fmt: skip
    step = 2
    for i in range(0, len(pts) - 1, step):
        j = min(i + step, len(pts) - 1)
        quad = Polygon([front[i], front[j], back[j], back[i]])
        if quad.area < 1e-3 or not quad.is_valid:
            continue
        y0, y1 = min(base[i : j + 1]), max(top[i : j + 1])
        piece.collision.append(prism_body(quad, y0, y1, origin, f"COL_HULL_{len(piece.collision)}"))
    return piece


# --- writing ----------------------------------------------------------------------------------


def write_pieces(pieces: Sequence[Piece], rules_color: Callable[[str], tuple[float, ...]],
                 out_dir: Path, vfs_dir: str,
                 texture_root: str = "../textures") -> list[dict[str, Any]]:  # fmt: skip
    """One ``.glb`` per piece (textured like the houses: the stone of the palette) and the index
    entries; stale files go."""
    from gothar_worldgen.textures.apply import texture_house

    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for piece in pieces:
        prims = [Primitive(m, rules_color(m), b.mesh()) for m, b in sorted(piece.builders.items())
                 if b.idx]  # type: ignore[arg-type]  # fmt: skip
        if not prims:
            continue
        prims = texture_house(prims, texture_root, piece.key)
        data = glb_bytes_multi(prims, piece.key, piece.collision)
        path = out_dir / f"{piece.key}.glb"
        if not path.is_file() or path.read_bytes() != data:
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_bytes(data)
            tmp.replace(path)
        entries.append({"id": piece.key, "kind": piece.kind, "mesh": f"{vfs_dir}/{piece.key}.glb",
                        "pos": [round(c, 4) for c in piece.origin],
                        "triangles": piece.triangles(),
                        **({"category": "gameplay"} if piece.gameplay else {})})  # fmt: skip
    referenced = {f"{e['id']}.glb" for e in entries}
    for stale in out_dir.glob("*.glb"):
        if stale.name not in referenced:
            stale.unlink()
    return entries


def plan_streetworks(streets: Sequence[dict[str, Any]], squares: Sequence[dict[str, Any]],
                     houses: Sequence[Polygon], area: Polygon, height: Height,
                     rules: dict[str, Any],
                     walls: Sequence[dict[str, Any]] = (),
                     gap_steps: Sequence[dict[str, Any]] = (),
                     ) -> tuple[list[Piece], dict[str, Any]]:  # fmt: skip
    """All pieces and statistics; ``walls`` and ``gap_steps`` (in the walls' gaps before doors):
    planned by export-terrain."""
    pieces: list[Piece] = []
    for w in walls:
        wp = wall_piece(w, rules["walls"])
        if wp is not None:
            pieces.append(wp)
    for g in gap_steps:
        sp = steps_piece(
            g["id"], LineString(g["points"]), float(g["width"]), height, rules["steps"]
        )
        if sp is not None:
            pieces.append(sp)
    steps_m = 0.0
    step_lines = []
    for s in streets:
        pts = s.get("points") or []
        if s.get("highway") != "steps" or len(pts) < 2:
            continue
        if s.get("tunnel") or s.get("bridge") or s.get("layer"):
            continue
        line = LineString(pts).intersection(area)
        for k, part in enumerate(getattr(line, "geoms", [line])):
            if not isinstance(part, LineString) or part.length < 1.0:
                continue
            step_lines.append(part)
            key = f"steps_{s.get('osmId', 'x')}_{k}".lower()
            p = steps_piece(key, part, float(s.get("widthM") or 1.5), height, rules["steps"])
            if p is not None:
                pieces.append(p)
                steps_m += part.length
    blocked: list[Any] = [*houses, *(Polygon(q["polygon"]) for q in squares
                                     if len(q.get("polygon") or []) >= 3),
                          *(g.buffer(1.0) for g in step_lines)]  # fmt: skip
    import shapely

    blocked = [g for g in (shapely.make_valid(q) for q in blocked) if not g.is_empty]
    strips = gutter_strips(streets, area, blocked, rules["gutters"])
    gutters = gutter_pieces(strips, height, rules["gutters"])
    pieces += gutters
    stats = {"steps": sum(1 for p in pieces if p.kind == "steps" and "_gap" not in p.key),
             "gapSteps": sum(1 for p in pieces if p.kind == "steps" and "_gap" in p.key),
             "stepsM": round(steps_m, 1),
             "walls": sum(1 for p in pieces if p.kind == "wall"),
             "gutterM": round(sum(g.length for g in strips), 1),
             "gutterFiles": len(gutters)}  # fmt: skip
    return pieces, stats


def write_index(path: Path, entries: list[dict[str, Any]], stats: dict[str, Any]) -> None:
    doc = {"format": INDEX_FORMAT, "version": INDEX_VERSION, "entries": entries, "stats": stats}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8", newline="\n")
