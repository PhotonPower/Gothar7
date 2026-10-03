"""Rig definitions: the human reference skeleton (data/human_reference.toml) and the monster
rigs (data/monsters/<species>.toml, characters-pipeline.md §7).

Pure Python (no numpy, no bpy): used by the validator and by the Blender scripts.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from importlib import resources
from pathlib import Path

from gothar_chargen.collision import Collision, CollisionError, parse_collision

Vec3 = tuple[float, float, float]

KINDS = ("human", "monster")
# Bones every monster rig must have (contract §7): the engine aims, attaches and bites with them.
MONSTER_REQUIRED = ("root", "pelvis", "neck_01", "head", "socket_mouth")
ORIENTATION_AXES = ("up", "forward", "left")
SPECIES_RE = re.compile(r"^[a-z][a-z0-9_]*$")


class SkeletonError(Exception):
    """The skeleton definition file is malformed."""


@dataclass(frozen=True)
class BoneSpec:
    name: str
    parent: str | None
    socket: bool
    head: Vec3
    tail: Vec3
    up: Vec3 | None = None  # align the bone's local Z axis to this direction ...
    roll: float | None = None  # ... or set the Blender bone roll (radians) directly


@dataclass(frozen=True)
class RigSpec:
    name: str
    height: float
    bind_pose: str
    max_bones: int
    max_influences: int
    morph_targets: tuple[str, ...]
    bones: tuple[BoneSpec, ...] = field(default_factory=tuple)
    kind: str = "human"
    species: str = ""  # monsters: clip mode and folder name (monsters/<species>/)
    # axis -> (bone a, bone b): the direction a -> b points mainly along the glTF axis
    # (up = +Y, forward = +Z, left = +X); empty for the human rig (fixed checks).
    orientation: tuple[tuple[str, tuple[str, str]], ...] = ()
    collision: Collision | None = None  # monsters: engine character capsule (§7.1)

    @property
    def is_monster(self) -> bool:
        return self.kind == "monster"

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
        if ("up" in raw) == ("roll" in raw):
            raise SkeletonError(f"{where}: needs exactly one of 'up' or 'roll'")
        spec = BoneSpec(
            name=name,
            parent=raw.get("parent"),
            socket=bool(raw.get("socket", False)),
            head=_vec(raw.get("head"), where + " head"),
            tail=_vec(raw.get("tail"), where + " tail"),
            up=_vec(raw["up"], where + " up") if "up" in raw else None,
            roll=float(raw["roll"]) if "roll" in raw else None,
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
                    up=(-spec.up[0], spec.up[1], spec.up[2]) if spec.up else None,
                    roll=-spec.roll if spec.roll is not None else None,
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

    kind = str(rig.get("kind", "human"))
    if kind not in KINDS:
        raise SkeletonError(f"kind must be one of {KINDS}, got '{kind}'")
    species = str(rig.get("species", ""))
    orientation: list[tuple[str, tuple[str, str]]] = []
    for axis, pair in rig.get("orientation", {}).items():
        if axis not in ORIENTATION_AXES:
            raise SkeletonError(f"orientation: unknown axis '{axis}'")
        if not isinstance(pair, list) or len(pair) != 2 or not set(pair) <= seen:
            raise SkeletonError(f"orientation.{axis}: expected two bones of this rig")
        orientation.append((axis, (str(pair[0]), str(pair[1]))))
    if kind == "monster":
        if not SPECIES_RE.match(species):
            raise SkeletonError(f"monster rig needs an ASCII species id, got '{species}'")
        missing = [b for b in MONSTER_REQUIRED if b not in seen]
        if missing:
            raise SkeletonError(f"monster rig lacks required bones {missing}")
        if {a for a, _ in orientation} != set(ORIENTATION_AXES):
            raise SkeletonError(f"monster rig needs [rig.orientation] {ORIENTATION_AXES}")
    collision = None
    if "collision" in rig:
        try:
            collision = parse_collision(rig["collision"])
        except CollisionError as e:
            raise SkeletonError(str(e)) from e
    elif kind == "monster":
        raise SkeletonError("monster rig needs [rig.collision] (gothar-chargen collision)")

    return RigSpec(
        name=str(rig.get("name", "")),
        height=float(rig.get("height", 1.8)),
        bind_pose=str(rig.get("bind_pose", "T")),
        max_bones=int(rig.get("max_bones", 128)),
        max_influences=int(rig.get("max_influences", 4)),
        morph_targets=tuple(rig.get("morph_targets", [])),
        bones=all_bones,
        kind=kind,
        species=species,
        orientation=tuple(orientation),
        collision=collision,
    )


def packaged_species() -> list[str]:
    """Monster species with a packaged rig (data/monsters/<species>.toml)."""
    folder = resources.files("gothar_chargen.data.monsters")
    return sorted(
        p.name[:-5]
        for p in folder.iterdir()
        if p.name.endswith(".toml") and not p.name.endswith(".build.toml")
    )


def monster_rig_text(species: str) -> str:
    res = resources.files("gothar_chargen.data.monsters").joinpath(f"{species}.toml")
    if not SPECIES_RE.match(species) or not res.is_file():
        raise SkeletonError(f"no monster rig '{species}' (data/monsters/<species>.toml)")
    return res.read_text(encoding="utf-8")


def species_of(path: Path) -> str | None:
    """``.../monsters/<species>/...`` -> species; None for human files."""
    parts = path.parts
    for i, part in enumerate(parts[:-1]):
        if part == "monsters" and i + 2 < len(parts):
            return parts[i + 1]
    return None


def load_rig(path: Path | None = None, species: str | None = None) -> RigSpec:
    """Loads a rig definition: a .toml file, a packaged monster rig, or the human reference."""
    if species is not None:
        text = monster_rig_text(species)
    elif path is None:
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
