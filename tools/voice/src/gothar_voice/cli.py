"""gothar-voice app | check | scan"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from .db import VoiceDb


def repo_root() -> Path:
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / "CLAUDE.md").exists() and (p / "assets").is_dir():
            return p
    return Path.cwd()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="gothar-voice")
    ap.add_argument("command", choices=["app", "check", "scan"])
    ap.add_argument("--lang", default="de")
    args = ap.parse_args(argv)
    root = repo_root()
    if args.command == "app":
        app = Path(__file__).with_name("app.py")
        return subprocess.call(
            [sys.executable, "-m", "streamlit", "run", str(app), "--", "--lang", args.lang]
        )
    db = VoiceDb.load(root / "assets/source/voice", args.lang)
    if args.command == "check":
        errors = db.validate()
        for line in db.lines.values():
            if line.selected is not None and not db.final_path(line.key).exists():
                errors.append(f"{line.key}: gewählter Take fehlt ({db.final_path(line.key)})")
        print("\n".join(errors) or f"OK: {len(db.lines)} Zeilen")
        return 1 if errors else 0
    missing = db.scan_scripts(root / "game/scripts")
    for key, where in missing.items():
        print(f"{key}\t{where}")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
