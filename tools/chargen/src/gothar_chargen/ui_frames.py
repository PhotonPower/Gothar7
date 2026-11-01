"""Own UI frames for the game UI (M14, docs/modules/ui.md "Plan M14", owner decision F2: dark like
Gothic - wood, leather, parchment). Everything is procedural (numpy and the standard library only),
deterministic for a seed, and written as RGBA PNG to ``assets/source/ui/frames/``.

Resolution: the images are drawn at **2 image pixels per UI unit** (UI unit = pixel at 1080p), so
they are 1:1 at 4K and downsampled at 1080p; the frames in ``style.toml`` therefore use
``scale = 0.5`` and slices in image pixels (twice the border width in UI units).

9-slice rules the generator keeps (the engine stretches edges along one axis and the centre in
both, ``Canvas::frame``):

- corners hold everything that must not stretch (iron fittings, rivets, ornaments);
- the edges depend (nearly) only on the distance to the image border, so stretching keeps them;
- the centre is a periodic tile (seamless when tiled) with little contrast (smooth when stretched);
- the backgrounds (``bg_*``) are fully periodic tiles for screens that tile a texture.
"""

from __future__ import annotations

import struct
import zlib
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import numpy as np

SCALE = 0.5  # UI units per image pixel (style.toml "scale")
SS = 2  # supersampling while drawing (anti-aliased shapes)
DEFAULT_SEED = 7
FRAMES_DIR = Path("assets/source/ui/frames")

Array = np.ndarray


@dataclass(frozen=True)
class FrameSpec:
    """One image: size and 9-slice borders in image pixels (left, top, right, bottom)."""

    name: str
    width: int
    height: int
    slice: tuple[int, int, int, int]
    purpose: str
    background: bool = False  # a periodic tile without borders
    center: str = "tile"  # centre: "tile" periodic, "tile_x" periodic along x (bar fills),
    # "clear" transparent (bar frame), "fixed" not meant to stretch (slider knob)

    @property
    def file(self) -> str:
        return f"{self.name}.png"

    @property
    def center_box(self) -> tuple[int, int, int, int]:
        """Centre region (x0, y0, x1, y1) in image pixels."""
        left, top, right, bottom = self.slice
        return left, top, self.width - right, self.height - bottom


SPECS: tuple[FrameSpec, ...] = (
    FrameSpec("panel", 192, 192, (48, 48, 48, 48), "window/panel: dark wood, iron corner fittings"),
    FrameSpec("parchment", 192, 192, (40, 40, 40, 40), "diary, documents, dialog answers"),
    FrameSpec("leather", 192, 192, (32, 32, 32, 32), "inventory background, embossed seam"),
    FrameSpec("tooltip", 160, 160, (32, 32, 32, 32), "info box / tooltip: dark, brass line"),
    FrameSpec("cell", 128, 128, (24, 24, 24, 24), "inventory cell"),
    FrameSpec("cell_selected", 128, 128, (24, 24, 24, 24), "inventory cell, selected"),
    FrameSpec("cell_equipped", 128, 128, (24, 24, 24, 24), "inventory cell, equipped item"),
    FrameSpec("tab", 160, 64, (28, 24, 28, 8), "tab, inactive"),
    FrameSpec("tab_active", 160, 64, (28, 24, 28, 8), "tab, active (open towards the panel)"),
    FrameSpec("button", 192, 64, (32, 24, 32, 24), "button"),
    FrameSpec("button_focus", 192, 64, (32, 24, 32, 24), "button, focused"),
    FrameSpec("button_pressed", 192, 64, (32, 24, 32, 24), "button, pressed"),
    FrameSpec(
        "bar", 128, 48, (24, 16, 24, 16), "HUD bar frame (centre transparent)", center="clear"
    ),
    FrameSpec("bar_hp", 64, 28, (6, 6, 6, 6), center="tile_x", purpose="bar fill: health (red)"),
    FrameSpec("bar_mana", 64, 28, (6, 6, 6, 6), center="tile_x", purpose="bar fill: mana (blue)"),
    FrameSpec("bar_enemy", 64, 28, (6, 6, 6, 6), center="tile_x", purpose="bar fill: enemy health"),
    FrameSpec(
        "bar_air", 64, 28, (6, 6, 6, 6), center="tile_x", purpose="bar fill: air while diving"
    ),
    FrameSpec("subtitle", 160, 80, (56, 32, 56, 32), "subtitle backdrop, soft and subtle"),
    FrameSpec("highlight", 64, 64, (16, 16, 16, 16), "selected list entry (dialog, menus)"),
    FrameSpec("slider_track", 64, 32, (16, 12, 16, 12), "options: slider groove"),
    FrameSpec(
        "slider_knob",
        48,
        48,
        (20, 20, 20, 20),
        "options: slider knob, drawn at its size",
        center="fixed",
    ),
    FrameSpec("bg_wood", 256, 256, (0, 0, 0, 0), "tile: dark wooden boards", background=True),
    FrameSpec("bg_leather", 256, 256, (0, 0, 0, 0), "tile: dark leather", background=True),
    FrameSpec("bg_parchment", 256, 256, (0, 0, 0, 0), "tile: parchment", background=True),
)
BAR_OPENING = (14, 10, 114, 38)  # bar: opening for the fill (x0, y0, x1, y1), image pixels


def spec(name: str) -> FrameSpec:
    for s in SPECS:
        if s.name == name:
            return s
    raise KeyError(name)


# --------------------------------------------------------------------------- helpers


def _hex(color: str) -> Array:
    c = color.lstrip("#")
    return np.array([int(c[i : i + 2], 16) / 255.0 for i in (0, 2, 4)])


def _smooth(a: float, b: float, x: Array) -> Array:
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _ramp(t: Array, colors: list[str]) -> Array:
    stops = np.linspace(0.0, 1.0, len(colors))
    cols = np.array([_hex(c) for c in colors])
    t = np.clip(t, 0.0, 1.0)
    return np.stack([np.interp(t, stops, cols[:, k]) for k in range(3)], axis=-1)


def _mix(a: Array, b: Array, t: Array) -> Array:
    return a * (1.0 - t[..., None]) + b * t[..., None]


def periodic_noise(
    rng: np.random.Generator,
    height: int,
    width: int,
    cells_y: int,
    cells_x: int,
    octaves: int = 1,
    gain: float = 0.5,
) -> Array:
    """Value noise that wraps around (period = the array), zero mean, unit deviation."""
    total = np.zeros((height, width))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        cy, cx = cells_y * 2**o, cells_x * 2**o
        lattice = rng.random((cy, cx))
        yy = (np.arange(height) + 0.5) / height * cy
        xx = (np.arange(width) + 0.5) / width * cx
        y0 = np.floor(yy).astype(int)
        x0 = np.floor(xx).astype(int)
        ty = yy - y0
        tx = xx - x0
        ty = ty * ty * (3 - 2 * ty)
        tx = tx * tx * (3 - 2 * tx)
        y0 %= cy
        x0 %= cx
        y1 = (y0 + 1) % cy
        x1 = (x0 + 1) % cx
        top = lattice[y0][:, x0] * (1 - tx) + lattice[y0][:, x1] * tx
        bot = lattice[y1][:, x0] * (1 - tx) + lattice[y1][:, x1] * tx
        total += amp * (top * (1 - ty)[:, None] + bot * ty[:, None])
        norm += amp
        amp *= gain
    total /= norm
    return (total - total.mean()) / (total.std() + 1e-9)


def _blur(a: Array, radius: int) -> Array:
    """Separable box blur (edges clamped), applied twice."""
    if radius <= 0:
        return a
    out = a
    for _ in range(2):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            c = np.cumsum(np.pad(out, pad, mode="edge"), axis=axis)
            n = out.shape[axis]
            hi = np.take(c, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
            lo = np.take(c, np.arange(0, n), axis=axis)
            out = (hi - lo) / (2 * radius + 1)
    return out


_LIGHT = np.array([-0.45, -0.55, 0.70])
_LIGHT = _LIGHT / np.linalg.norm(_LIGHT)
_HALF = _LIGHT + np.array([0.0, 0.0, 1.0])
_HALF = _HALF / np.linalg.norm(_HALF)


def _lit(
    albedo: Array, height: Array, bump: float = 1.0, spec_amount: float = 0.0, wrap: bool = False
) -> Array:
    """Lambert light from the top left over a height field (heights in image pixels)."""
    if wrap:
        gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
        gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
    else:
        gy, gx = np.gradient(height)
    gx = gx * SS * bump
    gy = gy * SS * bump
    inv = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    nx, ny, nz = -gx * inv, -gy * inv, inv
    ndl = nx * _LIGHT[0] + ny * _LIGHT[1] + nz * _LIGHT[2]
    diffuse = np.clip(ndl / _LIGHT[2], 0.0, 1.8)
    out = albedo * (0.3 + 0.7 * diffuse)[..., None]
    if spec_amount > 0.0:
        ndh = np.clip(nx * _HALF[0] + ny * _HALF[1] + nz * _HALF[2], 0.0, 1.0)
        out = out + spec_amount * (ndh**40)[..., None]
    return out


class _Canvas:
    """Supersampled drawing surface: coordinates in final image pixels (pixel centres)."""

    def __init__(self, s: FrameSpec, rng: np.random.Generator) -> None:
        self.spec = s
        self.rng = rng
        self.w, self.h = s.width, s.height
        self.H, self.W = s.height * SS, s.width * SS
        yy, xx = np.mgrid[0 : self.H, 0 : self.W]
        self.x = (xx + 0.5) / SS
        self.y = (yy + 0.5) / SS
        self.dx = np.minimum(self.x, self.w - self.x)  # distance to the left/right border
        self.dy = np.minimum(self.y, self.h - self.y)
        self.e = np.minimum(self.dx, self.dy)  # distance to the image border
        self.horizontal = self.dy <= self.dx  # top/bottom band (else left/right)

    def rounded(self, inset: float, radius: float, top: float | None = None) -> Array:
        """Signed distance (negative inside) to a rounded rectangle inset from the border. With
        ``top`` the shape starts at y = top and stays open at the bottom (tabs)."""
        dx = self.dx - inset
        dy = self.dy - inset
        if top is not None:  # open at the bottom: only the top corners round
            dy = np.where(self.y > self.h / 2, 1e3, self.y - top)
        qx = radius - dx
        qy = radius - dy
        outside = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2)
        inside = np.minimum(np.maximum(qx, qy), 0.0)
        return outside + inside - radius

    def tile(self, render: Callable[[np.random.Generator, int, int], tuple[Array, Array]]) -> Array:
        """A periodic centre tile, repeated over the whole canvas (aligned to the centre region).
        ``render(rng, h, w)`` returns (rgb, alpha) of one period at supersampled size."""
        x0, y0, x1, y1 = self.spec.center_box
        th, tw = (y1 - y0) * SS, (x1 - x0) * SS
        rgb, alpha = render(self.rng, th, tw)
        iy = (np.arange(self.H) - y0 * SS) % th
        ix = (np.arange(self.W) - x0 * SS) % tw
        self.alpha_tile = alpha[iy][:, ix]
        return rgb[iy][:, ix]

    def finish(self, rgb: Array, alpha: Array) -> Array:
        """Downsample to the final size (alpha-weighted colour) -> uint8 RGBA."""
        h, w = self.h, self.w
        a = np.clip(alpha, 0.0, 1.0).reshape(h, SS, w, SS)
        c = np.clip(rgb, 0.0, 1.0).reshape(h, SS, w, SS, 3)
        asum = a.sum(axis=(1, 3))
        csum = (c * a[..., None]).sum(axis=(1, 3))
        plain = c.mean(axis=(1, 3))
        col = np.where(asum[..., None] > 1e-6, csum / np.maximum(asum, 1e-6)[..., None], plain)
        out = np.zeros((h, w, 4), dtype=np.uint8)
        out[..., :3] = np.round(col * 255)
        out[..., 3] = np.round(asum / (SS * SS) * 255)
        return out


def _over(dst: Array, src: Array, a: Array) -> Array:
    return dst * (1.0 - a[..., None]) + src * a[..., None]


# --------------------------------------------------------------------------- materials


def _wood_albedo(t: Array, dark: bool = False) -> Array:
    if dark:
        return _ramp(t, ["#140d08", "#22160d", "#2e1e12", "#3a2616"])
    return _ramp(t, ["#1f150c", "#322114", "#452e1b", "#583c24"])


def _wood_field(rng: np.random.Generator, h: int, w: int, horizontal: bool) -> Array:
    """Grain value 0..1 with streaks along the length (rows for horizontal grain)."""
    if horizontal:
        streak = periodic_noise(rng, h, w, max(4, h // 6), 2, octaves=3)
        warp = periodic_noise(rng, h, w, 3, 2, octaves=2)
        v = np.arange(h)[:, None] / SS + 0 * np.arange(w)[None, :]
    else:
        streak = periodic_noise(rng, h, w, 2, max(4, w // 6), octaves=3)
        warp = periodic_noise(rng, h, w, 2, 3, octaves=2)
        v = np.arange(w)[None, :] / SS + 0 * np.arange(h)[:, None]
    rings = 0.5 + 0.5 * np.sin(2 * np.pi * (v / 7.0 + 0.6 * warp))
    return np.clip(0.5 + 0.16 * streak + 0.22 * (rings**3 - 0.3), 0.0, 1.0)


def _iron_albedo(rng: np.random.Generator, h: int, w: int, brass: bool = False) -> Array:
    n = periodic_noise(rng, h, w, 6, 6, octaves=3)
    rust = periodic_noise(rng, h, w, 5, 5, octaves=2)
    if brass:
        base = _ramp(0.5 + 0.15 * n, ["#5a4220", "#8a6a34", "#b08a44"])
        return _mix(base, _hex("#3e3018") * np.ones_like(base), _smooth(0.9, 1.6, rust) * 0.6)
    base = _ramp(0.5 + 0.15 * n, ["#24231f", "#34322e", "#46433d"])
    return _mix(base, _hex("#5a3420") * np.ones_like(base), _smooth(0.7, 1.5, rust) * 0.7)


def _dome(sdf: Array, radius: float) -> Array:
    """Height of a dome over the inside of a distance field (0 outside)."""
    t = np.clip(-sdf / radius, 0.0, 1.0)
    return np.sqrt(1.0 - (1.0 - t) ** 2) * radius


def _circle(c: _Canvas, cx: float, cy: float, r: float, mirrored: bool = True) -> Array:
    """Signed distance to circles at (cx, cy) measured from the nearest corner (all four)."""
    x = c.dx if mirrored else c.x
    y = c.dy if mirrored else c.y
    return np.sqrt((x - cx) ** 2 + (y - cy) ** 2) - r


def _capsule(
    px: Array, py: Array, a: tuple[float, float], b: tuple[float, float], r: float
) -> Array:
    ax, ay = a
    bx, by = b
    vx, vy = bx - ax, by - ay
    t = np.clip(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0.0, 1.0)
    return np.sqrt((px - ax - t * vx) ** 2 + (py - ay - t * vy) ** 2) - r


def _rivets(c: _Canvas, points: list[tuple[float, float]], r: float) -> Array:
    sdf = np.full((c.H, c.W), 1e3)
    for px, py in points:
        sdf = np.minimum(sdf, _circle(c, px, py, r))
    return sdf


# --------------------------------------------------------------------------- frames


def _dark_wood_tile(
    contrast: float,
) -> Callable[[np.random.Generator, int, int], tuple[Array, Array]]:
    def render(rng: np.random.Generator, h: int, w: int) -> tuple[Array, Array]:
        g = _wood_field(rng, h, w, horizontal=True)
        low = periodic_noise(rng, h, w, 2, 2, octaves=2)
        t = 0.5 + contrast * (g - 0.5) + 0.05 * low
        height = (g - 0.5) * 0.6
        return _lit(_wood_albedo(t, dark=True), height, wrap=True), np.ones((h, w))

    return render


def _panel(c: _Canvas) -> tuple[Array, Array]:
    rng = c.rng
    rgb = c.tile(_dark_wood_tile(0.45))
    e = c.e
    gh = _wood_field(rng, c.H, c.W, horizontal=True)
    gv = _wood_field(rng, c.H, c.W, horizontal=False)
    grain = np.where(c.horizontal, gh, gv)
    plank = _wood_albedo(0.15 + 0.85 * grain)
    height = np.zeros((c.H, c.W))
    in_plank = (e >= 3) & (e < 40)
    height += np.where(in_plank, 1.6 * _smooth(3, 7, e) * (1 - _smooth(36, 40, e)), 0.0)
    height += (grain - 0.5) * 0.5 * in_plank
    height -= 1.2 * ((e >= 40) & (e < 45))
    albedo = np.where(in_plank[..., None], plank, rgb)
    albedo = np.where(((e >= 40) & (e < 45))[..., None], _hex("#100a06"), albedo)
    albedo = np.where((e < 3)[..., None], _hex("#0e0906"), albedo)
    lit = _lit(albedo, height)
    lit = np.where((e >= 45)[..., None], rgb, lit)
    # iron corner fittings (L-shaped bracket with three rivets), identical in all four corners
    br = np.minimum(
        _capsule(c.dx, c.dy, (12, 12), (38, 12), 7.0), _capsule(c.dx, c.dy, (12, 12), (12, 38), 7.0)
    )
    br = np.minimum(br, np.maximum(c.dx - 19, c.dy - 19))
    br = np.maximum(br, 5 - np.minimum(c.dx, c.dy))  # keep clear of the outer outline
    riv = _rivets(c, [(12, 12), (35, 12), (12, 35)], 3.2)
    shadow = _blur(np.clip(0.5 - _shift(br, 1.5), 0, 1), 2)
    lit = lit * (1.0 - 0.55 * shadow[..., None])
    iron_h = _dome(br, 2.0) + _dome(riv, 3.2) * 0.9
    iron = _lit(_iron_albedo(rng, c.H, c.W), iron_h, spec_amount=0.25)
    cover = np.clip(0.5 - br * SS, 0, 1)
    lit = _over(lit, iron, cover)
    return lit, np.ones((c.H, c.W))


def _shift(a: Array, d: float) -> Array:
    """Distance field moved away from the light (towards the nearest corner's inside) by d."""
    s = round(d * SS)
    hh, ww = a.shape
    # shadows fall towards the image centre in every corner (mirrored fittings)
    q = np.full_like(a, 1e3)
    q[s:, s:] = a[:-s, :-s]  # top-left corner
    tl = q
    q = np.full_like(a, 1e3)
    q[s:, :-s] = a[:-s, s:]  # top-right
    tr = q
    q = np.full_like(a, 1e3)
    q[:-s, s:] = a[s:, :-s]  # bottom-left
    bl = q
    q = np.full_like(a, 1e3)
    q[:-s, :-s] = a[s:, s:]  # bottom-right
    brr = q
    yy, xx = np.mgrid[0:hh, 0:ww]
    left = xx < ww / 2
    top = yy < hh / 2
    return np.where(top, np.where(left, tl, tr), np.where(left, bl, brr))


def _parchment_tile(
    rng: np.random.Generator, h: int, w: int, contrast: float = 1.0
) -> tuple[Array, Array]:
    low = periodic_noise(rng, h, w, 3, 3, octaves=3)
    fib = periodic_noise(rng, h, w, max(8, h // 8), max(2, w // 48), octaves=2)
    stain = periodic_noise(rng, h, w, 4, 4, octaves=2)
    t = 0.55 + contrast * (0.09 * low + 0.012 * fib) - contrast * 0.1 * _smooth(1.0, 2.2, stain)
    albedo = _ramp(t, ["#8a6c42", "#b89a6a", "#d4bf94", "#e2d2ac"])
    return _lit(albedo, low * 0.4 + fib * 0.03, wrap=True), np.ones((h, w))


def _parchment(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(lambda r, h, w: _parchment_tile(r, h, w, 0.4))
    wobble = periodic_noise(c.rng, c.H, c.W, 10, 10, octaves=2)
    sdf = c.rounded(4.0, 14.0) + 1.6 * wobble
    alpha = np.clip(0.5 - sdf * SS / 1.5, 0.0, 1.0)
    depth = -sdf
    burn = 1.0 - _smooth(0.0, 22.0, depth)
    rgb = _mix(rgb, np.ones_like(rgb) * _hex("#6e5030"), 0.75 * burn**1.6)
    rgb = _mix(rgb, np.ones_like(rgb) * _hex("#3e2a16"), 0.8 * (1.0 - _smooth(0.0, 2.5, depth)))
    curl = 0.8 * (1.0 - _smooth(0.0, 6.0, depth))
    rgb = _lit(rgb, -curl) * 0.15 + rgb * 0.85
    return rgb, alpha


def _leather_tile(
    rng: np.random.Generator, h: int, w: int, contrast: float = 1.0
) -> tuple[Array, Array]:
    pebble = periodic_noise(rng, h, w, max(8, h // 5), max(8, w // 5), octaves=2)
    low = periodic_noise(rng, h, w, 3, 3, octaves=3)
    t = 0.5 + contrast * (0.08 * pebble + 0.12 * low)
    albedo = _ramp(t, ["#1c120b", "#2c1d12", "#3c2818", "#4a3220"])
    return _lit(albedo, contrast * pebble * 0.25, wrap=True), np.ones((h, w))


def _leather(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(lambda r, h, w: _leather_tile(r, h, w, 0.7))
    sdf = c.rounded(1.0, 12.0)
    depth = -sdf
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    height = 2.0 * _smooth(0.0, 6.0, depth) - 1.0 * np.exp(-(((depth - 14.0) / 1.0) ** 2))
    height += 0.6 * np.exp(-(((depth - 16.5) / 1.2) ** 2))
    rgb = _mix(rgb, np.ones_like(rgb) * _hex("#100a06"), 0.7 * (1.0 - _smooth(0.0, 7.0, depth)))
    rim = depth < 24
    lit = _lit(rgb, height)
    return np.where(rim[..., None], lit, rgb), alpha


def _flat_tile(color: str, alpha: float, grain: float = 0.02) -> Callable[..., tuple[Array, Array]]:
    def render(rng: np.random.Generator, h: int, w: int) -> tuple[Array, Array]:
        n = periodic_noise(rng, h, w, 3, 3, octaves=2)
        rgb = np.ones((h, w, 3)) * _hex(color) * (1.0 + grain * n)[..., None]
        return rgb, np.full((h, w), alpha)

    return render


def _tooltip(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(_flat_tile("#17100a", 0.93, 0.04))
    alpha = c.alpha_tile.copy()
    e = c.e
    shade = 1.0 - 0.5 * (1.0 - _smooth(8.0, 22.0, e))
    rgb = rgb * shade[..., None]
    alpha = np.where(e < 2, 0.97, alpha)
    rgb = np.where((e < 2)[..., None], _hex("#080504"), rgb)
    line = (e >= 5.5) & (e < 8.5)
    brass = _lit(_iron_albedo(c.rng, c.H, c.W, brass=True), 0.8 * _smooth(5.5, 7.0, e) * line)
    rgb = np.where(line[..., None], brass, rgb)
    alpha = np.where(line, 1.0, alpha)
    diamond = np.abs(c.dx - 16) + np.abs(c.dy - 16) - 6.0
    cover = np.clip(0.5 - diamond * SS, 0, 1)
    orn = _lit(_iron_albedo(c.rng, c.H, c.W, brass=True), _dome(diamond, 3.0), spec_amount=0.3)
    rgb = _over(rgb, orn, cover)
    alpha = np.maximum(alpha, cover)
    return rgb, alpha


def _cell(c: _Canvas, variant: str) -> tuple[Array, Array]:
    center = {"normal": "#120c08", "selected": "#2c1f10", "equipped": "#2e140e"}[variant]
    rgb = c.tile(_flat_tile(center, 0.62 if variant == "normal" else 0.7, 0.03))
    alpha = c.alpha_tile.copy()
    e = c.e
    inner = 1.0 - _smooth(9.0, 20.0, e)
    rgb = rgb * (1.0 - 0.45 * inner)[..., None]
    alpha = alpha + (0.85 - alpha) * inner * (e >= 9)
    if variant == "selected":
        glow = np.exp(-(((e - 9.0) / 6.0) ** 2)) * (e >= 9)
        rgb = rgb + glow[..., None] * _hex("#c08a30") * 0.55
        alpha = np.maximum(alpha, 0.7 * glow + alpha * (1 - glow))
    rim = (e >= 2) & (e < 7)
    rim_h = 1.2 * _smooth(2, 3.5, e) * (1 - _smooth(5.5, 7, e))
    if variant == "selected":
        rim_alb = _iron_albedo(c.rng, c.H, c.W, brass=True) * 1.25
        rim_rgb = _lit(rim_alb, rim_h, spec_amount=0.35)
    elif variant == "equipped":
        rim_rgb = _lit(_iron_albedo(c.rng, c.H, c.W) * 1.3, rim_h, spec_amount=0.25)
    else:
        g = np.where(
            c.horizontal, _wood_field(c.rng, c.H, c.W, True), _wood_field(c.rng, c.H, c.W, False)
        )
        rim_rgb = _lit(_wood_albedo(0.2 + 0.6 * g), rim_h)
    rgb = np.where(rim[..., None], rim_rgb, rgb)
    alpha = np.where(rim, 1.0, alpha)
    rgb = np.where(((e >= 7) & (e < 9))[..., None], _hex("#0a0604"), rgb)
    alpha = np.where((e >= 7) & (e < 9), 0.95, alpha)
    rgb = np.where((e < 2)[..., None], _hex("#0a0604"), rgb)
    alpha = np.where(e < 2, 0.9, alpha)
    if variant == "equipped":  # brass corner marks inside the rim
        tri = (c.dx + c.dy) - 22.0
        cover = np.clip(0.5 - tri * SS, 0, 1) * (np.minimum(c.dx, c.dy) >= 9)
        mark = _lit(_iron_albedo(c.rng, c.H, c.W, brass=True) * 1.2, 0.6 * _dome(tri, 2.0), 0.3)
        rgb = _over(rgb, mark, cover)
        alpha = np.maximum(alpha, cover)
    return rgb, alpha


def _tab(c: _Canvas, active: bool) -> tuple[Array, Array]:
    def wood_tile(rng: np.random.Generator, h: int, w: int) -> tuple[Array, Array]:
        g = _wood_field(rng, h, w, horizontal=True)
        t = (0.25 if active else 0.1) + 0.55 * g
        return _lit(_wood_albedo(t, dark=not active), (g - 0.5) * 0.5, wrap=True), np.ones((h, w))

    rgb = c.tile(wood_tile)
    top = 2.0 if active else 9.0
    sdf = c.rounded(2.0, 14.0, top=top)
    depth = -sdf
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    height = 1.4 * _smooth(0.0, 4.0, depth)
    lit = _lit(rgb, height * (depth < 8))
    lit = np.where((depth < 1.5)[..., None], _hex("#0c0805"), lit)
    if active:
        trim = (depth >= 2.0) & (depth < 4.5)
        brass = _lit(_iron_albedo(c.rng, c.H, c.W, brass=True) * 1.2, 0.6 * trim, spec_amount=0.3)
        lit = np.where(trim[..., None], brass, lit)
    else:
        lit = lit * 0.8
    return lit, alpha


def _button(c: _Canvas, state: str) -> tuple[Array, Array]:
    def wood_tile(rng: np.random.Generator, h: int, w: int) -> tuple[Array, Array]:
        g = _wood_field(rng, h, w, horizontal=True)
        base = {"normal": 0.2, "focus": 0.35, "pressed": 0.05}[state]
        alb = _wood_albedo(base + 0.55 * g, dark=state == "pressed")
        return _lit(alb, (g - 0.5) * 0.5, wrap=True), np.ones((h, w))

    rgb = c.tile(wood_tile)
    sdf = c.rounded(2.0, 8.0)
    depth = -sdf
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    sign = -1.0 if state == "pressed" else 1.0
    height = sign * 1.4 * _smooth(0.0, 5.0, depth)
    lit = _lit(rgb, height * (depth < 9))
    cap = (c.dx < 22) & (depth > 1.5)
    cap_h = 1.2 * _smooth(1.5, 4, depth) * (1 - _smooth(19, 22, c.dx)) + _dome(
        _rivets(c, [(11, 11)], 2.6), 2.6
    )
    metal = _iron_albedo(c.rng, c.H, c.W, brass=state == "focus")
    if state == "pressed":
        metal = metal * 0.75
    caps = _lit(metal, cap_h, spec_amount=0.3)
    lit = np.where(cap[..., None], caps, lit)
    lit = np.where((depth < 1.5)[..., None], _hex("#0c0805"), lit)
    if state == "focus":
        ring = (depth >= 1.5) & (depth < 3.5) & ~cap
        lit = np.where(ring[..., None], _hex("#c89848"), lit)
    return lit, alpha


def _bar_frame(c: _Canvas) -> tuple[Array, Array]:
    outer = c.rounded(1.0, 6.0)
    x0, y0, x1, y1 = BAR_OPENING
    ox = np.maximum(x0 - c.x, c.x - x1)
    oy = np.maximum(y0 - c.y, c.y - y1)
    opening = np.maximum(ox, oy)  # > 0 outside the opening
    sdf = np.maximum(outer, -opening)
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    depth = np.minimum(-outer, opening)
    height = 1.5 * _smooth(0.0, 3.0, depth) + _dome(_rivets(c, [(7, 9), (7, 39)], 2.4), 2.4)
    rgb = _lit(_iron_albedo(c.rng, c.H, c.W), height, spec_amount=0.3)
    rgb = np.where((-outer < 1.2)[..., None], _hex("#080605"), rgb)
    return rgb, alpha


_BAR_COLORS = {
    "bar_hp": ["#3e0606", "#7a1010", "#a81c1c", "#d0503c"],
    "bar_mana": ["#0a1440", "#18308a", "#2a48b8", "#6080e0"],
    "bar_enemy": ["#3a0c04", "#6e1e0c", "#9a3414", "#c86a3a"],
    "bar_air": ["#123040", "#2e6278", "#5a9ab4", "#a8d4e0"],
}


def _bar_fill(c: _Canvas) -> tuple[Array, Array]:
    v = c.y / c.h  # 0 top .. 1 bottom
    t = 0.85 - 0.75 * v + 0.25 * np.exp(-(((v - 0.22) / 0.1) ** 2))
    rgb = _ramp(t, _BAR_COLORS[c.spec.name])
    sdf = c.rounded(0.5, 3.0)
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    return rgb, alpha


def _subtitle(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(_flat_tile("#0a0705", 0.6, 0.0))
    sdf = c.rounded(2.0, 28.0)
    alpha = 0.6 * _smooth(0.0, 28.0, -sdf)
    return rgb, alpha


def _highlight(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(_flat_tile("#a87830", 0.3, 0.0))
    sdf = c.rounded(1.0, 6.0)
    depth = -sdf
    alpha = 0.3 * _smooth(0.0, 10.0, depth)
    line = np.exp(-(((depth - 2.0) / 1.0) ** 2))
    alpha = np.clip(alpha + 0.75 * line * (depth > 0), 0.0, 1.0)
    rgb = _mix(rgb, np.ones_like(rgb) * _hex("#d8a850"), line)
    return rgb, alpha


def _slider_track(c: _Canvas) -> tuple[Array, Array]:
    rgb = c.tile(_flat_tile("#0c0805", 0.9, 0.0))
    sdf = c.rounded(2.0, 9.0)
    depth = -sdf
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0) * 0.95
    height = -1.5 * _smooth(0.0, 4.0, depth)
    rim = _lit(_iron_albedo(c.rng, c.H, c.W), height, spec_amount=0.2)
    rgb = np.where((depth < 4.0)[..., None], rim, rgb)
    return rgb, alpha


def _slider_knob(c: _Canvas) -> tuple[Array, Array]:
    c.tile(_flat_tile("#8a6a34", 1.0, 0.0))  # centre bookkeeping only; the knob is not sliced
    sdf = np.sqrt((c.x - c.w / 2) ** 2 + (c.y - c.h / 2) ** 2) - 20.0
    alpha = np.clip(0.5 - sdf * SS, 0.0, 1.0)
    ring = np.abs(sdf + 7.0) - 1.0
    height = _dome(sdf, 6.0) - 0.8 * np.clip(0.5 - ring, 0.0, 1.0)
    knob = _lit(_iron_albedo(c.rng, c.H, c.W, brass=True) * 1.1, height, spec_amount=0.35)
    knob = np.where((sdf > -1.2)[..., None], _hex("#1a1208"), knob)
    return knob, alpha


def _bg_wood(c: _Canvas) -> tuple[Array, Array]:
    rng = c.rng
    g = _wood_field(rng, c.H, c.W, horizontal=False)
    board = np.floor(c.x / 64.0)
    shift = periodic_noise(rng, c.H, 4, 1, 4)[0][board.astype(int) % 4]
    t = 0.2 + 0.6 * g + 0.08 * shift
    u = c.x % 64.0
    joint = np.minimum(u, 64.0 - u)
    height = 1.5 * _smooth(0.0, 3.0, joint) + (g - 0.5) * 0.6
    rgb = _lit(_wood_albedo(t, dark=True), height, wrap=True)
    rgb = np.where((joint < 0.8)[..., None], _hex("#080504"), rgb)
    return rgb, np.ones((c.H, c.W))


def _bg_leather(c: _Canvas) -> tuple[Array, Array]:
    return _leather_tile(c.rng, c.H, c.W)


def _bg_parchment(c: _Canvas) -> tuple[Array, Array]:
    return _parchment_tile(c.rng, c.H, c.W)


_DRAW: dict[str, Callable[[_Canvas], tuple[Array, Array]]] = {
    "panel": _panel,
    "parchment": _parchment,
    "leather": _leather,
    "tooltip": _tooltip,
    "cell": lambda c: _cell(c, "normal"),
    "cell_selected": lambda c: _cell(c, "selected"),
    "cell_equipped": lambda c: _cell(c, "equipped"),
    "tab": lambda c: _tab(c, False),
    "tab_active": lambda c: _tab(c, True),
    "button": lambda c: _button(c, "normal"),
    "button_focus": lambda c: _button(c, "focus"),
    "button_pressed": lambda c: _button(c, "pressed"),
    "bar": _bar_frame,
    "bar_hp": _bar_fill,
    "bar_mana": _bar_fill,
    "bar_enemy": _bar_fill,
    "bar_air": _bar_fill,
    "subtitle": _subtitle,
    "highlight": _highlight,
    "slider_track": _slider_track,
    "slider_knob": _slider_knob,
    "bg_wood": _bg_wood,
    "bg_leather": _bg_leather,
    "bg_parchment": _bg_parchment,
}


def render_frame(s: FrameSpec, seed: int = DEFAULT_SEED) -> Array:
    """(height, width, 4) uint8 RGBA of one frame; the same seed gives the same image."""
    rng = np.random.default_rng([seed, zlib.crc32(s.name.encode())])
    c = _Canvas(s, rng)
    rgb, alpha = _DRAW[s.name](c)
    return c.finish(rgb, alpha)


# --------------------------------------------------------------------------- PNG


def _paeth(a: Array, b: Array, c: Array) -> Array:
    p = a + b - c
    pa, pb, pc = np.abs(p - a), np.abs(p - b), np.abs(p - c)
    return np.where((pa <= pb) & (pa <= pc), a, np.where(pb <= pc, b, c))


def png_bytes(rgba: Array) -> bytes:
    """RGBA PNG (8 bit) with the filter chosen per row (smallest sum of absolute residuals)."""
    height, width = rgba.shape[:2]
    img = rgba.reshape(height, width * 4).astype(np.int16)
    prev = np.vstack([np.zeros((1, width * 4), np.int16), img[:-1]])
    left = np.hstack([np.zeros((height, 4), np.int16), img[:, :-4]])
    upleft = np.hstack([np.zeros((height, 4), np.int16), prev[:, :-4]])
    candidates = [
        img,
        img - left,
        img - prev,
        img - (left + prev) // 2,
        img - _paeth(left, prev, upleft),
    ]
    filtered = [(f % 256).astype(np.uint8) for f in candidates]
    costs = np.stack([np.abs(f.astype(np.int8).astype(np.int16)).sum(axis=1) for f in filtered])
    choice = costs.argmin(axis=0)
    raw = b"".join(bytes([k]) + filtered[k][row].tobytes() for row, k in enumerate(choice))

    def chunk(kind: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(kind + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def decode_png(data: bytes) -> Array:
    """Decodes an 8-bit RGBA, non-interlaced PNG (what ``png_bytes`` writes) -> uint8 array."""
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("not a PNG")
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype != 6 or interlace != 0:
                raise ValueError("only 8-bit RGBA, non-interlaced PNGs")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(height, width * 4 + 1)
    out = np.zeros((height, width * 4), np.int32)
    prev = np.zeros(width * 4, np.int32)
    for y in range(height):
        f, line = raw[y, 0], raw[y, 1:].astype(np.int32)
        if f == 0:
            row = line
        elif f == 2:
            row = (line + prev) % 256
        elif f == 1:
            row = line.copy()
            for x in range(4, width * 4):
                row[x] = (row[x] + row[x - 4]) % 256
        else:
            row = line.copy()
            for x in range(width * 4):
                a = row[x - 4] if x >= 4 else 0
                b = prev[x]
                if f == 3:
                    row[x] = (row[x] + (a + b) // 2) % 256
                else:
                    c = prev[x - 4] if x >= 4 else 0
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                    row[x] = (row[x] + pred) % 256
        out[y] = row
        prev = row
    return out.astype(np.uint8).reshape(height, width, 4)


# --------------------------------------------------------------------------- output


def write_frames(
    out_dir: Path, seed: int = DEFAULT_SEED, only: list[str] | None = None
) -> list[Path]:
    """Writes every frame (or the names in ``only``) as ``<name>.png`` into ``out_dir``."""
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for s in SPECS:
        if only and s.name not in only:
            continue
        path = out_dir / s.file
        path.write_bytes(png_bytes(render_frame(s, seed)))
        written.append(path)
    return written


def style_toml(prefix: str = "ui/frames/") -> str:
    """The [frames.*] entries for style.toml matching ``SPECS`` (backgrounds have no slice)."""
    lines = []
    for s in SPECS:
        lines.append(f"[frames.{s.name}]  # {s.purpose}")
        lines.append(f'image = "{prefix}{s.file}"')
        lines.append(f"slice = [{', '.join(str(v) for v in s.slice)}]")
        lines.append(f"scale = {SCALE}")
        lines.append("")
    return "\n".join(lines)
