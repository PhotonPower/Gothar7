"""The ``waynet`` block in the engine's layout (world_format WorldFile.cpp): one point, freepoint or
edge per line, sorted, numbers as the engine rounds them, so a generator run and an editor save of
the same net give the same bytes."""

from __future__ import annotations

import json
import math
from typing import Any

import numpy as np

NL = "\n"


def _fixed(f: np.float32) -> str:
    """The fewest decimals in fixed notation that read back to the float32 ``f``."""
    for d in range(10):
        text = f"{float(f):.{d}f}"
        if np.float32(float(text)) == f:
            return text
    return f"{float(f):.9f}"


def tidy(value: float) -> float:
    """As the engine writes a number (world.md "Zahlen", world_format ``tidy``): stored as float32,
    the fewest decimals that give it back; more than six decimals (noise) are rounded to 1e-5
    first (half away from zero); no -0."""
    f = np.float32(value)
    text = _fixed(f)
    if "." in text and len(text) - text.index(".") - 1 > 6:
        v = float(f) * 1e5
        rounded = math.copysign(math.floor(abs(v) + 0.5), v) / 1e5
        text = _fixed(np.float32(rounded))
    r = float(text)
    return 0.0 if r == 0.0 else r


def _dir(d: Any) -> list[float] | None:  # noqa: ANN401
    """The written direction: horizontal and unit length (the engine keeps it as written)."""
    if d is None:
        return None
    x, z = float(d[0]), float(d[2])
    n = math.hypot(x, z)
    if n <= 1e-6:
        return None
    return [tidy(x / n), 0.0, tidy(z / n)]


def _point(p: dict[str, Any]) -> str:
    out: dict[str, Any] = {"name": p["name"], "pos": [tidy(v) for v in p["pos"]]}
    d = _dir(p.get("dir"))
    if d is not None:
        out["dir"] = d
    if p.get("owner") == "worldgen":
        out["owner"] = "worldgen"
    return json.dumps(out, separators=(",", ":"), ensure_ascii=False)


def normalized(block: dict[str, Any]) -> dict[str, Any]:
    """Sorted points and freepoints, edges with the smaller name first, duplicates merged
    (generated only if every copy was)."""
    edges: dict[tuple[str, str], bool] = {}
    for e in block.get("edges", []):
        a, b = sorted(e[:2])
        gen = len(e) > 2 and e[2] == "worldgen"
        edges[(a, b)] = edges.get((a, b), True) and gen
    return {
        "points": sorted(block.get("points", []), key=lambda p: p["name"]),
        "edges": [[a, b, "worldgen"] if g else [a, b] for (a, b), g in sorted(edges.items())],
        "freepoints": sorted(block.get("freepoints", []), key=lambda p: p["name"]),
    }


def _list(lines: list[str]) -> str:
    if not lines:
        return "[]"
    body = "".join((NL if i == 0 else "," + NL) + "      " + s for i, s in enumerate(lines))
    return "[" + body + NL + "    ]"


def waynet_text(block: dict[str, Any]) -> str:
    """The value of ``"waynet"`` as the engine writes it (what follows ``"waynet": ``)."""
    b = normalized(block)
    points = _list([_point(p) for p in b["points"]])
    edges = _list([json.dumps(e, separators=(",", ":"), ensure_ascii=False) for e in b["edges"]])
    freepoints = _list([_point(p) for p in b["freepoints"]])
    return (
        "{" + NL + '    "points": ' + points + "," + NL + '    "edges": ' + edges + "," + NL
        + '    "freepoints": ' + freepoints + NL + "  }"
    )  # fmt: skip
