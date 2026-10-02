# 03 – Roadmap

Jede Phase hat Aufgaben (Checkboxen) und eine **Definition of Done (DoD)**. Eine Phase ist
abgeschlossen, wenn alle Aufgaben erledigt sind, die DoD erfüllt ist, CI grün ist und die
Modul-Doku den tatsächlichen Stand beschreibt. Phasen bauen aufeinander auf; innerhalb einer
Phase ist die Reihenfolge der Aufgaben eine Empfehlung.

**Aktueller Stand:** Phase **M4** (Welt & Szene) läuft – ADR 0005 (EnTT, `VobId`-Vertrag, API-Grenze) akzeptiert (#42); als Erstes EnTT-Registry, `VobId` und Transform-Hierarchie. Welt-Spur: **W4** Schritt 1 (Fassaden-Werkzeug mit synthetischen Bildern) läuft, Aufnahmetour folgt; Annotations-Oberfläche als Web-UI (Entscheidung Projektinhaber). Figuren-Spur: **F1** (Referenz-Rig) in Arbeit – Voraussetzung für M6. Phase **M3** (Asset-System & Pipeline) abgeschlossen – VFS mit `.g7pak` v2 (zstd), `AssetManager` (Handles, Cache, asynchrones Laden), Engine-Anbindung (Mounts aus `engine.toml`), Hot-Reload (Texturen/Modelle, Shader), `g7-cook` (glTF → `.g7mesh`, Bilder → KTX2/UASTC als Vorgabe, Packen, Manifest mit inkrementellem Kochen; ADR 0016), `render` lädt KTX2 (BC7/BC5) und bevorzugt gekochte Meshes; DoD Ende-zu-Ende geprüft (Spiel nur aus `data.g7pak`, Textur-Hot-Reload; #34, #37, #40). Phase **M2** (Renderer-Grundlagen) abgeschlossen – OpenGL-Kontext (4.5+), glad, Debug-Output, RHI (Buffer, Texture, Sampler, Shader, Pipeline, Framebuffer), Shader-System (Includes, Hot-Reload), Kamera (Reverse-Z, Frustum, Debug-Flugkamera), statische glTF-Meshes (fastgltf, `--view-mesh`), Texturen (PNG/JPEG, Mipmaps, Anisotropie, sRGB), Materialien (Normal-Map, Emissive, Alpha-Test/Blend, beidseitig), Licht (Sonne, Hemisphären-Ambient, Punktlichter), Sonnenschatten (CSM), HDR mit Tonemapping (ACES), Distanznebel, Debug-Draw (F2) und das ImGui-Debugfenster (F1), Testszenen (`--scene`, Frustum-Culling, `--benchmark`, `--screenshot`) stehen; DoD-Szene mit 608 FPS (RTX 3080) bzw. 214 FPS (Intel UHD). M0 und M1 abgeschlossen (M1-Abnahme am echten Fenster: Aktionen im Log, Pause, sauberes Schließen).

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
- [x] Weltformat `.g7world` (Text/JSON für Versionierbarkeit, binäre gekochte Variante) – Text v1 (ADR 0017, `--world`, `--save-world`, Testwelt `testworld/camp.g7world`); Binärvariante bei Bedarf
- [ ] Statisches Welt-Mesh (Gelände + Architektur) mit Kollisionsgeometrie
- [ ] **Heightmap-Terrain** (Kacheln, LOD, Splatmap mit 4–8 Schichten, Löcher) – Grundlage für W2 (Leonberg-Gelände)
- [ ] Vob-Typen: Mesh, Licht, Sound-Emitter, Trigger, Startpunkt, Mob (Platzhalter)
- [ ] Spielzeit & **Tag/Nacht-Zyklus**: Sonnenstand, Himmelsfarben (Verlauf je Uhrzeit), Sterne, Mond
- [ ] Sichtbarkeit: Frustum-Culling, Distanz-Culling/LOD für Vobs; Innenräume über Portale/Zonen (später)
- [ ] Mehrere Welten + Weltwechsel (Levelwechsel-Trigger)
- [ ] **Editor-Grundlage**: Editor-Modus, Vobs auswählen/verschieben/drehen (Gizmos, ImGuizmo), Welt speichern

**DoD:** Eine Testwelt mit Gelände, Lager-Hütten und Lagerfeuer wird geladen, Tag/Nacht läuft
sichtbar, im Editor lassen sich Vobs platzieren und speichern.

## M5 – Physik & Charaktersteuerung
- [ ] Jolt Physics integrieren (ADR 0004), Welt-Kollision aus statischem Mesh
- [ ] Raycasts/Shapecasts-API (Fokus, Kamera, KI-Sicht)
- [ ] Charakter-Controller: gehen, rennen, schleichen, Treppen/Steigungen, rutschen an steilen Hängen
- [ ] Springen, **Kanten hochziehen** (Kantenerkennung per Shapecast), Fallschaden
- [ ] Schwimmen/Tauchen (Wasservolumen, Luftvorrat)
- [ ] Third-Person-Kamera im Gothic-Stil: Verfolgung mit Trägheit, Kollision, Modi (Normal, Kampf, Dialog, Schwimmen)
- [ ] Trigger-Volumen (Betreten/Verlassen-Events)

**DoD:** Eine Kapsel-Figur bewegt sich mit Gothic-artiger Steuerung durch die Testwelt, klettert,
schwimmt; Kamera clippt nicht durch Wände.

## M6 – Animation  → Meilenstein A
_Benötigt F1 (Referenz-Rig, Platzhalterfigur) und für den Meilenstein die Prio-A-Animationen aus F2._
- [ ] Skelett + Skinning (GPU), glTF-Skins
- [ ] Clips, Sampling, Blending (Crossfade), additive Layer (Oberkörper getrennt)
- [ ] Animations-Zustandsautomat (datengetrieben), Übergänge mit Bedingungen
- [ ] Animations-Events (Fußschritte, Treffer-Fenster, Item-Wechsel Hand/Gürtel)
- [ ] Root Motion für Interaktionen/Kampf, In-Place für Fortbewegung
- [ ] Attachments: Items an Knochen (Hand, Rücken, Gürtel), Rüstungs-/Kopfwechsel (Mesh-Tausch)
- [ ] Morph-Targets für Gesichter (Lippen-Synchronisation grob, Blinzeln)
- [ ] Look-At (Kopf dreht zu Gesprächspartner)

**DoD / Meilenstein A:** Animierter Held läuft, rennt, springt, klettert, schwimmt durch die
Testwelt bei Tag und Nacht; Debug-UI zeigt Animationszustände.

## M7 – Scripting
- [ ] Lua 5.4 + sol2 (ADR 0006), Skript-VM pro Spielsitzung, Sandbox (kein `io`/`os`)
- [ ] Modul-/Ordnerstruktur in `game/scripts`, Lade-Reihenfolge
- [ ] Instanz-System: `Item{...}`, `Npc{...}`, `Info{...}`, `Quest{...}` als deklarative Tabellen
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
- [ ] Auswahl-Menüs (Choices) innerhalb einer Info
- [ ] Handel-Bildschirm und Lernen über Dialog
- [ ] Tagebuch: Aufträge (laufend/erfolgreich/gescheitert), Einträge, Notizen
- [ ] Kapitelwechsel-Mechanik

**DoD / Meilenstein B (Vertical Slice):** Ein kleines Lager mit 5–10 NPCs mit Routinen, 3 Quests
(Botengang, Beschaffung, Konflikt), Handel, ein Lehrer, Truhen, Tag/Nacht – durchspielbar.

## M11 – Kampf
- [ ] Waffenmodi (Faust, Einhand, Zweihand, Bogen, Armbrust), Ziehen/Wegstecken
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
- [ ] Post-Processing: Bloom, Farbkorrektur je Tageszeit/Zone, SSAO optional
- [ ] Vegetation: Instancing, Wind-Animation, Grasfelder
- [ ] Job-System, Multithreading für Animation/KI/Culling
- [ ] Welt-Streaming in Zellen, Ladezeiten-Optimierung
- [ ] Tracy-Profiling, Performance-Budget pro System
- [ ] Packaging (Installer/ZIP), Crash-Reporting (Minidumps), Versions-/Build-Info
- [ ] Optional: Vulkan-Backend hinter der RHI (ADR)

**DoD / Meilenstein D:** Release-Build läuft stabil ≥ 60 FPS in der größten Welt auf Zielhardware.

---

# Welt-Spur: Leonberg → Spielort

Spezifikation: `docs/design/leonberg-pipeline.md`. Werkzeuge in `tools/worldgen/` (Python ≥ 3.11),
Rohdaten außerhalb des Repos (`DATA_ROOT`).

**Aktueller Stand Welt-Spur:** W1 abgeschlossen (PR #24): `gothar-worldgen` erzeugt aus LGL- und OSM-Daten Terrain, `buildings.json` (5392 Gebäude), `streets.json` und `features.json` für Leonberg (Ursprung Marktbrunnen). W2/W3 warten auf das M4-Terrain. **W4 läuft:** Die Kerne des Fassaden-Werkzeugs (`facade/`: 360°-Projektion, Fassaden-Entzerrung aus `buildings.json`, GPS-Posen, Override-Schema, `facade preview`) stehen und sind mit synthetischen Bildern getestet. Dazu kommen die Einzelbild-Extraktion per ffmpeg und der automatische Zeitabgleich von Video und GPS (`facade frames`). Die Annotations-Web-UI (`facade ui`: Karte, Fassadeneditor, Speichern als Override-JSON) steht; die Auswahllisten sind ein Entwurf zur Festlegung durch den Projektinhaber. Als Nächstes: Aufnahmetour und Test der Oberfläche durch den Projektinhaber, Prüfung mit echten Daten, Schritt 3b (Pose-Korrektur, Bildvergleich).

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
- [ ] Heightmap-Import in das Terrain-System, Splatmap-Grundbelegung aus Straßen/Nutzung
- [ ] Testwelt `leonberg_terrain.g7world` mit Tag/Nacht

**DoD:** Das Leonberger Gelände ist in der Engine sichtbar und (ab M5) begehbar.

## W3 – Klötzchen-Leonberg & Maßstabstest  (benötigt W1, W2; sinnvoll ab M5)
- [ ] Blender-Add-on „Gothar Buildings“ Grundgerüst: `buildings.json` lesen, Baukörper + Dach als Massen, `.glb`-Export
- [ ] Welt-Assembler v1: Terrain + Gebäude → `.g7world`
- [ ] Begehung mit Spielfigur/Kamera; Maßstabsfaktoren und Gassenverbreiterung festlegen (Ergebnis in `leonberg.toml` + Design-Doku)

**DoD:** Graue Altstadt begehbar; Maßstabsentscheidung dokumentiert.

## W4 – Aufnahmen & Fassaden-Werkzeug  (keine Engine-Abhängigkeit)
- [ ] Aufnahmetour(en) nach Leitfaden (Abschnitt 6 der Design-Doku)
- [ ] Bild-Extraktion aus Insta360-Export, GPS-Zuordnung, optional SfM-Verfeinerung
  - Stand: Einzelbilder per ffmpeg, Zeitabgleich mit dem GPS-Track und Posen je Bild (`facade frames`, `frames.json`), synthetisch geprüft; SfM offen
- [ ] Fassaden-Ausschnitt + Entzerrung pro Gebäude
  - Stand: Entzerrung per Projektion auf die Fassadenebene umgesetzt (`facade/rectify.py`, `facade preview`), synthetisch geprüft; abhaken nach der Prüfung mit echten Aufnahmen
- [ ] Annotations-Oberfläche → Override-JSON pro Gebäude
  - Stand: Override-Schema (`facade/overrides.py`) und Web-UI (`facade ui`, Schritt 3a) umgesetzt; abhaken nach dem Test durch den Projektinhaber
- [ ] Annotation der Häuser am Marktplatz (erste ~20 Gebäude)

**DoD:** Für jedes Haus am Marktplatz gibt es eine entzerrte Fassadenreferenz und eine Annotation.

## W5 – Fachwerk-Generator  (benötigt W3, W4)
- [ ] Modularer Baukasten + Trim-Sheets (Balken, Putz, Stein, Holz, Dach)
- [ ] Regeln: Stockwerke, Auskragung, Fachwerk-Muster-Katalog, Öffnungen, Dachdeckung, Gauben, Schornsteine
- [ ] Overrides aus W4 anwenden; Seeds für Variation; `locked`-Schutz für Handarbeit
- [ ] LOD-Erzeugung, Kollisions-Mesh
- [ ] Stil-Referenzblatt (Farben, Materialien, Alterung) in `docs/design/`

**DoD:** Der Marktplatz ist mittelalterlich und stilistisch geschlossen in der Engine zu sehen.

## W6 – Straßen, Mauer, Ausstattung  (benötigt W5, M4-Editor)
- [ ] Straßen/Plätze aus OSM → Splatmap, Rinnen, Stufen, Stützmauern
- [ ] Stadtmauer mit Toren
- [ ] Requisiten- und Vegetationsverteilung über Regeln/Masken
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

**Aktueller Stand Figuren-Spur:** F1 abgeschlossen (bis auf das Kochen von Skin/Clips, das zu M6 gehört): Referenz-Rig (T-Pose, 60 Knochen,
Geometrie aus dem Quaternius-Rig, CC0), Export-Einstellungen, `tools/chargen` mit Rig-Validator (CI-Job `chargen`), Platzhalterfigur +
3 Test-Clips mit Events; Verträge mit engine abgestimmt. Nächste Phase: F2 (Basis-Animationsset).

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
- [ ] Retargeting-Mappings (Quaternius, Mixamo) und Stapel-Retargeting in Blender
- [ ] Animations-Export: Sets nach Konvention, Timeline-Marker → `events.toml`, Root-Motion/In-Place
- [ ] Abgleich-Werkzeug Animationsliste ↔ vorhandene Clips (Fortschrittsbericht)
- [ ] Alle **Prio-A**-Animationen (mindestens als Platzhalter-Qualität)
- [ ] Prio-B-Fortbewegung je Waffenmodus

**DoD:** Meilenstein A ist mit diesen Animationen erreichbar; Bericht zeigt 0 fehlende Prio-A-Clips.

## F3 – Figuren-Baukasten  (benötigt F1)
- [ ] 2 Grundkörper × 3 Staturen (MPFB2, stilisiert), Köpfe als separate Meshes mit Morph-Targets
- [ ] Haare/Bärte, erste Kleidungs-/Rüstungslinien (Lumpen, leicht, mittel)
- [ ] Baukasten-Werkzeug: Zusammensetzen, Passform-Prüfung, LODs, Farbvarianten
- [ ] Stil-Referenzblatt Figuren (gemeinsam mit W5)

**DoD:** 5 unterscheidbare NPCs in der Engine, alle auf dem Referenz-Rig.

## F4 – Gothic-spezifische Animationen  (benötigt F2; für M8–M11)
- [ ] Mocap-Workflow testen (2–3 Dienste), Entscheidung dokumentieren
- [ ] Mob-Interaktionen, Item-Benutzung, Ambient-Routinen, Dialog-Gesten
- [ ] Nahkampf je Talentstufe, Fernkampf, Magie, Treffer/Tod/Bewusstlos
- [ ] Alle **Prio-B**-Animationen fertig

**DoD:** Vertical Slice (Meilenstein B) ohne Platzhalter-Animationen.

## F5 – Monster  (benötigt F1; für M9/M11)
- [ ] CC0-Platzhalter für 3 Arten (Rudeltier, Keiler, Laufvogel)
- [ ] Eigene Rigs + Mindest-Sets je Art, Design-Doku der Arten
- [ ] Validator-Regeln für Monster-Rigs

**DoD:** Drei Monsterarten mit vollständigem Mindest-Set in der Engine.
