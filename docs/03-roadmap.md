# 03 – Roadmap

Jede Phase hat Aufgaben (Checkboxen) und eine **Definition of Done (DoD)**. Eine Phase ist
abgeschlossen, wenn alle Aufgaben erledigt sind, die DoD erfüllt ist, CI grün ist und die
Modul-Doku den tatsächlichen Stand beschreibt. Phasen bauen aufeinander auf; innerhalb einer
Phase ist die Reihenfolge der Aufgaben eine Empfehlung.

**Aktueller Stand:** Phase **M12** (Magie und Partikel) läuft – Partikelsystem mit Emittern als Daten (#221), Zauber als Inhalt: Runen, Spruchrollen, Kreise, Mana (#223), Wirken des Helden mit Runenplätzen, Aufladen, Feuerpfeil, Heilung, Schlaf (#236), Beschwörung eines Wolfs (#237); Entscheidungen Z1–Z9 vom Projektinhaber; als Nächstes Verwandlung (Z7), Zauber-KI und Szenario. Phase **M11** (Kampf) weitgehend abgeschlossen (offen: Waffen-Shapecast entlang der Animation) – Kampfkern mit Treffern, Schaden, Parade, Bewusstlosigkeit (#208), Heldensteuerung, Plündern, Kampf-KI (#212), Fernkampf mit Bogen und Armbrust und M11-Szenario (#215), Kampfkamera und Zweihänder (#219), Kampfzüge nach den Clips (#226); Entscheidungen K1–K9 und R1–R4 vom Projektinhaber (K8: Wiederaufstehen am Ort). Phase **M10** (Dialoge und Quests) abgeschlossen, **Meilenstein B vom Projektinhaber abgenommen** (2026-10-05, „Testlager ist ok“) – Infos und Antworten (#187), Dialogkamera, Gesten, Mund und Blick (#188), Handel in Gulden und Lehrer (#189), Tagebuch und Kapitel (#191), Platzhalter-Geschichte mit drei Aufträgen (#192), Leonbergs Bewohner mit Routinen, Händlern und Lehrer (#196), Sprechtext-Schlüssel und Werkzeug `tools/voice` (#204). Weiteres Engine: NPCs öffnen und schließen Türen (#199, #232), Raum-Ambient aus Innenzonen (#207), Innenkamera (#206), Tageslicht-Fensterlichter (#225), Tiere im Tempo der Clips (#202), stiller NPC-Halt außerhalb der Simulation behoben (#228); CI: Auto-Merge mit Pflicht-Checks auf main, Neustart abgebrochener Jobs (#214), Runtime-Tests in Gruppen (#216), feste Zufallszahlen (#213). Welt-Spur: **W7** läuft – Routinen-Orte, Freepoints und Mobs an 30 Häusern (#190), begehbare Häuser mit Erdgeschoss-Raum (#195), eingerichtet mit Mobs, Herd mit Rauchfang, Licht und Wegpunkten (#197, #198, #203), Türen geschlossen (#210), Innenzonen (#218), Hausrat (#224), große Erdgeschosse in Räume geteilt (#227), Fenster mit Tageslicht (#230), ein Texturordner (#229); als Nächstes mehr Licht und dichtere Einrichtung. Figuren-Spur: Figurenvielfalt F3v abgeschlossen (Alltagskleidung #193, Gilden-Figuren und `figure_sets.toml` #194, Sets je Geschlecht #201, Schürzen und langer Rock #205; CC0-Lizenzprüfung #186); Kampf-Clips M11 (#209, #217), Zweihänder sowie Magie-Gegenstände und -Clips M12 (#222); **F5** eigene Monster (vom Projektinhaber ausgewählt, `docs/design/monsters.md`): Werkzeug `gothar-chargen creature`, Schinder (#231, #234), Quaderbuckel (#233), Glemsmahr (#235); als Nächstes Bergleu (Boss). Phase **M9** (NPC-KI) abgeschlossen (bis auf Kampf- und Zauber-Ereignisse) – Wegnetz-Pfadsuche (#161), Zustände und Tagesabläufe mit Freepoints und KI-LOD (#165), Wahrnehmung, Waffe ziehen, private Bereiche, Warnungen mit eigenen Zurufen (#173), Einstellungen, Kameraden, Fliehen (#175), Tier-KI mit Revier, Rudel 2–4, Tag/Nacht, Drohen wie Gothic (#179), Fenster „AI“ und DoD-Szenariotest (#180); Fixes: Timer im Handler (#168), Hang und Fortschritt (#174), Zahlenformat byte-gleich (#167). Welt-Spur zusätzlich: Dachfirst-Fix (#158), Hinterhäuser (#160), Wegnetz Leonberg mit Pforten (#171, #181), Türen verlegt (#176, #178), Sitzbank (#163), Gebäudenutzungen: Vorschlag (#182), 30 Häuser ausgewählt (Koordinator, vom Projektinhaber übertragen); als Nächstes Routinen-Orte, dann begehbare Häuser (5) und Treppen. Figuren-Spur zusätzlich: Haut durch Kleidung und Fransen (#155), Routinen- und Reaktions-Clips (#162), Dialog-Gesten gegen dlg/a_neutral (#164, #169), Axt (#166), Ziehen/Wegstecken (#172); als Nächstes Figurenvielfalt (Gilden-Outfits, figure_sets.toml). Phase **M8** (Gameplay-Kern) abgeschlossen – Teil A Werte, Talente, Stufen nach Gothic-1-Formel in Lua, Inventar ohne Gewicht, Held `pc_hero`, Wegnetz-Block typisiert (#137; Vertrag `waynet` #135); Teil B Fokus, Vob-Typ `item`, Aufheben mit `t_pickup_ground`, Inventar-Fenster (#140); Teil C Mobs nach `mobs.toml` v1 (Fassung E, Schiedsentscheidung Koordinator; #139), Truhen/Türen mit Schloss, Schlüssel und Dietrich (Bruchchance 50/25/5 %, Entscheidung Projektinhaber), Amboss mit Rezept „Grobes Schwert“, Bett mit Schlaf bis 8/12/20/0 Uhr und vollen LP/Mana (#143, #145); Item-Modelle im Lager, Weltwechsel-Fix, `render_gpu` in zwei ctest-Einträge geteilt (#150); Textur-Cache je VFS-Pfad (#138). Teil D: Benutzen im Stand (Apfel +5, Brot +10, Trank +40 LP), Schriftstücke, Taschendiebstahl wie Gothic 1 (Talent + Geschick ≥ `pickpocket_dex`), Besitz an Items und Mobs mit Ereignis `theft` (#154; Werte Projektinhaber). LOD-Auswahl statischer Modelle (#156), Schatten-Akne behoben (#157) und Schatten-Kadenz (#170), CI-Gruppen für GPU-Tests (#159). Phase **M7** (Scripting) abgeschlossen – Lua 5.4.7 + sol2 (ADR 0006 akzeptiert), Sandbox, Lade-Reihenfolge, geprüfte Instanzen (#127), Bindings mit generierter `docs/script-api.md`, Story/Timer/Ereignisse (#130), Konsole (^), Hot-Reload, Beispielinhalt mit eigenen Namen (#133). Welt-Spur zusätzlich: Pomeranzengarten und Kirche aus Modellen des Projektinhabers mit Bodenregel (#131, #132), Zwinger-Pforte und Durchgang Zwerchstraße (#136), prozedurale Texturen auf allen 1405 Häusern und Kopfsteinpflaster (#144, nach drei Proberunden vom Projektinhaber abgenommen), Mob-Modelle Truhe/Amboss/Bett/Tür (#148), Häuser-LOD-Stufen (#152), dunklere Sockel (#153); Dachfirst-Fix für ~760 Kernhäuser in Arbeit. Figuren-Spur zusätzlich: schwere Rüstung (#128), abgetragene Stoffe `gothar-chargen fabrics` (#142), Platzhalter-Clips für Mobs und Item-Benutzung (#139, #141, Bett #147), Waffen und Handgegenstände F6 (#146, #149); als Nächstes Haut-durch-Kleidung und Fransen (F3o). Phase **M6** (Animation) abgeschlossen, **Meilenstein A vom Projektinhaber abgenommen** – eigene Animations-Laufzeit (ADR 0019), GPU-Skinning, animierter Held `farmer` aus Teilen, Fußgleiten über Clip-Eigengeschwindigkeit, Gesicht (Blinzeln, Ausdrücke, Lippen grob), Look-At, Attachments, Figuren-Montage zur Laufzeit (#122), Tiere Wolf/Keiler/Laufvogel (#106–#126). Werkzeuge: Flugmodus F3, „Position kopieren“ F6 mit `--cam/--yaw/--pitch/--fly` (#120), Autopilot `--walk` (#109). Welt-Spur: **W3** abgeschlossen (Begehung, Maßstab 1:1 endgültig; #110, #112, #114), Hanghäuser/Spalten/steile Wege korrigiert (#121), Glems und Parksee als Wasser (#108), Marktbrunnen aus dem Modell des Projektinhabers (#114); Pomeranzengarten (Geländer, Obelisk- und Gartenbrunnen) und Stadtkirche aus Modellen des Projektinhabers in Arbeit (Bodenregel, damit nichts im Gelände versinkt). Figuren-Spur: schwere Rüstung (Gothic-Richtung, dunkles Eisen, Nasal- und Kesselhelm) läuft; Schulterfalte als Skinning-Grenze dokumentiert (#119). Phase **M5** (Physik & Charaktersteuerung) abgeschlossen (#92, #99, #102, #103) – Jolt, statische Kollision mit `COL_`-Formen, Spielfigur (Zylinder) mit Gothic-Steuerung (Standard rennen, Shift gehen), Third-Person-Kamera, Springen (0,9/1,1 m), Kanten hochziehen (1,0/1,6/2,2 m), Fallschaden ab 4 m, Schwimmen/Tauchen (Vob-Typ `water`); Werte in `movement.toml`. Welt-Spur zusätzlich: Stadtmauer mit Mauerhäusern (#95, #97) und Schloss mit Pomeranzengarten per Skript (#100), als Nächstes Glems als Wasser und Begehung. Figuren-Spur zusätzlich: Figuren entstehen beim Bauen (#98), Rüstung leicht/mittel (#101), Kopfbedeckungen inkl. eigenem Nasalhelm (#104). Phase **M4** (Welt & Szene) abgeschlossen (#86; Vermerk: Editor-Bedienung noch nicht von Hand geprüft) – ADR 0005 (EnTT, `VobId`-Vertrag) und ADR 0017 (nlohmann-json) akzeptiert; `world::Scene` (Registry, `VobId`, Transform-Hierarchie, #44) und Weltformat `.g7world` v1 mit `--world`/`--save-world` und Testwelt `testworld/camp.g7world` (#47) stehen, dazu das Heightmap-Terrain (#51, #57: Kacheln mit LOD, Schatten, Splatmap bis 8 Schichten, Löcher; Cooker erkennt Splat/Löcher über den `terrain`-Block), die Vob-Typen `start`/`sound`/`trigger`/`mob` (#62) und Sichtbarkeit Teil A (#65: quadratischer Einbruch bei vielen Modellen auf `AssetManager::pruneCache` zurückgeführt und behoben, 5400 Modelle ~90 statt 13 FPS; Geometrie-Arena); Sichtbarkeit Teil B (#70 Raster, Distanz-/Größen-Culling mit Kategorie deco/gameplay; #73 Multi-Draw, auf Intel per `auto` aus), Weltwechsel (#75, Levelwechsel-Trigger, Zustand beim Zurückkehren erhalten) und Spielzeit mit Tag/Nacht (#79, Kurven in `assets/source/data/environment.toml`, vom Projektinhaber abgenommen, Mittagsdunst reduziert #80), Editor-Grundlage mit eigenen Gizmos (#84; ImGuizmo verworfen, Entscheidung Projektinhaber). ADR 0008 (keine Original-Gothic-Formate) akzeptiert. Welt-Spur: **W2** abgeschlossen (Heightmap, Splatmap, Tag/Nacht; #56, #59, #81); **W3** fertig bis auf die Begehung (M5); **W4** wartet auf die Aufnahmetour (Pose-Korrektur); **W5** läuft – Fachwerk-Generator mit festgelegtem Stil (`docs/design/leonberg-stil.md`, Stilwahl vom Projektinhaber an den Koordinator übertragen), Rückbau großer Neubauten zu schmalen Fachwerkhäusern (Entscheidung Projektinhaber), Palette, Alterung ohne Texturen; Leonberg-Kern 1405 Häuser, ~1,86 Mio. Dreiecke (#69, #71, #76, #78, #81, #83); als Nächstes Gauben/Schornsteine, dann Texturen. Figuren-Spur: **F1**, **F2** abgeschlossen; **F3** läuft – erster Mensch im Stil A mit Texturen (#68), Köpfe mit 15 Gesichts-Morphs (#82), als Nächstes Körper/Staturen und Kleidung für 5 Test-NPCs; **F5**-Platzhalter Wolf, Keiler, Laufvogel fertig (#72, #74, #77). Phase **M3** (Asset-System & Pipeline) abgeschlossen – VFS mit `.g7pak` v2 (zstd), `AssetManager` (Handles, Cache, asynchrones Laden), Engine-Anbindung (Mounts aus `engine.toml`), Hot-Reload (Texturen/Modelle, Shader), `g7-cook` (glTF → `.g7mesh`, Bilder → KTX2/UASTC als Vorgabe, Packen, Manifest mit inkrementellem Kochen; ADR 0016), `render` lädt KTX2 (BC7/BC5) und bevorzugt gekochte Meshes; DoD Ende-zu-Ende geprüft (Spiel nur aus `data.g7pak`, Textur-Hot-Reload; #34, #37, #40). Phase **M2** (Renderer-Grundlagen) abgeschlossen – OpenGL-Kontext (4.5+), glad, Debug-Output, RHI (Buffer, Texture, Sampler, Shader, Pipeline, Framebuffer), Shader-System (Includes, Hot-Reload), Kamera (Reverse-Z, Frustum, Debug-Flugkamera), statische glTF-Meshes (fastgltf, `--view-mesh`), Texturen (PNG/JPEG, Mipmaps, Anisotropie, sRGB), Materialien (Normal-Map, Emissive, Alpha-Test/Blend, beidseitig), Licht (Sonne, Hemisphären-Ambient, Punktlichter), Sonnenschatten (CSM), HDR mit Tonemapping (ACES), Distanznebel, Debug-Draw (F2) und das ImGui-Debugfenster (F1), Testszenen (`--scene`, Frustum-Culling, `--benchmark`, `--screenshot`) stehen; DoD-Szene mit 608 FPS (RTX 3080) bzw. 214 FPS (Intel UHD). M0 und M1 abgeschlossen (M1-Abnahme am echten Fenster: Aktionen im Log, Pause, sauberes Schließen).

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
- [x] Lua 5.4 + sol2 (ADR 0006), Skript-VM pro Spielsitzung, Sandbox (kein `io`/`os`)
  - ADR 0006 akzeptiert, Lua 5.4.7 + sol2 3.5.0 privat in `script`; `ScriptVm` mit Sandbox (kein io/os/debug/load, `require` nur unterhalb der Skripte), Befehls- und Speichergrenze, Fehler mit Datei:Zeile (A); die Engine lädt `game/scripts` beim Start (C)
- [x] Modul-/Ordnerstruktur in `game/scripts`, Lade-Reihenfolge – `lib/` → `data/` → Rest (A); Ordner, VFS-Mount `scripts/` und Beispielinhalt (C)
- [x] Instanz-System: `Item{...}`, `Npc{...}`, `Info{...}`, `Quest{...}` als deklarative Tabellen – Arten mit Schema (A), `Item`/`Npc`/`Info`/`Quest`/`Routine` aus `gameplay::defineContentKinds` (C); weitere Felder mit M8 ff.
- [x] Engine-API-Bindings (dokumentiert, generierte Referenz `docs/script-api.md`) – `ScriptVm::bind` (B); `gothar --script-api=…`, ctest `game.script_api` prüft, dass die Datei aktuell ist (C)
- [ ] Globale Story-Variablen, persistent (für Save)
  - Stand Teil B: Tabelle `Story`, `storyForSave` (nur Daten) und `setStory`; Spielstand mit M15
- [x] Timer/verzögerte Aufrufe, Ereignis-Hooks – `after`/`every`/`cancel` in Spielzeit (`ScriptVm::tick`), `on`/`emit` (B); `world_loaded`, `scripts_reloaded` (C)
- [x] Hot-Reload im Entwicklungsmodus, Fehler mit Datei/Zeile im Log – geänderte Skripte laden neu, `Story` bleibt (`[assets] hot_reload`)
- [x] Ingame-Konsole (Lua-Befehle, Cheats wie `insert`, `goto`, `time`) – Taste ^ (Aktion `console`), Verlauf; `insert`, `teleport` (`goto` ist in Lua ein Schlüsselwort), `time`, `where`

**DoD:** Items und NPC-Instanzen werden aus Lua definiert und per Konsole in die Welt gesetzt.

## M8 – Gameplay-Kern
- [x] Attribute & Talente, Erfahrung, Stufen, Lernpunkte (Formeln in Skripten) – `gameplay::Character`, `data/progression.lua` (Stufe n: 500·n(n+1)/2, 10 Lernpunkte), Held = `Npc "pc_hero"` (A)
- [x] Gilden + Einstellungs-Tabelle – `Attitudes`/`attitude(a, b)` in `data/guilds.lua`, Auswertung mit M9 (A)
- [x] Items: Kategorien (Nahkampf, Fernkampf, Rüstung, Munition, Nahrung, Trank, Rune, Spruchrolle, Schriftstück, Schlüssel, Sonstiges), Wert, Bedingungen (Stärke X) – Schema prüft `category`, `requires` beim Ausrüsten (A)
- [x] Inventar (Spieler + NPC + Truhen), Ausrüsten, Gewicht optional (Gothic hat keins) – Spieler/NPC und Ausrüsten (A), Truhen (C1)
- [x] Wegnetz-Block der `.g7world` lesen/schreiben (Vertrag mit welt, world.md „Wegnetz“; Pfadsuche M9) (A)
- [x] **Fokus-System** (Ziel-Auswahl nach Blickrichtung/Distanz/Priorität) – `gameplay::selectFocus`, `data/focus.toml`, Name über dem Ziel (B)
- [x] Items in der Welt (Vob-Typ `item`), Aufheben mit der Aktionstaste und `none/t_pickup_ground` (Event `pickup`), Inventar-Fenster (Tab), `drop_item` (B)
- [x] **Mob-Interaktion**: Zustandsfolge mit Animationen, Benutzer-Slots, Items verbrauchen/erzeugen (Schmieden, Braten) – Ablauf, Slots, Clips (`data/mobs.toml`), Lua-`Mob` (C1); Rezepte (`Recipe`) am Amboss (C2)
- [x] Truhen, Türen, Schlösser + Dietrich-Minispiel, Schlüssel – Bruchchance 50/25/5 % nach Talent (C1)
- [x] Schlafen → Zeit vorspulen – bis Morgen/Mittag/Abend/Mitternacht, danach LP/Mana voll (C2)
- [x] Item benutzen (Essen, Tränke, Lesen von Schriftstücken) – nur im Stand, Wirkung beim Event `use` (D)
- [x] Taschendiebstahl (Talent + Geschick) – wie Gothic 1: mit Talent und Geschick ≥ `pickpocket_dex` sicher, sonst bemerkt (D)
- [x] Besitzverhältnisse (Items und Bereiche gehören NPCs/Gilden) als Basis für Diebstahl-Reaktionen – `owner` an Items und Mobs, Ereignis `theft`; Reaktionen mit M9 (D)

**DoD:** Spieler kann Items aufheben, ausrüsten, Truhen knacken, am Amboss schmieden, schlafen.

## M9 – NPC-KI
- [x] Wegnetz: Wegpunkte, Kanten, Freepoints; A*-Pfadsuche; Pfadglättung; Fallback-Navigation zwischen Netz und Position – `ai::Waynet`, NPCs mit eigener Kapsel, `npc_goto`, Wegnetz und Routen in F2 (A)
- [x] NPC-Zustandsautomat: Skript-Zustände `begin/loop/end`, Zustandswechsel, Unterbrechungen – Lua-Art `State` (begin/loop/finish), Befehlsliste (`npc_goto`, `npc_play` …), `npc_start_state` (B)
- [x] **Tagesabläufe** (Routinen) mit Zeitfenstern, Routinenwechsel per Skript (z. B. Kapitelwechsel) – `routine` am Npc, `set_routine`, `insert_npc`; Testlager: vier Leute mit Tagesablauf (`camp_people()`) (B)
- [x] Freepoint-Belegung (Sitzplätze am Lagerfeuer etc.) – `npc_goto_freepoint` reserviert den nächsten freien (B)
- [x] **Wahrnehmung**: Sicht (Kegel + Raycast), Gehör (Lärmereignisse mit Radius), Reichweiten, Update-Takt nach Distanz – Schleichen und Nacht verkürzen die Sicht, Werte in `data/perception.lua` (C1)
- [ ] Wahrnehmungs-Ereignisse: Spieler gesehen, Waffe gezogen, Kampf, Diebstahl, Betreten privater Bereiche, Zauber, Item angefasst – umgesetzt: gesehen, Waffe gezogen (Taste `draw_weapon`, 1h-/Faust-Haltung), Diebstahl, fremder Mob, privater Bereich (`trigger.owner`), Geräusch; Warnungen und Drohen in `ai/perceptions.lua` (C1); offen: Kampf (M11), Zauber (M13)
- [x] Einstellungen (dauerhaft/temporär), Gruppenhilfe, Fliehen – `ai/attitudes.lua` (`npc_attitude`, `set_attitude`, `set_temp_attitude`), Hilferuf an befreundete Gilden (`assess_call`, `npcs_near`), Feiglinge fliehen (`npc_flee`); Fliehen bei wenig Leben mit M11 (C2)
- [x] Monster-KI: Revier, Rudel, Fressen/Schlafen, Jagd, Flucht – Tiere sind Npcs mit `species` (Kapseln in `data/creatures.toml`), Verhalten in `ai/monsters.lua`, Werte in `data/creatures.lua`; Rudel `insert_pack`, Drohen und Angriff (bis M11: verfolgen) nach Entscheidung des Projektinhabers (D)
- [x] KI-LOD: weit entfernte NPCs „springen“ entlang ihrer Routine statt simuliert zu werden – über 80 m (zurück unter 75 m) (B)
- [x] Debug-Ansicht: Wegnetz, aktueller Zustand/Routine pro NPC, Wahrnehmungsradien – Fenster „AI“ (F1: Zustand, Tagesablauf, Befehlsliste, Wahrnehmung, Einstellung je NPC; ein Klick zeigt Sichtkegel und Hörweite in F2), Wegnetz und Routen in F2 (E)

**DoD:** 10 NPCs folgen über 24 Spielstunden fehlerfrei ihren Routinen und reagieren auf
gezogene Waffen und Betreten ihrer Hütte.
*Nachgewiesen* durch den Szenariotest `tests/runtime/test_engine_m9_scenario.cpp`: vier Leute und sechs Tiere
(Wolfsrudel, Keiler, zwei Laufvögel) einen Spieltag lang im Zeitraffer ohne Skriptfehler im Zustand ihres
Tagesablaufs am richtigen Ort; die Wache warnt binnen 30 Ticks vor gezogener Waffe und bemerkt den Spieler in
ihrem Bereich binnen 30 Ticks (E).

## M10 – Dialoge & Quests  → Meilenstein B
- [x] Info-System: Bedingung, Beschreibung, Priorität, `important` (NPC spricht an), `permanent`, `onlyOnce` – gesagte Infos in `Story.told`, `important` in 3 m Sichtweite, `approach` geht auf den Spieler zu (A)
- [x] Dialog-Ablauf: Kamera-Schnitte (Über-die-Schulter), Sprachausgabe/Untertitel, Gesten-Animationen – Untertitel; Sprachausgabe später über die Zeilen-Schlüssel (E4); dlg-Gesten additiv gegen dlg/a_neutral, Mund und Blick (B)
- [x] Sprechtexte mit Schlüsseln (Vorschlag B, vom Koordinator bestätigt): Texte inline in Lua (E2), `gothar-voice scan` vergibt `<info>_NN` in Quelltext-Reihenfolge (say, choice, Antwort-Tabellen) und Zurufe `svm_<gilde>_<m|f>_<anlass>_NN` (Projektinhaber: Stimme je Gilde und Geschlecht, `Npc.voice` überschreibt) in `assets/source/voice/lines.de.json`; die Engine schlägt den Schlüssel über (Info, Text) nach; Werkzeug `tools/voice` (Streamlit) für TTS-Takes, ins Repo nur der gewählte Take `voice/de/<key>.wav`, alle Takes lokal unter `DATA_ROOT/voice/takes`
- [x] Kameramodus „Dialog“ der Third-Person-Kamera (aus M5: Schuss/Gegenschuss als Datensatz in `movement.toml` bzw. Dialogdaten) – `data/dialog.lua` (B)
- [x] Auswahl-Menüs (Choices) innerhalb einer Info – `choice(text, fn)` (A)
- [x] Handel-Bildschirm und Lernen über Dialog – Tauschhandel in Gulden (Info `trade = true`, `data/trade.lua`), Lehrer über `teach_menu` (`data/teaching.lua`); Händler: alter Mann, Lehrer: Holzfäller (C)
- [x] Tagebuch: Aufträge (laufend/erfolgreich/gescheitert), Einträge, Notizen – `lib/diary.lua` (in Story gespeichert), Fenster „Tagebuch“ auf der Aktion log (N bzw. J) (D)
- [x] Kapitelwechsel-Mechanik – `set_chapter(n, titel)`, Ereignis `chapter_changed` (D)

**DoD / Meilenstein B (Vertical Slice):** Ein kleines Lager mit 5–10 NPCs mit Routinen, 3 Quests
(Botengang, Beschaffung, Konflikt), Handel, ein Lehrer, Truhen, Tag/Nacht – durchspielbar.
*Inhalt* (Platzhalter-Geschichte „Bauern und Wache“, eigene Texte, Teil E): Testlager mit Torwache, Bäuerin, Holzfäller
(Lehrer: Stärke), altem Mann (Händler), Tieren; Aufträge Botengang (Essen für den Holzfäller), Beschaffung (Grobes
Schwert für die Wache, am Amboss geschmiedet), Konflikt (der Ring der Bäuerin: abkaufen, von der Wache holen lassen
oder stehlen); danach Kapitel 2. *Nachgewiesen* headless in `tests/runtime/test_engine_m10_scenario.cpp`.
**Meilenstein B vom Projektinhaber abgenommen, 2026-10-05** („Testlager ist ok“).

## M11 – Kampf
- [x] Waffenmodi (Faust, Einhand, Zweihand, Bogen, Armbrust), Ziehen/Wegstecken – Fernkampfwaffen mit eigener Taste (R1)
- [x] Kameramodus „Kampf“ der Third-Person-Kamera (aus M5: näher, Blick auf den Gegner bei Ziel-Lock) – `[camera.combat]`, weich überblendet, drinnen der nähere Abstand (K5)
- [x] Nahkampf: Angriffe mit Treffer-Fenstern aus Animations-Events, Kombos abhängig vom Talent, Parieren, Ausweichschritt – Gothic-1-Tasten, Maus als Zweitbelegung; bis zu den Clips eine Zeitleiste (A, B)
- [ ] Trefferprüfung (Waffen-Shapecast entlang der Animation), Treffer-Reaktionen, Rückstoß
- [x] Schadensmodell: Schadensarten × Schutzwerte, kritische Treffer abhängig vom Talent – Waffe + Stärke − Schutz, mindestens 5, kritisch 0/10/20 % (K2, K3; Teil A)
- [x] Fernkampf: Zielen, Projektile mit Ballistik, Munition – Bogen und Armbrust des Helden, Trefferchance nach Talent auf das Ziel, Pfeile bleiben stecken bzw. liegen (R1–R4; E)
- [x] Bewusstlosigkeit vs. Tod, Plündern – bewusstlos 30 s, Schlag auf Liegende tötet, Plündern, Zeugen (K7; A, C)
- [x] Kampf-KI: Abstand halten, Angriffsmuster pro Gegnertyp, Gruppenkampf, Rückzug – `ai/combat.lua`: zwei zugleich, Flucht bei wenig Leben, Tiere jagen (D)
- [x] Ziel-Lock im Kampf – das nächste NPC, das fokussierte zuerst, vor dem Helden bevorzugt (K5; B)

**DoD:** Kampf gegen Mensch, Wolfsrudel und einen starken Gegner fühlt sich responsiv an; Talentstufen sind spürbar.

## M12 – Magie & Partikel
- [x] Partikelsystem (GPU-Instancing, Emitter-Definitionen als Daten) – `data/fx/*.toml`, prozedurale Sprites, Lichter, `fx()` (A)
- [x] Runen/Spruchrollen, Mana, Kreise, Wirken mit Aufladung – `Spell`-Instanzen, `castBlocked` (B), Wirken des Helden mit Runenplätzen 4–9, Aufladen, Feuerpfeil/Heilung/Schlaf (C1)
- [x] Zaubertypen: Projektil, Fläche, Selbst, Verwandlung (in Tier), Kontrolle (Schlaf, Furcht), Beschwörung – Wolfsgestalt (C2), Wolf rufen (C2), Schlaf/Schrecken (C1, D), zaubernde NPCs (D); Telekinese nach M17 verschoben
- [x] Visuelle Effekte (Licht, Partikel) und Trefferwirkungen – Schweif, Einschlag, Beschwörungswirbel, Brennen (D2), `none/t_hit_magic`; Shader-Effekte nach M17 verschoben; Meilenstein-Szenario `test_engine_m12_scenario.cpp` (E)

**DoD:** Feuerpfeil, Heilung, Schlaf, Verwandlung und Beschwörung funktionieren inkl. KI-Reaktion.

## M13 – Audio & dynamische Musik
- [ ] miniaudio-Integration (ADR 0007), Mixer-Busse (Musik, Effekte, Sprache, Ambient)
- [ ] 3D-Sound mit Abschwächung, Verdeckung (einfacher Raycast-Filter)
- [ ] Ambient-Zonen (Wind, Sumpf, Höhle), Zufalls-Einzelgeräusche
- [x] Sprachausgabe mit Lippensync-Daten, Untertitel-Synchronisation
  - Teil D: gewählter Take `voice/<sprache>/<key>.wav` (WAV vorerst, OGG mit echten Takes und ADR) als 3D-Stimme am Sprecher (voll bis 4 m); die Zeile dauert Take + 0,3 s, Untertitel folgen; Mund `vis_aa` aus der Lautstärke (30 Hz); Musik −6 dB beim Sprechen; Zurufe ebenso; ohne Take wie bisher
  - Quelle: gewählte Takes `assets/source/voice/<sprache>/<key>.wav` → Cooker `voice/<sprache>/<key>.ogg`; fehlt die Datei, nur Untertitel mit Lesedauer aus der Textlänge
- [x] **Dynamisches Musiksystem**: Musik-Zonen, Zustände (Standard/Bedrohung/Kampf) × Tag/Nacht, musikalische Übergänge auf Taktgrenzen, Stingers
  - Teil C: `data/music.toml`, `audio::MusicPlayer` (Sample-genau verkettet, Wechsel auf der nächsten Taktgrenze bzw. am Segmentende), Zustand aus echten Feinden mit 5 s Hysterese, außerhalb der Zonen Stille (Bedrohung und Kampf mit `common`), Stinger quest/level_up/death/chapter, Platzhalter `gothar-audio music`; DoD-Szenario F in `tests/runtime/test_engine_m13_music.cpp`
- [ ] Fußschritt-Sounds nach Material

**DoD:** Musik wechselt hörbar sauber beim Betreten des Lagers, bei Gefahr und im Kampf.

## M14 – UI & Menüs
- [ ] Spiel-UI-Framework (ADR 0009): Layout, Texturen-Rahmen (9-Slice), Schrift (MSDF), Gamepad-Navigation
- [ ] HUD: Leben, Mana, Gegner-Leben, Fokus-Namen, Luftanzeige
- [ ] Inventar-Bildschirm (Kategorien, Item-Vorschau als 3D-Modell), Handel, Truhe
- [ ] Charakterbildschirm, Tagebuch-Bildschirm, Karte (falls Karten-Item)
- [ ] Hauptmenü, Optionen (Grafik, Audio, Steuerung), Ladebildschirm
- [ ] Lokalisierung (Schlüssel → Text-Tabellen DE/EN), Untertitel
  - Gesprochene Texte kommen aus `assets/source/voice/lines.<sprache>.json` (eine Datei je Sprache)
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
- [ ] Magie: Telekinese (Zaubertyp „Kontrolle“) – verschoben aus M12, Entscheidung Projektinhaber 2026-10-08
- [ ] Magie: Shader-Effekte beim Wirken und Treffen (Verzerrung, Glühen) – verschoben aus M12, Entscheidung Projektinhaber 2026-10-08

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
- [x] Modularer Baukasten + Trim-Sheets (Balken, Putz, Stein, Holz, Dach)
  - Stand: eigene prozedurale Texturen (Putz, Fußband, Regenschlieren, Bruchstein, Balken, Biberschwanz, Moos, Bretter) auf allen Häusern, Palette als Tönung, Kopfsteinpflaster im Gelände; drei Proberunden vom Projektinhaber abgenommen (2026-10-04)
- [x] Regeln: Stockwerke, Auskragung, Fachwerk-Muster-Katalog, Öffnungen, Dachdeckung, Gauben, Schornsteine
  - Stand: Mechanik und Muster-Katalog, Dachdeckung (Palette), steile Dächer, Stilzuweisung, Schornsteine und Gauben umgesetzt (`--mode medieval`, `leonberg-stil.md`)
- [ ] Overrides aus W4 anwenden; Seeds für Variation; `locked`-Schutz für Handarbeit
  - Stand: `storeys`, `jettyM`, `frontFacade` (Öffnungen), `seed`, `locked`, `keep`, `age`, `dormers`, `chimneys` werden angewendet; abhaken mit den echten Annotationen
- [x] LOD-Erzeugung, Kollisions-Mesh
  - Stand: Kollision umgesetzt (`COL_HULL_` je konvexem Baukörperteil, Ersatz `COL_`-Netz; Vertrag in `docs/modules/asset.md`); LOD: jedes Haus mit `_lod1` (vereinfacht, ca. 31 %) und `_lod2` (Baukörper mit Fenstern, ca. 5 %), Vertrag in `docs/coordination.md`; die Auswahl nach Entfernung baut engine
- [ ] Stil-Referenzblatt (Farben, Materialien, Alterung) in `docs/design/`
  - Stand: `docs/design/leonberg-stil.md` (Entscheidung 2026-10-03), Palette justiert, Alterung umgesetzt (First-Durchhang, schiefe Ständer, unregelmäßige Fenster, Moos; mit den Texturen Schmutz-Fußband, Regenschlieren und Moos-Ziegel)

**DoD:** Der Marktplatz ist mittelalterlich und stilistisch geschlossen in der Engine zu sehen.

## W6 – Straßen, Mauer, Ausstattung  (benötigt W5, M4-Editor)
- [x] Straßen/Plätze aus OSM → Splatmap, Rinnen, Stufen, Stützmauern
  - Stand: `gothar-worldgen streetworks` (W-E): 54 Steintreppen auf gleichmäßig steigendem Gelände, ca. 12,5 km Rinnen (Mitte unter 6 m Breite, sonst seitlich), ca. 140 Stützmauern (trocken, an Haus und Stadtmauer gemörtelt; Bergseite hält den Hang, Talseite mit Brüstung) mit eingeebneter Straßenhälfte; Türen behalten ihren Zugang, Wegnetz besser als vorher (116 statt 121 Teile)
- [ ] Stadtmauer mit Toren
  - Stand: Ring mit Türmen, Tortürmen, Pforten, Treppen und Kollision umgesetzt (`gothar-worldgen citywall`, W-E2), Mauerhäuser auf der Linie (Außenseite als Mauer), Schloss als eigenes Modell (Blender-Skript, W-E3)
  - Stand: Pforte in der Zwingermauer vor dem Garten-Westtor; Durchgang (Override `passages`) durch das Haus an der Pforte Zwerchstraße Nord
- [ ] Requisiten- und Vegetationsverteilung über Regeln/Masken
  - Stand: Marktbrunnen als Handmodell (Modell des Projektinhabers, per Skript an 1700 angepasst, W-E4)
  - Stand: Pomeranzengarten aus drei Modellen des Projektinhabers (Geländer mit Pavillons, Obelisk- und zwei Gartenbrunnen), Parterre nach dem Geländer, an den LoD2-Pavillons ausgerichtet (W-E5)
  - Stand: Stadtkirche aus dem Modell des Projektinhabers statt des LoD2-Gebäudes (W-E6; mittig auf dem LoD2-Grundriss, Entscheidung K1)
  - Stand: Bodenregel für alle Handmodelle – nichts versinkt im Gelände; Garten auf drei waagrechten Terrassen mit Stützmauern und Treppen, Kirche mit Fundament, Prüfung `qa/grounding.py` (#131, #132)
  - Stand: Gassen belebt (`outdoor.json`, `outdoor.py`): ca. 2000 Requisiten an den Hauswänden nach Nutzung, Bäume aus OSM, Obstbäume und Büsche in den Höfen, Gras an den Wandfüßen (~3700 Mesh-Vobs, deco); Wege, Türen, Routinen-Orte und Wegnetz bleiben frei, das Wegnetz bleibt gleich
  - Stand: Gassen dichter: Gruppen (Fassstapel, Holzstöße, Kistenstapel, Karren) an den Wänden und am Straßenrand, ca. 6150 Requisiten, 12 Marktstände auf dem Marktplatz (~8900 Gassen-Vobs); Pfade und Fahrbahnen bleiben frei
  - Stand: Zunftschilder über den Türen der 16 Werkstätten, Läden, Tavernen und Wachen (Ausleger mit eigenem Symbol: Krug, Brezel, Hackbeil, Hufeisen, Becken, Mörser, Hellebarde, Pokal, Tuchballen, Waage, Schere, Stiefel, Säge, Krug; keine Wappen)
- [x] Wegnetz-Vorschlag aus Straßenachsen – `gothar-worldgen waynet`: ca. 3200 Punkte (1245 an Türen), Hauptnetz 94 %, Freepoints an Brunnen, Markt, Toren, Beeten; Pforte am Schlosshang und bei (−137, 76); Autopilot 30 Wege A→B (Spieler und NPCs)

**DoD:** Die komplette Altstadt ist ausgestattet und hat ein vorläufiges Wegnetz.

## W7 – Integration & Feinschliff  (benötigt M16, M17)
- [ ] Handarbeit im Editor, Zellen/Streaming, Performance-Budget
- [ ] Credits (LGL, OSM, Asset-Lizenzen) im Spiel
- [x] Gebäudenutzungen für Gameplay festlegen (Schmiede, Taverne, Händler …) – `uses-suggest` (OSM/ALKIS), Auswahl des Koordinators in `data/leonberg/uses.json` (30 Häuser), Routinen-Wegpunkte, Freepoints und Mobs (`docs/design/leonberg-routinen-orte.md`)
  - Stand: Treppen ins Obergeschoss der 5 begehbaren Häuser (höchstens 35°, Kollision als glatte Rampe, Deckenausschnitt mit Geländer), Obergeschoss mit eigenen Räumen, Fenstern und Licht; Betten und Truhen oben, Wegpunkte `…_TREPPE`, `…_TREPPE_OBEN`, `…_OBEN…`, Innenzonen je Geschoss und über der Treppe

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
F6 läuft: 12 Waffen und Handgegenstände (`gothar-chargen build-items`, Validator `item.*`), zuletzt Besen, Krug und Axt für M9; Essen/Trinken und Bogenhaltung mit den Gegenständen nachgerichtet.

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
  - Stand Bärte (2026-10-08): eigene **Stoppeln** `beard_stubble` für alle 8 Männerköpfe (`gothar-chargen stubble`, weicher Rand über Dichtebänder, 600 Dreiecke, Morphs; an 6 Figuren); ein eigener **Vollbart** ist zurückgestellt (Projektinhaber: später mit besserer Technik; Probebilder Haarkarten + Unterhülle in `C:\GotharData\review\f3-baerte\`)
  - Stand: Kleidungs-Kit (F3e): Stücke als eigene Teile je Statur (Männer: grobes Hemd, Fischerpullover, Stiefel, Stoffschuhe; Frauen: grobes Hemd, Mieder, langer Rock, flache Schuhe, Stiefeletten), Körper darunter ausgeblendet, neutrale geteilte Texturen + Palette; jedes Stück auf jeder Statur geprüft. 5 Test-NPCs: `farmer`, `peasant_woman`, `laborer`, `guard`, `old_man` (14–19 k Dreiecke). Rüstungs-Kit (F3g): leicht (eigenes Lederwams, Handschuhe) und mittel (Kettenhemd, gewickelte Hose und Stiefel, Handschuhe) auf allen 6 Staturen, eigene Farbtexturen, neutrale Namen, eigene Teile per `[derive]`; Testfiguren `test_armor_*`. Kopfbedeckungen (F3h): Kapuze, Lederkappe, Eisenhaube, Nasalhelm (eigene Geometrie) pro Statur, blenden das Haar aus (`hides`, §6.2). Köpfe auf Halshöhe der Grundkörper gebaut, Validator-Regel Augenhöhe (F3i). farmer aus Teilen statt mit eingebauter Kleidung (F3j; Haut stach in Bewegung durch die Hose). Schwere Rüstung (F3l): Brustplatte, Schulterstücke, Beinschienen, Panzerhandschuhe über der mittleren Linie, Kesselhelm; dunkles Eisen, eigene Teile. Abgetragene Stoffe (F3n): Stoff-Bibliothek (ambientCG) + gebackene Kleidungstexturen mit `wear`-Alterung (`gothar-chargen fabrics`). Haut durch Kleidung (F3o): Haut unter der eigenen Kleidung der Grundkörper entfernt, Prüfung in Bewegung `gothar-chargen poke` (Regel `fit.poke_motion`, Test-NPCs ≤ 6,3 statt bis 106 cm²), Spalt Hemdsaum/Hosenbund geschlossen; ausgefranste Säume mit Alpha-Test (`MASK`) an groben Hemden und Rock. Figurenvielfalt (F3v): 13 Köpfe (8 m, 5 w) mit Frisur- und Bart-Kits je Kopf (`hair_<kopf>`, neutrale Texturen, Farbe über die Palette; nur CC0 laut Datei-Kopf, automatisch geprüft); Alltags-Kits `garb_<m|w>_<statur>` (Hemd, weite Hose, Stufenrock, Kutte/Rock, Tunika, saubere Varianten, Schürzen aus Leinen und Leder als eigene Tafel, Gürtel, Strohhut); Gilden-Figuren: 33 Manifeste in `data/figure_sets.toml` (Vertrag mit engine: `citizen`, `craftsman`, `guard`, `farmer`, `hunter`, `outcast`) und die benannten Leonberger `smith`, `innkeeper`, `market_woman`, `gate_guard`, `guard_captain`. Figurenvielfalt F3v damit abgeschlossen; Nachbesserung: Schürzen als fallender Stoff (Hüfte, Falten, schmalerer Saum), langer Rock auch für schlanke Frauen (`[inside]`)
- [x] Baukasten-Werkzeug: Zusammensetzen, Passform-Prüfung, LODs, Farbvarianten – `gothar-chargen assemble` (Manifest `figures/<name>.figure.toml`, Palette), `fit.*`/`lod.*`/`mesh.budget` im Validator, LOD-Vertrag mit engine (characters-pipeline.md §2.2); getestet mit eigenen Testteilen (`parts/test`, Figuren `test_plain`, `test_rags`). ADR 0018 (MPFB2) angenommen. Seit 2026-10-03 Figuren beim Bauen statt im Repo: Teile mit LODs und Zusammenbau-Daten, `assemble` in reinem Python (Vertrag §6.2)
- [ ] Stil-Referenzblatt Figuren (gemeinsam mit W5)
  - Stand: Stilentscheidung Figuren gefallen (Stufe A realistisch mit Texturen, 2026-10-03; Stilproben-Seite für den Projektinhaber)
  - Stand 2026-10-08: `docs/design/figuren-stil.md` (Proportionen, Farbwelt, Texturdichte, Alterung, Budgets, Namensregeln, was wir bewusst nicht machen; zwei Übersichtsbilder) liegt dem Projektinhaber vor; große Blätter in `C:\GotharData\review\stil\`. Abhaken nach seiner Freigabe.

**DoD:** 5 unterscheidbare NPCs in der Engine, alle auf dem Referenz-Rig.
Stand: die 5 Test-NPCs liegen als `.glb` vor (strikt gültig); „in der Engine“ hängt an M6 (Skin/Clips kochen).

## F4 – Gothic-spezifische Animationen  (benötigt F2; für M8–M11)
- [ ] Mocap-Workflow testen (2–3 Dienste), Entscheidung dokumentieren
- [ ] Mob-Interaktionen, Item-Benutzung, Ambient-Routinen, Dialog-Gesten
  - Stand: Platzhalter für M8 (Mobs, Items) und M9 (Routinen und Reaktionen nach engines Liste: Set `amb` mit Sitzen, Wache, Anlehnen, Reden, Zuhören, Schlafen, Lagerfeuer, Fegen, Krug, Schwerttraining, Holzhacken, Ernten, Gießen, Reparieren; Bank als Mob; `none/s_idle_look`, `t_idle_scratch`, `t_warn`, `t_point`, `t_surprised`, `t_search`); Dialog-Gesten für M10 als additive Clips `dlg/a_*` gegen die Referenz `dlg/a_neutral` (Vertrag §3.2, Validator `anim.additive`)
- [ ] Nahkampf (Talent über die Abspielrate), Fernkampf, Magie, Treffer/Tod/Bewusstlos
  - Stand: Platzhalter für M11 Teil A (2026-10-05, Vertrag mit engine): `none/t_hit_light`, `t_die_front/back`, `t_ko`, `s_ko`, `t_ko_getup`; `fist/t_attack_combo1..2`, `t_parry`; `1h/t_attack_combo1..4`, `t_attack_l/r`, `t_parry`, `t_dodge_back` – Events `hit_*` und `combo_*`, jeder Angriff aus und in die Kampfhaltung (Rezept `framed`); Teil B: `2h` (dieselben Namen, linke Hand per `two_hands` am Griff), `bow/s_aim`, `t_shoot` (Event `release`), `t_reload`, `cbow` ebenso – damit alle 31 Kampf-Clips von engines Liste als Platzhalter
  - Stand Magie (M12, 2026-10-06): `mag/t_draw`, `t_sheath`, `t_invest`, `s_invest`, `t_cast_projectile/target/self/area/summon` (Event `cast`), `s_cast_loop`, `t_cast_loop_end`, `t_cast_fail`; `none/t_hit_magic`, `s_burn` – alle aus und in `mag/s_idle`
- [ ] Alle **Prio-B**-Animationen fertig

**DoD:** Vertical Slice (Meilenstein B) ohne Platzhalter-Animationen.

## F5 – Monster  (benötigt F1; für M9/M11)
- [x] CC0-Platzhalter für 3 Arten (Rudeltier, Keiler, Laufvogel) – Monster-Vertrag mit engine (characters-pipeline.md §7.1); `wolf` (22 Knochen, 0,85 m), `keiler` (25 Knochen, 0,95 m), `laufvogel` (12 Knochen, 1,6 m), je 12/12 Clips des Mindest-Sets (Quaternius CC0 + Keyframe-Platzhalter, `gothar-chargen monster`/`build-set`)
- [x] Eigene Gangarten mit realistischem Tempo (2026-10-05, mit engine abgestimmt): Rezept `gait` (Füße stehen, Beine per IK, Körper federt über das Standbein); Wolf Gehen 1,2 / Traben 3,0 (`wolf/s_trot`, neu) / Galopp 6,0 m/s, Keiler 1,0 / 5,0, Laufvogel 1,3 / 6,5 – schneller als der rennende Held; Blend-Punkte und KI-Tempo setzt engine
- [x] Eigene Rigs + Mindest-Sets je Art, Design-Doku der Arten
  - Stand (2026-10-07): vier eigene Arten vom Projektinhaber ausgewählt (`docs/design/monsters.md`: Schinder, Quaderbuckel, Glemsmahr, Bergleu); Werkzeug `gothar-chargen creature` (Körperbeschreibung → Mesh, Rig, Fell-Textur und Normal-Map, characters-pipeline.md §7.4); **Schinder** fertig: 26 Knochen, lod0 7834 Dreiecke, 15 Clips (Tempo 1,3 / 6,5 / Schleichen 0,7 m/s, Sonderclips `s_sneak`, `t_call`, `s_cower`; mit engine abgestimmt); **Quaderbuckel** fertig: 29 Knochen (Stirnschild `brow_shield` unter `chest`), 60 starre Sandstein-Platten, lod0 7956 Dreiecke, 17 Clips (Tempo 0,8 / 3,5 / Anrennen 4,5 m/s am Ort, Rammstoß, Warnen, Kopf einziehen); **Glemsmahr** fertig: neues Rig mit 37 Knochen (mit engine), sehnig, emissive Augen, lod0 7636 Dreiecke, 19 Clips (Tempo 1,4 / 6,5 / Schleichen 0,9 m/s, Aufrichten, Sprung und Satz zurück am Ort, Zurückweichen vor Licht); **Bergleu** (Boss) fertig: Katzen-Rig mit 41 Knochen (mit engine), Mähne aus Strähnen, Säbelzähne, Steinhörner, lod0 11096 Dreiecke, 19 Clips (Tempo 1,5 / 8,0 / Anpirschen 1,0 m/s, Sprung am Ort, Schwanzschlag, Brüllen, Raserei) – damit alle vier eigenen Arten; **Wolf** eigen (2026-10-08) statt Quaternius-Platzhalter: gleiche Knochen-, Clipnamen, Events und Tempo, dazu `jaw` und `ear_l/r` (mit engine), lod0 7376 Dreiecke, 13 eigene Clips; Verwandlungs-Übergang für M12 (mit engine): `none/t_transform_out/in`, `wolf/t_transform_in/out` (je 0,8 s, Event `swap` bei Bild 12 der _out-Clips); **Keiler** eigen (2026-10-08) statt Quaternius-Platzhalter: gleiche Knochen-, Clipnamen, Events und Tempo, dazu `jaw` und `ear_l/r` (mit engine), Borstenkamm, gebogene Hauer, kurze kräftige Läufe, lod0 7590 Dreiecke, 12 eigene Clips; **Laufvogel** eigen (2026-10-08): eigene Silhouette (schieferblaues Gefieder, rostroter Halskragen, Federkamm, Hakenschnabel), dazu `neck_02`, `jaw`, `wing_l/r` (mit engine), lod0 6824 Dreiecke, 12 eigene Clips – damit kein Quaternius-Tier mehr im Spiel
- [x] Validator-Regeln für Monster-Rigs – Rig je Art nach Pfad, Pflichtknochen, Ausrichtungs-Hinweise, Größe relativ zur Art, Clip-Modus = Art, `anim.root_motion` (s_walk/s_run vorwärts, t_turn_l/r drehen root); Werkzeuge `gothar-chargen monster`, Rezepte `advance`/`keyposes`

**DoD:** Drei Monsterarten mit vollständigem Mindest-Set in der Engine.

## F6 – Waffen & Handgegenstände  (benötigt F1; für M8–M11)
- [x] Erste Stücke, realistisch und texturiert: `it_sword_old` (rostig, schartig), `it_sword_crude` (frisch geschmiedet), `it_club`, `it_bow_short`, `it_apple`, `it_bread`, `it_potion_heal_small`, `it_lockpick`, `it_key` (ein Modell für alle Schlüssel) – `gothar-chargen build-items` (eigene Geometrie per Code, ambientCG-Texturen), Socket-Ausrichtung mit engine abgestimmt (characters-pipeline.md §3.1, §6.3), Validator `item.*` in CI, Prüfbilder jedes Stücks am Socket einer Testfigur
- [ ] Weitere Waffen und Gegenstände nach Bedarf von M10/M11 (Zweihänder, Armbrust, Pfeile, Fackel, Spruchrollen …)
  - Stand: `it_arrow`, `it_bolt` (Ursprung Schaftmitte, mit engine abgestimmt), `it_crossbow` (2026-10-05, M11), `it_sword_2h` (Zweihänder, 2026-10-06); für M12 die Runen `it_rune_firebolt`, `_heal`, `_sleep`, `_transform_wolf`, `_summon_wolf`, `it_scroll`, `it_potion_mana_small`; Fackel `it_torch` mit Knoten `socket_flame` und Clips im eigenen Set `torch` (`none/t_torch_light`, `a_torch_hold`, `t_torch_drop`; wie Gothic 1 auch mit Fäusten bzw. Einhandwaffe, 2026-10-08); Gangarten-Varianten für NPC-Vielfalt (Prio C, 2026-10-08): `anims/human/gait.glb` mit `none/s_idle|s_walk[|s_run]_<v>` für woman, military, old, relaxed, Feld `[anim] variant` im Figuren-Manifest, allen Frauen, Wachen, Alten und einigen Männern zugeordnet

**DoD:** Die Gegenstände liegen in der Welt (M8) und sitzen in der Hand bzw. am Gürtel und Rücken (M10/M11).
