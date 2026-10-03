"""Start point vobs (world.md, "Vob-Typen": ``start``, pos = feet, camera 1.7 m above)."""

from __future__ import annotations

import json
import math
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

IDENTITY = (0.0, 0.0, 0.0, 1.0)


@dataclass(frozen=True)
class StartPoint:
    name: str
    x: float
    z: float
    height_above_ground: float = 0.0
    rot: tuple[float, float, float, float] = IDENTITY


# Leonberg: the origin is the market fountain (world.md example). Overview: 30 degrees down.
DEFAULT_STARTS = (
    StartPoint("START_MARKTPLATZ", 0.0, 8.0),
    StartPoint("START_UEBERSICHT", 0.0, 60.0, 40.0, (-0.258819, 0.0, 0.0, 0.965926)),
)


def load_starts(path: Path) -> tuple[StartPoint, ...]:
    """Extra start points of a site (``data/<site>/starts.json``, versioned): walkthrough stations
    and places for the owner to jump to; looking along ``yawDeg`` (0 = -Z, 90 = -X)."""
    if not path.is_file():
        return ()
    doc = json.loads(path.read_text(encoding="utf-8"))
    out = []
    for e in doc.get("starts", []):
        half = math.radians(float(e.get("yawDeg", 0.0))) / 2
        rot = (0.0, round(math.sin(half), 6) + 0.0, 0.0, round(math.cos(half), 6))
        out.append(StartPoint(str(e["name"]), float(e["x"]), float(e["z"]), 0.0, rot))
    return tuple(out)


def start_vob(vid: int, s: StartPoint, ground_y: float) -> dict[str, Any]:
    return {
        "id": vid,
        "type": "start",
        "name": s.name,
        "pos": [
            round(s.x, 5) + 0.0,
            round(ground_y + s.height_above_ground, 5) + 0.0,
            round(s.z, 5) + 0.0,
        ],
        "rot": list(s.rot),
    }


def add_start_points(
    doc: dict[str, Any],
    starts: tuple[StartPoint, ...],
    ground: Callable[[float, float], float],
    known: Callable[[str], bool] = lambda key: False,
    id_for: Callable[[str, int], int] | None = None,
) -> int:
    """Adds start points that are missing (by name, case-insensitive) and were never created before.

    ``known(key)`` tells whether a start point was created in an earlier run (then a missing one was
    deleted on purpose and is not re-created); ``id_for(key, floor)`` hands out stable ids.
    Returns the number of start points added.
    """
    vobs = doc.setdefault("vobs", [])
    present = {str(v.get("name", "")).upper() for v in vobs if v.get("type") == "start"}
    added = 0
    for s in starts:
        key = f"start:{s.name}"
        if s.name.upper() in present or known(key):
            continue
        floor = max([int(doc.get("nextVobId", 1))] + [int(v["id"]) + 1 for v in vobs])
        vid = id_for(key, floor) if id_for else floor
        vobs.append(start_vob(vid, s, ground(s.x, s.z)))
        doc["nextVobId"] = max(int(doc.get("nextVobId", 1)), vid + 1)
        added += 1
    vobs.sort(key=lambda v: int(v["id"]))
    return added
