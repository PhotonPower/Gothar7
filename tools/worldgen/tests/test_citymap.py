"""The town map for the map item (M14 F9)."""

import io
import tomllib
from pathlib import Path

import pytest
from PIL import Image
from shapely.geometry import Polygon, box

from gothar_worldgen.citymap import (
    CREDIT,
    CityMapError,
    Label,
    load_labels,
    map_toml,
    map_view,
    png_bytes,
    point_on,
    render,
)

FONTS = Path(__file__).resolve().parents[3] / "assets" / "source" / "fonts"


def test_the_view_frames_the_wall_north_up():
    view = map_view(box(-100.0, -50.0, 100.0, 50.0), 20.0, 480)
    assert (view.min_x, view.min_z, view.max_x, view.max_z) == (-120.0, -70.0, 120.0, 70.0)
    assert view.scale == pytest.approx(2.0) and view.height == 280
    assert view.px(-120.0, -70.0) == (0.0, 0.0)  # north-west corner at the top left
    assert view.px(120.0, 70.0) == pytest.approx((480.0, 280.0))


def test_labels_need_known_anchors():
    anchors = {"HANDMADE_KIRCHE": (10.0, 20.0)}
    (lb,) = load_labels({"labels": [{"text": "Kirche", "anchor": "HANDMADE_KIRCHE",
                                     "offset": [0, -5], "size": 0.8}]}, anchors)  # fmt: skip
    assert lb == Label("Kirche", (10.0, 15.0), 0.8, 0.0)
    with pytest.raises(CityMapError):
        load_labels({"labels": [{"text": "Rathaus", "anchor": "HANDMADE_RATHAUS"}]}, anchors)
    with pytest.raises(CityMapError):
        load_labels({"labels": [{"anchor": "HANDMADE_KIRCHE"}]}, anchors)


def test_a_line_label_sits_inside_the_view():
    view = map_view(box(0.0, 0.0, 100.0, 100.0), 0.0, 200)
    x, z = point_on([(-500.0, 50.0), (60.0, 50.0)], view)
    assert 30.0 <= x <= 70.0 and z == 50.0


def test_the_toml_gives_the_world_rectangle_and_the_credit():
    view = map_view(box(-100.0, -50.0, 100.0, 50.0), 20.0, 480)
    doc = tomllib.loads(map_toml(view, "ui/maps/leonberg.png"))
    assert doc["image"] == "ui/maps/leonberg.png" and (doc["width"], doc["height"]) == (480, 280)
    assert (doc["min_x"], doc["min_z"], doc["max_x"], doc["max_z"]) == (-120.0, -70.0, 120.0, 70.0)
    assert doc["pixels_per_metre"] == 2.0 and doc["credit"] == CREDIT


def test_the_map_is_a_small_palette_png_the_same_on_every_run():
    wall = box(-60.0, -40.0, 60.0, 40.0)
    view = map_view(wall, 15.0, 600)
    houses = [box(-20.0, -10.0, -10.0, 0.0), box(5.0, 5.0, 15.0, 12.0)]
    streets = [{"points": [[-80.0, 0.0], [80.0, 0.0]], "widthM": 6.0}]
    fonts = {"caps": FONTS / "AlegreyaSC-Regular.ttf", "text": FONTS / "Alegreya-Regular.ttf"}
    labels = [Label("Markt", (0.0, -20.0))]

    def once() -> bytes:
        square = [box(-5.0, -5.0, 5.0, 5.0)]
        ring = Polygon(wall.exterior.coords)
        img = render(view, houses, streets, square, [], [], [], ring, [(60.0, 0.0)], labels,
                     "Leonberg", fonts)  # fmt: skip
        return png_bytes(img, 32)

    data = once()
    assert data == once()
    img = Image.open(io.BytesIO(data))
    assert img.mode == "P" and img.size == (view.width, view.height)
    house = img.convert("RGB").getpixel(tuple(round(c) for c in view.px(-15.0, -5.0)))
    assert house[0] > house[2] + 40  # a house is drawn in ochre
