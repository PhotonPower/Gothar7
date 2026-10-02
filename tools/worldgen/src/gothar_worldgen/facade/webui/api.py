"""Web UI logic without HTTP: buildings, candidate images, rectified facades, overrides.

Every request names files only indirectly (building id, capture name, frame index), never by path.
Error messages are meant for the browser and contain no local paths.
"""

from __future__ import annotations

import io
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from PIL import Image as PilImage

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.overrides import (
    OverrideError,
    from_json,
    load,
    path_for,
    save,
    to_json,
    validate_against,
)
from gothar_worldgen.facade.poses import rank_views
from gothar_worldgen.facade.preview import load_buildings, load_equirect
from gothar_worldgen.facade.rectify import FacadeError, facade_from_footprint, rectify

CANDIDATES_PER_EDGE = 3
PX_PER_M_CHOICES = (10, 20, 50, 100)
_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")


class ApiError(Exception):
    """A request that cannot be served; ``status`` is the HTTP status code."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status


def safe_name(value: str, what: str) -> str:
    """Building ids and capture names: letters, digits, ``_.-``; no path characters."""
    if not _NAME.match(value) or ".." in value:
        raise ApiError(400, f"invalid {what}")
    return value


@dataclass(frozen=True)
class Frame:
    capture: str
    index: int
    file: str  # relative to the capture directory, as listed in frames.json
    pose: CameraPose
    utc: str


@dataclass
class Workspace:
    """One site: work data in DATA_ROOT, overrides in the repository."""

    site: str
    work_dir: Path
    overrides_dir: Path
    vocabulary_path: Path
    buildings: dict[str, dict[str, Any]] = field(default_factory=dict)
    frames: list[Frame] = field(default_factory=list)
    image_width: int = 0

    @classmethod
    def open(
        cls, site: str, work_dir: Path, overrides_dir: Path, vocabulary_path: Path
    ) -> Workspace:
        ws = cls(site, work_dir, overrides_dir, vocabulary_path)
        try:
            ws.buildings = load_buildings(work_dir / "buildings.json")
        except FacadeError as e:
            raise ApiError(500, str(e)) from None
        ws.reload_frames()
        return ws

    # --- captures -------------------------------------------------------------------------

    def reload_frames(self) -> None:
        self.frames = []
        captures = self.work_dir / "captures"
        for manifest in sorted(captures.glob("*/frames.json")) if captures.is_dir() else []:
            name = manifest.parent.name
            if not _NAME.match(name):
                continue
            doc = json.loads(manifest.read_text(encoding="utf-8"))
            for i, f in enumerate(doc.get("frames", [])):
                pose = CameraPose(f["x"], f["y"], f["z"], f.get("headingDeg", 0.0))
                self.frames.append(Frame(name, i, f["file"], pose, f.get("utc", "")))
        if self.frames and not self.image_width:
            with PilImage.open(self._frame_path(self.frames[0])) as img:
                self.image_width = img.width

    def _frame_path(self, frame: Frame) -> Path:
        base = (self.work_dir / "captures" / frame.capture).resolve()
        path = (base / frame.file).resolve()
        if not path.is_relative_to(base):
            raise ApiError(400, "frame outside its capture")
        return path

    def frame(self, capture: str, index: int) -> Frame:
        safe_name(capture, "capture")
        for f in self.frames:
            if f.capture == capture and f.index == index:
                return f
        raise ApiError(404, "unknown frame")

    # --- buildings ------------------------------------------------------------------------

    def building(self, building_id: str) -> dict[str, Any]:
        safe_name(building_id, "building id")
        b = self.buildings.get(building_id)
        if b is None:
            raise ApiError(404, "unknown building")
        return b

    def status(self, building_id: str) -> str:
        path = path_for(self.overrides_dir, building_id)
        if not path.is_file():
            return "open"
        try:
            return "locked" if load(path).locked else "annotated"
        except OverrideError:
            return "invalid"

    def summary(self) -> dict[str, Any]:
        """All buildings (footprint, core flag, status) and the camera positions."""
        return {
            "site": self.site,
            "buildings": [
                {
                    "id": bid,
                    "footprint": [[round(x, 2), round(z, 2)] for x, z in b.get("footprint", [])],
                    "inCore": bool(b.get("inCore")),
                    "status": self.status(bid),
                }
                for bid, b in self.buildings.items()
            ],
            "cameras": [
                {
                    "capture": f.capture,
                    "index": f.index,
                    "x": round(f.pose.x, 2),
                    "z": round(f.pose.z, 2),
                }
                for f in self.frames
            ],
        }

    def detail(self, building_id: str) -> dict[str, Any]:
        """Edges with their best images, and the current override."""
        b = self.building(building_id)
        poses = [f.pose for f in self.frames]
        edges = []
        for e in range(len(b.get("footprint", []))):
            try:
                facade = facade_from_footprint(b, e)
            except FacadeError:
                continue
            ranked = rank_views(facade, poses, self.image_width) if poses else []
            edges.append(
                {
                    "edge": e,
                    "widthM": round(facade.width_m, 3),
                    "heightM": round(facade.height_m, 3),
                    "candidates": [
                        {
                            "capture": self.frames[c.index].capture,
                            "index": self.frames[c.index].index,
                            "utc": self.frames[c.index].utc,
                            "distanceM": round(c.distance_m, 1),
                            "angleDeg": round(c.angle_deg, 1),
                            "score": round(c.score, 2),
                        }
                        for c in ranked[:CANDIDATES_PER_EDGE]
                    ],
                }
            )
        override = self.get_override(building_id)
        return {
            "id": building_id,
            "inCore": bool(b.get("inCore")),
            "groundY": b.get("groundY"),
            "roof": b.get("roof"),
            "edges": edges,
            "override": override,
            "status": self.status(building_id),
        }

    # --- facade images --------------------------------------------------------------------

    def facade_png(
        self, building_id: str, edge: int, capture: str, index: int, px_per_m: int
    ) -> bytes:
        """Rectified facade as PNG; cached in ``<work>/facade_cache``."""
        if px_per_m not in PX_PER_M_CHOICES:
            raise ApiError(400, f"px per metre must be one of {PX_PER_M_CHOICES}")
        b = self.building(building_id)
        frame = self.frame(capture, index)
        cache = (
            self.work_dir
            / "facade_cache"
            / f"{building_id}_e{edge}_{capture}_{index}_{px_per_m}.png"
        )
        if cache.is_file():
            return cache.read_bytes()
        try:
            facade = facade_from_footprint(b, edge)
            image = load_equirect(self._frame_path(frame))
            view = rectify(image, frame.pose, facade, float(px_per_m))
        except FacadeError as e:
            raise ApiError(422, _strip_paths(str(e))) from None
        buf = io.BytesIO()
        PilImage.fromarray(view.image).save(buf, format="PNG")
        data = buf.getvalue()
        cache.parent.mkdir(parents=True, exist_ok=True)
        tmp = cache.with_suffix(".tmp")
        tmp.write_bytes(data)
        tmp.replace(cache)
        return data

    # --- overrides ------------------------------------------------------------------------

    def get_override(self, building_id: str) -> dict[str, Any] | None:
        self.building(building_id)
        path = path_for(self.overrides_dir, building_id)
        if not path.is_file():
            return None
        try:
            return to_json(load(path))
        except OverrideError:
            raise ApiError(409, "the stored override is invalid; fix the file by hand") from None

    def put_override(self, building_id: str, data: Any) -> dict[str, Any]:  # noqa: ANN401
        b = self.building(building_id)
        if isinstance(data, dict) and data.get("id") != building_id:
            raise ApiError(400, "override id does not match the building")
        try:
            override = from_json(data)
        except OverrideError as e:
            raise ApiError(400, str(e)) from None
        problems = validate_against(override, b)
        if problems:
            raise ApiError(400, "; ".join(problems))
        save(self.overrides_dir, override)
        return {
            "saved": building_id,
            "status": self.status(building_id),
            "override": to_json(override),
        }

    def vocabulary(self) -> dict[str, Any]:
        try:
            return json.loads(self.vocabulary_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            raise ApiError(500, "facade vocabulary missing or invalid") from None


def _strip_paths(message: str) -> str:
    """Drop anything that looks like a local path from a message."""
    return re.sub(r"([A-Za-z]:)?[\\/][^\s:]*", "<path>", message)
