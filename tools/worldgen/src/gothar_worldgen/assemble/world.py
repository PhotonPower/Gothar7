"""Assemble ``<site>.g7world`` from the terrain world and the building index.

Ownership rule (docs/design/leonberg-pipeline.md, W-G), so editor work survives new runs:
- The assembler owns exactly the vobs whose ids are listed in ``vob_ids.json`` (versioned).
  Everything else in the world file (editor vobs, their parents, other keys) is kept as it is.
- Owned building/cell/group vobs mirror the data and are rewritten on every run, except buildings
  whose override is ``locked``: their vob stays exactly as it is in the file.
- Start points are created once (if no start point of that name exists) and never rewritten.
- Owned vobs whose source disappeared are removed; their ids stay in ``vob_ids.json`` and are never
  used again. An owned group that still has editor children is kept.
- New ids come from max(nextVobId of the world, of ``vob_ids.json``, highest id + 1), so they never
  collide with ids the editor handed out (VobId contract, ADR 0005).
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from gothar_worldgen.export.starts import DEFAULT_STARTS, add_start_points
from gothar_worldgen.export.terrain import ExportError, world_text

IDS_FORMAT = "gothar-vob-ids"
IDS_VERSION = 1
ROOT_NAME = "WORLDGEN_BUILDINGS"
GROUP_CELL_M = 64.0
IDENTITY = [0.0, 0.0, 0.0, 1.0]


class AssembleError(Exception):
    """Inconsistent input; the message is meant for the user."""


@dataclass
class VobIds:
    """Stable assignment key -> VobId; keys are never removed, ids never reused."""

    ids: dict[str, int]
    next_id: int

    @classmethod
    def load(cls, path: Path) -> VobIds:
        if not path.is_file():
            return cls({}, 1)
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("format") != IDS_FORMAT or doc.get("version") != IDS_VERSION:
            raise AssembleError(f"{path.name}: not a {IDS_FORMAT} v{IDS_VERSION} file")
        ids = {str(k): int(v) for k, v in doc.get("ids", {}).items()}
        if len(set(ids.values())) != len(ids):
            raise AssembleError(f"{path.name}: an id is assigned twice")
        return cls(ids, max(int(doc.get("nextVobId", 1)), max(ids.values(), default=0) + 1))

    def save(self, path: Path) -> None:
        doc = {"format": IDS_FORMAT, "version": IDS_VERSION, "nextVobId": self.next_id,
               "ids": dict(sorted(self.ids.items(), key=lambda kv: kv[1]))}  # fmt: skip
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8", newline="\n")
        tmp.replace(path)

    def get(self, key: str, floor: int) -> int:
        """Id of ``key``; a new one (>= ``floor``) if the key is new."""
        if key not in self.ids:
            self.next_id = max(self.next_id, floor)
            self.ids[key] = self.next_id
            self.next_id += 1
        return self.ids[key]

    @property
    def owned(self) -> set[int]:
        return set(self.ids.values())


def _tidy(v: float) -> float:
    return round(float(v), 5) + 0.0


def _vob(vid: int, kind: str, name: str, pos: list[float], parent: int | None = None,
         rot: list[float] | None = None, mesh: str | None = None) -> dict[str, Any]:  # fmt: skip
    """Vob in the key order of the engine writer."""
    v: dict[str, Any] = {"id": vid, "type": kind, "name": name}
    if parent is not None:
        v["parent"] = parent
    v["pos"] = [_tidy(x) for x in pos]
    v["rot"] = rot or IDENTITY
    if mesh is not None:
        v["mesh"] = mesh
    return v


@dataclass
class AssembleResult:
    world: dict[str, Any]
    added: int = 0
    updated: int = 0
    removed: int = 0
    kept_locked: int = 0
    kept_editor: int = 0


def assemble(
    terrain_world: dict[str, Any],
    index: dict[str, Any],
    existing: dict[str, Any] | None,
    ids: VobIds,
    name: str,
    locked: frozenset[str] = frozenset(),
    ground: Any = None,  # noqa: ANN401  callable (x, z) -> y for start points
) -> AssembleResult:
    if "terrain" not in terrain_world:
        raise AssembleError("the terrain world has no terrain block (run export-terrain)")
    doc: dict[str, Any] = (
        dict(existing)
        if existing
        else {"version": 1, "name": name, "nextVobId": 1, "staticMeshes": []}
    )
    if doc.get("version") != 1:
        raise AssembleError("existing world file is not version 1")
    doc["terrain"] = terrain_world["terrain"]
    old = {int(v["id"]): v for v in doc.get("vobs", [])}
    owned = ids.owned
    floor = max([int(doc.get("nextVobId", 1)), ids.next_id] + [i + 1 for i in old])
    result = AssembleResult(doc)

    fresh: dict[int, dict[str, Any]] = {}
    root = ids.get("group:root", floor)
    fresh[root] = _vob(root, "empty", ROOT_NAME, [0.0, 0.0, 0.0])
    for e in index.get("entries", []):
        x, _, z = e.get("pos", [0.0, 0.0, 0.0])
        cell = (math.floor(x / GROUP_CELL_M), math.floor(z / GROUP_CELL_M))
        gkey = f"group:cell_{cell[0]}_{cell[1]}"
        gid = ids.get(gkey, floor)
        if gid not in fresh:
            fresh[gid] = _vob(
                gid, "empty", f"CELL_{cell[0]}_{cell[1]}".replace("-", "M"), [0, 0, 0], root
            )
        key = f"{e['kind']}:{e['id']}"
        vid = ids.get(key, floor)
        if e.get("locked") or e["id"] in locked:
            if vid in old:
                fresh[vid] = old[vid]
                result.kept_locked += 1
                continue
            if "pos" not in e:  # locked, never assembled before, no data: nothing to place
                continue
        vob_name = (
            f"BLD_{e['id']}"
            if e["kind"] == "building"
            else f"CELLMESH_{e['id']}".upper().replace("-", "M")
        )
        fresh[vid] = _vob(vid, "mesh", vob_name, e["pos"], gid, mesh=e["mesh"])

    # Owned groups that editor vobs still hang on survive even if empty of buildings.
    editor = {i: v for i, v in old.items() if i not in owned}
    for v in editor.values():
        p = v.get("parent")
        if p in old and p not in fresh and p in owned and old[p].get("type") == "empty":
            fresh[p] = old[p]
    vobs: dict[int, dict[str, Any]] = dict(editor)
    result.kept_editor = len(editor)
    for vid, v in fresh.items():
        if vid not in old:
            result.added += 1
        elif old[vid] != v:
            result.updated += 1
        vobs[vid] = v
    # Start points: owned by id, but only ever created, never rewritten.
    for vid, v in old.items():
        if vid in owned and v.get("type") == "start":
            vobs[vid] = v
    result.removed = sum(1 for vid in old if vid in owned and vid not in vobs)
    doc["vobs"] = [vobs[k] for k in sorted(vobs)]
    if ground is not None:
        result.added += add_start_points(
            doc, DEFAULT_STARTS, ground, known=lambda k: k in ids.ids, id_for=ids.get
        )
    doc["nextVobId"] = max(
        [int(doc.get("nextVobId", 1)), ids.next_id] + [int(v["id"]) + 1 for v in doc["vobs"]]
    )
    ids.next_id = max(ids.next_id, doc["nextVobId"])
    # Key order of the engine writer.
    ordered = {
        k: doc[k] for k in ("version", "name", "nextVobId", "staticMeshes", "terrain") if k in doc
    }
    ordered.update({k: v for k, v in doc.items() if k not in ordered})
    result.world = ordered
    return result


def load_world(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise AssembleError(f"{path.name}: invalid JSON ({e})") from None


def write_world(path: Path, doc: dict[str, Any]) -> None:
    try:
        text = world_text(doc)
    except ExportError as e:  # pragma: no cover - world_text raises nothing today
        raise AssembleError(str(e)) from None
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8", newline="\n")
    tmp.replace(path)
