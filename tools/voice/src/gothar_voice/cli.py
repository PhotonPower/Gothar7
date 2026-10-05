"""gothar-voice app | check | scan [--write]"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from .db import VoiceDb, data_root
from .luascan import scan


def repo_root() -> Path:
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / "CLAUDE.md").exists() and (p / "assets").is_dir():
            return p
    return Path.cwd()


def load_db(root: Path, lang: str) -> VoiceDb:
    """The database of the checkout; the takes below DATA_ROOT/voice/takes where it is set."""
    takes = data_root(root)
    return VoiceDb.load(root / "assets/source/voice", lang, takes / "voice/takes" if takes else None)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="gothar-voice")
    ap.add_argument("command", choices=["app", "check", "scan"])
    ap.add_argument("--lang", default="de")
    ap.add_argument("--write", action="store_true", help="scan: bring the database in line with the scripts")
    args = ap.parse_args(argv)
    root = repo_root()
    if args.command == "app":
        app = Path(__file__).with_name("app.py")
        return subprocess.call(
            [sys.executable, "-m", "streamlit", "run", str(app), "--", "--lang", args.lang]
        )
    db = load_db(root, args.lang)
    if args.command == "check":
        errors = db.validate()
        for line in db.lines.values():
            if line.selected is not None and not db.final_path(line.key).exists():
                errors.append(f"{line.key}: gewählter Take fehlt ({db.final_path(line.key)})")
        report = db.merge(scan(root / "game/scripts"))
        if report.dirty:
            errors.append(
                f"Datenbank passt nicht zu den Skripten ({len(report.added)} neu, {len(report.changed)} geändert, "
                f"{len(report.orphaned)} verwaist, {len(report.updated)} aktualisiert): gothar-voice scan --write"
            )
        print("\n".join(errors) or f"OK: {len(db.lines)} Zeilen")
        return 1 if errors else 0
    result = scan(root / "game/scripts")
    report = db.merge(result)
    for title, keys in (
        ("neu", report.added),
        ("Text geändert (wieder offen)", report.changed),
        ("verwaist", report.orphaned),
        ("aktualisiert", report.updated),
    ):
        for key in keys:
            print(f"{title}\t{key}\t{db.lines[key].text}")
    for where in result.skipped:
        print(f"ohne festen Text (kein Schlüssel)\t{where}")
    if args.write:
        db.save()
        print(f"{db.db_path}: {len(db.lines)} Zeilen")
        return 0
    return 1 if report.dirty else 0


if __name__ == "__main__":
    raise SystemExit(main())
