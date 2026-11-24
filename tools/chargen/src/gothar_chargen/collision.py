"""Collision capsule of a monster species (contract §7.1, engine M5/M9): `[rig.collision]`.

The capsule is derived from the reference mesh: the torso without tail and lower legs (by the
skin weights), the snout just inside. Long animals get a lying capsule along +Z (facing
direction), others an upright one standing on the ground. Coordinates are rig space (glTF: Y up,
+Z forward), relative to `root` at the origin. Pure Python (numpy), no Blender.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from gothar_chargen.gltf import Gltf
from gothar_chargen.meshdata import mesh_data

SHAPES = ("capsule_upright", "capsule_lying")
LYING_ASPECT = 1.3  # torso longer than this times its height: lying capsule
# bones whose vertices stay outside the capsule: tail, lower legs and feet (quadrupeds, birds);
# arthropods (§7.5): feelers, eye stalks, the spread legs, pincer arms and the crystal carrier
_LOOSE = re.compile(
    r"^(tail_.*|.*_lower_[lr]|.*_foot_[lr]|calf_[lr]|foot_[lr]|antenna_.*|leg_\d+_upper_[lr]"
    r"|arm_upper_[lr]|claw_.*|eye_stalk_[lr]|crystals)$"
)


class CollisionError(Exception):
    """[rig.collision] is malformed or cannot be derived."""


@dataclass(frozen=True)
class Collision:
    shape: str
    radius: float  # metres
    length: float  # metres, whole capsule incl. both half spheres (upright: height)
    offset: tuple[float, float, float]  # capsule centre relative to root, rig space
    axis: str = "z"  # lying capsule along +Z (facing), or "x": crosswise (crab, §7.5)

    def to_toml(self) -> str:
        x, y, z = self.offset
        return (
            "[rig.collision]            # engine character capsule (§7.1), derived from the mesh\n"
            f'shape = "{self.shape}"\n'
            f"radius = {self.radius:.2f}\n"
            f"length = {self.length:.2f}\n"
            f"offset = [{x:.2f}, {y:.2f}, {z:.2f}]\n" + ('axis = "x"\n' if self.axis == "x" else "")
        )


def parse_collision(raw: object) -> Collision:
    if not isinstance(raw, dict):
        raise CollisionError("[rig.collision] must be a table")
    shape = raw.get("shape")
    if shape not in SHAPES:
        raise CollisionError(f"collision.shape must be one of {SHAPES}, got {shape!r}")
    radius, length, offset = raw.get("radius"), raw.get("length"), raw.get("offset")
    if not isinstance(radius, int | float) or radius <= 0:
        raise CollisionError("collision.radius must be > 0")
    if not isinstance(length, int | float) or length < 2 * radius:
        raise CollisionError("collision.length must be >= 2 * radius (it includes the caps)")
    if not isinstance(offset, list) or len(offset) != 3:
        raise CollisionError("collision.offset must be [x, y, z]")
    off = tuple(float(v) for v in offset)
    axis = raw.get("axis", "z")
    if axis not in ("z", "x") or (axis == "x" and shape != "capsule_lying"):
        raise CollisionError('collision.axis must be "z" or "x" (x only for capsule_lying)')
    centre = (off[0], off[1], off[2])
    return Collision(str(shape), float(radius), float(length), centre, str(axis))


def derive_collision(gltf: Gltf) -> Collision:
    """Capsule around the torso of the first skinned mesh (see module docstring)."""
    nodes = gltf.list("nodes")
    mesh_node = next((i for i, n in enumerate(nodes) if "mesh" in n and "skin" in n), None)
    if mesh_node is None:
        raise CollisionError("no skinned mesh to derive the collision capsule from")
    data = mesh_data(gltf, mesh_node)
    if data.joints is None or data.weights is None:
        raise CollisionError("mesh has no skin weights")
    dominant = data.joints[np.arange(len(data.joints)), data.weights.argmax(axis=1)]
    names = np.array([str(nodes[j].get("name", "")) for j in dominant])
    torso = data.positions[[not _LOOSE.match(n) for n in names]]
    if len(torso) == 0:
        raise CollisionError("no torso vertices (all weighted to tail/legs?)")
    lo, hi = torso.min(axis=0), torso.max(axis=0)
    width, height, depth = hi - lo
    if width > LYING_ASPECT * height and width > depth:  # wider than long: crosswise (crab)
        radius = max(height, depth) / 2.0
        length = max(width, 2.0 * radius)
        centre = ((lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0, (lo[2] + hi[2]) / 2.0)
        return Collision("capsule_lying", _r(radius), _r(length), _v(centre), "x")
    if depth > LYING_ASPECT * height:
        radius = max(height, width) / 2.0
        length = max(depth, 2.0 * radius)
        centre = (0.0, (lo[1] + hi[1]) / 2.0, (lo[2] + hi[2]) / 2.0)
        return Collision("capsule_lying", _r(radius), _r(length), _v(centre))
    top = float(data.positions[:, 1].max())  # upright: from the ground to the top of the head
    radius = min(width, depth) / 2.0
    centre = (0.0, top / 2.0, (lo[2] + hi[2]) / 2.0)
    return Collision("capsule_upright", _r(radius), _r(max(top, 2.0 * radius)), _v(centre))


def _r(v: float) -> float:
    return round(float(v), 2)


def _v(v: tuple[float, float, float]) -> tuple[float, float, float]:
    return (_r(v[0]), _r(v[1]), _r(v[2]))


_BLOCK = re.compile(r"^\[rig\.collision\][^\n]*\n(?:(?!\[)[^\n]*\n)*", re.MULTILINE)


def write_collision(rig_toml: Path, collision: Collision) -> None:
    """Replaces (or inserts before the first [[bone]]) the [rig.collision] table."""
    text = rig_toml.read_text(encoding="utf-8")
    block = collision.to_toml() + "\n"
    if _BLOCK.search(text):
        text = _BLOCK.sub(lambda _: block, text, count=1)
    else:
        i = text.find("[[bone]]")
        if i < 0:
            raise CollisionError(f"{rig_toml.name}: no [[bone]] table")
        text = text[:i] + block + text[i:]
    rig_toml.write_text(text, encoding="utf-8", newline="\n")
