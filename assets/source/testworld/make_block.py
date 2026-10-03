"""Writes block.gltf: a 1 x 1 x 1 m stone block standing on its origin (y 0..1), own content (CC0).

The test world scales it into climbing blocks (M5: jump and ledges). Run from this folder:
    python make_block.py
"""

import base64
import json
import struct

FACES = [  # normal, four corners (counter-clockwise seen from outside)
    ((1, 0, 0), [(0.5, 0, 0.5), (0.5, 0, -0.5), (0.5, 1, -0.5), (0.5, 1, 0.5)]),
    ((-1, 0, 0), [(-0.5, 0, -0.5), (-0.5, 0, 0.5), (-0.5, 1, 0.5), (-0.5, 1, -0.5)]),
    ((0, 1, 0), [(-0.5, 1, 0.5), (0.5, 1, 0.5), (0.5, 1, -0.5), (-0.5, 1, -0.5)]),
    ((0, -1, 0), [(-0.5, 0, -0.5), (0.5, 0, -0.5), (0.5, 0, 0.5), (-0.5, 0, 0.5)]),
    ((0, 0, 1), [(-0.5, 0, 0.5), (0.5, 0, 0.5), (0.5, 1, 0.5), (-0.5, 1, 0.5)]),
    ((0, 0, -1), [(0.5, 0, -0.5), (-0.5, 0, -0.5), (-0.5, 1, -0.5), (0.5, 1, -0.5)]),
]


def main() -> None:
    positions: list[float] = []
    normals: list[float] = []
    indices: list[int] = []
    for normal, corners in FACES:
        base = len(positions) // 3
        for corner in corners:
            positions.extend(corner)
            normals.extend(normal)
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    pos_bytes = struct.pack(f"<{len(positions)}f", *positions)
    nrm_bytes = struct.pack(f"<{len(normals)}f", *normals)
    idx_bytes = struct.pack(f"<{len(indices)}H", *indices)
    buffer = pos_bytes + nrm_bytes + idx_bytes
    gltf = {
        "asset": {"version": "2.0", "generator": "make_block.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "block"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": "stone", "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.53, 0.5, 1.0]}}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 24, "type": "VEC3",
             "min": [-0.5, 0.0, -0.5], "max": [0.5, 1.0, 0.5]},
            {"bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": 36, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes), "byteLength": len(nrm_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes) + len(nrm_bytes), "byteLength": len(idx_bytes)},
        ],
        "buffers": [{"byteLength": len(buffer),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode()}],
    }
    with open("block.gltf", "w", encoding="utf-8", newline="\n") as f:
        json.dump(gltf, f, indent=1)
        f.write("\n")


if __name__ == "__main__":
    main()
