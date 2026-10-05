"""Voice-line database: one JSON file per language; the chosen take of each line next to it.

Layout:
    assets/source/voice/lines.de.json        database (versioned)
    assets/source/voice/de/<key>.wav         chosen take (versioned; the cooker makes voice/de/<key>.ogg)
    DATA_ROOT/voice/takes/de/<key>__tNN.wav  every take of a line (local only, project owner's decision)

The keys come from the scripts (``luascan``); the database adds direction, status and takes.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import tomllib
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path

from .luascan import ScanResult

SCHEMA = 1
KEY_RE = re.compile(r"^[a-z][a-z0-9_]*$")
GENDERS = ("m", "f")
STATUSES = ("offen", "aufgenommen", "abgenommen")
DATA_ROOT_ENV = "GOTHAR_DATA_ROOT"


@dataclass
class Take:
    file: str  # file name below the takes folder, e.g. "dia_x_01__t02.wav"
    note: str = ""  # e.g. "etwas langsamer", "TTS: stability 0.3"
    added: str = ""


@dataclass
class Line:
    key: str
    text: str
    speaker: str = ""  # Npc instance ("npc_gate_guard"), "hero" or "svm" (shouts)
    gender: str = "m"
    voice: str = ""  # voice group (guild or Npc.voice): which TTS voice
    direction: str = ""  # stage direction
    context: str = ""  # where it is said ("dialogs/gate_guard.lua")
    status: str = "offen"
    orphan: bool = False  # no longer in the scripts (kept, only marked)
    selected: int | None = None  # index into takes
    takes: list[Take] = field(default_factory=list)

    def validate(self) -> list[str]:
        errors = []
        if not KEY_RE.match(self.key):
            errors.append(f"{self.key}: Schlüssel nur a-z, 0-9, _ (beginnt mit Buchstabe)")
        if self.gender not in GENDERS:
            errors.append(f"{self.key}: gender muss m oder f sein")
        if self.status not in STATUSES:
            errors.append(f"{self.key}: status muss {', '.join(STATUSES)} sein")
        if self.selected is not None and not 0 <= self.selected < len(self.takes):
            errors.append(f"{self.key}: selected zeigt auf keinen Take")
        return errors


@dataclass
class MergeReport:
    added: list[str] = field(default_factory=list)
    changed: list[str] = field(default_factory=list)  # text changed: open again
    orphaned: list[str] = field(default_factory=list)
    updated: list[str] = field(default_factory=list)  # speaker, gender, voice or context changed

    @property
    def dirty(self) -> bool:
        """The database does not match the scripts (the CI check fails)."""
        return bool(self.added or self.changed or self.orphaned or self.updated)


def data_root(repo: Path) -> Path | None:
    """``$GOTHAR_DATA_ROOT``, else ``paths.data_root`` of ``tools/worldgen/config/local.toml`` (one machine
    config for all tools); None if neither is there."""
    env = os.environ.get(DATA_ROOT_ENV)
    if env:
        return Path(env)
    local = repo / "tools/worldgen/config/local.toml"
    if local.exists():
        root = tomllib.loads(local.read_text(encoding="utf-8")).get("paths", {}).get("data_root")
        if root:
            return Path(root)
    return None


class VoiceDb:
    def __init__(self, root: Path, lang: str = "de", takes_root: Path | None = None) -> None:
        self.root = Path(root)  # assets/source/voice
        self.lang = lang
        self.takes_root = takes_root  # DATA_ROOT/voice/takes (None: no takes on this machine)
        self.lines: dict[str, Line] = {}

    # --- paths ---------------------------------------------------------------------------------------------
    @property
    def db_path(self) -> Path:
        return self.root / f"lines.{self.lang}.json"

    @property
    def lang_dir(self) -> Path:
        return self.root / self.lang

    @property
    def takes_dir(self) -> Path:
        if self.takes_root is None:
            raise RuntimeError(
                f"kein DATA_ROOT: ${DATA_ROOT_ENV} setzen oder paths.data_root in "
                "tools/worldgen/config/local.toml (Takes liegen nur lokal)"
            )
        return self.takes_root / self.lang

    def final_path(self, key: str) -> Path:
        return self.lang_dir / f"{key}.wav"

    def take_path(self, take: Take) -> Path:
        return self.takes_dir / take.file

    # --- load/save -----------------------------------------------------------------------------------------
    @classmethod
    def load(cls, root: Path, lang: str = "de", takes_root: Path | None = None) -> VoiceDb:
        db = cls(root, lang, takes_root)
        if db.db_path.exists():
            raw = json.loads(db.db_path.read_text(encoding="utf-8"))
            if raw.get("schema") != SCHEMA:
                raise ValueError(f"{db.db_path}: unbekanntes Schema {raw.get('schema')}")
            for key, d in raw["lines"].items():
                takes = [Take(**t) for t in d.pop("takes", [])]
                db.lines[key] = Line(key=key, takes=takes, **d)
        return db

    def save(self) -> None:
        errors = self.validate()
        if errors:
            raise ValueError("\n".join(errors))
        out: dict = {"schema": SCHEMA, "lang": self.lang, "lines": {}}
        for key in sorted(self.lines):
            d = asdict(self.lines[key])
            d.pop("key")
            out["lines"][key] = d
        self.root.mkdir(parents=True, exist_ok=True)
        tmp = self.db_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        tmp.replace(self.db_path)  # atomic: no half JSON after a crash

    def validate(self) -> list[str]:
        return [e for line in self.lines.values() for e in line.validate()]

    # --- edit ----------------------------------------------------------------------------------------------
    def add_line(self, line: Line) -> None:
        if line.key in self.lines:
            raise KeyError(f"{line.key} gibt es schon")
        errors = line.validate()
        if errors:
            raise ValueError("\n".join(errors))
        self.lines[line.key] = line

    def add_take(self, key: str, data: bytes, note: str = "", select: bool | None = None) -> Take:
        """Stores a WAV as the next take (the tool names the file)."""
        if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
            raise ValueError("keine WAV-Datei (RIFF/WAVE-Kopf fehlt)")
        line = self.lines[key]
        n = 1 + max((_take_number(t.file) for t in line.takes), default=0)
        take = Take(
            file=f"{key}__t{n:02d}.wav", note=note, added=datetime.now(UTC).strftime("%Y-%m-%d %H:%M")
        )
        path = self.take_path(take)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        line.takes.append(take)
        if line.status == "offen":
            line.status = "aufgenommen"
        if select or (select is None and line.selected is None):
            self.select_take(key, len(line.takes) - 1)
        return take

    def select_take(self, key: str, index: int) -> None:
        line = self.lines[key]
        line.selected = index
        dst = self.final_path(key)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.take_path(line.takes[index]), dst)

    def delete_take(self, key: str, index: int) -> None:
        line = self.lines[key]
        take = line.takes.pop(index)
        self.take_path(take).unlink(missing_ok=True)
        if line.selected == index:
            line.selected = None
            self.final_path(key).unlink(missing_ok=True)
        elif line.selected is not None and line.selected > index:
            line.selected -= 1
        if not line.takes:
            line.status = "offen"

    # --- the scripts ---------------------------------------------------------------------------------------
    def merge(self, scanned: ScanResult) -> MergeReport:
        """Brings the database in line with the scripts. New texts are added (open); a changed text opens the
        line again (its chosen take no longer fits and is removed, the takes stay); lines no longer in the
        scripts are only marked ``orphan``. Direction, status and takes of unchanged lines stay."""
        report = MergeReport()
        for key, s in scanned.lines.items():
            line = self.lines.get(key)
            if line is None:
                self.lines[key] = Line(key, s.text, s.speaker, s.gender, s.voice, context=s.context)
                report.added.append(key)
                continue
            if line.text != s.text:
                line.text = s.text
                line.status = "offen"
                if line.selected is not None:
                    line.selected = None
                    self.final_path(key).unlink(missing_ok=True)
                report.changed.append(key)
            if line.orphan:
                line.orphan = False
                report.updated.append(key)
            if (line.speaker, line.gender, line.voice, line.context) != (
                s.speaker,
                s.gender,
                s.voice,
                s.context,
            ):
                line.speaker, line.gender, line.voice, line.context = s.speaker, s.gender, s.voice, s.context
                if key not in report.changed:
                    report.updated.append(key)
        for key, line in self.lines.items():
            if key not in scanned.lines and not line.orphan:
                line.orphan = True
                report.orphaned.append(key)
        return report


def _take_number(file: str) -> int:
    m = re.search(r"__t(\d+)\.wav$", file)
    return int(m.group(1)) if m else 0
