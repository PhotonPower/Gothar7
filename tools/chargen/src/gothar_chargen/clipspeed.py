"""Natural ground speed of locomotion clips (characters-pipeline.md §3, field ``speed``).

Clips in place (humans, §3): the speed at which a planted foot moves backwards relative to the
figure. Clips with root motion (monsters, §7: ``root`` walks forward): the horizontal speed of
``root``. Played at this movement speed the feet do not slide. Only locomotion clips (LOCOMOTION)
get one. Pure Python + numpy (forward kinematics over the glTF skeleton), so the CI can check that
the numbers in ``<set>.events.toml`` match the clips.
"""

from __future__ import annotations

from collections.abc import Callable

import numpy as np

from gothar_chargen.gltf import Gltf

FPS = 30  # sampling rate (the frame rate of the clip contract)
CONTACT = 0.02  # metres above its lowest point a foot counts as planted
GROUND = (
    0.15  # metres: a planted foot must be this close to the ground (not swimming, not climbing)
)
MIN_SPEED = 0.15  # m/s: slower clips stand (idle, turning in place)
ROOT_MOTION = 0.01  # metres of root travel over a clip that make it a root motion clip
LOCOMOTION = (
    "s_walk",
    "s_trot",
    "s_run",
    "s_sneak",
    "s_strafe",
)  # clip name prefixes (after "<set>/")
HUMAN_FEET = ("ball_l", "ball_r")
ANIMAL_FEET = ("front_foot_l", "front_foot_r", "back_foot_l", "back_foot_r")


def _quat_to_matrix(q: np.ndarray) -> np.ndarray:
    """(..., 4) xyzw quaternions -> (..., 3, 3)."""
    q = q / np.linalg.norm(q, axis=-1, keepdims=True)
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    m = np.empty((*q.shape[:-1], 3, 3))
    m[..., 0, 0] = 1 - 2 * (y * y + z * z)
    m[..., 0, 1] = 2 * (x * y - z * w)
    m[..., 0, 2] = 2 * (x * z + y * w)
    m[..., 1, 0] = 2 * (x * y + z * w)
    m[..., 1, 1] = 1 - 2 * (x * x + z * z)
    m[..., 1, 2] = 2 * (y * z - x * w)
    m[..., 2, 0] = 2 * (x * z - y * w)
    m[..., 2, 1] = 2 * (y * z + x * w)
    m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def _sample(times: np.ndarray, values: np.ndarray, t: np.ndarray, rotation: bool) -> np.ndarray:
    """Linear (rotations: normalised linear) interpolation of keys at times t."""
    idx = np.clip(np.searchsorted(times, t, side="right") - 1, 0, len(times) - 1)
    nxt = np.minimum(idx + 1, len(times) - 1)
    span = np.where(nxt > idx, times[nxt] - times[idx], 1.0)
    a = np.clip((t - times[idx]) / span, 0.0, 1.0)[:, None]
    v0, v1 = values[idx], values[nxt]
    if rotation:
        v1 = np.where((np.sum(v0 * v1, axis=1) < 0)[:, None], -v1, v1)
        out = v0 * (1 - a) + v1 * a
        return out / np.linalg.norm(out, axis=1, keepdims=True)
    return v0 * (1 - a) + v1 * a


def _parents(gltf: Gltf) -> dict[int, int]:
    return {c: i for i, n in enumerate(gltf.doc.get("nodes", [])) for c in n.get("children", [])}


def duration(gltf: Gltf, animation: dict) -> float:
    return max(
        (float(np.asarray(gltf.accessor(s["input"])).max()) for s in animation["samplers"]),
        default=0.0,
    )


def global_matrices(gltf: Gltf, animation: dict, t: np.ndarray) -> Callable[[int], np.ndarray]:
    """Returns node index -> (len(t), 4, 4) global transforms of the animation at times t (s)."""
    nodes = gltf.doc["nodes"]
    parents = _parents(gltf)
    channels: dict[tuple[int, str], tuple[np.ndarray, np.ndarray]] = {}
    for ch in animation["channels"]:
        sampler = animation["samplers"][ch["sampler"]]
        times = np.asarray(gltf.accessor(sampler["input"]), dtype=np.float64).ravel()
        values = np.asarray(gltf.accessor(sampler["output"]), dtype=np.float64)
        target = ch["target"]
        channels[(target["node"], target["path"])] = (times, values.reshape(len(times), -1))
    cache: dict[int, np.ndarray] = {}

    def global_of(i: int) -> np.ndarray:
        if i in cache:
            return cache[i]
        node = nodes[i]
        trans = np.tile(
            np.asarray(node.get("translation", [0, 0, 0]), dtype=np.float64), (len(t), 1)
        )
        rot = np.tile(np.asarray(node.get("rotation", [0, 0, 0, 1]), dtype=np.float64), (len(t), 1))
        scale = np.tile(np.asarray(node.get("scale", [1, 1, 1]), dtype=np.float64), (len(t), 1))
        if (i, "translation") in channels:
            trans = _sample(*channels[(i, "translation")], t, rotation=False)
        if (i, "rotation") in channels:
            rot = _sample(*channels[(i, "rotation")], t, rotation=True)
        if (i, "scale") in channels:
            scale = _sample(*channels[(i, "scale")], t, rotation=False)
        local = np.zeros((len(t), 4, 4))
        local[:, :3, :3] = _quat_to_matrix(rot) * scale[:, None, :]
        local[:, :3, 3] = trans
        local[:, 3, 3] = 1.0
        out = global_of(parents[i]) @ local if i in parents else local
        cache[i] = out
        return out

    return global_of


def bone_paths(
    gltf: Gltf, animation: dict, bones: list[str], fps: int = FPS
) -> dict[str, np.ndarray]:
    """Positions (frames, 3) of the named bones in glTF scene space (y up) over the animation."""
    names = {n.get("name"): i for i, n in enumerate(gltf.doc["nodes"])}
    t = np.arange(int(round(duration(gltf, animation) * fps)) + 1) / fps
    global_of = global_matrices(gltf, animation, t)
    return {b: global_of(names[b])[:, :3, 3] for b in bones if b in names}


def feet_of(gltf: Gltf) -> list[str]:
    names = {n.get("name") for n in gltf.doc.get("nodes", [])}
    for feet in (HUMAN_FEET, ANIMAL_FEET):
        if set(feet) <= names:
            return list(feet)
    return []


def planted_speed(paths: dict[str, np.ndarray], fps: int = FPS) -> float | None:
    """Mean horizontal speed of the planted feet (None: no foot on the ground or standing)."""
    speeds = []
    for pos in paths.values():
        height = pos[:, 1]  # glTF: y up
        if height.min() > GROUND:
            continue
        planted = height <= height.min() + CONTACT
        both = planted[:-1] & planted[1:]
        if not both.any():
            continue
        step = np.linalg.norm(np.diff(pos[:, [0, 2]], axis=0), axis=1) * fps
        speeds.append(step[both])
    if not speeds:
        return None
    speed = float(np.concatenate(speeds).mean())
    return speed if speed >= MIN_SPEED else None


def clip_speeds(gltf: Gltf) -> dict[str, float]:
    """Natural speed (m/s, 2 decimals) of every locomotion clip of a set file."""
    feet = feet_of(gltf)  # clips in place need feet; root motion clips do not
    out: dict[str, float] = {}
    for animation in gltf.doc.get("animations", []):
        name = str(animation.get("name", ""))
        if not name.split("/")[-1].startswith(LOCOMOTION):
            continue
        paths = bone_paths(gltf, animation, [*feet, "root"])
        root = paths.pop("root", None)
        travel = None if root is None else root[-1, [0, 2]] - root[0, [0, 2]]
        if travel is not None and float(np.linalg.norm(travel)) > ROOT_MOTION:
            speed: float | None = float(np.linalg.norm(travel)) * FPS / max(1, len(root) - 1)
        elif feet:
            speed = planted_speed(paths)
        else:
            speed = None
        if speed is not None and speed >= MIN_SPEED:
            out[name] = round(speed, 2)
    return out
