# perf – Plan M17 (Profiling, Budgets, Job-System)

Stand: **Plan freigegeben** (Projektinhaber, 2026-11-07; alle Vorschläge übernommen). **Teil B umgesetzt:** `gothar --perf=<namen>|all` mit `assets/source/data/perf.toml` (markt, gasse, stube um 10:00 und 21:00; Stress `markt_voll` mit 60 NPCs mehr), Bericht `perf_<name>.json`, Ladephasen; erste Werte in `docs/perf/2026-11-07-erste-messung.md`. **Teil C zurückgestellt; Teil D: KI-LOD-Obergrenze nach Anzahl umgesetzt.**
Ziel (Meilenstein D): Release-Build stabil ≥ 60 FPS in der größten Welt (Leonberg) auf Zielhardware. Erst messen, dann
parallelisieren. Die Entscheidungen des Projektinhabers stehen am Ende, nummeriert.

## Ausgangslage
- **Hauptschleife** (`docs/02-architecture.md`) läuft auf einem Thread: fester Zeitschritt (Physik, Spieler, KI mit
  Wahrnehmung, Kreaturen und deren Animation, Skripte), dann Rendern (Culling, Schattenkaskaden, Gelände, Figuren,
  Post). Nebenläufig sind nur die Lade-Threads des `AssetManager`.
- **Profiling:** `G7_PROFILE_SCOPE` (`core/Profiler.hpp`) mit eingebautem Sammler je Frame, aus (`G7_PROFILING=OFF`).
  Es gibt nur 4 Zonen (frame, fixedUpdate, render, frameCap). Tracy ist in `05-build.md` für M17 vorgemerkt, ohne ADR.
- **Messen:** `--benchmark` (Testszene) und Kamerapositionen mit `--fly --cam` (Leonberg: Markt, Gasse, Kirche, Stube
  im Gasthaus, Überblick). Bekannte Zahlen: Leonberg-Überblick 23,5 ms (Debug), Marktplatz 2,05 ms GPU-Hauptpass
  (RTX 3080 Laptop, Release), Intel UHD Überblick 22–25 ms; Budgets für Inhalte in `render.md` „Leistungsbudgets“.
- **Schon vorhanden:** KI-LOD (`[ai] simulation_distance` 80 m), Figuren-Culling (`figure_distance` 150 m),
  Sichtbarkeits-Raster mit Multi-Draw, Lichtauswahl je Bild (E1), LOD statischer Modelle.

## Teil A – Tracy (ADR 0024 „Vorgeschlagen“)
- **Neue Abhängigkeit:** Tracy (BSD-3), vcpkg `tracy`. Sie braucht eine ADR; ADR 0024 entsteht mit Status
  „Vorgeschlagen“ im Teil-A-PR und wird vor der Umsetzung bestätigt.
- **Nur mit `G7_PROFILING=ON`** (eigenes Preset `profile`: Release mit Symbolen und Tracy). Normale Builds und das Paket
  bleiben ohne Tracy; die Makros sind dort leer wie heute (Entscheidung 2).
- **Abbildung:** `G7_PROFILE_SCOPE` → `ZoneScopedN`, `G7_PROFILE_FRAME` → `FrameMark`, Thread-Namen für
  Job-Arbeiter. GPU-Zonen (`TracyOpenGL`) in `render` für Schatten, Hauptpass, Post. Speicher-Zonen nur auf Wunsch.
- **Eingebauter Sammler bleibt** für das Overlay (F1) und für Tests ohne Tracy.
- **Zonen ergänzen** (je System eine bis drei): Physik-Schritt, Spieler, KI (Routinen, Wahrnehmung, Wege), Animation
  (Abtasten, Blenden, Skinning-Matrizen), Skripte, Audio, Culling, Schatten je Kaskade, Gelände, Figuren, UI, Post,
  Laden.

## Teil B – Budget je System und Messung in Leonberg
- **Budget bei 60 FPS (16,6 ms)**, festgelegt (Entscheidungen 1, 5):

  | System | CPU Hauptthread | Hinweis |
  |---|---|---|
  | Simulation gesamt | ≤ 4,0 ms | fester Schritt 60 Hz |
  | – Physik (Jolt) | ≤ 1,0 ms | |
  | – KI inkl. Wahrnehmung und Wege | ≤ 1,5 ms | 60 NPCs in Leonberg, davon ≤ 25 simuliert |
  | – Animation (alle Figuren) | ≤ 1,5 ms | nach Teil C parallel |
  | Skripte (Lua) | ≤ 0,5 ms | |
  | Culling und Draw-Listen | ≤ 1,0 ms | |
  | Audio, UI, Rest | ≤ 1,0 ms | |
  | GPU gesamt | ≤ 10 ms | Schatten ≤ 2,5, Hauptpass ≤ 5, Post ≤ 1 |

- **Mess-Szenarien** `gothar --perf=<name>` (Datei `assets/source/data/perf.toml`, eigene Szenen, keine Weltänderung):
  - **Markt:** Held am Marktbrunnen (`--cam=10.8,1.8,7.7`, Blick über den Platz), 10:00 Uhr, Händler aktiv.
  - **Gasse:** enge Gasse am Hang (`--cam=-76.8,-0.4,-135.3`), viele Häuser in der Nähe, Schatten.
  - **Stube:** im Gasthaus (`--cam=-89.7,1.4,27.0`), Innenraum, Fensterlichter, Herdfeuer, NPCs am Tisch.

  Je Szenario: 5 s Anlauf, dann 600 Bilder mit dem Helden an fester Stelle (gleiche Zufallszahlen). Ausgegeben werden
  p50/p95/p99 der Bildzeit und je Zone, GPU-Zeiten aus Timer-Abfragen, Draws, Dreiecke, Lichter, simulierte NPCs.
  Ergebnis als JSON (`perf_<szenario>.json`) und eine Zeile im Log.
- **Wo gemessen wird:** Release-Build, 1600×900, Schatten an, auf RTX 3080 Laptop und Intel UHD (Entscheidung 1).
  Ein CI-Lauf ist nicht vorgesehen (GPU-Zeiten in CI sind nicht vergleichbar). Ein Bericht je Messung kommt in
  `docs/perf/` (Datum, Hash, Hardware, Tabelle).
- **Ladezeit:** Start bis erstes Bild in Leonberg, aufgeteilt in Welt, Häuser, Figuren und Clips, Skripte. Diese
  Zahl entscheidet über `.g7skin`/`.g7anim` (siehe `release.md`, Teil D).
- **Umsetzung (Teil B):** `tools/perf` (EngineTool, siehe `tools.md`). Bericht `perf_<name>.json`: `scenario`,
  `build`, `renderer`, `resolution`, `profiling`, je Uhrzeit ein Eintrag in `runs` mit `frame` (average/p50/p95/p99/
  worst in ms), `zones` (Zeit je Bild als p50/p95/p99, Aufrufe je Bild; nur mit `G7_PROFILING`), `gpu_passes` (Mittel
  der geglätteten Timer-Abfragen), `gpu_total_ms`, `stats` (Draws, Dreiecke, Lichter je Bild, NPCs, simulierte NPCs im
  Mittel und maximal), dazu `load_phases` (Name, Dauer, Ende seit Start: assets, scripts, window/OpenGL, shaders,
  scene rendering, world file, world, houses and models, figures and clips, first frame, models settled).
  Abweichung vom Plan: Die Uhrzeiten eines Szenarios laufen im selben Prozess nacheinander (Zeitsprung, dann erneut
  Anlauf). Die Welt kommt aus `--world` (Standard Leonberg), nicht aus perf.toml.

## Teil C – Job-System (Animation, KI, Culling)
**Zurückgestellt** (Koordinator nach der ersten Messung, 2026-11-07): Alle Szenarien liegen weit im Budget (Markt p99
6,5 ms, Stress p99 10 ms bei 16,7 ms), das Job-System ist im Moment nicht nötig. Es kommt erst, wenn ein Szenario das
Budget reißt. Ebenso bleibt `.g7skin`/`.g7anim` nach Entscheidung 9 zurückgestellt (3,85 s Ladezeit sind in Ordnung).

- **Eigenes, kleines System in `core`** (`g7/core/Jobs.hpp`): fester Pool aus `std::jthread`,
  `parallelFor(count, chunk, fn)` und `JobGroup::wait()`. Keine Aufgaben-Graphen, kein Work-Stealing zu Beginn.
  Fremdbibliotheken (enkiTS, Taskflow) wären eine neue Abhängigkeit mit ADR; der Bedarf ist klein (Entscheidung 6).
- **Regeln:**
  - **Lua nur auf dem Hauptthread** (sol2 ist nicht thread-sicher). KI-Zustände und alles, was Skripte ruft, bleibt
    seriell. Parallel laufen nur reine Berechnungen.
  - **Determinismus:** Ergebnisse werden in fester Reihenfolge (nach ID) übernommen. Szenario-Tests mit festen
    Zufallszahlen müssen byte-gleich bleiben (heute: M9-, M11-, M15-Szenarien).
  - **Kein Schreiben in geteilte Daten** aus Jobs: jeder Job schreibt in sein eigenes Ergebnis-Feld, der Hauptthread
    verteilt.
- **Erste Kandidaten** (nach der Messung in Teil B, das Budget entscheidet):
  1. **Animation:** Abtasten und Blenden je Figur (Animator, Look-At, Gesicht) parallel. Clip-Events werden
     gesammelt und nach der Parallel-Phase in ID-Reihenfolge ausgelöst.
  2. **Wahrnehmung der KI:** Sicht-Strahlen und Hören je simuliertem NPC parallel; Jolt-Abfragen sind lesend
     thread-sicher. Die Ereignisse an Lua gehen danach seriell raus.
  3. **Culling:** Sichtbarkeits-Raster und Draw-Listen je Zelle parallel, Kaskaden der Schatten getrennt.
  4. Später: Pfadsuche (A*) je Anfrage, Partikel.
- **Anzahl Threads:** Kerne − 1, höchstens 8; `[jobs] threads` in `engine.toml` (0 = automatisch, 1 = alles seriell
  für die Fehlersuche).
- **Tests:** `tests/core/test_jobs.cpp` (Reihenfolge der Ergebnisse, Ausnahmen, 1 Thread = seriell); bestehende
  Szenario-Tests laufen mit 1 und mit 4 Threads und müssen dasselbe ergeben.

## Teil D – Optimieren nach Messung
**Umgesetzt:** KI-LOD mit Obergrenze nach Anzahl (`[ai] max_simulated`, 32; die nächsten zum Spieler bzw. zur Kamera,
NPCs im Kampf oder Dialog immer; `ai.md`). Messung vorher und nachher: `docs/perf/2026-11-07-ki-lod-obergrenze.md`.

Erst nach A–C, gezielt nach den Zahlen: Animations-LOD (ferne Figuren seltener abtasten, ADR 0019 sieht es vor),
Schattenkaskaden seltener für ferne Kacheln, Draw-Bündel, Ladezeit. Jede Änderung mit Messung vorher und nachher in
`docs/perf/`.

## Reihenfolge und Umfang
| Teil | Inhalt | Umfang |
|---|---|---|
| A | ADR 0024 Tracy, Preset `profile`, Zonen in allen Systemen | klein bis mittel |
| B | Budgets, `--perf`-Szenarien Markt/Gasse/Stube, Bericht, Ladezeit | mittel |
| C | Job-System in `core`, Animation, Wahrnehmung, Culling parallel | groß |
| D | Optimierungen nach Messung | offen |

A und B zuerst: Sie liefern die Zahlen, nach denen C priorisiert wird.

## Entscheidungen des Projektinhabers (2026-11-07, alle wie vorgeschlagen)
1. **Zielhardware für die 60 FPS?** **Entschieden:** RTX 3080 Laptop bei 1600×900 und hohen Einstellungen muss 60 FPS
   sicher halten; Intel UHD bei 1280×720 und niedrigen Einstellungen (Schatten kürzer, weniger Lichter) ≥ 30 FPS.
2. **Tracy im Paket?** **Entschieden:** nein, nur im Preset `profile`. Das ausgelieferte Spiel öffnet keinen Port.
3. **Mess-Zeitpunkt in Leonberg?** **Entschieden:** 10:00 Uhr (Händler und Handwerker aktiv) und 21:00 Uhr (Fensterlichter,
   Fackeln); beide in jedem Szenario.
4. **Wie viele NPCs gleichzeitig?** **Entschieden:** die Leonberger aus den Routinen (heute 19 Bewohner plus Lager) als
   Grundlast, ein Stress-Szenario „Markt voll“ mit 60 NPCs um den Brunnen.
5. **Budget-Tabelle (Teil B) so festlegen?** **Entschieden:** ja, als Richtwert für die Systeme; verbindlich wird sie wie die
   Inhalts-Budgets in `render.md`, nach der ersten Messung.
6. **Eigenes Job-System statt Bibliothek?** **Entschieden:** ja, eigenes kleines in `core` (Pool und `parallelFor`). Eine
   Bibliothek erst, wenn Aufgaben-Graphen nötig werden, dann mit ADR.
