"""Writes window_glass.gltf: a plastered wall (4 x 3 x 0.3 m, front at z = 0, standing on its origin) with three
windows - wooden frames and panes of window glass marked as in the contract with welt (render.md "Spiegelnde
Fenster"): bullseye panes, diamond panes and a plain pane. Own content (CC0). Test model for M17 windows F1 until
welt delivers its houses. Run from this folder:
    python make_window.py
"""

import base64
import json
import struct

DARK = [0.06, 0.07, 0.065, 1.0]  # the room behind the pane


def box(x0: float, x1: float, y0: float, y1: float, z0: float, z1: float) -> list:
    """Faces (normal, four corners counter-clockwise from outside) of an axis-aligned box."""
    return [
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
    ]


def quad(x0: float, x1: float, y0: float, y1: float, z: float) -> list:
    return [((0, 0, 1), [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)])]


def main() -> None:
    windows = [-1.25, 0.0, 1.25]
    parts: list[tuple[str, list]] = [("plaster", box(-2.0, 2.0, 0.0, 3.0, -0.3, 0.0))]
    frames: list = []
    for x in windows:
        # Frame: four bars 8 cm wide, 4 cm proud of the wall, around a 0.76 x 1.06 m opening.
        frames += box(x - 0.46, x + 0.46, 1.05, 1.13, 0.0, 0.04)
        frames += box(x - 0.46, x + 0.46, 2.19, 2.27, 0.0, 0.04)
        frames += box(x - 0.46, x - 0.38, 1.13, 2.19, 0.0, 0.04)
        frames += box(x + 0.38, x + 0.46, 1.13, 2.19, 0.0, 0.04)
    parts.append(("frame", frames))
    for x, name in zip(windows, ["glass_bullseye", "glass_diamond", "glass_plain"]):
        parts.append((name, quad(x - 0.38, x + 0.38, 1.13, 2.19, 0.02)))

    buffer = b""
    views: list[dict] = []
    accessors: list[dict] = []
    primitives: list[dict] = []

    def add_view(data: bytes, target: int) -> int:
        nonlocal buffer
        while len(buffer) % 4:
            buffer += b"\0"
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(data), "target": target})
        buffer += data
        return len(views) - 1

    for material, faces in enumerate(parts):
        positions: list[float] = []
        normals: list[float] = []
        indices: list[int] = []
        for normal, corners in faces[1]:
            base = len(positions) // 3
            for corner in corners:
                positions.extend(corner)
                normals.extend(normal)
            indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        count = len(positions) // 3
        lo = [min(positions[i::3]) for i in range(3)]
        hi = [max(positions[i::3]) for i in range(3)]
        p = add_view(struct.pack(f"<{len(positions)}f", *positions), 34962)
        n = add_view(struct.pack(f"<{len(normals)}f", *normals), 34962)
        i = add_view(struct.pack(f"<{len(indices)}H", *indices), 34963)
        accessors.append({"bufferView": p, "componentType": 5126, "count": count, "type": "VEC3", "min": lo, "max": hi})
        accessors.append({"bufferView": n, "componentType": 5126, "count": count, "type": "VEC3"})
        accessors.append({"bufferView": i, "componentType": 5123, "count": len(indices), "type": "SCALAR"})
        a = len(accessors) - 3
        primitives.append({"attributes": {"POSITION": a, "NORMAL": a + 1}, "indices": a + 2, "material": material})

    def glass(name: str, pattern: str) -> dict:
        return {
            "name": name,
            "pbrMetallicRoughness": {"baseColorFactor": DARK},
            "extras": {"glass": True, "glass_pattern": pattern},
        }

    gltf = {
        "asset": {"version": "2.0", "generator": "make_window.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "window_glass"}],
        "meshes": [{"primitives": primitives}],
        "materials": [
            {"name": "plaster", "pbrMetallicRoughness": {"baseColorFactor": [0.72, 0.67, 0.58, 1.0]}},
            {"name": "frame", "pbrMetallicRoughness": {"baseColorFactor": [0.22, 0.14, 0.08, 1.0]}},
            glass("glass_bullseye", "bullseye"),
            glass("glass_diamond", "diamond"),
            glass("glass_plain", "plain"),
        ],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [
            {
                "byteLength": len(buffer),
                "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode(),
            }
        ],
    }
    with open("window_glass.gltf", "w", encoding="utf-8") as f:
        json.dump(gltf, f, indent=1)
        f.write("\n")


if __name__ == "__main__":
    main()
