# 03 – Roadmap

Jede Phase hat Aufgaben (Checkboxen) und eine **Definition of Done (DoD)**. Eine Phase ist
abgeschlossen, wenn alle Aufgaben erledigt sind, die DoD erfüllt ist, CI grün ist und die
Modul-Doku den tatsächlichen Stand beschreibt. Phasen bauen aufeinander auf; innerhalb einer
Phase ist die Reihenfolge der Aufgaben eine Empfehlung.

**Aktueller Stand:** Phase **M7** (Scripting) läuft – ADR 0006 Lua 5.4.7 + sol2 akzeptiert (Projektinhaber, nach Abwägung der Alternativen), Teil A (Lua-VM, Sandbox, Lade-Reihenfolge, geprüfte Instanzen) steht (#127), eigene Bezeichner statt Gothic-Namen; als Nächstes Bindings/Story/Timer (B), Konsole/Hot-Reload/Beispielinhalt (C). Phase **M6** (Animation) abgeschlossen, **Meilenstein A vom Projektinhaber abgenommen** – eigene Animations-Laufzeit (ADR 0019), GPU-Skinning, animierter Held `farmer` aus Teilen, Fußgleiten über Clip-Eigengeschwindigkeit, Gesicht (Blinzeln, Ausdrücke, Lippen grob), Look-At, Attachments, Figuren-Montage zur Laufzeit (#122), Tiere Wolf/Keiler/Laufvogel (#106–#126). Werkzeuge: Flugmodus F3, „Position kopieren“ F6 mit `--cam/--yaw/--pitch/--fly` (#120), Autopilot `--walk` (#109). Welt-Spur: **W3** abgeschlossen (Begehung, Maßstab 1:1 endgültig; #110, #112, #114), Hanghäuser/Spalten/steile Wege korrigiert (#121), Glems und Parksee als Wasser (#108), Marktbrunnen aus dem Modell des Projektinhabers (#114); Pomeranzengarten (Geländer, Obelisk- und Gartenbrunnen) und Stadtkirche aus Modellen des Projektinhabers in Arbeit (Bodenregel, damit nichts im Gelände versinkt). Figuren-Spur: schwere Rüstung (Gothic-Richtung, dunkles Eisen, Nasal- und Kesselhelm) läuft; Schulterfalte als Skinning-Grenze dokumentiert (#119). Phase **M5** (Physik & Charaktersteuerung) abgeschlossen (#92, #99, #102, #103) – Jolt, statische Kollision mit `COL_`-Formen, Spielfigur (Zylinder) mit Gothic-Steuerung (Standard rennen, Shift gehen), Third-Person-Kamera, Springen (0,9/1,1 m), Kanten hochziehen (1,0/1,6/2,2 m), Fallschaden ab 4 m, Schwimmen/Tauchen (Vob-Typ `water`); Werte in `movement.toml`. Welt-Spur zusätzlich: Stadtmauer mit Mauerhäusern (#95, #97) und Schloss mit Pomeranzengarten per Skript (#100), als Nächstes Glems als Wasser und Begehung. Figuren-Spur zusätzlich: Figuren entstehen beim Bauen (#98), Rüstung leicht/mittel (#101), Kopfbedeckungen inkl. eigenem Nasalhelm (#104). Phase **M4** (Welt & Szene) abgeschlossen (#86; Vermerk: Editor-Bedienung noch nicht von Hand geprüft) – ADR 0005 (EnTT, `VobId`-Vertrag) und ADR 0017 (nlohmann-json) akzeptiert; `world::Scene` (Registry, `VobId`, Transform-Hierarchie, #44) und Weltformat `.g7world` v1 mit `--world`/`--save-world` und Testwelt `testworld/camp.g7world` (#47) stehen, dazu das Heightmap-Terrain (#51, #57: Kacheln mit LOD, Schatten, Splatmap bis 8 Schichten, Löcher; Cooker erkennt Splat/Löcher über den `terrain`-Block), die Vob-Typen `start`/`sound`/`trigger`/`mob` (#62) und Sichtbarkeit Teil A (#65: quadratischer Einbruch bei vielen Modellen auf `AssetManager::pruneCache` zurückgeführt und behoben, 5400 Modelle ~90 statt 13 FPS; Geometrie-Arena); Sichtbarkeit Teil B (#70 Raster, Distanz-/Größen-Culling mit Kategorie deco/gameplay; #73 Multi-Draw, auf Intel per `auto` aus), Weltwechsel (#75, Levelwechsel-Trigger, Zustand beim Zurückkehren erhalten) und Spielzeit mit Tag/Nacht (#79, Kurven in `assets/source/data/environment.toml`, vom Projektinhaber abgenommen, Mittagsdunst reduziert #80), Editor-Grundlage mit eigenen Gizmos (#84; ImGuizmo verworfen, Entscheidung Projektinhaber). ADR 0008 (keine Original-Gothic-Formate) akzeptiert. Welt-Spur: **W2** abgeschlossen (Heightmap, Splatmap, Tag/Nacht; #56, #59, #81); **W3** fertig bis auf die Begehung (M5); **W4** wartet auf die Aufnahmetour (Pose-Korrektur); **W5** läuft – Fachwerk-Generator mit festgelegtem Stil (`docs/design/leonberg-stil.md`, Stilwahl vom Projektinhaber an den Koordinator übertragen), Rückbau großer Neubauten zu schmalen Fachwerkhäusern (Entscheidung Projektinhaber), Palette, Alterung ohne Texturen; Leonberg-Kern 1405 Häuser, ~1,86 Mio. Dreiecke (#69, #71, #76, #78, #81, #83); als Nächstes Gauben/Schornsteine, dann Texturen. Figuren-Spur: **F1**, **F2** abgeschlossen; **F3** läuft – erster Mensch im Stil A mit Texturen (#68), Köpfe mit 15 Gesichts-Morphs (#82), als Nächstes Körper/Staturen und Kleidung für 5 Test-NPCs; **F5**-Platzhalter Wolf, Keiler, Laufvogel fertig (#72, #74, #77). Phase **M3** (Asset-System & Pipeline) abgeschlossen – VFS mit `.g7pak` v2 (zstd), `AssetManager` (Handles, Cache, asynchrones Laden), Engine-Anbindung (Mounts aus `engine.toml`), Hot-Reload (Texturen/Modelle, Shader), `g7-cook` (glTF → `.g7mesh`, Bilder → KTX2/UASTC als Vorgabe, Packen, Manifest mit inkrementellem Kochen; ADR 0016), `render` lädt KTX2 (BC7/BC5) und bevorzugt gekochte Meshes; DoD Ende-zu-Ende geprüft (Spiel nur aus `data.g7pak`, Textur-Hot-Reload; #34, #37, #40). Phase **M2** (Renderer-Grundlagen) abgeschlossen – OpenGL-Kontext (4.5+), glad, Debug-Output, RHI (Buffer, Texture, Sampler, Shader, Pipeline, Framebuffer), Shader-System (Includes, Hot-Reload), Kamera (Reverse-Z, Frustum, Debug-Flugkamera), statische glTF-Meshes (fastgltf, `--view-mesh`), Texturen (PNG/JPEG, Mipmaps, Anisotropie, sRGB), Materialien (Normal-Map, Emissive, Alpha-Test/Blend, beidseitig), Licht (Sonne, Hemisphären-Ambient, Punktlichter), Sonnenschatten (CSM), HDR mit Tonemapping (ACES), Distanznebel, Debug-Draw (F2) und das ImGui-Debugfenster (F1), Testszenen (`--scene`, Frustum-Culling, `--benchmark`, `--screenshot`) stehen; DoD-Szene mit 608 FPS (RTX 3080) bzw. 214 FPS (Intel UHD). M0 und M1 abgeschlossen (M1-Abnahme am echten Fenster: Aktionen im Log, Pause, sauberes Schließen).

| Phase | Thema | Meilenstein |
|---|---|---|
| M0 | Fundament | |
| M1 | Plattform & Hauptschleife | |
| M2 | Renderer-Grundlagen | |
| M3 | Asset-System & Pipeline | |
| M4 | Welt & Szene (+ Editor-Grundlage) | |
| M5 | Physik & Charaktersteuerung | |
| M6 | Animation | **A: „Held läuft durch Testwelt“** |
| M7 | Scripting | |
| M8 | Gameplay-Kern | |
| M9 | NPC-KI | |
| M10 | Dialoge & Quests | **B: „Vertical Slice – ein lebendiges Lager“** |
| M11 | Kampf | |
| M12 | Magie & Partikel | |
| M13 | Audio & dynamische Musik | |
| M14 | UI & Menüs | |
| M15 | Speichern/Laden | **C: „Spielbares Kapitel“** |
| M16 | Editor & Werkzeuge | |
| M17 | Atmosphäre, Performance, Release | **D: „Release-Kandidat Engine“** |

Parallel läuft die **Welt-Spur W1–W7** (Leonberg → Spielort, siehe Abschnitt „Welt-Spur“ am Ende und
`docs/design/leonberg-pipeline.md`). Sie besteht überwiegend aus Python-Werkzeugen in `tools/worldgen/`
und kann unabhängig von den Engine-Phasen bearbeitet werden; Abhängigkeiten sind je Phase angegeben.
Ebenso die **Figuren-Spur F1–F5** (Rig, Animationen, Figuren, Monster – `docs/design/characters-pipeline.md`).
Wie die Spuren zusammenarbeiten: `docs/coordination.md`.

---

## M0 – Fundament
- [x] Repository-Struktur, CMake (Module als Bibliotheken), CMakePresets, vcpkg-Manifest
- [x] `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`
- [x] core: Typen, Logging, Assertions, `Result<T>`, Stopwatch, `FixedStep`
- [x] Test-Infrastruktur (doctest), erste Tests
- [x] CI-Workflow (Windows + Linux, Build + Test + Smoke-Test) in `.github/workflows/ci.yml`
- [x] CI einmal erfolgreich durchgelaufen (ggf. Workflow korrigieren)
- [x] core: Dateisystem-Helfer (Datei lesen/schreiben, Pfade relativ zum Spielverzeichnis)
- [x] core: Mathe-Bibliothek festlegen (ADR 0002: glm) und Typ-Aliasse `Vec3`, `Quat`, `Mat4`, `Transform`
- [x] core: String-Hilfen (Hash `StringId` für schnelle Namensvergleiche, case-insensitive Vergleich)
- [x] core: Konfigurationsdatei laden (TOML, z. B. toml++)
- [x] core: einfacher Profiler-Hook (Makros, später Tracy)

**DoD:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` läuft auf
Windows und Linux grün; core hat > 80 % Testabdeckung seiner Logik.
✅ Erfüllt: CI grün auf Windows + Linux; core-Zeilenabdeckung 89 % (CI-Job `coverage`, gcovr).

## M1 – Plattform & Hauptschleife
- [x] SDL3 einbinden (vcpkg), Fenster erzeugen, Größenänderung, Vollbild
- [x] Eingabe: Tastatur, Maus (relativ für Kamera), Gamepad
- [x] **Aktions-Mapping** (Aktion „Vorwärts“, „Aktion“, „Waffe ziehen“ … → Tasten), aus Konfiguration
- [x] Hauptschleife in `Engine::run` mit Event-Polling, Fenster-Schließen, VSync/Frame-Limit (VSync wirkt ab M2 mit dem GL-Kontext)
- [x] Headless-Modus beibehalten (Tests/CI)
- [x] Zeitskalierung/Pause

**DoD:** Fenster öffnet sich, reagiert auf Eingaben (Log-Ausgabe der Aktionen), schließt sauber.
✅ Erfüllt: manuell am echten Fenster (Windows) geprüft – `--verbose` loggt `action move_forward`, `action action`, `action draw_weapon`, `action pause` (paused/resumed), Schließen beendet sauber; CI grün auf Windows + Linux.

## M2 – Renderer-Grundlagen
- [x] OpenGL 4.6 Core Context (ADR 0003; Mindestversion 4.5), Loader (glad), Debug-Callback
- [x] Dünne RHI-Schicht: Buffer, Texture, Shader/Program, Pipeline-State, Framebuffer
- [x] Shader-System (GLSL-Dateien, Includes, Hot-Reload)
- [x] Kamera (Perspektive, Frustum, Reverse-Z), Free-Fly-Debugkamera
- [x] Statische Meshes aus glTF laden (vorläufig direkt, ab M3 über asset)
- [x] Texturen (PNG/JPEG über stb_image; KTX2 → M3 mit dem Cooker, ADR 0014), Mipmaps, anisotrope Filterung
- [x] Material-Modell: Albedo, Normal, Alpha-Test (Laub!), Emissive – bewusst schlicht/stilisiert
- [x] Licht: gerichtete Sonne + Ambient, Punktlichter (Fackeln, Lagerfeuer) – Forward mit ≤ 8 Lichtern pro Objekt (Clustered bei Bedarf)
- [x] Schatten: Cascaded Shadow Maps für die Sonne (4 Kaskaden im Atlas, texelstabil, PCF)
- [x] Distanznebel, Gamma/Tonemapping (HDR-Ziel RGBA16F, ACES/Reinhard, exp²-Nebel in Horizontfarbe)
- [x] Debug-Draw (Linien, Boxen, Kugeln, Text im Raum; verdeckt gestrichelt, Bitmap-Schrift, Overlay F2)
- [x] Dear ImGui für Debug-Overlays (FPS, Statistiken; ADR 0015, eigene Backends, F1)

**DoD:** Testszene (Boden, einige Häuser/Bäume als glTF) mit Sonne, Schatten, Fackellicht und Nebel
bei ≥ 60 FPS auf Mittelklasse-Hardware.
- [x] **DoD erfüllt:** `assets/source/testscene/scene.toml` (Lager mit Hütten, Feuer, Markt, Zaun und Wald; Kenney-Modelle,
  CC0; 163 Objekte, 7 Fackellichter, Sonne mit 4 Schattenkaskaden, Nebel), `--benchmark` über 4 Viewpoints bei
  1600 × 900 im Release-Build: **RTX 3080 Laptop** langsamster Viewpoint 1,64 ms (608 FPS), p99 3,5 ms;
  **Intel UHD (integriert, schwächer als Mittelklasse)** 4,68 ms (214 FPS), p99 6,5 ms.

## M3 – Asset-System & Pipeline
- [x] Virtuelles Dateisystem: Mount-Punkte (Ordner, `.g7pak`), Priorität (Mods überschreiben) – `.g7pak` v1 ohne Kompression, Engine-Anbindung mit den Asset-Handles
- [x] Asset-Handles (typisiert, referenzgezählt), Cache, asynchrones Laden auf Worker-Threads – `AssetManager` im Modul asset
- [x] Engine-Anbindung: `Vfs` + `AssetManager` in `Engine`, Mounts aus `engine.toml`, Meshes/Bilder über Handles bzw. VFS (`[assets]`, `G7_DEV_ASSETS`, `local/` für Dateien außerhalb)
- [x] Hot-Reload für Texturen, Shader, Skripte im Entwicklungsmodus (Texturen/Modelle über `AssetManager::checkForChanges`, `[assets] hot_reload`; Shader seit M2; Skripte nutzen ab M7 dieselben Handles)
- [x] `g7-cook`: glTF → Laufzeit-Mesh/Skelett/Animation, PNG → KTX2 (BC7/BC5), OGG bleibt, Archiv packen; Laufzeit lädt KTX2 (libktx, aus M2 verschoben – ADR 0014)
  - ADR 0016: glTF → `.g7mesh`, Bilder → KTX2 (UASTC + zstd, Mips, Normal-Maps erkannt; Vorgabe von `g7-cook`), `.g7pak` v2 mit zstd; `asset` liest KTX2 als `TextureData`, `render` lädt BC7/BC5 hoch. Skelett/Animation folgen mit M6 (Spur F/M6)
- [x] Asset-Manifest mit Abhängigkeiten und Hashes (inkrementelles Kochen) – `<out>/.g7cook/manifest.txt`, `--full`

**DoD:** Spiel lädt ausschließlich aus `assets/cooked`, Änderung einer Textur wird ohne Neustart sichtbar.

## M4 – Welt & Szene (+ Editor-Grundlage)
- [x] EnTT-Registry, Komponenten-Grundsatz, `VobId`, Transform-Hierarchie (ADR 0005) – `world::Scene`, Registry nicht in der API
- [x] Weltformat `.g7world` als Text/JSON (v1, ADR 0017, `--world`, `--save-world`, Testwelt `testworld/camp.g7world`)
- [ ] Gekochte Binärvariante von `.g7world` – verschoben bis zum Bedarf (große Welten), **kein Teil der M4-DoD**
- [x] **Heightmap-Terrain** (Kacheln, LOD, Splatmap mit 4–8 Schichten, Löcher) – Grundlage für W2 (Leonberg-Gelände)
  - Teil A: `terrain`-Block in `.g7world` (Vertrag mit welt), `world::Heightfield`, `render::TerrainRenderer` (64er-Kacheln, 4 LOD-Stufen mit Schürzen, Culling, Schatten); Leonberg 2000×2000 mit 619 FPS (RTX 3080). Teil B: `splat` (bis 8 Schichten, Texture-Arrays) und `holes` (je Zelle) im `terrain`-Block, Cooker erkennt Splat-Karten als lineare Daten, `Heightfield::isHole`
- [x] Vob-Typen: Mesh, Licht, Sound-Emitter, Trigger, Startpunkt, Mob (Platzhalter)
  - `start` (Kamera in Augenhöhe, `--start`), `sound` (Daten + Debug-Draw, Abspielen mit der Audio-Phase), `trigger` (`world::TriggerSystem`, deterministische Enter/Leave-Ereignisse, `target` reserviert; in M4 mit der Kamera), `mob` (Mesh + Definition, Interaktion in M8); Debug-Draw (F2) für alle
- [x] Spielzeit & **Tag/Nacht-Zyklus**: Sonnenstand, Himmelsfarben (Verlauf je Uhrzeit), Sterne, Mond – `GameTime` (global, Sprünge als ein Ereignis), Kurven in `data/environment.toml`, Mond als Nachtlicht, Himmel mit Sonne/Mond/Sternen; Werte = Vorgaben bis zur Abnahme (Screenshots lokal)
- [x] Sichtbarkeit: Frustum-Culling, Distanz-Culling/LOD für Vobs; Innenräume über Portale/Zonen (später) – Mesh-LOD für Vobs und Portale/Zonen später (kein Teil der M4-DoD)
  - Stand Teil A: Überlinearität bei vielen Modellen behoben (Ursache `AssetManager::pruneCache`, O(n²) je Frame; 5400 eigene Modelle 83 → ~10 ms), Geometrie-Arena (alle Meshes in gemeinsamen Puffern, 0 Pufferbindungen je Frame), Benchmark mit Draws/Binds. Offen (Teil B): Instancing, Raster, Distanz-/Größen-Culling
  - Stand Teil B1: Raster (64-m-Zellen), Sichtweite `view_distance` und Größen-Culling `size_cull` (nur Deko; `category` deco/gameplay in `.g7world`, Mobs immer gameplay), Vorgaben in engine.toml. 
  - Teil B2: Multi-Draw nach Material-Werten (`glMultiDrawElementsIndirect`, Index über `baseInstance`), geteilte neutrale Texturen; Leonberg-Kern RTX 3,06 → 2,17 ms; auf Intel UHD ~10 % langsamer, daher `multi_draw = "auto"` (aus auf Intel) – offener Punkt in render.md
  - Hänger in Testläufen (lokal, Windows-CI `cook`) gelöst: Lost-Wakeup im basisu-Job-Pool von libktx 4.4.2; `g7-cook` kodiert basisu einthreadig und parallelisiert selbst über die Texturen (`asset.md`)
- [x] Mehrere Welten + Weltwechsel (Levelwechsel-Trigger) – `trigger.changeWorld`, Wechsel zwischen zwei Frames, Zustand verlassener Welten im Speicher (Spielstand: save), kein Pingpong; Testwelten Lager ↔ Höhle
- [x] **Editor-Grundlage**: Editor-Modus, Vobs auswählen/verschieben/drehen (Gizmos, ImGuizmo), Welt speichern – `gothar --editor` (`tools/editor`, `EngineTool`), **eigene Gizmos** (ImGuizmo verworfen, ADR 0015-Nachtrag), Auswahl per Klick und Liste, Inspektor, Modelle platzieren, Duplizieren/Löschen, Speichern mit `.bak`, Warnung bei Generator-Vobs (`generator`-Kopf); Undo/Wegnetz/Zonen in M16

**DoD:** Eine Testwelt mit Gelände, Lager-Hütten und Lagerfeuer wird geladen, Tag/Nacht läuft
sichtbar, im Editor lassen sich Vobs platzieren und speichern.

**M4 abgeschlossen (2026-10-03, Entscheidung Projektinhaber):** DoD erfüllt – Testwelt `testworld/camp.g7world` mit Gelände,
Lager und Lagerfeuer; Tag/Nacht mit Abnahme (#79, #80); Editor platziert und speichert Vobs (#84, Ende-zu-Ende-Test).
Vermerk: **Editor-Bedienung (Maus/Drag) vom Projektinhaber noch nicht von Hand geprüft** (automatisiert getestet sind
Gizmo-Mathematik und Operationen). Die Kollision statischer Welt-Meshes ist nach M5 verschoben.

## M5 – Physik & Charaktersteuerung
- [x] Vorarbeit: Modul `world_format` (`.g7world` ohne render/physics/EnTT), g7-cook linkt nur noch das – geprüft beim Konfigurieren
- [x] Jolt Physics integrieren (ADR 0004), Welt-Kollision aus statischem Mesh – Gelände und Architektur (aus M4 verschoben: „Statisches Welt-Mesh mit Kollisionsgeometrie“)
  - Jolt 5.6.0 (vcpkg, `nodeps` per FetchContent mit SHA256); Gelände als HeightField mit Löchern, Modelle aus `COL_`-Knoten (Vertrag mit welt/figuren, `asset.md`) bzw. Render-Mesh, Form je Modell geteilt; `.g7mesh` v2 mit Kollisionsteilen
  - Leonberg-Kern: Aufbau 1,13 s ohne `COL_` (1,69 Mio. Dreiecke), 0,25 s mit den `COL_HULL_` aus #93 (Release)
- [x] Raycasts/Shapecasts-API (Fokus, Kamera, KI-Sicht) – `raycast`, `sphereCast`, `overlapSphere` mit Layer-Masken, `Engine::physics()`
- [x] Charakter-Controller: gehen, rennen, schleichen, Treppen/Steigungen, rutschen an steilen Hängen
  - `physics::CharacterController` (Jolt CharacterVirtual), aufrechter Zylinder r 0,3/1,8 m statt Kapsel (scharfe Stufengrenze, gemessen, `physics.md`); `gameplay::PlayerMovement` mit Gothic-Gangart (Standard rennen, Shift gehen), Werte in `data/movement.toml` (Hot-Reload); Spielfigur am Startpunkt, Trigger melden die Figur; F3 freie Kamera
- [x] Springen, **Kanten hochziehen** (Kantenerkennung per Shapecast), Fallschaden
  - Sprung 0,9 m (Stand) bzw. 1,1 m (Rennen), keine Luftsteuerung; Kanten 1,0/1,6/2,2 m per Strahlen und Formtest, Gleitbahn bis M6; Fallschaden ab 4 m mit 10 LP/m (Log bis M8); Kletterplatz in der Testwelt (`START_KLETTERPLATZ`)
- [x] Schwimmen/Tauchen (Wasservolumen, Luftvorrat)
  - Vob-Typ `water` (Vertrag mit welt), `world::WaterBodies`; schwimmen ab hüfttiefem Wasser, tauchen mit sneak, auftauchen mit jump (Entscheidung Projektinhaber), Luft 30 s, Ertrinken 10 LP/s (Log bis M8), Wasser fängt Fälle ab; Platzhalter-Wasserfläche bis M17; Teich in der Testwelt (`START_TEICH`)
- [x] Third-Person-Kamera im Gothic-Stil: Verfolgung mit Trägheit, Kollision, Modi (Normal, Kampf, Dialog, Schwimmen)
  - Modi „normal“ (Trägheit für Position und Gieren, Mausneigung, `sphereCast` gegen Wände, `gameplay::ThirdPersonCamera`) und „schwimmen“ (bleibt über der Wasseroberfläche, beim Tauchen darunter) umgesetzt; der Kampfmodus folgt mit M11, der Dialogmodus mit M10 (dort eingetragen)
- [x] Trigger-Volumen (Betreten/Verlassen-Events) – seit M4 (`world::TriggerSystem`), seit M5 meldet die Spielfigur statt der Kamera (Levelwechsel per Laufen getestet)

**DoD:** Eine Kapsel-Figur bewegt sich mit Gothic-artiger Steuerung durch die Testwelt, klettert,
schwimmt; Kamera clippt nicht durch Wände.
**DoD erfüllt** (2026-10-03, PR M5 Teil E): Figur (Zylinder statt Kapsel, `physics.md`) läuft, rennt, schleicht und springt
mit Gothic-Steuerung durch die Testwelt, klettert die drei Kantenklassen am Kletterplatz, schwimmt und taucht im Teich;
die Kamera bleibt vor Wänden (Tests: `render_gpu` „Player GPU …“, Kamera sieht den Kopf nach 10 s Umherlaufen;
Screenshots `C:\GotharData\review\m5-dod`).

## M6 – Animation  → Meilenstein A
_Benötigt F1 (Referenz-Rig, Platzhalterfigur) und für den Meilenstein die Prio-A-Animationen aus F2._
- [x] Skelett + Skinning (GPU), glTF-Skins
  - Stand Teil A: glTF-Skins, Skelett, LOD-Teile, Morph-Targets, Clips und `events.toml` laden (`asset/SkinnedModel.hpp`, ADR 0019 eigene Laufzeit); CMake-Ziel `g7_figures`
  - Teil C: GPU-Skinning (`render::SkinnedMesh`, `MeshRenderer::drawSkinned`, Schatten), Held als animierte Figur (`[game] hero`, Vorgabe `farmer`, sonst Gliederpuppe), Fenster „Animation“ im Debug-UI (F1)
- [x] Clips, Sampling, Blending (Crossfade), additive Layer (Oberkörper getrennt) – `animation/Clip.hpp`, `Skeleton.hpp`, Overlay mit Knochenmaske (Teil B)
- [x] Animations-Zustandsautomat (datengetrieben), Übergänge mit Bedingungen – `animgraph.toml`, `Animator`, Graph der Menschen `data/anim/human.animgraph.toml` (Teil B)
- [ ] Animations-Events (Fußschritte, Treffer-Fenster, Item-Wechsel Hand/Gürtel)
  - Stand Teil B: Events feuern nach Vertrag §3 an einen Callback; Teil C zeigt die Events des Helden im Debug-UI; Verbraucher (Schritte, Treffer, Item-Wechsel) folgen mit Teil D bzw. M11/M13
- [ ] Root Motion für Interaktionen/Kampf, In-Place für Fortbewegung
  - Stand Teil B: `root_motion`-Zustände melden die Bewegung von `root`; Teil C: Klettern folgt der Root Motion, auf die Kante skaliert; Interaktionen/Kampf mit M10/M11
- [x] Attachments: Items an Knochen (Hand, Rücken, Gürtel), Rüstungs-/Kopfwechsel (Mesh-Tausch)
  - D1: Modelle an Sockets (`Engine::attachToPlayer`, mit Schatten, Debug-UI-Teststab). D2: Figuren zur Laufzeit aus Teilen (`assembleFigure`, gleich mit `gothar-chargen assemble`, CI-Vergleich), Held aus `farmer.figure.toml`, Kopf/Rüstung/Helm tauschen (`setPlayerPart`, `setPlayerCloth`, Debug-UI „Outfit“)
- [x] Morph-Targets für Gesichter (Lippen-Synchronisation grob, Blinzeln) – `FaceAnimator` (D1): Blinzeln, Ausdrücke, grobes Sprechen; Lippensynchronisation nach Audio mit M13
- [x] Look-At (Kopf dreht zu Gesprächspartner) – `LookAt` (D1): Hals und Kopf, Grenzen und Geschwindigkeit aus `[look_at]`; Gesprächspartner setzen mit M12
- [x] Tiere: Graphen für wolf, keiler, laufvogel, Root Motion mit Drehung, gemeinsame `AnimatedFigure` mit dem Helden, Test-Tiere und Fenster „Creatures“ mit Vorführung (D3); Monster-Vobs, KI und Kollision mit M9

**DoD / Meilenstein A:** Animierter Held läuft, rennt, springt, klettert, schwimmt durch die
Testwelt bei Tag und Nacht; Debug-UI zeigt Animationszustände.
**Meilenstein A abgenommen 2026-10-03 vom Projektinhaber** (Debug-Build 238ea82, „Animation ist ok“). Offen in M6: Teil D2 (Rüstungs-/Kopfwechsel zur Laufzeit) und D3 (Tiere).

## M7 – Scripting
- [ ] Lua 5.4 + sol2 (ADR 0006), Skript-VM pro Spielsitzung, Sandbox (kein `io`/`os`)
  - Stand Teil A: ADR 0006 akzeptiert, Lua 5.4.7 + sol2 3.5.0 privat in `script`; `ScriptVm` mit Sandbox (kein io/os/debug/load, `require` nur unterhalb der Skripte), Befehls- und Speichergrenze, Fehler mit Datei:Zeile; Anbindung pro Spielsitzung mit Teil C
- [ ] Modul-/Ordnerstruktur in `game/scripts`, Lade-Reihenfolge
  - Stand Teil A: Lade-Reihenfolge `lib/` → `data/` → Rest, je alphabetisch (`loadOrder`); Ordner und Beispielinhalt mit Teil C
- [ ] Instanz-System: `Item{...}`, `Npc{...}`, `Info{...}`, `Quest{...}` als deklarative Tabellen
  - Stand Teil A: Instanz-Arten (`defineKind`) mit Schema (Typen, Pflichtfelder, Bereiche, Verweise, unbekannte Felder), Prüfung nach dem Laden; die Arten selbst registriert gameplay mit Teil C
- [ ] Engine-API-Bindings (dokumentiert, generierte Referenz `docs/script-api.md`)
- [ ] Globale Story-Variablen, persistent (für Save)
- [ ] Timer/verzögerte Aufrufe, Ereignis-Hooks
- [ ] Hot-Reload im Entwicklungsmodus, Fehler mit Datei/Zeile im Log
- [ ] Ingame-Konsole (Lua-Befehle, Cheats wie `insert`, `goto`, `time`)

**DoD:** Items und NPC-Instanzen werden aus Lua definiert und per Konsole in die Welt gesetzt.

## M8 – Gameplay-Kern
- [ ] Attribute & Talente, Erfahrung, Stufen, Lernpunkte (Formeln in Skripten)
- [ ] Gilden + Einstellungs-Tabelle
- [ ] Items: Kategorien (Nahkampf, Fernkampf, Rüstung, Munition, Nahrung, Trank, Rune, Spruchrolle, Schriftstück, Schlüssel, Sonstiges), Wert, Bedingungen (Stärke X)
- [ ] Inventar (Spieler + NPC + Truhen), Ausrüsten, Gewicht optional (Gothic hat keins)
- [ ] **Fokus-System** (Ziel-Auswahl nach Blickrichtung/Distanz/Priorität)
- [ ] **Mob-Interaktion**: Zustandsfolge mit Animationen, Benutzer-Slots, Items verbrauchen/erzeugen (Schmieden, Braten)
- [ ] Truhen, Türen, Schlösser + Dietrich-Minispiel, Schlüssel
- [ ] Schlafen → Zeit vorspulen
- [ ] Item benutzen (Essen, Tränke, Lesen von Schriftstücken)
- [ ] Taschendiebstahl (Talent + Geschick)
- [ ] Besitzverhältnisse (Items und Bereiche gehören NPCs/Gilden) als Basis für Diebstahl-Reaktionen

**DoD:** Spieler kann Items aufheben, ausrüsten, Truhen knacken, am Amboss schmieden, schlafen.

## M9 – NPC-KI
- [ ] Wegnetz: Wegpunkte, Kanten, Freepoints; A*-Pfadsuche; Pfadglättung; Fallback-Navigation zwischen Netz und Position
- [ ] NPC-Zustandsautomat: Skript-Zustände `begin/loop/end`, Zustandswechsel, Unterbrechungen
- [ ] **Tagesabläufe** (Routinen) mit Zeitfenstern, Routinenwechsel per Skript (z. B. Kapitelwechsel)
- [ ] Freepoint-Belegung (Sitzplätze am Lagerfeuer etc.)
- [ ] **Wahrnehmung**: Sicht (Kegel + Raycast), Gehör (Lärmereignisse mit Radius), Reichweiten, Update-Takt nach Distanz
- [ ] Wahrnehmungs-Ereignisse: Spieler gesehen, Waffe gezogen, Kampf, Diebstahl, Betreten privater Bereiche, Zauber, Item angefasst
- [ ] Einstellungen (dauerhaft/temporär), Gruppenhilfe, Fliehen
- [ ] Monster-KI: Revier, Rudel, Fressen/Schlafen, Jagd, Flucht
- [ ] KI-LOD: weit entfernte NPCs „springen“ entlang ihrer Routine statt simuliert zu werden
- [ ] Debug-Ansicht: Wegnetz, aktueller Zustand/Routine pro NPC, Wahrnehmungsradien

**DoD:** 10 NPCs folgen über 24 Spielstunden fehlerfrei ihren Routinen und reagieren auf
gezogene Waffen und Betreten ihrer Hütte.

## M10 – Dialoge & Quests  → Meilenstein B
- [ ] Info-System: Bedingung, Beschreibung, Priorität, `important` (NPC spricht an), `permanent`, `onlyOnce`
- [ ] Dialog-Ablauf: Kamera-Schnitte (Über-die-Schulter), Sprachausgabe/Untertitel, Gesten-Animationen
- [ ] Kameramodus „Dialog“ der Third-Person-Kamera (aus M5: Schuss/Gegenschuss als Datensatz in `movement.toml` bzw. Dialogdaten)
- [ ] Auswahl-Menüs (Choices) innerhalb einer Info
- [ ] Handel-Bildschirm und Lernen über Dialog
- [ ] Tagebuch: Aufträge (laufend/erfolgreich/gescheitert), Einträge, Notizen
- [ ] Kapitelwechsel-Mechanik

**DoD / Meilenstein B (Vertical Slice):** Ein kleines Lager mit 5–10 NPCs mit Routinen, 3 Quests
(Botengang, Beschaffung, Konflikt), Handel, ein Lehrer, Truhen, Tag/Nacht – durchspielbar.

## M11 – Kampf
- [ ] Waffenmodi (Faust, Einhand, Zweihand, Bogen, Armbrust), Ziehen/Wegstecken
- [ ] Kameramodus „Kampf“ der Third-Person-Kamera (aus M5: näher, Blick auf den Gegner bei Ziel-Lock)
- [ ] Nahkampf: Angriffe mit Treffer-Fenstern aus Animations-Events, Kombos abhängig vom Talent, Parieren, Ausweichschritt
- [ ] Trefferprüfung (Waffen-Shapecast entlang der Animation), Treffer-Reaktionen, Rückstoß
- [ ] Schadensmodell: Schadensarten × Schutzwerte, kritische Treffer abhängig vom Talent
- [ ] Fernkampf: Zielen, Projektile mit Ballistik, Munition
- [ ] Bewusstlosigkeit vs. Tod, Plündern
- [ ] Kampf-KI: Abstand halten, Angriffsmuster pro Gegnertyp, Gruppenkampf, Rückzug
- [ ] Ziel-Lock im Kampf

**DoD:** Kampf gegen Mensch, Wolfsrudel und einen starken Gegner fühlt sich responsiv an; Talentstufen sind spürbar.

## M12 – Magie & Partikel
- [ ] Partikelsystem (GPU-Instancing, Emitter-Definitionen als Daten)
- [ ] Runen/Spruchrollen, Mana, Kreise, Wirken mit Aufladung
- [ ] Zaubertypen: Projektil, Fläche, Selbst, Verwandlung (in Tier), Kontrolle (Schlaf, Furcht, Telekinese), Beschwörung
- [ ] Visuelle Effekte (Licht, Partikel, Shader) und Trefferwirkungen

**DoD:** Feuerpfeil, Heilung, Schlaf, Verwandlung und Beschwörung funktionieren inkl. KI-Reaktion.

## M13 – Audio & dynamische Musik
- [ ] miniaudio-Integration (ADR 0007), Mixer-Busse (Musik, Effekte, Sprache, Ambient)
- [ ] 3D-Sound mit Abschwächung, Verdeckung (einfacher Raycast-Filter)
- [ ] Ambient-Zonen (Wind, Sumpf, Höhle), Zufalls-Einzelgeräusche
- [ ] Sprachausgabe mit Lippensync-Daten, Untertitel-Synchronisation
- [ ] **Dynamisches Musiksystem**: Musik-Zonen, Zustände (Standard/Bedrohung/Kampf) × Tag/Nacht, musikalische Übergänge auf Taktgrenzen, Stingers
- [ ] Fußschritt-Sounds nach Material

**DoD:** Musik wechselt hörbar sauber beim Betreten des Lagers, bei Gefahr und im Kampf.

## M14 – UI & Menüs
- [ ] Spiel-UI-Framework (ADR 0009): Layout, Texturen-Rahmen (9-Slice), Schrift (MSDF), Gamepad-Navigation
- [ ] HUD: Leben, Mana, Gegner-Leben, Fokus-Namen, Luftanzeige
- [ ] Inventar-Bildschirm (Kategorien, Item-Vorschau als 3D-Modell), Handel, Truhe
- [ ] Charakterbildschirm, Tagebuch-Bildschirm, Karte (falls Karten-Item)
- [ ] Hauptmenü, Optionen (Grafik, Audio, Steuerung), Ladebildschirm
- [ ] Lokalisierung (Schlüssel → Text-Tabellen DE/EN), Untertitel
- [ ] Bildschirmtexte (z. B. „Erfahrung +50“), Nachrichten

**DoD:** Das Vertical Slice ist vollständig ohne Debug-UI spielbar.

## M15 – Speichern/Laden  → Meilenstein C
- [ ] Serialisierung: Welt-Delta gegenüber Ausgangszustand (geänderte/entfernte/neue Vobs), NPC-Zustände, Inventare, Skript-Variablen, Tagebuch, Spielzeit
- [ ] Binärformat mit Versionsnummer + Migrationspfad, Metadaten (Screenshot, Spielzeit, Ort)
- [ ] Speicherplätze, Quicksave/Quickload, Speichern beim Weltwechsel (Zustand jeder besuchten Welt)
- [ ] Robustheit: atomisches Schreiben (temp + rename), Prüfsumme

**DoD / Meilenstein C:** Ein komplettes Kapitel (Inhalt eigener Wahl) ist mit Speichern/Laden durchspielbar.

## M16 – Editor & Werkzeuge
- [x] Autopilot `gothar --walk` (vorgezogen für die W3-Begehung): Route ablaufen, Protokoll `walk.jsonl` und Zusammenfassung, Screenshots; auch ohne Grafikgerät (`docs/modules/tools.md`, Vertrag mit welt)
- [ ] Wegnetz-Editor (Punkte setzen, verbinden, Freepoints, Validierung)
- [ ] Trigger-, Zonen- (Musik/Ambient), Licht- und Mob-Editor
- [ ] Vorschau von Tagesabläufen (Zeitregler im Editor, NPC-Geister an Routinen-Positionen)
- [ ] Prefabs / Vob-Vorlagen
- [ ] Gelände-Werkzeuge oder Import-Workflow aus Blender (ADR)
- [ ] Undo/Redo

**DoD:** Ein neuer Ort mit Wegnetz, Zonen und NPC-Routinen lässt sich ohne Texteditor bauen.

## M17 – Atmosphäre, Performance, Release → Meilenstein D
- [ ] Wetter: Regen (Partikel + nasse Oberflächen), Gewitter, Wind für Vegetation
- [ ] Wasser-Rendering (Reflexion, Brechung light), Unterwasser-Effekt
- [ ] Spiegelnde Fenster (Wunsch Projektinhaber, „Raytracing“) – Optionen: Reflexions-Proben/Cubemaps je Zone, Screen-Space-Reflections; echtes Raytracing nur mit dem optionalen Vulkan-Backend (eigene ADR); offener Punkt in `render.md`
- [ ] Post-Processing: Bloom, Farbkorrektur je Tageszeit/Zone, SSAO optional
- [ ] Vegetation: Instancing, Wind-Animation, Grasfelder
- [ ] Job-System, Multithreading für Animation/KI/Culling
- [ ] Welt-Streaming in Zellen, Ladezeiten-Optimierung
- [ ] Tracy-Profiling, Performance-Budget pro System
- [ ] Packaging (Installer/ZIP), Crash-Reporting (Minidumps), Versions-/Build-Info
- [ ] Gekochtes Format für Figuren und Animationen (`.g7skin`/`.g7anim`, ADR 0019 Folgearbeit) – spätestens vor dem ersten reinen `.g7pak`-Release; bis dahin laden Figuren und Clips aus losen glTF (`asset.md`)
- [ ] Optional: Vulkan-Backend hinter der RHI (ADR)

**DoD / Meilenstein D:** Release-Build läuft stabil ≥ 60 FPS in der größten Welt auf Zielhardware.

---

# Welt-Spur: Leonberg → Spielort

Spezifikation: `docs/design/leonberg-pipeline.md`. Werkzeuge in `tools/worldgen/` (Python ≥ 3.11),
Rohdaten außerhalb des Repos (`DATA_ROOT`).

**Aktueller Stand Welt-Spur:** W1 abgeschlossen (PR #24): `gothar-worldgen` erzeugt aus LGL- und OSM-Daten Terrain, `buildings.json` (5392 Gebäude), `streets.json` und `features.json` für Leonberg (Ursprung Marktbrunnen). **W2 läuft:** `export-terrain` schreibt `assets/source/worlds/leonberg/leonberg_terrain.g7world` mit `terrain`-Block: Heightmap und Splat-Karten in `generated/` (nicht versioniert), 7 Schichten aus Straßen, Gebäuden, OSM-Nutzung und Neigung, Platzhalter-Albedos; Tag/Nacht läuft (#79); **W2 abgeschlossen**. **W3 Teil 1:** `buildings` erzeugt 905 Klötzchen-Häuser der Altstadt (`.glb`, generiert), `assemble` setzt `leonberg.g7world` mit stabilen VobIds und Startpunkten zusammen (Regel für Editor-Handarbeit in der Design-Doku); Blender-Add-on für Handarbeit; die Begehung wartet auf M5. **W5-Vorarbeit:** Fachwerk-Regelwerk `buildings --mode medieval` (Stockwerke, Auskragung, Öffnungen, Fachwerk aus Musterdaten, Dachüberstand, Seeds, Budget) mit Platzhalter-Materialien; Begriffe und Zahlen in `building_rules.json` sind ein Entwurf für den Projektinhaber. **Rückbau §7** (Entscheidung 2026-10-03): große Neubauten werden durch schmale Fachwerkhäuser ersetzt (132 → 633 Häuser; steile historische Dächer und das Alte Rathaus bleiben). **Stil festgelegt** (Koordinator im Auftrag des Projektinhabers): Stilzuweisung, Muster-Katalog, Palette, Moos, steile Dächer; Stil-Referenzblatt `docs/design/leonberg-stil.md`. W3 wartet auf M5. **W4 läuft:** Die Kerne des Fassaden-Werkzeugs (`facade/`: 360°-Projektion, Fassaden-Entzerrung aus `buildings.json`, GPS-Posen, Override-Schema, `facade preview`) stehen und sind mit synthetischen Bildern getestet. Dazu kommen die Einzelbild-Extraktion per ffmpeg und der automatische Zeitabgleich von Video und GPS (`facade frames`). Die Annotations-Web-UI (`facade ui`: Karte, Fassadeneditor, Speichern als Override-JSON) steht und wurde vom Projektinhaber getestet; die Auswahllisten sind ein Entwurf zur Festlegung durch den Projektinhaber. Als Nächstes: Aufnahmetour, Prüfung mit echten Daten, Schritt 3b (Pose-Korrektur, Bildvergleich), W2-Splatmap.

## W1 – Geodaten-Import  (keine Engine-Abhängigkeit)
- [x] `tools/worldgen` als Python-Paket einrichten (pyproject, Lint/Format mit ruff, Tests mit pytest)
- [x] Konfiguration: `config/leonberg.toml` (Gebiet, Ursprung, Maßstab), `config/local.toml` (DATA_ROOT, nicht versioniert)
- [x] Download-Hilfe/Anleitung für LGL-Kacheln (DGM1, LoD2, DOP) und OSM-Ausschnitt
- [x] DGM1 → Heightmap (`terrain.r16` + `terrain.json`) im lokalen Koordinatensystem
- [x] LoD2 (CityGML) → `buildings.json` (Grundriss, Dachtyp, Trauf-/Firsthöhe, Bodenhöhe)
- [x] OSM → `streets.json`, `features.json`
- [x] Vorschau-PNG (Höhen, Grundrisse, Straßen) und Plausibilitätsprüfungen
- [x] Ursprungspunkt und Gebietsgrenzen am Luftbild prüfen und festlegen

**DoD:** Ein Befehl (`gothar-worldgen import leonberg`) erzeugt alle Zwischendaten reproduzierbar; Tests mit LGL-Testdaten laufen in CI.
✅ Erfüllt: `import leonberg` erzeugt alle Zwischendaten, zwei Läufe waren byte-identisch (2026-10-02). Der CI-Job `worldgen` mit LGL- und OSM-Testausschnitten ist grün unter Linux (py3.11) und Windows (py3.12), PR #24.

## W2 – Leonberg-Gelände in der Engine  (benötigt M2, M4-Terrain)
- [x] Heightmap-Import in das Terrain-System, Splatmap-Grundbelegung aus Straßen/Nutzung
  - `export-terrain` (PR #56 Heightmap, Splatmap mit 7 Schichten); Platzhalter-Albedos, endgültige Texturen mit W5
- [x] Testwelt `leonberg_terrain.g7world` mit Tag/Nacht
  - lädt mit `--time=HH:MM` (engines Tag/Nacht #79), Startpunkte Marktplatz und Übersicht; geprüft 12:00, 19:30, 23:00
- [x] Gewässer als `water`-Vobs (nach M5 Teil E): Glems und Parksee, Bachbett bzw. Becken in der Heightmap (weicht dort bewusst vom DGM ab)

**DoD:** Das Leonberger Gelände ist in der Engine sichtbar und (ab M5) begehbar.

## W3 – Klötzchen-Leonberg & Maßstabstest  (benötigt W1, W2; sinnvoll ab M5)
- [x] Blender-Add-on „Gothar Buildings“ Grundgerüst: `buildings.json` lesen, Baukörper + Dach als Massen, `.glb`-Export
  - Generator in Python (`buildings`, eigener glTF-Writer), Add-on für Handarbeit (Import/Zurückschreiben, `locked`)
- [x] Welt-Assembler v1: Terrain + Gebäude → `.g7world`
  - `assemble`: `leonberg.g7world` (1029 Vobs), `vob_ids.json`, Startpunkte; Engine lädt ohne Warnungen
- [x] Begehung mit Spielfigur/Kamera; Maßstabsfaktoren und Gassenverbreiterung festlegen (Ergebnis in `leonberg.toml` + Design-Doku)
  - Teil 1 statisch: `begehung` prüft Gassen, Gefälle, Türen, Mauer gegen die Spielfigur; Bericht mit Entscheidungspunkten E1–E6 in `docs/design/leonberg-begehung.md`, Stationen als Startpunkte (`data/leonberg/starts.json`)
  - Teil 2 mit `gothar --walk` (#109): `walk-routes` (Stationen, alle Wege, Tore/Pforten), `walk-report`; 17,4 km ohne Sturz, Hänger nur am Gelände an bekannten Steilstellen; bestätigt 1:1
  - E3 entschieden (Projektinhaber, 2026-10-03): Maßstab 1:1 endgültig, keine Gassenverbreiterung (`leonberg.toml`)
  - E1/E4/E5 umgesetzt: Türen nach Gelände (Sockel, Freitreppen, Obergeschoss-Eingänge, Abgänge), Spalten per Kollision geschlossen, steile Wege auf 42° geglättet

**DoD:** Graue Altstadt begehbar; Maßstabsentscheidung dokumentiert.

## W4 – Aufnahmen & Fassaden-Werkzeug  (keine Engine-Abhängigkeit)
- [ ] Aufnahmetour(en) nach Leitfaden (Abschnitt 6 der Design-Doku)
- [ ] Bild-Extraktion aus Insta360-Export, GPS-Zuordnung, optional SfM-Verfeinerung
  - Stand: Einzelbilder per ffmpeg, Zeitabgleich mit dem GPS-Track und Posen je Bild (`facade frames`, `frames.json`), synthetisch geprüft; SfM offen
- [ ] Fassaden-Ausschnitt + Entzerrung pro Gebäude
  - Stand: Entzerrung per Projektion auf die Fassadenebene umgesetzt (`facade/rectify.py`, `facade preview`), synthetisch geprüft; abhaken nach der Prüfung mit echten Aufnahmen
- [x] Annotations-Oberfläche → Override-JSON pro Gebäude
  - Override-Schema (`facade/overrides.py`) und Web-UI (`facade ui`, PR #53), vom Projektinhaber getestet (2026-10-03); Pose-Korrektur und Bildvergleich folgen als 3b
- [ ] Annotation der Häuser am Marktplatz (erste ~20 Gebäude)

**DoD:** Für jedes Haus am Marktplatz gibt es eine entzerrte Fassadenreferenz und eine Annotation.

## W5 – Fachwerk-Generator  (benötigt W3, W4)
- [ ] Modularer Baukasten + Trim-Sheets (Balken, Putz, Stein, Holz, Dach)
- [x] Regeln: Stockwerke, Auskragung, Fachwerk-Muster-Katalog, Öffnungen, Dachdeckung, Gauben, Schornsteine
  - Stand: Mechanik und Muster-Katalog, Dachdeckung (Palette), steile Dächer, Stilzuweisung, Schornsteine und Gauben umgesetzt (`--mode medieval`, `leonberg-stil.md`)
- [ ] Overrides aus W4 anwenden; Seeds für Variation; `locked`-Schutz für Handarbeit
  - Stand: `storeys`, `jettyM`, `frontFacade` (Öffnungen), `seed`, `locked`, `keep`, `age`, `dormers`, `chimneys` werden angewendet; abhaken mit den echten Annotationen
- [ ] LOD-Erzeugung, Kollisions-Mesh
  - Stand: Kollision umgesetzt (`COL_HULL_` je konvexem Baukörperteil, Ersatz `COL_`-Netz; Vertrag in `docs/modules/asset.md`); LOD offen
- [ ] Stil-Referenzblatt (Farben, Materialien, Alterung) in `docs/design/`
  - Stand: `docs/design/leonberg-stil.md` (Entscheidung 2026-10-03), Palette justiert, Alterung ohne Texturen umgesetzt (First-Durchhang, schiefe Ständer, unregelmäßige Fenster, Moos); Schmutz und Regenstreifen folgen mit Texturen

**DoD:** Der Marktplatz ist mittelalterlich und stilistisch geschlossen in der Engine zu sehen.

## W6 – Straßen, Mauer, Ausstattung  (benötigt W5, M4-Editor)
- [ ] Straßen/Plätze aus OSM → Splatmap, Rinnen, Stufen, Stützmauern
- [ ] Stadtmauer mit Toren
  - Stand: Ring mit Türmen, Tortürmen, Pforten, Treppen und Kollision umgesetzt (`gothar-worldgen citywall`, W-E2), Mauerhäuser auf der Linie (Außenseite als Mauer), Schloss als eigenes Modell (Blender-Skript, W-E3)
- [ ] Requisiten- und Vegetationsverteilung über Regeln/Masken
  - Stand: Marktbrunnen als Handmodell (Modell des Projektinhabers, per Skript an 1700 angepasst, W-E4)
- [ ] Wegnetz-Vorschlag aus Straßenachsen

**DoD:** Die komplette Altstadt ist ausgestattet und hat ein vorläufiges Wegnetz.

## W7 – Integration & Feinschliff  (benötigt M16, M17)
- [ ] Handarbeit im Editor, Zellen/Streaming, Performance-Budget
- [ ] Credits (LGL, OSM, Asset-Lizenzen) im Spiel
- [ ] Gebäudenutzungen für Gameplay festlegen (Schmiede, Taverne, Händler …)

**DoD:** Leonberg ist als fertiger Spielort im Vertical Slice / Kapitel nutzbar.

---

# Figuren-Spur: Figuren & Animationen

Spezifikation: `docs/design/characters-pipeline.md`, Liste: `docs/design/animation-list.md`.
Werkzeuge in `tools/chargen/` (Python, Blender-Add-on), Assets in `assets/source/characters/`.

**Aktueller Stand Figuren-Spur:** F1 abgeschlossen (bis auf das Kochen von Skin/Clips, das zu M6 gehört). F2 abgeschlossen: `build-set`,
`report`, Pose-Marker-Events; 36/36 Prio-A-Clips (18 Quaternius CC0, 18 Keyframe-Platzhalter) und 48 Clips Fortbewegung je Waffenmodus
(9 Sets). Kein Mixamo (öffentliches Repo). Eigengeschwindigkeit je Fortbewegungs-Clip in `events.toml` (`speed`, gegen Fußgleiten; M6-Prüfung). F3 läuft: Baukasten-Werkzeuge stehen, Stil A (realistisch, Texturen) entschieden,
Menschen aus MPFB2-Rezepten (`gothar-chargen human`), Textur-Vertrag, Gesichts-Morphs, 6 Grundkörper, 5 Köpfe, Kleidungs-Kit,
5 Test-NPCs; Figuren entstehen beim Bauen (`gothar-chargen assemble`, reines Python, nicht versioniert, §6.2); Rüstungs-Kit
leicht/mittel, Kopfbedeckungen. F5 läuft: Monster-Vertrag mit engine, Validator-Regeln und Werkzeuge stehen (seit der M6-Durchsicht auch: Füße
gleiten nicht, Liegeposen über dem Boden), erste Art `wolf` mit vollständigem
Mindest-Set (Platzhalter): `wolf`, `keiler`, `laufvogel`; offen: eigene Rigs/Arten mit Design-Doku (nach Stil-Entscheidung).

## F1 – Referenz-Rig & Konventionen  (keine Engine-Abhängigkeit; Voraussetzung für M6)
- [x] Referenz-Rig `assets/source/characters/rig/human_reference.blend` nach animation.md („Referenz-Skelett“) – T-Pose, erzeugt mit `gothar-chargen build-rig`, mit Gliederpuppe als Testfigur
- [x] Export-Einstellungen glTF dokumentiert (Maßstab, Ausrichtung, Skin, Morph-Targets) – characters-pipeline.md §2.1, `gothar-chargen export`
- [x] `tools/chargen` als Python-Paket (ruff, pytest) mit **Rig-Validator**, in CI eingebunden (Job `chargen`: Tests + `gothar-chargen validate --strict` über `assets/source/characters/`)
- [x] CC0-Platzhalterfigur auf das Referenz-Rig übertragen, validiert, mit `g7-cook` gekocht – `figures/placeholder_mannequin` (Quaternius-Mannequin, CC0) + Test-Clips `anims/human/none.glb` (`none/s_idle`, `s_walk`, `s_run` aus UAL1, Fußkontakt-Events), erzeugt mit `gothar-chargen build-placeholder`; `g7-cook` kocht die Figur als `.g7mesh` (ohne Skin)
- [x] Namenskonvention und `events.toml`-Format mit dem Engine-Strang abgestimmt (`docs/coordination.md`) – von engine bestätigt 2026-10-03 inkl. Kanal-Regel (Translation nur root/pelvis, keine Skalierung) und Event-Präzisierungen (characters-pipeline.md §3)
- Hinweis: Skin/Skelett/Animation kocht `g7-cook` erst mit M6 (`.g7skel`/`.g7anim`); bis dahin heißt „gekocht“ für die Platzhalterfigur: `.g7mesh` ohne Skin, und ein Clip-Set ohne Mesh ergibt ein leeres `.g7mesh`

**DoD:** Platzhalterfigur + 3 Test-Clips bestehen den Validator und liegen gekocht vor; M6 kann starten.
✅ Erfüllt, soweit ohne M6 möglich: Figur und Clips bestehen `gothar-chargen validate --strict` (CI); die Figur kocht als `.g7mesh`. Skin und Clips kocht `g7-cook` mit M6, dann ist nur noch die Prüfung nötig.

## F2 – Basis-Animationsset  (benötigt F1)
- [x] Bone-Mappings Quaternius UAL1/UAL2 (CC0) und Stapel-Übertragung in Blender (`gothar-chargen build-set`, Clip-Listen `data/clips/<set>.toml`) – **kein Mixamo** (öffentliches Repo, Entscheidung 2026-10-03); Mocap-Retargeting folgt in F4
- [x] Animations-Export: Sets nach Konvention, Pose-Marker → `events.toml` (dazu automatische Fußkontakt-/Lande-Events), Root Motion (z. B. `t_climb_low`) bzw. In-Place
- [x] Abgleich-Werkzeug Animationsliste ↔ vorhandene Clips (`gothar-chargen report`)
- [x] Alle **Prio-A**-Animationen (mindestens als Platzhalter-Qualität) – 36/36 (`gothar-chargen report`): 18 aus Quaternius bzw. abgeleitet, 18 als Keyframe-Platzhalter (`platzhalter-K`, Rezepte in `blender/keyframes.py`; Ersatz durch Mocap in F4); Sets `none`, `swim`, `dive`
- [x] Prio-B-Fortbewegung je Waffenmodus – 6 Modi × 8 Clips (Sets `fist`, `1h`, `2h`, `bow`, `cbow`, `mag`): Haltung aus Quaternius (fist/1h/cbow/mag) bzw. Keyframe-Pose (2h/bow), Fortbewegung geschichtet (Beine/Wirbelsäule aus `none`, Arme/Kopf aus der Haltung); Validator prüft Sprünge je Frame und geschlossene Schleifen

**DoD:** Meilenstein A ist mit diesen Animationen erreichbar; Bericht zeigt 0 fehlende Prio-A-Clips.
✅ Erfüllt: Bericht zeigt 0 fehlende Prio-A-Clips (CI prüft es); dazu 48/48 Clips Prio-B-Fortbewegung. Qualität: Platzhalter (Quaternius CC0 bzw. Keyframe), Ersatz durch Mocap in F4.

## F3 – Figuren-Baukasten  (benötigt F1)
- [x] 2 Grundkörper × 3 Staturen (MPFB2, Stil A realistisch), Köpfe als separate Meshes mit Morph-Targets
  - Stand: Werkzeug `gothar-chargen human` (Rezept → MPFB2 → Referenz-Rig → Teile body/head/hair, Texturen extern, Textur-Vertrag §2.3); erste Figur `farmer` (14,3 k Dreiecke, 3 LOD-Stufen). Köpfe mit 15 Gesichts-Morph-Targets (Viseme, Blinzeln, Ausdrücke; Vertrag mit engine §6.1, mit Zähnen/Zunge). Grundkörper 2 Geschlechter × 3 Staturen und 4 Köpfe (jung/alt, m/w, mit Bart) als Rezepte, frei kombinierbar je Geschlecht (Halsnaht und Haut beim Zusammenbau, alle 12 Kombinationen geprüft). 5 Köpfe (jung/mittel/alt, m/w, Bart/Schnurrbart).
- [ ] Haare/Bärte, erste Kleidungs-/Rüstungslinien (Lumpen, leicht, mittel)
  - Stand: Kleidungs-Kit (F3e): Stücke als eigene Teile je Statur (Männer: grobes Hemd, Fischerpullover, Stiefel, Stoffschuhe; Frauen: grobes Hemd, Mieder, langer Rock, flache Schuhe, Stiefeletten), Körper darunter ausgeblendet, neutrale geteilte Texturen + Palette; jedes Stück auf jeder Statur geprüft. 5 Test-NPCs: `farmer`, `peasant_woman`, `laborer`, `guard`, `old_man` (14–19 k Dreiecke). Rüstungs-Kit (F3g): leicht (eigenes Lederwams, Handschuhe) und mittel (Kettenhemd, gewickelte Hose und Stiefel, Handschuhe) auf allen 6 Staturen, eigene Farbtexturen, neutrale Namen, eigene Teile per `[derive]`; Testfiguren `test_armor_*`. Kopfbedeckungen (F3h): Kapuze, Lederkappe, Eisenhaube, Nasalhelm (eigene Geometrie) pro Statur, blenden das Haar aus (`hides`, §6.2). Köpfe auf Halshöhe der Grundkörper gebaut, Validator-Regel Augenhöhe (F3i). farmer aus Teilen statt mit eingebauter Kleidung (F3j; Haut stach in Bewegung durch die Hose). Offen: schwere Rüstung, eigene Stoff-Trim-Sheets
- [x] Baukasten-Werkzeug: Zusammensetzen, Passform-Prüfung, LODs, Farbvarianten – `gothar-chargen assemble` (Manifest `figures/<name>.figure.toml`, Palette), `fit.*`/`lod.*`/`mesh.budget` im Validator, LOD-Vertrag mit engine (characters-pipeline.md §2.2); getestet mit eigenen Testteilen (`parts/test`, Figuren `test_plain`, `test_rags`). ADR 0018 (MPFB2) angenommen. Seit 2026-10-03 Figuren beim Bauen statt im Repo: Teile mit LODs und Zusammenbau-Daten, `assemble` in reinem Python (Vertrag §6.2)
- [ ] Stil-Referenzblatt Figuren (gemeinsam mit W5)
  - Stand: Stilentscheidung Figuren gefallen (Stufe A realistisch mit Texturen, 2026-10-03; Stilproben-Seite für den Projektinhaber)

**DoD:** 5 unterscheidbare NPCs in der Engine, alle auf dem Referenz-Rig.
Stand: die 5 Test-NPCs liegen als `.glb` vor (strikt gültig); „in der Engine“ hängt an M6 (Skin/Clips kochen).

## F4 – Gothic-spezifische Animationen  (benötigt F2; für M8–M11)
- [ ] Mocap-Workflow testen (2–3 Dienste), Entscheidung dokumentieren
- [ ] Mob-Interaktionen, Item-Benutzung, Ambient-Routinen, Dialog-Gesten
- [ ] Nahkampf je Talentstufe, Fernkampf, Magie, Treffer/Tod/Bewusstlos
- [ ] Alle **Prio-B**-Animationen fertig

**DoD:** Vertical Slice (Meilenstein B) ohne Platzhalter-Animationen.

## F5 – Monster  (benötigt F1; für M9/M11)
- [x] CC0-Platzhalter für 3 Arten (Rudeltier, Keiler, Laufvogel) – Monster-Vertrag mit engine (characters-pipeline.md §7.1); `wolf` (22 Knochen, 0,85 m), `keiler` (25 Knochen, 0,95 m), `laufvogel` (12 Knochen, 1,6 m), je 12/12 Clips des Mindest-Sets (Quaternius CC0 + Keyframe-Platzhalter, `gothar-chargen monster`/`build-set`)
- [ ] Eigene Rigs + Mindest-Sets je Art, Design-Doku der Arten
- [x] Validator-Regeln für Monster-Rigs – Rig je Art nach Pfad, Pflichtknochen, Ausrichtungs-Hinweise, Größe relativ zur Art, Clip-Modus = Art, `anim.root_motion` (s_walk/s_run vorwärts, t_turn_l/r drehen root); Werkzeuge `gothar-chargen monster`, Rezepte `advance`/`keyposes`

**DoD:** Drei Monsterarten mit vollständigem Mindest-Set in der Engine.
