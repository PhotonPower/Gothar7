"""Compares docs/design/animation-list.md with the clips in anims/ and monsters/ (progress report).

The list's "Prio A" table names clips exactly. A cell may hold several names; later names may omit
the mode (``none/t_walk_2_run``, ``t_run_2_walk``) and ``_l/r`` stands for both sides.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from gothar_chargen.gltf import Gltf, GltfError
from gothar_chargen.naming import is_clip_name

_CODE = re.compile(r"`([^`]+)`")


class ReportError(Exception):
    """The animation list cannot be read."""


@dataclass(frozen=True)
class ListRow:
    names: tuple[str, ...]
    status: str
    line: int


@dataclass
class Progress:
    rows: list[ListRow]  # Prio A (must be complete: --fail-missing)
    present: dict[str, Path]  # clip name -> file
    missing: list[str] = field(default_factory=list)
    stale: list[str] = field(default_factory=list)  # list status contradicts the files
    sections: dict[str, list[ListRow]] = field(default_factory=dict)  # other written-out tables

    @property
    def listed(self) -> list[str]:
        return [n for r in self.rows for n in r.names]

    def section_counts(self) -> dict[str, tuple[int, int]]:
        """Heading -> (present, listed) for the written-out tables besides Prio A."""
        counts = {}
        for heading, rows in self.sections.items():
            names = [n for r in rows for n in r.names]
            counts[heading] = (sum(n in self.present for n in names), len(names))
        return counts

    @property
    def extra(self) -> list[str]:
        listed = set(self.listed) | {
            n for rows in self.sections.values() for r in rows for n in r.names
        }
        return sorted(n for n in self.present if n not in listed)

    def to_dict(self) -> dict:
        return {
            "prio_a": {"listed": len(self.listed), "present": len(self.listed) - len(self.missing)},
            "sections": {
                h: {"present": p, "listed": n} for h, (p, n) in self.section_counts().items()
            },
            "missing": self.missing,
            "stale": self.stale,
            "extra": self.extra,
        }


def expand_names(cell: str) -> list[str]:
    """Clip names of one table cell (see module docstring)."""
    names: list[str] = []
    prefix = ""
    for token in _CODE.findall(cell):
        variants = [token[:-4] + "_l", token[:-4] + "_r"] if token.endswith("_l/r") else [token]
        for t in variants:
            if is_clip_name(t):
                name = t
            elif prefix and is_clip_name(f"{prefix}/{t}"):
                name = f"{prefix}/{t}"
            else:
                raise ReportError(f"cannot read clip name '{t}' in: {cell.strip()}")
            prefix = name.rsplit("/", 1)[0]
            names.append(name)
    return names


def parse_tables(text: str) -> list[tuple[str, list[ListRow]]]:
    """All tables with 'Name' and 'Status' columns, with the heading above each table."""
    tables: list[tuple[str, list[ListRow]]] = []
    heading = ""
    header: list[str] | None = None
    rows: list[ListRow] = []

    def close() -> None:
        nonlocal header, rows
        if rows:
            tables.append((heading, rows))
        header, rows = None, []

    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("#"):
            close()
            heading = line.lstrip("#").strip()
            continue
        if not line.startswith("|"):
            if header is not None:
                close()
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = [c.lower() for c in cells]
            if heading.startswith("Prio A") and not {"name", "status"} <= set(header):
                raise ReportError("Prio A table needs 'Name' and 'Status' columns")
            continue
        if set(line) <= set("|-: ") or not {"name", "status"} <= set(header):
            continue
        name_col, status_col = header.index("name"), header.index("status")
        rows.append(ListRow(tuple(expand_names(cells[name_col])), cells[status_col], number))
    close()
    return tables


def parse_prio_a(text: str) -> list[ListRow]:
    """Rows of the table under the "## Prio A" heading: (names, status)."""
    for heading, rows in parse_tables(text):
        if heading.startswith("Prio A"):
            return rows
    raise ReportError("no 'Prio A' table found")


def collect_clips(anims_dir: Path) -> dict[str, Path]:
    clips: dict[str, Path] = {}
    for glb in sorted(anims_dir.rglob("*.glb")):
        try:
            gltf = Gltf.load(glb)
        except GltfError:
            continue
        for anim in gltf.list("animations"):
            clips.setdefault(str(anim.get("name", "")), glb)
    return clips


def _check_status(row: ListRow, present: dict[str, Path], stale: list[str]) -> list[str]:
    missing = [n for n in row.names if n not in present]
    status = row.status.split()[0].lower() if row.status else ""
    if missing and (status.startswith("platzhalter") or status == "fertig"):
        stale.append(f"line {row.line}: status '{status}' but missing {missing}")
    elif not missing and status == "offen":
        stale.append(f"line {row.line}: status 'offen' but all clips exist")
    return missing


def progress(list_text: str, anims_dir: Path, *more_dirs: Path) -> Progress:
    """`anims_dir` and `more_dirs` (e.g. monsters/) are searched for clips."""
    tables = parse_tables(list_text)
    rows = parse_prio_a(list_text)
    present = collect_clips(anims_dir)
    for folder in more_dirs:
        for name, path in collect_clips(folder).items():
            present.setdefault(name, path)
    result = Progress(rows=rows, present=present)
    for row in rows:
        result.missing += _check_status(row, result.present, result.stale)
    for heading, other in tables:
        if heading.startswith("Prio A"):
            continue
        result.sections[heading] = other
        for row in other:
            _check_status(row, result.present, result.stale)
    return result
