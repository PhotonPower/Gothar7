# audio

**Zweck:** Klangwelt. Bibliothek: **miniaudio** mit stb_vorbis (ADR 0007, angenommen 2026-10-08), privat.

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

## Umsetzung Teil A (M13, ADR 0007 angenommen)
- **Modul `audio`** (`g7/audio/Audio.hpp`, miniaudio und stb_vorbis privat, PImpl):
  - `parseSoundDefs` liest Klänge als Daten.
  - `AudioSystem` hat einen Mixer mit einer Gruppe je Bus (`music`, `effects`, `voice`, `ambient`, `ui`) unter dem Master.
  - Clips werden einmal zu Float-PCM in der Mixerrate dekodiert (WAV, FLAC, MP3, OGG Vorbis); jeder Klang liest sie über eine eigene Referenz.
  - `play(def, ort?, verzögerung?)`: Ohne Ort ist es ein 2D-Klang, mit Ort ein 3D-Klang mit linearer Abschwächung zwischen `min_distance` und `max_distance`, ohne Doppler (Entscheidung). Die Verzögerung gilt Sample-genau auf der Mixer-Uhr (für die Musik in Teil C).
  - Dazu `stop` (mit Ausblenden), `setVolume` (mit Überblenden), `setListener`, Bus- und Master-Lautstärke.
- **Ohne Ausgabegerät** (headless, CI, `[audio] device = false` bzw. wenn keins aufgeht): `update(sekunden)` rendert die Frames selbst. Tests hören so in jedem Lauf dasselbe; `lastPeak()` meldet den lautesten Wert.
- **Klänge als Daten:** `assets/source/data/sounds.toml`, je Name `files` (eine zufällig), `volume`, `volume_jitter`, `pitch_jitter`, `min_distance`, `max_distance`, `bus`, `loop`. Clips lädt die Engine beim ersten Abspielen aus dem VFS.
- **Engine:**
  - `[audio]` in `engine.toml`: `enabled`, `device`, `sample_rate`, `master`, je Bus eine Lautstärke. Das Menü folgt mit M14.
  - Der Hörer sitzt an der Kamera.
  - Anim-Events `sound:<name>` der Clips spielen an der Figur (Held und NPCs).
  - Lua: `sound(name, x?, y?, z?)`, `sound_stop(id, fade?)`, `sound_playing(id)`.
- **Platzhalter** (Entscheidung Projektinhaber): `tools/audio` (`gothar-audio placeholders`) erzeugt synthetische WAVs unter `assets/source/sounds/`; echte Klänge ersetzen sie unter gleichem Namen.
- **Folgt:** C Musik, D Sprache, E Fußschritte.

## Umsetzung Teil B: Raum
- **Verdeckung** (Entscheidung 8): Jeder 3D-Klang läuft über einen eigenen Tiefpass (`setMuffle`, 0 offen … 1 stark,
  Grenzfrequenz 20 kHz bis etwa 600 Hz).
  - Die Engine prüft zehnmal je Sekunde einen Strahl von der Kamera zum Klang (Welt-Kollision). Liegt etwas dazwischen,
    wird der Klang auf 0,8 gedämpft, sonst wieder offen.
- **Ambiente** (`assets/source/data/ambient.toml`): je Name `loop` und `loop_night`, Einzelklänge `randoms` und
  `randoms_night`, `interval` und `distance` als [min, max], `fade`.
  - Es gilt die Zone vom Typ `ambient`, in der der Held steht (world.md „Zonen“, die kleinste).
  - Wechselt die Zone oder Tag/Nacht (20–6 Uhr), blendet die alte Schleife aus und die neue ein.
  - Die Einzelklänge kommen zufällig verteilt 4–15 m um den Hörer, als 3D-Klänge.
- **Test-Lager:** die Ambiente `camp` im Zaun, `wald` als große Box drumherum, schon die Musik-Zone `LAGER` (Teil C).
  Platzhalter `amb_wind`, `amb_camp`, `amb_night` (nahtlose Schleifen), `bird`, `owl`.
- **Innenräume** (abgeleitet aus den indoor-Zonen, mit dem Koordinator abgestimmt; welt braucht keine eigenen Boxen):
  - In einem Raum gilt `innen` (leiser Raumklang, ab und zu Holzknacken).
  - Das Ambiente von draußen läuft gedämpft weiter, mit 30 % Lautstärke und Tiefpass 0,8.
  - Eine ambient-Box, die kleiner ist als der Raum, geht vor: `schmiede_esse` (Feuer, Amboss), `gasthaus_stube` (Gemurmel, Krüge).
- **Leonberg** (welts Namen): `feld` (Vorgabe), `stadt_gasse`, `stadt_markt`, `schlossgarten`, `ufer`, `wald`, `brunnen`, `stadtmauer`, dazu die Räume oben.
  - Neue Platzhalter-Schleifen (4 s): `amb_field`, `amb_town`, `amb_market`, `amb_water`, `amb_fountain`, `amb_fire`, `amb_tavern`, `amb_room`.
  - Neue Einzelklänge: `crow`, `dog_bark`, `wood_creak`, `mug_clink`, `frog`.
- **Spruch-Klänge:** `Spell`-Feld `sounds = { cast, impact }`; das Wirken klingt an der Hand, der Einschlag am Treffpunkt.

## Sprache (Teil D, umgesetzt) – `runtime/EngineVoice.cpp`
Entscheidungen Projektinhaber 2026-10-08: WAV vorerst (OGG-Umwandlung im Cooker erst mit echten Takes und eigener
ADR), Ducking −6 dB mit 0,3 s Blende, Dialogstimme 3D am Sprecher, keine Platzhalter-Stimmen im Repo.
- **Take:** `voice/<sprache>/<key>.wav` (oder `.ogg`), `[voice] language` (Vorgabe `de`). Der Schlüssel kommt wie bisher
  aus `voiceKey` (Dialogzeilen `dia_…`, Zurufe `svm_…`).
- **Dialog:** Wird eine Zeile gezeigt und es gibt ihren Take, spricht ihn der Sprecher.
  - 3D am Kopf (1,6 m), voll bis 4 m, still ab 40 m, Bus `voice`; die Stimme folgt dem Sprecher.
  - Die Zeile dauert dann Take + 0,3 s, die Untertitel folgen der Stimme. Ohne Take bleibt die Lesedauer aus der
    Textlänge.
  - Überspringen und das Gesprächsende brechen die Stimme ab. Je Sprecher spricht eine Stimme.
- **Zurufe** (`npc_said`) sprechen ebenso, wenn ihr Take existiert; aus der Ferne leiser (3D).
- **Lippensync aus der Lautstärke** (Entscheidung 7):
  - `AudioSystem::clipEnvelope` liefert RMS je 1/30 s, auf das lauteste Fenster normiert; `AudioSystem::position(id)`
    die Abspielposition.
  - Solange die Stimme läuft, setzt die Engine `FaceAnimator::setMouthOpen(hüllkurve[position])`.
  - Der Mund öffnet `vis_aa` bis `talkWeight`, über 40 ms geglättet; `vis_oh` rundet ihn leicht mit 2,5 Hz. Danach
    gibt `nullopt` den Mund an die grobe Sprechbewegung zurück.
- **Ducking:** Solange jemand spricht, steht der Musik-Bus auf `[audio] duck` (0,5 = −6 dB) mal `music`, über 0,3 s
  geblendet.
- **Lua:** `voice_playing(npc)`, Ereignis `voice_line(npc, key, seconds)`.
- **Nebenbei behoben:** NPCs mit Kapsel (alle Menschen) übersprangen Blick und Gesicht; jetzt blinzeln sie, sprechen
  und schauen den Helden im Gespräch an.
- **Tests:** `tests/runtime/test_engine_voice.cpp` mit einem synthetischen Take aus einem temporären Mount:
  Mund offen im Laut und zu in der Pause, Dauer, Ducking, Überspringen. Dazu audio (Hüllkurve, Position) und
  animation (`setMouthOpen`).

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
