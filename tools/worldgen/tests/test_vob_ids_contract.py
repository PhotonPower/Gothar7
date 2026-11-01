"""The VobId contract with engine (world.md "Stabilität der welt-Vobs", M15 save games): checked
against the committed data of every site, and against origin/main where git has it."""

import json
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
DATA = REPO / "tools" / "worldgen" / "data"
SITES = sorted(p.parent.name for p in DATA.glob("*/vob_ids.json"))


def _ids(site: str) -> dict:
    return json.loads((DATA / site / "vob_ids.json").read_text(encoding="utf-8"))


@pytest.mark.parametrize("site", SITES)
def test_ids_are_unique_and_below_the_next_id(site: str):
    doc = _ids(site)
    ids = doc["ids"]
    assert len(set(ids.values())) == len(ids)  # no id under two keys
    assert doc["nextVobId"] > max(ids.values(), default=0)


@pytest.mark.parametrize("site", SITES)
def test_every_vob_of_the_world_stands_under_its_key(site: str):
    world_path = REPO / "assets" / "source" / "worlds" / site / f"{site}.g7world"
    if not world_path.is_file():
        pytest.skip(f"{site}: no world file")
    world = json.loads(world_path.read_text(encoding="utf-8"))
    owned = set(_ids(site)["ids"].values())
    vobs = [v["id"] for v in world.get("vobs", []) if v.get("owner", "worldgen") == "worldgen"]
    assert len(set(vobs)) == len(vobs)
    assert [i for i in vobs if i not in owned] == []
    assert world["nextVobId"] > max(vobs, default=0)
    assert world["nextVobId"] <= _ids(site)["nextVobId"]


def _base(site: str) -> dict | None:
    """vob_ids.json on origin/main, if git and that ref are there (not in a shallow CI clone)."""
    path = f"tools/worldgen/data/{site}/vob_ids.json"
    try:
        out = subprocess.run(["git", "show", f"origin/main:{path}"], cwd=REPO, capture_output=True,
                             check=False, timeout=30)  # fmt: skip
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    return json.loads(out.stdout.decode("utf-8"))


@pytest.mark.parametrize("site", SITES)
def test_no_key_disappears_and_no_id_changes_against_main(site: str):
    base = _base(site)
    if base is None:
        pytest.skip("origin/main not available")
    now = _ids(site)
    changed = {k: (v, now["ids"].get(k)) for k, v in base["ids"].items() if now["ids"].get(k) != v}
    assert changed == {}  # a key gone or its id changed
    assert now["nextVobId"] >= base["nextVobId"]
    fresh = [v for k, v in now["ids"].items() if k not in base["ids"]]
    assert all(v >= base["nextVobId"] for v in fresh)  # new ids never reuse old ones
