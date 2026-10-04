"""Procedural trim textures for the generated houses (W5, decision of the project owner 2026-10-04).

Own work, no photo sources: every texture is computed from a seed, so it is reproducible and free
of licences. A texture is a pair of images that tile seamlessly:

- ``albedo``: a light, nearly neutral detail layer around 1.0 on average; the renderer multiplies it
  with the palette colour (``baseColorFactor``), so the decided style colours stay as they are;
- ``normal``: tangent-space normal map (OpenGL convention, +Y up) from a height field.

Kinds (``KINDS``) and the metres one texture covers (``TILE_M``): plaster, plaster with dirt at the
foot of the wall (``plaster_low``, tiles only along u), rubble stone, timber grain (along u), plain
tiles (Biberschwanz), boards. Coordinates: u to the right, v up (row 0 of an image is the top).
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt

Image = npt.NDArray[np.float64]

TILE_M = {  # metres covered by one texture (u, v)
    "plaster": (2.0, 2.0),
    "plaster_low": (2.0, 0.8),  # the dirty band at the foot of a wall, v from the ground up
    "stone": (2.0, 2.0),
    "timber": (1.0, 0.25),  # along the beam, across it
    "roof": (2.0, 2.0),
    "boards": (1.0, 1.0),
    "roof_moss": (2.0, 2.0),
    "plaster_streak": (1.0, 0.6),  # a window wide, 0.6 m down from the sill (stretched to it)
}


# linear albedo 1.0 is stored as 0.75; materials multiply their colour by 1/0.75
ALBEDO_SCALE = 0.75
# dark wood carries strong grain: stored lower so highlights up to 2.5x survive
KIND_SCALE = {"timber": 0.4, "boards": 0.5}


def albedo_scale(kind: str) -> float:
    return KIND_SCALE.get(kind, ALBEDO_SCALE)


@dataclass
class Texture:
    kind: str
    albedo: Image  # (h, w) or (h, w, 3), linear-ish values around 1.0
    height: Image  # (h, w), 0..1
    normal_strength: float

    def normal(self) -> Image:
        return normal_map(self.height, self.normal_strength)


# --- periodic noise --------------------------------------------------------------------------


def periodic_noise(
    shape: tuple[int, int],
    rng: np.random.Generator,
    beta: float = 2.0,
    low_cut: float = 1.0,
    aniso: tuple[float, float] = (1.0, 1.0),
) -> Image:
    """Seamless fractal noise (power spectrum 1/f^beta) normalised to 0..1; ``aniso`` stretches
    the features (frequencies scaled per axis: (rows, columns))."""
    h, w = shape
    fy = np.fft.fftfreq(h)[:, None] * h / aniso[0]
    fx = np.fft.rfftfreq(w)[None, :] * w / aniso[1]
    f = np.hypot(fx, fy)
    amp = np.where(f < low_cut, 0.0, 1.0 / np.maximum(f, 1e-9) ** (beta / 2))
    spec = (rng.normal(size=amp.shape) + 1j * rng.normal(size=amp.shape)) * amp
    out = np.fft.irfft2(spec, s=shape)
    return (out - out.min()) / max(float(np.ptp(out)), 1e-12)


def periodic_cells(
    shape: tuple[int, int],
    n: int,
    rng: np.random.Generator,
    jitter: float = 0.8,
    rows: int | None = None,
) -> tuple[Image, Image, npt.NDArray[np.int64]]:
    """Seamless Voronoi cells on a jittered grid of ``n`` columns x ``rows`` (default ``n``) seeds:
    (distance to the nearest seed, edge distance = second minus first, cell index). Distances are
    measured in units of the texture width; rows of a coursed grid alternate by half a cell."""
    h, w = shape
    m = rows or n
    gy, gx = np.mgrid[0:m, 0:n]
    off = (gy % 2) * 0.5 if rows else 0.0
    sy = (gy + 0.5 + rng.uniform(-jitter, jitter, (m, n)) / 2) / m
    sx = ((gx + 0.5 + off + rng.uniform(-jitter, jitter, (m, n)) / 2) / n) % 1.0
    seeds = np.stack([sy, sx], axis=-1).reshape(-1, 2)
    yy, xx = np.mgrid[0:h, 0:w]
    py, px = (yy + 0.5) / h, (xx + 0.5) / w
    # a coursed grid weights vertical distance by rows / n: cells come out wide and low
    aspect = h / w * (m / n if rows else 1.0)
    d1 = np.full(shape, np.inf)
    d2 = np.full(shape, np.inf)
    idx = np.zeros(shape, dtype=np.int64)
    for k, (cy, cx) in enumerate(seeds):
        dy = np.abs(py - cy)
        dx = np.abs(px - cx)
        d = np.hypot(np.minimum(dy, 1 - dy) * aspect, np.minimum(dx, 1 - dx))  # wrap: seamless
        closer = d < d1
        d2 = np.where(closer, d1, np.minimum(d2, d))
        idx = np.where(closer, k, idx)
        d1 = np.where(closer, d, d1)
    return d1, d2 - d1, idx


def normal_map(height: Image, strength: float) -> Image:
    """Tangent-space normals (x right, y up = towards row 0, z out) from a periodic height field,
    as 0..1 RGB."""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) / 2
    dy = (np.roll(height, 1, axis=0) - np.roll(height, -1, axis=0)) / 2  # image rows run down
    n = np.stack([-dx * strength, -dy * strength, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def _mean_one(img: Image, mean: float = 1.0) -> Image:
    return img * (mean / float(img.mean()))


# --- kinds -----------------------------------------------------------------------------------


def plaster(size: int, rng: np.random.Generator) -> Texture:
    """Old lime plaster: mottled, grimy, with rain streaks and a few chipped spots."""
    coarse = periodic_noise((size, size), rng, beta=3.0)
    fine = periodic_noise((size, size), rng, beta=1.2, low_cut=8.0)
    trowel = periodic_noise((size, size), rng, beta=2.4, aniso=(1.0, 3.0))  # long, flat strokes
    streaks = periodic_noise((size, size), rng, beta=2.0, aniso=(10.0, 1.0))  # rain, vertical
    grime = periodic_noise((size, size), rng, beta=2.6)
    chips = periodic_noise((size, size), rng, beta=1.8, low_cut=3.0)
    chipped = np.clip((chips - 0.8) / 0.03, 0.0, 1.0)  # small spots where the plaster is gone
    albedo = (
        0.92
        + 0.32 * (coarse - 0.5)
        + 0.08 * (fine - 0.5)
        + 0.05 * (trowel - 0.5)
        - 0.35 * np.clip(streaks - 0.45, 0.0, 1.0)
        - 0.3 * np.clip(grime - 0.5, 0.0, 1.0)
    )
    albedo = albedo * (1 - chipped) + 0.45 * chipped  # dark mortar and stone behind
    height = 0.5 * fine + 0.25 * trowel + 0.15 * coarse - 0.5 * chipped
    return Texture("plaster", _mean_one(albedo), height - height.min(), 2.2)


def plaster_low(size: int, rng: np.random.Generator) -> Texture:
    """Plaster with splash dirt and green-grey damp rising from the ground (bottom rows)."""
    base = plaster(size, rng)
    h = size // 2  # 2.0 x 0.8 m: half as many rows
    albedo = base.albedo[:h] if base.albedo.ndim == 2 else base.albedo[:h, :, 0]
    height = base.height[:h]
    v = 1.0 - (np.arange(h)[:, None] + 0.5) / h  # 0 at the bottom row, 1 at the top
    blot = periodic_noise((h, size), rng, beta=2.2)
    edge = 0.35 + 0.45 * blot  # ragged upper edge of the damp
    damp = np.clip((edge - v) / 0.25, 0.0, 1.0)
    splash = np.clip(periodic_noise((h, size), rng, beta=0.8, low_cut=16.0) * 3 - 2.2, 0, 1)
    splash *= np.clip(1.0 - v / 0.5, 0.0, 1.0)
    rgb = np.repeat(albedo[:, :, None], 3, axis=2)
    tint = np.array([0.6, 0.62, 0.54])  # damp: darker, a little green
    rgb = rgb * (1 - damp[:, :, None]) + rgb * tint * damp[:, :, None]
    rgb *= 1.0 - 0.25 * splash[:, :, None]
    # the top row must match plain plaster (continuous with the wall above)
    top = np.clip((v - 0.85) / 0.15, 0.0, 1.0)[:, :, None]
    rgb = rgb * (1 - top) + np.repeat(albedo[:, :, None], 3, axis=2) * top
    return Texture("plaster_low", rgb, height, 1.6)


def _courses(rng: np.random.Generator, total: int, lo: int, hi: int) -> list[int]:
    """Random lengths in [lo, hi] that add up to ``total`` exactly (rows or stones of a row)."""
    out: list[int] = []
    left = total
    while left > hi:
        n = int(rng.integers(lo, hi + 1))
        if left - n < lo:
            n = left - lo
        out.append(n)
        left -= n
    out.append(left)
    return out


def stone(size: int, rng: np.random.Generator) -> Texture:
    """Coursed rubble masonry: rough courses of different height (also thin flat stones), stones
    of different length with broken, uneven edges, deep dark mortar joints; darker than the
    palette."""
    px_m = size / TILE_M["stone"][0]  # pixels per metre
    rows = _courses(rng, size, int(0.08 * px_m), int(0.32 * px_m))
    wobble = periodic_noise((size, size), rng, beta=2.8)  # large-scale edge displacement
    ragged = periodic_noise((size, size), rng, beta=1.4, low_cut=10.0)  # small chips
    grain = periodic_noise((size, size), rng, beta=1.3, low_cut=8.0)
    pits = np.clip(periodic_noise((size, size), rng, beta=0.6, low_cut=24.0) * 4 - 3.0, 0, 1)
    dist = np.full((size, size), -1.0)  # distance inside the stone (pixels), < 0 in the joint
    shade = np.ones((size, size))
    warm = np.zeros((size, size))
    tilt = np.zeros((size, size))  # each stone face a little inclined: lit unevenly
    y0 = 0
    for h in rows:
        widths = _courses(rng, size, int(0.15 * px_m), int(0.65 * px_m))
        x0 = int(rng.integers(0, size))  # each course starts somewhere else (wraps)
        ys = np.arange(y0, y0 + h)
        for w in widths:
            cols = (x0 + np.arange(w)) % size
            ly = (ys - y0)[:, None].astype(float)  # local coordinates in the cell
            lx = np.arange(w)[None, :].astype(float)
            m = min(h, w)
            # skewed, inset edges: no stone is a rectangle
            sl, sr = rng.uniform(-0.2, 0.2, 2)
            st, sb = rng.uniform(-0.08, 0.08, 2)
            ol, orr, ot, ob = rng.uniform(0.0, 0.04 * m, 4)
            d_left = lx - (ol + sl * (ly - h / 2))
            d_right = (w - 1 - orr + sr * (ly - h / 2)) - lx
            d_top = ly - (ot + st * (lx - w / 2))
            d_bottom = (h - 1 - ob + sb * (lx - w / 2)) - ly
            dx = np.minimum(d_left, d_right)
            dy = np.minimum(d_top, d_bottom)
            r = m * rng.uniform(0.05, 0.25)  # some corners sharp, some broken off
            corner = r - np.hypot(np.clip(r - dx, 0, None), np.clip(r - dy, 0, None))
            sub = np.ix_(ys, cols)
            d = (np.minimum(np.minimum(dx, dy), corner)
                 - 0.07 * m * (wobble[sub] - 0.45) - 2.5 * (ragged[sub] - 0.5))  # fmt: skip
            dist[sub] = d
            shade[sub] = rng.uniform(0.45, 1.0)
            warm[sub] = rng.uniform(0.0, 0.1)
            gy = (ys - y0)[:, None] / max(h - 1, 1) - 0.5
            gx = np.arange(w)[None, :] / max(w - 1, 1) - 0.5
            tilt[sub] = rng.uniform(-0.25, 0.25) * gx + rng.uniform(-0.3, 0.1) * gy
            x0 += w
        y0 += h
    joint = 0.8 + 2.2 * periodic_noise((size, size), rng, beta=2.0)  # 0.3 .. 1.2 cm per side
    mortar = np.clip(1.0 - (dist - joint) / 1.2, 0.0, 1.0)
    face = np.clip((dist - joint) / 10.0, 0.0, 1.0) ** 0.6  # broken edges fall off
    val = shade * (0.85 + 0.45 * (grain - 0.5) + tilt) - 0.3 * pits
    val *= 0.7 + 0.3 * face
    rgb = np.stack([val * (1 + warm), val, val * (1 - warm)], axis=-1)
    lime = np.array([0.2, 0.19, 0.17])  # recessed, dirty joints in shadow: clearly darker
    rgb = (
        rgb * (1 - mortar[:, :, None]) + lime * (0.8 + 0.4 * grain[:, :, None]) * mortar[:, :, None]
    )
    height = 0.85 * face * (1 - mortar) + 0.12 * grain - 0.1 * pits + 0.2 * tilt * (1 - mortar)
    return Texture("stone", _mean_one(np.clip(rgb, 0.05, None), 0.7), height - height.min(), 6.0)


def timber(size: int, rng: np.random.Generator) -> Texture:
    """Old oak beam seen from the side: strong grain along u, long drying cracks, knots."""
    h = size // 4  # 1.0 x 0.25 m
    warp = periodic_noise((h, size), rng, beta=3.0, aniso=(1.0, 6.0))
    v = (np.arange(h)[:, None] + 0.5) / h
    rings = 0.5 + 0.5 * np.sin((v * 18 + warp * 4.0) * 2 * np.pi)
    fibres = periodic_noise((h, size), rng, beta=1.0, low_cut=4.0, aniso=(1.0, 14.0))
    crack_field = periodic_noise((h, size), rng, beta=2.6, aniso=(1.0, 12.0))
    crack = np.clip(1.0 - np.abs(crack_field - 0.5) * 30, 0.0, 1.0)
    crack *= periodic_noise((h, size), rng, beta=3.0, aniso=(1.0, 4.0)) > 0.35  # not everywhere
    yy, xx = np.mgrid[0:h, 0:size]
    knots = np.zeros((h, size))
    for _ in range(3):
        cy, cx = rng.uniform(0, h), rng.uniform(0, size)
        dyk = np.minimum(np.abs(yy - cy), h - np.abs(yy - cy))
        dxk = np.minimum(np.abs(xx - cx), size - np.abs(xx - cx))
        knots = np.maximum(knots, np.clip(1 - np.hypot(dxk / 14, dyk / 8), 0, 1))
    worn = periodic_noise((h, size), rng, beta=2.2)
    albedo = (
        1.0
        + 0.9 * (rings - 0.5)
        + 0.6 * (fibres - 0.5)
        + 0.5 * (worn - 0.5)
        - 0.9 * crack
        - 0.6 * knots
    )
    height = 0.5 * rings + 0.35 * fibres - 0.9 * crack - 0.3 * knots
    return Texture("timber", _mean_one(np.clip(albedo, 0.08, None)), height - height.min(), 3.5)


def roof(size: int, rng: np.random.Generator) -> Texture:
    """Plain tiles (Biberschwanz) in staggered rows; u along the eave, v up the slope."""
    rows, cols = 13, 11  # per 2 m: course 0.155 m, tile 0.18 m
    yy, xx = np.mgrid[0:size, 0:size]
    rv = (size - 1 - yy + 0.5) / size * rows  # row coordinate from the bottom (eave) up
    row = np.floor(rv).astype(int)
    cu = xx / size * cols + (row % 2) * 0.5  # staggered
    col = np.floor(cu).astype(int) % cols
    fu = cu - np.floor(cu)  # 0..1 across a tile
    fv = rv - row  # 0 at the visible lower edge of a tile, 1 under the next row
    # segmental lower end: the tile's bottom edge rises towards its corners
    round_end = 0.42 * (1 - np.sqrt(np.clip(1 - (2 * fu - 1) ** 2, 0, 1)))
    gap = (fu < 0.03) | (fu > 0.97) | (fv < round_end)
    shade = rng.uniform(0.72, 1.2, (rows, cols))[row % rows, col]
    grain = periodic_noise((size, size), rng, beta=1.6, low_cut=6.0)
    under = np.clip((fv - 0.78) / 0.22, 0.0, 1.0)  # shadow under the next row's edge
    albedo = shade * (0.92 + 0.14 * (grain - 0.5)) * (1 - 0.35 * under)
    # through a gap one sees the tile of the row below, in its shadow
    albedo = np.where(gap, 0.5 * shade, albedo)
    height = np.where(gap, 0.05, 0.35 + 0.65 * fv) + 0.08 * grain
    return Texture("roof", _mean_one(albedo), height, 3.0)


def boards(size: int, rng: np.random.Generator) -> Texture:
    """Vertical boards (doors, shutters): 0.14 m wide, grain along v."""
    n = 7
    xx = np.arange(size)[None, :] / size * n
    seam = np.abs(xx - np.round(xx)) < 0.025
    grain = periodic_noise((size, size), rng, beta=1.4, low_cut=3.0, aniso=(10.0, 1.0))
    shade = rng.uniform(0.85, 1.1, n)[np.floor(xx).astype(int) % n]
    albedo = np.where(seam, 0.4, shade * (0.9 + 0.12 * (grain - 0.5)))
    albedo = np.broadcast_to(albedo, (size, size)).copy()
    height = np.where(seam, 0.0, 0.6 + 0.3 * grain)
    return Texture("boards", _mean_one(albedo), np.broadcast_to(height, (size, size)).copy(), 2.0)


def roof_moss(size: int, rng: np.random.Generator) -> Texture:
    """Tiles of the shady side: moss in the joints and along the lower tile edges, in patches."""
    base = roof(size, rng)
    patches = np.clip((periodic_noise((size, size), rng, beta=2.4) - 0.45) / 0.3, 0.0, 1.0)
    low_edges = np.clip(1.0 - base.height / 0.5, 0.0, 1.0)  # joints and lower edges
    moss = np.clip(patches * (0.4 + 0.8 * low_edges), 0.0, 1.0)
    rgb = np.repeat(base.albedo[:, :, None], 3, axis=2)
    green = np.array([0.75, 1.0, 0.45])
    rgb = rgb * (1 - moss[:, :, None]) + rgb * green * 1.15 * moss[:, :, None]
    return Texture("roof_moss", _mean_one(rgb), base.height + 0.15 * moss, base.normal_strength)


STREAK_VARIANTS = 4  # side by side in the streak texture, one chosen per window


def plaster_streak(size: int, rng: np.random.Generator) -> Texture:
    """Plaster under a window sill with a soft rain streak baked in, ``STREAK_VARIANTS`` side by
    side; the streak is part of the wall (opaque), so it fades smoothly into plain plaster at the
    edges: a darker smudge under the sill running out in a few short, soft trails."""
    base = plaster(size, rng)
    h = size // 2
    w = size // STREAK_VARIANTS
    albedo = base.albedo[:h]
    v = (np.arange(h)[:, None] + 0.5) / h  # 0 at the sill, 1 at the bottom
    marks = []
    for _ in range(STREAK_VARIANTS):
        u = (np.arange(w)[None, :] + 0.5) / w
        soft = periodic_noise((h, w), rng, beta=3.2, aniso=(5.0, 1.0))
        smudge = np.exp(-v / rng.uniform(0.18, 0.32))
        trails = np.zeros((h, w))
        for _ in range(int(rng.integers(2, 5))):
            cu = rng.uniform(0.18, 0.82)
            width = rng.uniform(0.08, 0.18)
            length = rng.uniform(0.5, 1.0)
            lobe = np.exp(-(((u - cu) / width) ** 2))
            trails = np.maximum(trails, lobe * np.clip(1.0 - v / length, 0.0, 1.0) ** 1.2)
        sides = np.clip(np.minimum(u, 1 - u) / 0.15, 0.0, 1.0) ** 0.8
        bottom = np.clip((1.0 - v) / 0.25, 0.0, 1.0)
        mark = np.clip(0.9 * smudge + 1.0 * trails, 0.0, 1.0) * (0.8 + 0.4 * soft) * sides * bottom
        marks.append(np.clip(mark, 0.0, 1.0))
    mark = np.concatenate(marks, axis=1)
    tint = np.array([0.42, 0.4, 0.36])  # grey-brown dirt
    rgb = np.repeat(albedo[:, :, None], 3, axis=2)
    rgb = rgb * (1 - mark[:, :, None]) + rgb * tint * mark[:, :, None]
    return Texture("plaster_streak", rgb, base.height[:h], base.normal_strength)


def cobbles(size: int, rng: np.random.Generator) -> Texture:
    """Cobblestones (Kopfsteinpflaster) for the terrain: round, irregular stones of different
    size, not in rows, with sandy gaps; the terrain has no normal maps, so the roundness is in
    the albedo (lit from above, darker towards the gaps)."""
    n = 13  # about 13 x 13 stones per tile (2 m): 8..20 cm
    d1, edge, idx = periodic_cells((size, size), n, rng, jitter=1.0)
    spacing = size / n
    radius = rng.uniform(0.55, 0.8, n * n)[idx] * spacing  # bigger and smaller stones
    wobble = 2.5 * (periodic_noise((size, size), rng, beta=2.2) - 0.5)
    gap = 1.5 + 1.5 * periodic_noise((size, size), rng, beta=2.0)
    # round stones (circles about their seed), never across the cell border to the neighbour
    inside = np.minimum(edge * size - gap, radius - d1 * size + wobble)
    dome = np.clip(inside / 9.0, 0.0, 1.0)
    dome = np.sqrt(dome * (2 - dome))  # round profile
    light = np.roll(dome, (2, 2), axis=(0, 1)) - dome  # light from the upper left
    shade = rng.uniform(0.75, 1.1, n * n)[idx]
    warm = rng.uniform(-0.04, 0.06, n * n)[idx]
    grain = periodic_noise((size, size), rng, beta=1.2, low_cut=10.0)
    val = shade * (0.55 + 0.45 * dome + 0.35 * np.clip(-light, -0.3, 0.6)) * (0.9 + 0.2 * grain)
    rgb = np.stack([val * (1 + warm), val, val * (1 - warm)], axis=-1)
    sand = np.array([0.6, 0.56, 0.48]) * (0.8 + 0.3 * grain[:, :, None])
    in_gap = (dome <= 0.0)[:, :, None]
    rgb = np.where(in_gap, sand, rgb)
    return Texture("cobbles", _mean_one(rgb), dome, 3.0)


KINDS: dict[str, Callable[[int, np.random.Generator], Texture]] = {
    "plaster": plaster,
    "plaster_low": plaster_low,
    "stone": stone,
    "timber": timber,
    "roof": roof,
    "boards": boards,
    "roof_moss": roof_moss,
    "plaster_streak": plaster_streak,
}
TERRAIN_KINDS = {"cobbles": cobbles}  # baked into the terrain layer albedos (export/splat.py)


def make(kind: str, size: int = 1024, seed: int = 7) -> Texture:
    """One texture; the seed is mixed with the kind, so kinds differ but stay reproducible."""
    rng = np.random.default_rng([seed, sum(ord(c) * 31**i for i, c in enumerate(kind)) % 2**31])
    return (KINDS.get(kind) or TERRAIN_KINDS[kind])(size, rng)


def to_png(img: Image, path: Path, srgb: bool, scale: float = ALBEDO_SCALE) -> None:
    """Write an image (0..1, or around 1.0 for albedo) as 8-bit PNG; albedo is stored sRGB with
    1.0 at 200/255 so that variations above 1 survive (the material factor makes up for it,
    ``ALBEDO_SCALE``)."""
    from PIL import Image as PILImage

    a = np.clip(img, 0.0, None)
    alpha = None
    if a.ndim == 3 and a.shape[2] == 4:  # alpha stays linear
        a, alpha = a[:, :, :3], np.clip(a[:, :, 3:], 0.0, 1.0)
    if srgb:
        a = np.clip(a * scale, 0.0, 1.0)
        a = np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055)
    if alpha is not None:
        a = np.concatenate([a, alpha], axis=2)
    a = np.clip(np.round(a * 255), 0, 255).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = "L" if a.ndim == 2 else ("RGBA" if a.shape[2] == 4 else "RGB")
    PILImage.fromarray(a, mode).save(path, optimize=True)
