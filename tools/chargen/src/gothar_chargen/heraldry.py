"""Own coat of arms of the castle on the Engelberg (Stauferburg Engelberg, owner decision
2026-11-28: an angel or angel wings on a shield). Everything is procedural (numpy and the standard
library only), deterministic for a seed, and written as RGBA PNG to
``assets/source/items/textures/heraldry/``:

- ``engelberg_arms.png``: the shield alone (transparent outside), the motif for tabards and decor;
- ``engelberg_banner.png``: a hanging banner (cloth, field colour, the arms, swallowtail cut out in
  alpha - use alpha MASK) for the castle; ``welt`` maps it onto its banner and flag geometry.

The arms are freely designed (``figuren-stil.md``: no likeness to real arms or brands): on a blue
heater shield with a gold bordure a silver angel in a long robe with a golden halo between a pair
of raised silver wings. ``DRAFTS`` holds two alternatives for the review sheet (wings only, a
winged angel head on red). Colours stay within the style sheet (saturation <= 0.6, no pure white or
black).

Coordinates of a shield: ``u`` right, ``v`` up, the shield spans u in -0.8..0.8 and v in -0.95..0.9.
"""

from __future__ import annotations

import colorsys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from gothar_chargen.ui_frames import periodic_noise, png_bytes

Array = np.ndarray

DEFAULT_SEED = 7
SS = 3  # supersampling per axis
HERALDRY_DIR = Path("assets/source/items/textures/heraldry")
ARMS_FILE = "engelberg_arms.png"
BANNER_FILE = "engelberg_banner.png"
ARMS_SIZE = 512
BANNER_SIZE = (512, 1024)  # width, height


def _hex(color: str) -> Array:
    c = color.lstrip("#")
    return np.array([int(c[i : i + 2], 16) / 255.0 for i in (0, 2, 4)])


@dataclass(frozen=True)
class Arms:
    """Tinctures and charges of one design."""

    name: str
    field: str  # field colour
    metal: str  # bordure and halo (gold)
    wing: str  # the wings (silver)
    line: str  # outlines
    figure: str = "angel"  # between the wings: "angel" (head and robe), "head" or "none"
    purpose: str = ""

    def colours(self) -> dict[str, Array]:
        return {k: _hex(getattr(self, k)) for k in ("field", "metal", "wing", "line")}


# The chosen design and two alternatives for the review sheet (the owner may switch).
ENGELBERG = Arms(
    "engelberg", "#2f4a6a", "#b0904c", "#e6e0d2", "#2a2a2e", "angel", "blue, silver angel, wings"
)
DRAFTS: tuple[Arms, ...] = (
    ENGELBERG,
    Arms("wings_blue", "#2f4a6a", "#b0904c", "#e6e0d2", "#2a2a2e", "none", "blue, silver wings"),
    Arms("head_red", "#7a3632", "#b0904c", "#e6e0d2", "#2a2a2e", "head", "red, winged angel head"),
)


def saturation(color: str) -> float:
    return colorsys.rgb_to_hsv(*_hex(color))[1]


# --------------------------------------------------------------------------- shapes


def _cover(sdf: Array, px: float) -> Array:
    """Anti-aliased coverage of a signed distance (negative inside), ``px`` = one pixel."""
    return np.clip(0.5 - sdf / px, 0.0, 1.0)


SHIELD_TOP = 0.9
CHARGE_DROP = 0.12
CHARGE_SCALE = 0.9
SHIELD_HALF = 0.8
SHIELD_KNEE = 0.2  # sides straight above, arcs below
SHIELD_POINT = -0.95
# arc through (HALF, KNEE) with a vertical tangent and through (0, POINT)
_DROP = SHIELD_KNEE - SHIELD_POINT
ARC_R = (SHIELD_HALF**2 + _DROP**2) / (2 * SHIELD_HALF)
ARC_C = SHIELD_HALF - ARC_R


def shield_sdf(u: Array, v: Array) -> Array:
    """Signed distance to the heater shield (negative inside)."""
    au = np.abs(u)
    side = np.where(
        v >= SHIELD_KNEE, au - SHIELD_HALF, np.hypot(au - ARC_C, v - SHIELD_KNEE) - ARC_R
    )
    return np.maximum(v - SHIELD_TOP, side)


def _feather(
    u: Array, v: Array, base: tuple[float, float], angle: float, length: float, width: float
) -> tuple[Array, Array, Array]:
    """A pointed feather from ``base`` along ``angle`` (degrees, 0 = +u): sdf, along, across."""
    d = np.array([np.cos(np.radians(angle)), np.sin(np.radians(angle))])
    px, py = u - base[0], v - base[1]
    s = px * d[0] + py * d[1]
    t = -px * d[1] + py * d[0]
    f = np.clip(s / length, 0.0, 1.0)
    # wide soon after the base, tapering to a rounded tip
    half = width * np.sqrt(np.clip(f / 0.25, 0.0, 1.0)) * np.clip((1.0 - f) / 0.55, 0.0, 1.0) ** 0.7
    half = np.maximum(half, width * 0.18)
    sdf = np.abs(t) - half
    sdf = np.where(s < 0.0, np.hypot(px, py) - width * 0.18, sdf)
    sdf = np.where(s > length, np.hypot(s - length, t) - width * 0.18, sdf)
    return sdf, f, t / np.maximum(half, 1e-6)


# One wing (the right one; the left one is mirrored at u = 0): the arm is a quadratic Bezier from
# the shoulder behind the head, arching up and out to the wrist; rows of feathers hang from it.
# Row: (count, arm parameter from, to, angle from, to (degrees, 0 = +u), length from, to, width).
ARM = ((0.05, 0.14), (0.26, 0.78), (0.54, 0.56))
WING_ROWS = (
    (9, 0.16, 1.00, -96.0, -78.0, 0.34, 0.74, 0.066),  # primaries: longest at the wrist
    (8, 0.12, 0.96, -92.0, -66.0, 0.20, 0.40, 0.062),  # coverts
    (7, 0.08, 0.92, -88.0, -52.0, 0.10, 0.17, 0.056),  # small coverts along the arm
)


def _arm(t: float) -> tuple[tuple[float, float], tuple[float, float]]:
    """Point and unit tangent of the arm at parameter t (0 shoulder .. 1 wrist)."""
    (x0, y0), (x1, y1), (x2, y2) = ARM
    a, b, c = (1 - t) ** 2, 2 * (1 - t) * t, t * t
    p = (a * x0 + b * x1 + c * x2, a * y0 + b * y1 + c * y2)
    dx = 2 * (1 - t) * (x1 - x0) + 2 * t * (x2 - x1)
    dy = 2 * (1 - t) * (y1 - y0) + 2 * t * (y2 - y1)
    n = float(np.hypot(dx, dy))
    return p, (dx / n, dy / n)


def _wing(u: Array, v: Array, c: dict[str, Array], px: float) -> tuple[Array, Array]:
    """Both wings (mirrored at u = 0): rgb, alpha."""
    au = np.abs(u)
    rgb = np.zeros(u.shape + (3,))
    alpha = np.zeros(u.shape)
    line_w = 0.011
    for count, t0, t1, a0, a1, l0, l1, width in WING_ROWS:
        # from the wrist inwards: the inner feathers lie over the outer ones
        for i in reversed(range(count)):
            k = i / (count - 1)
            base, _ = _arm(t0 + (t1 - t0) * k)
            ang = a0 + (a1 - a0) * k
            length = l0 + (l1 - l0) * k**1.3
            r = np.radians(ang)
            tip = (base[0] + length * np.cos(r), base[1] + length * np.sin(r))
            pad = width + 2 * px
            near = (
                (au > min(base[0], tip[0]) - pad)
                & (au < max(base[0], tip[0]) + pad)
                & (v > min(base[1], tip[1]) - pad)
                & (v < max(base[1], tip[1]) + pad)
            )
            if not near.any():
                continue
            sdf, f, across = _feather(au[near], v[near], base, ang, length, width)
            cover = _cover(sdf, px)
            inner = _cover(sdf + line_w, px)
            shade = 0.82 + 0.18 * (1.0 - np.abs(across)) - 0.12 * f
            body = c["wing"] * np.clip(shade, 0.0, 1.0)[..., None]
            shaft = _cover(np.abs(across) * width - 0.004, px) * (f < 0.8) * (f > 0.08)
            body = body * (1.0 - 0.3 * shaft[..., None])
            col = c["line"] * (1.0 - inner[..., None]) + body * inner[..., None]
            rgb[near] = rgb[near] * (1.0 - cover[..., None]) + col * cover[..., None]
            alpha[near] = np.maximum(alpha[near], cover)
    # the arm itself: a band along the curve with a dark outline (the leading edge)
    ts = np.linspace(0.0, 1.0, 48)
    pts = np.array([_arm(float(t))[0] for t in ts])
    dist = np.full(u.shape, 1e3)
    for (xa, ya), (xb, yb) in zip(pts[:-1], pts[1:], strict=True):
        ex, ey = xb - xa, yb - ya
        h = np.clip(((au - xa) * ex + (v - ya) * ey) / (ex * ex + ey * ey), 0.0, 1.0)
        dist = np.minimum(dist, np.hypot(au - xa - h * ex, v - ya - h * ey))
    band = dist - 0.03
    cover = _cover(band, px)
    inner = _cover(band + line_w, px)
    col = c["line"] * (1.0 - inner[..., None]) + (c["wing"] * 0.95) * inner[..., None]
    rgb = rgb * (1.0 - cover[..., None]) + col * cover[..., None]
    return rgb, np.maximum(alpha, cover)


HEAD_CENTRE = (0.0, 0.22)
HEAD_R = 0.112
HALO = (0.155, 0.028)  # radius, half width of the ring


def _angel_head(u: Array, v: Array, c: dict[str, Array], px: float) -> tuple[Array, Array]:
    """A small angel head with a golden halo: rgb, alpha."""
    du, dv = u - HEAD_CENTRE[0], v - HEAD_CENTRE[1]
    r = np.hypot(du, dv)
    line_w = 0.012
    # halo ring behind the head
    ring = np.abs(r - HALO[0]) - HALO[1]
    cover = _cover(ring, px)
    inner = _cover(ring + line_w * 0.8, px)
    col = c["line"] * (1.0 - inner[..., None]) + c["metal"] * inner[..., None]
    rgb, alpha = col * cover[..., None], cover
    # head (silver, slightly warm) with hair (gold) on top, a simple neck below
    neck = np.maximum(np.abs(du) - 0.045, np.abs(dv + HEAD_R * 1.15) - 0.06)
    head = np.minimum(r - HEAD_R, neck)
    cover = _cover(head, px)
    inner = _cover(head + line_w, px)
    face = c["wing"] * np.array([1.0, 0.95, 0.9])
    # hair (gold): a cap over the forehead and locks down the sides
    hair = (r < HEAD_R) & (
        (dv > HEAD_R * 0.25) | ((np.abs(du) > HEAD_R * 0.62) & (dv > -HEAD_R * 0.35))
    )
    body = np.where(hair[..., None], c["metal"] * 0.95, face)
    # eyes: two small dark dots
    for sx in (-1.0, 1.0):
        eye = np.hypot(du - sx * HEAD_R * 0.38, dv + HEAD_R * 0.02) - HEAD_R * 0.09
        e = _cover(eye, px)
        body = body * (1.0 - e[..., None]) + c["line"] * e[..., None]
    col = c["line"] * (1.0 - inner[..., None]) + body * inner[..., None]
    rgb = rgb * (1.0 - cover[..., None]) + col * cover[..., None]
    return rgb, np.maximum(alpha, cover)


ROBE = (0.10, 0.25, -0.62)  # half width at the neck, half width at the hem, height of the hem


def _robe(u: Array, v: Array, c: dict[str, Array], px: float) -> tuple[Array, Array]:
    """The angel's long robe below the head (silver, with folds and folded hands): rgb, alpha."""
    top = HEAD_CENTRE[1] - HEAD_R * 0.9
    neck, hem_w, hem = ROBE
    f = np.clip((top - v) / (top - hem), 0.0, 1.0)  # 0 at the neck .. 1 at the hem
    half = neck + (hem_w - neck) * np.sqrt(f)  # shoulders round, then falling straight
    hem_line = hem - 0.035 * np.cos(u / hem_w * np.pi * 1.5)  # wavy hem
    sdf = np.maximum(np.abs(u) - half, np.maximum(v - top, hem_line - v))
    cover = _cover(sdf, px)
    line_w = 0.011
    inner = _cover(sdf + line_w, px)
    shade = 0.9 + 0.1 * np.cos(u / np.maximum(half, 1e-6) * np.pi * 0.5)
    body = c["wing"] * shade[..., None]
    # folds: thin dark lines fanning down from the waist
    waist = top - 0.22
    for x in (-0.55, -0.2, 0.2, 0.55):
        fold = np.abs(u - x * half * np.clip((waist - v) / (waist - hem), 0, 1)) - 0.004
        m = _cover(fold, px) * (v < waist - 0.03) * (v > hem + 0.04)
        body = body * (1.0 - 0.4 * m[..., None])
    # folded hands at the chest: a small rounded bar with sleeves
    hands = np.hypot(np.maximum(np.abs(u) - 0.05, 0.0), v - (top - 0.13)) - 0.032
    h = _cover(hands, px)
    hi = _cover(hands + line_w * 0.8, px)
    skin = c["wing"] * np.array([1.0, 0.95, 0.9])
    hand_col = c["line"] * (1.0 - hi[..., None]) + skin * hi[..., None]
    body = body * (1.0 - h[..., None]) + hand_col * h[..., None]
    col = c["line"] * (1.0 - inner[..., None]) + body * inner[..., None]
    return col, cover


def shield(u: Array, v: Array, arms: Arms, px: float) -> tuple[Array, Array]:
    """The whole shield (field, bordure, charges) at shield coordinates: rgb, alpha (0..1)."""
    c = arms.colours()
    sdf = shield_sdf(u, v)
    outer = _cover(sdf, px)
    # field with a soft vertical gradient and a curved light (a slightly domed shield)
    light = 1.0 + 0.10 * v - 0.12 * (u / SHIELD_HALF) ** 2
    rgb = c["field"] * light[..., None]
    cv = v + CHARGE_DROP  # the charges sit a little below the middle of the shield
    k = CHARGE_SCALE
    wings, wa = _wing(u / k, cv / k, c, px / k)
    rgb = rgb * (1.0 - wa[..., None]) + wings * wa[..., None]
    if arms.figure == "angel":
        robe, ra = _robe(u / k, cv / k, c, px / k)
        rgb = rgb * (1.0 - ra[..., None]) + robe * ra[..., None]
    if arms.figure in ("angel", "head"):
        head, ha = _angel_head(u / k, cv / k, c, px / k)
        rgb = rgb * (1.0 - ha[..., None]) + head * ha[..., None]
    # bordure in gold between two dark lines, clipped to the shield
    bord = np.maximum(-(sdf + 0.075), sdf)  # band from the edge 0.075 inwards
    band = _cover(bord, px)
    lines = _cover(np.abs(sdf + 0.075) - 0.008, px) + _cover(np.abs(sdf + 0.004) - 0.010, px)
    gold = c["metal"] * (0.92 + 0.12 * v)[..., None]
    rgb = rgb * (1.0 - band[..., None]) + gold * band[..., None]
    rgb = (
        rgb * (1.0 - np.clip(lines, 0, 1)[..., None]) + c["line"] * np.clip(lines, 0, 1)[..., None]
    )
    return rgb, outer


# --------------------------------------------------------------------------- images


def _grid(width: int, height: int) -> tuple[Array, Array]:
    """Supersampled pixel centres in image pixels (x right, y down)."""
    yy, xx = np.mgrid[0 : height * SS, 0 : width * SS]
    return (xx + 0.5) / SS, (yy + 0.5) / SS


def _finish(rgb: Array, alpha: Array, width: int, height: int) -> Array:
    """Downsample (alpha-weighted colour) -> uint8 RGBA."""
    a = np.clip(alpha, 0.0, 1.0).reshape(height, SS, width, SS)
    col = np.clip(rgb, 0.0, 1.0).reshape(height, SS, width, SS, 3)
    asum = a.sum(axis=(1, 3))
    csum = (col * a[..., None]).sum(axis=(1, 3))
    plain = col.mean(axis=(1, 3))
    out_rgb = np.where(asum[..., None] > 1e-6, csum / np.maximum(asum, 1e-6)[..., None], plain)
    out = np.zeros((height, width, 4), np.uint8)
    out[..., :3] = np.round(out_rgb * 255)
    out[..., 3] = np.round(asum / (SS * SS) * 255)
    return out


def render_arms(arms: Arms = ENGELBERG, size: int = ARMS_SIZE) -> Array:
    """(size, size, 4) uint8 RGBA: the shield, transparent outside."""
    x, y = _grid(size, size)
    scale = size / 2.0 / 0.97  # shield fills the square with a small margin
    u = (x - size / 2.0) / scale
    v = (size / 2.0 - y) / scale - 0.025
    rgb, alpha = shield(u, v, arms, 1.0 / scale)
    return _finish(rgb, alpha, size, size)


def _cloth(rng: np.random.Generator, h: int, w: int) -> Array:
    """Woven cloth brightness around 1 (threads + soft blotches) at supersampled size."""
    warp = np.sin((np.arange(w) + 0.5) / SS * np.pi * 0.9)[None, :]
    weft = np.sin((np.arange(h) + 0.5) / SS * np.pi * 0.9)[:, None]
    threads = 0.025 * (warp * weft)
    blot = periodic_noise(rng, h, w, 6, 3, octaves=4)
    fine = periodic_noise(rng, h, w, 48, 24, octaves=2)
    return 1.0 + threads + 0.045 * blot + 0.02 * fine


def render_banner(
    arms: Arms = ENGELBERG, seed: int = DEFAULT_SEED, size: tuple[int, int] = BANNER_SIZE
) -> Array:
    """(height, width, 4) uint8 RGBA: a hanging banner. Top: hem for the pole (8 %), sides: gold
    edging, the arms in the upper middle, at the bottom a swallowtail (alpha 0 in the cut)."""
    width, height = size
    rng = np.random.default_rng([seed, 0xBA77E2])
    x, y = _grid(width, height)
    c = arms.colours()
    s = x / width  # 0..1 across
    t = y / height  # 0..1 down
    cloth = _cloth(rng, height * SS, width * SS)
    rgb = c["field"] * cloth[..., None]
    # hem at the top (folded over a pole): darker band with a seam
    hem = 0.08
    in_hem = t < hem
    rgb = np.where(in_hem[..., None], rgb * 0.82, rgb)
    seam = (np.abs(t - hem) < 0.0025) & ((np.floor(x / 6.0) % 2) == 0)
    rgb = np.where(seam[..., None], c["metal"] * 0.8, rgb)
    shade = 1.0 - 0.10 * np.exp(-(((t - hem) / 0.02) ** 2)) * (t > hem)
    rgb = rgb * shade[..., None]
    # gold edging along the sides and above the swallowtail
    edge = 0.045
    side = np.minimum(s, 1.0 - s)
    tail_depth = 0.16  # cut-out depth of the swallowtail (fraction of the height)
    cut = 1.0 - tail_depth * (1.0 - np.abs(2.0 * s - 1.0))  # bottom border line
    bottom = cut - t  # distance (in height fractions) above the bottom outline
    gold_band = ((side < edge) | (bottom * height / width < edge)) & (t > hem)
    stripe = (np.abs(side - edge) < 0.006) | (np.abs(bottom * height / width - edge) < 0.006)
    rgb = np.where(gold_band[..., None], c["metal"] * cloth[..., None], rgb)
    rgb = np.where((stripe & (t > hem))[..., None], c["line"] * 1.2, rgb)
    # the arms in the upper middle
    centre = (0.5, 0.40)
    sw = 0.78  # shield width as a fraction of the banner width
    scale = width * sw / (2 * SHIELD_HALF)
    u = (x - centre[0] * width) / scale
    v = (centre[1] * height - y) / scale
    m = shield_sdf(u, v) < 2.0 / scale
    srgb, sa = shield(u[m], v[m], arms, 1.0 / scale)
    srgb = srgb * (0.6 + 0.4 * cloth[m])[..., None]  # embroidered into the cloth
    rgb[m] = rgb[m] * (1.0 - sa[..., None]) + srgb * sa[..., None]
    # weathering: faded towards the bottom, a little dirt
    dirt = periodic_noise(rng, height * SS, width * SS, 4, 2, octaves=3)
    fade = 1.0 - 0.06 * t + 0.035 * dirt
    rgb = rgb * fade[..., None]
    alpha = np.clip(0.5 + bottom * height, 0.0, 1.0)  # the swallowtail cut, 1 px soft
    return _finish(rgb, alpha, width, height)


# --------------------------------------------------------------------------- output


def write_heraldry(out_dir: Path, seed: int = DEFAULT_SEED) -> list[Path]:
    """Writes ``engelberg_arms.png`` and ``engelberg_banner.png`` into ``out_dir``."""
    out_dir.mkdir(parents=True, exist_ok=True)
    arms = out_dir / ARMS_FILE
    arms.write_bytes(png_bytes(render_arms(ENGELBERG)))
    banner = out_dir / BANNER_FILE
    banner.write_bytes(png_bytes(render_banner(ENGELBERG, seed)))
    return [arms, banner]


def write_drafts(out_dir: Path, seed: int = DEFAULT_SEED) -> list[Path]:
    """Every design of ``DRAFTS`` as ``arms_<name>.png`` and ``banner_<name>.png`` (review only)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for arms in DRAFTS:
        for kind, img in (("arms", render_arms(arms)), ("banner", render_banner(arms, seed))):
            path = out_dir / f"{kind}_{arms.name}.png"
            path.write_bytes(png_bytes(img))
            paths.append(path)
    return paths
