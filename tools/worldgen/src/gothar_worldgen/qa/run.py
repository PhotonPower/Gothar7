"""``gothar-worldgen check <site>``: previews + plausibility report for ``work/<site>/``."""

from __future__ import annotations

import json
from pathlib import Path
from typing import TextIO

from gothar_worldgen.qa.checks import FAIL, OK, WARN, overall_status, report_document, run_checks
from gothar_worldgen.qa.preview import write_previews
from gothar_worldgen.qa.workdata import load_work

_MARK = {OK: "ok  ", WARN: "WARN", FAIL: "FAIL"}


def run_qa(site: str, work_dir: Path, out: TextIO) -> str:
    """Write ``report.json``, ``preview.png``, ``preview_core.png``; return the overall status."""
    data = load_work(work_dir)
    results = run_checks(data)
    report = report_document(site, results)
    (work_dir / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n"
    )
    previews = write_previews(data, work_dir)
    print(f"  checks: {report['status']}", file=out)
    for r in results:
        print(f"    [{_MARK[r.status]}] {r.id}: {r.message}", file=out)
    print(f"  preview: {', '.join(p.name for p in previews)}, report.json", file=out)
    return overall_status(results)
