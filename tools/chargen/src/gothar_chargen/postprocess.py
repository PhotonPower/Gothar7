"""Post-processing of exported .glb files (after Blender's glTF export).

Blender's sampled animation export writes translation, rotation and scale for every bone. The
contract (characters-pipeline.md §3) allows translation only on ``root`` and ``pelvis`` and no
scale, so clips keep each figure's own bone lengths. ``strip_animation_channels`` removes the rest
and ``compact`` drops the now unused accessors, buffer views and binary data.
"""

from __future__ import annotations

from typing import Any

from gothar_chargen.gltf import Gltf

TRANSLATED_BONES = frozenset({"root", "pelvis"})


def strip_animation_channels(gltf: Gltf, translated: frozenset[str] = TRANSLATED_BONES) -> int:
    """Removes scale channels and translation channels outside ``translated``; returns the count."""
    nodes = gltf.list("nodes")
    removed = 0
    for anim in gltf.list("animations"):
        keep: list[dict[str, Any]] = []
        for ch in anim.get("channels", []):
            target = ch.get("target", {})
            name = nodes[target["node"]].get("name") if "node" in target else None
            path = target.get("path")
            if path == "scale" or (path == "translation" and name not in translated):
                removed += 1
            else:
                keep.append(ch)
        # drop unused samplers and renumber
        used = sorted({ch["sampler"] for ch in keep})
        remap = {old: new for new, old in enumerate(used)}
        anim["samplers"] = [anim["samplers"][i] for i in used]
        for ch in keep:
            ch["sampler"] = remap[ch["sampler"]]
        anim["channels"] = keep
    if removed:
        compact(gltf)
    return removed


def _used_accessors(doc: dict[str, Any]) -> set[int]:
    used: set[int] = set()
    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            used.update(prim.get("attributes", {}).values())
            if "indices" in prim:
                used.add(prim["indices"])
            for target in prim.get("targets", []):
                used.update(target.values())
    for skin in doc.get("skins", []):
        if "inverseBindMatrices" in skin:
            used.add(skin["inverseBindMatrices"])
    for anim in doc.get("animations", []):
        for s in anim.get("samplers", []):
            used.update((s["input"], s["output"]))
    return used


def compact(gltf: Gltf) -> None:
    """Removes unreferenced accessors and buffer views and repacks the binary chunk."""
    doc = gltf.doc
    accessors = doc.get("accessors", [])
    views = doc.get("bufferViews", [])
    used_acc = sorted(_used_accessors(doc))
    acc_map = {old: new for new, old in enumerate(used_acc)}

    used_views: set[int] = set()
    for i in used_acc:
        acc = accessors[i]
        if "bufferView" in acc:
            used_views.add(acc["bufferView"])
        sparse = acc.get("sparse")
        if sparse:
            used_views.update((sparse["indices"]["bufferView"], sparse["values"]["bufferView"]))
    for img in doc.get("images", []):
        if "bufferView" in img:
            used_views.add(img["bufferView"])

    new_bin = bytearray()
    view_map: dict[int, int] = {}
    new_views = []
    for old in sorted(used_views):
        view = dict(views[old])
        start = int(view.get("byteOffset", 0))
        data = gltf.bin[start : start + int(view["byteLength"])]
        new_bin += b"\0" * (-len(new_bin) % 4)
        view["byteOffset"] = len(new_bin)
        new_bin += data
        view_map[old] = len(new_views)
        new_views.append(view)

    new_accessors = []
    for old in used_acc:
        acc = dict(accessors[old])
        if "bufferView" in acc:
            acc["bufferView"] = view_map[acc["bufferView"]]
        if "sparse" in acc:
            for part in ("indices", "values"):
                acc["sparse"][part]["bufferView"] = view_map[acc["sparse"][part]["bufferView"]]
        new_accessors.append(acc)

    def remap(index: int) -> int:
        return acc_map[index]

    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            prim["attributes"] = {k: remap(v) for k, v in prim.get("attributes", {}).items()}
            if "indices" in prim:
                prim["indices"] = remap(prim["indices"])
            if "targets" in prim:
                prim["targets"] = [{k: remap(v) for k, v in t.items()} for t in prim["targets"]]
    for skin in doc.get("skins", []):
        if "inverseBindMatrices" in skin:
            skin["inverseBindMatrices"] = remap(skin["inverseBindMatrices"])
    for anim in doc.get("animations", []):
        for s in anim.get("samplers", []):
            s["input"], s["output"] = remap(s["input"]), remap(s["output"])
    for img in doc.get("images", []):
        if "bufferView" in img:
            img["bufferView"] = view_map[img["bufferView"]]

    doc["accessors"] = new_accessors
    doc["bufferViews"] = new_views
    gltf.bin = bytes(new_bin)
    if doc.get("buffers"):
        doc["buffers"][0]["byteLength"] = len(gltf.bin)
