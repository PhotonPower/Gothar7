"""Gait cycles for animals (F5): walk, trot and gallop as data, legs solved with planar IK.

A gait is one loop of ``period`` frames. The root moves forward at ``speed`` m/s (monster contract
§7: root motion); every leg touches the ground at its ``phase`` (fraction of the cycle) and stays
there for ``duty`` of the cycle – planted in the world while the body passes over it, so the feet
do not slide at exactly this speed. In the rest of the cycle the foot swings forward in an arc
``lift`` m high. The stride under the body is ``speed * duty * period / 30`` m, centred ``reach``
m ahead of the point below the hip. Upper and lower leg are solved in the side plane (two-bone
IK, the knee bends as at rest); the foot stays flat while planted and curls by ``curl`` degrees
in the swing. Choose ``period`` so that every ``phase * period`` and ``duty * period`` is a
whole frame: touch-down and lift-off then fall on frames (no half-planted frames).
Body motion on top: ``bob`` (m, ``bob_cycles`` per loop), ``pitch`` of the pelvis
and ``flex`` of a spine bone (degrees, gallop), ``nod`` of the neck and ``tail`` swing.

Pure Python (math only), so the parameters and the leg solution are tested without Blender; the
recipe ``gait`` in ``blender/keyframes.py`` applies it to the rig.

Data (``[clip.params]`` of a ``keyframe = "gait"`` clip)::

    speed = 6.0                 # m/s: root motion = the clip's natural speed
    period = 10                 # frames per cycle (30 fps)
    duty = 0.3                  # share of the cycle a foot is planted
    lift = 0.08                 # m: swing height of the foot
    legs = [                    # touch-down phase, optional reach (m forward) and curl (deg)
        { upper = "back_upper_l", lower = "back_lower_l", foot = "back_foot_l", phase = 0.0 },
        ...
    ]
    bob = 0.03                  # m up and down (optional)
    bob_cycles = 2              # 2: walk, trot (every footfall pair); 1: gallop
    crouch = 0.04               # m body lowered: straight legs reach further (optional)
    pitch = 4.0                 # deg pelvis rocking (optional)
    flex = { bone = "spine_01", degrees = 12.0, phase = 0.25 }   # spine flexion (optional)
    nod = { bone = "neck_01", degrees = 4.0, phase = 0.0 }       # neck counter motion (optional)
    tail = { bones = ["tail_01", "tail_02"], degrees = 8.0, lag = 0.1 }  # tail swing (optional)
"""

from __future__ import annotations

import math
from dataclasses import dataclass

FPS = 30
KEYS = {
    "speed",
    "period",
    "duty",
    "lift",
    "legs",
    "bob",
    "crouch",
    "bob_cycles",
    "pitch",
    "flex",
    "nod",
    "tail",
}
LEG_KEYS = {"upper", "lower", "foot", "phase", "reach", "curl"}
REACH_MARGIN = 0.995  # a leg is never stretched beyond this share of its length (knee stays bent)


class GaitError(ValueError):
    """Malformed gait parameters."""


@dataclass(frozen=True)
class Leg:
    upper: str
    lower: str
    foot: str
    phase: float  # touch-down, fraction of the cycle
    reach: float = 0.0  # m: stride centre ahead of the point below the hip
    curl: float = 0.0  # deg: foot rotation in the swing (about the left axis)


@dataclass(frozen=True)
class Wave:
    bones: tuple[str, ...]
    degrees: float
    phase: float = 0.0  # fraction of the cycle; tail: lag per bone


@dataclass(frozen=True)
class Gait:
    speed: float
    period: int
    duty: float
    lift: float
    legs: tuple[Leg, ...]
    bob: float = 0.0
    bob_cycles: int = 2
    crouch: float = 0.0  # m: body lowered (straight legs reach further forward and back)
    pitch: float = 0.0
    flex: Wave | None = None
    nod: Wave | None = None
    tail: Wave | None = None

    @property
    def stride(self) -> float:
        """Metres a planted foot travels backwards under the body."""
        return self.speed * self.duty * self.period / FPS


def _number(data: dict, key: str, lo: float, hi: float, default: float | None = None) -> float:
    value = data.get(key, default)
    if not isinstance(value, int | float) or isinstance(value, bool) or not lo <= value <= hi:
        raise GaitError(f"gait: {key} must be a number in {lo}..{hi}")
    return float(value)


def _wave(data: object, key: str, many: bool = False) -> Wave | None:
    if data is None:
        return None
    if not isinstance(data, dict):
        raise GaitError(f"gait: {key} must be a table")
    bones = data.get("bones" if many else "bone")
    names = bones if many else [bones]
    if not isinstance(names, list) or not names or not all(isinstance(b, str) for b in names):
        raise GaitError(f"gait: {key} needs {'bones (list)' if many else 'bone'}")
    return Wave(
        tuple(names),
        _number(data, "degrees", -45.0, 45.0),
        _number(data, "lag" if many else "phase", -1.0, 1.0, 0.0),
    )


def parse_gait(params: dict) -> Gait:
    unknown = set(params) - KEYS - {"base"}
    if unknown:
        raise GaitError(f"gait: unknown keys {sorted(unknown)}")
    legs_raw = params.get("legs")
    if not isinstance(legs_raw, list) or not 2 <= len(legs_raw) <= 4:
        raise GaitError("gait: legs must list 2 to 4 legs")
    legs = []
    for leg in legs_raw:
        if not isinstance(leg, dict) or set(leg) - LEG_KEYS:
            raise GaitError(f"gait: leg keys are {sorted(LEG_KEYS)}")
        names = [leg.get(k) for k in ("upper", "lower", "foot")]
        if not all(isinstance(n, str) and n for n in names):
            raise GaitError("gait: every leg needs upper, lower and foot bones")
        legs.append(
            Leg(
                names[0],
                names[1],
                names[2],
                _number(leg, "phase", 0.0, 1.0),
                _number(leg, "reach", -0.5, 0.5, 0.0),
                _number(leg, "curl", -120.0, 120.0, 0.0),
            )
        )
    period = params.get("period")
    if not isinstance(period, int) or isinstance(period, bool) or not 4 <= period <= 120:
        raise GaitError("gait: period must be an integer in 4..120 frames")
    cycles = params.get("bob_cycles", 2)
    if cycles not in (1, 2):
        raise GaitError("gait: bob_cycles must be 1 or 2")
    return Gait(
        speed=_number(params, "speed", 0.1, 20.0),
        period=period,
        duty=_number(params, "duty", 0.1, 0.9),
        lift=_number(params, "lift", 0.0, 0.5),
        legs=tuple(legs),
        bob=_number(params, "bob", 0.0, 0.2, 0.0),
        bob_cycles=int(cycles),
        crouch=_number(params, "crouch", 0.0, 0.3, 0.0),
        pitch=_number(params, "pitch", -30.0, 30.0, 0.0),
        flex=_wave(params.get("flex"), "flex"),
        nod=_wave(params.get("nod"), "nod"),
        tail=_wave(params.get("tail"), "tail", many=True),
    )


def foot_offset(gait: Gait, leg: Leg, frame: float) -> tuple[float, float, bool]:
    """Foot target relative to its stride centre: (forward m, up m, planted). Planted, it moves
    backwards at exactly ``speed``; in the swing it comes forward in a smooth arc."""
    t = (frame / gait.period - leg.phase) % 1.0
    half = gait.stride / 2
    if t < gait.duty:  # planted: from the front of the stride to the back
        return half - gait.stride * t / gait.duty, 0.0, True
    s = (t - gait.duty) / (1.0 - gait.duty)
    eased = s - math.sin(2 * math.pi * s) / (2 * math.pi)  # starts and ends slowly
    return -half + gait.stride * eased, gait.lift * math.sin(math.pi * s), False


def _angle(y: float, z: float) -> float:
    """Angle of a side-plane vector, increasing from +Y (backwards) towards +Z (up)."""
    return math.atan2(z, y)


def _rotate(y: float, z: float, a: float) -> tuple[float, float]:
    c, s = math.cos(a), math.sin(a)
    return y * c - z * s, y * s + z * c


def solve_leg(
    upper: tuple[float, float], lower: tuple[float, float], target: tuple[float, float]
) -> tuple[float, float, float]:
    """Two-bone IK in the side plane (y back, z up), angles about the left axis (radians).

    ``upper``/``lower``: bone vectors at rest; ``target``: from the hip to the foot. Returns the
    world rotation of the upper bone, the rotation of the lower bone relative to it (the knee
    bends to the side it bends at rest) and the share of the target distance that was reached
    (1.0 = reached; less when the leg is too short and stays stretched)."""
    lu, ll = math.hypot(*upper), math.hypot(*lower)
    d = math.hypot(*target)
    reachable = min(max(d, abs(lu - ll) + 1e-6), (lu + ll) * REACH_MARGIN)
    rest = _angle(*lower) - _angle(*upper)
    rest = (rest + math.pi) % (2 * math.pi) - math.pi
    c = (reachable**2 - lu**2 - ll**2) / (2 * lu * ll)
    bend = math.copysign(math.acos(max(-1.0, min(1.0, c))), rest or 1.0)
    beta = bend - rest
    v = (upper[0] + _rotate(*lower, beta)[0], upper[1] + _rotate(*lower, beta)[1])
    alpha = _angle(*target) - _angle(*v)
    return alpha, beta, (reachable / d if d > reachable else 1.0)


VAULT_MARGIN = 0.985  # planted legs are given this share of their reach (IK stays below straight)


def required_drop(hip_height: float, forward: float, length: float) -> float:
    """Metres the hip must come down so that a leg of ``length`` reaches a foot ``forward`` m
    ahead of or behind it on the ground ``hip_height`` m below (0: reachable as it is)."""
    reach = length * VAULT_MARGIN
    if abs(forward) >= reach:
        return hip_height  # unreachable at any height: as low as possible
    return max(0.0, hip_height - math.sqrt(reach**2 - forward**2))


def vault(
    front: float, rear: float, y_front: float | None, y_rear: float | None, y_pivot: float
) -> tuple[float, float]:
    """Pelvis lowering ``dz`` (m) and pitch ``phi`` (radians about the left axis, at ``y_pivot``)
    that lower the front hips by ``front`` and the rear hips by ``rear`` (y back). With one
    group of legs (bipeds) only the lowering."""
    if y_front is None or y_rear is None:
        return max(front, rear), 0.0
    phi = (rear - front) / (y_front - y_rear)
    return front + (y_front - y_pivot) * phi, phi


def smooth_cyclic(values: list[float]) -> list[float]:
    """A loop's per-frame values without jerks: the maximum of each frame and its neighbours,
    then averaged with them (the first and last frame are the same pose)."""
    n = len(values) - 1  # values[n] repeats values[0]
    if n < 3:
        return list(values)
    ring = values[:n]
    peak = [max(ring[i - 1], ring[i], ring[(i + 1) % n]) for i in range(n)]
    out = [(peak[i - 1] + peak[i] + peak[(i + 1) % n]) / 3 for i in range(n)]
    return [*out, out[0]]
