"""Splat layers for the terrain (contract: docs/modules/world.md, "Splat-Schichten").

A first coverage from the work data, meant as a starting point for hand painting in the editor:
streets and squares (cobblestone in the old town, gravel outside), mud under and around buildings
and along water, forest floor, fields, rock on steep slopes, meadow everywhere else.

Masks are rasterized on the heightmap grid (one weight pixel per sample), blurred by a few metres
and composited by priority, so the weights of every pixel sum to 1. The layer albedos are small
procedural placeholders; the real terrain textures are a design decision for later (W5 style sheet).
"""

from __future__ import annotations

import math
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt
from PIL import Image, ImageDraw, ImageFilter
from shapely.geometry import Polygon

from gothar_worldgen.export.terrain import Grid

ALBEDO_SIZE = 512  # all layers the same size (engine texture array); the cobbles need it


@dataclass(frozen=True)
class Layer:
    name: str  # shown in the editor
    key: str  # file name of the albedo
    tile: float  # metres per texture repeat
    color: tuple[int, int, int]  # base colour of the placeholder albedo (sRGB)


# Channel order = layer order; layer 0 is the fallback where nothing else applies.
LAYERS = (
    Layer("Wiese", "wiese", 4.0, (92, 112, 52)),
    Layer("Kopfstein", "kopfstein", 2.0, (122, 114, 104)),
    Layer("Kies", "kies", 3.0, (150, 136, 112)),
    Layer("Matsch", "matsch", 4.0, (96, 78, 58)),
    Layer("Waldboden", "waldboden", 5.0, (70, 62, 40)),
    Layer("Acker", "acker", 6.0, (120, 92, 62)),
    Layer("Fels", "fels", 6.0, (118, 116, 110)),
)
WIESE, KOPFSTEIN, KIES, MATSCH, WALDBODEN, ACKER, FELS = range(len(LAYERS))
# Painting order: later layers cover earlier ones.
PRIORITY = (ACKER, WALDBODEN, FELS, MATSCH, KIES, KOPFSTEIN)

FOREST_KINDS = {"forest", "scrub", "wood"}
FIELD_KINDS = {"farmland", "allotments", "garden", "orchard", "vineyard"}
WATER_KINDS = {"stream", "ditch", "river", "canal", "drain"}
NO_SURFACE_HIGHWAYS = {"motorway", "motorway_link", "trunk", "trunk_link"}  # modern, removed later
DEFAULT_STREET_WIDTH_M = 3.0
TREE_RADIUS_M = 3.0
BUILDING_MARGIN_M = 1.5
WATER_WIDTH_M = 3.0
RAIL_WIDTH_M = 4.0
ROCK_SLOPE_DEG = (35.0, 45.0)  # rock fades in between these slopes
# Paving fades out between these slopes and earth takes its place: a street is level across now
# (W6), the bank beside it is no street (coordinator 2026-10-08: no stretched cobbles there).
BANK_SLOPE_DEG = (28.0, 32.0)
BLUR_M = 1.5


class _Canvas:
    """Binary mask on the grid; world (x, z) -> pixel coordinates (pixel centre = sample)."""

    def __init__(self, grid: Grid) -> None:
        self.grid = grid
        self.image = Image.new("L", (grid.width, grid.height), 0)
        self.draw = ImageDraw.Draw(self.image)

    def px(self, x: float, z: float) -> tuple[float, float]:
        g = self.grid
        return (x - g.first_x) / g.cell + 0.5, (z - g.first_z) / g.cell + 0.5

    def polygon(self, points: Sequence[Sequence[float]], outline_m: float = 0.0) -> None:
        if len(points) < 3:
            return
        pts = [self.px(x, z) for x, z in points]
        self.draw.polygon(pts, fill=255)
        if outline_m > 0:
            self.line(points, 2 * outline_m, closed=True)

    def line(self, points: Sequence[Sequence[float]], width_m: float, closed: bool = False) -> None:
        if len(points) < 2:
            return
        pts = [self.px(x, z) for x, z in points]
        if closed:
            pts.append(pts[0])
        width = max(1, round(width_m / self.grid.cell))
        self.draw.line(pts, fill=255, width=width, joint="curve")
        r = width / 2  # round caps, so segments join without gaps
        for px, pz in (pts[0], pts[-1]):
            self.draw.ellipse((px - r, pz - r, px + r, pz + r), fill=255)

    def erase(self, points: Sequence[Sequence[float]]) -> None:
        if len(points) >= 3:
            self.draw.polygon([self.px(x, z) for x, z in points], fill=0)

    def disc(self, x: float, z: float, radius_m: float) -> None:
        px, pz = self.px(x, z)
        r = max(0.5, radius_m / self.grid.cell)
        self.draw.ellipse((px - r, pz - r, px + r, pz + r), fill=255)

    def weights(self, blur_m: float = BLUR_M) -> npt.NDArray[np.float32]:
        img = self.image
        radius = blur_m / self.grid.cell
        if radius >= 0.5:
            img = img.filter(ImageFilter.GaussianBlur(radius))
        return np.asarray(img, dtype=np.float32) / 255.0


def _inside(rect: dict[str, float] | None, points: Iterable[Sequence[float]]) -> bool:
    """True if the first point lies in ``rect`` (good enough to sort streets into core/outside)."""
    if rect is None:
        return False
    for x, z in points:
        return rect["minX"] <= x <= rect["maxX"] and rect["minZ"] <= z <= rect["maxZ"]
    return False


def slope_fade(grid: Grid, lo: float, hi: float) -> npt.NDArray[np.float32]:
    """0 below ``lo`` degrees of slope, 1 above ``hi``, linear between."""
    gz, gx = np.gradient(grid.heights, grid.cell)
    slope = np.degrees(np.arctan(np.hypot(gx, gz)))
    return np.clip((slope - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)


def slope_rock(grid: Grid) -> npt.NDArray[np.float32]:
    return slope_fade(grid, *ROCK_SLOPE_DEG)


def _holds_parterre(polygon: Sequence[Sequence[float]], gardens: Sequence[dict[str, Any]]) -> bool:
    if len(polygon) < 3:
        return False
    area = Polygon(polygon).buffer(0)
    return any(area.intersects(Polygon(g_area)) for g in gardens for g_area in g.get("gravel", []))


def layer_masks(
    grid: Grid,
    buildings: Sequence[dict[str, Any]],
    streets: Sequence[dict[str, Any]],
    squares: Sequence[dict[str, Any]],
    features: Sequence[dict[str, Any]],
    core: dict[str, float] | None,
    gardens: Sequence[dict[str, Any]] = (),
) -> dict[int, npt.NDArray[np.float32]]:
    """Coverage 0..1 per layer (except the fallback ``WIESE``).

    ``gardens``: formal gardens (``handmade.json``): gravel over the ``gravel`` areas with lawn
    beds (``lawn``, meadow layer) left out; no field layer there.
    """
    canvases = {i: _Canvas(grid) for i in (KOPFSTEIN, KIES, MATSCH, WALDBODEN, ACKER)}
    for s in streets:
        if s.get("highway") in NO_SURFACE_HIGHWAYS:
            continue
        layer = KOPFSTEIN if _inside(core, s["points"]) else KIES
        canvases[layer].line(s["points"], float(s.get("widthM") or DEFAULT_STREET_WIDTH_M))
    for q in squares:
        layer = KOPFSTEIN if _inside(core, q["polygon"]) else KIES
        canvases[layer].polygon(q["polygon"])
    for b in buildings:
        canvases[MATSCH].polygon(b.get("footprint") or [], outline_m=BUILDING_MARGIN_M)
    for f in features:
        kind, geometry = f.get("kind"), f.get("geometry")
        if kind in FOREST_KINDS and geometry == "polygon":
            canvases[WALDBODEN].polygon(f["polygon"])
        elif kind == "tree" and geometry == "point":
            canvases[WALDBODEN].disc(*f["position"], TREE_RADIUS_M)
        elif kind in FIELD_KINDS and geometry == "polygon":
            if kind == "garden" and _holds_parterre(f["polygon"], gardens):
                continue  # a formal garden: lawn (meadow) instead of a field
            canvases[ACKER].polygon(f["polygon"])
        elif kind in WATER_KINDS and geometry == "line":
            canvases[MATSCH].line(f["points"], WATER_WIDTH_M)
        elif kind == "rail" and geometry == "line":
            canvases[KIES].line(f["points"], RAIL_WIDTH_M)
    for g in gardens:
        for area in g.get("gravel", []):
            canvases[ACKER].erase(area)
            canvases[MATSCH].erase(area)
            canvases[KIES].polygon(area)
        for bed in g.get("lawn", []):
            canvases[KIES].erase(bed)
    masks = {i: c.weights() for i, c in canvases.items()}
    bank = slope_fade(grid, *BANK_SLOPE_DEG)
    for paved in (KOPFSTEIN, KIES):  # steep banks beside the streets: earth, not paving
        masks[MATSCH] = np.maximum(masks[MATSCH], masks[paved] * bank)
        masks[paved] = masks[paved] * (1.0 - bank)
    masks[FELS] = slope_rock(grid)
    return masks


def composite(
    masks: dict[int, npt.NDArray[np.float32]], shape: tuple[int, int]
) -> npt.NDArray[np.float32]:
    """Weights (layers, rows, cols) summing to 1: meadow, then ``PRIORITY`` painted over it."""
    w = np.zeros((len(LAYERS), *shape), dtype=np.float32)
    w[WIESE] = 1.0
    for layer in PRIORITY:
        m = masks.get(layer)
        if m is None:
            continue
        w *= 1.0 - m
        w[layer] += m
    return w


def encode_maps(weights: npt.NDArray[np.float32]) -> list[npt.NDArray[np.uint8]]:
    """RGBA maps: channel k of map m = layer 4m + k (unused channels 0)."""
    n = weights.shape[0]
    maps = []
    for m in range(math.ceil(n / 4)):
        rgba = np.zeros((*weights.shape[1:], 4), dtype=np.uint8)
        for k in range(4):
            if 4 * m + k < n:
                rgba[..., k] = np.rint(weights[4 * m + k] * 255).astype(np.uint8)
        maps.append(rgba)
    return maps


def _periodic_noise(rng: np.random.Generator, size: int, scale: float) -> npt.NDArray[np.float64]:
    """Tileable smooth noise in [-1, 1]: white noise low-pass filtered in the Fourier domain."""
    f = np.fft.fftfreq(size)
    fx, fy = np.meshgrid(f, f)
    spectrum = np.fft.fft2(rng.standard_normal((size, size))) * np.exp(
        -((fx**2 + fy**2) * scale**2)
    )
    n = np.real(np.fft.ifft2(spectrum))
    return n / (np.abs(n).max() or 1.0)


def placeholder_albedo(layer: Layer, size: int = ALBEDO_SIZE) -> npt.NDArray[np.uint8]:
    """Small tileable placeholder texture (deterministic per layer)."""
    rng = np.random.default_rng(sum(map(ord, layer.key)))
    k = size / 128  # the noise keeps its look at any size
    shade = 0.6 * _periodic_noise(rng, size, 12.0 * k) + 0.4 * _periodic_noise(rng, size, 40.0 * k)
    if layer.key == "kopfstein":
        # Round, irregular cobbles (decision of the project owner 2026-10-04), baked shading.
        from gothar_worldgen.textures.procedural import make

        tex = make("cobbles", size)
        rgb = np.array(layer.color, dtype=np.float64) * tex.albedo
        return (np.clip(np.rint(rgb / 2) * 2, 0, 255)).astype(np.uint8)
    if layer.key == "acker":
        y = np.arange(size)[:, None]
        shade = shade * 0.6 + 0.4 * np.sin(2 * np.pi * y / (size / 8))  # furrows
    base = np.array(layer.color, dtype=np.float64)
    rgb = base * (1.0 + 0.18 * shade[..., None])
    # Few levels: placeholders stay small in the repository.
    return (np.clip(np.rint(rgb / 4) * 4, 0, 255)).astype(np.uint8)


def write_png(path: Path, pixels: npt.NDArray[np.uint8], mode: str) -> None:
    """Deterministic PNG without gAMA/iCCP chunks (splat maps are linear data)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    Image.fromarray(pixels, mode).save(tmp, format="PNG", optimize=False, compress_level=9)
    tmp.replace(path)


@dataclass(frozen=True)
class SplatPaths:
    maps_dir: Path  # generated/ (not versioned)
    maps_vfs: str  # VFS folder of the maps
    albedo_dir: Path  # versioned placeholders
    albedo_vfs: str
    name: str  # prefix of the map files


def write_splat(weights: npt.NDArray[np.float32], paths: SplatPaths) -> dict[str, Any]:
    """Writes maps and placeholder albedos; returns the ``splat`` block."""
    maps = []
    for i, rgba in enumerate(encode_maps(weights)):
        file = f"{paths.name}_splat{i}.png"
        write_png(paths.maps_dir / file, rgba, "RGBA")
        maps.append(f"{paths.maps_vfs}/{file}")
    layers = []
    for layer in LAYERS:
        file = f"{layer.key}.png"
        write_png(paths.albedo_dir / file, placeholder_albedo(layer), "RGB")
        layers.append(
            {"name": layer.name, "albedo": f"{paths.albedo_vfs}/{file}", "tile": layer.tile}
        )
    return {"maps": maps, "layers": layers}


def coverage(weights: npt.NDArray[np.float32]) -> dict[str, float]:
    """Share of the area per layer (for the summary)."""
    total = weights.sum(axis=(1, 2))
    return {layer.name: float(total[i] / total.sum()) for i, layer in enumerate(LAYERS)}
