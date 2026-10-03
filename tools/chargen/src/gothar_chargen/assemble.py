"""Assembles figures from their manifest and parts – pure Python + numpy, no Blender (§6.2).

`gothar-chargen assemble` writes ``figures/<name>.glb`` for every ``figures/<name>.figure.toml``;
the results are not versioned (generated at build time, then cooked by g7-cook). Steps, all driven
by data the parts carry (partdata.py), so the engine can do the same at run time later:

1. Skeleton and skin from the body part; every part's LOD meshes become nodes ``<role>_lod<n>``
   (joints remapped by bone name onto the body's skin).
2. Body triangles hidden by the garments are dropped (``covers`` masks of each garment).
3. Neck: the body's ring is moved onto the head's ring, nearby body vertices follow (``falloff``).
   The rings are paired by the cyclic shift and direction with the smallest total distance.
4. Materials merged by name (``skin.001`` -> ``skin``); the head's skin wins, so body and face share
   one texture. Palette colours become ``baseColorFactor`` (the texture files stay shared).
5. Output is deterministic; ``asset.extras.gothar.inputs`` hashes the manifest and the parts.
"""

from __future__ import annotations

import copy
import hashlib
import json
import os
import re
from pathlib import Path
from typing import Any

import numpy as np

from gothar_chargen.figure import CLOTH_PREFIX, Figure, load_figure
from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.human import HIDEABLE
from gothar_chargen.meshdata import split_lod
from gothar_chargen.partdata import data_of, geometry_hash

GENERATOR = "gothar-chargen assemble"
KEEP = ("placeholder_mannequin.glb",)  # versioned figures that have no manifest
_DUP = re.compile(r"^(?P<base>.+)\.\d{3}$")
_FLOAT, _UBYTE, _USHORT, _UINT = 5126, 5121, 5123, 5125
_ARRAY_BUFFER, _ELEMENT_ARRAY_BUFFER = 34962, 34963
_NP = {_FLOAT: np.float32, _UBYTE: np.uint8, _USHORT: np.uint16, _UINT: np.uint32}
_TYPES = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}


NECK_LIFT_MAX = 0.006  # metres the neck seam may move up or down when snapping


class AssembleError(Exception):
    """A figure cannot be assembled (missing part, stale data, mismatching rings ...)."""


def _base(name: str) -> str:
    m = _DUP.match(name)
    return m["base"] if m else name


class _Builder:
    """Collects the new document and binary chunk."""

    def __init__(self) -> None:
        self.bin = bytearray()
        self.doc: dict[str, Any] = {
            "asset": {},
            "scene": 0,
            "scenes": [],
            "nodes": [],
            "meshes": [],
            "skins": [],
            "materials": [],
            "textures": [],
            "images": [],
            "samplers": [],
            "accessors": [],
            "bufferViews": [],
            "buffers": [{"byteLength": 0}],
        }
        self._copied: dict[tuple[int, int], int] = {}

    def _view(self, data: bytes, target: int | None) -> int:
        self.bin += b"\0" * (-len(self.bin) % 4)
        view: dict[str, Any] = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.bin += data
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def _src_view(self, src: Gltf, view: int) -> bytes:
        v = src.doc["bufferViews"][view]
        if v.get("byteStride"):
            raise AssembleError("interleaved buffers are not supported")
        start = int(v.get("byteOffset", 0))
        return src.bin[start : start + int(v["byteLength"])]

    def copy_accessor(self, src: Gltf, index: int) -> int:
        key = (id(src), index)
        if key in self._copied:
            return self._copied[key]
        acc = copy.deepcopy(src.doc["accessors"][index])
        if "bufferView" in acc:
            view = src.doc["bufferViews"][acc["bufferView"]]
            data = self._src_view(src, acc["bufferView"])
            off = int(acc.pop("byteOffset", 0))
            size = (
                int(acc["count"]) * _comp_count(acc) * np.dtype(_NP[acc["componentType"]]).itemsize
            )
            acc["bufferView"] = self._view(data[off : off + size], view.get("target"))
        if "sparse" in acc:
            for part in ("indices", "values"):
                sp = acc["sparse"][part]
                data = self._src_view(src, sp["bufferView"])
                off = int(sp.pop("byteOffset", 0))
                sp["bufferView"] = self._view(data[off:], None)
        self.doc["accessors"].append(acc)
        self._copied[key] = len(self.doc["accessors"]) - 1
        return self._copied[key]

    def add_array(self, arr: np.ndarray, component: int, target: int, minmax: bool = False) -> int:
        arr = np.ascontiguousarray(arr.astype(_NP[component]))
        count, ncomp = (arr.shape[0], 1) if arr.ndim == 1 else arr.shape
        acc: dict[str, Any] = {
            "bufferView": self._view(arr.tobytes(), target),
            "componentType": component,
            "count": int(count),
            "type": _TYPES[int(ncomp)],
        }
        if minmax:
            acc["min"] = [float(x) for x in arr.reshape(count, -1).min(axis=0)]
            acc["max"] = [float(x) for x in arr.reshape(count, -1).max(axis=0)]
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def result(self) -> Gltf:
        doc = {k: v for k, v in self.doc.items() if v != []}
        doc["buffers"] = [{"byteLength": len(self.bin)}]
        return Gltf(doc=doc, bin=bytes(self.bin))


def _comp_count(acc: dict[str, Any]) -> int:
    return {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]


def _mesh_nodes(g: Gltf) -> dict[int, int]:
    """LOD level -> node index of a part's mesh nodes."""
    out = {}
    for i, n in enumerate(g.list("nodes")):
        if "mesh" in n:
            _, level = split_lod(str(n.get("name", "")))
            out[0 if level is None else level] = i
    return out


def _pair_rings(body: np.ndarray, head: np.ndarray) -> np.ndarray:
    """Index into `head` for each body ring point: cyclic shift and direction, least distance."""
    n = len(body)
    best = None
    for order in (np.arange(n), np.arange(n)[::-1]):
        for shift in range(n):
            idx = np.roll(order, -shift)
            cost = float(np.linalg.norm(body - head[idx], axis=1).sum())
            if best is None or cost < best[0]:
                best = (cost, idx)
    assert best is not None
    return best[1]


def _image_uri(uri: str, part: Path, out: Path) -> str:
    target = (part.parent / uri).resolve()
    try:
        return os.path.relpath(target, out.parent.resolve()).replace("\\", "/")
    except ValueError as e:  # Windows: another drive
        raise AssembleError(f"{out}: output must be on the same drive as the parts ({e})") from e


def inputs_hash(manifest: Path, parts: dict[str, Path]) -> str:
    h = hashlib.sha256(manifest.read_bytes())
    for role in sorted(parts):
        h.update(role.encode())
        h.update(parts[role].read_bytes())
    return h.hexdigest()[:16]


ROLE_ORDER = ("body", "head", "hair", "beard")  # then the garments in list order (= engine, M6 D2)


def _role_order(figure: Figure, hidden: set[str] | frozenset[str] = frozenset()) -> list[str]:
    """Parts in the fixed order of the contract (§6.2), independent of the manifest's key order:
    body, head, hair, beard, then the garments as listed; the engine assembles the same way."""
    roles = [r for r in ROLE_ORDER if r in figure.parts]
    roles += [r for r in figure.parts if r not in ROLE_ORDER]
    return [r for r in roles if r not in hidden]


def assemble_figure(figure: Figure, manifest: Path, characters: Path, out: Path) -> Gltf:
    paths = figure.part_paths(characters)
    for role, path in paths.items():
        if not path.is_file():
            raise AssembleError(f"{figure.name}: part '{role}' not found: {path}")
    parts = {role: Gltf.load(p) for role, p in paths.items()}
    body, head = parts["body"], parts["head"]
    body_data, head_data = data_of(body), data_of(head)
    if "neck" not in body_data or "neck" not in head_data:
        raise AssembleError(
            f"{figure.name}: body/head without neck data (gothar-chargen part-data)"
        )
    garments = {r: g for r, g in parts.items() if r.startswith(CLOTH_PREFIX)}
    for role, g in garments.items():
        covers = data_of(g).get("covers")
        if covers is None:
            raise AssembleError(f"{figure.name}: {role} has no covers data")
        if covers["body"] != figure.parts["body"] or covers["body_hash"] != geometry_hash(body):
            raise AssembleError(
                f"{figure.name}: {role} was fitted to {covers['body']}, not to this body "
                "(or the body changed: run gothar-chargen part-data)"
            )
    # pieces worn over the head drop whole roles (helmets and hoods: the hair); union over pieces
    hidden = set().union(*(data_of(g).get("hides", []) for g in garments.values()))
    if hidden - set(HIDEABLE):
        raise AssembleError(f"{figure.name}: hides only {HIDEABLE}, got {sorted(hidden)}")
    parts = {r: g for r, g in parts.items() if r not in hidden}

    b = _Builder()
    b.doc["asset"] = {
        "generator": GENERATOR,
        "version": "2.0",
        "extras": {"gothar": {"figure": figure.name, "inputs": inputs_hash(manifest, paths)}},
    }

    # 1. skeleton from the body part: its nodes without the mesh nodes
    body_nodes = body.doc["nodes"]
    mesh_idx = set(_mesh_nodes(body).values())
    keep = [i for i in range(len(body_nodes)) if i not in mesh_idx]
    remap = {old: new for new, old in enumerate(keep)}
    for old in keep:
        node = copy.deepcopy(body_nodes[old])
        if "children" in node:
            node["children"] = [remap[c] for c in node["children"] if c in remap]
            if not node["children"]:
                del node["children"]
        b.doc["nodes"].append(node)
    arm_parent = next(
        (remap[p] for p, n in enumerate(body_nodes) if set(n.get("children", [])) & mesh_idx), 0
    )
    skin = copy.deepcopy(body.doc["skins"][0])
    skin["joints"] = [remap[j] for j in skin["joints"]]
    skin["inverseBindMatrices"] = b.copy_accessor(body, skin["inverseBindMatrices"])
    if "skeleton" in skin:
        skin["skeleton"] = remap[skin["skeleton"]]
    b.doc["skins"].append(skin)
    joint_slot = {b.doc["nodes"][j]["name"]: k for k, j in enumerate(skin["joints"])}
    scenes = copy.deepcopy(body.doc["scenes"])
    for sc in scenes:
        sc["nodes"] = [remap[n] for n in sc.get("nodes", []) if n in remap]
    b.doc["scenes"] = scenes

    # 4. materials (head first: its skin wins), textures, images, samplers
    material_of: dict[str, int] = {}
    image_of: dict[str, int] = {}
    sampler_of: dict[str, int] = {}
    order = ["head"] + [r for r in _role_order(figure, hidden) if r != "head"]
    part_material: dict[tuple[str, int], int] = {}
    for role in order:
        g = parts[role]
        for mi, mat in enumerate(g.list("materials")):
            base = _base(str(mat.get("name", f"material_{mi}")))
            if base not in material_of:
                new = copy.deepcopy(mat)
                new["name"] = base
                pbr = new.setdefault("pbrMetallicRoughness", {})
                for slot_owner, key in ((pbr, "baseColorTexture"), (new, "normalTexture")):
                    if key in slot_owner:
                        slot_owner[key]["index"] = _copy_texture(
                            b, g, slot_owner[key]["index"], paths[role], out, image_of, sampler_of
                        )
                if base in figure.palette:
                    pbr["baseColorFactor"] = [*(round(c, 6) for c in figure.palette[base]), 1.0]
                b.doc["materials"].append(new)
                material_of[base] = len(b.doc["materials"]) - 1
            part_material[(role, mi)] = material_of[base]

    # 2./3. meshes per role and LOD
    roles = _role_order(figure, hidden)
    head_lods = _mesh_nodes(head)
    for role in roles:
        g = parts[role]
        joint_map = _joint_map(g, joint_slot)
        for level, ni in sorted(_mesh_nodes(g).items()):
            node = g.doc["nodes"][ni]
            mesh = g.doc["meshes"][node["mesh"]]
            positions_override: dict[int, np.ndarray] = {}
            hidden: dict[int, list[tuple[int, int]]] = {}
            if role == "body":
                lod_name = str(node.get("name"))
                if level in head_lods:
                    positions_override = _neck_snap(
                        g, mesh, body_data, lod_name, head, head_data, head_lods[level]
                    )
                for gr in garments.values():
                    for prim, first, end in data_of(gr)["covers"]["lods"].get(lod_name, []):
                        hidden.setdefault(prim, []).append((first, end))
            new_mesh = {"name": f"{role}_lod{level}", "primitives": []}
            for key in ("extras", "weights"):
                if key in mesh:
                    new_mesh[key] = copy.deepcopy(mesh[key])
            for pi, prim in enumerate(mesh["primitives"]):
                new_prim = _copy_primitive(
                    b, g, prim, positions_override.get(pi), hidden.get(pi, []), joint_map
                )
                if new_prim is None:
                    continue  # fully hidden
                if "material" in prim:
                    new_prim["material"] = part_material[(role, prim["material"])]
                new_mesh["primitives"].append(new_prim)
            if not new_mesh["primitives"]:
                continue
            b.doc["meshes"].append(new_mesh)
            b.doc["nodes"].append(
                {"name": f"{role}_lod{level}", "mesh": len(b.doc["meshes"]) - 1, "skin": 0}
            )
            b.doc["nodes"][arm_parent].setdefault("children", []).append(len(b.doc["nodes"]) - 1)
    result = b.result()
    result.path = out
    return result


def _joint_map(g: Gltf, joint_slot: dict[str, int]) -> np.ndarray | None:
    """Slot in the part's skin -> slot in the figure's skin (None: identical)."""
    joints = g.doc["skins"][0]["joints"]
    names = [g.doc["nodes"][j]["name"] for j in joints]
    try:
        mapping = np.array([joint_slot[n] for n in names], dtype=np.int64)
    except KeyError as e:
        raise AssembleError(f"bone {e} of a part is not in the body's skeleton") from e
    return None if np.array_equal(mapping, np.arange(len(mapping))) else mapping


def _copy_texture(
    b: _Builder,
    g: Gltf,
    tex_index: int,
    part: Path,
    out: Path,
    image_of: dict[str, int],
    sampler_of: dict[str, int],
) -> int:
    tex = copy.deepcopy(g.doc["textures"][tex_index])
    image = copy.deepcopy(g.doc["images"][tex["source"]])
    if "uri" in image:
        image["uri"] = _image_uri(image["uri"], part, out)
        key = image["uri"]
    else:
        raise AssembleError(f"{part.name}: embedded images are not supported (externalize them)")
    if key not in image_of:
        b.doc["images"].append(image)
        image_of[key] = len(b.doc["images"]) - 1
    tex["source"] = image_of[key]
    if "sampler" in tex:
        sampler = g.doc["samplers"][tex["sampler"]]
        skey = json.dumps(sampler, sort_keys=True)
        if skey not in sampler_of:
            b.doc["samplers"].append(copy.deepcopy(sampler))
            sampler_of[skey] = len(b.doc["samplers"]) - 1
        tex["sampler"] = sampler_of[skey]
    for i, existing in enumerate(b.doc["textures"]):
        if existing == tex:
            return i
    b.doc["textures"].append(tex)
    return len(b.doc["textures"]) - 1


def _copy_primitive(
    b: _Builder,
    g: Gltf,
    prim: dict[str, Any],
    positions: np.ndarray | None,
    hidden: list[tuple[int, int]],
    joint_map: np.ndarray | None,
) -> dict[str, Any] | None:
    new: dict[str, Any] = {"attributes": {}}
    for name, acc in prim["attributes"].items():
        if name == "POSITION" and positions is not None:
            new["attributes"][name] = b.add_array(positions, _FLOAT, _ARRAY_BUFFER, minmax=True)
        elif name.startswith("JOINTS_") and joint_map is not None:
            src = g.doc["accessors"][acc]
            remapped = joint_map[g.accessor(acc).astype(np.int64)]
            comp = _USHORT if remapped.max(initial=0) > 255 else src["componentType"]
            new["attributes"][name] = b.add_array(remapped, comp, _ARRAY_BUFFER)
        else:
            new["attributes"][name] = b.copy_accessor(g, acc)
    if "indices" in prim:
        if hidden:
            tris = g.accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
            keep = np.ones(len(tris), dtype=bool)
            for first, end in hidden:
                keep[first:end] = False
            if not keep.any():
                return None
            comp = g.doc["accessors"][prim["indices"]]["componentType"]
            new["indices"] = b.add_array(tris[keep].reshape(-1), comp, _ELEMENT_ARRAY_BUFFER)
        else:
            new["indices"] = b.copy_accessor(g, prim["indices"])
    if "targets" in prim:
        new["targets"] = [{k: b.copy_accessor(g, a) for k, a in t.items()} for t in prim["targets"]]
    if "mode" in prim:
        new["mode"] = prim["mode"]
    return new


def _neck_snap(
    body: Gltf,
    mesh: dict[str, Any],
    body_data: dict[str, Any],
    body_node: str,
    head: Gltf,
    head_data: dict[str, Any],
    head_node_index: int,
) -> dict[int, np.ndarray]:
    """New body positions per primitive: ring onto the head's ring, falloff vertices follow."""
    ring = body_data["neck"].get(body_node)
    head_node = str(head.doc["nodes"][head_node_index].get("name"))
    head_ring = head_data["neck"].get(head_node)
    if not ring or not head_ring:
        raise AssembleError(f"no neck ring for {body_node} / {head_node}")
    if len(ring) != len(head_ring):
        raise AssembleError(
            f"neck rings differ: {body_node} {len(ring)} vs {head_node} {len(head_ring)} points"
        )
    positions = {
        pi: body.accessor(p["attributes"]["POSITION"]).astype(np.float64).copy()
        for pi, p in enumerate(mesh["primitives"])
    }
    head_mesh = head.doc["meshes"][head.doc["nodes"][head_node_index]["mesh"]]
    head_pos = {
        pi: head.accessor(p["attributes"]["POSITION"]).astype(np.float64)
        for pi, p in enumerate(head_mesh["primitives"])
    }
    body_pts = np.array([positions[pt[0][0]][pt[0][1]] for pt in ring])
    head_pts = np.array([head_pos[pt[0][0]][pt[0][1]] for pt in head_ring])
    pairing = _pair_rings(body_pts, head_pts)
    delta = head_pts[pairing] - body_pts
    lift = float(delta[:, 1].mean())  # widths differ between builds, heights must not
    if abs(lift) > NECK_LIFT_MAX:
        raise AssembleError(
            f"{head_node} sits {lift * 1000:+.0f} mm above the neck of {body_node} (max "
            f"{NECK_LIFT_MAX * 1000:.0f} mm): rebuild the head (gothar-chargen human)"
        )
    for prim, vertex, k, weight in body_data.get("falloff", {}).get(body_node, []):
        positions[int(prim)][int(vertex)] += delta[int(k)] * float(weight)
    for k, point in enumerate(ring):
        for prim, vertex in point:
            positions[prim][vertex] = head_pts[pairing[k]]
    return positions


def figure_out_path(manifest: Path) -> Path:
    return manifest.with_name(manifest.name[: -len(".figure.toml")] + ".glb")


def assemble_all(
    figures_dir: Path, characters: Path, manifests: list[Path] | None = None
) -> list[Path]:
    """Assembles the given (default: all) manifests; removes outputs whose manifest is gone."""
    all_manifests = sorted(figures_dir.glob("*.figure.toml"))
    written = []
    for manifest in manifests or all_manifests:
        figure = load_figure(manifest)
        out = figure_out_path(manifest)
        try:
            gltf = assemble_figure(figure, manifest, characters, out)
        except GltfError as e:
            raise AssembleError(f"{figure.name}: {e}") from e
        data = gltf.to_bytes()
        if not out.is_file() or out.read_bytes() != data:
            out.write_bytes(data)
        written.append(out)
    if manifests is None:
        expected = {figure_out_path(m).name for m in all_manifests} | set(KEEP)
        for stale in sorted(figures_dir.glob("*.glb")):
            if stale.name not in expected:
                stale.unlink()
    return written
