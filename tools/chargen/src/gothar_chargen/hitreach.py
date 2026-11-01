"""Reach of human melee strikes for the hit windows (gameplay.md "Trefferprüfung").

The engine's weapon sweep (``Combat.weapon_sweep``) moves a capsule from ``socket_hand_r`` along
socket +Y (length = the item's +Y extent) between ``hit_start`` and ``hit_end``; fists are short
capsules (``fist_length`` 0.12 m) at both hand sockets. This module samples that blade per clip
frame (30 fps) in model space, i.e. relative to the figure's position (glTF: +Y up, +Z forward,
+X left; the strikes are no root-motion states, so a lunge of ``root`` carries the blade), so the
windows can be set on the frames where the blade swings forward through the space in front of
the body (height ``ZONE_LOW`` .. ``ZONE_HIGH``, at least ``ZONE_FRONT`` ahead). Python + numpy.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from gothar_chargen.clipspeed import FPS, duration, global_matrices
from gothar_chargen.gltf import Gltf

ZONE_LOW = 0.6  # m above the ground
ZONE_HIGH = 1.8
ZONE_FRONT = 0.5  # m in front of the figure's position
SAMPLES = 11  # points along the blade (grip .. tip)
FIST_LENGTH = 0.12  # engine CombatSettings::fist_length
# weapon item per mode (the reference for the sweep length: the old sword, the two-hander)
WEAPONS = {"1h": "it_sword_old", "2h": "it_sword_2h"}
SOCKETS = {"fist": ("socket_hand_r", "socket_hand_l"), "1h": ("socket_hand_r",)}
SOCKETS["2h"] = SOCKETS["1h"]


def item_length(item_glb: Path) -> float:
    """+Y extent of an item model (the engine's sweep length, contract §3.1)."""
    gltf = Gltf.load(item_glb)
    tops = [
        float(gltf.doc["accessors"][prim["attributes"]["POSITION"]]["max"][1])
        for mesh in gltf.doc.get("meshes", [])
        for prim in mesh["primitives"]
    ]
    return max(tops)


def weapon_length(mode: str, items_dir: Path) -> float:
    return item_length(items_dir / f"{WEAPONS[mode]}.glb") if mode in WEAPONS else FIST_LENGTH


@dataclass(frozen=True)
class BladePath:
    """Blade points per frame: ``points[frame, socket, sample] = (x, y, z)`` in model space."""

    clip: str
    points: np.ndarray

    @property
    def frames(self) -> int:
        return int(self.points.shape[0])

    @property
    def tips(self) -> np.ndarray:
        """(frames, sockets, 3) blade tips."""
        return self.points[:, :, -1]

    def in_zone(self) -> np.ndarray:
        """(frames,) number of blade points in front of the body (all sockets)."""
        p = self.points
        mask = (p[..., 2] >= ZONE_FRONT) & (p[..., 1] >= ZONE_LOW) & (p[..., 1] <= ZONE_HIGH)
        return mask.reshape(self.frames, -1).sum(axis=1)

    def ahead(self) -> np.ndarray:
        """(frames,) farthest blade point ahead within the zone's heights (-inf: none)."""
        p = self.points
        z = np.where((p[..., 1] >= ZONE_LOW) & (p[..., 1] <= ZONE_HIGH), p[..., 2], -np.inf)
        return z.reshape(self.frames, -1).max(axis=1)

    def window_reach(self, start: int, end: int) -> tuple[float, float, float]:
        """Farthest point ahead within frames start..end and the height range of the blade points
        in the zone there: (forward, low, high); low/high are nan without any point in the zone."""
        p = self.points[start : end + 1].reshape(-1, 3)
        zone = p[(p[:, 2] >= ZONE_FRONT) & (p[:, 1] >= ZONE_LOW) & (p[:, 1] <= ZONE_HIGH)]
        if not len(zone):
            return float(p[:, 2].max()), float("nan"), float("nan")
        return float(zone[:, 2].max()), float(zone[:, 1].min()), float(zone[:, 1].max())


def blade_paths(set_glb: Path, mode: str, length: float) -> dict[str, BladePath]:
    """Blade paths of all clips of a human set (``mode`` = fist, 1h, 2h)."""
    gltf = Gltf.load(set_glb)
    names = {n.get("name"): i for i, n in enumerate(gltf.doc["nodes"])}
    along = np.linspace(0.0, length, SAMPLES)
    out = {}
    for animation in gltf.doc.get("animations", []):
        t = np.arange(int(round(duration(gltf, animation) * FPS)) + 1) / FPS
        global_of = global_matrices(gltf, animation, t)
        per_socket = []
        for socket in SOCKETS[mode]:
            m = global_of(names[socket])
            origin = m[:, :3, 3]
            axis = m[:, :3, 1] / np.linalg.norm(m[:, :3, 1], axis=1, keepdims=True)
            per_socket.append(origin[:, None, :] + axis[:, None, :] * along[None, :, None])
        out[animation["name"]] = BladePath(animation["name"], np.stack(per_socket, axis=1))
    return out
