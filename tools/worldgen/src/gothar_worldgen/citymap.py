"""The town map of a site (M14 F9): a drawn plan on parchment for the map item, from the game's
houses (``buildings_index.json``), OSM streets, squares, water and gardens, the town wall and a
few named places; no player position (as Gothic 1). North is up (-Z).

Writes ``ui/maps/<site>.png`` (a palette image) and ``ui/maps/<site>.toml`` (the image's world
rectangle for the engine). Labels and their anchors are content: ``data/<site>/citymap.json``.
"""

from __future__ import annotations

import io
import math
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from shapely.geometry import LineString, Polygon, box

Pt = tuple[float, float]
RGB = tuple[int, int, int]

PAPER: RGB = (229, 211, 168)
PAPER_DARK: RGB = (196, 170, 120)
INK: RGB = (62, 44, 28)
HOUSE: RGB = (184, 140, 92)
HOUSE_EDGE: RGB = (88, 60, 36)
LANE: RGB = (238, 224, 190)
SQUARE: RGB = (236, 220, 180)
WATER: RGB = (132, 156, 150)
WATER_EDGE: RGB = (84, 104, 100)
GREEN: RGB = (168, 172, 112)
GREEN_EDGE: RGB = (118, 122, 72)
WALL: RGB = (74, 56, 40)
CREDIT = "© OpenStreetMap-Mitwirkende"
GREEN_KINDS = {"garden", "park", "orchard", "meadow", "grass", "allotments", "forest", "scrub"}


class CityMapError(Exception):
    """Invalid map annotation; the message is meant for the user."""


@dataclass(frozen=True)
class Label:
    text: str
    at: Pt
    size: float = 1.0  # relative to the label size
    angle: float = 0.0  # degrees, counter-clockwise on the map


@dataclass(frozen=True)
class MapView:
    """The world rectangle (x east, z south) drawn into ``width`` x ``height`` pixels."""

    min_x: float
    min_z: float
    max_x: float
    max_z: float
    width: int

    @property
    def scale(self) -> float:  # pixels per metre
        return self.width / (self.max_x - self.min_x)

    @property
    def height(self) -> int:
        return round((self.max_z - self.min_z) * self.scale)

    def px(self, x: float, z: float) -> Pt:
        return ((x - self.min_x) * self.scale, (z - self.min_z) * self.scale)


def map_view(outline: Polygon, margin_m: float, width: int) -> MapView:
    """The rectangle round ``outline`` (the town wall) with ``margin_m`` on each side."""
    x0, z0, x1, z1 = outline.bounds
    return MapView(round(x0 - margin_m, 1), round(z0 - margin_m, 1), round(x1 + margin_m, 1),
                   round(z1 + margin_m, 1), width)  # fmt: skip


def load_labels(doc: dict[str, Any], anchors: dict[str, Pt]) -> list[Label]:
    """``citymap.json`` labels: ``text`` and ``anchor`` (a key of ``anchors``: vob names, gate
    keys, feature names), optional ``offset`` [dx, dz] in metres, ``size``, ``angle``."""
    out = []
    for k, item in enumerate(doc.get("labels", [])):
        if "text" not in item or "anchor" not in item:
            raise CityMapError(f"labels[{k}]: needs text and anchor")
        if item["anchor"] not in anchors:
            raise CityMapError(f"labels[{k}]: unknown anchor {item['anchor']!r}")
        x, z = anchors[item["anchor"]]
        dx, dz = item.get("offset", [0.0, 0.0])
        out.append(Label(str(item["text"]), (x + float(dx), z + float(dz)),
                         float(item.get("size", 1.0)), float(item.get("angle", 0.0))))  # fmt: skip
    return out


def _paper(width: int, height: int, seed: int) -> Image.Image:
    """Parchment: a smooth blotchy tone, darker towards the edges."""
    rng = np.random.default_rng(seed)
    coarse = Image.fromarray((rng.random((12, 16)) * 255).astype(np.uint8), "L")
    blotch = np.asarray(coarse.resize((width, height), Image.Resampling.BICUBIC), np.float32) / 255
    fine = Image.fromarray((rng.random((height // 4, width // 4)) * 255).astype(np.uint8), "L")
    grain = np.asarray(fine.resize((width, height), Image.Resampling.BILINEAR), np.float32) / 255
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    edge = np.maximum(np.abs(xx / width - 0.5), np.abs(yy / height - 0.5)) * 2
    dark = np.clip(0.25 * blotch + 0.08 * grain + 0.55 * np.clip(edge - 0.75, 0, 1) / 0.25, 0, 1)
    a, b = np.asarray(PAPER, np.float32), np.asarray(PAPER_DARK, np.float32)
    img = a[None, None, :] * (1 - dark[..., None]) + b[None, None, :] * dark[..., None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def _poly(draw: ImageDraw.ImageDraw, view: MapView, ring: Sequence[Pt], fill: RGB,
          outline: RGB | None, width: int = 1) -> None:  # fmt: skip
    pts = [view.px(x, z) for x, z in ring]
    if len(pts) >= 3:
        draw.polygon(pts, fill=fill, outline=outline, width=width)


def _line(draw: ImageDraw.ImageDraw, view: MapView, pts: Sequence[Pt], colour: RGB,
          width_m: float) -> None:  # fmt: skip
    w = max(1, round(width_m * view.scale))
    xy = [view.px(x, z) for x, z in pts]
    if len(xy) >= 2:
        draw.line(xy, fill=colour, width=w, joint="curve")
        r = w / 2
        for px, pz in (xy[0], xy[-1]):
            draw.ellipse([px - r, pz - r, px + r, pz + r], fill=colour)


def _text(img: Image.Image, label: Label, view: MapView, font: ImageFont.FreeTypeFont) -> None:
    """A label in ink with a soft paper halo, rotated by its angle round its centre."""
    x0, y0, x1, y1 = font.getbbox(label.text)
    w, h = x1 - x0 + 16, y1 - y0 + 16
    layer = Image.new("L", (w, h), 0)
    ImageDraw.Draw(layer).text((8 - x0, 8 - y0), label.text, font=font, fill=255)
    halo = layer.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(2))
    if label.angle:
        layer = layer.rotate(label.angle, resample=Image.Resampling.BICUBIC, expand=True)
        halo = halo.rotate(label.angle, resample=Image.Resampling.BICUBIC, expand=True)
    cx, cy = view.px(*label.at)
    at = (round(cx - layer.width / 2), round(cy - layer.height / 2))
    img.paste(Image.new("RGB", halo.size, PAPER), at, halo.point(lambda v: v * 3 // 4))
    img.paste(Image.new("RGB", layer.size, INK), at, layer)


def _compass(draw: ImageDraw.ImageDraw, c: Pt, r: float, font: ImageFont.FreeTypeFont) -> None:
    """A wind rose: the north point dark, the letter N above it."""
    x, y = c
    for k in range(8):
        a = math.radians(k * 45)
        length = r if k % 2 == 0 else r * 0.55
        tip = (x + math.sin(a) * length, y - math.cos(a) * length)
        side = r * 0.14
        left = (x + math.sin(a - math.pi / 2) * side, y - math.cos(a - math.pi / 2) * side)
        right = (x + math.sin(a + math.pi / 2) * side, y - math.cos(a + math.pi / 2) * side)
        draw.polygon([tip, left, (x, y)], fill=INK)
        draw.polygon([tip, right, (x, y)], fill=PAPER if k else INK, outline=INK)
    draw.ellipse([x - r * 0.08, y - r * 0.08, x + r * 0.08, y + r * 0.08], fill=PAPER, outline=INK)
    tw = font.getlength("N")
    draw.text((x - tw / 2, y - r - font.size * 1.25), "N", font=font, fill=INK)


def _scale_bar(draw: ImageDraw.ImageDraw, view: MapView, at: Pt,
               font: ImageFont.FreeTypeFont) -> None:  # fmt: skip
    """100 m in four alternating fields."""
    x, y = at
    seg = 25 * view.scale
    for k in range(4):
        draw.rectangle([x + k * seg, y, x + (k + 1) * seg, y + 10], outline=INK,
                       fill=INK if k % 2 == 0 else PAPER)  # fmt: skip
    for k, t in ((0, "0"), (4, "100 m")):
        tw = font.getlength(t)
        draw.text((x + k * seg - tw / 2, y + 16), t, font=font, fill=INK)


def render(view: MapView, houses: Sequence[Polygon], streets: Sequence[dict[str, Any]],
           squares: Sequence[Polygon], waters: Sequence[Polygon],
           waterways: Sequence[tuple[list[Pt], float]], greens: Sequence[Polygon],
           wall: Polygon, gates: Sequence[Pt], labels: Sequence[Label], title: str,
           fonts: dict[str, Path], seed: int = 7) -> Image.Image:  # fmt: skip
    """The map: paper, gardens, water, lanes, squares, houses, the wall, labels, wind rose."""
    img = _paper(view.width, view.height, seed)
    draw = ImageDraw.Draw(img)
    frame = box(view.min_x, view.min_z, view.max_x, view.max_z)
    for g in greens:
        _poly(draw, view, list(g.exterior.coords), GREEN, GREEN_EDGE, 2)
    for g in waters:
        _poly(draw, view, list(g.exterior.coords), WATER, WATER_EDGE, 3)
    for pts, width in waterways:
        _line(draw, view, pts, WATER_EDGE, width + 1.6)
        _line(draw, view, pts, WATER, width)
    for s in streets:
        if LineString(s["points"]).intersects(frame):
            _line(draw, view, s["points"], LANE, float(s.get("widthM", 3.0)))
    for q in squares:
        _poly(draw, view, list(q.exterior.coords), SQUARE, None)
    edge = max(1, round(0.25 * view.scale))
    for h in houses:
        _poly(draw, view, list(h.exterior.coords), HOUSE, HOUSE_EDGE, edge)
    ring = list(wall.exterior.coords)
    _line(draw, view, ring, WALL, 2.4)
    tower = 3.2 * view.scale
    for k, (x, z) in enumerate(ring[:-1]):
        if k % 6 == 0:  # towers along the wall, drawn small
            px, pz = view.px(x, z)
            t = tower / 2
            draw.ellipse([px - t, pz - t, px + t, pz + t], fill=WALL)
    for x, z in gates:
        px, pz = view.px(x, z)
        draw.rectangle([px - tower, pz - tower, px + tower, pz + tower], fill=WALL, outline=INK)
    size = max(18, round(view.width / 60))
    sc = fonts["caps"]
    for lb in labels:
        _text(img, lb, view, ImageFont.truetype(str(sc), round(size * lb.size)))
    draw = ImageDraw.Draw(img)
    big = ImageFont.truetype(str(sc), size * 3)
    top = view.min_z + size * 2.6 / view.scale  # the title on a paper halo, at the top
    _text(img, Label(title, ((view.min_x + view.max_x) / 2, top)), view, big)
    draw = ImageDraw.Draw(img)
    _compass(draw, (view.width - size * 4, size * 5), size * 2.6,
             ImageFont.truetype(str(sc), round(size * 1.2)))  # fmt: skip
    small = ImageFont.truetype(str(fonts["text"]), round(size * 0.7))
    _scale_bar(draw, view, (size * 2, view.height - size * 3), small)
    cw = small.getlength(CREDIT)
    draw.text((view.width - cw - size, view.height - size * 1.6), CREDIT, font=small, fill=INK)
    m = round(size * 0.4)  # a double frame line
    draw.rectangle([m, m, view.width - 1 - m, view.height - 1 - m], outline=INK, width=3)
    draw.rectangle([m * 2, m * 2, view.width - 1 - m * 2, view.height - 1 - m * 2], outline=INK)
    return img


def png_bytes(img: Image.Image, colours: int = 64) -> bytes:
    """A palette PNG (small; the map has few tones), the same bytes on every run."""
    pal = img.quantize(colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    buf = io.BytesIO()
    pal.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def map_toml(view: MapView, image: str, credit: str = CREDIT) -> str:
    """The image's world rectangle for the engine (north up: -Z at the top)."""
    return (f'# Stadtplan (welt, gothar-worldgen citymap): Bild und sein Weltausschnitt\n'
            f'image = "{image}"\nwidth = {view.width}\nheight = {view.height}\n'
            f'# Weltkoordinaten der Bildränder (x nach Osten, z nach Süden; Norden oben)\n'
            f'min_x = {view.min_x}\nmin_z = {view.min_z}\nmax_x = {view.max_x}\n'
            f'max_z = {view.max_z}\npixels_per_metre = {round(view.scale, 4)}\n'
            f'credit = "{credit}"\n')  # fmt: skip


def in_view(view: MapView, polys: Sequence[Polygon]) -> list[Polygon]:
    frame = box(view.min_x, view.min_z, view.max_x, view.max_z)
    return [p for p in polys if p.is_valid and p.intersects(frame)]


def point_on(pts: Sequence[Pt], view: MapView, margin_m: float = 30.0) -> Pt:
    """A point of a line well inside the view (for a label), else its middle."""
    inner = box(view.min_x + margin_m, view.min_z + margin_m, view.max_x - margin_m,
                view.max_z - margin_m)  # fmt: skip
    piece = LineString(pts).intersection(inner)
    line = piece if isinstance(piece, LineString) and not piece.is_empty else LineString(pts)
    p = line.interpolate(0.5, normalized=True)
    return (round(p.x, 2), round(p.y, 2))
