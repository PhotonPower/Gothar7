"""Turns a CC0 source animal into a contract monster rig (F5, characters-pipeline.md §7).

Run inside Blender 4.5 on the source file:
    blender --background <source.blend> --factory-startup --python prepare_monster.py -- \
        --config <data/monsters/<art>.build.toml> --characters <assets/source/characters> \
        --clips-out <DATA_ROOT/.../<art>_clips.blend> --rig-out <data/monsters/<art>.toml>

Steps: sample every action as world-space bone deformations (sources often drive legs through
IK constraints), drop helper bones and constraints, rename bones to the contract, turn the animal
to face -Y (glTF +Z), scale it to its real height, add `root` on the ground and the sockets, then
key the recorded deformations on the new rig (exact up to float precision). Writes
    monsters/<art>/rig/<art>_reference.blend + .glb   rig + placeholder mesh (node "body")
    <clips-out>                                       .blend with the actions (clip source, local)
    <rig-out>                                         rig definition (names, parents, geometry)
"""

from __future__ import annotations

import argparse
import math
import sys
import tomllib
from pathlib import Path

import bpy  # type: ignore[import-not-found]
from mathutils import Matrix, Quaternion, Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.blender.settings import GLTF_EXPORT_SETTINGS  # noqa: E402

FPS = 30
_FORWARD = {"+X": -90.0, "-X": 90.0, "+Y": 180.0, "-Y": 0.0}  # Z rotation that turns it to -Y


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    p = argparse.ArgumentParser(prog="prepare_monster")
    p.add_argument("--config", type=Path, required=True)
    p.add_argument("--characters", type=Path, required=True)
    p.add_argument("--clips-out", type=Path, required=True)
    p.add_argument("--rig-out", type=Path, required=True)
    return p.parse_args(argv)


Poses = dict[str, list[dict[str, Matrix]]]  # action -> frames (30 fps) -> bone -> world deformation


def _record_poses(arm: bpy.types.Object, actions: dict[str, str], names: dict[str, str]) -> Poses:
    """Samples every source action at 30 fps as world-space deformations (constraints applied).

    Per bone the deformation D = pose_world @ rest_world^-1 is what moves the mesh. Transferring D
    (instead of local keys or absolute poses) keeps the motion exact even though Blender recomputes
    bone rolls when the armature is rotated later.
    """
    scene = bpy.context.scene
    src_fps = scene.render.fps / scene.render.fps_base
    poses: Poses = {}
    for src_name, new_name in actions.items():
        arm.animation_data.action = bpy.data.actions[src_name]
        start, end = bpy.data.actions[src_name].frame_range
        count = int(round((end - start) * FPS / src_fps))
        frames = []
        for i in range(count + 1):
            t = start + i * src_fps / FPS
            scene.frame_set(int(t), subframe=t - int(t))
            frames.append(
                {
                    names[pb.name]: (arm.matrix_world @ pb.matrix)
                    @ (arm.matrix_world @ pb.bone.matrix_local).inverted()
                    for pb in arm.pose.bones
                    if pb.name in names
                }
            )
        poses[new_name] = frames
    arm.animation_data.action = None
    return poses


_TRANSLATED = ("root", "pelvis")  # the only bones with translation keys (contract §3/§7)
_MOVED = 1e-4  # metres: a source bone head further off than this was translated
_CONTACT = 0.05  # metres: a foot target this low stands on the ground and must be reached
_MAX_DROP = 0.07  # share of the animal's height the pelvis may go down for that


def _chain(arm: bpy.types.Object, bone: str) -> list[str]:
    """Up to three bones below `bone`, each the only deforming child of the one before (a limb:
    upper, lower, foot – or, below a shoulder/hip bone, upper, lower, foot)."""
    names: list[str] = []
    current = arm.data.bones[bone]
    while len(names) < 3:
        children = [c for c in current.children if c.use_deform]
        if len(children) != 1:
            break
        current = children[0]
        names.append(current.name)
    return names


def _two_bone(head: Vector, target: Vector, pole: Vector, upper: float, lower: float) -> Vector:
    """Knee position for a limb from `head` reaching `target`, bent towards `pole`."""
    d = target - head
    dist = min(max(d.length, abs(upper - lower) + 1e-6), upper + lower - 1e-6)
    u = d.normalized()
    a = (upper * upper - lower * lower + dist * dist) / (2.0 * dist)
    r = math.sqrt(max(upper * upper - a * a, 0.0))
    side = (pole - head) - u * (pole - head).dot(u)
    return head + u * a + (side.normalized() * r if side.length > 1e-9 else Vector())


def _aim(rot: Quaternion, was: Vector, now: Vector) -> Quaternion:
    """Rotation `rot` turned so that direction `was` becomes `now`."""
    return was.normalized().rotation_difference(now.normalized()) @ rot


def _place(arm: bpy.types.Object, bone: str, rot: Quaternion) -> None:
    """Sets a bone's armature-space rotation; its head stays where the parent puts it."""
    pb = arm.pose.bones[bone]
    pb.matrix = Matrix.LocRotScale(pb.head, rot, (1.0, 1.0, 1.0))
    pb.location = (0.0, 0.0, 0.0)  # rounding residue; not keyed for this bone
    bpy.context.view_layer.update()


def _drop_needed(hip: Vector, foot: Vector, reach: float) -> float:
    """How far the hip must move down (-Z) so a limb of length `reach` touches `foot`."""
    d = foot - hip
    excess = d.length_squared - reach * reach
    if excess <= 0.0:
        return 0.0
    return -d.z - math.sqrt(max(d.z * d.z - excess, 0.0))


def _apply_pose(arm: bpy.types.Object, order: list[str], want: dict[str, tuple]) -> float:
    """Poses the rig so every bone matches `want` (bone -> (head, rotation)) without translating
    bones other than root/pelvis: a limb whose first bone was moved in the source is solved as a
    two-bone chain so its end lands on the source position (the knee bends towards the source
    knee); the end bone gets the source rotation.

    Returns how far the pelvis would have to go down for every limb to reach (0 if all do).
    """
    drop = 0.0
    for pb in arm.pose.bones:  # heads below must not include the previous frame's channels
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    done: set[str] = set()
    for bone in order:
        if bone not in want or bone in done:
            continue
        head, rot = want[bone]
        pb = arm.pose.bones[bone]
        if bone in _TRANSLATED:
            pb.matrix = Matrix.LocRotScale(head, rot, (1.0, 1.0, 1.0))
            bpy.context.view_layer.update()
            continue
        chain = _chain(arm, bone)
        if (pb.head - head).length > _MOVED and len(chain) >= 2 and all(c in want for c in chain):
            if len(chain) == 3 and "_foot_" in chain[2]:  # shoulder/hip: aim, solve the leg
                _place(arm, bone, _aim(rot, want[chain[0]][0] - head, want[chain[0]][0] - pb.head))
                bone, chain = chain[0], chain[1:]
                pb = arm.pose.bones[bone]
                head, rot = want[bone]
            drop = max(drop, _solve_limb(arm, bone, chain[0], chain[1], want))
            done.update(chain[:2])
        else:
            _place(arm, bone, rot)
    return drop


def _solve_limb(
    arm: bpy.types.Object, top: str, mid: str, end: str, want: dict[str, tuple]
) -> float:
    """Two-bone solve: `top` and `mid` turn so the head of `end` lands on its source position
    (knee bent towards the source knee); `end` gets its source rotation. Returns the pelvis drop
    this limb would need to reach – only for a foot on the ground; a leg stretched in the air
    may fall a little short."""
    rest = arm.data.bones
    upper = (rest[mid].head_local - rest[top].head_local).length
    lower = (rest[end].head_local - rest[mid].head_local).length
    head = arm.pose.bones[top].head.copy()
    top_was, knee_was, foot = want[top][0], want[mid][0], want[end][0]
    knee = _two_bone(head, foot, knee_was, upper, lower)
    _place(arm, top, _aim(want[top][1], knee_was - top_was, knee - head))
    _place(arm, mid, _aim(want[mid][1], foot - knee_was, foot - knee))
    _place(arm, end, want[end][1])
    if foot.z > _CONTACT:
        return 0.0
    return _drop_needed(head, foot, (upper + lower) * 0.999)


def _rekey(
    arm: bpy.types.Object, poses: Poses, world: Matrix, max_drop: float
) -> list[bpy.types.Action]:
    """Turns recorded deformations (mapped by `world`, the total transform) into actions.

    The pelvis goes down at most `max_drop` metres for legs that cannot reach (sources with
    stretching IK would otherwise push the body into the ground). Prints per action the largest
    pelvis drop and how far feet and other joints end up from their source positions.
    """
    world_inv = world.inverted()
    order = [b.name for b in arm.data.bones]  # parents first
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    actions = []
    for name, frames in poses.items():
        action = bpy.data.actions.new(name)
        arm.animation_data.action = action
        worst = {"drop": (0.0, "pelvis", 0), "feet": (0.0, "", 0), "other": (0.0, "", 0)}
        for frame, pose in enumerate(frames):
            want = {}
            for bone in pose:
                rest = arm.matrix_world @ arm.data.bones[bone].matrix_local
                loc, rot, _ = (world @ pose[bone] @ world_inv @ rest).decompose()
                want[bone] = (loc, rot)
            drop = _apply_pose(arm, order, want)
            drop = min(drop, max_drop)
            worst["drop"] = max(worst["drop"], (drop, "pelvis", frame))
            if drop > 0.0 and "pelvis" in want:  # legs too short without the source's
                head, rot = want["pelvis"]  # translated hips: lower the body instead
                want["pelvis"] = (head - Vector((0.0, 0.0, drop)), rot)
                _apply_pose(arm, order, want)
            for bone, (head, _) in want.items():
                kind = "feet" if "_foot_" in bone else "other"
                off = (arm.pose.bones[bone].head - head).length
                if bone != "pelvis" and off > worst[kind][0]:
                    worst[kind] = (off, bone, frame)
            for bone in order:
                if bone in pose:
                    pb = arm.pose.bones[bone]
                    pb.keyframe_insert("rotation_quaternion", frame=frame, group=bone)
                    if bone in _TRANSLATED:
                        pb.keyframe_insert("location", frame=frame, group=bone)
        for fc in action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"
        actions.append(action)
        report = ", ".join(f"{k} {v[0] * 100:.1f} cm ({v[1]} @ {v[2]})" for k, v in worst.items())
        print(f"[chargen] {name}: max {report}")
    arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    return actions


def _transform(
    arm: bpy.types.Object, mesh: bpy.types.Object, forward: str, height: float
) -> Matrix:
    """Applies all object transforms, turns the animal to -Y, scales it to `height`, feet on z=0.

    Returns the total world transform applied (old world -> new world).
    """
    for o in (arm, mesh):
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    top = max((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    bottom = min((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    scale = height / (top - bottom)
    arm.rotation_euler = (0.0, 0.0, math.radians(_FORWARD[forward]))
    arm.scale = (scale, scale, scale)
    arm.location = (0.0, 0.0, -bottom * scale)
    bpy.context.view_layer.update()
    applied = arm.matrix_world.copy()
    mesh.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return applied


def _rename_bones(
    arm: bpy.types.Object, drop: list[str], names: dict[str, str], parents: dict[str, str]
) -> None:
    """Removes constraints, re-parents (source names, e.g. IK-target feet onto the lower legs),
    drops helper bones and renames the rest to the contract."""
    bpy.context.view_layer.objects.active = arm
    for pb in arm.pose.bones:
        for c in list(pb.constraints):
            pb.constraints.remove(c)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    for child, parent in parents.items():
        eb[child].parent = eb[parent]
        eb[child].use_connect = False
    for name in drop:
        if name in eb:
            eb.remove(eb[name])
    for old, new in names.items():
        eb[old].name = new
    unknown = [b.name for b in eb if b.name not in names.values()]
    bpy.ops.object.mode_set(mode="OBJECT")
    if unknown:
        raise SystemExit(f"bones without a contract name: {unknown}")


def _add_root_and_sockets(
    arm: bpy.types.Object, mesh: bpy.types.Object, sockets: dict[str, dict]
) -> Matrix:
    """root on the ground below the pelvis, moved to the origin; sockets at bone tails.

    Returns the translation applied to move the pelvis above the origin.
    """
    pelvis_ground = arm.data.bones["pelvis"].head_local.copy()
    pelvis_ground.z = 0.0
    for o in (arm, mesh):
        if o.parent not in (arm, mesh):  # a parented mesh moves with the armature
            o.location -= pelvis_ground
    bpy.ops.object.select_all(action="DESELECT")
    for o in (arm, mesh):
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    root = eb.new("root")
    root.head = Vector((0.0, 0.0, 0.0))
    root.tail = Vector((0.0, -0.2, 0.0))
    root.align_roll(Vector((0.0, 0.0, 1.0)))
    for b in eb:
        if b.parent is None and b.name != "root":
            b.parent = root
            b.use_connect = False
    for name, spec in sockets.items():
        parent = eb[spec["parent"]]
        s = eb.new(name)
        direction = (parent.tail - parent.head).normalized()
        s.head = parent.tail.copy()
        s.tail = parent.tail + direction * float(spec.get("length", 0.05))
        s.roll = parent.roll
        s.parent = parent
        s.use_deform = False
    bpy.ops.object.mode_set(mode="OBJECT")
    return Matrix.Translation(-pelvis_ground)


def _export(objects: list[bpy.types.Object], path: Path, animations: bool) -> None:
    for o in bpy.context.scene.objects:
        o.select_set(o in objects)
    settings = dict(GLTF_EXPORT_SETTINGS, use_selection=True, export_animations=animations)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), **settings)
    print(f"[chargen] wrote {path}")


_ORIENTATION = {
    "up": ["root", "head"],
    "forward": ["pelvis", "head"],
    "left": ["front_foot_r", "front_foot_l"],
}


def _write_rig(
    arm: bpy.types.Object,
    art: str,
    height: float,
    sockets: set[str],
    path: Path,
    orientation: dict[str, list[str]],
) -> None:
    lines = [
        f'# Monster rig "{art}" (contract, characters-pipeline.md §7), generated by',
        "# prepare_monster.py from the CC0 source. Names, parents and sockets are the contract,",
        "# the geometry is the bind pose.",
        "",
        "[rig]",
        f'name = "{art}_reference"',
        'kind = "monster"',
        f'species = "{art}"',
        f"height = {height:.3f}",
        'bind_pose = "natural"',
        "max_bones = 64",
        "max_influences = 4",
        "morph_targets = []",
        'required = ["root", "pelvis", "neck_01", "head", "socket_mouth"]',
        "",
        "[rig.orientation]          # pairs (a, b): direction a -> b points mainly along the axis",
        *(f'{axis} = ["{a}", "{b}"]' for axis, (a, b) in orientation.items()),
        "",
    ]

    def fmt(v: Vector) -> str:
        return "[" + ", ".join("0" if abs(x) < 5e-7 else f"{x:.6f}" for x in v) + "]"

    # rolls first: switching modes invalidates Bone references
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    rolls = {b.name: b.roll for b in arm.data.edit_bones}
    bpy.ops.object.mode_set(mode="OBJECT")
    order: list[bpy.types.Bone] = []

    def visit(b: bpy.types.Bone) -> None:
        order.append(b)
        for c in sorted(b.children, key=lambda c: c.name):
            visit(c)

    visit(arm.data.bones["root"])
    for b in order:
        lines += ["[[bone]]", f'name = "{b.name}"']
        if b.parent:
            lines.append(f'parent = "{b.parent.name}"')
        if b.name in sockets:
            lines.append("socket = true")
        lines += [
            f"head = {fmt(b.head_local)}",
            f"tail = {fmt(b.tail_local)}",
            f"roll = {rolls[b.name]:.6f}",
            "",
        ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"[chargen] wrote {path}")


def _set_color(material: bpy.types.Material, hex_color: str) -> None:
    """Base colour of a placeholder material from "#rrggbb" (sRGB)."""
    srgb = [int(hex_color[i : i + 2], 16) / 255.0 for i in (1, 3, 5)]
    linear = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb]
    material.diffuse_color = (*linear, 1.0)
    if material.node_tree:
        for node in material.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                node.inputs["Base Color"].default_value = (*linear, 1.0)


def main() -> None:
    args = _parse_args()
    cfg = tomllib.loads(args.config.read_text(encoding="utf-8"))
    art = cfg["art"]
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH" and o.parent is arm)
    for o in [o for o in bpy.data.objects if o not in (arm, mesh)]:
        bpy.data.objects.remove(o)
    arm.animation_data_create()
    sockets = cfg.get("sockets", {})

    poses = _record_poses(arm, cfg["actions"], cfg["bones"])
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    _rename_bones(arm, cfg.get("drop", []), cfg["bones"], cfg.get("parents", {}))
    applied = _transform(arm, mesh, cfg["forward"], float(cfg["height"]))
    shift = _add_root_and_sockets(arm, mesh, sockets)
    scale = applied.to_scale().x
    actions = _rekey(arm, poses, shift @ applied, _MAX_DROP * float(cfg["height"]))

    arm.name = arm.data.name = f"{art}_reference"
    mesh.name = mesh.data.name = "body"
    for slot in mesh.material_slots:
        if slot.material:
            if slot.material.name in cfg.get("colors", {}):
                _set_color(slot.material, cfg["colors"][slot.material.name])
            slot.material.name = cfg.get("material", "fur")
    bpy.context.scene.render.fps = FPS

    # clip source: the actions on the contract rig (build_set.py reads them from this .blend)
    for action in actions:
        action.use_fake_user = True
    args.clips_out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.clips_out.resolve()), copy=True, compress=True)
    print(f"[chargen] wrote {args.clips_out}")
    for action in actions:
        bpy.data.actions.remove(action)

    rig_dir = args.characters / "monsters" / art / "rig"
    rig_dir.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(
        filepath=str((rig_dir / f"{art}_reference.blend").resolve()), compress=True
    )
    _export([arm, mesh], rig_dir / f"{art}_reference.glb", animations=False)
    height = max((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    orientation = {**_ORIENTATION, **cfg.get("orientation", {})}
    _write_rig(arm, art, height, set(sockets), args.rig_out, orientation)
    print(f"[chargen] {art}: scale {scale:.4f}, height {height:.3f} m, {len(arm.data.bones)} bones")


if __name__ == "__main__":
    main()
