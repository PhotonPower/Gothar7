import io
import json
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from gothar_worldgen.facade.webui.api import ApiError, Workspace, safe_name

from .webui_fixture import make_site, override


@pytest.fixture
def ws(tmp_path: Path) -> Workspace:
    return make_site(tmp_path)


def test_summary(ws: Workspace):
    s = ws.summary()
    assert s["site"] == "testsite"
    assert [b["id"] for b in s["buildings"]] == ["B1", "B2"]
    assert s["buildings"][0]["status"] == "open"
    assert s["buildings"][0]["inCore"] is True
    assert len(s["cameras"]) == 2
    assert ws.image_width == 1024


def test_detail_ranks_candidates_per_edge(ws: Workspace):
    d = ws.detail("B1")
    assert [e["edge"] for e in d["edges"]] == [0, 1, 2, 3]
    south = d["edges"][0]
    assert (south["widthM"], south["heightM"]) == (4.0, 6.0)
    assert [c["index"] for c in south["candidates"]] == [0, 1]  # the closer camera first
    assert south["candidates"][0]["capture"] == "cap1"
    assert d["edges"][2]["candidates"] == []  # north side: cameras are behind it
    assert d["override"] is None


def test_facade_png_is_rectified_and_cached(ws: Workspace):
    png = ws.facade_png("B1", 0, "cap1", 0, 20)
    img = np.asarray(Image.open(io.BytesIO(png)))
    assert img.shape[:2] == (120, 80)  # 6 m x 4 m at 20 px/m
    assert img[115, 5].mean() < 80 and img[115, 15].mean() > 180  # checkerboard bottom-left
    cache = ws.work_dir / "facade_cache" / "B1_e0_cap1_0_20.png"
    assert cache.read_bytes() == png
    assert ws.facade_png("B1", 0, "cap1", 0, 20) == png


@pytest.mark.parametrize(
    ("args", "status"),
    [
        (("B1", 0, "cap1", 0, 33), 400),  # px not allowed
        (("B9", 0, "cap1", 0, 20), 404),
        (("B1", 0, "cap2", 0, 20), 404),
        (("B1", 0, "cap1", 7, 20), 404),
        (("B1", 0, "../cap1", 0, 20), 400),
        (("B1", 2, "cap1", 0, 20), 422),  # camera behind the north facade
    ],
)
def test_facade_png_errors(ws: Workspace, args, status):
    with pytest.raises(ApiError) as e:
        ws.facade_png(*args)
    assert e.value.status == status
    assert ":\\" not in str(e.value) and "/tmp" not in str(e.value)


def test_frame_files_cannot_leave_their_capture(tmp_path: Path):
    ws = make_site(tmp_path)
    manifest = ws.work_dir / "captures" / "cap1" / "frames.json"
    doc = json.loads(manifest.read_text(encoding="utf-8"))
    doc["frames"][0]["file"] = "../../buildings.json"
    manifest.write_text(json.dumps(doc), encoding="utf-8")
    ws.reload_frames()
    with pytest.raises(ApiError) as e:
        ws.facade_png("B1", 0, "cap1", 0, 20)
    assert e.value.status == 400


def test_put_and_get_override(ws: Workspace):
    res = ws.put_override("B1", override(locked=True, notes="Ecke"))
    assert res["status"] == "locked"
    saved = json.loads((ws.overrides_dir / "B1.json").read_text(encoding="utf-8"))
    assert saved["frontFacade"]["timber"] == "mann"
    assert ws.get_override("B1") == saved
    assert ws.summary()["buildings"][0]["status"] == "locked"
    assert ws.detail("B1")["override"]["notes"] == "Ecke"


@pytest.mark.parametrize(
    "data",
    [
        override(id="B2"),  # id mismatch
        override(storeys=[-1.0]),
        override(frontFacade={"edge": 0, "openings": [
            {"storey": 0, "type": "window", "x": 3.5, "w": 1.0, "h": 1.0}]}),  # outside 4 m
        override(frontFacade={"edge": 9}),
        ["not", "an", "object"],
    ],
)  # fmt: skip
def test_put_override_rejects_invalid(ws: Workspace, data):
    with pytest.raises(ApiError) as e:
        ws.put_override("B1", data)
    assert e.value.status == 400
    assert not (ws.overrides_dir / "B1.json").exists()


def test_invalid_stored_override(ws: Workspace):
    ws.overrides_dir.mkdir(parents=True)
    (ws.overrides_dir / "B1.json").write_text("{", encoding="utf-8")
    assert ws.status("B1") == "invalid"
    with pytest.raises(ApiError) as e:
        ws.get_override("B1")
    assert e.value.status == 409


@pytest.mark.parametrize("bad", ["", "..", "a/b", "a\\b", "..\\x", "%2e%2e", "x" * 200, ".hidden"])
def test_safe_name(bad):
    with pytest.raises(ApiError):
        safe_name(bad, "building id")


def test_vocabulary_is_marked_as_draft(ws: Workspace):
    v = ws.vocabulary()
    assert v["status"].startswith("festgelegt")
    for key in ("style", "timber", "infill", "roofCover"):
        ids = [item["id"] for item in v[key]]
        assert ids and len(ids) == len(set(ids))
        assert all(item["label"] for item in v[key])


def test_workspace_without_import(tmp_path: Path):
    with pytest.raises(ApiError) as e:
        Workspace.open("x", tmp_path / "work", tmp_path / "o", tmp_path / "v.json")
    assert e.value.status == 500
