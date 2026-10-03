import os
import shutil
import subprocess
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.buildings.batch import SINK_M, file_stem, generate, ground_range
from gothar_worldgen.buildings.gltf import read_glb
from gothar_worldgen.export.terrain import Grid

GRID = Grid(np.zeros((101, 101)), -50.0, -50.0, 1.0)


def house(bid: str, x: float, z: float, core: bool = True, ground: float = 0.0,
          width: float = 6.0) -> dict:  # fmt: skip
    fp = [[x, z], [x + width, z], [x + width, z - 4], [x, z - 4]]
    return {"id": bid, "inCore": core, "footprint": fp, "groundY": ground,
            "roof": {"type": "saddle", "eaveY": ground + 5, "ridgeY": ground + 8,
                     "ridgeDir": [1, 0]}}  # fmt: skip


def test_file_stem_is_case_safe():
    a, b = file_stem("DEBW_001ZFS"), file_stem("DEBW_001Zfs")
    assert a != b and a.lower() == a and b.lower() == b
    assert a.startswith("debw_001zfs_") and file_stem("a/b").startswith("a_b_")


def test_ground_range():
    heights = np.zeros((101, 101))
    heights[:, 53:] = -2.0  # a step 3 m east of x = 0
    grid = Grid(heights, -50.0, -50.0, 1.0)
    assert ground_range(grid, [[0, 0], [6, 0], [6, -4], [0, -4]]) == (-2.0, 0.0)
    assert ground_range(grid, [[-20, 0], [-14, 0], [-14, -4], [-20, -4]]) == (0.0, 0.0)
    assert ground_range(None, [[0, 0], [1, 0], [1, 1]]) is None
    assert ground_range(grid, [[500, 500], [501, 500], [501, 501]]) is None


def test_core_buildings_one_file_each(tmp_path: Path):
    blds = [house("B_A", 0, 0), house("B_a", 10, 0, width=7), house("FAR", 30, 30, core=False)]
    res = generate(blds, GRID, tmp_path, "worlds/t/generated/buildings")
    entries = res.index["entries"]
    assert [e["id"] for e in entries] == ["B_A", "B_a"]  # surroundings skipped in "core"
    files = sorted(p.name for p in tmp_path.glob("*.glb"))
    assert len(files) == 2  # ids differing in case do not overwrite each other
    e = entries[0]
    assert e["mesh"] == f"worlds/t/generated/buildings/{file_stem('B_A')}.glb"
    assert e["pos"] == [3.0, -SINK_M, -2.0] and e["triangles"] > 0
    doc, _ = read_glb((tmp_path / f"{file_stem('B_A')}.glb").read_bytes())
    assert doc["accessors"][0]["max"][1] == pytest.approx(8.0 + SINK_M)


def test_identical_meshes_share_a_file(tmp_path: Path):
    # Identical geometry (e.g. a duplicate in the data) is written once; the name does not matter.
    res = generate([house("A", 0, 0), house("B", 0, 0)], GRID, tmp_path, "v")
    assert res.shared == 1 and len(list(tmp_path.glob("*.glb"))) == 1
    assert res.index["entries"][0]["mesh"] == res.index["entries"][1]["mesh"]


def test_surroundings_are_merged_per_cell(tmp_path: Path):
    blds = [house("A", 0, 0), house("F1", 70, 10, core=False), house("F2", 80, 20, core=False),
            house("F3", -120, 10, core=False)]  # fmt: skip
    res = generate(blds, None, tmp_path, "v", area="all")
    cells = [e for e in res.index["entries"] if e["kind"] == "cell"]
    assert sorted((c["id"], c["buildings"]) for c in cells) == [("cell_-2_0", 1), ("cell_1_0", 2)]
    assert res.index["stats"]["cells"] == 2 and res.index["stats"]["buildings"] == 1
    with pytest.raises(ValueError):
        generate(blds, None, tmp_path, "v", area="town")


def test_base_follows_the_lowest_ground_and_reports_steps(tmp_path: Path):
    heights = np.zeros((101, 101))
    heights[:, 53:] = -2.0
    grid = Grid(heights, -50.0, -50.0, 1.0)
    res = generate([house("STEP", 0, 0)], grid, tmp_path, "v")
    assert res.index["entries"][0]["pos"][1] == pytest.approx(-2.0 - SINK_M)
    assert res.steps == [("STEP", 2.0)]
    assert res.index["entries"][0]["dgmMinY"] == -2.0


def test_locked_buildings_keep_their_file_and_stale_files_go(tmp_path: Path):
    generate([house("A", 0, 0), house("OLD", 20, 20)], GRID, tmp_path, "v")
    locked_file = tmp_path / f"{file_stem('A')}.glb"
    locked_file.write_bytes(b"hand made")
    res = generate([house("A", 0, 0)], GRID, tmp_path, "v", locked=frozenset({"A"}))
    assert locked_file.read_bytes() == b"hand made" and res.kept_locked == 1
    assert res.index["entries"][0] == {"id": "A", "kind": "building", "locked": True,
                                       "mesh": f"v/{file_stem('A')}.glb"}  # fmt: skip
    assert not (tmp_path / f"{file_stem('OLD')}.glb").exists()  # building gone, file removed


def _find_cook() -> Path | None:
    env = os.environ.get("G7_COOK")
    candidates = [Path(env)] if env else []
    root = Path(__file__).resolve().parents[3]
    for preset in ("release", "debug"):
        candidates.append(root / "build" / preset / "tools" / "asset-cooker" / "g7-cook.exe")
        candidates.append(root / "build" / preset / "tools" / "asset-cooker" / "g7-cook")
    on_path = shutil.which("g7-cook")
    if on_path:
        candidates.append(Path(on_path))
    return next((c for c in candidates if c.is_file()), None)


RULES = Path(__file__).resolve().parents[1] / "data" / "building_rules.json"


def test_medieval_mode(tmp_path: Path):
    from gothar_worldgen.buildings.medieval import load_rules

    rules = load_rules(RULES)
    blds = [house("A", 0, 0), house("B", 20, 0, width=9)]
    res = generate(blds, GRID, tmp_path, "v", mode="medieval", rules=rules)
    st = res.index["stats"]
    assert res.index["mode"] == "medieval" and st["buildings"] == 2
    assert st["budget"]["trianglesPerBuilding"] == rules.data["budget"]["trianglesPerBuilding"]
    assert st["trianglesPerBuilding"]["max"] >= st["trianglesPerBuilding"]["median"] > 0
    for e in res.index["entries"]:
        doc, _ = read_glb((tmp_path / e["mesh"].split("/")[-1]).read_bytes())
        for m in doc["materials"]:  # equal material values in every house (engine batching)
            assert m["pbrMetallicRoughness"]["baseColorFactor"] == rules.data["palette"][m["name"]]
    assert sum(res.index["stats"]["style"]["style"].values()) == 2
    with pytest.raises(ValueError):
        generate(blds, GRID, tmp_path, "v", mode="medieval")  # rules missing
    with pytest.raises(ValueError):
        generate(blds, GRID, tmp_path, "v", mode="baroque")


@pytest.mark.skipif(_find_cook() is None, reason="g7-cook not built (set G7_COOK)")
@pytest.mark.parametrize("mode", ["massing", "medieval"])
def test_generated_glb_cooks_with_g7_cook(tmp_path: Path, mode: str):
    from gothar_worldgen.buildings.medieval import load_rules

    src = tmp_path / "source" / "worlds" / "t"
    rules = load_rules(RULES) if mode == "medieval" else None
    generate([house("A", 0, 0)], GRID, src, "worlds/t", mode=mode, rules=rules)
    out = tmp_path / "cooked"
    cmd = [str(_find_cook()), "--source", str(tmp_path / "source"), "--out", str(out)]
    done = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert done.returncode == 0, done.stdout + done.stderr
    assert list(out.rglob("*.g7mesh")), done.stdout
    assert "warn" not in (done.stdout + done.stderr).lower()
