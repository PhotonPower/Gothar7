"""Rig validator: checks character .glb files against their rig (human reference or monster).

Contract: docs/modules/animation.md ("Referenz-Skelett"), docs/design/characters-pipeline.md.
Bone names/hierarchy come from data/human_reference.toml (monsters: data/monsters/<species>.toml);
the bind pose (local joint transforms) is compared with the exported reference file
assets/source/characters/rig/human_reference.glb (monsters/<species>/rig/<species>_reference.glb).
"""

from __future__ import annotations

import contextlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

from gothar_chargen.clipfix import check_set
from gothar_chargen.clipspeed import clip_speeds
from gothar_chargen.collision import CollisionError, derive_collision
from gothar_chargen.events import SPEED_TOLERANCE, events_path_for, load_events
from gothar_chargen.fit import check_fit
from gothar_chargen.gltf import Gltf, GltfError, Trs, node_trs, quat_angle_deg
from gothar_chargen.images import ImageError, ImageInfo, image_info, is_power_of_two
from gothar_chargen.meshdata import mesh_data, split_lod
from gothar_chargen.naming import clip_mode, is_clip_name, is_loop_clip, is_monster_clip
from gothar_chargen.postprocess import MASK_ROLES, TRANSLATED_BONES, material_role
from gothar_chargen.skeleton import RigSpec

ERROR = "error"
WARNING = "warning"

_ROOT = "root"
MAX_MORPHS = 16  # per mesh (engine, §6)
_MAX_LISTED = 8  # bones listed per message before "..."


@dataclass(frozen=True)
class Tolerances:
    rotation_error_deg: float = 5.0
    rotation_warn_deg: float = 1.0
    length_error_rel: float = 0.15  # bone offset length vs. reference
    length_warn_rel: float = 0.05
    length_abs_floor: float = 0.01  # metres; below this, relative checks use the floor
    scale: float = 1e-3
    root_offset: float = 0.01  # metres
    bind_matrix: float = 1e-3
    weight_sum: float = 1e-2
    height_error: tuple[float, float] = (1.50, 2.10)
    height_warn: tuple[float, float] = (1.65, 1.95)
    height_rel_error: float = 0.30  # monsters: vs. rig height
    height_rel_warn: float = 0.10
    ground: float = 0.03
    jump_rotation_error_deg: float = 120.0  # per key (exported at 30 fps: per frame)
    jump_rotation_warn_deg: float = 90.0
    jump_translation_m: float = 0.5
    loop_rotation_deg: float = 5.0  # first vs. last key of s_* clips
    collision_drift: float = 0.05  # metres: stored capsule vs. one derived from the mesh
    # monsters (§7): root motion of s_walk/s_run and turning of t_turn_l/r
    monster_advance_min_m_s: float = 0.1
    monster_turn_min_deg: float = 45.0
    # LOD contract (characters-pipeline.md §2.2)
    figure_triangles_max: int = 20_000  # lod0 per figure
    lod_ratio_warn: tuple[float, ...] = (1.0, 0.6, 0.3)  # max. share of lod0 per level
    # texture contract (characters-pipeline.md §2.3): max. edge length per material role
    texture_max: tuple[tuple[str, int], ...] = (
        ("skin", 2048),
        ("cloth", 1024),
        ("hair", 1024),
        ("beard", 1024),
        ("fur", 1024),  # monsters (§7.1)
        ("eyes", 256),
        ("eyebrows", 256),
        ("eyelashes", 256),
        ("teeth", 256),
        ("tongue", 256),
    )


@dataclass(frozen=True)
class Issue:
    level: str
    code: str
    message: str


@dataclass
class Report:
    path: Path
    issues: list[Issue] = field(default_factory=list)
    stats: dict[str, Any] = field(default_factory=dict)

    def error(self, code: str, message: str) -> None:
        self.issues.append(Issue(ERROR, code, message))

    def warning(self, code: str, message: str) -> None:
        self.issues.append(Issue(WARNING, code, message))

    @property
    def errors(self) -> list[Issue]:
        return [i for i in self.issues if i.level == ERROR]

    @property
    def warnings(self) -> list[Issue]:
        return [i for i in self.issues if i.level == WARNING]

    def ok(self, strict: bool = False) -> bool:
        return not self.errors and not (strict and self.warnings)

    def to_dict(self) -> dict[str, Any]:
        return {
            "path": str(self.path),
            "ok": self.ok(),
            "stats": self.stats,
            "issues": [
                {"level": i.level, "code": i.code, "message": i.message} for i in self.issues
            ],
        }


ReferencePose = dict[str, Trs]


def _listed(names: list[str]) -> str:
    shown = ", ".join(names[:_MAX_LISTED])
    return shown + (f", ... (+{len(names) - _MAX_LISTED})" if len(names) > _MAX_LISTED else "")


def reference_pose(gltf: Gltf) -> ReferencePose:
    """Local joint transforms of the skeleton below ``root`` (the bind pose of a reference file)."""
    nodes = gltf.list("nodes")
    root = _find_root(gltf)
    if root is None:
        raise GltfError("reference file has no 'root' node")
    return {nodes[i].get("name", ""): node_trs(nodes[i]) for i in _skeleton_nodes(gltf, root)}


def _find_root(gltf: Gltf) -> int | None:
    matches = [i for i, n in enumerate(gltf.list("nodes")) if n.get("name") == _ROOT]
    return matches[0] if len(matches) == 1 else None


def _skeleton_nodes(gltf: Gltf, root: int) -> list[int]:
    """root and all descendants that are bones (no mesh/camera attached), depth first."""
    nodes = gltf.list("nodes")
    out: list[int] = []
    stack = [root]
    seen: set[int] = set()
    while stack:
        i = stack.pop()
        if i in seen:
            raise GltfError("node hierarchy contains a cycle")
        seen.add(i)
        node = nodes[i]
        if "mesh" in node or "camera" in node:
            continue
        out.append(i)
        stack.extend(reversed(node.get("children", [])))
    return out


class _Checker:
    def __init__(
        self,
        gltf: Gltf,
        rig: RigSpec,
        reference: ReferencePose | None,
        tol: Tolerances,
        report: Report,
    ) -> None:
        self.g = gltf
        self.rig = rig
        self.ref = reference
        self.tol = tol
        self.r = report
        self.nodes = gltf.list("nodes")
        self.parents = gltf.node_parents()
        self.world = gltf.world_matrices()
        self.bone_index: dict[str, int] = {}

    def name(self, i: int) -> str:
        return str(self.nodes[i].get("name", f"#{i}"))

    def pos(self, bone: str) -> np.ndarray | None:
        i = self.bone_index.get(bone)
        return None if i is None else self.world[i][:3, 3]

    # --- skeleton --------------------------------------------------------------------------

    def skeleton(self) -> bool:
        roots = [i for i, n in enumerate(self.nodes) if n.get("name") == _ROOT]
        if not roots:
            self.r.error("skeleton.missing", "no node named 'root' (reference skeleton not found)")
            return False
        if len(roots) > 1:
            self.r.error("skeleton.missing", "more than one node named 'root'")
            return False
        root = roots[0]
        bones = _skeleton_nodes(self.g, root)
        names = [self.name(i) for i in bones]
        self.r.stats["bones"] = len(bones)

        dupes = sorted({n for n in names if names.count(n) > 1})
        if dupes:
            self.r.error("skeleton.duplicate", f"duplicate bone names: {_listed(dupes)}")
        self.bone_index = {self.name(i): i for i in bones}

        expected = self.rig.parents
        missing = [n for n in expected if n not in self.bone_index]
        extra = [n for n in names if n not in expected]
        if missing:
            self.r.error("skeleton.names", f"missing bones: {_listed(missing)}")
        if extra:
            self.r.error("skeleton.names", f"bones not in the reference rig: {_listed(extra)}")
        if len(bones) > self.rig.max_bones:
            self.r.error(
                "skeleton.count", f"{len(bones)} bones exceed the limit {self.rig.max_bones}"
            )

        wrong_parent = []
        for name, idx in self.bone_index.items():
            want = expected.get(name, None)
            if name not in expected or name == _ROOT:
                continue
            got = self.name(self.parents[idx]) if idx in self.parents else None
            if got != want:
                wrong_parent.append(f"{name} (parent {got}, expected {want})")
        if wrong_parent:
            self.r.error("skeleton.hierarchy", f"wrong parents: {_listed(wrong_parent)}")

        # scale: bones and everything above root must be unscaled
        chain = list(bones)
        p = self.parents.get(root)
        while p is not None:
            chain.append(p)
            p = self.parents.get(p)
        scaled = [
            self.name(i)
            for i in chain
            if np.max(np.abs(node_trs(self.nodes[i]).scale - 1.0)) > self.tol.scale
        ]
        if scaled:
            self.r.error("skeleton.scale", f"scaled nodes (scale must be 1): {_listed(scaled)}")

        root_pos = self.world[root][:3, 3]
        if np.linalg.norm(root_pos) > self.tol.root_offset:
            self.r.error(
                "skeleton.root_origin",
                f"'root' must sit at the origin, is at {np.round(root_pos, 3).tolist()}",
            )
        return not missing

    def orientation(self) -> None:
        """Y up, character faces +Z, left side is +X (glTF convention)."""
        if self.rig.orientation:
            self._orientation_hints()
            return

        def axis_ok(a: str, b: str, axis: int) -> bool:
            """True if the direction from bone a to bone b points mainly along +axis."""
            pa, pb = self.pos(a), self.pos(b)
            if pa is None or pb is None:
                return True
            d = pb - pa
            n = float(np.linalg.norm(d))
            return n > 0 and d[axis] / n > 0.7

        root = self.pos(_ROOT)
        pelvis = self.pos("pelvis")
        up = axis_ok("pelvis", "head", 1) and (
            root is None or pelvis is None or pelvis[1] - root[1] > 0.5
        )
        if not up:
            self.r.error("orientation.up", "expected Y up: head above pelvis above the ground")
        if not (axis_ok("foot_l", "ball_l", 2) and axis_ok("foot_r", "ball_r", 2)):
            self.r.error("orientation.forward", "character must face +Z (toes in front of ankles)")
        if not axis_ok("hand_r", "hand_l", 0):
            self.r.error("orientation.side", "left side must be +X (hand_l at +X, hand_r at -X)")

    def _orientation_hints(self) -> None:
        """Rigs with [rig.orientation] (monsters): each bone pair must point along its axis."""
        axes = {
            "up": (1, "orientation.up", "Y up"),
            "forward": (2, "orientation.forward", "facing +Z"),
            "left": (0, "orientation.side", "left side +X"),
        }
        for axis, (a, b) in self.rig.orientation:
            pa, pb = self.pos(a), self.pos(b)
            if pa is None or pb is None:
                continue
            index, code, text = axes[axis]
            d = pb - pa
            n = float(np.linalg.norm(d))
            if n == 0 or d[index] / n <= 0.5:
                self.r.error(code, f"expected {text}: {a} -> {b} points along {np.round(d, 2)}")

    def bind_pose(self) -> None:
        if self.ref is None:
            return
        rot_err, rot_warn, len_err, len_warn = [], [], [], []
        t = self.tol
        for name, idx in self.bone_index.items():
            ref = self.ref.get(name)
            if ref is None or name == _ROOT:
                continue
            trs = node_trs(self.nodes[idx])
            angle = quat_angle_deg(trs.rotation, ref.rotation)
            if angle > t.rotation_error_deg:
                rot_err.append(f"{name} {angle:.1f}°")
            elif angle > t.rotation_warn_deg:
                rot_warn.append(f"{name} {angle:.1f}°")
            ref_len = float(np.linalg.norm(ref.translation))
            diff = abs(float(np.linalg.norm(trs.translation)) - ref_len)
            label = f"{name} {diff * 100:.1f} cm"
            if diff > max(t.length_error_rel * ref_len, t.length_abs_floor):
                len_err.append(label)
            elif diff > max(t.length_warn_rel * ref_len, t.length_abs_floor / 2):
                len_warn.append(label)
        if rot_err:
            self.r.error(
                "pose.rotation",
                f"bind pose differs from the reference T-pose (> {t.rotation_error_deg}°): "
                + _listed(rot_err),
            )
        if rot_warn:
            self.r.warning(
                "pose.rotation", "bind pose rotation differs slightly: " + _listed(rot_warn)
            )
        if len_err:
            self.r.error(
                "pose.proportion",
                f"bone lengths differ more than {t.length_error_rel:.0%} from the reference: "
                + _listed(len_err),
            )
        if len_warn:
            self.r.warning(
                "pose.proportion", "bone lengths differ noticeably: " + _listed(len_warn)
            )

    # --- meshes and skins ------------------------------------------------------------------

    def meshes(self, part_file: bool = False) -> None:
        meshes = self.g.list("meshes")
        skins = self.g.list("skins")
        morphs: set[str] = set()
        min_y, max_y = np.inf, -np.inf
        skinned = 0
        for ni, node in enumerate(self.nodes):
            if "mesh" not in node:
                continue
            mesh = meshes[node["mesh"]]
            morphs.update(mesh.get("extras", {}).get("targetNames", []))
            if "skin" not in node:
                self.r.warning("skin.missing", f"mesh node '{self.name(ni)}' is not skinned")
                continue
            skinned += 1
            skin = skins[node["skin"]]
            joints = skin.get("joints", [])
            self._check_skin_joints(node["skin"], skin, ni)
            for pi, prim in enumerate(mesh.get("primitives", [])):
                where = f"mesh '{mesh.get('name', node['mesh'])}' primitive {pi}"
                lo, hi = self._position_bounds(prim)
                if lo is not None and hi is not None:
                    min_y, max_y = min(min_y, lo), max(max_y, hi)
                self._check_weights(prim, joints, where)
        self.r.stats["skinned_meshes"] = skinned
        self.r.stats["morph_targets"] = sorted(morphs)

        allowed = set(self.rig.morph_targets)
        unknown = sorted(m for m in morphs if m not in allowed)
        if unknown:
            self.r.error(
                "morph.name", f"morph targets not in the naming contract (§6): {_listed(unknown)}"
            )
        self._check_morph_sets(meshes)

        if skinned and np.isfinite(max_y) and not part_file:
            height = max_y - min_y
            self.r.stats["height"] = round(float(height), 3)
            lo_e, hi_e = self.tol.height_error
            lo_w, hi_w = self.tol.height_warn
            if self.rig.is_monster:  # relative to the species' rig height
                h, e, w = self.rig.height, self.tol.height_rel_error, self.tol.height_rel_warn
                lo_e, hi_e = round(h * (1 - e), 2), round(h * (1 + e), 2)
                lo_w, hi_w = round(h * (1 - w), 2), round(h * (1 + w), 2)
            if not lo_e <= height <= hi_e:
                self.r.error(
                    "mesh.height", f"figure is {height:.2f} m tall (allowed {lo_e}-{hi_e} m)"
                )
            elif not lo_w <= height <= hi_w:
                self.r.warning(
                    "mesh.height", f"figure is {height:.2f} m tall (usual {lo_w}-{hi_w} m)"
                )
            if abs(min_y) > self.tol.ground:
                self.r.warning("mesh.ground", f"lowest vertex at y = {min_y:.3f} m, expected ~0")

    def collision(self) -> None:
        """Monsters: the stored capsule must still fit the mesh (re-derive after mesh changes)."""
        stored = self.rig.collision
        if not self.rig.is_monster or stored is None:
            return
        if not any("mesh" in n and "skin" in n for n in self.nodes):
            return  # animation files carry no mesh
        try:
            derived = derive_collision(self.g)
        except CollisionError:
            return
        drift = max(
            abs(stored.radius - derived.radius),
            abs(stored.length - derived.length),
            *(abs(a - b) for a, b in zip(stored.offset, derived.offset, strict=True)),
        )
        if stored.shape != derived.shape or drift > self.tol.collision_drift:
            self.r.warning(
                "collision.stale",
                f"[rig.collision] does not fit the mesh any more (derived: {derived}); "
                "run gothar-chargen collision",
            )

    def _check_morph_sets(self, meshes: list[dict[str, Any]]) -> None:
        """Contract §6 (engine): a mesh with morph targets carries the complete list in contract
        order, every primitive has one target per name, at most 16 per mesh."""
        contract = list(self.rig.morph_targets)
        for mesh in meshes:
            names = list(mesh.get("extras", {}).get("targetNames", []))
            counts = {len(p.get("targets", [])) for p in mesh.get("primitives", [])}
            if not names and counts <= {0}:
                continue
            label = mesh.get("name", "?")
            if len(names) > MAX_MORPHS:
                self.r.error(
                    "morph.count", f"mesh '{label}': {len(names)} morph targets (max {MAX_MORPHS})"
                )
            if names != contract:
                self.r.error(
                    "morph.order",
                    f"mesh '{label}': morph targets must be the full contract list in order "
                    f"({', '.join(contract)}), got {_listed(names)}",
                )
            if counts != {len(names)}:
                self.r.error(
                    "morph.primitives",
                    f"mesh '{label}': every primitive needs {len(names)} targets, "
                    f"got {sorted(counts)}",
                )

    # --- LOD levels (contract §2.2) ------------------------------------------------------------

    def lods(self, part_file: bool) -> None:
        groups: dict[str, dict[int, int]] = {}
        unsuffixed: list[int] = []
        for i, node in enumerate(self.nodes):
            if "mesh" not in node:
                continue
            base, level = split_lod(self.name(i))
            if level is None:
                unsuffixed.append(i)
            else:
                groups.setdefault(base, {})[level] = i
        triangles = {
            i: mesh_data(self.g, i).triangle_count
            for i in range(len(self.nodes))
            if "mesh" in self.nodes[i]
        }
        level_totals: dict[int, int] = {}
        for base, levels in sorted(groups.items()):
            present = sorted(levels)
            if present != list(range(len(present))):
                self.r.error("lod.gap", f"'{base}': LOD levels {present} are not continuous from 0")
            ref = levels.get(0)
            for level, idx in levels.items():
                node = self.nodes[idx]
                if level > 0 and self._has_morphs(node):
                    self.r.error("lod.morph", f"'{self.name(idx)}': morph targets only on _lod0")
                if ref is None or level == 0:
                    continue
                base_node = self.nodes[ref]
                if self.parents.get(idx) != self.parents.get(ref):
                    self.r.error("lod.mismatch", f"'{self.name(idx)}': other parent than _lod0")
                if not np.allclose(self.g.local_matrix(idx), self.g.local_matrix(ref), atol=1e-5):
                    self.r.error("lod.mismatch", f"'{self.name(idx)}': other transform than _lod0")
                if not self._same_skin(node.get("skin"), base_node.get("skin")):
                    self.r.error("lod.mismatch", f"'{self.name(idx)}': other skin than _lod0")
            for level, idx in levels.items():
                level_totals[level] = level_totals.get(level, 0) + triangles[idx]
        shared = sum(triangles[i] for i in unsuffixed)
        lod0 = level_totals.get(0, 0) + shared
        self.r.stats["triangles"] = lod0
        if groups:
            self.r.stats["lods"] = max(level_totals) + 1
        if part_file or not self.r.stats.get("skinned_meshes"):
            return
        limit = self.tol.figure_triangles_max
        if lod0 > limit:
            self.r.error(
                "mesh.budget", f"{lod0} triangles at lod0, budget {limit} per figure (§2.2)"
            )
        for level, total in sorted(level_totals.items()):
            if level == 0 or level >= len(self.tol.lod_ratio_warn) or lod0 == 0:
                continue
            share = (total + shared) / lod0
            if share > self.tol.lod_ratio_warn[level]:
                self.r.warning(
                    "lod.ratio",
                    f"lod{level} keeps {share:.0%} of the lod0 triangles "
                    f"(expected ≤ {self.tol.lod_ratio_warn[level]:.0%})",
                )

    def _has_morphs(self, node: dict[str, Any]) -> bool:
        mesh = self.g.list("meshes")[node["mesh"]]
        return any(p.get("targets") for p in mesh.get("primitives", []))

    def _same_skin(self, a: int | None, b: int | None) -> bool:
        if a == b:
            return True
        if a is None or b is None:
            return False
        skins = self.g.list("skins")
        if skins[a].get("joints") != skins[b].get("joints"):
            return False
        if "inverseBindMatrices" not in skins[a] or "inverseBindMatrices" not in skins[b]:
            return "inverseBindMatrices" not in skins[a] and "inverseBindMatrices" not in skins[b]
        return bool(
            np.allclose(
                self.g.accessor(skins[a]["inverseBindMatrices"]),
                self.g.accessor(skins[b]["inverseBindMatrices"]),
                atol=1e-6,
            )
        )

    # --- textures (contract §2.3) --------------------------------------------------------------

    def _image(self, index: int) -> tuple[bytes | None, bool, str]:
        """(image bytes, embedded?, label); bytes None if unreadable."""
        image = self.g.list("images")[index]
        label = str(image.get("name") or image.get("uri") or f"image {index}")
        if "bufferView" in image:
            view = self.g.list("bufferViews")[image["bufferView"]]
            start = int(view.get("byteOffset", 0))
            return self.g.bin[start : start + int(view["byteLength"])], True, label
        uri = image.get("uri", "")
        if uri.startswith("data:") or self.g.path is None:
            return None, True, label
        path = self.g.path.parent / uri
        if not path.is_file():
            self.r.error("tex.missing", f"{label}: file {uri} not found")
            return None, False, label
        return path.read_bytes(), False, label

    def textures(self) -> None:
        limits = dict(self.tol.texture_max)
        textures = self.g.list("textures")
        infos: dict[int, ImageInfo] = {}
        sizes = []
        for mat in self.g.list("materials"):
            name = str(mat.get("name", ""))
            role = material_role(name)
            limit = limits.get(role, limits["cloth"])
            pbr = mat.get("pbrMetallicRoughness", {})
            slots = {"base": pbr.get("baseColorTexture"), "normal": mat.get("normalTexture")}
            dims: dict[str, tuple[int, int]] = {}
            for slot, ref in slots.items():
                if not ref or ref.get("index", -1) >= len(textures):
                    continue
                source = textures[ref["index"]].get("source")
                if source is None:
                    continue
                data, embedded, label = self._image(source)
                if data is None:
                    continue
                try:
                    info = infos.get(source) or image_info(data)
                except ImageError as e:
                    self.r.error("tex.format", f"{label}: {e}")
                    continue
                infos[source] = info
                dims[slot] = (info.width, info.height)
                sizes.append(info.width * info.height)
                where = f"material '{name}' ({role}) {slot} texture {label}"
                if max(info.width, info.height) > limit:
                    self.r.error(
                        "tex.size",
                        f"{where}: {info.width}×{info.height} exceeds {limit}² for {role}",
                    )
                if not (is_power_of_two(info.width) and is_power_of_two(info.height)):
                    self.r.error(
                        "tex.pow2", f"{where}: {info.width}×{info.height} is not a power of two"
                    )
                if embedded:
                    self.r.warning(
                        "tex.embedded", f"{where}: embedded – reference a file in textures/ instead"
                    )
                if (
                    slot == "base"
                    and role in MASK_ROLES
                    and not (info.format == "png" and info.alpha)
                ):
                    self.r.error("tex.alpha", f"{where}: mask textures must be PNG with alpha")
                if slot == "normal" and info.format == "jpeg":
                    self.r.warning(
                        "tex.format", f"{where}: normal maps should be PNG (JPEG artefacts)"
                    )
            if (
                "base" in dims
                and "normal" in dims
                and (dims["normal"][0] > dims["base"][0] or dims["normal"][1] > dims["base"][1])
            ):
                self.r.error("tex.size", f"material '{name}': normal map larger than base colour")
            if role in MASK_ROLES and "base" in dims and mat.get("alphaMode") != "MASK":
                self.r.error(
                    "tex.alpha_mode",
                    f"material '{name}' ({role}): alphaMode must be MASK, "
                    f"is {mat.get('alphaMode', 'OPAQUE')}",
                )
        if sizes:
            self.r.stats["textures"] = len(infos)

    def fit(self) -> None:
        for issue in check_fit(self.g):
            if issue.level == ERROR:
                self.r.error(issue.code, issue.message)
            else:
                self.r.warning(issue.code, issue.message)

    def _check_skin_joints(self, skin_index: int, skin: dict[str, Any], mesh_node: int) -> None:
        joints = skin.get("joints", [])
        outside = [self.name(j) for j in joints if self.name(j) not in self.bone_index]
        if outside:
            self.r.error(
                "skin.joints",
                f"skin {skin_index} uses joints outside the skeleton: {_listed(outside)}",
            )
        if "inverseBindMatrices" not in skin:
            return
        try:
            ibm = self.g.accessor(skin["inverseBindMatrices"])
        except GltfError as e:
            self.r.error("skin.bind", f"skin {skin_index}: {e}")
            return
        if len(ibm) != len(joints):
            self.r.error(
                "skin.bind", f"skin {skin_index}: inverse bind matrix count != joint count"
            )
            return
        mesh_inv = np.linalg.inv(self.world[mesh_node])
        off = [
            self.name(j)
            for j, m in zip(joints, ibm, strict=True)
            if np.max(np.abs(mesh_inv @ self.world[j] @ m - np.eye(4))) > self.tol.bind_matrix
        ]
        if off:
            self.r.error(
                "skin.bind",
                "joint nodes are not in the bind pose of the skin (export with rest position): "
                + _listed(off),
            )

    def _position_bounds(self, prim: dict[str, Any]) -> tuple[float | None, float | None]:
        idx = prim.get("attributes", {}).get("POSITION")
        if idx is None:
            return None, None
        acc = self.g.list("accessors")[idx]
        if "min" in acc and "max" in acc:
            return float(acc["min"][1]), float(acc["max"][1])
        try:
            pos = self.g.accessor(idx)
        except GltfError:
            return None, None
        return float(pos[:, 1].min()), float(pos[:, 1].max())

    def _check_weights(self, prim: dict[str, Any], joints: list[int], where: str) -> None:
        attrs = prim.get("attributes", {})
        if any(k.startswith(("JOINTS_", "WEIGHTS_")) and not k.endswith("_0") for k in attrs):
            self.r.error(
                "skin.influences",
                f"{where}: more than {self.rig.max_influences} bone influences per vertex "
                "(JOINTS_1/WEIGHTS_1 present)",
            )
        if "JOINTS_0" not in attrs or "WEIGHTS_0" not in attrs:
            self.r.error("skin.weights", f"{where}: skinned mesh without JOINTS_0/WEIGHTS_0")
            return
        try:
            j = self.g.accessor(attrs["JOINTS_0"]).astype(np.int64)
            w = self.g.accessor(attrs["WEIGHTS_0"]).astype(np.float64)
        except GltfError as e:
            self.r.error("skin.weights", f"{where}: {e}")
            return
        if j.shape != w.shape:
            self.r.error("skin.weights", f"{where}: JOINTS_0 and WEIGHTS_0 differ in size")
            return
        used = w > 1e-6
        if np.any(j[used] >= len(joints)) or np.any(j[used] < 0):
            self.r.error("skin.weights", f"{where}: joint index out of range")
            return
        sums = w.sum(axis=1)
        bad = int(np.count_nonzero(np.abs(sums - 1.0) > self.tol.weight_sum))
        if bad:
            self.r.error("skin.weights", f"{where}: {bad} vertices with weights not summing to 1")
        socket_joints = {
            k for k, node in enumerate(joints) if self.name(node).startswith("socket_")
        }
        if socket_joints:
            hit = sorted(
                {self.name(joints[k]) for k in np.unique(j[used]) if int(k) in socket_joints}
            )
            if hit:
                self.r.error(
                    "skin.socket_weights", f"{where}: sockets carry weights: {_listed(hit)}"
                )

    # --- animations ------------------------------------------------------------------------

    def animations(self) -> None:
        anims = self.g.list("animations")
        names: dict[str, float] = {}
        for ai, anim in enumerate(anims):
            name = str(anim.get("name", f"#{ai}"))
            if not is_clip_name(name):
                self.r.error("anim.name", f"clip '{name}' violates the naming convention (§3)")
            elif self.rig.is_monster and clip_mode(name) != self.rig.species:
                self.r.error(
                    "anim.name", f"clip '{name}' must start with '{self.rig.species}/' (§7)"
                )
            elif not self.rig.is_monster and is_monster_clip(name):
                self.r.error("anim.name", f"monster clip '{name}' in a human file")
            if name in names:
                self.r.error("anim.name", f"duplicate clip '{name}'")
            foreign = sorted(
                {
                    self.name(ch["target"]["node"])
                    for ch in anim.get("channels", [])
                    if "node" in ch.get("target", {})
                    and self.name(ch["target"]["node"]) not in self.bone_index
                }
            )
            if foreign:
                self.r.error(
                    "anim.target",
                    f"clip '{name}' animates nodes outside the skeleton: {_listed(foreign)}",
                )
            self._check_channels(name, anim)
            self._check_motion(name, anim)
            names[name] = self._duration(anim)
            if self.rig.is_monster:
                self._check_monster_root(name, anim, names[name])
        self.r.stats["clips"] = len(anims)
        self._events(names)
        if self.g.path is not None:  # feet that stand, lying poses above the ground (clipfix)
            for code, message in check_set(self.g.path, self.g):
                self.r.error(code, message)

    def _check_channels(self, clip: str, anim: dict[str, Any]) -> None:
        """Translation only on root/pelvis, no scale (keeps each figure's bone lengths)."""
        moved, scaled = set(), set()
        for ch in anim.get("channels", []):
            target = ch.get("target", {})
            if "node" not in target:
                continue
            bone = self.name(target["node"])
            if target.get("path") == "translation" and bone not in TRANSLATED_BONES:
                moved.add(bone)
            elif target.get("path") == "scale":
                scaled.add(bone)
        if moved:
            self.r.error(
                "anim.channels",
                f"clip '{clip}' translates bones other than root/pelvis: {_listed(sorted(moved))} "
                "(export with gothar-chargen export)",
            )
        if scaled:
            self.r.error(
                "anim.channels", f"clip '{clip}' has scale channels: {_listed(sorted(scaled))}"
            )

    def _check_motion(self, clip: str, anim: dict[str, Any]) -> None:
        """Jumps between consecutive keys and, for loops (s_*), a closed cycle."""
        t = self.tol
        jumps, warn, open_loop = [], [], []
        loop = is_loop_clip(clip)
        for ch in anim.get("channels", []):
            target = ch.get("target", {})
            if "node" not in target or target.get("path") not in ("rotation", "translation"):
                continue
            sampler = anim["samplers"][ch["sampler"]]
            try:
                values = self.g.accessor(sampler["output"]).astype(np.float64)
            except GltfError:
                continue
            if sampler.get("interpolation") == "CUBICSPLINE" or len(values) < 2:
                continue
            bone = self.name(target["node"])
            if target["path"] == "rotation":
                values /= np.maximum(np.linalg.norm(values, axis=1, keepdims=True), 1e-12)
                dots = np.abs(np.sum(values[1:] * values[:-1], axis=1)).clip(0, 1)
                step = float(np.degrees(2 * np.arccos(dots)).max())
                if step > t.jump_rotation_error_deg:
                    jumps.append(f"{bone} {step:.0f}°")
                elif step > t.jump_rotation_warn_deg:
                    warn.append(f"{bone} {step:.0f}°")
                end = float(np.degrees(2 * np.arccos(min(1.0, abs(float(values[0] @ values[-1]))))))
                if loop and end > t.loop_rotation_deg:
                    open_loop.append(f"{bone} {end:.0f}°")
            else:
                step = float(np.linalg.norm(np.diff(values, axis=0), axis=1).max())
                if step > t.jump_translation_m:
                    jumps.append(f"{bone} {step:.2f} m")
        if jumps:
            self.r.error("anim.jump", f"clip '{clip}' jumps between frames: {_listed(jumps)}")
        if warn:
            self.r.warning("anim.jump", f"clip '{clip}' has very fast rotations: {_listed(warn)}")
        if open_loop:
            self.r.warning(
                "anim.loop", f"loop '{clip}' does not end where it starts: {_listed(open_loop)}"
            )

    def _root_keys(self, anim: dict[str, Any], path: str) -> np.ndarray | None:
        root = self.bone_index.get(_ROOT)
        for ch in anim.get("channels", []):
            target = ch.get("target", {})
            if target.get("node") == root and target.get("path") == path:
                sampler = anim["samplers"][ch["sampler"]]
                return self.g.accessor(sampler["output"]).astype(np.float64)
        return None

    def _check_monster_root(self, clip: str, anim: dict[str, Any], duration: float) -> None:
        """Contract §7: s_walk/s_run move forward in the root channel, t_turn_l/r turn the root."""
        action = clip.rsplit("/", 1)[-1]
        if action in ("s_walk", "s_run"):
            keys = self._root_keys(anim, "translation")
            forward = 0.0 if keys is None or len(keys) < 2 else float(keys[-1][2] - keys[0][2])
            speed = forward / duration if duration > 0 else 0.0
            if speed < self.tol.monster_advance_min_m_s:
                self.r.error(
                    "anim.root_motion",
                    f"clip '{clip}' must move 'root' forward (+Z), got {speed:.2f} m/s",
                )
        elif action in ("t_turn_l", "t_turn_r"):
            keys = self._root_keys(anim, "rotation")
            yaw = 0.0 if keys is None or len(keys) < 2 else _yaw_deg(keys[0], keys[-1])
            left = action.endswith("_l")
            if (yaw if left else -yaw) < self.tol.monster_turn_min_deg:
                side = "left" if left else "right"
                self.r.error(
                    "anim.root_motion",
                    f"clip '{clip}' must turn 'root' about +Y to the {side} "
                    f"(>= {self.tol.monster_turn_min_deg:.0f}°), got {yaw:.0f}°",
                )

    def _duration(self, anim: dict[str, Any]) -> float:
        end = 0.0
        accessors = self.g.list("accessors")
        for s in anim.get("samplers", []):
            acc = accessors[s["input"]]
            if "max" in acc:
                end = max(end, float(acc["max"][0]))
            else:
                with contextlib.suppress(GltfError):
                    end = max(end, float(self.g.accessor(s["input"]).max(initial=0.0)))
        return end

    def _events(self, clips: dict[str, float]) -> None:
        path = self.g.path
        if path is None:
            return
        ev_path = events_path_for(path)
        if not ev_path.is_file():
            return
        ev_file, errors = load_events(ev_path)
        for e in errors:
            self.r.error("events.format", f"{ev_path.name}: {e}")
        if ev_file is None:
            return
        for clip, events in ev_file.clips.items():
            if clip not in clips:
                self.r.error("events.clip", f"{ev_path.name}: clip '{clip}' not in {path.name}")
                continue
            last = round(clips[clip] * ev_file.fps)
            # loops (s_*): the last frame equals frame 0, so events must lie before it
            loop = is_loop_clip(clip)
            late = [
                f"{e.name}@{e.frame}"
                for e in events
                if e.frame > last or (loop and e.frame == last)
            ]
            if late:
                bound = f"< {last} (loop: last frame = frame 0)" if loop else f"<= {last}"
                self.r.error(
                    "events.frame",
                    f"{ev_path.name}: clip '{clip}' allows frames {bound}, got " + _listed(late),
                )
        self.r.stats["events"] = sum(len(v) for v in ev_file.clips.values())
        measured = clip_speeds(self.g)
        for clip in sorted(set(measured) | set(ev_file.speeds)):
            have, want = ev_file.speeds.get(clip), measured.get(clip)
            if have is None or want is None or abs(have - want) > SPEED_TOLERANCE * want:
                self.r.error(
                    "events.speed",
                    f"{ev_path.name}: clip '{clip}' speed {have} m/s, measured {want} m/s "
                    "(gothar-chargen speeds)",
                )


def _rotate(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Rotates v by the glTF quaternion q = (x, y, z, w)."""
    u, w = q[:3], q[3]
    return v + 2.0 * np.cross(u, np.cross(u, v) + w * v)


def _quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Hamilton product of glTF quaternions (x, y, z, w)."""
    (ax, ay, az, aw), (bx, by, bz, bw) = a, b
    return np.array(
        [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ]
    )


def _yaw_deg(q0: np.ndarray, q1: np.ndarray) -> float:
    """Turn about +Y (parent space) from key q0 to key q1 in degrees, left (+X) positive.

    Uses the change q1 * q0^-1, so the bone's own rest rotation does not matter.
    """
    inverse = np.array([-q0[0], -q0[1], -q0[2], q0[3]]) / float(np.dot(q0, q0))
    forward = _rotate(_quat_mul(q1, inverse), np.array([0.0, 0.0, 1.0]))
    return float(np.degrees(np.arctan2(forward[0], forward[2])))


def is_part_file(path: Path) -> bool:
    """Files below a ``parts`` folder are figure parts (body, head, hair ...), not whole figures."""
    return "parts" in path.parts


def validate_gltf(
    gltf: Gltf,
    rig: RigSpec,
    reference: ReferencePose | None = None,
    tolerances: Tolerances | None = None,
    path: Path | None = None,
) -> Report:
    report = Report(path or gltf.path or Path("<memory>"))
    checker = _Checker(gltf, rig, reference, tolerances or Tolerances(), report)
    try:
        if checker.skeleton():
            checker.orientation()
            checker.bind_pose()
        part_file = is_part_file(report.path)
        checker.meshes(part_file)
        checker.collision()
        checker.lods(part_file)
        checker.textures()
        if not part_file:
            checker.fit()
        checker.animations()
    except (GltfError, KeyError, IndexError, TypeError, ValueError) as e:
        report.error("gltf.structure", f"malformed glTF: {e}")
    return report


def validate_file(
    path: Path,
    rig: RigSpec,
    reference: ReferencePose | None = None,
    tolerances: Tolerances | None = None,
) -> Report:
    try:
        gltf = Gltf.load(path)
    except GltfError as e:
        report = Report(path)
        report.error("gltf.parse", str(e))
        return report
    return validate_gltf(gltf, rig, reference, tolerances, path)
