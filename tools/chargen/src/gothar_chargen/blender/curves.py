"""Clip data in memory and operations on it (needs mathutils, runs inside Blender).

A clip is ``Curves``: {(data_path, array_index): [(frame, value), ...]} with frames from 0, the
data paths being ``pose.bones["<bone>"].rotation_quaternion`` (w, x, y, z) and, for root and
pelvis only, ``pose.bones["<bone>"].location``. Values are pose-bone (rest-relative) channels.
"""

from __future__ import annotations

import re

from mathutils import Quaternion, Vector  # type: ignore[import-not-found]

Curves = dict[tuple[str, int], list[tuple[float, float]]]
# A pose: bone -> (rotation, location or None); rotation/location are pose-bone channels.
Pose = dict[str, tuple[Quaternion, Vector | None]]

BONE_PATH = re.compile(r'^pose\.bones\["([^"]+)"\]\.(\w+)$')
TRANSLATED = ("root", "pelvis")


def rot_path(bone: str) -> str:
    return f'pose.bones["{bone}"].rotation_quaternion'


def loc_path(bone: str) -> str:
    return f'pose.bones["{bone}"].location'


def length(curves: Curves) -> float:
    return max((k[-1][0] for k in curves.values() if k), default=0.0)


def evaluate(keys: list[tuple[float, float]], frame: float) -> float:
    if frame <= keys[0][0]:
        return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:], strict=False):
        if frame <= f1:
            t = (frame - f0) / (f1 - f0) if f1 > f0 else 0.0
            return v0 + (v1 - v0) * t
    return keys[-1][1]


def reverse(curves: Curves) -> Curves:
    end = length(curves)
    return {key: sorted((end - f, v) for f, v in keys) for key, keys in curves.items()}


def concat(parts: list[Curves]) -> Curves:
    result: Curves = {}
    offset = 0.0
    for part in parts:
        for key, keys in part.items():
            dest = result.setdefault(key, [])
            dest.extend(
                (f + offset, v) for f, v in keys if not (dest and f + offset <= dest[-1][0])
            )
        offset += length(part) + 1.0
    return result


# --- poses -------------------------------------------------------------------------------------


def pose_at(curves: Curves, frame: float, bones: list[str]) -> Pose:
    """Pose of a clip at a (fractional) frame; bones without keys stay at rest."""
    pose: Pose = {}
    for bone in bones:
        rp = rot_path(bone)
        if (rp, 0) in curves:
            q = Quaternion([evaluate(curves[(rp, i)], frame) for i in range(4)])
            q.normalize()
        else:
            q = Quaternion()
        lp = loc_path(bone)
        loc = None
        if bone in TRANSLATED:
            loc = Vector(
                [evaluate(curves[(lp, i)], frame) if (lp, i) in curves else 0.0 for i in range(3)]
            )
        pose[bone] = (q, loc)
    return pose


def mix(a: Pose, b: Pose, w: float) -> Pose:
    """Per-bone slerp/lerp from pose a to pose b."""
    out: Pose = {}
    for bone, (qa, la) in a.items():
        qb, lb = b.get(bone, (Quaternion(), None))
        q = qa.slerp(qb, w)
        loc = la.lerp(lb, w) if la is not None and lb is not None else la
        out[bone] = (q, loc)
    return out


def to_curves(poses: list[Pose]) -> Curves:
    """Keys one pose per frame (0, 1, 2, ...), keeping quaternion signs continuous."""
    curves: Curves = {}
    previous: dict[str, Quaternion] = {}
    for frame, pose in enumerate(poses):
        for bone, (q, loc) in pose.items():
            q = q.copy()
            if bone in previous and previous[bone].dot(q) < 0:
                q.negate()
            previous[bone] = q
            for i in range(4):
                curves.setdefault((rot_path(bone), i), []).append((float(frame), q[i]))
            if loc is not None:
                for i in range(3):
                    curves.setdefault((loc_path(bone), i), []).append((float(frame), loc[i]))
    return curves


def blend(a: Curves, b: Curves, frames: int, bones: list[str]) -> Curves:
    """Cross-fade from clip a (looping) to clip b over `frames` frames (smoothstep weight)."""
    len_a = length(a) or 1.0
    poses = []
    for frame in range(frames + 1):
        t = frame / frames
        w = t * t * (3 - 2 * t)
        poses.append(mix(pose_at(a, frame % len_a, bones), pose_at(b, frame, bones), w))
    return to_curves(poses)
