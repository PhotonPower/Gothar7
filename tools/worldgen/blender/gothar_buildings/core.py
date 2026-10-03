"""Logic of the Blender add-on without bpy (tested with pytest; runs in Blender's Python 3.11).

The add-on brings generated building ``.glb`` files into Blender for hand work and writes them back.
Writing back marks the building ``locked`` in its override, so ``gothar-worldgen buildings`` and
``assemble`` keep the hand-made file and vob (leonberg-pipeline.md, W-C/W-G).
"""

from __future__ import annotations

import json
import os
from pathlib import Path


def gltf_to_blender(x: float, y: float, z: float) -> tuple[float, float, float]:
    """Engine/glTF (+Y up, -Z north) -> Blender (+Z up, +Y north)."""
    return (x, -z, y)


def blender_to_gltf(x: float, y: float, z: float) -> tuple[float, float, float]:
    return (x, z, -y)


class Site:
    """Paths of one site, derived from its ``buildings_index.json``.

    ``assets/source/worlds/<site>/generated/buildings_index.json`` -> repository root, VFS root
    (``assets/source``) and the override folder ``tools/worldgen/data/<site>/buildings``.
    """

    def __init__(self, index_path: Path) -> None:
        self.index_path = Path(index_path)
        generated = self.index_path.parent
        self.name = generated.parent.name
        self.vfs_root = generated.parents[2]  # .../assets/source
        repo = generated.parents[4]
        self.overrides = repo / "tools" / "worldgen" / "data" / self.name / "buildings"
        doc = json.loads(self.index_path.read_text(encoding="utf-8"))
        self.entries = {e["id"]: e for e in doc.get("entries", []) if e.get("kind") == "building"}

    def mesh_file(self, building_id: str) -> Path:
        return self.vfs_root / self.entries[building_id]["mesh"]

    def nearby(self, x: float, z: float, radius: float) -> list[str]:
        """Building ids whose origin lies within ``radius`` metres of local (x, z)."""
        out = []
        for bid, e in self.entries.items():
            pos = e.get("pos")
            if pos and (pos[0] - x) ** 2 + (pos[2] - z) ** 2 <= radius**2:
                out.append(bid)
        return sorted(out)


def mark_locked(overrides_dir: Path, building_id: str, note: str = "") -> Path:
    """Sets ``locked: true`` in the building's override (creates a minimal one if needed).

    Keeps all other keys; writes the documented key order for new files.
    """
    if not building_id or any(c in building_id for c in "/\\") or building_id in (".", ".."):
        raise ValueError(f"invalid building id {building_id!r}")
    path = Path(overrides_dir) / f"{building_id}.json"
    doc = (
        json.loads(path.read_text(encoding="utf-8"))
        if path.is_file()
        else {"id": building_id, "keep": True}
    )
    if doc.get("id") != building_id:
        raise ValueError(f"{path.name}: id {doc.get('id')!r} does not match")
    doc["locked"] = True
    if note and not doc.get("notes"):
        doc["notes"] = note
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(
        json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n"
    )
    os.replace(tmp, path)
    return path
