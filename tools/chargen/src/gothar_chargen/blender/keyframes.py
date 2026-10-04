"""Keyframe recipes: placeholder clips built from code ("platzhalter-K" in animation-list.md).

Rough on purpose – they make every Prio-A state reachable until mocap replaces them (F4).
Poses are written as rotations about world axes of the T-pose (Blender: X = character's left,
-Y = forward, Z = up), applied per bone in the order listed; e.g. ``("Z", -90), ("X", -80)``
turns the left upper arm forward, then lifts it. Such an offset is converted to the bone's
local frame (conjugation with its rest orientation) and multiplied onto the pose channel.

Recipes: recipe(rig, params, clips) -> Curves; ``clips`` holds the earlier clips of the set.
"""

from __future__ import annotations

import math
from collections.abc import Callable

import bpy  # type: ignore[import-not-found]
from mathutils import Quaternion, Vector  # type: ignore[import-not-found]

from gothar_chargen.blender.common import FPS
from gothar_chargen.blender.curves import Curves, Pose, length, mix, pose_at, to_curves

Rotations = dict[str, list[tuple[str, float]]]
_AXES = {"X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0)), "Z": Vector((0, 0, 1))}


class RigInfo:
    """Rest orientations of the reference armature (armature space)."""

    def __init__(self, arm: bpy.types.Object) -> None:
        self.bones = [b.name for b in arm.data.bones]
        self.rest = {b.name: b.matrix_local.to_quaternion() for b in arm.data.bones}
        self._children = {b.name: [c.name for c in b.children_recursive] for b in arm.data.bones}

    def subtree(self, bone: str) -> set[str]:
        """`bone` and all bones below it."""
        if bone not in self._children:
            raise ValueError(f"unknown bone '{bone}'")
        return {bone, *self._children[bone]}

    def offset(self, bone: str, axis: str, degrees: float) -> Quaternion:
        """World-axis rotation (at rest) as a pose-channel offset of `bone`."""
        r = self.rest[bone]
        return r.inverted() @ Quaternion(_AXES[axis], math.radians(degrees)) @ r

    def rotate(self, pose: Pose, bone: str, rotations: list[tuple[str, float]]) -> None:
        q, loc = pose[bone]
        for axis, degrees in rotations:
            q = self.offset(bone, axis, degrees) @ q
        pose[bone] = (q, loc)

    def move(self, pose: Pose, bone: str, world: Vector) -> None:
        """Adds an armature-space offset to the location channel of root/pelvis."""
        q, loc = pose[bone]
        pose[bone] = (q, (loc or Vector()) + self.rest[bone].inverted() @ world)

    def scale_motion(self, pose: Pose, bone: str, factors: Vector) -> None:
        """Scales the armature-space location of root/pelvis per axis."""
        q, loc = pose[bone]
        if loc is None:
            return
        world = self.rest[bone] @ loc
        world = Vector(a * b for a, b in zip(world, factors, strict=True))
        pose[bone] = (q, self.rest[bone].inverted() @ world)

    def rest_pose(self) -> Pose:
        return {
            b: (Quaternion(), Vector() if b in ("root", "pelvis") else None) for b in self.bones
        }

    def pose_from(self, rotations: Rotations) -> Pose:
        pose = self.rest_pose()
        for bone, rots in rotations.items():
            self.rotate(pose, bone, rots)
        return pose


def mirror(rotations: Rotations) -> Rotations:
    """Mirror across the character's YZ plane: swap _l/_r, negate Y and Z rotations."""

    def side(name: str) -> str:
        if name.endswith("_l"):
            return name[:-2] + "_r"
        if name.endswith("_r"):
            return name[:-2] + "_l"
        return name

    out: Rotations = {}
    for bone, rots in rotations.items():
        out[side(bone)] = [(a, d if a == "X" else -d) for a, d in rots]
    return out


def _sign(params: dict) -> float:
    side = params.get("side", "l")
    if side not in ("l", "r"):
        raise ValueError(f"side must be 'l' or 'r', got {side!r}")
    return 1.0 if side == "l" else -1.0  # +Z rotation turns towards the character's left


def _smooth(t: float) -> float:
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


# --- locomotion --------------------------------------------------------------------------------


def strafe(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Walk cycle with the hips turned sideways and the torso turned back to the front."""
    base = clips[params["base"]]
    yaw = _sign(params) * float(params.get("yaw", 70))
    poses = []
    for frame in range(int(length(base)) + 1):
        pose = pose_at(base, frame, rig.bones)
        rig.rotate(pose, "pelvis", [("Z", yaw)])
        for spine in ("spine_01", "spine_02", "spine_03"):
            rig.rotate(pose, spine, [("Z", -yaw / 3)])
        poses.append(pose)
    return to_curves(poses)


def turn(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Turning on the spot: idle with a few damped walk steps and a body twist (no root motion)."""
    idle, walk = clips[params["idle"]], clips[params["walk"]]
    frames = int(params.get("frames", 30))
    yaw = _sign(params) * float(params.get("yaw", 30))
    step = float(params.get("step", 0.4))
    len_idle, len_walk = length(idle) or 1.0, length(walk) or 1.0
    poses = []
    for frame in range(frames + 1):
        s = math.sin(math.pi * frame / frames)
        pose = mix(
            pose_at(idle, frame % len_idle, rig.bones),
            pose_at(walk, frame % len_walk, rig.bones),
            step * s,
        )
        rig.rotate(pose, "pelvis", [("Z", yaw * s)])
        rig.rotate(pose, "spine_02", [("Z", 0.3 * yaw * s)])
        rig.rotate(pose, "head", [("Z", 0.4 * yaw * s)])
        poses.append(pose)
    return to_curves(poses)


def scale_root(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Root-motion clip stretched in time and height (e.g. climb 1 m -> 1.4 m)."""
    base = clips[params["base"]]
    time = float(params.get("time", 1.0))
    height = float(params["height"]) / float(params.get("base_height", 1.0))
    frames = round(length(base) * time)
    poses = []
    for frame in range(frames + 1):
        pose = pose_at(base, frame / time, rig.bones)
        rig.scale_motion(pose, "root", Vector((1.0, 1.0, height)))
        poses.append(pose)
    return to_curves(poses)


# --- ladder ------------------------------------------------------------------------------------

# Left hand high, right hand at chest height, right knee raised (mirrored for the other phase).
LADDER: Rotations = {
    "upperarm_l": [("Z", -90), ("X", -80)],
    "lowerarm_l": [("Z", -25)],
    "upperarm_r": [("Z", 90), ("X", -60)],
    "lowerarm_r": [("Z", 20)],
    "thigh_r": [("X", -65)],
    "calf_r": [("X", 80)],
    "foot_r": [("X", -15)],
    "thigh_l": [("X", -15)],
    "calf_l": [("X", 25)],
    "foot_l": [("X", -10)],
    "spine_01": [("X", 8)],
    "head": [("X", -10)],
}


def ladder(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Ladder climbing cycle: alternating limbs, root rises `rise` m per cycle (down: reversed)."""
    frames = int(params.get("frames", 40))
    rise = float(params.get("rise", 0.6))
    down = params.get("direction", "up") == "down"
    a, b = rig.pose_from(LADDER), rig.pose_from(mirror(LADDER))
    poses = []
    for frame in range(frames + 1):
        t = frame / frames
        phase = 1.0 - t if down else t
        pose = mix(a, b, (1 - math.cos(2 * math.pi * phase)) / 2)
        rig.move(pose, "root", Vector((0.0, 0.0, -rise * t if down else rise * t)))
        poses.append(pose)
    return to_curves(poses)


def ladder_on(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """From standing to the ladder pose, stepping `forward` m in and `up` m onto the first rung."""
    idle = rig_pose0(clips[params["idle"]], rig)
    frames = int(params.get("frames", 20))
    forward, up = float(params.get("forward", 0.3)), float(params.get("up", 0.2))
    target = rig.pose_from(LADDER)
    poses = []
    for frame in range(frames + 1):
        w = _smooth(frame / frames)
        pose = mix(idle, target, w)
        rig.move(pose, "root", Vector((0.0, -forward * w, up * w)))
        poses.append(pose)
    return to_curves(poses)


def ladder_off(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """From the ladder pose at the top onto the platform: up first, then forward, then standing."""
    idle = rig_pose0(clips[params["idle"]], rig)
    frames = int(params.get("frames", 30))
    forward, up = float(params.get("forward", 0.4)), float(params.get("up", 0.9))
    start = rig.pose_from(LADDER)
    poses = []
    for frame in range(frames + 1):
        t = frame / frames
        pose = mix(start, idle, _smooth(t))
        rig.move(pose, "root", Vector((0.0, -forward * _smooth(2 * t - 1), up * _smooth(1.6 * t))))
        poses.append(pose)
    return to_curves(poses)


def rig_pose0(curves: Curves, rig: RigInfo) -> Pose:
    return pose_at(curves, 0.0, rig.bones)


# --- swimming / diving -------------------------------------------------------------------------


def yaw_wave(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """A looping clip with a body twist to one side and back (turning in water, no root motion)."""
    base = clips[params["base"]]
    frames = int(params.get("frames", 30))
    yaw = _sign(params) * float(params.get("yaw", 35))
    len_base = length(base) or 1.0
    poses = []
    for frame in range(frames + 1):
        s = math.sin(math.pi * frame / frames)
        pose = pose_at(base, frame % len_base, rig.bones)
        rig.rotate(pose, "pelvis", [("Z", yaw * s)])
        rig.rotate(pose, "head", [("Z", 0.3 * yaw * s)])
        poses.append(pose)
    return to_curves(poses)


def pitch(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Clip tilted head-down by `angle` degrees, optionally slowed (`time`) and damped (`damp`)."""
    base = clips[params["base"]]
    angle = float(params.get("angle", 20))
    time = float(params.get("time", 1.0))
    damp = float(params.get("damp", 0.0))
    rest = pose_at(base, 0.0, rig.bones)
    poses = []
    for frame in range(round(length(base) * time) + 1):
        pose = pose_at(base, frame / time, rig.bones)
        if damp:
            pose = mix(pose, rest, damp)
        rig.rotate(pose, "pelvis", [("X", angle)])
        poses.append(pose)
    return to_curves(poses)


# --- sliding -----------------------------------------------------------------------------------

# Sliding down a slope standing: knees bent, left foot ahead, arms out for balance.
SLIDE: Rotations = {
    "thigh_l": [("X", -30)],
    "calf_l": [("X", 45)],
    "foot_l": [("X", -15)],
    "thigh_r": [("X", 15)],
    "calf_r": [("X", 25)],
    "foot_r": [("X", -40)],
    "spine_01": [("X", 12)],
    "spine_02": [("X", 6)],
    "upperarm_l": [("Y", 35)],
    "upperarm_r": [("Y", -35)],
    "lowerarm_l": [("Z", -25)],
    "lowerarm_r": [("Z", 25)],
}


def slide(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Slope-sliding loop: fixed balance pose with a slow sway."""
    frames = int(params.get("frames", 40))
    sway = float(params.get("sway", 4))
    drop = float(params.get("drop", 0.09))
    poses = []
    for frame in range(frames + 1):
        s = math.sin(2 * math.pi * frame / frames)
        pose = rig.pose_from(SLIDE)
        rig.move(pose, "pelvis", Vector((0.0, 0.0, -drop)))
        rig.rotate(pose, "spine_01", [("Y", sway * s)])
        rig.rotate(pose, "upperarm_l", [("Y", 1.5 * sway * s)])
        rig.rotate(pose, "upperarm_r", [("Y", 1.5 * sway * s)])
        poses.append(pose)
    return to_curves(poses)


# --- static poses from data ---------------------------------------------------------------------


def pose(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Static pose from `rotations` (bone -> [[axis, degrees], ...]) with slight breathing.

    Meant as the upper body of a weapon stance, layered over idle legs.
    """
    raw = params.get("rotations")
    if not isinstance(raw, dict):
        raise ValueError("pose needs a 'rotations' table")
    rotations: Rotations = {}
    for bone, rots in raw.items():
        if bone not in rig.rest:
            raise ValueError(f"pose: unknown bone '{bone}'")
        rotations[bone] = [(str(a), float(d)) for a, d in rots]
    frames = int(params.get("frames", 60))
    breathe = float(params.get("breathe", 1.5))
    poses = []
    for frame in range(frames + 1):
        current = rig.pose_from(rotations)
        rig.rotate(current, "spine_02", [("X", breathe * math.sin(2 * math.pi * frame / frames))])
        poses.append(current)
    return to_curves(poses)


# --- monsters: key poses and root motion (F5) ---------------------------------------------------


Offset = tuple[Quaternion, Vector, Quaternion]  # before the base pose, move, twist after it


def _offset_pose(rig: RigInfo, spec: dict) -> dict[str, Offset]:
    """Channel offsets of a pose: rotate = {bone = [[axis, deg], ...]},
    move = {root|pelvis = [x, y, z]}, twist = {bone = deg}.

    Axes are world axes at rest (X = left, -Y = forward, Z = up); moves are in metres. A twist
    turns the bone about its own length axis after the base pose and the rotation (forearm roll:
    the hand turns in place, e.g. thumb up to bring a bottle neck to the mouth).
    """
    unknown = set(spec) - {"rotate", "move", "twist"}
    if unknown:
        raise ValueError(f"keyposes: unknown pose keys {sorted(unknown)}")
    offsets: dict[str, Offset] = {}
    for bone, rots in spec.get("rotate", {}).items():
        if bone not in rig.rest:
            raise ValueError(f"keyposes: unknown bone '{bone}'")
        q = Quaternion()
        for axis, degrees in rots:
            q = rig.offset(bone, str(axis), float(degrees)) @ q
        offsets[bone] = (q, Vector(), Quaternion())
    for bone, world in spec.get("move", {}).items():
        if bone not in ("root", "pelvis"):
            raise ValueError(f"keyposes: only root and pelvis can move, not '{bone}'")
        q, _, tw = offsets.get(bone, (Quaternion(), Vector(), Quaternion()))
        offsets[bone] = (q, rig.rest[bone].inverted() @ Vector([float(v) for v in world]), tw)
    for bone, degrees in spec.get("twist", {}).items():
        if bone not in rig.rest:
            raise ValueError(f"keyposes: unknown bone '{bone}'")
        q, loc, _ = offsets.get(bone, (Quaternion(), Vector(), Quaternion()))
        offsets[bone] = (q, loc, Quaternion((0.0, 1.0, 0.0), math.radians(float(degrees))))
    return offsets


def hold(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """A frame of `base` held as a quiet loop with slight breathing (lying in bed, looking into
    an open chest). params: base, frame (default 0; -1 = last), frames (default 60)."""
    base = clips[params["base"]]
    at = float(params.get("frame", 0))
    if at < 0:
        at = length(base)
    frames = int(params.get("frames", 60))
    breathe = float(params.get("breathe", 1.0))
    poses = []
    for frame in range(frames + 1):
        pose = pose_at(base, at, rig.bones)
        if frames:  # frames = 0: a single frame (reference poses)
            rig.rotate(pose, "spine_02", [("X", breathe * math.sin(2 * math.pi * frame / frames))])
        poses.append(pose)
    return to_curves(poses)


def keyposes(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """Named poses at key frames, eased in between (smoothstep), over a looping `base` clip.

    params: poses = {name = {rotate = ..., move = ..., twist = ...}}, keys = [[frame, name], ...]
    (first key at frame 0, "rest" = no offset), optional base = earlier clip (default: rest pose).
    Offsets are multiplied onto the base pose, so a pose on top of idle still breathes.
    """
    named = {"rest": {}}
    for name, spec in params.get("poses", {}).items():
        named[name] = _offset_pose(rig, spec)
    keys = [(int(f), str(n)) for f, n in params.get("keys", [])]
    if (
        len(keys) < 2
        or keys[0][0] != 0
        or any(b[0] <= a[0] for a, b in zip(keys, keys[1:], strict=False))
    ):
        raise ValueError("keyposes: keys need >= 2 entries with increasing frames from 0")
    for _, name in keys:
        if name not in named:
            raise ValueError(f"keyposes: unknown pose '{name}'")
    base = clips[params["base"]] if "base" in params else None
    len_base = (length(base) or 1.0) if base is not None else 1.0
    poses = []
    for frame in range(keys[-1][0] + 1):
        i = max(k for k in range(len(keys)) if keys[k][0] <= frame)
        j = min(i + 1, len(keys) - 1)
        span = keys[j][0] - keys[i][0]
        s = _smooth((frame - keys[i][0]) / span) if span else 0.0
        a, b = named[keys[i][1]], named[keys[j][1]]
        pose = pose_at(base, frame % len_base, rig.bones) if base is not None else rig.rest_pose()
        for bone in set(a) | set(b):
            qa, la, ta = a.get(bone, (Quaternion(), Vector(), Quaternion()))
            qb, lb, tb = b.get(bone, (Quaternion(), Vector(), Quaternion()))
            q, loc = pose[bone]
            pose[bone] = (
                qa.slerp(qb, s) @ q @ ta.slerp(tb, s),
                None if loc is None else loc + la.lerp(lb, s),
            )
        poses.append(pose)
    return to_curves(poses)


def _amplified(q: Quaternion, factor: float) -> Quaternion:
    axis, angle = q.to_axis_angle()
    return Quaternion(axis, angle * factor)


def advance(rig: RigInfo, params: dict, clips: dict[str, Curves]) -> Curves:
    """In-place locomotion loop with forward root motion (monster contract §7).

    params: base (in-place loop), speed (m/s forward), time (< 1 plays faster), amplify (scales
    every joint's rotation away from rest, e.g. 1.25 for longer strides when running).
    """
    base = clips[params["base"]]
    speed = float(params["speed"])
    time = float(params.get("time", 1.0))
    amplify = float(params.get("amplify", 1.0))
    frames = round(length(base) * time)
    poses = []
    for frame in range(frames + 1):
        pose = pose_at(base, frame / time, rig.bones)
        if amplify != 1.0:
            pose = {b: (_amplified(q, amplify), loc) for b, (q, loc) in pose.items()}
        rig.move(pose, "root", Vector((0.0, -speed * frame / FPS, 0.0)))
        poses.append(pose)
    return to_curves(poses)


RECIPES: dict[str, Callable[[RigInfo, dict, dict[str, Curves]], Curves]] = {
    "strafe": strafe,
    "turn": turn,
    "hold": hold,
    "scale_root": scale_root,
    "ladder": ladder,
    "ladder_on": ladder_on,
    "ladder_off": ladder_off,
    "yaw_wave": yaw_wave,
    "pitch": pitch,
    "slide": slide,
    "pose": pose,
    "keyposes": keyposes,
    "advance": advance,
}
