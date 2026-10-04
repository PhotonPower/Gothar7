"""Figure sets data/figure_sets.toml (contract figuren -> engine, characters-pipeline.md §6.4)."""

from __future__ import annotations

import tomllib
from pathlib import Path

from gothar_chargen.figure import load_figure

SOURCE = Path(__file__).resolve().parents[3] / "assets" / "source"
SETS_FILE = SOURCE / "data" / "figure_sets.toml"
NAMED = {"smith", "innkeeper", "market_woman", "gate_guard", "guard_captain"}  # one NPC each
GUILDS_MEN_ONLY = {"guard", "hunter"}  # owner decision 2026-10-04, as in Gothic


def _sets() -> dict[str, list[str]]:
    data = tomllib.loads(SETS_FILE.read_text(encoding="utf-8"))
    assert set(data) == {"sets"}, "only [sets]"
    return data["sets"]


def test_every_listed_manifest_exists_and_loads():
    sets = _sets()
    assert sets
    for name, members in sets.items():
        assert members, name
        for rel in members:
            assert rel.startswith("characters/figures/") and rel.endswith(".figure.toml"), rel
            path = SOURCE / rel
            assert path.is_file(), rel
            load_figure(path)  # a valid manifest


def test_each_manifest_in_one_set_and_no_named_figures():
    seen: dict[str, str] = {}
    for name, members in _sets().items():
        for rel in members:
            stem = Path(rel).name.removesuffix(".figure.toml")
            assert stem not in NAMED, f"{stem} belongs to one NPC (Npc.figure), not to a set"
            assert stem not in seen, f"{stem} in {seen.get(stem)} and {name}"
            seen[stem] = name
            assert stem.startswith(f"{name}_"), f"{stem}: manifests are named <set>_<m|f>_<n>"


def test_men_only_sets():
    for name in GUILDS_MEN_ONLY & set(_sets()):
        for rel in _sets()[name]:
            assert "_m_" in Path(rel).name, f"{rel}: {name} figures are men only"
