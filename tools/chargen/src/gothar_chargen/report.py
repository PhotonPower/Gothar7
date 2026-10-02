"""Compares docs/design/animation-list.md with the clips in anims/human/*.glb (progress report).

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
    rows: list[ListRow]
    present: dict[str, Path]  # clip name -> file
    missing: list[str] = field(default_factory=list)
    stale: list[str] = field(default_factory=list)  # list status contradicts the files

    @property
    def listed(self) -> list[str]:
        return [n for r in self.rows for n in r.names]

    @property
    def extra(self) -> list[str]:
        listed = set(self.listed)
        return sorted(n for n in self.present if n not in listed)

    def to_dict(self) -> dict:
        return {
            "prio_a": {"listed": len(self.listed), "present": len(self.listed) - len(self.missing)},
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


def parse_prio_a(text: str) -> list[ListRow]:
    """Rows of the first table after the "## Prio A" heading: (names, status)."""
    rows: list[ListRow] = []
    in_section = False
    header: list[str] | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("## "):
            if in_section:
                break
            in_section = line.startswith("## Prio A")
            continue
        if not in_section or not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = [c.lower() for c in cells]
            continue
        if set(line) <= set("|-: "):
            continue
        try:
            name_col, status_col = header.index("name"), header.index("status")
        except ValueError as e:
            raise ReportError("Prio A table needs 'Name' and 'Status' columns") from e
        rows.append(ListRow(tuple(expand_names(cells[name_col])), cells[status_col], number))
    if not rows:
        raise ReportError("no 'Prio A' table found")
    return rows


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


def progress(list_text: str, anims_dir: Path) -> Progress:
    rows = parse_prio_a(list_text)
    result = Progress(rows=rows, present=collect_clips(anims_dir))
    for row in rows:
        missing = [n for n in row.names if n not in result.present]
        result.missing += missing
        status = row.status.split()[0].lower() if row.status else ""
        if missing and (status.startswith("platzhalter") or status == "fertig"):
            result.stale.append(f"line {row.line}: status '{status}' but missing {missing}")
        elif not missing and status == "offen":
            result.stale.append(f"line {row.line}: status 'offen' but all clips exist")
    return result
