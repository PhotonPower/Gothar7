# voice – Sprechtexte und Takes

Datenbank aller gesprochenen Zeilen (Text, M/F, Stimme, Regieanweisung) mit Takes aus TTS oder Aufnahme.
Spezifikation Sprache: `docs/modules/audio.md` („Sprache“), Lokalisierung: `docs/modules/ui.md`.

## Ablage
```
assets/source/voice/lines.de.json            Datenbank (eine je Sprache)
assets/source/voice/de/takes/<key>__tNN.wav  alle Takes einer Zeile (Nummern laufen weiter, nie überschrieben)
assets/source/voice/de/<key>.wav             gewählter Take – daraus macht der Cooker voice/de/<key>.ogg
```
Schlüssel: `dia_<npc>_<info>_NN` für Dialoge (wie in `say(self, other, "dia_gate_guard_hello_01")`),
`svm_<stimme>_<anlass>` für allgemeine Zurufe, die jede Stimme einmal braucht.

## Benutzen
```
pip install -e "tools/voice[app,dev]"
gothar-voice app      # Streamlit-Oberfläche
gothar-voice check    # Schema prüfen, gewählte Takes vorhanden?
gothar-voice scan     # say(...)-Schlüssel in game/scripts, die in der Datenbank fehlen
```
In der App: Tabelle bearbeiten und speichern, offene Zeilen als CSV für einen TTS-Stapel laden,
eine Zeile wählen, WAVs hochladen (mehrere = mehrere Takes), anhören, Take wählen.
