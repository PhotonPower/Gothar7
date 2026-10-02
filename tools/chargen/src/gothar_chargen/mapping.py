"""Bone mappings source rig -> reference rig (data/mappings/<name>.toml).

Pure Python (tomllib, fnmatch): used by the Blender scripts and by tests.
"""

from __future__ import annotations

import fnmatch
import tomllib
from dataclasses import dataclass
from importlib import resources
from pathlib import Path


class MappingError(Exception):
    """The mapping file is malformed or does not fit the reference rig."""


@dataclass(frozen=True)
class BoneMap:
    name: str
    license: str
    bones: dict[str, str]
    ignore: tuple[str, ...]

    def is_ignored(self, source: str) -> bool:
        return any(fnmatch.fnmatchcase(source, p) for p in self.ignore)

    def target(self, source: str, reference: set[str]) -> str | None:
        """Reference bone for a source bone; None if it is ignored or unknown."""
        if self.is_ignored(source):
            return None
        if source in self.bones:
            return self.bones[source]
        return source if source in reference else None

    def resolve(self, sources: list[str], reference: set[str]) -> dict[str, str]:
        """Maps all source bones; raises if a reference bone would be filled twice."""
        result: dict[str, str] = {}
        used: dict[str, str] = {}
        for s in sources:
            t = self.target(s, reference)
            if t is None:
                continue
            if t not in reference:
                raise MappingError(f"{self.name}: '{s}' maps to unknown bone '{t}'")
            if t in used:
                raise MappingError(f"{self.name}: '{s}' and '{used[t]}' both map to '{t}'")
            used[t] = s
            result[s] = t
        return result


def parse_mapping(data: dict) -> BoneMap:
    source = data.get("source", {})
    bones = data.get("bones", {})
    if not isinstance(bones, dict) or not all(isinstance(v, str) for v in bones.values()):
        raise MappingError("[bones] must map source names to reference names")
    patterns = data.get("ignore", {}).get("patterns", [])
    return BoneMap(
        name=str(source.get("name", "")),
        license=str(source.get("license", "")),
        bones=dict(bones),
        ignore=tuple(patterns),
    )


def load_mapping(name_or_path: str | Path) -> BoneMap:
    """Loads a packaged mapping by name (e.g. ``quaternius_ual2``) or a .toml file."""
    path = Path(name_or_path)
    if path.suffix == ".toml":
        text = path.read_text(encoding="utf-8")
    else:
        res = resources.files("gothar_chargen.data.mappings").joinpath(f"{name_or_path}.toml")
        if not res.is_file():
            raise MappingError(f"unknown mapping '{name_or_path}'")
        text = res.read_text(encoding="utf-8")
    try:
        return parse_mapping(tomllib.loads(text))
    except tomllib.TOMLDecodeError as e:
        raise MappingError(str(e)) from e
