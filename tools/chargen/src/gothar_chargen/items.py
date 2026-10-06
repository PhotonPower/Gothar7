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


def potion(seg: int, glass_material: str = "glass_red") -> list[Mesh]:
    glass = Mesh(glass_material)
    profile = [
        (0.0, -0.06), (0.03, -0.058), (0.035, -0.05), (0.035, 0.02), (0.03, 0.036),
        (0.014, 0.05), (0.011, 0.056), (0.011, 0.082), (0.014, 0.085), (0.014, 0.09),
        (0.0, 0.09),
    ]  # fmt: skip
    lathe(glass, profile, seg, tile=0.1)
    cork = Mesh("cork")
    lathe(cork, [(0.0, 0.088), (0.0105, 0.088), (0.0115, 0.108), (0.0, 0.11)], seg, tile=0.05)
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
    "stone", "parchment", "wax_red", *(f"rune_{spell}" for spell in RUNES),
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
    "it_potion_mana_small": (0.12, 0.2), "it_scroll": (0.18, 0.24),
    **{f"it_rune_{spell}": (0.05, 0.08) for spell in RUNES},
}  # fmt: skip


def _lod0_geometry(g: Gltf, item: str) -> tuple[np.ndarray, int] | None:
    lod0 = next((n for n in g.list("nodes") if n.get("name") == f"{item}_lod0"), None)
    if lod0 is None:
        return None
    prims = g.doc["meshes"][lod0["mesh"]]["primitives"]
    pos = np.concatenate([np.asarray(g.accessor(p["attributes"]["POSITION"])) for p in prims])
    return pos, sum(g.doc["accessors"][p["indices"]]["count"] // 3 for p in prims)


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
    if extent[1] < max(extent[0], extent[2]) * 0.9:
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
