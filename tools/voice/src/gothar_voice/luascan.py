"""Spoken texts from the game scripts (Lua): the single source of the voice keys.

Texts are inline in Lua (owner decision E2); the keys come from here, in source order:

* Dialogue: inside ``Info "<info>" { ... }`` first its ``description`` (chosen from the menu, the hero says it),
  then every literal text of ``say(npc, "...")`` (the Info's NPC),
  ``say("hero", "...")`` (the hero), ``choice("...", fn)`` (the hero says the chosen answer) and the reply table
  of ``teach_menu(npc, offers, { ok = "...", ... })`` (the NPC) gets ``<info>_NN`` (01, 02, ... per distinct text).
* Shouts: every text of ``Shouts`` (``data/shouts.lua``) for every voice of ``Voices`` (``data/voices.lua``):
  ``svm_<voice>_<m|f>_<occasion>_NN`` (NN = variant).

The engine finds a key again from (Info, text) resp. the shout's text (``docs/modules/audio.md`` "Sprache").
Only literal strings count; texts built at run time are reported, not keyed.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

HERO = "hero"
SVM = "svm"


@dataclass(frozen=True)
class Token:
    kind: str  # "name", "string", "number", "op"
    value: str
    line: int


@dataclass
class ScannedLine:
    key: str
    text: str
    speaker: str  # Npc instance, "hero" or "svm"
    gender: str  # "m" / "f"
    voice: str  # voice group (shouts; for dialogue the speaker's)
    context: str  # file: "dialogs/gate_guard.lua" (no line: editing elsewhere must not change it)


@dataclass
class ScanResult:
    lines: dict[str, ScannedLine] = field(default_factory=dict)
    skipped: list[str] = field(default_factory=list)  # say/choice without a literal text: "file:line"


_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "a": "\a", "b": "\b", "f": "\f"}
_OPS = ("==", "~=", "<=", ">=", "..", "::")


def tokenize(source: str) -> list[Token]:
    """Lua tokens without comments; long strings and the common escapes, enough for content scripts."""
    tokens: list[Token] = []
    i, line, n = 0, 1, len(source)
    while i < n:
        c = source[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif source.startswith("--", i):
            m = re.match(r"--\[(=*)\[", source[i:])
            if m:
                end = source.find("]" + m.group(1) + "]", i)
                end = n if end < 0 else end + len(m.group(1)) + 2
            else:
                end = source.find("\n", i)
                end = n if end < 0 else end
            line += source.count("\n", i, end)
            i = end
        elif c == "[" and re.match(r"\[=*\[", source[i:]):
            m = re.match(r"\[(=*)\[", source[i:])
            assert m
            start = i + len(m.group(0))
            end = source.find("]" + m.group(1) + "]", start)
            end = n if end < 0 else end
            text = source[start:end]
            text = text.removeprefix(chr(10))  # Lua drops a newline right after the opening brackets
            tokens.append(Token("string", text, line))
            line += source.count("\n", i, end)
            i = end + len(m.group(1)) + 2
        elif c in "\"'":
            out: list[str] = []
            j = i + 1
            while j < n and source[j] != c:
                if source[j] == "\\" and j + 1 < n:
                    out.append(_ESCAPES.get(source[j + 1], source[j + 1]))
                    j += 2
                else:
                    out.append(source[j])
                    j += 1
            tokens.append(Token("string", "".join(out), line))
            i = j + 1
        elif c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", source[i:])
            assert m
            tokens.append(Token("name", m.group(0), line))
            i += len(m.group(0))
        elif c.isdigit():
            m = re.match(r"[0-9][0-9a-fA-FxX._]*", source[i:])
            assert m
            tokens.append(Token("number", m.group(0), line))
            i += len(m.group(0))
        else:
            op = next((o for o in _OPS if source.startswith(o, i)), c)
            tokens.append(Token("op", op, line))
            i += len(op)
    return tokens


def _closing(tokens: list[Token], start: int) -> int:
    """Index of the bracket closing the one at ``start`` ("(", "{" or "[")."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    depth = 0
    for k in range(start, len(tokens)):
        t = tokens[k]
        if t.kind == "op" and t.value in pairs:
            depth += 1
        elif t.kind == "op" and t.value in pairs.values():
            depth -= 1
            if depth == 0:
                return k
    return len(tokens) - 1


def _arguments(tokens: list[Token], open_paren: int) -> list[list[Token]]:
    """The top-level arguments of the call whose "(" is at ``open_paren``."""
    end = _closing(tokens, open_paren)
    args: list[list[Token]] = [[]]
    k = open_paren + 1
    while k < end:
        t = tokens[k]
        if t.kind == "op" and t.value in "({[":
            close = _closing(tokens, k)
            args[-1].extend(tokens[k : close + 1])
            k = close + 1
            continue
        if t.kind == "op" and t.value == ",":
            args.append([])
        else:
            args[-1].append(t)
        k += 1
    return [a for a in args if a]


def parse_value(tokens: list[Token], k: int = 0) -> tuple[object, int]:
    """A literal value (string, number, true/false/nil, table) at ``k``: (value, index after it).
    Tables become dicts (fields) with array entries under 1, 2, ...; anything else is None."""
    t = tokens[k]
    if t.kind == "string":
        return t.value, k + 1
    if t.kind == "number":
        try:
            return float(t.value), k + 1
        except ValueError:
            return None, k + 1
    if t.kind == "name" and t.value in ("true", "false", "nil"):
        return {"true": True, "false": False, "nil": None}[t.value], k + 1
    if t.kind == "op" and t.value == "{":
        end = _closing(tokens, k)
        table: dict[object, object] = {}
        index = 1
        j = k + 1
        while j < end:
            if tokens[j].kind == "op" and tokens[j].value in ",;":
                j += 1
                continue
            key: object = None
            if tokens[j].kind == "name" and j + 1 < end and tokens[j + 1].value == "=":
                key = tokens[j].value
                j += 2
            elif tokens[j].value == "[" and tokens[j].kind == "op":
                close = _closing(tokens, j)
                key, _ = parse_value(tokens, j + 1)
                j = close + 2  # past "] ="
            value, j = _skip_expression(tokens, j, end)
            if key is None:
                table[index] = value
                index += 1
            else:
                table[key] = value
        return table, end + 1
    value, j = _skip_expression(tokens, k, len(tokens))
    return value, j


def _skip_expression(tokens: list[Token], j: int, end: int) -> tuple[object, int]:
    """The value at ``j`` if it is a literal on its own, else None; index of the "," / ";" / end after it."""
    start = j
    while j < end and not (tokens[j].kind == "op" and tokens[j].value in ",;"):
        if tokens[j].kind == "op" and tokens[j].value in "({[":
            j = _closing(tokens, j) + 1
        elif tokens[j].kind == "name" and tokens[j].value == "function":
            j = _function_end(tokens, j) + 1
        else:
            j += 1
    if j - start == 1 or (
        tokens[start].kind == "op" and tokens[start].value == "{" and _closing(tokens, start) == j - 1
    ):
        if tokens[start].kind == "op" and tokens[start].value == "{":
            value, _ = parse_value(tokens, start)
            return value, j
        if tokens[start].kind in ("string", "number") or tokens[start].value in ("true", "false", "nil"):
            value, _ = parse_value(tokens, start)
            return value, j
    return None, j


def _function_end(tokens: list[Token], k: int) -> int:
    """Index of the ``end`` closing the ``function`` at ``k``."""
    depth = 0
    j = k
    while j < len(tokens):
        t = tokens[j]
        if t.kind == "name":
            if t.value in ("function", "do", "then"):
                # "elseif ... then" continues an if block: no new depth
                if t.value == "then" and _is_elseif_then(tokens, j):
                    pass
                else:
                    depth += 1
            elif t.value == "repeat":
                depth += 1
            elif t.value in ("end", "until"):
                depth -= 1
                if depth == 0:
                    return j
        j += 1
    return len(tokens) - 1


def _is_elseif_then(tokens: list[Token], j: int) -> bool:
    k = j - 1
    while k >= 0:
        t = tokens[k]
        if t.kind == "name" and t.value in ("if", "elseif"):
            return t.value == "elseif"
        if t.kind == "name" and t.value in ("then", "end", "do", "function"):
            return False
        k -= 1
    return False


def _instances(tokens: list[Token], kind: str) -> list[tuple[str, int, int]]:
    """``<kind> "name" { ... }`` (also ``<kind>("name")({...})`` is not used): (name, "{" index, "}" index)."""
    found = []
    for k in range(len(tokens) - 2):
        if (
            tokens[k].kind == "name"
            and tokens[k].value == kind
            and tokens[k + 1].kind == "string"
            and tokens[k + 2].kind == "op"
            and tokens[k + 2].value == "{"
            and (k == 0 or tokens[k - 1].value not in (".", ":"))
        ):
            found.append((tokens[k + 1].value, k + 2, _closing(tokens, k + 2)))
    return found


def _fields(tokens: list[Token], open_brace: int) -> dict[object, object]:
    table, _ = parse_value(tokens, open_brace)
    return table if isinstance(table, dict) else {}


@dataclass(frozen=True)
class NpcVoice:
    gender: str
    voice: str


def npc_voices(scripts: Path, voices: dict[str, list[str]]) -> dict[str, NpcVoice]:
    """Gender and voice group of every Npc (``gender``, ``voice``; else the guild if it is a voice)."""
    out: dict[str, NpcVoice] = {HERO: NpcVoice("m", "")}
    for path in sorted(scripts.rglob("*.lua")):
        tokens = tokenize(path.read_text(encoding="utf-8"))
        for name, open_brace, _ in _instances(tokens, "Npc"):
            f = _fields(tokens, open_brace)
            gender = f.get("gender") if f.get("gender") in ("m", "f") else "m"
            voice = f.get("voice") or (f.get("guild") if f.get("guild") in voices else "")
            out[name] = NpcVoice(str(gender), str(voice or ""))
    out[HERO] = out.get("pc_hero", out[HERO])
    return out


def _literal(arg: list[Token]) -> str | None:
    return arg[0].value if len(arg) == 1 and arg[0].kind == "string" else None


def scan_dialogues(scripts: Path, people: dict[str, NpcVoice], result: ScanResult) -> None:
    for path in sorted(scripts.rglob("*.lua")):
        rel = path.relative_to(scripts).as_posix()
        tokens = tokenize(path.read_text(encoding="utf-8"))
        for info, open_brace, close_brace in _instances(tokens, "Info"):
            npc = _fields(tokens, open_brace).get("npc")
            npc = npc if isinstance(npc, str) else ""
            description = _fields(tokens, open_brace).get("description")
            texts: dict[str, ScannedLine] = {}

            def add(text: str, speaker: str, info: str = info, texts: dict = texts, rel: str = rel) -> None:
                if text in texts:
                    return
                key = f"{info}_{len(texts) + 1:02d}"
                who = people.get(speaker, NpcVoice("m", ""))
                texts[text] = ScannedLine(key, text, speaker, who.gender, who.voice, rel)

            if isinstance(description, str):
                add(description, HERO)  # chosen from the menu, the hero asks it first
            k = open_brace
            while k < close_brace:
                t = tokens[k]
                if (
                    t.kind == "name"
                    and t.value in ("say", "choice", "teach_menu")
                    and tokens[k + 1].value == "("
                    and tokens[k - 1].value not in (".", ":")
                ):
                    args = _arguments(tokens, k + 1)
                    if t.value == "say" and len(args) >= 2:
                        text = _literal(args[1])
                        speaker = HERO if _literal(args[0]) == HERO else npc
                        if text is None:
                            result.skipped.append(f"{rel}:{t.line}")
                        else:
                            add(text, speaker)
                    elif t.value == "choice" and args:
                        text = _literal(args[0])
                        if text is None:
                            result.skipped.append(f"{rel}:{t.line}")
                        else:
                            add(text, HERO)
                    elif t.value == "teach_menu" and len(args) >= 3 and args[2][0].value == "{":
                        replies, _ = parse_value(args[2], 0)
                        if isinstance(replies, dict):
                            for value in replies.values():
                                if isinstance(value, str):
                                    add(value, npc)
                    k += 2  # nested calls inside the arguments are visited too
                    continue
                k += 1
            for line in texts.values():
                result.lines[line.key] = line


def load_table(path: Path, name: str) -> dict[object, object]:
    """The literal table ``<name> = { ... }`` of a data script."""
    tokens = tokenize(path.read_text(encoding="utf-8"))
    for k in range(len(tokens) - 2):
        if (
            tokens[k].kind == "name"
            and tokens[k].value == name
            and tokens[k + 1].value == "="
            and tokens[k + 2].value == "{"
        ):
            table, _ = parse_value(tokens, k + 2)
            return table if isinstance(table, dict) else {}
    return {}


def voices_of(scripts: Path) -> dict[str, list[str]]:
    """``Voices`` of ``data/voices.lua``: voice group -> genders ("m", "f")."""
    table = load_table(scripts / "data/voices.lua", "Voices")
    out: dict[str, list[str]] = {}
    for voice, genders in table.items():
        if isinstance(voice, str) and isinstance(genders, dict):
            out[voice] = [g for g in genders.values() if g in ("m", "f")]
    return out


def scan_shouts(scripts: Path, voices: dict[str, list[str]], result: ScanResult) -> None:
    shouts = load_table(scripts / "data/shouts.lua", "Shouts")
    for occasion, texts in shouts.items():
        if not isinstance(occasion, str):
            continue
        variants = (
            [texts]
            if isinstance(texts, str)
            else [v for v in texts.values() if isinstance(v, str)]
            if isinstance(texts, dict)
            else []
        )
        for voice, genders in voices.items():
            for gender in genders:
                for n, text in enumerate(variants, 1):
                    key = f"svm_{voice}_{gender}_{occasion}_{n:02d}"
                    result.lines[key] = ScannedLine(
                        key, text, SVM, gender, voice, f"data/shouts.lua: {occasion}"
                    )


def scan(scripts: Path) -> ScanResult:
    """All spoken texts of the scripts with their keys."""
    result = ScanResult()
    voices = voices_of(scripts)
    scan_dialogues(scripts, npc_voices(scripts, voices), result)
    scan_shouts(scripts, voices, result)
    return result
