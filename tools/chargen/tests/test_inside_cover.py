"""Skin inside loose garments (partdata `inside`, recipe `[inside]`): skirts hide the thighs."""

from __future__ import annotations

import math

import numpy as np
import pytest

from gothar_chargen.human import HumanError, parse_human
from gothar_chargen.partdata import LodMesh, covered_triangles


def _tube(
    radius: float, y0: float, y1: float, material: str, segments: int = 16, turn: float = 0.0
) -> LodMesh:
    """Open cylinder around the y axis (glTF: y up), normals and faces pointing outwards."""
    rows = []
    for y in (y0, y1):
        rows += [
            (
                radius * math.cos(2 * math.pi * (i + turn) / segments),
                y,
                radius * math.sin(2 * math.pi * (i + turn) / segments),
            )
            for i in range(segments)
        ]
    pos = np.array(rows, dtype=float)
    nrm = pos.copy()
    nrm[:, 1] = 0.0
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    tris = []
    for i in range(segments):
        a, b = i, (i + 1) % segments
        c, d = a + segments, b + segments
        tris += [(a, c, b), (b, c, d)]  # outward winding
    return LodMesh("mesh_lod0", [pos], [nrm], [np.array(tris)], [material])


def _hidden_rows(ranges: list[list[int]], count: int) -> np.ndarray:
    hidden = np.zeros(count, dtype=bool)
    for _prim, first, end in ranges:
        hidden[first:end] = True
    return hidden


def test_thighs_inside_a_skirt_are_hidden_only_with_inside():
    thighs = _tube(0.1, 0.5, 0.8, "skin")  # 15 cm inside the skirt
    skirt = _tube(0.25, 0.3, 0.9, "cloth_skirt", turn=0.37)  # not ray-aligned
    count = len(thighs.triangles[0])
    assert not _hidden_rows(covered_triangles(thighs, skirt, []), count).any()
    assert _hidden_rows(covered_triangles(thighs, skirt, [], inside=0.3), count).all()
    # too far inside for the given distance: kept
    assert not _hidden_rows(covered_triangles(thighs, skirt, [], inside=0.1), count).any()


def test_legs_below_the_skirt_stay_visible():
    shins = _tube(0.1, 0.0, 0.25, "skin")  # below the skirt's hem at 0.3
    skirt = _tube(0.25, 0.3, 0.9, "cloth_skirt", turn=0.37)  # not ray-aligned
    count = len(shins.triangles[0])
    assert not _hidden_rows(covered_triangles(shins, skirt, [], inside=0.3), count).any()


KIT = {
    "version": 1,
    "parts": ["cloth"],
    "triangles": 3000,
    "macro": {"gender": 0.0},
    "assets": {
        "skin": "skins/a/a.mhmat",
        "eyes": "eyes/e/e.mhclo",
        "clothes": ["clothes/skirt/skirt.mhclo"],
    },
}


def test_inside_recipe_key():
    h = parse_human({**KIT, "inside": {"skirt": 0.3}}, "cloth_x")
    assert h.inside == {"skirt": 0.3}


@pytest.mark.parametrize(
    ("inside", "message"),
    [({"skirt": 0.01}, "metres"), ({"skirt": True}, "metres"), ({"other": 0.3}, "unknown pieces")],
)
def test_invalid_inside(inside, message):
    with pytest.raises(HumanError, match=message):
        parse_human({**KIT, "inside": inside}, "cloth_x")
