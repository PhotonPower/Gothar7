import io
import json
from pathlib import Path

import numpy as np
import pytest

from gothar_worldgen.cli import EXIT_ERROR, EXIT_OK, main
from gothar_worldgen.export.terrain import (
    ExportError,
    Grid,
    check_terrain_block,
    crop,
    export_terrain,
    load_grid,
    terrain_block,
    world_document,
    world_text,
)
from gothar_worldgen.geo.terrain import U16_MAX, quantize_heights

W, H, CELL = 41, 21, 1.0
FIRST = (-20.0, -10.0)  # sample centres: x -20 .. 20, z -10 .. 10


def plane(x: np.ndarray, z: np.ndarray) -> np.ndarray:
    return 3.0 + 0.25 * x - 0.5 * z  # rises to the east and to the north


def write_work(work: Path) -> np.ndarray:
    xs = FIRST[0] + np.arange(W) * CELL
    zs = FIRST[1] + np.arange(H) * CELL
    gx, gz = np.meshgrid(xs, zs)
    y = plane(gx, gz)
    values, min_y, max_y = quantize_heights(y)
    work.mkdir(parents=True, exist_ok=True)
    values.astype("<u2").tofile(work / "terrain.r16")
    meta = {
        "format": "gothar-terrain", "version": 1, "width": W, "height": H, "cellSize": CELL,
        "firstSample": {"x": FIRST[0], "z": FIRST[1]},
        "heightRange": {"minY": min_y, "maxY": max_y, "stepM": (max_y - min_y) / U16_MAX},
        "areas": {"core": {"minX": -5.0, "minZ": -5.0, "maxX": 5.0, "maxZ": 5.0},
                  "surroundings": {"minX": -20.0, "minZ": -10.0, "maxX": 20.0, "maxZ": 10.0}},
    }  # fmt: skip
    (work / "terrain.json").write_text(json.dumps(meta), encoding="utf-8")
    (work / "buildings.json").write_text(
        json.dumps({"buildings": [{"id": "B1", "footprint": [[0, 0], [3, 0], [3, -3], [0, -3]]}]}),
        encoding="utf-8",
    )
    (work / "streets.json").write_text(
        json.dumps(
            {
                "streets": [
                    {"highway": "residential", "widthM": 3.0, "points": [[-20, 2], [20, 2]]}
                ],
                "squares": [],
            }
        ),
        encoding="utf-8",
    )
    (work / "features.json").write_text(json.dumps({"features": []}), encoding="utf-8")
    return y


def decode(path: Path, block: dict) -> np.ndarray:
    raw = np.fromfile(path, dtype="<u2").reshape(block["height"], block["width"])
    return block["minY"] + raw / U16_MAX * (block["maxY"] - block["minY"])


def test_load_grid(tmp_path: Path):
    y = write_work(tmp_path)
    g = load_grid(tmp_path)
    assert (g.width, g.height, g.first_x, g.first_z, g.cell) == (W, H, -20.0, -10.0, 1.0)
    np.testing.assert_allclose(g.heights, y, atol=0.0005)
    assert g.heights[0, -1] > g.heights[-1, 0]  # row 0 = north, column 0 = west


def test_load_grid_errors(tmp_path: Path):
    with pytest.raises(ExportError, match="import"):
        load_grid(tmp_path)
    write_work(tmp_path)
    (tmp_path / "terrain.r16").write_bytes(b"\0\0")
    with pytest.raises(ExportError, match="samples"):
        load_grid(tmp_path)


def test_crop_and_thin(tmp_path: Path):
    write_work(tmp_path)
    g = load_grid(tmp_path)
    core = crop(g, {"minX": -5.0, "minZ": -5.0, "maxX": 5.0, "maxZ": 5.0})
    assert (core.width, core.height, core.first_x, core.first_z) == (11, 11, -5.0, -5.0)
    np.testing.assert_allclose(core.heights[0, 0], plane(np.array(-5.0), np.array(-5.0)), atol=1e-3)
    thin = crop(g, None, step=2)
    assert (thin.width, thin.height, thin.cell) == (21, 11, 2.0)
    assert (thin.first_x, thin.first_z) == FIRST
    with pytest.raises(ExportError):
        crop(g, {"minX": 100.0, "minZ": 100.0, "maxX": 101.0, "maxZ": 101.0})
    with pytest.raises(ExportError):
        crop(g, None, step=0)


def test_export_round_trip(tmp_path: Path):
    y = write_work(tmp_path / "work")
    grid = crop(load_grid(tmp_path / "work"), {"minX": -5, "minZ": -5, "maxX": 5, "maxZ": 5})
    world = tmp_path / "out" / "w.g7world"
    r16 = tmp_path / "out" / "generated" / "w.r16"
    res = export_terrain(grid, "w", world, r16, "worlds/x/generated/w.r16")
    doc = json.loads(world.read_text(encoding="utf-8"))
    block = doc["terrain"]
    assert list(block) == ["version", "heightmap", "width", "height", "cellSize", "firstSample",
                           "minY", "maxY"]  # fmt: skip
    assert block["firstSample"] == [-5.0, -5.0] and block["heightmap"] == "worlds/x/generated/w.r16"
    assert r16.stat().st_size == 11 * 11 * 2
    # Re-quantized over the smaller range: finer steps, and the heights survive both encodings.
    assert res.step_mm < (y.max() - y.min()) / U16_MAX * 1000
    np.testing.assert_allclose(decode(r16, block), y[5:16, 15:26], atol=0.001)
    raw = np.fromfile(r16, dtype="<u2")
    assert raw.min() <= 1 and raw.max() >= U16_MAX - 1  # range rounded outwards to mm
    assert check_terrain_block(block) == []


def test_full_export_keeps_the_original_range_and_values(tmp_path: Path):
    write_work(tmp_path / "work")
    meta = json.loads((tmp_path / "work" / "terrain.json").read_text(encoding="utf-8"))
    res = export_terrain(load_grid(tmp_path / "work"), "w", tmp_path / "w.g7world",
                         tmp_path / "w.r16", "w.r16")  # fmt: skip
    assert (res.min_y, res.max_y) == (meta["heightRange"]["minY"], meta["heightRange"]["maxY"])
    assert (tmp_path / "w.r16").read_bytes() == (tmp_path / "work" / "terrain.r16").read_bytes()


def test_contract_example_from_world_md():
    grid = Grid(np.broadcast_to(0.0, (2000, 2000)), -999.5, -999.5, 1.0)
    block = terrain_block(grid, "worlds/leonberg/terrain.r16", -50.991, 94.85)
    assert block == {"version": 1, "heightmap": "worlds/leonberg/terrain.r16", "width": 2000,
                     "height": 2000, "cellSize": 1.0, "firstSample": [-999.5, -999.5],
                     "minY": -50.991, "maxY": 94.85}  # fmt: skip
    # Sample (c, r) -> (x, z) as in world.md.
    for (c, r), xz in {(0, 0): (-999.5, -999.5), (1999, 0): (999.5, -999.5),
                       (0, 1999): (-999.5, 999.5), (1000, 500): (0.5, -499.5)}.items():  # fmt: skip
        assert (grid.first_x + c * grid.cell, grid.first_z + r * grid.cell) == xz
    # Height encoding examples.
    _, mn, mx = -50.991, -50.991, 94.85
    for y, v in ((0.0, 22913), (21.9, 32754), (mn, 0), (mx, 65535)):
        assert round((y - mn) / (mx - mn) * U16_MAX) == v


@pytest.mark.parametrize(
    ("change", "problem"),
    [({"version": 2}, "version"), ({"heightmap": "a.png"}, "heightmap"), ({"width": 1}, "width"),
     ({"cellSize": 0}, "cellSize"), ({"firstSample": [0]}, "firstSample"), ({"maxY": -60}, "maxY")],
)  # fmt: skip
def test_check_terrain_block(change, problem):
    grid = Grid(np.zeros((3, 3)), 0.0, 0.0, 1.0)
    block = {**terrain_block(grid, "a.r16", -1.0, 1.0), **change}
    assert any(problem in p for p in check_terrain_block(block))


def test_world_text_layout_like_the_engine():
    grid = Grid(np.zeros((3, 3)), -1.0, -1.0, 1.0)
    doc = world_document("t", terrain_block(grid, "t.r16", -1.0, 2.0), None)
    assert world_text(doc) == (
        '{\n  "version": 1,\n  "name": "t",\n  "nextVobId": 1,\n  "staticMeshes": [],\n'
        '  "terrain": {"version":1,"heightmap":"t.r16","width":3,"height":3,"cellSize":1.0,'
        '"firstSample":[-1.0,-1.0],"minY":-1.0,"maxY":2.0},\n  "vobs": []\n}\n'
    )
    vob = {
        "id": 4,
        "type": "empty",
        "name": "a",
        "pos": [0.0, 0.0, 0.0],
        "rot": [0.0, 0.0, 0.0, 1.0],
    }
    text = world_text({**doc, "vobs": [vob, {**vob, "id": 5}], "waynet": {"points": []}})
    assert '  "vobs": [\n    {"id":4,' in text and text.endswith(
        '\n  ],\n  "waynet": {"points":[]}\n}\n'
    )
    assert json.loads(text)["vobs"][1]["id"] == 5


def test_existing_world_keeps_vobs_and_ids(tmp_path: Path):
    write_work(tmp_path / "work")
    grid = load_grid(tmp_path / "work")
    world = tmp_path / "w.g7world"
    world.write_text(json.dumps({"version": 1, "name": "keep", "nextVobId": 9, "vobs": [
        {"id": 8, "type": "empty", "name": "x", "pos": [0, 0, 0], "rot": [0, 0, 0, 1]}],
        "zones": [1]}), encoding="utf-8")  # fmt: skip
    export_terrain(grid, "ignored", world, tmp_path / "w.r16", "w.r16")
    doc = json.loads(world.read_text(encoding="utf-8"))
    assert (doc["name"], doc["nextVobId"], doc["vobs"][0]["id"], doc["zones"]) == (
        "keep",
        9,
        8,
        [1],
    )
    assert doc["terrain"]["width"] == W
    world.write_text(json.dumps({"version": 2}), encoding="utf-8")
    with pytest.raises(ExportError, match="version 1"):
        export_terrain(grid, "x", world, tmp_path / "w.r16", "w.r16")


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    return main(list(argv), out=out), out.getvalue()


def test_cli_export_terrain(config_dir: Path, tmp_path: Path, monkeypatch):
    data_root = tmp_path / "data"
    write_work(data_root / "work" / "testsite")
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(data_root))
    assets = tmp_path / "assets"
    base = [
        "--config-dir",
        str(config_dir),
        "export-terrain",
        "testsite",
        "--assets-dir",
        str(assets),
    ]
    code, out = run(*base)
    assert code == EXIT_OK, out
    world = assets / "worlds" / "testsite" / "testsite_terrain.g7world"
    block = json.loads(world.read_text(encoding="utf-8"))["terrain"]
    assert block["heightmap"] == "worlds/testsite/generated/testsite_terrain.r16"
    assert (assets / "worlds" / "testsite" / "generated" / "testsite_terrain.r16").is_file()
    assert "41 x 21 samples" in out and "splat: 2 maps, 7 layers" in out
    assert len(json.loads(world.read_text(encoding="utf-8"))["terrain"]["splat"]["layers"]) == 7
    assert (assets / "worlds" / "testsite" / "generated" / "testsite_terrain_splat1.png").is_file()
    assert (assets / "worlds" / "testsite" / "layers" / "kopfstein.png").is_file()
    code, _ = run(*base, "--area", "core", "--step", "2", "--name", "core", "--no-splat")
    assert code == EXIT_OK
    core = json.loads((assets / "worlds/testsite/core.g7world").read_text(encoding="utf-8"))
    assert (core["terrain"]["width"], core["terrain"]["cellSize"]) == (6, 2.0)
    assert "splat" not in core["terrain"]


def test_cli_export_terrain_without_import(config_dir: Path, tmp_path: Path, monkeypatch, capsys):
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(tmp_path / "data"))
    code, _ = run("--config-dir", str(config_dir), "export-terrain", "testsite",
                  "--assets-dir", str(tmp_path / "a"))  # fmt: skip
    assert code == EXIT_ERROR
    assert "import" in capsys.readouterr().err


def test_generated_folder_is_git_ignored():
    gitignore = Path(__file__).resolve().parents[3] / "assets" / "source" / "worlds" / ".gitignore"
    assert "*/generated/" in gitignore.read_text(encoding="utf-8").splitlines()


def test_start_points_are_added_once(tmp_path: Path):
    from gothar_worldgen.export.starts import DEFAULT_STARTS

    write_work(tmp_path / "work")
    grid = load_grid(tmp_path / "work")
    world = tmp_path / "w.g7world"
    export_terrain(grid, "w", world, tmp_path / "w.r16", "w.r16", starts=DEFAULT_STARTS)
    export_terrain(grid, "w", world, tmp_path / "w.r16", "w.r16", starts=DEFAULT_STARTS)
    doc = json.loads(world.read_text(encoding="utf-8"))
    starts = {v["name"]: v for v in doc["vobs"] if v["type"] == "start"}
    assert list(starts) == ["START_MARKTPLATZ", "START_UEBERSICHT"] and doc["nextVobId"] == 3
    # Feet on the terrain: the plane at (0, 8) is 3 + 0 - 4 = -1 m.
    assert starts["START_MARKTPLATZ"]["pos"] == pytest.approx([0.0, -1.0, 8.0], abs=0.002)
    assert starts["START_UEBERSICHT"]["rot"] == [-0.258819, 0.0, 0.0, 0.965926]
