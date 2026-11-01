import json
from pathlib import Path

from gothar_voice.cli import check_texts, repo_root
from gothar_voice.texts import ScriptTexts, Text, is_text, scan_texts

SCRIPTS = {
    "data/voices.lua": 'Voices = { guard = { "m" }, farmer = { "m", "f" } }\n',
    "data/shouts.lua": 'Shouts = { warn = { "Weg damit!", "Letzte Warnung!" }, calm = "Na also." }\n',
    "data/guilds.lua": 'Guilds = { farmer = { name = "Bauern" } }\nGuilds.wolf = { name = "Wölfe" }\n',
    "data/teaching.lua": (
        'Teaching = { talent_names = { picklock = "Schlösser öffnen", sneak = "Schleichen" }, limit = 100 }\n'
        'function teach_menu(npc) choice("Zurück.", function() end) end\n'
    ),
    "items/food.lua": (
        'Item "it_apple" { name = "Apfel", category = "food", mesh = "items/it_apple.glb" }\n'
        'Item "it_letter" { name = "Brief", text = "Bauer,\n\nkomm.", category = "document" }\n'
    ),
    "npcs/people.lua": 'Npc "npc_guard" { name = "Wache", guild = "guard", routine = "rtn_guard" }\n',
    "quests/q.lua": (
        'Quest "quest_x" { name = "Ein Auftrag", description = "Tu etwas.", topic = "topic_camp" }\n'
        'function done() quest_success("quest_x", "Erledigt.") set_chapter(2, "Weiter") notice(msg) end\n'
    ),
    "dialogs/d.lua": """
Info "dia_guard_hello" {
    npc = "npc_guard",
    description = "Hallo.",
    run = function(npc)
        say(npc, "Was willst du?")
        quest_entry("quest_x", "Die Wache weiß Rat.")
    end,
}
""",
}


def write_scripts(root: Path) -> Path:
    for rel, text in SCRIPTS.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


def test_is_text():
    assert is_text("Apfel") and is_text("ein Satz") and is_text("öl")
    assert not is_text("it_apple") and not is_text("items/it_apple.glb") and not is_text("")


def test_scan_texts_keys(tmp_path):
    texts = {k: v.de for k, v in scan_texts(write_scripts(tmp_path)).texts.items()}
    assert texts == {
        "dia_guard_hello_01": "Hallo.",
        "dia_guard_hello_02": "Was willst du?",
        "svm.calm_01": "Na also.",
        "svm.warn_01": "Weg damit!",
        "svm.warn_02": "Letzte Warnung!",
        "data.guilds.name_01": "Bauern",
        "data.guilds.name_02": "Wölfe",
        "data.teaching.talent_names_01": "Schlösser öffnen",
        "data.teaching.talent_names_02": "Schleichen",
        "data.teaching.choice_01": "Zurück.",
        "dia_guard_hello.quest_entry_01": "Die Wache weiß Rat.",
        "it_apple.name": "Apfel",
        "it_letter.name": "Brief",
        "it_letter.text": "Bauer,\n\nkomm.",
        "npc_guard.name": "Wache",
        "quest_x.name": "Ein Auftrag",
        "quest_x.description": "Tu etwas.",
        "quests.q.quest_success_01": "Erledigt.",
        "quests.q.set_chapter_01": "Weiter",
    }


def test_merge_adds_marks_outdated_removes_and_carries_over(tmp_path):
    scripts = write_scripts(tmp_path / "scripts")
    db = ScriptTexts(tmp_path / "text", "en")
    db.texts["it_apple.name"] = Text("it_apple.name", "Apfel", "Apple", "items/food.lua", "entwurf")
    db.texts["npc_guard.name"] = Text("npc_guard.name", "Torwache", "Gate Guard", "", "abgenommen")
    db.texts["old.key"] = Text("old.key", "Weg damit!", "Put that away!", "", "entwurf")
    db.texts["gone.key"] = Text("gone.key", "Fort", "Gone", "", "entwurf")
    report = db.merge(scan_texts(scripts))
    assert "it_apple.name" not in report.added and db.texts["it_apple.name"].status == "entwurf"
    assert report.changed == ["npc_guard.name"] and db.texts["npc_guard.name"].status == "veraltet"
    assert db.texts["npc_guard.name"].de == "Wache" and db.texts["npc_guard.name"].text == "Gate Guard"
    assert set(report.removed) == {"old.key", "gone.key"}
    assert "svm.warn_01" in report.carried and db.texts["svm.warn_01"].text == "Put that away!"
    assert db.texts["quest_x.name"].status == "offen" and db.texts["quest_x.name"].text == ""
    problems = db.problems()
    assert any("npc_guard.name" in p for p in problems) and any("quest_x.name" in p for p in problems)
    db.save()
    raw = json.loads(db.path.read_text(encoding="utf-8"))
    assert raw["schema"] == 1 and raw["lang"] == "en" and "it_apple.name" in raw["texts"]
    assert ScriptTexts.load(db.root, "en").texts["svm.warn_01"].text == "Put that away!"
    assert ScriptTexts.languages(db.root) == ["en"]


def test_validate():
    assert Text("k", "Apfel", "Apple", status="offen").validate()
    assert Text("k", "Apfel", "", status="entwurf").validate()
    assert Text("k", "Apfel", "Apple", status="kaputt").validate()
    assert not Text("k", "Apfel", "Apple", status="entwurf").validate()


def test_repository_translations_match_the_scripts():
    """What CI checks with `gothar-voice check`: every content text translated, none outdated."""
    assert check_texts(repo_root()) == []
