from pathlib import Path

from gothar_voice.cli import load_db, repo_root
from gothar_voice.luascan import parse_value, scan, tokenize


def write(root: Path, rel: str, text: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def test_tokenize_strings_and_comments():
    tokens = tokenize(
        'say(npc, "Er sagt: \\"Halt!\\"") -- say(npc, "no")\n--[[ say(npc, "no") ]] x = [[lang]]'
    )
    strings = [t.value for t in tokens if t.kind == "string"]
    assert strings == ['Er sagt: "Halt!"', "lang"]


def test_parse_table():
    table, _ = parse_value(
        tokenize('{ a = "x", "erst", { "b", "c" }, n = 3, f = function(x) return x, 1 end }')
    )
    assert table["a"] == "x" and table[1] == "erst" and table[2] == {1: "b", 2: "c"} and table["n"] == 3.0
    assert table["f"] is None


SCRIPTS = {
    "data/voices.lua": 'Voices = { guard = { "m" }, farmer = { "m", "f" } }\n',
    "data/shouts.lua": 'Shouts = { warn = { "Weg damit!", "Letzte Warnung!" }, calm = "Na also." }\n',
    "npcs/people.lua": (
        'Npc "npc_guard" { name = "Wache", guild = "guard" }\n'
        'Npc "npc_woman" { name = "Bäuerin", guild = "farmer", gender = "f" }\n'
        'Npc "npc_smith" { name = "Schmied", guild = "citizen", voice = "craftsman" }\n'
    ),
    "dialogs/d.lua": """
Info "dia_guard_hello" {
    npc = "npc_guard",
    nr = 1,
    description = "Hallo.", -- the menu text: the hero says it first
    run = function(npc)
        say(npc, "Was willst du?")
        say("hero", "Nur reden.")
        if Story.x then
            say(npc, "Schon wieder du.")
        elseif Story.y then
            say(npc, "Was willst du?") -- the same text again: the same key
        end
        choice("Arbeit?", function()
            say(npc, "Frag die Bäuerin.")
        end)
        say(npc, name_of(npc) .. "!") -- built at run time: no key
    end,
}

Info "dia_woman_teach" {
    npc = "npc_woman",
    run = function(npc)
        teach_menu(npc, { { talent = "x", lp = 5 } }, { ok = "Gut so.", lp = "Später." })
    end,
}
""",
}


def test_scan_dialogues_in_source_order(tmp_path):
    for rel, text in SCRIPTS.items():
        write(tmp_path, rel, text)
    result = scan(tmp_path)
    hello = {k: (v.text, v.speaker) for k, v in result.lines.items() if k.startswith("dia_guard_hello")}
    assert hello == {
        "dia_guard_hello_01": ("Hallo.", "hero"),
        "dia_guard_hello_02": ("Was willst du?", "npc_guard"),
        "dia_guard_hello_03": ("Nur reden.", "hero"),
        "dia_guard_hello_04": ("Schon wieder du.", "npc_guard"),
        "dia_guard_hello_05": ("Arbeit?", "hero"),  # the hero says the chosen answer
        "dia_guard_hello_06": ("Frag die Bäuerin.", "npc_guard"),
    }
    assert result.lines["dia_guard_hello_02"].voice == "guard"
    assert result.lines["dia_guard_hello_01"].context == "dialogs/d.lua"
    # teach_menu replies (each could be the answer), with the teacher's gender
    assert (
        result.lines["dia_woman_teach_01"].text == "Gut so."
        and result.lines["dia_woman_teach_02"].text == "Später."
    )
    assert result.lines["dia_woman_teach_01"].gender == "f"
    assert result.skipped == ["dialogs/d.lua:17"]


def test_scan_shouts_for_every_voice(tmp_path):
    for rel, text in SCRIPTS.items():
        write(tmp_path, rel, text)
    svm = {k: v.text for k, v in scan(tmp_path).lines.items() if k.startswith("svm_")}
    assert svm == {
        "svm_guard_m_warn_01": "Weg damit!",
        "svm_guard_m_warn_02": "Letzte Warnung!",
        "svm_guard_m_calm_01": "Na also.",
        "svm_farmer_m_warn_01": "Weg damit!",
        "svm_farmer_m_warn_02": "Letzte Warnung!",
        "svm_farmer_m_calm_01": "Na also.",
        "svm_farmer_f_warn_01": "Weg damit!",
        "svm_farmer_f_warn_02": "Letzte Warnung!",
        "svm_farmer_f_calm_01": "Na also.",
    }


def test_the_repository_database_matches_the_scripts():
    """CI: every spoken text of game/scripts has its key in assets/source/voice/lines.de.json
    (after changing texts: gothar-voice scan --write)."""
    root = repo_root()
    db = load_db(root, "de")
    report = db.merge(scan(root / "game/scripts"))
    assert not report.added, f"neu (gothar-voice scan --write): {report.added[:5]}"
    assert not report.changed, f"Text geändert (gothar-voice scan --write): {report.changed[:5]}"
    assert not report.updated and not report.orphaned, "gothar-voice scan --write"
    assert not db.validate()
