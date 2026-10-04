"""Placeholder mob models for the test camp (M8 part C) until welt's models exist.

Axes as in the mob contract (characters-pipeline.md 3.1): Y up, origin on the ground, the front faces +Z.
Run: uv run --no-project python assets/source/testworld/mobs/make_placeholders.py
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

HERE = Path(__file__).parent

Box = tuple[tuple[float, float, float], tuple[float, float, float]]  # min, max


def box_geometry(
    lo: tuple[float, float, float], hi: tuple[float, float, float]
) -> tuple[list, list, list]:
    """24 vertices (flat normals), 36 indices."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    faces = [
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
    ]
    positions, normals, indices = [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, indices


def write_glb(
    path: Path,
    parts: list[
        tuple[str, list[Box], tuple[float, float, float], tuple[float, float, float]]
    ],
) -> None:
    """parts: (node name, boxes, node translation, colour). One mesh and material per part."""
    buffer = bytearray()
    accessors, views, meshes, nodes, materials = [], [], [], [], []

    def add_view(data: bytes, target: int) -> int:
        while len(buffer) % 4:
            buffer.append(0)
        views.append(
            {
                "buffer": 0,
                "byteOffset": len(buffer),
                "byteLength": len(data),
                "target": target,
            }
        )
        buffer.extend(data)
        return len(views) - 1

    for name, boxes, translation, colour in parts:
        positions, normals, indices = [], [], []
        for lo, hi in boxes:
            p, n, i = box_geometry(lo, hi)
            indices += [len(positions) + k for k in i]
            positions += p
            normals += n
        pos_view = add_view(b"".join(struct.pack("<3f", *v) for v in positions), 34962)
        nrm_view = add_view(b"".join(struct.pack("<3f", *v) for v in normals), 34962)
        idx_view = add_view(b"".join(struct.pack("<H", i) for i in indices), 34963)
        lo = [min(v[a] for v in positions) for a in range(3)]
        hi = [max(v[a] for v in positions) for a in range(3)]
        accessors += [
            {
                "bufferView": pos_view,
                "componentType": 5126,
                "count": len(positions),
                "type": "VEC3",
                "min": lo,
                "max": hi,
            },
            {
                "bufferView": nrm_view,
                "componentType": 5126,
                "count": len(normals),
                "type": "VEC3",
            },
            {
                "bufferView": idx_view,
                "componentType": 5123,
                "count": len(indices),
                "type": "SCALAR",
            },
        ]
        materials.append(
            {
                "name": name.lower(),
                "pbrMetallicRoughness": {
                    "baseColorFactor": [*colour, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.9,
                },
            }
        )
        a = len(accessors) - 3
        meshes.append(
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {"POSITION": a, "NORMAL": a + 1},
                        "indices": a + 2,
                        "material": len(materials) - 1,
                    }
                ],
            }
        )
        nodes.append(
            {"name": name, "mesh": len(meshes) - 1, "translation": list(translation)}
        )

    while len(buffer) % 4:
        buffer.append(0)
    gltf = {
        "asset": {"version": "2.0", "generator": "gothar make_placeholders.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buffer)}],
    }
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(buffer))
    glb += struct.pack("<II", len(text), 0x4E4F534A) + text
    glb += struct.pack("<II", len(buffer), 0x004E4942) + bytes(buffer)
    path.write_bytes(glb)
    print(f"{path.name}: {len(glb)} bytes")


WOOD = (0.45, 0.30, 0.17)
DARK = (0.30, 0.20, 0.12)
IRON = (0.32, 0.33, 0.36)
CLOTH = (0.65, 0.58, 0.45)

if __name__ == "__main__":
    # Chest 0.9 x 0.6 x 0.6: body up to 0.5 m, the lid (node MOB_LID) with its pivot at the back edge (hinge).
    write_glb(
        HERE / "chest.glb",
        [
            (
                "CHEST_BODY",
                [
                    ((-0.45, 0.0, -0.3), (0.45, 0.5, 0.3)),
                    ((-0.05, 0.38, 0.3), (0.05, 0.46, 0.33)),
                ],
                (0, 0, 0),
                WOOD,
            ),
            (
                "MOB_LID",
                [((-0.46, 0.0, 0.0), (0.46, 0.1, 0.62))],
                (0, 0.5, -0.31),
                DARK,
            ),
        ],
    )
    # Door blade 1.0 x 2.0 x 0.06, origin at the hinge bottom, the blade along +X; handle at 1.0 m.
    write_glb(
        HERE / "door.glb",
        [
            ("DOOR_BLADE", [((0.0, 0.0, -0.03), (1.0, 2.0, 0.03))], (0, 0, 0), WOOD),
            (
                "DOOR_HANDLE",
                [((0.82, 0.97, -0.07), (0.92, 1.03, 0.07))],
                (0, 0, 0),
                IRON,
            ),
        ],
    )
    # Anvil: working surface at 0.8 m.
    write_glb(
        HERE / "anvil.glb",
        [
            (
                "ANVIL",
                [
                    ((-0.2, 0.0, -0.15), (0.2, 0.55, 0.15)),
                    ((-0.35, 0.55, -0.12), (0.35, 0.8, 0.12)),
                ],
                (0, 0, 0),
                IRON,
            ),
        ],
    )
    # Bed 2.0 x 0.9, lying surface 0.45 m; the long side faces +Z, where the slot "side" of data/mobs.toml is.
    write_glb(
        HERE / "bed.glb",
        [
            ("BED_FRAME", [((-1.0, 0.0, -0.45), (1.0, 0.35, 0.45))], (0, 0, 0), WOOD),
            ("BED_STRAW", [((-0.95, 0.35, -0.4), (0.95, 0.45, 0.4))], (0, 0, 0), CLOTH),
        ],
    )
