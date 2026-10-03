"""Per-building annotations ("overrides", docs/design/leonberg-pipeline.md §4).

One JSON file per building in ``tools/worldgen/data/<site>/buildings/<id>.json`` (versioned). The
facade tool writes them, the building generator (W5) reads them. Unknown keys are kept, so newer
tools can add fields without older ones losing them.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

OPENING_TYPES = ("door", "window", "gate")
RUECKBAU_MODES = ("auto", "none", "split")


class OverrideError(Exception):
    """Invalid annotation; the message is meant for the user."""


@dataclass
class Opening:
    storey: int  # 0 = ground floor
    type: str  # door | window | gate
    x: float  # metres from the left facade edge (seen from outside)
    w: float  # width in metres
    h: float  # height in metres
    y: float | None = None  # sill height above the storey floor; None = door/gate on the floor


@dataclass
class FrontFacade:
    edge: int  # footprint edge index (see facade.rectify.facade_from_footprint)
    openings: list[Opening] = field(default_factory=list)
    timber: str | None = None  # framing pattern, e.g. "mann", "andreaskreuz"
    infill: str | None = None  # e.g. "plaster_ochre"


@dataclass
class BuildingOverride:
    id: str
    keep: bool = True
    style: str | None = None
    storeys: list[float] = field(default_factory=list)  # storey heights in metres, bottom up
    jetty_m: float | None = None
    front_facade: FrontFacade | None = None
    roof_cover: str | None = None
    notes: str | None = None
    seed: int | None = None
    locked: bool = False  # generator must not overwrite hand-made work
    rueckbau: str | None = None  # auto (default) | none (never replace) | split (always)
    extra: dict[str, Any] = field(default_factory=dict)  # unknown keys, written back unchanged


def _number(
    d: dict[str, Any], key: str, where: str, *, positive: bool = False, optional: bool = False
) -> float | None:
    if key not in d or d[key] is None:
        if optional:
            return None
        raise OverrideError(f"{where}: missing '{key}'")
    v = d[key]
    if isinstance(v, bool) or not isinstance(v, int | float) or not math.isfinite(v):
        raise OverrideError(f"{where}: '{key}' must be a number")
    if positive and v <= 0:
        raise OverrideError(f"{where}: '{key}' must be > 0")
    return float(v)


def _string(d: dict[str, Any], key: str, where: str) -> str | None:
    v = d.get(key)
    if v is not None and not isinstance(v, str):
        raise OverrideError(f"{where}: '{key}' must be a string")
    return v


_KNOWN = {
    "id",
    "keep",
    "style",
    "storeys",
    "jettyM",
    "frontFacade",
    "roofCover",
    "notes",
    "seed",
    "locked",
    "rueckbau",
}


def from_json(data: Any) -> BuildingOverride:  # noqa: ANN401
    if not isinstance(data, dict):
        raise OverrideError("override must be a JSON object")
    bid = data.get("id")
    if not isinstance(bid, str) or not bid:
        raise OverrideError("override needs a non-empty 'id'")
    where = f"override {bid}"
    for key in ("keep", "locked"):
        if key in data and not isinstance(data[key], bool):
            raise OverrideError(f"{where}: '{key}' must be true or false")
    storeys = data.get("storeys", [])
    if not isinstance(storeys, list):
        raise OverrideError(f"{where}: 'storeys' must be a list")
    storey_heights = [
        _number({"h": s}, "h", f"{where} storeys[{i}]", positive=True)
        for i, s in enumerate(storeys)
    ]
    rueckbau = data.get("rueckbau")
    if rueckbau is not None and rueckbau not in RUECKBAU_MODES:
        raise OverrideError(f"{where}: 'rueckbau' must be one of {', '.join(RUECKBAU_MODES)}")
    seed = data.get("seed")
    if seed is not None and (isinstance(seed, bool) or not isinstance(seed, int)):
        raise OverrideError(f"{where}: 'seed' must be an integer")

    front = None
    if data.get("frontFacade") is not None:
        f = data["frontFacade"]
        if not isinstance(f, dict):
            raise OverrideError(f"{where}: 'frontFacade' must be an object")
        edge = f.get("edge")
        if isinstance(edge, bool) or not isinstance(edge, int) or edge < 0:
            raise OverrideError(f"{where}: frontFacade.edge must be an index >= 0")
        openings = []
        for i, o in enumerate(f.get("openings", [])):
            ow = f"{where} opening {i}"
            if not isinstance(o, dict):
                raise OverrideError(f"{ow}: must be an object")
            storey = o.get("storey")
            if isinstance(storey, bool) or not isinstance(storey, int) or storey < 0:
                raise OverrideError(f"{ow}: 'storey' must be an index >= 0")
            if storey_heights and storey >= len(storey_heights):
                raise OverrideError(
                    f"{ow}: storey {storey} does not exist ({len(storey_heights)} storeys)"
                )
            kind = o.get("type")
            if kind not in OPENING_TYPES:
                raise OverrideError(f"{ow}: 'type' must be one of {', '.join(OPENING_TYPES)}")
            openings.append(
                Opening(storey, kind, _number(o, "x", ow), _number(o, "w", ow, positive=True),
                        _number(o, "h", ow, positive=True), _number(o, "y", ow, optional=True))
            )  # fmt: skip
        front = FrontFacade(
            edge, openings, _string(f, "timber", where), _string(f, "infill", where)
        )

    return BuildingOverride(
        id=bid,
        keep=data.get("keep", True),
        style=_string(data, "style", where),
        storeys=[h for h in storey_heights if h is not None],
        jetty_m=_number(data, "jettyM", where, optional=True),
        front_facade=front,
        roof_cover=_string(data, "roofCover", where),
        notes=_string(data, "notes", where),
        seed=seed,
        locked=data.get("locked", False),
        rueckbau=rueckbau,
        extra={k: v for k, v in data.items() if k not in _KNOWN},
    )


def to_json(o: BuildingOverride) -> dict[str, Any]:
    """JSON object in the documented key order; optional fields only when set."""
    out: dict[str, Any] = {"id": o.id, "keep": o.keep}
    if o.style is not None:
        out["style"] = o.style
    if o.storeys:
        out["storeys"] = o.storeys
    if o.jetty_m is not None:
        out["jettyM"] = o.jetty_m
    if o.front_facade is not None:
        f = o.front_facade
        front: dict[str, Any] = {"edge": f.edge, "openings": []}
        for op in f.openings:
            entry: dict[str, Any] = {
                "storey": op.storey,
                "type": op.type,
                "x": op.x,
                "w": op.w,
                "h": op.h,
            }
            if op.y is not None:
                entry["y"] = op.y
            front["openings"].append(entry)
        if f.timber is not None:
            front["timber"] = f.timber
        if f.infill is not None:
            front["infill"] = f.infill
        out["frontFacade"] = front
    if o.roof_cover is not None:
        out["roofCover"] = o.roof_cover
    if o.notes is not None:
        out["notes"] = o.notes
    if o.seed is not None:
        out["seed"] = o.seed
    if o.locked:
        out["locked"] = True
    if o.rueckbau is not None:
        out["rueckbau"] = o.rueckbau
    out.update(o.extra)
    return out


def validate_against(o: BuildingOverride, building: dict[str, Any]) -> list[str]:
    """Checks that need the building from buildings.json; returns problems (empty = fine)."""
    problems = []
    footprint = building.get("footprint") or []
    if o.front_facade is None:
        return problems
    f = o.front_facade
    if f.edge >= len(footprint):
        problems.append(
            f"frontFacade.edge {f.edge} does not exist (footprint has {len(footprint)} edges)"
        )
        return problems
    a, b = footprint[f.edge], footprint[(f.edge + 1) % len(footprint)]
    width = math.dist(a, b)
    for i, op in enumerate(f.openings):
        if op.x < 0 or op.x + op.w > width + 1e-6:
            problems.append(
                f"opening {i} ({op.type}) lies outside the facade (width {width:.2f} m)"
            )
    return problems


def path_for(directory: Path, building_id: str) -> Path:
    if not building_id or "/" in building_id or "\\" in building_id or building_id in (".", ".."):
        raise OverrideError(f"invalid building id '{building_id}'")
    return directory / f"{building_id}.json"


def load(path: Path) -> BuildingOverride:
    try:
        return from_json(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError) as e:
        raise OverrideError(f"{path}: {e}") from None


def save(directory: Path, o: BuildingOverride) -> Path:
    path = path_for(directory, o.id)
    directory.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(
        json.dumps(to_json(o), indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n"
    )
    tmp.replace(path)
    return path


def load_all(directory: Path) -> dict[str, BuildingOverride]:
    """All overrides of a site directory, by building id; the file name must match the id."""
    result: dict[str, BuildingOverride] = {}
    if not directory.is_dir():
        return result
    for path in sorted(directory.glob("*.json")):
        o = load(path)
        if path.stem != o.id:
            raise OverrideError(f"{path}: id '{o.id}' does not match the file name")
        result[o.id] = o
    return result
