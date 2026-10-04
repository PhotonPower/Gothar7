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


def _stem(rel: str) -> str:
    return Path(rel).name.removesuffix(".figure.toml")


def test_sets_by_sex_and_their_unions():
    """<set>_m / <set>_f hold one sex each (Npc has no sex field); <set> is their union. Every
    manifest is in exactly one sex set, no named figure in any set."""
    sets = _sets()
    seen: dict[str, str] = {}
    groups = {name for name in sets if not name.endswith(("_m", "_f"))}
    for name, members in sets.items():
        stems = [_stem(rel) for rel in members]
        assert not NAMED & set(stems), f"{name}: named figures belong to one NPC (Npc.figure)"
        assert len(stems) == len(set(stems)), f"{name}: listed twice"
        if name in groups:
            by_sex = [s for sex in ("_m", "_f") for s in sets.get(name + sex, [])]
            assert sorted(members) == sorted(by_sex), f"{name} must be the union of its sex sets"
            continue
        group, sex = name[:-2], name[-1]
        assert group in groups, f"{name}: no union set '{group}'"
        for stem in stems:
            assert stem.startswith(f"{group}_{sex}_"), f"{stem} in {name}: <set>_<m|f>_<n>"
            assert stem not in seen, f"{stem} in {seen.get(stem)} and {name}"
            seen[stem] = name


def test_men_only_sets():
    sets = _sets()
    for name in GUILDS_MEN_ONLY & set(sets):
        assert f"{name}_f" not in sets, f"{name} is men only"
        assert sorted(sets[name]) == sorted(sets[f"{name}_m"])
