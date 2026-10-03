import json
from pathlib import Path

import pytest

from gothar_worldgen.facade.overrides import (
    OverrideError,
    from_json,
    load,
    load_all,
    path_for,
    save,
    to_json,
    validate_against,
)

# Example from docs/design/leonberg-pipeline.md §4 (plus the optional sill height "y").
EXAMPLE = {
    "id": "DEBW_0010000abc",
    "keep": True,
    "style": "buergerhaus",
    "storeys": [3.2, 2.9, 2.8],
    "jettyM": 0.35,
    "frontFacade": {
        "edge": 2,
        "openings": [
            {"storey": 0, "type": "door", "x": 1.2, "w": 1.1, "h": 2.0},
            {"storey": 1, "type": "window", "x": 0.8, "w": 0.7, "h": 0.9, "y": 0.9},
        ],
        "timber": "mann",
        "infill": "plaster_ochre",
    },
    "roofCover": "tiles_old",
    "notes": "Eckhaus am Marktplatz",
    "seed": 1234,
}


def front(**opening):
    """Override change with a front facade holding one opening (x = 0)."""
    return {"frontFacade": {"edge": 0, "openings": [{"x": 0, **opening}]}}


BUILDING = {"id": "DEBW_0010000abc", "footprint": [[0, 0], [8, 0], [8, -6], [0, -6]]}


def test_round_trip_keeps_the_documented_format():
    o = from_json(EXAMPLE)
    assert o.storeys == [3.2, 2.9, 2.8]
    assert o.front_facade.openings[1].y == 0.9
    assert o.front_facade.openings[0].y is None
    assert to_json(o) == EXAMPLE
    assert not o.locked


def test_unknown_keys_and_locked_survive():
    data = {"id": "B1", "locked": True, "facadePhotos": ["2026-10-04/0042.jpg"]}
    assert to_json(from_json(data)) == {"id": "B1", "keep": True, "locked": True,
                                        "facadePhotos": ["2026-10-04/0042.jpg"]}  # fmt: skip


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"id": ""}, "id"),
        ({"keep": "yes"}, "keep"),
        ({"storeys": [3.0, -1.0]}, "storeys"),
        ({"seed": 1.5}, "seed"),
        ({"jettyM": "a"}, "jettyM"),
        ({"frontFacade": {"edge": -1}}, "edge"),
        (front(storey=0, type="hole", w=1, h=1), "type"),
        (front(storey=5, type="door", w=1, h=2), "storey 5"),
        (front(storey=0, type="door", w=0, h=2), "'w'"),
    ],
)  # fmt: skip
def test_invalid_overrides(change, message):
    with pytest.raises(OverrideError, match=message):
        from_json({**EXAMPLE, **change})


def test_validate_against_building():
    o = from_json(EXAMPLE)
    assert validate_against(o, BUILDING) == []  # edge 2 is 8 m wide
    o.front_facade.openings[0].x = 7.5
    assert any("outside the facade" in p for p in validate_against(o, BUILDING))
    o.front_facade.edge = 9
    assert any("does not exist" in p for p in validate_against(o, BUILDING))


def test_save_load_and_load_all(tmp_path: Path):
    path = save(tmp_path, from_json(EXAMPLE))
    assert path.name == "DEBW_0010000abc.json"
    assert json.loads(path.read_text(encoding="utf-8")) == EXAMPLE
    assert path.read_text(encoding="utf-8").endswith("\n")
    assert to_json(load(path)) == EXAMPLE
    assert set(load_all(tmp_path)) == {"DEBW_0010000abc"}
    assert load_all(tmp_path / "missing") == {}

    (tmp_path / "other.json").write_text(json.dumps({"id": "B2"}), encoding="utf-8")
    with pytest.raises(OverrideError, match="file name"):
        load_all(tmp_path)


@pytest.mark.parametrize("bad", ["", "../x", "a/b", "a\\b", ".."])
def test_path_for_rejects_unsafe_ids(tmp_path: Path, bad):
    with pytest.raises(OverrideError):
        path_for(tmp_path, bad)


def test_broken_file(tmp_path: Path):
    (tmp_path / "B.json").write_text("{", encoding="utf-8")
    with pytest.raises(OverrideError):
        load(tmp_path / "B.json")


def test_passages_are_parsed_checked_and_written_back():
    from gothar_worldgen.facade.overrides import from_json, to_json

    doc = {"id": "B", "keep": True,
           "passages": [{"axis": [[0, 0], [0, 10]], "w": 2.0, "note": "Durchgang"}]}  # fmt: skip
    o = from_json(doc)
    (ps,) = o.passages
    assert ps.axis == ((0.0, 0.0), (0.0, 10.0)) and ps.w == 2.0 and ps.h == 3.0
    assert from_json(to_json(o)).passages == o.passages
    for bad in (
        [{"axis": [[0, 0]]}],
        [{"axis": [[0, 0], [0, 0.5]]}],
        "x",
        [{"axis": [[0, 0], [0, 5]], "w": -1}],
    ):
        with pytest.raises(OverrideError):
            from_json({"id": "B", "passages": bad})
