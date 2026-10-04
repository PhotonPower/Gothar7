"""Level terraces for the castle garden (W6, decision of the project owner 2026-10-03).

The garden ground was a sloping plane, and the owner saw pavilions and fountains sink into it. Like
real baroque gardens the parterre now lies on level terraces: the west half, the middle field (with
the obelisk fountain) and the east half, each at the median DGM height inside it. ``plan`` works
out, in the garden frame (s along the parterre, t across, from ``schloss_geometry.garden_layout``):

- ``terraces``: rectangles with their level;
- ``walls``: retaining wall pieces (about 2 m) along the outer edges and between the terraces, from
  below the lower side up to the higher side; ``bodies``: one collision box per run of pieces
  between two openings. On the outer edges a piece is a stone ledge ``LEDGE_W`` wide on the higher
  side, its top flush with that side (inside where the ground falls away outside, outside where it
  rises): the heightmap cannot step, it slopes over a cell (up to 1.4 m across the turned grid),
  and the ledge covers that slope;
- ``stairs``: flights at the gates of the railing (north side, outer ends), on the lower side
  rising towards the edge; between the terraces a landing at the gate and a side flight along the
  wall, beside the obelisk fountain (``landings``; sunk into the middle field with thin cheek walls
  where it is higher than the half, ``cut``); walls leave an opening at every flight;
- ``pads`` (world): the heightmap is set to each terrace's level (and lowered under sunken
  flights), so it deliberately leaves the DGM there (``export-terrain``); strips ``STRIP_M`` wide
  along the outer edges move the heightmap's slope under the ledges (inside lowered to the outer
  ground, ``clampBelow``; outside set to the terrace level), ``STRIP_IN`` wide between the terraces
  (``LEDGE_IN`` wide ledges); all of them ``exact`` (cell centres inside, no half-cell margin).
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

import numpy as np

SAMPLE_M = 2.0  # wall pieces along an edge
PARAPET_M = 0.6  # on top of a wall where the ground outside falls away by more than STEP_M
STEP_M = 0.4  # differences below this need neither stairs nor a parapet
RISE_M = 0.18
RUN_M = 0.3
STAIR_W = 2.4  # a little wider than the 2 m gates
WALL_T = 0.45
LEDGE_W = 3.0  # outer retaining walls: ledge on the higher side (the beds keep 3.9 m off the edge)
STRIP_M = 1.5  # heightmap strip that puts the cell slope under the ledge (1 m cells, turned grid)
# between the terraces there is only room for a narrower ledge (fountain, beds 1.5 m off); its
# strip is tuned for that width (the slope left over is at most about 18 % of the step)
LEDGE_IN = 1.5
STRIP_IN = 0.7
SIDE_W = 0.8  # side flights between a terrace wall and the obelisk fountain
CHEEK_T = 0.2

Pt = tuple[float, float]


def _median(values: list[float]) -> float:
    return round(float(np.median(values)) * 20) / 20


def _r(p: Pt) -> list[float]:
    return [round(float(p[0]), 3), round(float(p[1]), 3)]


def _at(a: Pt, b: Pt, u: float) -> Pt:
    return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)


def _off(p: Pt, d: Pt, k: float) -> Pt:
    return (p[0] + d[0] * k, p[1] + d[1] * k)


class _Planner:
    def __init__(self, frame: Any, height: Callable[[float, float], float]) -> None:  # noqa: ANN401
        self.frame = frame
        self.height = height
        self.walls: list[dict[str, Any]] = []
        self.bodies: list[dict[str, Any]] = []
        self.stairs: list[dict[str, Any]] = []
        self.cuts: list[dict[str, Any]] = []
        self.landings: list[dict[str, Any]] = []
        self.strips: list[dict[str, Any]] = []

    def h(self, p: Pt) -> float:
        x, _, z = self.frame.p(p[0], p[1], 0.0)
        return float(self.height(x, z))

    def run_walls(
        self,
        a: Pt,
        b: Pt,
        inward: Pt,
        pieces: list[tuple[float, ...]],
        thick: float = WALL_T,
    ) -> None:
        """Wall pieces (u0, u1, y0, y1[, flip]) along a->b, ``thick`` towards ``inward`` (away
        from it with ``flip``); one collision box per contiguous run on one side."""
        run: list[tuple[float, ...]] = []

        def side(p: tuple[float, ...]) -> list[float]:
            k = -1.0 if len(p) > 4 and p[4] else 1.0
            return [inward[0] * k, inward[1] * k]

        def flush() -> None:
            if run:
                self.bodies.append(
                    {
                        "a": _r(_at(a, b, run[0][0])),
                        "b": _r(_at(a, b, run[-1][1])),
                        "in": side(run[0]),
                        "y0": round(min(p[2] for p in run), 3),
                        "y1": round(max(p[3] for p in run), 3),
                        "t": thick,
                    }
                )
                run.clear()

        last = None
        for p in pieces:
            if last is not None and (abs(p[0] - last) > 1e-6 or side(p) != side(run[-1])):
                flush()
            self.walls.append(
                {
                    "a": _r(_at(a, b, p[0])),
                    "b": _r(_at(a, b, p[1])),
                    "in": side(p),
                    "y0": round(p[2], 3),
                    "y1": round(p[3], 3),
                    "t": thick,
                }
            )
            run.append(p)
            last = p[1]
        flush()

    def edge(self, y: float, a: Pt, b: Pt, out: Pt, gates: list[float]) -> None:
        """Outer edge a->b of a terrace at level y (``out``: outward unit vector in (s, t))."""
        length = float(np.hypot(b[0] - a[0], b[1] - a[1]))
        openings = []
        for g in gates:  # a flight at each gate where the step is big enough
            at = _at(a, b, g)
            outer = self.h(_off(at, out, 0.8))
            if outer > y:  # the top landing lies on the ledge band outside
                outer = max(self.h(_off(at, out, d)) for d in (0.5, 1.5, 2.5, 3.5))
            if abs(outer - y) < STEP_M:
                continue
            d = (-out[0], -out[1]) if outer > y else out  # into the lower side
            self.stairs.append(
                {
                    "top": _r(at),
                    "dir": [d[0], d[1]],
                    "y0": round(min(outer, y), 3),
                    "y1": round(max(outer, y), 3),
                    "w": STAIR_W,
                }
            )
            half_w = STAIR_W / 2 / length
            openings.append((g - half_w, g + half_w))
        n = max(1, int(np.ceil(length / SAMPLE_M)))
        pieces = []
        for i in range(n):
            u0, u1 = i / n, (i + 1) / n
            outside = [
                self.h(_off(_at(a, b, u), out, d))
                for u in (u0, (u0 + u1) / 2, u1)
                for d in (0.5, 1.5, 2.5, 3.5)
            ]
            lo, hi = min(outside), max(outside)
            near = min(outside[0], outside[4], outside[8])  # 0.5 m outside
            rises = float(np.mean(outside)) > y  # the ground outside is higher
            p0, p1 = _at(a, b, u0), _at(a, b, u1)
            k = STRIP_M if rises else -STRIP_M
            self.strips.append(
                {
                    "st": [_r(p0), _r(p1), _r(_off(p1, out, k)), _r(_off(p0, out, k))],
                    "y": round(y if rises else near, 3),
                    "clampBelow": not rises,
                }
            )
            for o0, o1 in openings:  # cut the piece around an opening
                if u0 < o1 and o0 < u1:
                    u0, u1 = (o1, u1) if u0 >= o0 else (u0, o0)
            if u1 - u0 < 1e-3:
                continue
            if rises:  # ledge outside at the outer ground, holding it back
                pieces.append((u0, u1, y - 0.3, max(hi, y), True))
            else:  # ledge inside at the terrace level, its face down to the outer ground
                pieces.append((u0, u1, lo - 0.3, y, False))
        self.run_walls(a, b, (-out[0], -out[1]), pieces, LEDGE_W)


def plan(
    frame: Any,  # noqa: ANN401  schloss_geometry.GardenFrame
    height: Callable[[float, float], float],
    extent: dict[str, float],
) -> dict[str, Any]:
    """Terraces, walls, stairs and heightmap pads of the garden.

    ``frame``: the garden frame (``p(s, t, y)``, ``centre``); ``height``: DGM at world (x, z);
    ``extent``: half sizes of the railing (``halfS`` along, ``halfT`` across, ``innerS`` the
    distance of the inner fences from the centre, optional ``gateS`` the distance of the north
    gates of both halves from the centre: their flights lie on the gates).
    """
    cs, ct = frame.centre
    hs, ht, inner = extent["halfS"] + 0.5, extent["halfT"] + 0.4, extent["innerS"]
    t0, t1 = ct - ht, ct + ht
    pl = _Planner(frame, height)
    spans = {
        "west": (cs - hs, cs - inner),
        "mitte": (cs - inner, cs + inner),
        "ost": (cs + inner, cs + hs),
    }
    terraces = []
    for k, (a, b) in spans.items():
        samples = [
            pl.h((s, t)) for s in np.arange(a + 0.5, b, 1.0) for t in np.arange(t0 + 0.5, t1, 1.0)
        ]
        terraces.append(
            {
                "key": k,
                "s": [round(a, 3), round(b, 3)],
                "t": [round(t0, 3), round(t1, 3)],
                "y": _median(samples),
            }
        )
    level = {tr["key"]: tr["y"] for tr in terraces}
    gate_s = extent.get("gateS")  # the railing model's north gates, from the centre
    for tr in terraces:
        (a, b), y = tr["s"], tr["y"]
        gate = 0.5  # north: a gate on the axis of each part, or where the railing has its gates
        if gate_s is not None and tr["key"] != "mitte":
            at = cs - float(gate_s) if tr["key"] == "west" else cs + float(gate_s)
            gate = (at - a) / (b - a)
        pl.edge(y, (a, t1), (b, t1), (0.0, 1.0), [gate])
        pl.edge(y, (a, t0), (b, t0), (0.0, -1.0), [])  # south: the garden falls to the old wall
    west, east = terraces[0], terraces[-1]
    pl.edge(west["y"], (west["s"][0], t0), (west["s"][0], t1), (-1.0, 0.0), [0.5])
    pl.edge(east["y"], (east["s"][1], t0), (east["s"][1], t1), (1.0, 0.0), [0.5])
    # between the terraces: a wall at the boundary with a ledge on the higher side, and a flight in
    # the middle field at the gate
    for half, s_b, toward in (("west", cs - inner, 1.0), ("ost", cs + inner, -1.0)):
        y_h, y_m = level[half], level["mitte"]
        lo, hi = min(y_h, y_m), max(y_h, y_m)
        a, b = (s_b, t0), (s_b, t1)
        if hi - lo < STEP_M:
            pl.run_walls(a, b, (toward, 0.0), [(0.0, 1.0, lo - 0.3, hi)])
            continue
        n = int(np.ceil((hi - lo) / RISE_M))
        run = n * RUN_M
        s_in = s_b + toward * WALL_T  # inner face of the boundary wall
        s_out = s_in + toward * SIDE_W
        s_mid = (s_in + s_out) / 2
        land = (ct - STAIR_W / 2, ct + STAIR_W / 2)
        strip = sorted([s_in, s_out])
        span = t1 - t0

        def u(t: float, t0: float = t0, span: float = span) -> float:
            return (t - t0) / span

        if y_m > y_h:  # the half is lower: a pit at its level, the flight rises out of it
            face, into = s_b, toward  # the ledge lies in the middle field
            pl.stairs.append(
                {
                    "top": _r((s_mid, land[1])),
                    "dir": [0.0, 1.0],
                    "y0": round(lo, 3),
                    "y1": round(hi, 3),
                    "w": SIDE_W,
                    "side": True,  # walls and landing come from the plan
                    "cut": True,
                }
            )
            pl.cuts.append({"s": strip, "t": [land[0], land[1] + run], "y": round(lo - 0.05, 3)})
            # the ledge leaves room for the pit; beside the flight only the wall
            pl.run_walls(a, b, (into, 0.0), [(0.0, u(land[0] - CHEEK_T), lo - 0.3, hi)], LEDGE_IN)
            pl.run_walls(
                a,
                b,
                (into, 0.0),
                [(u(land[0] - CHEEK_T), u(land[0]), lo - 0.3, hi),
                 (u(land[1]), u(land[1] + run), lo - 0.3, hi)],
            )  # fmt: skip
            pl.run_walls(a, b, (into, 0.0), [(u(land[1] + run), 1.0, lo - 0.3, hi)], LEDGE_IN)
            # thin cheek walls on the open sides of the pit
            s_cheek = s_out + toward * CHEEK_T
            cheek = [(0.0, 1.0, lo - 0.3, hi)]
            pl.run_walls(
                (s_cheek, land[0]), (s_cheek, land[1] + run), (-toward, 0.0), cheek, CHEEK_T
            )
            pl.run_walls(
                (s_in, land[0] - CHEEK_T), (s_out, land[0] - CHEEK_T), (0.0, 1.0), cheek, CHEEK_T
            )
        else:  # the half is higher: a raised landing at its level, the flight runs down
            face, into = s_in, -toward  # the ledge lies in the half, from the wall's inner face
            thick = WALL_T + LEDGE_IN
            g0, g1 = u(land[0]), u(land[1])
            fa, fb = (face, t0), (face, t1)
            pl.run_walls(fa, fb, (into, 0.0), [(0.0, g0, lo - 0.3, hi), (g1, 1.0, lo - 0.3, hi)],
                         thick)  # fmt: skip
            pl.landings.append(
                {"s": strip, "t": list(land), "y0": round(lo - 0.2, 3), "y1": round(hi, 3)}
            )
            # the gate through the ledge, at the half's level
            pl.landings.append(
                {
                    "s": sorted([face, face + into * thick]),
                    "t": list(land),
                    "y0": round(lo - 0.2, 3),
                    "y1": round(hi, 3),
                }
            )
            pl.stairs.append(
                {
                    "top": _r((s_mid, land[1])),
                    "dir": [0.0, 1.0],
                    "y0": round(lo, 3),
                    "y1": round(hi, 3),
                    "w": SIDE_W,
                    "side": True,  # walls and landing come from the plan
                }
            )
        # the heightmap's slope under the ledge: the higher side lowered next to the face
        q0, q1 = face - into * 0.01, face + into * STRIP_IN
        pl.strips.append(
            {"st": [[q0, t0], [q1, t0], [q1, t1], [q0, t1]], "y": round(lo, 3), "clampBelow": True}
        )

    def world(s0: float, s1: float, ta: float, tb: float) -> list[list[float]]:
        return [
            [round(frame.p(s, t, 0)[0], 2), round(frame.p(s, t, 0)[2], 2)]
            for s, t in ((s0, ta), (s1, ta), (s1, tb), (s0, tb))
        ]

    pads = [  # the terraces overlap by a hair, so no cell centre on a boundary is left out
        {
            "polygon": world(tr["s"][0] - 0.01, tr["s"][1] + 0.01, t0, t1),
            "y": tr["y"],
            "exact": True,
        }
        for tr in terraces
    ]
    pads += [
        {"polygon": world(c["s"][0], c["s"][1], c["t"][0], c["t"][1]), "y": c["y"], "exact": True}
        for c in pl.cuts
    ]

    def world_pts(pts: list[list[float]]) -> list[list[float]]:
        return [[round(frame.p(a, b, 0)[0], 2), round(frame.p(a, b, 0)[2], 2)] for a, b in pts]

    pads += [
        {"polygon": world_pts(st["st"]), "y": st["y"], "exact": True}
        | ({"clampBelow": True} if st["clampBelow"] else {})
        for st in pl.strips
    ]
    return {
        "terraces": terraces,
        "walls": pl.walls,
        "bodies": pl.bodies,
        "stairs": pl.stairs,
        "landings": pl.landings,
        "pads": pads,
        "wallT": WALL_T,
        "ledgeW": LEDGE_W,
        "rise": RISE_M,
        "run": RUN_M,
    }


def level_at(terraces: list[dict[str, Any]], s: float) -> float | None:
    """Level of the terrace whose s-range holds ``s`` (garden frame), or None outside."""
    for tr in terraces:
        if tr["s"][0] - 1e-6 <= s <= tr["s"][1] + 1e-6:
            return float(tr["y"])
    return None
