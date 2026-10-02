"""Preview images of the intermediate data: hillshaded terrain with buildings, streets,
squares, water, walls and trees on top, plus legend and scale bar. North is up.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt
from PIL import Image, ImageDraw

from gothar_worldgen.geo.streets import parse_width
from gothar_worldgen.qa.workdata import WorkData

RGBA = tuple[int, int, int, int]
DEFAULT_WATERWAY_WIDTH_M = 2.0

BUILDING_CORE: RGBA = (178, 74, 52, 255)
BUILDING_OTHER: RGBA = (120, 112, 104, 255)
BUILDING_EDGE: RGBA = (40, 30, 25, 255)
SQUARE: RGBA = (240, 150, 40, 150)
WATER: RGBA = (70, 130, 200, 220)
WALL: RGBA = (140, 20, 20, 255)
TREE: RGBA = (30, 100, 40, 255)
LANDMARK: RGBA = (250, 220, 40, 255)
FOUNTAIN: RGBA = (60, 170, 240, 255)
CORE_FRAME: RGBA = (255, 255, 255, 255)
NO_TERRAIN: RGBA = (200, 200, 200, 255)
STREET_COLOURS: dict[str, RGBA] = {
    "road": (250, 250, 245, 230),
    "pedestrian": (250, 225, 120, 230),
    "track": (200, 160, 100, 230),
    "path": (150, 220, 140, 230),
    "steps": (220, 60, 200, 230),
}
LEGEND = (
    ("building (core)", BUILDING_CORE),
    ("building", BUILDING_OTHER),
    ("road", STREET_COLOURS["road"]),
    ("pedestrian", STREET_COLOURS["pedestrian"]),
    ("path", STREET_COLOURS["path"]),
    ("track", STREET_COLOURS["track"]),
    ("steps", STREET_COLOURS["steps"]),
    ("square", SQUARE),
    ("water", WATER),
    ("wall", WALL),
    ("tree", TREE),
    ("fountain", FOUNTAIN),
    ("landmark", LANDMARK),
)


@dataclass(frozen=True)
class View:
    """Local rectangle (x east, z south) rendered at ``res`` metres per pixel."""

    min_x: float
    min_z: float
    max_x: float
    max_z: float
    res: float

    @property
    def size(self) -> tuple[int, int]:
        return (
            round((self.max_x - self.min_x) / self.res),
            round((self.max_z - self.min_z) / self.res),
        )

    def px(self, x: float, z: float) -> tuple[float, float]:
        return (x - self.min_x) / self.res, (z - self.min_z) / self.res

    def pts(self, points: Iterable[Sequence[float]]) -> list[tuple[float, float]]:
        return [self.px(p[0], p[1]) for p in points]


def hillshade(heights: npt.NDArray[np.float32], cell: float) -> npt.NDArray[np.uint8]:
    """Grey-green relief: light from the north-west, tinted by height."""
    h = heights.astype(np.float64)
    d_south, d_east = np.gradient(h, cell)  # rows run south, columns east
    nx, ny, nz = -d_east, d_south, np.ones_like(h)  # normal, y = north
    light = np.array([-1.0, 1.0, 1.5])
    light /= np.linalg.norm(light)
    shade = (nx * light[0] + ny * light[1] + nz * light[2]) / np.sqrt(nx**2 + ny**2 + 1.0)
    span = float(h.max() - h.min()) or 1.0
    t = (h - h.min()) / span
    low, high = np.array([126, 150, 110]), np.array([214, 204, 170])
    tint = low + (high - low) * t[..., None]
    rgb = tint * (0.45 + 0.65 * np.clip(shade, 0.0, 1.0))[..., None]
    return np.clip(rgb, 0, 255).astype(np.uint8)


def _terrain_image(d: WorkData, view: View) -> Image.Image:
    t = d.terrain
    cell = t["cellSize"]
    img = Image.fromarray(hillshade(d.heights, cell))
    # Terrain samples are cell centres; the image of the full grid spans half a cell further.
    left = t["firstSample"]["x"] - cell / 2
    top = t["firstSample"]["z"] - cell / 2
    right, bottom = left + t["width"] * cell, top + t["height"] * cell
    canvas = Image.new("RGBA", view.size, NO_TERRAIN)
    # Part of the view covered by the terrain (views may extend beyond it).
    x0, z0 = max(view.min_x, left), max(view.min_z, top)
    x1, z1 = min(view.max_x, right), min(view.max_z, bottom)
    if x1 <= x0 or z1 <= z0:
        return canvas
    (px0, pz0), (px1, pz1) = view.px(x0, z0), view.px(x1, z1)
    size = (max(1, round(px1 - px0)), max(1, round(pz1 - pz0)))
    box = ((x0 - left) / cell, (z0 - top) / cell, (x1 - left) / cell, (z1 - top) / cell)
    part = img.resize(size, Image.Resampling.BILINEAR, box=box).convert("RGBA")
    canvas.paste(part, (round(px0), round(pz0)))
    return canvas


def _line_width(width_m: float, view: View) -> int:
    return max(1, round(width_m / view.res))


def render(d: WorkData, view: View) -> Image.Image:
    img = _terrain_image(d, view)
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)

    for f in d.features:
        if f["type"] == "water" and f["geometry"] == "polygon":
            draw.polygon(view.pts(f["polygon"]), fill=WATER)
        elif f["type"] == "waterway":
            width = parse_width(f.get("tags", {}).get("width")) or DEFAULT_WATERWAY_WIDTH_M
            draw.line(view.pts(f["points"]), fill=WATER, width=_line_width(width, view))
    for q in d.squares:
        draw.polygon(view.pts(q["polygon"]), fill=SQUARE)
    order = ("track", "path", "steps", "road", "pedestrian")
    for s in sorted(d.streets, key=lambda s: order.index(s["class"])):
        draw.line(
            view.pts(s["points"]),
            fill=STREET_COLOURS[s["class"]],
            width=_line_width(s["widthM"], view),
            joint="curve",
        )
    for b in d.buildings:
        fill = BUILDING_CORE if b.get("inCore") else BUILDING_OTHER
        draw.polygon(view.pts(b["footprint"]), fill=fill, outline=BUILDING_EDGE)
    tree_r = max(1.5, 2.0 / view.res)
    for f in d.features:
        if f["type"] == "wall":
            draw.line(view.pts(f["points"]), fill=WALL, width=_line_width(1.5, view) + 1)
        elif f["type"] == "tree":
            x, y = view.px(*f["position"])
            draw.ellipse([x - tree_r, y - tree_r, x + tree_r, y + tree_r], fill=TREE)
        elif f["type"] in ("landmark", "fountain"):
            colour = LANDMARK if f["type"] == "landmark" else FOUNTAIN
            if f["geometry"] == "polygon":
                draw.polygon(view.pts(f["polygon"]), outline=colour, width=3)
            elif f["geometry"] == "point":
                x, y = view.px(*f["position"])
                draw.ellipse([x - 5, y - 5, x + 5, y + 5], fill=colour, outline=BUILDING_EDGE)

    core = d.terrain["areas"]["core"]
    draw.rectangle(
        [*view.px(core["minX"], core["minZ"]), *view.px(core["maxX"], core["maxZ"])],
        outline=CORE_FRAME,
        width=2,
    )
    ox, oy = view.px(0.0, 0.0)
    draw.line([ox - 8, oy, ox + 8, oy], fill=(0, 0, 0, 255), width=3)
    draw.line([ox, oy - 8, ox, oy + 8], fill=(0, 0, 0, 255), width=3)
    _legend(draw, view)
    return Image.alpha_composite(img, overlay).convert("RGB")


def _legend(draw: ImageDraw.ImageDraw, view: View) -> None:
    pad, row = 8, 16
    w, h = 150, pad * 2 + row * (len(LEGEND) + 2)
    draw.rectangle([pad, pad, pad + w, pad + h], fill=(255, 255, 255, 210))
    y = pad * 2
    for label, colour in LEGEND:
        draw.rectangle([pad * 2, y + 2, pad * 2 + 18, y + 12], fill=colour, outline=BUILDING_EDGE)
        draw.text((pad * 2 + 26, y), label, fill=(0, 0, 0, 255))
        y += row
    bar_m = _scale_bar_metres(view)
    bar_px = bar_m / view.res
    y += row // 2
    draw.rectangle([pad * 2, y, pad * 2 + bar_px, y + 5], fill=(0, 0, 0, 255))
    draw.text((pad * 2, y + 7), f"{bar_m:g} m   N up", fill=(0, 0, 0, 255))


def _scale_bar_metres(view: View) -> float:
    target = 110 * view.res  # about 110 px
    for m in (10, 20, 25, 50, 100, 200, 250, 500, 1000):
        if m >= target * 0.6:
            return float(m)
    return 1000.0


def area_view(d: WorkData, area: str, res: float) -> View:
    a = d.terrain["areas"][area]
    return View(a["minX"], a["minZ"], a["maxX"], a["maxZ"], res)


def write_previews(d: WorkData, out_dir: Path) -> list[Path]:
    """``preview.png`` (whole area, 1 m/px) and ``preview_core.png`` (core, 0.5 m/px)."""
    targets = [
        (out_dir / "preview.png", area_view(d, "surroundings", 1.0 * d.terrain["cellSize"])),
        (out_dir / "preview_core.png", area_view(d, "core", 0.5 * d.terrain["cellSize"])),
    ]
    for path, view in targets:
        render(d, view).save(path, optimize=True)
    return [p for p, _ in targets]
