import io
import json
import shutil
from pathlib import Path

from gothar_worldgen.cli import EXIT_OK, main

from .test_export_terrain import write_work

RULES = Path(__file__).resolve().parents[1] / "data" / "building_rules.json"


def run(*argv: str) -> tuple[int, str]:
    out = io.StringIO()
    return main(list(argv), out=out), out.getvalue()


def setup(config_dir: Path, tmp_path: Path, monkeypatch) -> tuple[list[str], Path]:
    data_root = tmp_path / "data_root"
    work = data_root / "work" / "testsite"
    write_work(work)
    (work / "buildings.json").write_text(
        json.dumps(
            {
                "buildings": [
                    {
                        "id": "B1",
                        "inCore": True,
                        "groundY": 0.0,
                        "footprint": [[0, 0], [8, 0], [8, -6], [0, -6]],
                        "roof": {
                            "type": "saddle",
                            "eaveY": 6.0,
                            "ridgeY": 10.0,
                            "ridgeDir": [1, 0],
                            "pitchDeg": 53.0,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setenv("GOTHAR_DATA_ROOT", str(data_root))
    data = config_dir.parent / "data"
    data.mkdir(exist_ok=True)
    shutil.copy(RULES, data / "building_rules.json")
    assets = tmp_path / "assets"
    return ["--config-dir", str(config_dir)], assets


def test_buildings_default_is_medieval_and_assemble(config_dir: Path, tmp_path: Path, monkeypatch):
    base, assets = setup(config_dir, tmp_path, monkeypatch)
    code, out = run(*base, "export-terrain", "testsite", "--assets-dir", str(assets))
    assert code == EXIT_OK, out
    code, out = run(*base, "buildings", "testsite", "--assets-dir", str(assets))
    assert code == EXIT_OK, out
    index = json.loads(
        (assets / "worlds/testsite/generated/buildings_index.json").read_text(encoding="utf-8")
    )
    assert index["mode"] == "medieval" and "style" in index["stats"]
    assert "styles:" in out and "rueckbau:" in out
    code, out = run(*base, "assemble", "testsite", "--assets-dir", str(assets))
    assert code == EXIT_OK, out
    world = json.loads((assets / "worlds/testsite/testsite.g7world").read_text(encoding="utf-8"))
    assert any(v["name"] == "BLD_B1" for v in world["vobs"])
    code, out = run(
        *base, "buildings", "testsite", "--mode", "massing", "--assets-dir", str(assets)
    )
    index = json.loads(
        (assets / "worlds/testsite/generated/buildings_index.json").read_text(encoding="utf-8")
    )
    assert code == EXIT_OK and index["mode"] == "massing" and "rueckbau:" not in out
