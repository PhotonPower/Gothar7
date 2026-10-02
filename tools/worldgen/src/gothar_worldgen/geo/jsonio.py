"""JSON output shared by the import steps: readable header, one record per line."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def write_json_records(path: Path, header: dict[str, Any], lists: dict[str, list[Any]]) -> None:
    """Write ``header`` pretty-printed, then each list with one compact record per line.

    Keeps large files diff-friendly and still valid JSON (``json.loads`` gives header | lists).
    """
    head = json.dumps(header, indent=2, ensure_ascii=False).removesuffix("\n}")
    blocks = []
    for key, records in lists.items():
        lines = [json.dumps(r, ensure_ascii=False, separators=(",", ":")) for r in records]
        items = "\n" + ",\n".join(f"    {line}" for line in lines) + "\n  " if lines else ""
        blocks.append(f'  "{key}": [{items}]')
    sep = ",\n" if header else ""
    text = f"{head}{sep}" + ",\n".join(blocks) + "\n}\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
