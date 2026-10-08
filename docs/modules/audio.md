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

## Dynamisches Musiksystem (Teil C, umgesetzt; Gothic: DirectMusic)
Entscheidungen Projektinhaber 4 und 5 sowie zu (a)–(c) vom 2026-10-08.
- **Daten:** `data/music.toml` (`audio::parseMusicDefs`).
  - `[theme.<THEMA>]` mit `bpm`, `beats` (je Takt) und `volume`.
  - Mengen `[theme.<THEMA>.<std|thr|fgt>]` gelten für Tag und Nacht, `[theme.<THEMA>.<zustand>.<day|ngt>]` getrennt.
  - Felder einer Menge: `segments` (Dateien), optional `intro`, `transition` (`"next_bar"` als Vorgabe oder `"end"`),
    `fade` in s, eigenes `bpm`/`beats`.
  - Das Thema `common` gehört zu keiner Zone; es liefert die gemeinsame Bedrohungs- und Kampfmusik.
  - Stinger: `[stinger.<name>] files, volume`.
- **Thema:** die kleinste Box `music` um den Helden (1 m über den Füßen; world.md „Zonen“).
- **Wahl** (`chooseMusic`): Thema.Zustand.Tageszeit → Thema.Zustand → dasselbe in `common` → der nächstniedrigere
  Zustand.
- **Außerhalb jeder Musik-Box** ist Stille (Projektinhaber (a)). Ausnahme: die Zustände aus `outside`
  (`["thr", "fgt"]`, Projektinhaber, Möglichkeit 2) spielen dort die Musik von `common` und blenden danach auf einer
  Taktgrenze zu Stille aus. Ein Thema ohne Musik zählt wie keine Zone.
- **Zustand** (`runtime/EngineMusic.cpp`), alle 0,25 s geprüft, nur durch echte Feinde (Projektinhaber (c)):
  - `fgt`: Der Held hat in den letzten `fight_memory` s (4) getroffen oder wurde getroffen (auch pariert, auch Sprüche).
    Oder ein NPC bis `fight_range` (20 m), der ihn sieht, steht in einem der `fight_states`
    (`zs_attack`, `zs_mm_attack`, `zs_mm_hunt`) und kämpft gegen ihn (`fight_target(npc) == "hero"` in
    `ai/combat.lua`).
  - `thr`: Ein NPC bis `threat_range` (25 m), der ihn sieht, steht in `threat_states` (`zs_threaten`,
    `zs_mm_threaten`). Warnungen wegen der Waffe oder der Hütte zählen nicht.
  - Hysterese (`MusicStateFilter`): Nach oben wechselt der Zustand sofort, nach unten erst nach `hysteresis` s (5)
    ohne Anlass.
  - Tag und Nacht wie die Ambiente (Nacht 20–6 Uhr).
- **Abspielen** (`audio::MusicPlayer`, auf der Mixer-Uhr in Frames):
  - Segmente laufen einzeln und werden Sample-genau verkettet: Das nächste wird schon beim Start des laufenden für
    dessen Ende eingeplant. Gewählt wird zufällig, nicht zweimal dasselbe.
  - Ein Wechsel beginnt auf der nächsten Taktgrenze des laufenden Segments (`next_bar`, nach dessen Tempo) oder an
    seinem Ende (`end`; die Standard-Musik nach einem Kampf und beim Themenwechsel).
  - Das alte Segment blendet über `fade` s bis genau dorthin aus (miniaudio stop_time_with_fade); das neue beginnt dort,
    gegebenenfalls mit seinem `intro`.
  - Ändert sich der Wunsch vor dem Wechsel, klingt das geplante Segment nie. Aus der Stille beginnt die Musik sofort,
    nach einem Ausblenden erst an dessen Ende.
  - `AudioSystem` hat dafür `playAtFrame`, `stopAtFrame`, `clipFrames` und `frame`.
- **Stinger** auf dem nächsten Schlag über der Musik (`lib/music.lua`, Projektinhaber (b)):
  - `quest`: Ereignis `quest_success`;
  - `level_up`;
  - `chapter`: `chapter_changed`;
  - `death`: Der Held geht nieder, `npc_knocked_out`/`npc_killed` mit `"hero"`; er stirbt nicht, K8.
- **Lua:** `music_state()` (theme, state, night, set, segment, stinger), `music_stinger(name)`,
  `music_force(theme?, state?)` (zum Testen), Ereignis `music_changed(theme, state)`. Lautstärke über den Bus `music`
  aus engine.toml.
- **Platzhalter** (Projektinhaber 2): `gothar-audio music` (tools/audio).
  - Themen: LAGER (96 BPM, D-Dur-Pentatonik, gezupft) und STADT (120 BPM), jeweils Tag und Nacht.
  - `common`: Bedrohung (80 BPM, d-Moll-Bordun mit Herzschlag) und Kampf (150 BPM, Trommeln und Riff).
  - Dazu vier Stinger; zusammen 2,6 MB WAV unter `assets/source/music/`.
  - Die Tempi legen jeden Takt auf ganze Frames bei 22,05 und 48 kHz. Nachklänge laufen an den Anfang um, deshalb
    schließen die Schleifen ohne Knacken.
  - Echte Musik ersetzt die Dateien unter gleichem Namen; LAND hat keine Musik mehr (außerhalb Stille).
- **Tests:**
  - `tests/audio/test_music.cpp`: Daten, Wahl, Hysterese, Verkettung, Takt- und Endwechsel, Stille, Stinger.
  - `tests/runtime/test_engine_m13_music.cpp`, DoD-Szenario F: das Lager betreten; ein Bandit droht, dann greift er an;
    5 s danach wieder Lager-Musik am Segmentende; das Lager verlassen; Kampf draußen.
