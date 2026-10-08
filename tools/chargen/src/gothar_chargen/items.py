"""Weapons and hand items (F6): own geometry from code, written as glTF without Blender.

Contract (characters-pipeline.md §3.1 and "Gegenstände"): ``assets/source/items/<id>.glb``, a static
mesh without skin, nodes ``<id>_lod0..2``; the origin is the grip point (middle of the fist), item
+Y = grip axis out of the fist towards blade or tip, item +Z = edge or front side. The engine puts
the origin on the socket and takes over its full rotation (item +Y on socket +Y, +Z on +Z).

Shapes are lofts: cross-sections along a path (blades, grips, bow limbs, rings) and lathes (fruit,
bottle). LOD levels use fewer segments around. Textures (``items/textures/``): tiling ambientCG
sources resized in Blender, procedural ones (apple, bread, glass, cork, forged iron) from numpy.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from numpy.typing import ArrayLike

from gothar_chargen.fabrics import fractal, value_noise
from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.validate import Report

GENERATOR = "gothar-chargen build-items"
LOD_SEGMENTS = (1.0, 0.5, 0.25)  # share of the ring segments per LOD level
TRIANGLES_MAX = 1500


@dataclass
class Mesh:
    material: str
    positions: list[np.ndarray] = field(default_factory=list)
    uvs: list[np.ndarray] = field(default_factory=list)
    triangles: list[np.ndarray] = field(default_factory=list)

    def add(self, pos: np.ndarray, uv: np.ndarray, tris: np.ndarray) -> None:
        base = sum(len(p) for p in self.positions)
        self.positions.append(pos)
        self.uvs.append(uv)
        self.triangles.append(tris + base)

    def arrays(self) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        return (
            np.concatenate(self.positions).astype(np.float64),
            np.concatenate(self.uvs).astype(np.float64),
            np.concatenate(self.triangles).astype(np.int64),
        )


def _frames(path: np.ndarray, ref: np.ndarray, closed: bool) -> tuple[np.ndarray, np.ndarray]:
    """Per path point the axes (A, B) of the cross-section plane: A = T x ref, B = A x T."""
    n = len(path)
    if closed:
        tangents = np.roll(path, -1, axis=0) - np.roll(path, 1, axis=0)
    else:
        tangents = np.gradient(path, axis=0)
    length = np.linalg.norm(tangents, axis=1)
    for i in range(n):  # flat steps (two points at the same place): the neighbour's direction
        if length[i] < 1e-12:
            j = next(
                (j for j in [*range(i - 1, -1, -1), *range(i + 1, n)] if length[j] >= 1e-12), None
            )
            tangents[i] = tangents[j] if j is not None else (0.0, 1.0, 0.0)
            length[i] = np.linalg.norm(tangents[i])
    tangents /= length[:, None]
    a = np.cross(tangents, np.tile(ref, (n, 1)))
    weak = np.linalg.norm(a, axis=1) < 1e-6
    a[weak] = np.cross(tangents[weak], np.array([1.0, 0.0, 0.0]))
    a /= np.linalg.norm(a, axis=1, keepdims=True)
    b = np.cross(a, tangents)
    return a, b


def loft(
    mesh: Mesh,
    path: np.ndarray,
    sections: list[np.ndarray],
    ref: tuple[float, float, float] = (0.0, 0.0, 1.0),
    tile: float = 0.1,
    closed_path: bool = False,
    caps: bool = True,
    fit_uv: bool = False,
) -> None:
    """Cross-sections (k, 2) in the (A, B) plane at every path point; a closed ring per section.

    UVs in metres / `tile` (tiling textures), or 0..1 around and along with `fit_uv`.
    """
    path = np.asarray(path, dtype=np.float64)
    a, b = _frames(path, np.asarray(ref, dtype=np.float64), closed_path)
    k = len(sections[0])
    rings = np.array(
        [path[i] + s[:, :1] * a[i] + s[:, 1:] * b[i] for i, s in enumerate(sections)]
    )  # (n, k, 3)
    if closed_path:
        rings = np.concatenate([rings, rings[:1]])
    rows = np.concatenate([rings, rings[:, :1]], axis=1)  # seam: repeat the first point
    n = len(rows)
    around = np.concatenate(
        [np.zeros((n, 1)), np.cumsum(np.linalg.norm(np.diff(rows, axis=1), axis=2), axis=1)],
        axis=1,
    )
    centres = rows.mean(axis=1)
    along = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(centres, axis=0), axis=1))])
    if fit_uv:
        u = around / np.maximum(around[:, -1:], 1e-9)
        v = np.broadcast_to((along / max(along[-1], 1e-9))[:, None], u.shape)
    else:
        u = around / tile
        v = np.broadcast_to((along / tile)[:, None], u.shape)
    pos = rows.reshape(-1, 3)
    uv = np.stack([u.ravel(), np.asarray(v).ravel()], axis=1)
    tris = []
    w = k + 1
    for i in range(n - 1):
        for j in range(k):
            p0, p1 = i * w + j, i * w + j + 1
            p2, p3 = (i + 1) * w + j, (i + 1) * w + j + 1
            tris += [(p0, p2, p1), (p1, p2, p3)]
    mesh.add(pos, uv, np.array(tris))
    if caps and not closed_path:
        for i, flip in ((0, True), (len(sections) - 1, False)):
            ring = rings[i]
            if np.ptp(ring, axis=0).max() < 1e-5:
                continue  # closed to a point
            centre = ring.mean(axis=0)
            cap = np.vstack([ring, centre])
            local = np.stack([sections[i][:, 0], sections[i][:, 1]], axis=1)
            cuv = (
                np.vstack([local / tile, local.mean(axis=0) / tile])
                if not fit_uv
                else (np.vstack([local, local.mean(axis=0)]) * 0 + 0.5)
            )
            fan = [(j, k, (j + 1) % k) if not flip else (j, (j + 1) % k, k) for j in range(k)]
            mesh.add(cap, cuv, np.array(fan))


def circle(r: float, seg: int, rx: float = 1.0, ry: float = 1.0, phase: float = 0.0) -> np.ndarray:
    t = np.linspace(0, 2 * np.pi, seg, endpoint=False) + phase
    return np.stack([r * rx * np.cos(t), r * ry * np.sin(t)], axis=1)


def lathe(
    mesh: Mesh,
    profile: list[tuple[float, float]],
    seg: int,
    tile: float = 0.1,
    fit_uv: bool = False,
    squash: float = 1.0,
) -> None:
    """Surface of revolution around +Y; profile = [(radius, y), ...] bottom to top."""
    path = np.array([[0.0, y, 0.0] for _, y in profile])
    sections = [circle(max(r, 1e-6), seg, ry=squash) for r, _ in profile]
    loft(mesh, path, sections, tile=tile, fit_uv=fit_uv, caps=True)


# --- items ----------------------------------------------------------------------------------------


def _blade(
    mesh: Mesh,
    start: float,
    end: float,
    width: float,
    thick: float,
    seg_len: int,
    rng: np.random.Generator,
    nicks: int,
    uneven: float,
) -> None:
    ys = np.linspace(start, end, seg_len)
    t = (ys - start) / (end - start)
    widths = np.where(t < 0.82, width * (1 - 0.15 * t), width * 0.88 * (1 - (t - 0.82) / 0.18))
    widths = np.maximum(widths, 0.0012) * (1 + uneven * (rng.random(len(ys)) - 0.5))
    upper = widths.copy()
    spots = np.arange(2, len(ys) - 3)
    for i in rng.choice(spots, size=min(nicks, len(spots)), replace=False):
        upper[i] *= 0.78  # a nick in the edge (one station, short)
    thicks = np.maximum(thick * (1 - 0.6 * t), 0.0008)
    sections = [
        np.array([[0.0, upper[i]], [thicks[i], 0.0], [0.0, -widths[i]], [-thicks[i], 0.0]])
        for i in range(len(ys))
    ]
    path = np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1)
    loft(mesh, path, sections, tile=0.25)


def sword(seg: int, rust: bool) -> list[Mesh]:
    rng = np.random.default_rng(11 if rust else 23)
    metal = Mesh("metal_rust" if rust else "iron_forged")
    grip = Mesh("leather")
    loft(
        grip,
        np.array([[0, y, 0] for y in np.linspace(-0.09, 0.09, 6)]),
        [circle(0.016, seg)] * 6,
        tile=0.08,
    )
    pommel = [(0.0, -0.135), (0.018, -0.13), (0.026, -0.115), (0.022, -0.1), (0.014, -0.09)]
    if not rust:  # crude: a simple disc
        pommel = [(0.0, -0.112), (0.024, -0.11), (0.024, -0.09), (0.0, -0.088)]
    lathe(metal, pommel, seg, tile=0.1)
    guard_w = 0.1 if rust else 0.085
    loft(
        metal,
        # the cross-guard lies in the plane of the edges (along Z), across the flat of the blade
        np.array([[0, 0.098, z] for z in np.linspace(-guard_w, guard_w, 7)]),
        [np.array([[-0.009, -0.012], [-0.009, 0.012], [0.009, 0.012], [0.009, -0.012]])] * 7,
        tile=0.1,
    )
    _blade(
        metal,
        0.107,
        0.9,
        width=0.026 if rust else 0.029,
        thick=0.0035 if rust else 0.005,
        seg_len=16 + 2 * seg,
        rng=rng,
        nicks=4 if rust else 0,
        uneven=0.04 if rust else 0.12,
    )
    return [metal, grip]


def sword_2h(seg: int) -> list[Mesh]:
    """Two-handed sword: the origin where the right hand holds it (top of the long grip, under the
    cross-guard), the left hand 10 cm below (two_hands); grip, wide guard, long straight blade."""
    rng = np.random.default_rng(37)
    metal = Mesh("iron_forged")
    grip = Mesh("leather")
    loft(grip, np.array([[0, y, 0] for y in np.linspace(-0.27, 0.05, 8)]),
         [circle(0.017, seg)] * 8, tile=0.08)  # fmt: skip
    pommel = [(0.0, -0.33), (0.02, -0.325), (0.03, -0.305), (0.026, -0.285), (0.016, -0.27)]
    lathe(metal, pommel, seg, tile=0.1)
    loft(  # cross-guard in the plane of the edges, the ends bent slightly towards the blade
        metal,
        np.array([[0, 0.06 + 0.012 * (z / 0.15) ** 2, z] for z in np.linspace(-0.15, 0.15, 9)]),
        [np.array([[-0.01, -0.013], [-0.01, 0.013], [0.01, 0.013], [0.01, -0.013]])] * 9,
        tile=0.1,
    )
    _blade(metal, 0.07, 1.1, width=0.034, thick=0.006, seg_len=18 + 2 * seg, rng=rng,
           nicks=0, uneven=0.08)  # fmt: skip
    return [metal, grip]


RUNE_SIZE = (0.04, 0.062, 0.014)  # x (width), y (length, the long axis), z (thickness)


def rune(seg: int, spell: str) -> list[Mesh]:
    """Rune stone (M12): a flat stone with rounded ends, origin in the middle, +Y the long axis,
    +Z the face with the carved sign. One geometry for all runes; the face has its own material
    `rune_<spell>` (procedural: stone with the sign in the colour of the spell)."""
    w, length, th = RUNE_SIZE
    stone = Mesh("stone")
    ys = np.linspace(-length / 2, length / 2, 7)
    shape = np.sqrt(np.clip(1 - (2 * ys / length) ** 6, 0.05, 1))  # flat ends, rounded corners
    sections = [circle(1.0, max(8, seg), rx=w / 2 * s, ry=th / 2) for s in shape]
    loft(stone, np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1), sections, tile=0.05)
    face = Mesh(f"rune_{spell}")  # a patch lying on the curved face of the stone
    fw, fl, cols = w * 0.36, length * 0.38, 7
    xs = np.linspace(-fw, fw, cols)
    zs = th / 2 * np.sqrt(np.clip(1 - (2 * xs / w) ** 2, 0, 1)) + 0.0004
    pos = np.array([[x, y, z] for y in (-fl, fl) for x, z in zip(xs, zs, strict=True)])
    uv = np.array([[(x + fw) / (2 * fw), 1.0 - (y + fl) / (2 * fl)] for y in (-fl, fl) for x in xs])
    tris = [[c, c + 1, cols + c + 1] for c in range(cols - 1)]
    tris += [[c, cols + c + 1, cols + c] for c in range(cols - 1)]
    face.add(pos, uv, np.array(tris))
    return [stone, face]


def scroll(seg: int) -> list[Mesh]:
    """Spell scroll (M12): a rolled parchment along +Y (origin in the middle), a cord around it
    and a red wax seal on its +Z side."""
    paper = Mesh("parchment")
    r, half = 0.017, 0.1
    profile = [(0.0, -half), (r * 0.6, -half), (r, -half + 0.002), (r, half - 0.002),
               (r * 0.6, half), (0.0, half)]  # fmt: skip
    lathe(paper, profile, seg, tile=0.1)
    cord = Mesh("leather")
    loft(cord, np.array([[0.0, y, 0.0] for y in (-0.004, 0.004)]),
         [circle(r + 0.0015, seg)] * 2, tile=0.05)  # fmt: skip
    seal = Mesh("wax_red")
    loft(seal, np.array([[0.0, 0.0, r + 0.0005], [0.0, 0.0, r + 0.004]]),
         [circle(0.009, max(8, seg))] * 2, ref=(0.0, 1.0, 0.0), tile=0.02)  # fmt: skip
    return [paper, cord, seal]


def club(seg: int) -> list[Mesh]:
    rng = np.random.default_rng(5)
    ys = np.linspace(-0.12, 0.62, 18)
    t = (ys + 0.12) / 0.74
    radius = 0.017 + 0.03 * t**1.5
    radius[-1] *= 0.75  # rounded end
    angles = np.linspace(0, 2 * np.pi, seg, endpoint=False)
    knobs = np.ones((len(ys), seg))
    for _ in range(6):  # knots of cut-off twigs on the head
        kt, ka = rng.uniform(0.35, 0.92), rng.uniform(0, 2 * np.pi)
        da = np.angle(np.exp(1j * (angles - ka)))
        knobs += 0.35 * np.exp(-((t[:, None] - kt) ** 2) / 0.003 - da[None, :] ** 2 / 0.25)
    sections = [circle(r, seg) * knobs[i, :, None] for i, r in enumerate(radius)]
    path = np.stack([0.018 * np.sin(np.pi * t), ys, 0.008 * np.sin(2 * np.pi * t)], axis=1)
    wood = Mesh("bark")
    loft(wood, path, sections, tile=0.3)
    return [wood]


def bow(seg: int) -> list[Mesh]:
    ys = np.linspace(-0.6, 0.6, 21)
    # The string side is item +X: on socket_hand_l, +X runs along the forearm to the archer
    # (+Y = grip axis, +Z = up in the T-pose), so the tips bend towards +X.
    xs = 0.1 * (ys / 0.6) ** 2
    path = np.stack([xs, ys, np.zeros_like(ys)], axis=1)
    taper = 1 - 0.55 * np.abs(ys) / 0.6
    sections = [circle(1.0, max(6, seg)) * np.array([0.016 * f, 0.026 * f]) for f in taper]
    wood = Mesh("wood")
    loft(wood, path, sections, tile=0.3)
    wrap = Mesh("leather")
    gy = np.linspace(-0.07, 0.07, 5)
    loft(
        wrap,
        np.stack([0.1 * (gy / 0.6) ** 2, gy, np.zeros_like(gy)], axis=1),
        [circle(1.0, max(6, seg)) * np.array([0.019, 0.03])] * 5,
        tile=0.08,
    )
    loft(
        wrap,
        np.array([[0.1, -0.6, 0], [0.1, 0.6, 0]]),
        [circle(0.0016, 4)] * 2,
        tile=0.05,
    )  # the string
    return [wood, wrap]


def apple(seg: int) -> list[Mesh]:
    profile = [  # (radius, y): calyx dimple at the bottom, wider shoulders, stem cavity on top
        (0.0, -0.029), (0.009, -0.033), (0.022, -0.035), (0.033, -0.029), (0.04, -0.017),
        (0.043, -0.002), (0.042, 0.013), (0.038, 0.025), (0.029, 0.034), (0.017, 0.036),
        (0.007, 0.031), (0.0, 0.027),
    ]  # fmt: skip
    lift = 0.035  # held between thumb and fingers, above the fist (bites reach the mouth)
    skin = Mesh("apple")
    lathe(skin, [(r, y + lift) for r, y in profile], seg, fit_uv=True)
    stem = Mesh("wood")
    stem_path = np.array([[0, 0.026 + lift, 0], [0.002, 0.044 + lift, 0.003]])
    loft(stem, stem_path, [circle(0.0018, 5)] * 2)
    return [skin, stem]


def bread(seg: int) -> list[Mesh]:
    theta = np.linspace(-np.pi / 2, np.pi / 2, 13)  # denser stations at the rounded ends
    ys = 0.09 * np.sin(theta) + 0.04  # held at one end, the other end to the mouth
    r = 0.068 * np.cos(theta) ** 0.5  # a round loaf
    crust = Mesh("bread")
    lathe(crust, list(zip(r, ys, strict=True)), seg, fit_uv=True, squash=0.62)
    return [crust]


def potion(seg: int, glass_material: str = "glass_red", scale: float = 1.0) -> list[Mesh]:
    """Small bottle held at its body (the origin); `scale` for the medium potions."""
    glass = Mesh(glass_material)
    profile = [
        (0.0, -0.06), (0.03, -0.058), (0.035, -0.05), (0.035, 0.02), (0.03, 0.036),
        (0.014, 0.05), (0.011, 0.056), (0.011, 0.082), (0.014, 0.085), (0.014, 0.09),
        (0.0, 0.09),
    ]  # fmt: skip
    lathe(glass, [(r * scale, y * scale) for r, y in profile], seg, tile=0.1)
    cork = Mesh("cork")
    plug = [(0.0, 0.088), (0.0105, 0.088), (0.0115, 0.108), (0.0, 0.11)]
    lathe(cork, [(r * scale, y * scale) for r, y in plug], seg, tile=0.05)
    return [glass, cork]


def lockpick(seg: int) -> list[Mesh]:
    metal = Mesh("iron_forged")
    rect = np.array([[-0.0007, -0.0016], [-0.0007, 0.0016], [0.0007, 0.0016], [0.0007, -0.0016]])
    path = np.array([[0, -0.03, 0], [0, 0.1, 0], [0, 0.112, 0.003], [0, 0.118, 0.011]])
    loft(metal, path, [rect * [1, 2.2], rect, rect, rect], tile=0.05)
    loft(  # a bent ring as handle
        metal,
        np.array([[0, -0.03 - 0.009 + 0.009 * np.cos(a), 0.009 * np.sin(a)]
                  for a in np.linspace(0, 2 * np.pi, 12, endpoint=False)]),
        [circle(0.0012, max(4, seg // 2))] * 12,
        ref=(1.0, 0.0, 0.0),
        tile=0.05,
        closed_path=True,
    )  # fmt: skip
    return [metal]


def key(seg: int) -> list[Mesh]:
    metal = Mesh("iron_forged")
    ring = [
        [0.0, 0.014 * np.cos(a), 0.014 * np.sin(a)]
        for a in np.linspace(0, 2 * np.pi, 16, endpoint=False)
    ]
    loft(metal, np.array(ring), [circle(0.0028, max(4, seg // 2))] * 16,
         ref=(1.0, 0.0, 0.0), tile=0.05, closed_path=True)  # fmt: skip
    loft(metal, np.array([[0, 0.014, 0], [0, 0.085, 0]]), [circle(0.0034, max(5, seg // 2))] * 2,
         tile=0.05)  # fmt: skip
    rect = np.array([[-0.006, -0.0016], [-0.006, 0.0016], [0.006, 0.0016], [0.006, -0.0016]])
    loft(metal, np.array([[0, 0.072, 0.002], [0, 0.072, 0.019]]), [rect, rect],
         ref=(1.0, 0.0, 0.0), tile=0.05)  # fmt: skip
    return [metal]


def broom(seg: int) -> list[Mesh]:
    """Sweeping broom: a straight handle (held at its upper end, the origin) and a bundle of twigs
    along +Y, bound with a cord."""
    rng = np.random.default_rng(31)
    handle = Mesh("wood")
    loft(handle, np.array([[0, -0.2, 0], [0, 0.82, 0]]), [circle(0.016, max(6, seg // 2))] * 2,
         tile=0.3)  # fmt: skip
    twigs = Mesh("straw")
    ys = np.linspace(0.78, 1.2, 8)
    t = (ys - 0.78) / 0.42
    radius = 0.03 + 0.085 * t**0.7
    sections = []
    for i, r in enumerate(radius):
        shaggy = 1 + (0.25 * t[i]) * (rng.random(seg) - 0.5)  # loose twig ends
        sections.append(circle(r, seg, ry=0.55) * shaggy[:, None])  # flat bundle: wide along Z
    sections[-1] = sections[-1] * 1.05
    loft(twigs, np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1), sections, tile=0.25)
    cord = Mesh("cork")
    loft(cord, np.array([[0, 0.8, 0], [0, 0.84, 0]]), [circle(0.036, seg, ry=0.6)] * 2, tile=0.05)
    return [handle, twigs, cord]


def mug(seg: int) -> list[Mesh]:
    """Wooden mug held by its handle (the origin): +Y up, the cup on the side of +Z."""
    cup = Mesh("wood_dark")
    body = [(0.0, -0.05), (0.04, -0.05), (0.042, -0.04), (0.042, 0.06), (0.046, 0.065),
            (0.039, 0.065), (0.037, 0.06), (0.037, -0.035), (0.0, -0.035)]  # fmt: skip
    path = np.array([[0.0, y, 0.07] for _, y in body])
    loft(cup, path, [circle(max(r, 1e-6), seg) for r, _ in body], tile=0.1)
    handle = [
        [0.0, 0.015 + 0.035 * np.sin(a), 0.07 - 0.042 - 0.028 * np.cos(a)]
        for a in np.linspace(-np.pi / 2, np.pi / 2, 7)
    ]
    loft(cup, np.array(handle), [circle(1.0, 6) * np.array([0.006, 0.011])] * 7,
         ref=(1.0, 0.0, 0.0), tile=0.05)  # fmt: skip
    return [cup]


def axe(seg: int) -> list[Mesh]:
    """Woodcutter's axe: a handle along +Y (held near its lower end, the origin) and an iron head
    at the top whose edge points to +Z (contract: item +Z = edge)."""
    handle = Mesh("wood")
    ys = np.linspace(-0.12, 0.62, 6)
    radius = 0.017 - 0.002 * (ys + 0.12) / 0.74
    loft(handle, np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1),
         [circle(r, max(6, seg // 2), ry=1.25) for r in radius], tile=0.3)  # fmt: skip
    head = Mesh("iron_forged")
    zs = np.array([-0.035, -0.015, 0.02, 0.06, 0.1, 0.125])
    thick = np.array([0.016, 0.02, 0.018, 0.01, 0.004, 0.0015])  # along X
    height = np.array([0.04, 0.05, 0.05, 0.075, 0.11, 0.13])  # along Y, flared towards the edge
    path = np.stack([np.zeros_like(zs), np.full_like(zs, 0.57), zs], axis=1)
    sections = [
        np.array([[-th / 2, -h / 2], [th / 2, -h / 2], [th / 2, h / 2], [-th / 2, h / 2]])
        for th, h in zip(thick, height, strict=True)
    ]
    loft(head, path, sections, ref=(0.0, 1.0, 0.0), tile=0.1)
    return [handle, head]


TORCH_FLAME = (0.0, 0.6, 0.0)  # flame: top of the pitch head (marker socket_flame, +Y = item +Y)


def torch(seg: int) -> list[Mesh]:
    """Torch: a slightly crooked wooden stick held near its lower end (the origin) and a head of
    pitch-soaked rags wound round its top, bound with a cord; the flame sits above (TORCH_FLAME)."""
    rng = np.random.default_rng(41)
    stick = Mesh("wood_dark")
    ys = np.linspace(-0.13, 0.47, 7)
    t = (ys + 0.13) / 0.6
    path = np.stack([0.008 * np.sin(np.pi * t), ys, 0.005 * np.sin(2 * np.pi * t)], axis=1)
    loft(stick, path, [circle(0.015 + 0.004 * ti, max(6, seg // 2)) for ti in t], tile=0.3)
    head = Mesh("pitch")
    ys = np.linspace(0.33, 0.585, 16)
    t = (ys - 0.33) / 0.255
    radius = 0.021 + 0.017 * np.sin(np.pi * np.clip(t * 1.15, 0, 1)) ** 0.6
    radius[-1] = 0.006  # charred, rounded top
    radius[-2] *= 0.8
    wraps = 1 + 0.07 * np.sin(t * 2 * np.pi * 5.5)  # the rag wound in turns
    sections = []
    for i, r in enumerate(radius):
        lumpy = 1 + 0.08 * (rng.random(seg) - 0.5) * (0.4 + t[i])
        sections.append(circle(r * wraps[i], seg) * lumpy[:, None])
    loft(head, np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1), sections, tile=0.08)
    cord = Mesh("cork")
    for y in (0.34, 0.355):
        loft(cord, np.array([[0, y, 0], [0, y + 0.009, 0]]), [circle(0.0235, seg)] * 2, tile=0.05)
    return [stick, head, cord]


LUTE_GRIP = (0.0, 0.17, 0.012)  # origin: the right hand over the strings near the bridge


def _lute_section(w: float, d: float, n: int) -> np.ndarray:
    """Bowl section (x, z): the back half of an ellipse from -x through -z to +x, closed by the
    flat soundboard at z = 0 (the loft joins the last point to the first)."""
    a = np.linspace(np.pi, 0.0, n)
    return np.stack([w * np.cos(a), -d * np.sin(a)], axis=1)


def lute(seg: int) -> list[Mesh]:
    """Lute held at the soundboard where the right hand plucks (the origin, LUTE_GRIP): a
    pear-shaped bowl of staves (back, -Z) with a flat soundboard (+Z) and a dark rosette, the neck
    along +Y and the pegbox bent back; pale gut strings over the board."""
    o = np.array(LUTE_GRIP)
    n = max(7, seg // 2 + 1)
    ys = np.array([0.0, 0.015, 0.05, 0.1, 0.17, 0.24, 0.31, 0.37, 0.42, 0.45])
    ws = np.array([0.02, 0.07, 0.115, 0.148, 0.16, 0.155, 0.13, 0.095, 0.055, 0.032])
    bowl = Mesh("wood_dark")
    path = np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1) - o
    loft(bowl, path, [_lute_section(w, 0.85 * w, n) for w in ws], tile=0.12)
    board = Mesh("wood")  # the soundboard a little proud of the bowl's rim
    loft(board, path[1:-1] + np.array([0.0, 0.0, 0.002]),
         [np.array([[-w * 0.97, -0.001], [w * 0.97, -0.001], [w * 0.97, 0.001], [-w * 0.97, 0.001]])
          for w in ws[1:-1]], tile=0.2)  # fmt: skip
    rose = Mesh("wood_dark")
    loft(
        rose,
        np.array([[0.0, 0.26, 0.0032], [0.0, 0.26, 0.0036]]) - o,
        [circle(0.035, seg, ry=1.0)] * 2,
        ref=(0.0, 1.0, 0.0),
        tile=0.05,
    )
    neck = Mesh("wood_dark")
    ny = np.array([0.44, 0.75])
    loft(
        neck,
        np.stack([np.zeros(2), ny, np.zeros(2)], axis=1) - o,
        [np.array([[-w, -0.024], [w, -0.024], [w, 0.004], [-w, 0.004]]) for w in (0.03, 0.022)],
        tile=0.1,
    )
    peg = np.array([[0.0, 0.745, 0.0], [0.0, 0.785, -0.03], [0.0, 0.81, -0.08]]) - o
    loft(neck, peg, [np.array([[-w, -0.012], [w, -0.012], [w, 0.012], [-w, 0.012]]) for w in
                     (0.022, 0.02, 0.016)], ref=(1.0, 0.0, 0.0), tile=0.1)  # fmt: skip
    strings = Mesh("parchment")
    for x in np.linspace(-0.014, 0.014, 6):
        sx = x * np.array([2.2, 1.0])  # wider at the bridge
        path_s = np.array([[sx[0], 0.1, 0.008], [sx[1], 0.745, 0.008]]) - o
        loft(strings, path_s, [circle(0.0008, 3)] * 2, ref=(0.0, 0.0, 1.0), tile=0.5, caps=False)
    bridge = Mesh("wood_dark")
    loft(
        bridge,
        np.array([[-0.045, 0.1, 0.004], [0.045, 0.1, 0.004]]) - o,
        [np.array([[-0.004, -0.002], [0.004, -0.002], [0.004, 0.004], [-0.004, 0.004]])] * 2,
        ref=(0.0, 0.0, 1.0),
        tile=0.05,
    )
    return [bowl, board, rose, neck, strings, bridge]


# empty marker nodes per item (name -> position in item space), e.g. where engine puts the flame
MARKERS: dict[str, dict[str, tuple[float, float, float]]] = {
    "it_torch": {"socket_flame": TORCH_FLAME},  # engine: flame and light, +Y up out of the head
}


# --- goods for Leonberg's traders (2026-10-08) ----------------------------------------------------


def ham(seg: int) -> list[Mesh]:
    """Leg of ham held at its bone end (the origin), the thick end along +Y (bites reach the mouth
    as with bread); the shank bone sticks out below the grip."""
    meat = Mesh("ham")
    ys = np.array([0.0, 0.02, 0.06, 0.11, 0.16, 0.2, 0.235, 0.26, 0.275, 0.28])
    rs = np.array([0.022, 0.03, 0.045, 0.06, 0.068, 0.068, 0.06, 0.045, 0.025, 0.0])
    lathe(meat, list(zip(rs, ys, strict=True)), seg, fit_uv=True, squash=0.78)
    bone = Mesh("bone")
    lathe(bone, [(0.0, -0.06), (0.014, -0.058), (0.012, -0.045), (0.01, 0.01), (0.0, 0.012)],
          max(6, seg // 2), tile=0.05)  # fmt: skip
    return [meat, bone]


def sausage(seg: int) -> list[Mesh]:
    """A bent sausage tied at both ends, held near one end (the origin), the other end up (+Y)."""
    skin = Mesh("sausage")
    a = np.linspace(0.0, 1.0, 13)
    path = np.stack([0.045 * np.sin(np.pi * a), -0.02 + 0.23 * a, np.zeros_like(a)], axis=1)
    r = 0.017 * np.clip(np.sin(np.pi * a) * 3.0, 0.25, 1.0)  # pinched where it is tied
    loft(skin, path, [circle(ri, max(6, seg // 2)) for ri in r], ref=(0.0, 0.0, 1.0), fit_uv=True)
    string = Mesh("cork")
    for y in (-0.016, 0.206):
        x = 0.004 if y > 0 else 0.0
        ends = np.array([[x, y, 0.0], [x, y + 0.006, 0.0]])
        loft(string, ends, [circle(0.006, 6)] * 2, tile=0.05)
    return [skin, string]


def cheese(seg: int) -> list[Mesh]:
    """Wheel of cheese standing on its flat side, the origin in the middle of the bottom (+Y up)."""
    rind = Mesh("cheese")
    profile = [(0.0, 0.0), (0.095, 0.0), (0.108, 0.01), (0.112, 0.04), (0.108, 0.07), (0.095, 0.08),
               (0.0, 0.08)]  # fmt: skip
    lathe(rind, profile, seg, fit_uv=True)
    return [rind]


PRETZEL = [  # (x, y, z) of the dough strand, the knot crosses at (0, 0.08) with a little z
    (-0.032, 0.045, 0.0), (-0.012, 0.065, 0.006), (0.0, 0.08, 0.008), (0.03, 0.11, 0.004),
    (0.062, 0.112, 0.0), (0.085, 0.085, 0.0), (0.085, 0.045, 0.0), (0.06, 0.012, 0.0),
    (0.0, 0.0, 0.0), (-0.06, 0.012, 0.0), (-0.085, 0.045, 0.0), (-0.085, 0.085, 0.0),
    (-0.062, 0.112, 0.0), (-0.03, 0.11, -0.004), (0.0, 0.08, -0.008), (0.012, 0.065, -0.006),
    (0.032, 0.045, 0.0),
]  # fmt: skip


def pretzel(seg: int) -> list[Mesh]:
    """Pretzel held at the middle of its thick bottom arc (the origin), the knot up (+Y)."""
    crust = Mesh("bread")
    pts = np.array(PRETZEL)
    fine = []  # Catmull-Rom through the points, 3 steps per span
    for i in range(len(pts) - 1):
        p0, p1 = pts[max(i - 1, 0)], pts[i]
        p2, p3 = pts[i + 1], pts[min(i + 2, len(pts) - 1)]
        for s in np.linspace(0.0, 1.0, 3, endpoint=False):
            fine.append(0.5 * ((2 * p1) + (-p0 + p2) * s + (2 * p0 - 5 * p1 + 4 * p2 - p3) * s**2
                               + (-p0 + 3 * p1 - 3 * p2 + p3) * s**3))  # fmt: skip
    fine.append(pts[-1])
    path = np.array(fine)
    low = np.clip(1.0 - path[:, 1] / 0.08, 0.0, 1.0)  # thick at the bottom, thin arms
    sections = [circle(0.008 + 0.007 * lo, max(6, seg // 2)) * np.array([1.0, 0.85]) for lo in low]
    loft(crust, path, sections, ref=(0.0, 0.0, 1.0), fit_uv=True)
    return [crust]


def beer(seg: int) -> list[Mesh]:
    """Beer in a stoneware bottle with a wooden stopper, held at its body (the origin), +Y up."""
    jug = Mesh("clay")
    profile = [(0.0, -0.11), (0.045, -0.108), (0.055, -0.08), (0.058, -0.02), (0.05, 0.04),
               (0.03, 0.07), (0.017, 0.085), (0.016, 0.11), (0.019, 0.115),
               (0.0, 0.115)]  # fmt: skip
    lathe(jug, profile, seg, tile=0.1)
    stopper = Mesh("wood_dark")
    lathe(stopper, [(0.0, 0.112), (0.014, 0.112), (0.015, 0.135), (0.0, 0.138)], max(6, seg // 2),
          tile=0.05)  # fmt: skip
    return [jug, stopper]


def wine(seg: int) -> list[Mesh]:
    """Wine in a green glass bottle with a cork, held at its body (the origin), +Y up."""
    glass = Mesh("glass_green")
    profile = [(0.0, -0.1), (0.036, -0.098), (0.038, 0.06), (0.03, 0.09), (0.013, 0.13),
               (0.012, 0.185), (0.015, 0.19), (0.0, 0.19)]  # fmt: skip
    lathe(glass, profile, seg, tile=0.1)
    cork = Mesh("cork")
    lathe(cork, [(0.0, 0.188), (0.011, 0.188), (0.0115, 0.205), (0.0, 0.207)], max(6, seg // 2),
          tile=0.05)  # fmt: skip
    return [glass, cork]


def cloth_bolt(seg: int) -> list[Mesh]:
    """Bolt of cloth: a roll of undyed linen standing on one end, the origin in the middle of
    the bottom (+Y up); the outer turn ends in a slightly raised edge."""
    cloth = Mesh("linen")
    r = 0.07
    profile = [(0.0, 0.0), (r, 0.0), (r + 0.002, 0.25), (r, 0.5), (0.0, 0.5)]
    lathe(cloth, profile, seg, tile=0.12)
    edge = Mesh("linen")
    a = np.linspace(0.0, 0.5, 2)
    loft(edge, np.stack([np.full(2, r + 0.0015), a, np.zeros(2)], axis=1),
         [np.array([[-0.002, -0.004], [0.002, -0.004], [0.002, 0.004], [-0.002, 0.004]])] * 2,
         ref=(1.0, 0.0, 0.0), tile=0.12)  # fmt: skip
    return [cloth, edge]


def jug(seg: int) -> list[Mesh]:
    """Clay jug with a handle, held by its handle (the origin) like it_mug; +Y up, the jug on +Z."""
    body = Mesh("clay")
    profile = [(0.0, -0.1), (0.05, -0.1), (0.062, -0.06), (0.066, 0.0), (0.055, 0.06), (0.04, 0.09),
               (0.042, 0.13), (0.046, 0.135), (0.038, 0.135), (0.035, 0.1), (0.0, 0.1)]  # fmt: skip
    path = np.array([[0.0, y, 0.085] for _, y in profile])
    loft(body, path, [circle(max(r, 1e-6), seg) for r, _ in profile], tile=0.1)
    handle = [
        [0.0, 0.03 + 0.07 * np.sin(a), 0.085 - 0.062 - 0.03 * np.cos(a)]
        for a in np.linspace(-np.pi / 2, np.pi / 2, 7)
    ]
    loft(body, np.array(handle), [circle(1.0, 6) * np.array([0.007, 0.013])] * 7,
         ref=(1.0, 0.0, 0.0), tile=0.05)  # fmt: skip
    return [body]


def bowl(seg: int) -> list[Mesh]:
    """Clay bowl standing on its foot, the origin in the middle of the bottom (+Y up). Outer wall,
    inner wall and rim are separate surfaces; the inner wall's sections run the other way round
    so its normals face into the bowl (a profile going out and back in faces the clay inside)."""
    clay = Mesh("clay")

    def ring(profile: list[tuple[float, float]], inside: bool = False) -> None:
        path = np.array([[0.0, y, 0.0] for _, y in profile])
        sections = [circle(max(r, 1e-6), seg)[:: -1 if inside else 1] for r, _ in profile]
        loft(clay, path, sections, tile=0.1, caps=False)

    ring([(0.0, 0.0), (0.04, 0.0), (0.045, 0.008), (0.075, 0.03), (0.09, 0.065), (0.092, 0.07)])
    ring([(0.0, 0.014), (0.04, 0.016), (0.068, 0.036), (0.084, 0.07)], inside=True)
    ring([(0.084, 0.07), (0.092, 0.07)])
    return [clay]


LEAF_GAP = 0.0002  # metres each face of a leaf stands off its middle


def _leaf(
    mesh: Mesh,
    base: ArrayLike,
    direction: ArrayLike,
    side: ArrayLike,
    length: float,
    width: float,
    n: int = 5,
) -> None:
    """A flat leaf (lens shape) from `base` along `direction`, `side` across; both faces have their
    own vertices (no alpha in items: the leaf is geometry, seen from either side)."""
    base, direction, side = (np.asarray(v, dtype=np.float64) for v in (base, direction, side))
    direction = direction / np.linalg.norm(direction)
    side = side / np.linalg.norm(side)
    ts = np.linspace(0.0, 1.0, n + 1)
    half = 0.5 * width * np.sin(np.pi * ts) ** 0.8
    centre = base + direction[None, :] * (length * ts)[:, None]
    left, right = centre + side * half[:, None], centre - side * half[:, None]
    pos = np.empty((2 * (n + 1), 3))
    pos[0::2], pos[1::2] = left, right
    uv = np.stack([np.tile([0.0, 1.0], n + 1), np.repeat(ts, 2)], axis=1)
    tris = []
    for i in range(n):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        tris += [(a, b, c), (b, d, c)]
    tris = np.array(tris)
    normal = np.cross(direction, side)
    normal /= max(float(np.linalg.norm(normal)), 1e-9)
    mesh.add(pos + normal * LEAF_GAP, uv, tris)  # the two faces apart: no z-fighting
    mesh.add(pos - normal * LEAF_GAP, uv.copy(), tris[:, ::-1].copy())


def _stem(mesh: Mesh, points: ArrayLike, radius: float, seg: int = 5) -> None:
    pts = np.asarray(points, dtype=np.float64)
    loft(mesh, pts, [circle(radius, seg)] * len(pts), ref=(0.0, 0.0, 1.0), tile=0.05)


def _root(seg: int) -> Mesh:
    root = Mesh("root")
    lathe(root, [(0.0, -0.03), (0.006, -0.025), (0.012, -0.012), (0.01, 0.0), (0.0, 0.002)],
          max(6, seg // 2), tile=0.05)  # fmt: skip
    return root


def herb(seg: int, kind: str) -> list[Mesh]:
    """A whole plant held at the stem above its root (the origin), +Y up: sage (grey-green oval
    leaves in pairs), nettle (taller, dark pointed leaves), chamomile (thin stems, white
    flowers)."""
    rng = np.random.default_rng({"sage": 5, "nettle": 7, "chamomile": 9}[kind])
    stem = Mesh("leaf_dry" if kind == "sage" else "leaf_green")
    leaves = Mesh("leaf_sage" if kind == "sage" else "leaf_green")
    meshes = [stem, leaves, _root(seg)]
    if kind == "chamomile":
        petals, hearts = Mesh("flower_white"), Mesh("flower_yellow")
        for i in range(3):
            a = 2 * np.pi * i / 3 + 0.4
            top = np.array([0.025 * np.cos(a), 0.13 + 0.02 * i, 0.025 * np.sin(a)])
            _stem(stem, [[0, 0, 0], top * np.array([0.4, 0.5, 0.4]), top], 0.0012, 4)
            for k in range(8):  # petals around the flower head, facing up
                b = 2 * np.pi * k / 8
                d = np.array([np.cos(b), 0.15, np.sin(b)])
                _leaf(petals, top, d, np.cross(d, [0, 1, 0]), 0.011, 0.004, 3)
            heart = [(0.0, -0.002), (0.0045, 0.0), (0.0035, 0.003), (0.0, 0.0045)]
            path = np.array([top + np.array([0.0, y, 0.0]) for _, y in heart])
            loft(hearts, path, [circle(max(r, 1e-6), 6) for r, _ in heart], tile=0.02)
            for y in (0.04, 0.07):  # fine leaves on the stem
                p = top * y / top[1]
                _leaf(leaves, p, [np.cos(a + 1.2), 0.6, np.sin(a + 1.2)], [0, 0, 1], 0.03, 0.004, 3)
        meshes += [petals, hearts]
        return meshes
    height = 0.15 if kind == "sage" else 0.2
    _stem(stem, [[0, 0, 0], [0.004, height * 0.5, 0.0], [0.0, height, 0.003]], 0.0025)
    pairs = 5 if kind == "sage" else 6
    for i in range(pairs):
        y = height * (0.2 + 0.75 * i / pairs)
        a = i * np.pi / 2 + rng.uniform(-0.2, 0.2)  # opposite pairs, crossed
        size = 1.0 - 0.45 * i / pairs
        for s in (1.0, -1.0):
            d = np.array([s * np.cos(a), 0.5, s * np.sin(a)])
            length = (0.05 if kind == "sage" else 0.06) * size
            width = (0.022 if kind == "sage" else 0.028) * size
            _leaf(leaves, [0.0, y, 0.0], d, np.cross(d, [0, 1, 0]) + [0, 0.001, 0], length, width)
    return meshes


def herbs_dried(seg: int) -> list[Mesh]:
    """A bundle of dried herbs tied with a cord (the origin at the cord), the heads up (+Y)."""
    rng = np.random.default_rng(13)
    stems, leaves = Mesh("straw"), Mesh("leaf_dry")
    for i in range(9):
        a = 2 * np.pi * i / 9
        x, z = 0.006 * np.cos(a), 0.006 * np.sin(a)
        spread = 0.25 + 0.15 * rng.random()
        top = np.array([x * 6 * spread / 0.25, 0.14 + 0.03 * rng.random(), z * 6 * spread / 0.25])
        _stem(stems, [[x, -0.06, z], [x, 0.0, z], top], 0.0015, 4)
        for k in range(3):
            p = top * (0.55 + 0.15 * k)
            d = np.array([np.cos(a + k), 0.4, np.sin(a + k)])
            _leaf(leaves, p, d, np.cross(d, [0, 1, 0]), 0.03, 0.01, 3)
    cord = Mesh("cork")
    loft(cord, np.array([[0, -0.006, 0], [0, 0.006, 0]]), [circle(0.012, 8)] * 2, tile=0.05)
    return [stems, leaves, cord]


def ring(seg: int, metal: str) -> list[Mesh]:
    """Finger ring standing upright (+Y), the origin in its middle."""
    band = Mesh(metal)
    a = np.linspace(0, 2 * np.pi, seg, endpoint=False)
    path = np.stack([0.0105 * np.cos(a), 0.0105 * np.sin(a), np.zeros_like(a)], axis=1)
    loft(band, path, [circle(1.0, 6) * np.array([0.0012, 0.0022])] * len(a), ref=(0.0, 0.0, 1.0),
         closed_path=True, tile=0.02)  # fmt: skip
    return [band]


def amulet(seg: int) -> list[Mesh]:
    """Amulet: a gold pendant with a set stone on a leather cord loop; held at the pendant (the
    origin), the loop up (+Y)."""
    gold, stone, cord = Mesh("gold"), Mesh("stone"), Mesh("leather")
    loft(gold, np.array([[0, 0, -0.003], [0, 0, 0.003]]), [circle(0.02, seg, ry=1.25)] * 2,
         ref=(0.0, 1.0, 0.0), tile=0.03)  # fmt: skip
    loft(stone, np.array([[0, 0, 0.0028], [0, 0, 0.0045]]), [circle(0.011, seg, ry=1.25)] * 2,
         ref=(0.0, 1.0, 0.0), tile=0.02)  # fmt: skip
    a = np.linspace(-np.pi / 2, 1.5 * np.pi, 17)[:-1]
    loop = np.stack([0.05 * np.cos(a), 0.025 + 0.11 * (1 + np.sin(a)), np.zeros_like(a)], axis=1)
    loft(
        cord,
        loop,
        [circle(0.0015, 4)] * len(loop),
        ref=(0.0, 0.0, 1.0),
        closed_path=True,
        tile=0.05,
    )
    return [gold, stone, cord]


def chain(seg: int) -> list[Mesh]:
    """Gold chain of 26 alternating links hanging as a loop, held at its top (the origin), the
    loop hanging down (-Y)."""
    gold = Mesh("gold")
    n = 26
    a = np.linspace(0, 2 * np.pi, n, endpoint=False)
    centres = np.stack([0.06 * np.sin(a), -0.17 * (1 - np.cos(a)), np.zeros_like(a)], axis=1)
    for i in range(n):
        p, q = centres[i], centres[(i + 1) % n]
        along = (q - p) / np.linalg.norm(q - p)
        across = np.array([0.0, 0.0, 1.0]) if i % 2 else np.cross(along, [0.0, 0.0, 1.0])
        across = across / np.linalg.norm(across)
        b = np.linspace(0, 2 * np.pi, 7, endpoint=False)
        mid = (p + q) / 2
        link = (
            mid
            + along[None] * (0.011 * np.cos(b))[:, None]
            + across[None] * (0.006 * np.sin(b))[:, None]
        )
        loft(gold, link, [circle(0.0011, 3)] * len(link), ref=tuple(np.cross(along, across)),
             closed_path=True, tile=0.02)  # fmt: skip
    return [gold]


def hammer(seg: int) -> list[Mesh]:
    """Smith's and joiner's hammer: handle along +Y (held near its end, the origin), the iron head
    across at the top, its striking face to +Z (contract: item +Z = edge/front)."""
    handle = Mesh("wood")
    ys = np.linspace(-0.05, 0.26, 5)
    loft(
        handle,
        np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], axis=1),
        [circle(0.012 - 0.002 * (y + 0.05) / 0.31, max(6, seg // 2), ry=1.3) for y in ys],
        tile=0.2,
    )
    head = Mesh("iron_forged")
    zs = np.array([-0.05, -0.03, 0.02, 0.05, 0.055])
    size = np.array([0.012, 0.02, 0.022, 0.024, 0.02])
    loft(head, np.stack([np.zeros_like(zs), np.full_like(zs, 0.255), zs], axis=1),
         [np.array([[-s, -s], [s, -s], [s, s], [-s, s]]) * 0.9 for s in size], ref=(0.0, 1.0, 0.0),
         tile=0.05)  # fmt: skip
    return [handle, head]


def saw(seg: int) -> list[Mesh]:
    """Hand saw: a wooden grip (the origin) and a tapering steel blade along +Y, thin along X, the
    teeth on its +Z edge (contract: item +Z = edge)."""
    grip = Mesh("wood")
    loft(grip, np.array([[0, -0.06, 0.01], [0, 0.06, 0.01]]),
         [np.array([[-0.012, -0.03], [0.012, -0.03], [0.012, 0.035], [-0.012, 0.035]])] * 2,
         ref=(0.0, 0.0, 1.0), tile=0.1)  # fmt: skip
    blade = Mesh("iron_forged")
    ys = np.linspace(0.05, 0.55, 26)
    top = 0.045 - 0.025 * (ys - 0.05) / 0.5
    teeth = np.where(np.arange(len(ys)) % 2 == 0, 0.0, -0.006)
    outline_front = np.stack(
        [np.zeros_like(ys), ys, top + 0.005 + teeth], axis=1
    )  # toothed +Z edge
    outline_back = np.stack([np.zeros_like(ys), ys, np.full_like(ys, -0.035)], axis=1)
    for x, flip in ((0.0008, False), (-0.0008, True)):
        pos = np.concatenate([outline_back, outline_front]) + np.array([x, 0.0, 0.0])
        n = len(ys)
        uv = np.stack([pos[:, 1] / 0.5, (pos[:, 2] + 0.035) / 0.09], axis=1)
        tris = []
        for i in range(n - 1):
            a, b, c, d = i, i + 1, n + i, n + i + 1
            tris += [(a, c, b), (b, c, d)] if not flip else [(a, b, c), (b, d, c)]
        blade.add(pos, uv, np.array(tris))
    return [grip, blade]


def shears(seg: int) -> list[Mesh]:
    """Spring shears (tailor's shears of the time): two blades along +Y joined by a bow-shaped
    spring at the grip end (the origin), their edges facing each other across X."""
    iron = Mesh("iron_forged")
    a = np.linspace(-np.pi, 0.0, 9)
    bow = np.stack([0.022 * np.cos(a), -0.04 + 0.03 * np.sin(a), np.zeros_like(a)], axis=1)
    loft(iron, bow, [circle(1.0, 4) * np.array([0.002, 0.006])] * len(bow), ref=(0.0, 0.0, 1.0),
         tile=0.05)  # fmt: skip
    for s in (1.0, -1.0):
        ys = np.array([-0.04, 0.0, 0.06, 0.12, 0.155])
        xs = s * np.array([0.022, 0.012, 0.006, 0.003, 0.0005])
        widths = np.array([0.006, 0.009, 0.012, 0.009, 0.003])
        path = np.stack([xs, ys, np.zeros_like(ys)], axis=1)
        blade = [np.array([[-0.0012, -w], [0.0012, -w], [0.0012, w], [-0.0012, w]]) for w in widths]
        loft(iron, path, blade,
             ref=(0.0, 0.0, 1.0), tile=0.05)  # fmt: skip
    return [iron]


def _vanes(mesh: Mesh, y0: float, y1: float, r0: float, r1: float, count: int) -> None:
    """Thin flat vanes standing off the shaft (feathers of an arrow, vanes of a bolt): one flat
    section per vane, rotated about +Y; the first lies in the item +Z plane."""
    path = np.array([[0.0, y0, 0.0], [0.0, y1, 0.0]])
    for k in range(count):
        a = 2 * np.pi * k / count
        rot = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]])
        flat = np.array([[r0, -0.0004], [r1, -0.0004], [r1, 0.0004], [r0, 0.0004]]) @ rot.T
        loft(mesh, path, [flat, flat], ref=(1.0, 0.0, 0.0), tile=0.05)


def _projectile(length: float, radius: float, head: float, vanes: int, vane_len: float,
                vane_width: float, seg: int) -> list[Mesh]:  # fmt: skip
    """Arrow or bolt: shaft along +Y, the origin in the middle (centre of mass: projectile,
    stuck in a target, held in the middle), an iron head at +Y and vanes at the back end."""
    half = length / 2
    shaft = Mesh("wood")
    loft(shaft, np.array([[0, -half, 0], [0, half - head, 0]]),
         [circle(radius, max(4, seg))] * 2, tile=0.3)  # fmt: skip
    tip = Mesh("iron_forged")
    loft(tip, np.array([[0, half - head - 0.004, 0], [0, half - head * 0.45, 0], [0, half, 0]]),
         [circle(radius * 1.2, 4), circle(radius * 2.0, 4, phase=0.0), circle(radius * 0.08, 4)],
         tile=0.05)  # fmt: skip
    feather = Mesh("feather")
    _vanes(feather, -half + 0.015, -half + 0.015 + vane_len, radius, radius + vane_width, vanes)
    return [shaft, tip, feather]


def arrow(seg: int) -> list[Mesh]:
    return _projectile(0.75, 0.004, 0.05, 3, 0.11, 0.012, seg)


def bolt(seg: int) -> list[Mesh]:
    return _projectile(0.35, 0.006, 0.045, 2, 0.07, 0.011, seg)


def crossbow(seg: int) -> list[Mesh]:
    """Crossbow: origin at the grip (right hand, trigger), the stock along +Y to the front
    (contract: the longest axis), the prod across it (item X) at the front end, the string spanned
    across, +Z up. The cbow clips hold the stock in the fist (socket +Y forward)."""
    stock = Mesh("wood")
    ys = np.array([-0.3, -0.12, -0.02, 0.1, 0.42, 0.5])
    width = np.array([0.04, 0.035, 0.03, 0.032, 0.03, 0.034])
    height = np.array([0.1, 0.06, 0.05, 0.04, 0.035, 0.04])
    zoff = np.array([-0.03, -0.01, 0.0, 0.02, 0.02, 0.02])
    path = np.stack([np.zeros_like(ys), ys, zoff], axis=1)
    sections = [np.array([[-w / 2, -h / 2], [w / 2, -h / 2], [w / 2, h / 2], [-w / 2, h / 2]])
                for w, h in zip(width, height, strict=True)]  # fmt: skip
    loft(stock, path, sections, ref=(0.0, 0.0, 1.0), tile=0.3)
    prod = Mesh("iron_forged")
    xs = np.linspace(-0.3, 0.3, 11)
    bend = 0.06 * (xs / 0.3) ** 2  # the limb tips bend back towards the shooter
    limb = circle(1.0, max(4, seg // 2)) * np.array([0.008, 0.012])
    loft(prod, np.stack([xs, 0.47 - bend, np.full_like(xs, 0.035)], axis=1),
         [limb * (1 - 0.4 * abs(x) / 0.3) for x in xs], ref=(0.0, 0.0, 1.0), tile=0.1)  # fmt: skip
    string = Mesh("leather")
    for side in (-1, 1):
        loft(string, np.array([[side * 0.3, 0.41, 0.035], [0.0, 0.16, 0.04]]),
             [circle(0.0015, 4)] * 2, tile=0.05)  # fmt: skip
    return [stock, prod, string]


RUNES = ("firebolt", "heal", "sleep", "transform_wolf", "summon_wolf")  # it_rune_<spell> (M12)

ITEMS: dict[str, tuple[Callable[[int], list[Mesh]], int]] = {
    "it_sword_old": (lambda s: sword(s, rust=True), 12),
    "it_sword_crude": (lambda s: sword(s, rust=False), 12),
    "it_club": (club, 14),
    "it_bow_short": (bow, 10),
    "it_apple": (apple, 16),
    "it_bread": (bread, 16),
    "it_potion_heal_small": (potion, 16),
    "it_lockpick": (lockpick, 8),
    "it_key": (key, 10),
    "it_broom": (broom, 12),
    "it_mug": (mug, 14),
    "it_axe": (axe, 12),
    "it_arrow": (arrow, 5),
    "it_bolt": (bolt, 5),
    "it_crossbow": (crossbow, 10),
    "it_sword_2h": (sword_2h, 12),
    "it_potion_mana_small": (lambda s: potion(s, "glass_blue"), 16),
    "it_scroll": (scroll, 12),
    "it_ham": (ham, 14),
    "it_sausage": (sausage, 12),
    "it_cheese": (cheese, 20),
    "it_pretzel": (pretzel, 10),
    "it_beer": (beer, 16),
    "it_wine": (wine, 16),
    "it_potion_heal_medium": (lambda s: potion(s, "glass_red", 1.3), 16),
    "it_potion_mana_medium": (lambda s: potion(s, "glass_blue", 1.3), 16),
    "it_potion_speed": (lambda s: potion(s, "glass_green"), 16),
    "it_potion_strength": (lambda s: potion(s, "glass_amber"), 16),
    "it_cloth_bolt": (cloth_bolt, 16),
    "it_jug": (jug, 16),
    "it_bowl": (bowl, 20),
    "it_herb_sage": (lambda s: herb(s, "sage"), 8),
    "it_herb_nettle": (lambda s: herb(s, "nettle"), 8),
    "it_herb_chamomile": (lambda s: herb(s, "chamomile"), 8),
    "it_herbs_dried": (herbs_dried, 8),
    "it_ring_gold": (lambda s: ring(s, "gold"), 16),
    "it_ring_silver": (lambda s: ring(s, "silver"), 16),
    "it_amulet": (amulet, 14),
    "it_chain_gold": (chain, 8),
    "it_hammer": (hammer, 12),
    "it_saw": (saw, 8),
    "it_shears": (shears, 8),
    "it_torch": (torch, 12),
    "it_lute": (lute, 16),
    **{f"it_rune_{spell}": ((lambda sp: lambda s: rune(s, sp))(spell), 12) for spell in RUNES},
}

# textures: name -> (source below the item sources folder, size, colour factor) or procedural
TEXTURES: dict[str, tuple[str, int, tuple[float, float, float]]] = {
    "metal_rust": ("Metal021/Metal021_1K-JPG_Color.jpg", 512, (1.0, 1.0, 1.0)),
    "leather": ("Leather014/Leather014_1K-JPG_Color.jpg", 256, (1.0, 1.0, 1.0)),
    "wood": ("Wood049/Wood049_1K-JPG_Color.jpg", 512, (1.0, 1.0, 1.0)),
    "bark": ("Bark012/Bark012_1K-JPG_Color.jpg", 512, (0.62, 0.5, 0.4)),  # darker, browner
    "wood_dark": ("Wood060/Wood060_1K-JPG_Color.jpg", 256, (0.85, 0.8, 0.75)),
}
PROCEDURAL = (
    "iron_forged", "apple", "bread", "glass_red", "glass_blue", "cork", "straw", "feather",
    "stone", "parchment", "wax_red", "pitch", *(f"rune_{spell}" for spell in RUNES),
    "ham", "bone", "sausage", "cheese", "clay", "glass_green", "glass_amber", "linen",
    "root", "leaf_sage", "leaf_green", "leaf_dry", "flower_white", "flower_yellow",
    "gold", "silver",
)  # fmt: skip


RUNE_COLOURS = {  # sign colour per spell
    "firebolt": (0.95, 0.42, 0.08), "heal": (0.25, 0.85, 0.35), "sleep": (0.6, 0.35, 0.9),
    "transform_wolf": (0.7, 0.5, 0.25), "summon_wolf": (0.25, 0.4, 0.95),
}  # fmt: skip


def _rune_sign(spell: str, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Stroke mask (bool) of the sign of a spell, x and y in -1..1 (y up on the face)."""
    r = np.hypot(x, y)
    stroke = 0.09
    if spell == "firebolt":  # flame: the outline of a drop, round below and pointed above

        def drop(scale: float) -> np.ndarray:
            xs, ys = x / scale, (y + 0.2) / scale
            round_ = np.hypot(xs, ys) < 0.42
            point = (ys >= 0) & (ys < 0.95) & (np.abs(xs) < 0.42 * (1 - ys / 0.95))
            return round_ | point

        return drop(1.0) & ~drop(0.72)
    if spell == "heal":  # cross
        return ((np.abs(x) < stroke) & (np.abs(y) < 0.7)) | (
            (np.abs(y) < stroke) & (np.abs(x) < 0.7)
        )
    if spell == "sleep":  # crescent moon
        return (r < 0.65) & (np.hypot(x - 0.3, y - 0.15) > 0.55)
    if spell == "transform_wolf":  # spiral
        a = np.arctan2(y, x)
        return (np.abs(((r - 0.08 * a) % 0.25) - 0.12) < 0.05) & (r < 0.75)
    # summon: a ring with a star of lines
    lines = np.zeros_like(r, dtype=bool)
    for k in range(3):
        ang = np.pi * k / 3
        lines |= np.abs(x * np.sin(ang) - y * np.cos(ang)) < stroke * 0.7
    return (np.abs(r - 0.6) < stroke) | (lines & (r < 0.6))


def _rune_face(spell: str, size: int, n1: np.ndarray, n2: np.ndarray) -> np.ndarray:
    base = procedural_texture("stone", size)  # the face matches the stone around it
    v, u = np.mgrid[0:size, 0:size] / (size - 1)
    sign = _rune_sign(spell, 2 * u - 1, 1 - 2 * v)
    colour = np.array(RUNE_COLOURS[spell])[None, None] * (0.85 + 0.3 * n2[..., None])
    out = np.where(sign[..., None], colour, base)
    return np.clip(out, 0, 1)


def procedural_texture(name: str, size: int = 256) -> np.ndarray:
    """(size, size, 3) colour in 0..1 for the procedural materials."""
    rng = np.random.default_rng(sum(map(ord, name)))
    n1, n2 = fractal(size, rng, octaves=5, base=4), fractal(size, rng, octaves=3, base=16)
    if name == "iron_forged":  # dark, uneven forged iron with hammer marks
        lum = 0.17 + 0.1 * n1 + 0.06 * (n2 - 0.5)
        return np.clip(lum[:, :, None] * np.array([1.0, 1.0, 1.05]), 0, 1)
    if name == "apple":  # red with yellow-green streaks and speckles along u (around)
        streak = 0.5 + 0.5 * np.sin(np.linspace(0, 18 * np.pi, size))[None, :] * (n1 - 0.3)
        red, green = np.array([0.55, 0.08, 0.06]), np.array([0.6, 0.55, 0.15])
        c = red[None, None] * (1 - 0.6 * streak[..., None]) + green * 0.6 * streak[..., None]
        speck = (rng.random((size, size)) > 0.985)[..., None] * 0.25
        return np.clip(c + speck, 0, 1)
    if name in ("leaf_sage", "leaf_green", "leaf_dry"):  # leaf with a lighter midrib along v
        base = {"leaf_sage": (0.45, 0.52, 0.42), "leaf_green": (0.16, 0.32, 0.12),
                "leaf_dry": (0.5, 0.45, 0.32)}[name]  # fmt: skip
        u = np.linspace(0, 1, size)[None, :]
        rib = np.exp(-(((u - 0.5) / 0.05) ** 2))
        c = np.array(base)[None, None] * (0.8 + 0.3 * n1[..., None]) + 0.12 * rib[..., None]
        return np.clip(c, 0, 1)
    if name == "root":
        return np.clip(np.array([0.32, 0.24, 0.16])[None, None] * (0.7 + 0.5 * n1[..., None]), 0, 1)
    if name == "flower_white":
        return np.clip(np.array([0.93, 0.92, 0.86])[None, None] * (0.9 + 0.1 * n1[..., None]), 0, 1)
    if name == "flower_yellow":
        return np.clip(
            np.array([0.85, 0.65, 0.12])[None, None] * (0.85 + 0.2 * n1[..., None]), 0, 1
        )
    if name in ("gold", "silver"):  # polished metal with soft bright streaks
        base = (0.78, 0.6, 0.22) if name == "gold" else (0.72, 0.73, 0.75)
        streak = 0.5 + 0.5 * np.sin(np.linspace(0, 10 * np.pi, size))[None, :]
        c = np.array(base)[None, None] * (
            0.75 + 0.3 * streak[..., None] * n1[..., None] + 0.1 * n2[..., None]
        )
        return np.clip(c, 0, 1)
    if name == "ham":  # smoked crust: dark red-brown with lighter fat streaks along v
        streak = value_noise(size, 24, rng)[:, :1].repeat(size, axis=1).T
        c = np.array([0.45, 0.2, 0.13])[None, None] * (0.75 + 0.35 * n1[..., None])
        return np.clip(c + 0.18 * (streak[..., None] > 0.7) * np.array([0.9, 0.7, 0.55]), 0, 1)
    if name == "bone":  # pale, slightly yellow bone
        return np.clip(
            np.array([0.82, 0.77, 0.66])[None, None] * (0.85 + 0.2 * n1[..., None]), 0, 1
        )
    if name == "sausage":  # red-brown skin with pale fat specks
        c = np.array([0.5, 0.2, 0.14])[None, None] * (0.8 + 0.3 * n1[..., None])
        speck = (rng.random((size, size)) > 0.94)[..., None] * np.array([0.3, 0.25, 0.2])
        return np.clip(c + speck, 0, 1)
    if name == "cheese":  # waxed yellow-orange rind, mottled
        c = np.array([0.78, 0.58, 0.22])[None, None] * (0.8 + 0.3 * n1[..., None])
        return np.clip(c * (0.92 + 0.15 * n2[..., None]), 0, 1)
    if name == "clay":  # fired clay, red-brown, with a darker glaze band
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 4 * np.pi, size))[:, None]
        c = np.array([0.52, 0.3, 0.18])[None, None] * (0.8 + 0.3 * n1[..., None])
        return np.clip(c * (0.85 + 0.2 * band[..., None]), 0, 1)
    if name == "glass_green":  # dark green bottle glass (wine, speed potion)
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 6 * np.pi, size))[None, :]
        c = np.array([0.08, 0.28, 0.1])[None, None] * (0.8 + 0.4 * band[..., None] * n1[..., None])
        return np.clip(c, 0, 1)
    if name == "glass_amber":  # amber glass (strength potion)
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 6 * np.pi, size))[None, :]
        c = np.array([0.55, 0.3, 0.05])[None, None] * (0.8 + 0.4 * band[..., None] * n1[..., None])
        return np.clip(c, 0, 1)
    if name == "linen":  # undyed linen weave: fine checker of threads
        y, x = np.mgrid[0:size, 0:size]
        weave = 0.5 + 0.5 * np.sign(np.sin(x * np.pi / 3) * np.sin(y * np.pi / 3))
        c = np.array([0.78, 0.72, 0.6])[None, None] * (
            0.85 + 0.1 * weave[..., None] + 0.1 * n1[..., None]
        )
        return np.clip(c, 0, 1)
    if name == "bread":  # baked crust: darker on top (v = along the loaf, u = around)
        u = np.linspace(0, 1, size)[None, :]
        top = 0.5 + 0.5 * np.cos(2 * np.pi * (u - 0.25))
        c = np.array([0.72, 0.5, 0.26])[None, None] * (0.75 + 0.25 * n1[..., None])
        return np.clip(c * (1 - 0.35 * top[..., None]), 0, 1)
    if name == "glass_red":  # opaque dark red glass with soft bands
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 6 * np.pi, size))[None, :]
        c = np.array([0.38, 0.04, 0.05])[None, None] * (0.8 + 0.4 * band[..., None] * n1[..., None])
        return np.clip(c, 0, 1)
    if name == "straw":  # dry twigs: streaks along v (the bundle)
        streak = value_noise(size, 64, rng)[:, :1].repeat(size, axis=1).T
        c = np.array([0.62, 0.5, 0.3])[None, None] * (0.6 + 0.6 * streak[..., None])
        return np.clip(c * (0.85 + 0.3 * n2[..., None]), 0, 1)
    if name == "feather":  # grey-brown vanes with fine streaks along v
        streak = value_noise(size, 96, rng)[:, :1].repeat(size, axis=1).T
        c = np.array([0.66, 0.6, 0.52])[None, None] * (0.75 + 0.35 * streak[..., None])
        return np.clip(c * (0.9 + 0.2 * n2[..., None]), 0, 1)
    if name == "glass_blue":  # opaque deep blue glass with soft bands (mana)
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 6 * np.pi, size))[None, :]
        c = np.array([0.05, 0.12, 0.45])[None, None] * (0.8 + 0.4 * band[..., None] * n1[..., None])
        return np.clip(c, 0, 1)
    if name == "stone":  # grey river stone with fine grain
        c = np.array([0.42, 0.41, 0.39])[None, None] * (0.75 + 0.35 * n1[..., None])
        return np.clip(c * (0.9 + 0.2 * n2[..., None]), 0, 1)
    if name == "parchment":  # yellowed parchment with darker blotches
        c = np.array([0.82, 0.74, 0.56])[None, None] * (0.85 + 0.2 * n1[..., None])
        return np.clip(c - 0.12 * (n2[..., None] > 0.7), 0, 1)
    if name == "wax_red":
        return np.clip(np.array([0.55, 0.06, 0.05])[None, None] * (0.8 + 0.4 * n2[..., None]), 0, 1)
    if name.startswith("rune_"):  # stone with the carved, coloured sign of the spell
        return _rune_face(name[5:], size, n1, n2)
    if name == "pitch":  # rags soaked in pitch: tarry dark brown, wound bands along v, glossy spots
        band = 0.5 + 0.5 * np.sin(np.linspace(0, 22 * np.pi, size))[:, None]
        c = np.array([0.16, 0.11, 0.07])[None, None] * (0.7 + 0.5 * n1[..., None])
        c = c * (0.8 + 0.35 * band[..., None])
        return np.clip(c + 0.06 * (n2[..., None] > 0.72), 0, 1)
    if name == "cork":
        return np.clip(np.array([0.55, 0.4, 0.24])[None, None] * (0.7 + 0.5 * n2[..., None]), 0, 1)
    raise ValueError(f"unknown procedural texture {name}")


# --- glTF -----------------------------------------------------------------------------------------


def _normals(pos: np.ndarray, tris: np.ndarray) -> np.ndarray:
    """Smooth normals, welded across UV seams (same position)."""
    key = np.round(pos / 1e-6).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    face = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    acc = np.zeros((weld.max() + 1, 3))
    for c in range(3):
        np.add.at(acc, weld[tris[:, c]], face)
    n = acc[weld]
    length = np.linalg.norm(n, axis=1, keepdims=True)
    return np.where(length > 1e-12, n / np.maximum(length, 1e-12), [0.0, 1.0, 0.0])


def item_gltf(item: str, texture_uri: Callable[[str], str]) -> tuple[dict, bytes]:
    """glTF document and binary chunk of an item with its LOD levels."""
    build, segments = ITEMS[item]
    doc: dict = {
        "asset": {"version": "2.0", "generator": GENERATOR},
        "scene": 0,
        "scenes": [{"nodes": []}],
        "nodes": [],
        "meshes": [],
        "materials": [],
        "textures": [],
        "images": [],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "accessors": [],
        "bufferViews": [],
        "buffers": [],
    }
    blob = bytearray()
    material_of: dict[str, int] = {}

    def view(data: bytes, target: int) -> int:
        while len(blob) % 4:
            blob.append(0)
        doc["bufferViews"].append(
            {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target}
        )
        blob.extend(data)
        return len(doc["bufferViews"]) - 1

    def accessor(arr: np.ndarray, kind: str, target: int, minmax: bool = False) -> int:
        comp = 5126 if arr.dtype == np.float32 else 5125
        acc = {"bufferView": view(arr.tobytes(), target), "componentType": comp,
               "count": len(arr), "type": kind}  # fmt: skip
        if minmax:
            acc["min"] = [float(x) for x in arr.min(axis=0)]
            acc["max"] = [float(x) for x in arr.max(axis=0)]
        doc["accessors"].append(acc)
        return len(doc["accessors"]) - 1

    def material(name: str) -> int:
        if name not in material_of:
            doc["images"].append({"uri": texture_uri(name)})
            doc["textures"].append({"sampler": 0, "source": len(doc["images"]) - 1})
            doc["materials"].append({
                "name": name,
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": len(doc["textures"]) - 1},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.8,
                },
            })  # fmt: skip
            material_of[name] = len(doc["materials"]) - 1
        return material_of[name]

    for level, share in enumerate(LOD_SEGMENTS):
        seg = max(4, int(round(segments * share)))
        prims = []
        for mesh in build(seg):
            pos, uv, tris = mesh.arrays()
            nrm = _normals(pos, tris)
            prims.append({
                "attributes": {
                    "POSITION": accessor(pos.astype(np.float32), "VEC3", 34962, minmax=True),
                    "NORMAL": accessor(nrm.astype(np.float32), "VEC3", 34962),
                    "TEXCOORD_0": accessor(uv.astype(np.float32), "VEC2", 34962),
                },
                "indices": accessor(tris.astype(np.uint32).ravel(), "SCALAR", 34963),
                "material": material(mesh.material),
            })  # fmt: skip
        doc["meshes"].append({"name": f"{item}_lod{level}", "primitives": prims})
        doc["nodes"].append({"name": f"{item}_lod{level}", "mesh": len(doc["meshes"]) - 1})
        doc["scenes"][0]["nodes"].append(len(doc["nodes"]) - 1)
    for name, at in MARKERS.get(item, {}).items():
        doc["nodes"].append({"name": name, "translation": [float(v) for v in at]})
        doc["scenes"][0]["nodes"].append(len(doc["nodes"]) - 1)
    doc["buffers"].append({"byteLength": len(blob)})
    return doc, bytes(blob)


def glb_bytes(doc: dict, binary: bytes) -> bytes:
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    bn = binary + b"\0" * (-len(binary) % 4)
    chunks = (
        struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bn), 0x004E4942) + bn
    )
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


def write_glb(path: Path, doc: dict, binary: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(glb_bytes(doc, binary))


def texture_file(name: str) -> str:
    return f"{name}.jpg"


def build_items(items_dir: Path, only: list[str] | None = None) -> list[Path]:
    """Writes items/<id>.glb for every item (textures: items/textures/<name>.jpg)."""
    written = []
    for item in ITEMS:
        if only and item not in only:
            continue
        doc, binary = item_gltf(item, lambda name: "textures/" + texture_file(name))
        out = items_dir / f"{item}.glb"
        write_glb(out, doc, binary)
        written.append(out)
    return written


# --- checks ---------------------------------------------------------------------------------------

# expected length along +Y (metres) per item: (min, max)
LENGTHS = {
    "it_sword_old": (0.9, 1.15), "it_sword_crude": (0.9, 1.15), "it_club": (0.6, 0.85),
    "it_bow_short": (1.0, 1.4), "it_apple": (0.06, 0.1), "it_bread": (0.15, 0.3),
    "it_potion_heal_small": (0.12, 0.2), "it_lockpick": (0.1, 0.2), "it_key": (0.07, 0.15),
    "it_broom": (1.2, 1.6), "it_mug": (0.09, 0.15), "it_axe": (0.6, 0.9),
    "it_arrow": (0.7, 0.8), "it_bolt": (0.3, 0.4), "it_crossbow": (0.75, 0.9),
    "it_sword_2h": (1.3, 1.55),
    "it_ham": (0.3, 0.38), "it_sausage": (0.2, 0.28), "it_cheese": (0.07, 0.09),
    "it_pretzel": (0.11, 0.15), "it_beer": (0.22, 0.28), "it_wine": (0.27, 0.33),
    "it_potion_heal_medium": (0.18, 0.24), "it_potion_mana_medium": (0.18, 0.24),
    "it_potion_speed": (0.12, 0.2), "it_potion_strength": (0.12, 0.2),
    "it_cloth_bolt": (0.45, 0.55), "it_jug": (0.22, 0.28), "it_bowl": (0.06, 0.08),
    "it_herb_sage": (0.15, 0.22), "it_herb_nettle": (0.2, 0.28), "it_herb_chamomile": (0.15, 0.22),
    "it_herbs_dried": (0.18, 0.26), "it_ring_gold": (0.02, 0.03), "it_ring_silver": (0.02, 0.03),
    "it_amulet": (0.24, 0.3), "it_chain_gold": (0.3, 0.4), "it_hammer": (0.28, 0.34),
    "it_saw": (0.58, 0.65), "it_shears": (0.18, 0.24),
    "it_potion_mana_small": (0.12, 0.2), "it_scroll": (0.18, 0.24), "it_torch": (0.65, 0.75),
    "it_lute": (0.75, 0.9),
    **{f"it_rune_{spell}": (0.05, 0.08) for spell in RUNES},
}  # fmt: skip


def _lod0_geometry(g: Gltf, item: str) -> tuple[np.ndarray, int] | None:
    lod0 = next((n for n in g.list("nodes") if n.get("name") == f"{item}_lod0"), None)
    if lod0 is None:
        return None
    prims = g.doc["meshes"][lod0["mesh"]]["primitives"]
    pos = np.concatenate([np.asarray(g.accessor(p["attributes"]["POSITION"])) for p in prims])
    return pos, sum(g.doc["accessors"][p["indices"]]["count"] // 3 for p in prims)


# wider than tall by nature (standing on their bottom, +Y up): no longest-axis rule
AXIS_FREE = {"it_cheese", "it_bowl", "it_pretzel"}


def validate_item(path: Path) -> Report:
    """An item file against the contract: origin = grip inside the item, longest along +Y, size,
    triangle budget, LOD nodes, no skin, textures present, and the same as a fresh build."""
    report = Report(path)
    try:
        g = Gltf.load(path)
    except GltfError as e:
        report.error("gltf.parse", str(e))
        return report
    item = path.stem
    if g.doc.get("skins"):
        report.error("item.skin", "items are static meshes without a skin")
    names = {n.get("name") for n in g.list("nodes")}
    for level in range(len(LOD_SEGMENTS)):
        if f"{item}_lod{level}" not in names:
            report.error("item.lod", f"node {item}_lod{level} missing")
    for marker in MARKERS.get(item, {}):
        if marker not in names:
            report.error("item.marker", f"marker node {marker} missing")
    for image in g.list("images"):
        if "uri" in image and not (path.parent / image["uri"]).is_file():
            report.error("item.texture", f"texture {image['uri']} missing")
    lod0 = _lod0_geometry(g, item)
    if lod0 is None:
        return report
    pos, tris = lod0
    report.stats.update(triangles=tris, lods=len(names & {f"{item}_lod{i}" for i in range(3)}))
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    if tris > TRIANGLES_MAX:
        report.error("item.budget", f"{tris} triangles (max {TRIANGLES_MAX})")
    if not (np.all(lo <= 0.02) and np.all(hi >= -0.02)):
        report.error("item.origin", f"the origin (grip) lies outside the item ({lo}, {hi})")
    extent = hi - lo
    if item not in AXIS_FREE and extent[1] < max(extent[0], extent[2]) * 0.9:
        report.error("item.axis", f"the item is not longest along +Y (extent {extent.round(3)})")
    if item in LENGTHS:
        a, b = LENGTHS[item]
        if not a <= extent[1] <= b:
            report.error("item.size", f"{extent[1]:.2f} m along +Y, expected {a}..{b} m")
    if item in ITEMS:
        doc, binary = item_gltf(item, lambda name: "textures/" + texture_file(name))
        fresh = _lod0_geometry(Gltf.from_bytes(glb_bytes(doc, binary)), item)
        assert fresh is not None
        if fresh[1] != tris or fresh[0].shape != pos.shape or np.abs(fresh[0] - pos).max() > 1e-4:
            report.error("item.stale", "differs from a fresh build: run gothar-chargen build-items")
    else:
        report.warning("item.unknown", f"{item} is not built by build-items (data in items.py)")
    return report
