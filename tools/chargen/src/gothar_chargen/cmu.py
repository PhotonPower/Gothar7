"""CMU Graphics Lab Motion Capture Database (mocap.cs.cmu.edu) – ASF/AMC reader (F4 mocap).

Licence (quoted in assets/LICENSES.md): free for use, may be included in products, not to be resold
directly, even in converted form. Only the original ASF/AMC files of CMU are read (raw data below
DATA_ROOT/characters/cmu/raw, never in the repository); the clips are retargeted onto the reference
rig in Blender (``blender/cmu_retarget.py``) and land in the set .glb files like any other source.

Pure Python + numpy: skeleton (ASF), motion (AMC, 120 fps), forward kinematics. CMU world axes are
the glTF ones (Y up, +Z forward, +X the character's left); lengths in ASF units (``:units length``,
1 unit = 1/length inch) are converted to metres.
"""

from __future__ import annotations

import math
import tomllib
from dataclasses import dataclass, field
from importlib import resources
from pathlib import Path

import numpy as np

INCH = 0.0254
CMU_FPS = 120


@dataclass
class Bone:
    name: str
    direction: np.ndarray  # unit vector in the world at rest
    length: float  # metres
    axis: np.ndarray  # C: the bone's local frame at rest (3x3)
    dof: tuple[str, ...]
    parent: str | None = None
    children: list[str] = field(default_factory=list)


@dataclass
class Skeleton:
    bones: dict[str, Bone]
    root_order: tuple[str, ...]
    root_axis: np.ndarray

    def order(self) -> list[str]:
        """Bones parents first (root first)."""
        out, todo = [], ["root"]
        while todo:
            name = todo.pop(0)
            out.append(name)
            todo.extend(self.bones[name].children)
        return out


def euler(x: float, y: float, z: float) -> np.ndarray:
    """Rz @ Ry @ Rx from degrees (the ASF/AMC 'XYZ' order: X applied first)."""
    a, b, c = (math.radians(v) for v in (x, y, z))
    rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    rz = np.array([[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return rz @ ry @ rx


def parse_asf(text: str) -> Skeleton:
    scale = 1.0
    section = ""
    bones: dict[str, Bone] = {}
    root_order: tuple[str, ...] = ("TX", "TY", "TZ", "RX", "RY", "RZ")
    root_axis = np.eye(3)
    current: dict = {}
    lines = iter(text.splitlines())
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith(":"):
            section = line.split()[0]
            continue
        words = line.split()
        if section == ":units" and words[0] == "length":
            scale = INCH / float(words[1])
        elif section == ":root":
            if words[0] == "order":
                root_order = tuple(w.upper() for w in words[1:])
            elif words[0] == "axis":
                root_axis = np.eye(3)  # the CMU files use XYZ with zero orientation
        elif section == ":bonedata":
            if words[0] == "begin":
                current = {"dof": ()}
            elif words[0] == "end":
                c = euler(*current["axis"])
                bones[current["name"]] = Bone(
                    current["name"], np.array(current["direction"]),
                    current["length"] * scale, c, current["dof"],
                )  # fmt: skip
            elif words[0] == "name":
                current["name"] = words[1]
            elif words[0] == "direction":
                current["direction"] = [float(w) for w in words[1:4]]
            elif words[0] == "length":
                current["length"] = float(words[1])
            elif words[0] == "axis":
                current["axis"] = [float(w) for w in words[1:4]]
            elif words[0] == "dof":
                current["dof"] = tuple(words[1:])
        elif section == ":hierarchy":
            if words[0] in ("begin", "end"):
                continue
            parent, *kids = words
            for kid in kids:
                bones[kid].parent = parent
                if parent == "root":
                    continue
                bones[parent].children.append(kid)
    root = Bone("root", np.zeros(3), 0.0, root_axis, root_order, None)
    root.children = [b.name for b in bones.values() if b.parent == "root"]
    bones["root"] = root
    skeleton = Skeleton(bones, root_order, root_axis)
    skeleton.scale = scale  # type: ignore[attr-defined]
    return skeleton


def parse_amc(text: str) -> list[dict[str, list[float]]]:
    frames: list[dict[str, list[float]]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(("#", ":")):
            continue
        words = line.split()
        if len(words) == 1 and words[0].isdigit():
            frames.append({})
            continue
        if frames:
            frames[-1][words[0]] = [float(w) for w in words[1:]]
    return frames


@dataclass
class Pose:
    """One frame: world rotation (3x3) of every bone and the world position of its start (joint)
    and end, in metres."""

    rotation: dict[str, np.ndarray]
    start: dict[str, np.ndarray]
    end: dict[str, np.ndarray]


def forward(skeleton: Skeleton, frame: dict[str, list[float]]) -> Pose:
    scale = skeleton.scale  # type: ignore[attr-defined]
    rot: dict[str, np.ndarray] = {}
    start: dict[str, np.ndarray] = {}
    end: dict[str, np.ndarray] = {}
    values = dict(zip(skeleton.root_order, frame.get("root", [0.0] * 6), strict=False))
    rot["root"] = euler(values.get("RX", 0.0), values.get("RY", 0.0), values.get("RZ", 0.0))
    start["root"] = np.array([values.get(k, 0.0) for k in ("TX", "TY", "TZ")]) * scale
    end["root"] = start["root"]
    for name in skeleton.order()[1:]:
        bone = skeleton.bones[name]
        parent = bone.parent or "root"
        angles = dict(zip(bone.dof, frame.get(name, []), strict=False))
        local = euler(angles.get("rx", 0.0), angles.get("ry", 0.0), angles.get("rz", 0.0))
        c = bone.axis
        rot[name] = rot[parent] @ c @ local @ c.T
        start[name] = end[parent]
        end[name] = start[name] + rot[name] @ bone.direction * bone.length
    return Pose(rot, start, end)


def load_take(raw: Path, take: str) -> tuple[Skeleton, list[dict[str, list[float]]]]:
    """``take`` = '13_17' -> raw/13.asf + raw/13_17.amc."""
    subject = take.split("_")[0]
    asf = parse_asf((raw / f"{subject}.asf").read_text(encoding="latin-1"))
    return asf, parse_amc((raw / f"{take}.amc").read_text(encoding="latin-1"))


@dataclass(frozen=True)
class Segment:
    """A clip cut out of a take: ``name`` (action name in the source file), take, first and last
    frame at 120 fps (inclusive)."""

    name: str
    take: str
    start: int
    end: int


def load_segments() -> list[Segment]:
    text = resources.files("gothar_chargen.data").joinpath("cmu.toml").read_text("utf-8")
    data = tomllib.loads(text)
    return [
        Segment(name, *(s[k] for k in ("take", "start", "end")))
        for name, s in data["segment"].items()
    ]
