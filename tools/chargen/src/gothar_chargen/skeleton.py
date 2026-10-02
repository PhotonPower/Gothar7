"""Reference skeleton definition (data/human_reference.toml).

Pure Python (no numpy, no bpy): used by the validator and by the Blender generator.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from importlib import resources
from pathlib import Path

Vec3 = tuple[float, float, float]


class SkeletonError(Exception):
    """The skeleton definition file is malformed."""


@dataclass(frozen=True)
class BoneSpec:
    name: str
    parent: str | None
    socket: bool
    head: Vec3
    tail: Vec3
    up: Vec3


@dataclass(frozen=True)
class RigSpec:
    name: str
    height: float
    bind_pose: str
    max_bones: int
    max_influences: int
    morph_targets: tuple[str, ...]
    bones: tuple[BoneSpec, ...] = field(default_factory=tuple)

    def bone(self, name: str) -> BoneSpec:
        for b in self.bones:
            if b.name == name:
                return b
        raise KeyError(name)

    @property
    def names(self) -> list[str]:
        return [b.name for b in self.bones]

    @property
    def parents(self) -> dict[str, str | None]:
        return {b.name: b.parent for b in self.bones}

    @property
    def sockets(self) -> set[str]:
        return {b.name for b in self.bones if b.socket}


def _mirror_name(name: str) -> str:
    if name.endswith("_l"):
        return name[:-2] + "_r"
    return name


def _vec(value: object, where: str) -> Vec3:
    if not isinstance(value, list) or len(value) != 3:
        raise SkeletonError(f"{where}: expected a list of 3 numbers")
    x, y, z = (float(v) for v in value)
    return (x, y, z)


def parse_rig(data: dict) -> RigSpec:
    """Builds a RigSpec from parsed TOML, expanding ``mirror = true`` bones to their ``_r`` twin."""
    rig = data.get("rig")
    if not isinstance(rig, dict):
        raise SkeletonError("missing [rig] table")
    bones: list[BoneSpec] = []
    mirrored: list[BoneSpec] = []
    for i, raw in enumerate(data.get("bone", [])):
        where = f"bone #{i}"
        name = raw.get("name")
        if not isinstance(name, str) or not name:
            raise SkeletonError(f"{where}: missing name")
        where = f"bone '{name}'"
        spec = BoneSpec(
            name=name,
            parent=raw.get("parent"),
            socket=bool(raw.get("socket", False)),
            head=_vec(raw.get("head"), where + " head"),
            tail=_vec(raw.get("tail"), where + " tail"),
            up=_vec(raw.get("up"), where + " up"),
        )
        bones.append(spec)
        if raw.get("mirror", False):
            if not name.endswith("_l"):
                raise SkeletonError(f"{where}: mirror = true needs a name ending in _l")
            mirrored.append(
                BoneSpec(
                    name=_mirror_name(name),
                    parent=_mirror_name(spec.parent) if spec.parent else None,
                    socket=spec.socket,
                    head=(-spec.head[0], spec.head[1], spec.head[2]),
                    tail=(-spec.tail[0], spec.tail[1], spec.tail[2]),
                    up=(-spec.up[0], spec.up[1], spec.up[2]),
                )
            )
    # Mirrored bones go after all explicit ones: parents still precede children.
    all_bones = tuple(bones + mirrored)

    seen: set[str] = set()
    for b in all_bones:
        if b.name in seen:
            raise SkeletonError(f"duplicate bone '{b.name}'")
        if b.parent is not None and b.parent not in seen:
            raise SkeletonError(f"bone '{b.name}': parent '{b.parent}' must be defined before it")
        seen.add(b.name)
    roots = [b.name for b in all_bones if b.parent is None]
    if len(roots) != 1:
        raise SkeletonError(f"expected exactly one root bone, got {roots}")

    return RigSpec(
        name=str(rig.get("name", "")),
        height=float(rig.get("height", 1.8)),
        bind_pose=str(rig.get("bind_pose", "T")),
        max_bones=int(rig.get("max_bones", 128)),
        max_influences=int(rig.get("max_influences", 4)),
        morph_targets=tuple(rig.get("morph_targets", [])),
        bones=all_bones,
    )


def load_rig(path: Path | None = None) -> RigSpec:
    """Loads a rig definition; default is the packaged human reference skeleton."""
    if path is None:
        text = (
            resources.files("gothar_chargen.data")
            .joinpath("human_reference.toml")
            .read_text(encoding="utf-8")
        )
    else:
        text = path.read_text(encoding="utf-8")
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise SkeletonError(str(e)) from e
    return parse_rig(data)
