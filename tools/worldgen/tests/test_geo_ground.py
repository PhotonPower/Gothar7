import numpy as np

from gothar_worldgen.buildings.batch import SINK_M, generate
from gothar_worldgen.export.terrain import Grid
from gothar_worldgen.geo.ground import add_ground_ranges, ground_range

HEIGHTS = np.zeros((101, 101))
HEIGHTS[:, 53:] = -2.0  # step 3 m east of x = 0
GRID = Grid(HEIGHTS, -50.0, -50.0, 1.0)
FP = [[0, 0], [6, 0], [6, -4], [0, -4]]


def test_add_ground_ranges_keeps_key_order_and_covers_parts():
    part = {"id": "P", "footprint": [[-20, 0], [-14, 0], [-14, -4]], "groundY": 0.0}
    entry = {"id": "B", "footprint": FP, "groundY": 0.5, "heightM": 9.0, "parts": [part]}
    assert add_ground_ranges([entry], GRID) == 2
    assert list(entry)[:5] == ["id", "footprint", "groundY", "groundMinY", "groundMaxY"]
    assert (entry["groundMinY"], entry["groundMaxY"]) == (-2.0, 0.0)
    assert (entry["parts"][0]["groundMinY"], entry["parts"][0]["groundMaxY"]) == (0.0, 0.0)
    # Running twice does not duplicate or move the keys.
    add_ground_ranges([entry], GRID)
    assert list(entry).count("groundMinY") == 1 and list(entry)[3] == "groundMinY"


def test_entries_outside_the_heightmap_stay_untouched():
    entry = {"id": "F", "footprint": [[500, 500], [501, 500], [501, 501]], "groundY": 0.0}
    assert add_ground_ranges([entry], GRID) == 0 and "groundMinY" not in entry
    assert ground_range(GRID, entry["footprint"]) is None


def test_batch_uses_the_stored_range(tmp_path):
    b = {"id": "S", "inCore": True, "footprint": FP, "groundY": 0.0, "groundMinY": -5.0,
         "groundMaxY": 0.0, "roof": {"type": "flat", "eaveY": 4.0, "ridgeY": 4.0}}  # fmt: skip
    res = generate([b], None, tmp_path, "v")  # no grid: the stored values are used
    assert res.index["entries"][0]["pos"][1] == -5.0 - SINK_M
    assert res.steps == [("S", 5.0)]
