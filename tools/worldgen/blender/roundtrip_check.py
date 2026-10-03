"""Headless check of the add-on.

blender --background --factory-startup --python roundtrip_check.py -- <index> <id>

Imports one building, moves a vertex up by 1 m, writes it back; exits 1 on failure.
"""

import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gothar_buildings  # noqa: E402

args = sys.argv[sys.argv.index("--") + 1 :]
index, building_id = Path(args[0]), args[1]
gothar_buildings.register()
site = gothar_buildings.core.Site(index)
obj = gothar_buildings.import_building(site, building_id)
expected = gothar_buildings.core.gltf_to_blender(*site.entries[building_id]["pos"])
assert all(abs(a - b) < 1e-4 for a, b in zip(obj.location, expected, strict=True)), tuple(
    obj.location
)
top = max(v.co.z for v in obj.data.vertices)
for v in obj.data.vertices:
    if abs(v.co.z - top) < 1e-6:
        v.co.z += 1.0
gothar_buildings.export_building(obj, site)
print("ROUNDTRIP_OK", top)
gothar_buildings.unregister()
bpy.ops.wm.quit_blender()
