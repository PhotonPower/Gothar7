"""The ground under the rooms of enterable houses stays below their floor (W7 C1)."""

import numpy as np

from gothar_worldgen.export.pads import ROOM_FADE_M, ROOM_FLOOR_GAP_M, apply_pads, room_pads
from gothar_worldgen.export.terrain import Grid

# a slope rising 0.5 m per metre towards +x; a room 4.3 x 3.1 m off the grid lines, floor 0.0
RING = [[-2.15, -1.55], [2.15, -1.55], [2.15, 1.55], [-2.15, 1.55]]
ENTRIES = [{"id": "A", "interior": {"floor": 0.0, "ring": RING}}, {"id": "B"}]


def test_no_ground_above_the_floor_inside_the_room():
    hill = Grid(np.fromfunction(lambda r, c: (c - 10) * 0.5, (21, 21)), -10.0, -10.0, 1.0)
    pads = room_pads(ENTRIES, hill.cell)
    assert len(pads) == 1 and pads[0]["clampBelow"]  # only lowers
    out, n = apply_pads(hill, pads)
    assert n > 0
    for x in np.linspace(-2.15, 2.15, 44):
        for z in np.linspace(-1.55, 1.55, 32):
            assert out.height_at(float(x), float(z)) <= -ROOM_FLOOR_GAP_M + 1e-9
    assert out.height_at(-2.0, 0.0) == hill.height_at(-2.0, 0.0)  # already lower: untouched
    away = 2.15 + 1.0 + ROOM_FADE_M + 0.5
    assert out.height_at(away, 0.0) == hill.height_at(away, 0.0)  # beyond the fade
    eased = 2.15 + 1.0 + ROOM_FADE_M / 2  # half way out: on the way back up
    assert -ROOM_FLOOR_GAP_M < out.height_at(eased, 0.0) < hill.height_at(eased, 0.0)
    assert (out.heights <= hill.heights).all()
