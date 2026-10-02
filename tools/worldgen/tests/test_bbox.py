import pytest

from gothar_worldgen.geo.bbox import BBox, Tile, tiles_covering


def test_around_builds_square_box():
    b = BBox.around(500_000, 5_400_000, 250)
    assert (b.min_e, b.min_n, b.max_e, b.max_n) == (499_750, 5_399_750, 500_250, 5_400_250)
    assert b.width == b.height == 500


def test_degenerate_box_is_rejected():
    with pytest.raises(ValueError):
        BBox(10, 10, 10, 20)
    with pytest.raises(ValueError):
        BBox(10, 20, 30, 5)


def test_contains_is_half_open():
    b = BBox(0, 0, 10, 10)
    assert b.contains(0, 0)
    assert b.contains(9.99, 9.99)
    assert not b.contains(10, 5)
    assert not b.contains(5, 10)


def test_leonberg_surroundings_need_nine_km_tiles():
    tiles = tiles_covering(BBox.around(500_933, 5_405_056, 1000), 1000)
    assert [t.label for t in tiles] == [
        "499_5404", "500_5404", "501_5404",
        "499_5405", "500_5405", "501_5405",
        "499_5406", "500_5406", "501_5406",
    ]  # fmt: skip


def test_leonberg_core_with_two_km_tiles():
    tiles = tiles_covering(BBox.around(500_933, 5_405_056, 350), 2000)
    assert [t.label for t in tiles] == ["500_5404"]


def test_edge_on_tile_border_does_not_add_neighbour():
    tiles = tiles_covering(BBox(1000, 2000, 2000, 3000), 1000)
    assert tiles == [Tile(1000, 2000, 1000)]


def test_box_crossing_border_by_a_bit_adds_neighbour():
    tiles = tiles_covering(BBox(1000, 2000, 2000.5, 3000), 1000)
    assert [t.label for t in tiles] == ["1_2", "2_2"]


def test_grid_with_origin_offset():
    tiles = tiles_covering(BBox(999, 0, 1001, 1), 2000, origin_e=1000)
    assert [t.min_e for t in tiles] == [-1000, 1000]


def test_tile_bbox_matches_grid():
    t = Tile(500_000, 5_404_000, 2000)
    assert t.bbox == BBox(500_000, 5_404_000, 502_000, 5_406_000)


def test_invalid_tile_size():
    with pytest.raises(ValueError):
        tiles_covering(BBox(0, 0, 1, 1), 0)
