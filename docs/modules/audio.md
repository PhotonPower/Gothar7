# audio

**Zweck:** Klangwelt. Bibliothek: **miniaudio** (ADR 0007), privat.

## Bestandteile
- **Mixer-Busse**: Master → Musik, Effekte, Sprache, Ambient, UI; Lautstärken aus Optionen; Ducking (Musik leiser bei Sprache).
- **3D-Sounds**: Emitter-Komponente, Abschwächungskurven, Doppler aus, Verdeckung per Raycast (Tiefpass).
- **Sound-Definitionen** (Daten): Variationen (zufällige Auswahl), Lautstärke/Tonhöhen-Streuung, Reichweite.
- **Ambient-Zonen**: Loop + zufällige Einzelgeräusche, Überblendung zwischen Zonen, Tag/Nacht-Varianten.
- **Sprache**: Datei je Text-Schlüssel (`voice/de/dia_gate_guard_hello_01.ogg`), Dauer bestimmt Untertitel/Gesprächstempo; Lippensync aus Amplitude oder Phonem-Daten.
  Quelle ist die Sprechtext-Datenbank `assets/source/voice/lines.<sprache>.json` (Text, Sprecher, M/F, Stimme, Regie,
  Status, Takes), gepflegt mit `tools/voice`; der gewählte Take liegt als `assets/source/voice/<sprache>/<key>.wav`
  daneben, alle Takes nur lokal unter `DATA_ROOT/voice/takes/<sprache>/` (Entscheidung Projektinhaber).
  - **Schlüssel** (Vorschlag B, bestätigt 2026-10-05): Die Texte stehen inline in Lua (E2). `gothar-voice scan` vergibt
    die Schlüssel in Quelltext-Reihenfolge je Info: `<info>_NN` für `description` (der Held fragt, wenn er die Info im
    Menü wählt), `say(npc, …)`, `say("hero", …)`, `choice("…", fn)` (der Held sagt die Antwort) und die Antworten von
    `teach_menu`. Gleicher Text in einer Info = gleicher Schlüssel; zur Laufzeit gebaute Texte bekommen keinen (der
    Scan meldet sie). Die Engine (`asset::VoiceLines`) findet den Schlüssel über (Info, Text); kennt die Datenbank
    einen Text nicht, nimmt sie die laufende Nummer in der Info und meldet es im Debug-Log.
  - **Zurufe** (Projektinhaber): eine Stimme je Gilde und Geschlecht, Liste in `data/voices.lua`
    (guard m; farmer, outcast, citizen, craftsman m/f; hunter m). Schlüssel `svm_<stimme>_<m|f>_<anlass>_NN` für
    jeden Text aus `data/shouts.lua` (NN = Variante). Ein NPC ruft mit `Npc.voice` (sonst seiner Gilde) und
    `Npc.gender` (`"m"`/`"f"`, Vorgabe `"m"`); das Ereignis `npc_said(npc, text, key)` bekommt den Schlüssel.
  - **Pflege:** Texte ändern → `gothar-voice scan --write` (neue Zeilen offen, geänderte wieder offen und ohne
    gewählten Take, entfernte nur `orphan`). CI prüft mit `gothar-voice check`, dass die Datenbank zu den Skripten
    passt.

## Dynamisches Musiksystem (Gothic: DirectMusic)
- Musik-Zone (aus world) → **Thema** (z. B. `CAMP`, `FOREST`, `MINE`).
- Zustand: `Std` | `Thr` (Bedrohung: Feind nimmt Spieler wahr) | `Fgt` (Kampf) – ermittelt aus gameplay/ai.
- Tageszeit: `Day` | `Ngt`.
- Pro Thema × Zustand × Tageszeit eine Menge von Segmenten (Loops) mit Takt-/Tempo-Info.
- Übergänge **auf Taktgrenzen**, Überblendung oder Übergangs-Segment; Stinger (z. B. Quest gelöst).
- Hysterese: Kampf → Standard erst nach X Sekunden ohne Kampf.

```toml
# music/camp.toml
[theme.CAMP]
bpm = 96
[theme.CAMP.std.day]
segments = ["music/camp_day_a.ogg", "music/camp_day_b.ogg"]
[theme.CAMP.fgt]
segments = ["music/fight_generic_a.ogg"]
transition = "next_bar"
```
