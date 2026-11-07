"""CMU mocap -> reference rig (F4): the segments of data/cmu.toml as actions in one .blend.

Run inside Blender 4.5:
    blender --background --factory-startup --python cmu_retarget.py -- --raw <raw> --out <.blend>

Every mapped bone gets the CMU bone's world rotation since its rest pose (both rigs stand in a
T-pose; each bone is first aligned to the CMU bone's rest direction), converted from the CMU axes
(Y up, +Z forward) to Blender's (Z up, -Y forward); the pelvis follows the CMU hips, scaled by the
leg length ratio. Each segment starts facing forward over the origin; root stays put (no root
motion, as the clip contract wants for attacks). 120 fps are sampled down to 30.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np
from mathutils import Matrix, Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.build_set import _write_action  # noqa: E402
from gothar_chargen.blender.common import new_reference, save  # noqa: E402
from gothar_chargen.blender.curves import to_curves  # noqa: E402
from gothar_chargen.blender.keyframes import RigInfo  # noqa: E402
from gothar_chargen.cmu import CMU_FPS, forward, load_segments, load_take  # noqa: E402
from gothar_chargen.skeleton import load_rig  # noqa: E402

STEP = CMU_FPS // 30
CMU_TO_RIG = {
    "root": "pelvis",
    "lowerback": "spine_01",
    "upperback": "spine_02",
    "thorax": "spine_03",
    "lowerneck": "neck",
    "head": "head",
    "lclavicle": "clavicle_l",
    "lhumerus": "upperarm_l",
    "lradius": "lowerarm_l",
    "lhand": "hand_l",
    "rclavicle": "clavicle_r",
    "rhumerus": "upperarm_r",
    "rradius": "lowerarm_r",
    "rhand": "hand_r",
    "lfemur": "thigh_l",
    "ltibia": "calf_l",
    "lfoot": "foot_l",
    "ltoes": "ball_l",
    "rfemur": "thigh_r",
    "rtibia": "calf_r",
    "rfoot": "foot_r",
    "rtoes": "ball_r",
}
CV = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))  # CMU (x, y, z) -> Blender (x, -z, y)


def _m(a: np.ndarray) -> Matrix:
    return Matrix([list(map(float, row)) for row in a])


def _v(a: np.ndarray) -> Vector:
    return Vector([float(x) for x in a])


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="cmu_retarget")
    p.add_argument("--raw", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    return p.parse_args(argv)


def _alignments(ri: RigInfo, skeleton) -> dict[str, Matrix]:  # noqa: ANN001
    """Per target bone: the world rotation taking its rest direction onto the CMU bone's."""
    out = {}
    for src, dst in CMU_TO_RIG.items():
        bone = skeleton.bones[src]
        if src == "root" or not np.any(bone.direction):
            out[dst] = Matrix.Identity(3)
            continue
        rest = ri.matrix[dst]
        target_dir = (ri.tail[dst] - rest.translation).normalized()
        source_dir = (CV @ _v(bone.direction)).normalized()
        out[dst] = target_dir.rotation_difference(source_dir).to_matrix()
    return out


def _leg(ri: RigInfo, skeleton) -> float:  # noqa: ANN001
    target = sum((ri.tail[b] - ri.matrix[b].translation).length for b in ("thigh_l", "calf_l"))
    source = sum(skeleton.bones[b].length for b in ("lfemur", "ltibia"))
    return target / source


def _facing(pose) -> Matrix:  # noqa: ANN001
    """Yaw (Blender, about Z) that turns the CMU figure of this pose to face -Y."""
    right = CV @ _v(pose.start["lfemur"] - pose.start["rfemur"])  # towards the left hip (+X)
    right.z = 0.0
    fwd = right.normalized().cross(Vector((0.0, 0.0, 1.0)))  # left x up = forward
    have = math.atan2(fwd.y, fwd.x)
    return Matrix.Rotation(-math.pi / 2 - have, 3, "Z")


def main() -> None:
    args = _parse_args()
    rig = load_rig()
    arm = new_reference(rig)
    arm.animation_data_create()
    ri = RigInfo(arm)
    order = list(ri.bones)
    takes: dict[str, tuple] = {}
    actions = []
    for seg in load_segments():
        if seg.take not in takes:
            takes[seg.take] = load_take(args.raw, seg.take)
        skeleton, frames = takes[seg.take]
        align = _alignments(ri, skeleton)
        scale = _leg(ri, skeleton)
        first = forward(skeleton, frames[seg.start])
        yaw = _facing(first)
        origin = CV @ _v(first.start["root"])
        # ground: the CMU ankles of the first frame stand where the rig's ankles stand at rest
        ankle = min((CV @ _v(first.start[f])).z for f in ("lfoot", "rfoot"))
        rest_ankle = ri.matrix["foot_l"].translation.z
        hips = ri.matrix["pelvis"].translation.copy()
        poses = []
        for f in range(seg.start, seg.end + 1, STEP):
            src = forward(skeleton, frames[f])
            pose = ri.rest_pose()
            want = {}
            for s, d in CMU_TO_RIG.items():
                delta = yaw @ CV @ _m(src.rotation[s]) @ CV.transposed()
                want[d] = delta @ align[d] @ ri.matrix[d].to_3x3()
            for b in order:
                if b not in want:
                    continue
                parent = ri.parent[b]
                if parent is None:
                    base = ri.matrix[b].to_3x3()
                    q = (base.inverted() @ want[b]).to_quaternion()
                else:
                    rel = (ri.matrix[parent].inverted() @ ri.matrix[b]).to_3x3()
                    wp = ri.world(pose, parent).to_3x3()
                    q = ((wp @ rel).inverted() @ want[b]).to_quaternion()
                pose[b] = (q, pose[b][1])
            root = CV @ _v(src.start["root"])
            moved = yaw @ (root - origin) * scale
            height = (root.z - ankle) * scale + rest_ankle
            target = Vector((hips.x + moved.x, hips.y + moved.y, height))
            ri.move(pose, "pelvis", target - hips)
            poses.append(pose)
        # the rigs' legs differ a little: put the lower ankle of the first frame on rest height
        low = min(ri.world(poses[0], f).translation.z for f in ("foot_l", "foot_r"))
        for pose in poses:
            ri.move(pose, "pelvis", Vector((0.0, 0.0, rest_ankle - low)))
        action = _write_action(seg.name, to_curves(poses))
        action.use_fake_user = True
        actions.append(action)
        print(
            f"[chargen] cmu {seg.name}: {seg.take} [{seg.start}:{seg.end}] -> {len(poses)} frames"
        )
    save(args.out)


if __name__ == "__main__":
    main()
