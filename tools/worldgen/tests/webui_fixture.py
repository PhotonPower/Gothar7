"""Synthetic site for the web UI tests: two houses, one capture with two rendered 360° frames."""

import functools
import io
import json
import shutil
from pathlib import Path

from PIL import Image

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.rectify import facade_from_footprint
from gothar_worldgen.facade.webui.api import Workspace

from .facade_synth import render_equirect
from .test_facade_rectify import HOUSE

REPO_VOCABULARY = Path(__file__).resolve().parents[1] / "data" / "facade_vocabulary.json"
SHED = {
    "id": "B2",
    "inCore": False,
    "footprint": [[20.0, 0.0], [24.0, 0.0], [24.0, -3.0], [20.0, -3.0]],
    "groundY": 0.0,
    "roof": {"eaveY": 3.0, "ridgeY": 5.0},
}
POSES = [CameraPose(2.0, 1.6, 7.0, 0.0), CameraPose(2.0, 1.6, 25.0, 0.0)]


@functools.cache
def _frame_png(i: int) -> bytes:
    """Rendered once per test session; PNG bytes of frame ``i``."""

    buf = io.BytesIO()
    Image.fromarray(render_equirect(1024, POSES[i], facade_from_footprint(HOUSE, 0))).save(
        buf, format="PNG"
    )
    return buf.getvalue()


def make_site(root: Path) -> Workspace:
    work = root / "work"
    capture = work / "captures" / "cap1"
    (capture / "frames").mkdir(parents=True)
    house = {**HOUSE, "inCore": True}
    (work / "buildings.json").write_text(json.dumps({"buildings": [house, SHED]}), encoding="utf-8")
    frames = []
    for i, pose in enumerate(POSES):
        name = f"frames/f{i}.png"
        (capture / name).write_bytes(_frame_png(i))
        frames.append({"file": name, "videoTime": i, "utc": f"2026-10-04T07:00:0{i}.000Z",
                       "x": pose.x, "y": pose.y, "z": pose.z,
                       "headingDeg": pose.heading_deg})  # fmt: skip
    (capture / "frames.json").write_text(json.dumps({"frames": frames}), encoding="utf-8")
    vocabulary = root / "facade_vocabulary.json"
    shutil.copy(REPO_VOCABULARY, vocabulary)
    return Workspace.open("testsite", work, root / "overrides", vocabulary)


def override(**changes: object) -> dict:
    """A valid override for HOUSE (B1, 4 m x 6 m, front = edge 0)."""
    base = {
        "id": "B1",
        "keep": True,
        "storeys": [3.0, 3.0],
        "frontFacade": {
            "edge": 0,
            "openings": [{"storey": 0, "type": "door", "x": 1.0, "w": 1.0, "h": 2.0}],
            "timber": "mann",
        },
    }
    base.update(changes)
    return base
