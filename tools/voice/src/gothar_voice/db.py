"""Sprechtext-Datenbank: eine JSON-Datei je Sprache, Takes als WAV daneben.

Layout (relativ zur Repo-Wurzel):
    assets/source/voice/lines.de.json              Datenbank
    assets/source/voice/de/takes/<key>__tNN.wav    alle Takes einer Zeile
    assets/source/voice/de/<key>.wav               gewählter Take (das liest der Cooker -> voice/de/<key>.ogg)
"""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path

SCHEMA = 1
KEY_RE = re.compile(r"^[a-z][a-z0-9_]*$")
GENDERS = ("m", "f")
STATUSES = ("offen", "aufgenommen", "abgenommen")

# say(self, other, "key") / npc_say(npc, "key") / npc_shout(npc, "key") mit Schlüssel statt Freitext
_SAY_RE = re.compile(r'\b(?:say|npc_say|npc_shout)\s*\([^)"]*"([a-z][a-z0-9_]*)"\s*\)')


@dataclass
class Take:
    file: str  # relativ zum Sprachordner, z. B. "takes/dia_x_01__t02.wav"
    note: str = ""  # z. B. "etwas langsamer", "TTS: stability 0.3"
    added: str = ""


@dataclass
class Line:
    key: str
    text: str
    speaker: str = ""  # NPC-Instanz ("npc_gate_guard"), "hero" oder "svm" (allgemeine Zurufe)
    gender: str = "m"
    voice: int = 0  # Stimmnummer (Npc.voice); 0 = noch keine
    direction: str = ""  # Regieanweisung
    context: str = ""  # wo/wann gesagt (Datei, Situation)
    status: str = "offen"
    selected: int | None = None  # Index in takes
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


class VoiceDb:
    def __init__(self, root: Path, lang: str = "de") -> None:
        self.root = Path(root)  # assets/source/voice
        self.lang = lang
        self.lines: dict[str, Line] = {}

    # --- Pfade -------------------------------------------------------------------------------------------
    @property
    def db_path(self) -> Path:
        return self.root / f"lines.{self.lang}.json"

    @property
    def lang_dir(self) -> Path:
        return self.root / self.lang

    def final_path(self, key: str) -> Path:
        return self.lang_dir / f"{key}.wav"

    def take_path(self, take: Take) -> Path:
        return self.lang_dir / take.file

    # --- Laden/Speichern ---------------------------------------------------------------------------------
    @classmethod
    def load(cls, root: Path, lang: str = "de") -> VoiceDb:
        db = cls(root, lang)
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
        out = {"schema": SCHEMA, "lang": self.lang, "lines": {}}
        for key in sorted(self.lines):
            d = asdict(self.lines[key])
            d.pop("key")
            out["lines"][key] = d
        self.root.mkdir(parents=True, exist_ok=True)
        tmp = self.db_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        tmp.replace(self.db_path)  # atomar: kein halbes JSON nach Absturz

    def validate(self) -> list[str]:
        return [e for line in self.lines.values() for e in line.validate()]

    # --- Bearbeiten --------------------------------------------------------------------------------------
    def add_line(self, line: Line) -> None:
        if line.key in self.lines:
            raise KeyError(f"{line.key} gibt es schon")
        errors = line.validate()
        if errors:
            raise ValueError("\n".join(errors))
        self.lines[line.key] = line

    def add_take(self, key: str, data: bytes, note: str = "", select: bool | None = None) -> Take:
        """Speichert eine WAV als nächsten Take (Dateiname vergibt das Werkzeug)."""
        if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
            raise ValueError("keine WAV-Datei (RIFF/WAVE-Kopf fehlt)")
        line = self.lines[key]
        n = 1 + max((_take_number(t.file) for t in line.takes), default=0)
        take = Take(
            file=f"takes/{key}__t{n:02d}.wav",
            note=note,
            added=datetime.now(UTC).strftime("%Y-%m-%d %H:%M"),
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

    # --- Abgleich mit den Skripten -----------------------------------------------------------------------
    def scan_scripts(self, scripts_dir: Path) -> dict[str, str]:
        """Schlüssel aus say(...)-Aufrufen, die in der Datenbank fehlen -> Fundstelle."""
        missing: dict[str, str] = {}
        for path in sorted(Path(scripts_dir).rglob("*.lua")):
            for no, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                for key in _SAY_RE.findall(text):
                    if key not in self.lines and key not in missing:
                        missing[key] = f"{path.relative_to(scripts_dir).as_posix()}:{no}"
        return missing


def _take_number(file: str) -> int:
    m = re.search(r"__t(\d+)\.wav$", file)
    return int(m.group(1)) if m else 0
