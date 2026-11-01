# voice – Sprechtexte und Takes

Datenbank aller gesprochenen Zeilen (Text, Sprecher, M/F, Stimme, Regieanweisung, Status) mit Takes aus TTS oder
Aufnahme. Spezifikation: `docs/modules/audio.md` („Sprache“), Vertrag: `docs/coordination.md` („Sprechtexte“).

## Ablage
```
assets/source/voice/lines.de.json              Datenbank (eine je Sprache, versioniert)
assets/source/voice/de/<key>.wav               gewählter Take (versioniert) – daraus macht der Cooker voice/de/<key>.ogg
DATA_ROOT/voice/takes/de/<key>__tNN.wav        alle Takes einer Zeile, nur lokal (Nummern laufen weiter)
```
`DATA_ROOT` kommt aus `$GOTHAR_DATA_ROOT` oder `paths.data_root` in `tools/worldgen/config/local.toml`.

## Schlüssel
Die Texte stehen in den Skripten (`game/scripts`, inline wie `say(npc, "Halt!")`); `gothar-voice scan` vergibt die
Schlüssel, in Quelltext-Reihenfolge je Info:
- `<info>_NN` – `description` (der Held fragt), `say(...)`, `choice(...)`, Antworten von `teach_menu`
  (z. B. `dia_gate_guard_hello_01`);
- `svm_<stimme>_<m|f>_<anlass>_NN` – jeder Zuruf aus `data/shouts.lua` für jede Stimme aus `data/voices.lua`
  (z. B. `svm_guard_m_thief_01`).

Texte, Sprecher und Schlüssel ändert man in den Skripten, nicht hier; das Werkzeug pflegt Regie, Status und Takes.

## Benutzen
```
pip install -e "tools/voice[app,dev]"
gothar-voice scan            # was sich in den Skripten geändert hat (neu, geändert, verwaist)
gothar-voice scan --write    # Datenbank abgleichen: neu = offen, geändert = wieder offen, entfernt = verwaist
gothar-voice check           # Schema, gewählte Takes vorhanden, Datenbank passt zu den Skripten (CI)
gothar-voice app             # Streamlit-Oberfläche
gothar-voice texts           # Übersetzungen (Vorgabe --lang en): neue, geänderte, entfernte Inhaltstexte
gothar-voice texts --write   # text/script.en.json abgleichen: neu = offen, geändert = veraltet, entfernt = weg
```

## Übersetzungen (M14 Teil G)
`assets/source/text/script.<sprache>.json` übersetzt alle Texte, die der Spieler aus den Skripten liest oder hört:
gesprochene Zeilen (Sprach-Schlüssel), Zurufe (`svm.<anlass>_NN`), Namen und Beschreibungen (`<instanz>.<feld>`),
Tagebuch-Einträge, Kapitel, Meldungen (`<info bzw. datei>.<funktion>_NN`). Die Engine sucht nach dem deutschen
Text (`de`). Nach `texts --write` die offenen Einträge übersetzen (`text`, `status = "entwurf"`); geänderte deutsche
Texte stehen auf `veraltet`, bis die Übersetzung geprüft ist. `check` (CI) schlägt fehl, solange etwas offen,
veraltet oder nicht abgeglichen ist.
In der App: Regie und Status in der Tabelle bearbeiten und speichern, offene Zeilen als CSV für einen TTS-Stapel
laden, eine Zeile wählen, WAVs hochladen (mehrere = mehrere Takes), anhören, Take wählen; „game/scripts abgleichen“
entspricht `scan --write`.
