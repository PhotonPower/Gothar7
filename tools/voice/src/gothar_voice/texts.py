"""Translations of the content texts of the scripts: assets/source/text/script.<lang>.json (M14 part G).

The German texts stay inline in Lua (owner decision E2); this file holds their translation into another language.
``scan_texts`` collects every text the player reads or hears, with a key:

* spoken lines with their voice key (``luascan``: dialogue ``<info>_NN``); shouts once per occasion and variant
  ``svm.<occasion>_NN`` (the same text for every voice);
* fields of instances (``Item``, ``Npc``, ``Mob``, ``Quest``, ``Spell`` ...): ``name``, ``description``, ``text``,
  ``title`` -> ``<instance>.<field>``;
* the same fields in other tables (``Guilds``) and every value of a ``*_names`` table (``Teaching.talent_names``)
  -> ``<file>.<field>_NN``;
* the text arguments of ``quest_start``/``quest_entry``/``quest_success``/``quest_fail``, ``note``,
  ``set_chapter``, ``notice``, ``screen_message`` -> ``<info or file>.<function>_NN``.

Only literal strings that read like text count (a space, a capital or a non-ASCII letter: no identifiers). The
engine finds a translation by the German text (``de``), the key is for bookkeeping; texts built at run time come
from ``text/ui.<lang>.toml`` (Lua ``ui_text``) instead.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .luascan import Token, _arguments, _closing, scan, tokenize, voices_of

SCHEMA = 1
TEXT_FIELDS = ("name", "description", "text", "title")
STATUSES = ("offen", "entwurf", "abgenommen", "veraltet")
CALLS = {  # function -> indices of its text arguments
    "quest_start": (1,),
    "quest_entry": (1,),
    "quest_success": (1,),
    "quest_fail": (1,),
    "note": (0, 1),
    "set_chapter": (1,),
    "notice": (0,),
    "screen_message": (0,),
    "choice": (0,),  # outside an Info (inside: spoken lines with voice keys)
    "say": (1,),
}
SPOKEN_CALLS = ("choice", "say")


def is_text(value: str) -> bool:
    """Reads like text, not like an identifier, path or key."""
    if not value.strip():
        return False
    return " " in value or any(c.isupper() for c in value) or any(ord(c) > 127 for c in value)


@dataclass(frozen=True)
class ScannedText:
    key: str
    de: str
    context: str  # file ("items/food.lua")


@dataclass
class TextScan:
    texts: dict[str, ScannedText] = field(default_factory=dict)

    def add(self, key: str, de: str, context: str) -> None:
        if key not in self.texts:
            self.texts[key] = ScannedText(key, de, context)


class _Numbering:
    """``<base>_NN`` per distinct text of a base (01, 02 ...)."""

    def __init__(self) -> None:
        self._seen: dict[str, dict[str, str]] = {}

    def key(self, base: str, text: str) -> str:
        texts = self._seen.setdefault(base, {})
        if text not in texts:
            texts[text] = f"{base}_{len(texts) + 1:02d}"
        return texts[text]


def _literal(arg: list[Token]) -> str | None:
    return arg[0].value if len(arg) == 1 and arg[0].kind == "string" else None


def _scan_file(path: Path, rel: str, result: TextScan) -> None:
    tokens = tokenize(path.read_text(encoding="utf-8"))
    stem = rel.removesuffix(".lua").replace("/", ".")
    numbering = _Numbering()
    # Instances: their top-level text fields; Info descriptions are spoken lines (voice keys).
    spans: list[tuple[int, int, str, str]] = []  # (open, close, kind, name)
    covered: set[int] = set()  # token indices of strings already keyed
    for k in range(len(tokens) - 2):
        t = tokens[k]
        if (
            t.kind == "name"
            and t.value[:1].isupper()
            and tokens[k + 1].kind == "string"
            and tokens[k + 2].value == "{"
            and (k == 0 or tokens[k - 1].value not in (".", ":"))
        ):
            spans.append((k + 2, _closing(tokens, k + 2), t.value, tokens[k + 1].value))
    for open_brace, close_brace, kind, name in spans:
        depth = 0
        j = open_brace
        while j < close_brace:
            tj = tokens[j]
            if tj.kind == "op" and tj.value in "({[":
                depth += 1
            elif tj.kind == "op" and tj.value in ")}]":
                depth -= 1
            elif (
                depth == 1
                and tj.kind == "name"
                and tj.value in TEXT_FIELDS
                and tokens[j + 1].value == "="
                and tokens[j + 2].kind == "string"
                and tokens[j + 3].value in (",", ";", "}")
            ):
                if kind != "Info" and is_text(tokens[j + 2].value):
                    result.add(f"{name}.{tj.value}", tokens[j + 2].value, rel)
                covered.add(j + 2)
            j += 1

    def span_of(index: int) -> tuple[int, int, str, str] | None:
        inner = [s for s in spans if s[0] < index < s[1]]
        return min(inner, key=lambda s: s[1] - s[0]) if inner else None

    def context_of(index: int) -> str:
        span = span_of(index)
        return span[3] if span else stem

    def in_info(index: int) -> bool:
        return any(s[2] == "Info" for s in spans if s[0] < index < s[1])

    for k, t in enumerate(tokens):
        # Calls with text arguments.
        if (
            t.kind == "name"
            and t.value in CALLS
            and k + 1 < len(tokens)
            and tokens[k + 1].value == "("
            and (k == 0 or tokens[k - 1].value not in (".", ":", "function"))
            and not (t.value in SPOKEN_CALLS and in_info(k))
        ):
            args = _arguments(tokens, k + 1)
            for index in CALLS[t.value]:
                text = _literal(args[index]) if index < len(args) else None
                if text is not None and is_text(text):
                    base = f"{context_of(k)}.{t.value}"
                    result.add(numbering.key(base, text), text, rel)
        # Text fields of other tables ("Guilds.wolf = { name = "Wölfe" }").
        elif (
            t.kind == "name"
            and t.value in TEXT_FIELDS
            and k + 3 < len(tokens)
            and tokens[k + 1].value == "="
            and tokens[k + 2].kind == "string"
            and tokens[k + 3].value in (",", ";", "}")
            and k + 2 not in covered
            and not in_info(k)
            and is_text(tokens[k + 2].value)
        ):
            text = tokens[k + 2].value
            result.add(numbering.key(f"{stem}.{t.value}", text), text, rel)
        # Every value of a "*_names" table ("talent_names = { picklock = "Schlösser öffnen" }").
        elif (
            t.kind == "name"
            and t.value.endswith("_names")
            and k + 2 < len(tokens)
            and tokens[k + 1].value == "="
            and tokens[k + 2].value == "{"
        ):
            end = _closing(tokens, k + 2)
            for j in range(k + 3, end):
                if (
                    tokens[j].kind == "string"
                    and tokens[j + 1].value in (",", ";", "}")
                    and is_text(tokens[j].value)
                ):
                    result.add(numbering.key(f"{stem}.{t.value}", tokens[j].value), tokens[j].value, rel)


def scan_texts(scripts: Path) -> TextScan:
    """Every content text of the scripts with its key (see the module docstring)."""
    result = TextScan()
    spoken = scan(scripts)
    for key, line in spoken.lines.items():
        if line.speaker != "svm":
            result.add(key, line.text, line.context)
    # Shouts: once per occasion and variant (each voice says the same text).
    if voices_of(scripts):
        for key, line in sorted(spoken.lines.items()):
            if line.speaker == "svm":
                occasion_variant = key.split("_", 3)[3]  # svm_<voice>_<g>_<occasion>_NN
                result.add(f"svm.{occasion_variant}", line.text, "data/shouts.lua")
    for path in sorted(scripts.rglob("*.lua")):
        _scan_file(path, path.relative_to(scripts).as_posix(), result)
    return result


@dataclass
class Text:
    key: str
    de: str  # the German text it translates (inline in the scripts)
    text: str = ""  # the translation; empty: missing
    context: str = ""
    status: str = (
        "offen"  # offen (no translation), entwurf (draft), abgenommen (approved), veraltet (German changed)
    )

    def validate(self) -> list[str]:
        errors = []
        if self.status not in STATUSES:
            errors.append(f"{self.key}: status muss {', '.join(STATUSES)} sein")
        if self.text and self.status == "offen":
            errors.append(f"{self.key}: Übersetzung vorhanden, aber status offen")
        if not self.text and self.status != "offen":
            errors.append(f"{self.key}: keine Übersetzung, aber status {self.status}")
        return errors


@dataclass
class TextReport:
    added: list[str] = field(default_factory=list)
    changed: list[str] = field(default_factory=list)  # German changed: veraltet
    removed: list[str] = field(default_factory=list)
    carried: list[str] = field(default_factory=list)  # new key, the same German text as a removed one

    @property
    def dirty(self) -> bool:
        return bool(self.added or self.changed or self.removed)


class ScriptTexts:
    def __init__(self, root: Path, lang: str) -> None:
        self.root = Path(root)  # assets/source/text
        self.lang = lang
        self.texts: dict[str, Text] = {}

    @property
    def path(self) -> Path:
        return self.root / f"script.{self.lang}.json"

    @classmethod
    def load(cls, root: Path, lang: str) -> ScriptTexts:
        db = cls(root, lang)
        if db.path.exists():
            raw = json.loads(db.path.read_text(encoding="utf-8"))
            if raw.get("schema") != SCHEMA:
                raise ValueError(f"{db.path}: unbekanntes Schema {raw.get('schema')}")
            for key, d in raw["texts"].items():
                db.texts[key] = Text(key=key, **d)
        return db

    @classmethod
    def languages(cls, root: Path) -> list[str]:
        return sorted(p.name.split(".")[1] for p in Path(root).glob("script.*.json"))

    def validate(self) -> list[str]:
        return [e for t in self.texts.values() for e in t.validate()]

    def problems(self) -> list[str]:
        """Missing and outdated translations."""
        out = []
        for key in sorted(self.texts):
            t = self.texts[key]
            if t.status == "offen":
                out.append(f"{key}: Übersetzung fehlt ({t.de!r})")
            elif t.status == "veraltet":
                out.append(f"{key}: deutscher Text geändert, Übersetzung prüfen ({t.de!r})")
        return out

    def save(self) -> None:
        errors = self.validate()
        if errors:
            raise ValueError("\n".join(errors))
        out: dict = {"schema": SCHEMA, "lang": self.lang, "texts": {}}
        for key in sorted(self.texts):
            d = asdict(self.texts[key])
            d.pop("key")
            out["texts"][key] = d
        self.root.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        tmp.replace(self.path)

    def merge(self, scanned: TextScan) -> TextReport:
        """Brings the file in line with the scripts: new texts are added (offen; a translation of the same German
        text elsewhere is carried over), a changed German text keeps its translation as ``veraltet``, texts no
        longer in the scripts are removed."""
        report = TextReport()
        by_german = {t.de: t for t in self.texts.values() if t.text and t.status != "veraltet"}
        for key in [k for k in self.texts if k not in scanned.texts]:
            del self.texts[key]
            report.removed.append(key)
        for key, s in scanned.texts.items():
            t = self.texts.get(key)
            if t is None:
                known = by_german.get(s.de)
                if known is not None:
                    self.texts[key] = Text(key, s.de, known.text, s.context, known.status)
                    report.carried.append(key)
                else:
                    self.texts[key] = Text(key, s.de, context=s.context)
                report.added.append(key)
                continue
            if t.de != s.de:
                t.de = s.de
                if t.text:
                    t.status = "veraltet"
                report.changed.append(key)
            t.context = s.context
        return report
