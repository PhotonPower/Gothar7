"""Checks and repairs of clips against a reference mesh (M6 review of the creatures, F5).

- **Feet that stay planted** (root motion clips, monsters §7): the root must travel at the speed at
  which the planted feet move backwards relative to it, otherwise they slide. ``fit_root_speed``
  scales the horizontal root travel of ``s_walk*``/``s_run*`` to that stride speed.
- **Above the ground**: in lying and resting poses (``t_die*``, ``s_sleep*``: GROUNDED) the skinned
  reference mesh must not sink below the ground (y = 0). ``lift_to_ground`` raises ``pelvis`` key
  by key just enough. Other clips are not checked: toes dip a little when rolling, jumps and
  ladders leave the ground by design.

Pure Python + numpy; the validator runs the checks (``anim.slide``, ``anim.ground``).
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from gothar_chargen.clipspeed import (
    FPS,
    LOCOMOTION,
    duration,
    feet_of,
    global_matrices,
)
from gothar_chargen.gltf import Gltf

GROUND_TOLERANCE = 0.02  # metres the mesh may dip below the ground
SLIDE_TOLERANCE = 0.1  # m/s a planted foot may move forwards/backwards in the world
WATER_SETS = ("swim", "dive")  # sets played in water: no ground
GROUNDED = ("t_die", "s_sleep")  # clip name prefixes of poses lying on the ground


def reference_mesh_for(set_glb: Path) -> Path | None:
    """The skinned reference of a clip set: monsters/<art>/rig/<art>_reference.glb for
    monsters/<art>/anims/*.glb, rig/human_reference.glb for anims/human/*.glb."""
    if set_glb.parent.name != "anims" and set_glb.parent.parent.name != "anims":
        return None
    if set_glb.parent.name == "anims":  # monsters/<art>/anims/<art>.glb
        art = set_glb.parent.parent
        found = sorted((art / "rig").glob("*_reference.glb"))
        return found[0] if found else None
    candidate = set_glb.parent.parent.parent / "rig" / "human_reference.glb"
    return candidate if candidate.is_file() else None


class _Skin:
    """Reference mesh with its joints by name, for skinning poses of a clip file."""

    def __init__(self, ref: Gltf) -> None:
        skin = ref.doc["skins"][0]
        self.joints = [ref.doc["nodes"][j]["name"] for j in skin["joints"]]
        self.inverse_bind = np.asarray(ref.accessor(skin["inverseBindMatrices"]), dtype=np.float64)
        self.prims = []
        for mesh in ref.doc.get("meshes", []):
            for p in mesh["primitives"]:
                a = p["attributes"]
                if "JOINTS_0" not in a:
                    continue
                pos = np.asarray(ref.accessor(a["POSITION"]), dtype=np.float64)
                self.prims.append(
                    (
                        np.c_[pos, np.ones(len(pos))],
                        np.asarray(ref.accessor(a["JOINTS_0"]), dtype=np.int64),
                        np.asarray(ref.accessor(a["WEIGHTS_0"]), dtype=np.float64),
                    )
                )

    def lowest(self, clips: Gltf, animation: dict, t: np.ndarray) -> np.ndarray:
        """Lowest point (y) of the skinned mesh at each time."""
        names = {n.get("name"): i for i, n in enumerate(clips.doc["nodes"])}
        global_of = global_matrices(clips, animation, t)
        mats = np.stack(
            [global_of(names[j]) @ self.inverse_bind[k] for k, j in enumerate(self.joints)], axis=1
        )  # (times, joints, 4, 4)
        low = np.full(len(t), np.inf)
        for hp, joints, weights in self.prims:
            y = np.zeros((len(t), len(hp)))
            for q in range(4):
                m = mats[:, joints[:, q]]  # (times, verts, 4, 4)
                y += weights[:, q] * np.einsum("tvj,vj->tv", m[:, :, 1, :], hp)
            low = np.minimum(low, y.min(axis=1))
        return low


def _grounded(clips: Gltf) -> list[dict]:
    return [
        a
        for a in clips.doc.get("animations", [])
        if str(a.get("name", "")).split("/")[-1].startswith(GROUNDED)
    ]


def _times(clips: Gltf, animation: dict) -> np.ndarray:
    return np.arange(int(round(duration(clips, animation) * FPS)) + 1) / FPS


STANCE_CONTACT = 0.02  # metres above its lowest point a foot counts as standing


def _is_root_motion_locomotion(clips: Gltf, animation: dict) -> bool:
    name = str(animation.get("name", "")).split("/")[-1]
    if not name.startswith(LOCOMOTION):
        return False
    root = [n for n, x in enumerate(clips.doc["nodes"]) if x.get("name") == "root"]
    return bool(root) and any(
        c["target"]["node"] == root[0] and c["target"]["path"] == "translation"
        for c in animation["channels"]
    )


def foot_slide(clips: Gltf, animation: dict) -> tuple[float, float]:
    """(root speed, stride speed) in m/s along the root's travel: the stride speed is how fast the
    feet move backwards relative to the root while they stand (equal when nothing slides)."""
    names = {n.get("name"): i for i, n in enumerate(clips.doc["nodes"])}
    t = _times(clips, animation)
    global_of = global_matrices(clips, animation, t)
    root = global_of(names["root"])[:, :3, 3]
    travel = root[-1, [0, 2]] - root[0, [0, 2]]
    dist = float(np.linalg.norm(travel))
    if dist < 1e-6:
        return 0.0, 0.0
    direction = travel / dist
    rel = []
    for foot in feet_of(clips) or [n for n in names if n in ("foot_l", "foot_r")]:
        p = global_of(names[foot])[:, :3, 3]
        back = np.diff((p - root)[:, [0, 2]] @ direction) * FPS  # relative to the root
        h = p[:, 1]
        # stance: the foot moves backwards relative to the root and is on the ground at both
        # ends of the step (lift-off and touch-down frames would mix in the swing; fast gaits
        # stand only two or three frames)
        low = h <= h.min() + STANCE_CONTACT
        stance = (back < 0) & low[:-1] & low[1:]
        if stance.any():
            rel.append(-back[stance])
    stride = float(np.concatenate(rel).mean()) if rel else 0.0
    return dist / float(t[-1]), stride


def _channel(clips: Gltf, animation: dict, bone: str, path: str) -> tuple[int, int] | None:
    """(input accessor, output accessor) of the channel, or None."""
    node = next((i for i, n in enumerate(clips.doc["nodes"]) if n.get("name") == bone), None)
    for c in animation["channels"]:
        if c["target"].get("node") == node and c["target"]["path"] == path:
            s = animation["samplers"][c["sampler"]]
            return s["input"], s["output"]
    return None


def fit_root_speed(clips: Gltf, animation: dict) -> float | None:
    """Scales the horizontal root travel so that the planted feet stand; returns the factor."""
    if not _is_root_motion_locomotion(clips, animation):
        return None
    speed, stride = foot_slide(clips, animation)
    if speed <= 0 or stride <= 0:
        return None
    k = stride / speed
    channel = _channel(clips, animation, "root", "translation")
    assert channel is not None
    keys = np.asarray(clips.accessor(channel[1]), dtype=np.float64).copy()
    keys[:, [0, 2]] = keys[0, [0, 2]] + (keys[:, [0, 2]] - keys[0, [0, 2]]) * k
    clips.set_accessor(channel[1], keys)
    return k


def lift_to_ground(clips: Gltf, animation: dict, skin: _Skin) -> float:
    """Raises pelvis at each of its keys so the skinned mesh stays above the ground; returns the
    largest lift in metres (0: nothing to do)."""
    channel = _channel(clips, animation, "pelvis", "translation")
    if channel is None:
        return 0.0
    key_times = np.asarray(clips.accessor(channel[0]), dtype=np.float64).ravel()
    low = skin.lowest(clips, animation, key_times)
    lift = np.maximum(0.0, -low)
    if not lift.any():
        return 0.0
    names = {n.get("name"): i for i, n in enumerate(clips.doc["nodes"])}
    parent = {c: i for i, n in enumerate(clips.doc["nodes"]) for c in n.get("children", [])}
    pelvis = names["pelvis"]
    parent_global = (
        global_matrices(clips, animation, key_times)(parent[pelvis])
        if pelvis in parent
        else np.tile(np.eye(4), (len(key_times), 1, 1))
    )
    keys = np.asarray(clips.accessor(channel[1]), dtype=np.float64).copy()
    for i, up in enumerate(lift):  # world up -> pelvis translation in its parent's space
        keys[i] += np.linalg.solve(parent_global[i][:3, :3], np.array([0.0, up, 0.0]))
    clips.set_accessor(channel[1], keys)
    return float(lift.max())


def repair_set(set_glb: Path) -> list[str]:
    """Fits the root speed of root motion locomotion clips and lifts sinking clips; writes the
    file when something changed. Returns what was done."""
    reference = reference_mesh_for(set_glb)
    clips = Gltf.load(set_glb)
    done = []
    for animation in clips.doc.get("animations", []):
        k = fit_root_speed(clips, animation)
        if k is not None and abs(k - 1.0) > 0.01:
            done.append(f"{animation['name']}: root speed x{k:.2f}")
    if reference is not None and set_glb.stem not in WATER_SETS:
        skin = _Skin(Gltf.load(reference))
        for animation in _grounded(clips):
            lift = lift_to_ground(clips, animation, skin)
            if lift > 0.001:
                done.append(f"{animation['name']}: lifted {lift * 100:.1f} cm")
    if done:
        set_glb.write_bytes(clips.to_bytes())
    return done


def check_set(set_glb: Path, clips: Gltf) -> list[tuple[str, str]]:
    """(code, message) for clips whose feet slide or whose mesh sinks below the ground."""
    issues = []
    for animation in clips.doc.get("animations", []):
        if _is_root_motion_locomotion(clips, animation):
            speed, stride = foot_slide(clips, animation)
            if stride > 0 and abs(speed - stride) > SLIDE_TOLERANCE:
                issues.append(
                    (
                        "anim.slide",
                        f"{animation['name']}: root {speed:.2f} m/s, stride {stride:.2f} m/s – the "
                        "feet slide (gothar-chargen repair-clips)",
                    )
                )
    reference = reference_mesh_for(set_glb)
    if reference is None or set_glb.stem in WATER_SETS:
        return issues
    skin = _Skin(Gltf.load(reference))
    for animation in _grounded(clips):
        low = float(skin.lowest(clips, animation, _times(clips, animation)).min())
        if low < -GROUND_TOLERANCE:
            issues.append(
                (
                    "anim.ground",
                    f"{animation['name']}: the mesh sinks {-low * 100:.1f} cm below the ground "
                    "(gothar-chargen repair-clips)",
                )
            )
    return issues
