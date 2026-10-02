# 03 – Roadmap

Jede Phase hat Aufgaben (Checkboxen) und eine **Definition of Done (DoD)**. Eine Phase ist
abgeschlossen, wenn alle Aufgaben erledigt sind, die DoD erfüllt ist, CI grün ist und die
Modul-Doku den tatsächlichen Stand beschreibt. Phasen bauen aufeinander auf; innerhalb einer
Phase ist die Reihenfolge der Aufgaben eine Empfehlung.

**Aktueller Stand:** Phase **M0** (Fundament) – Skelett und Dateisystem-Helfer stehen; CI-Lauf noch unbestätigt, Mathe/StringId/Config/Profiler offen.

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

---

## M0 – Fundament
- [x] Repository-Struktur, CMake (Module als Bibliotheken), CMakePresets, vcpkg-Manifest
- [x] `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`
- [x] core: Typen, Logging, Assertions, `Result<T>`, Stopwatch, `FixedStep`
- [x] Test-Infrastruktur (doctest), erste Tests
- [x] CI-Workflow (Windows + Linux, Build + Test + Smoke-Test) in `.github/workflows/ci.yml`
- [ ] CI einmal erfolgreich durchgelaufen (ggf. Workflow korrigieren)
- [x] core: Dateisystem-Helfer (Datei lesen/schreiben, Pfade relativ zum Spielverzeichnis)
- [ ] core: Mathe-Bibliothek festlegen (ADR 0002: glm) und Typ-Aliasse `Vec3`, `Quat`, `Mat4`, `Transform`
- [ ] core: String-Hilfen (Hash `StringId` für schnelle Namensvergleiche, case-insensitive Vergleich)
- [ ] core: Konfigurationsdatei laden (TOML, z. B. toml++)
- [ ] core: einfacher Profiler-Hook (Makros, später Tracy)

**DoD:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` läuft auf
Windows und Linux grün; core hat > 80 % Testabdeckung seiner Logik.

## M1 – Plattform & Hauptschleife
- [ ] SDL3 einbinden (vcpkg), Fenster erzeugen, Größenänderung, Vollbild
- [ ] Eingabe: Tastatur, Maus (relativ für Kamera), Gamepad
- [ ] **Aktions-Mapping** (Aktion „Vorwärts“, „Aktion“, „Waffe ziehen“ … → Tasten), aus Konfiguration
- [ ] Hauptschleife in `Engine::run` mit Event-Polling, Fenster-Schließen, VSync/Frame-Limit
- [ ] Headless-Modus beibehalten (Tests/CI)
- [ ] Zeitskalierung/Pause

**DoD:** Fenster öffnet sich, reagiert auf Eingaben (Log-Ausgabe der Aktionen), schließt sauber.

## M2 – Renderer-Grundlagen
- [ ] OpenGL 4.6 Core Context (ADR 0003), Loader (glad), Debug-Callback
- [ ] Dünne RHI-Schicht: Buffer, Texture, Shader/Program, Pipeline-State, Framebuffer
- [ ] Shader-System (GLSL-Dateien, Includes, Hot-Reload)
- [ ] Kamera (Perspektive, Frustum), Free-Fly-Debugkamera
- [ ] Statische Meshes aus glTF laden (vorläufig direkt, ab M3 über asset)
- [ ] Texturen (PNG/KTX2), Mipmaps, anisotrope Filterung
- [ ] Material-Modell: Albedo, Normal, Alpha-Test (Laub!), Emissive – bewusst schlicht/stilisiert
- [ ] Licht: gerichtete Sonne + Ambient, Punktlichter (Fackeln, Lagerfeuer), Forward+ oder Clustered
- [ ] Schatten: Cascaded Shadow Maps für die Sonne
- [ ] Distanznebel, Gamma/Tonemapping
- [ ] Debug-Draw (Linien, Boxen, Kugeln, Text im Raum)
- [ ] Dear ImGui für Debug-Overlays (FPS, Statistiken)

**DoD:** Testszene (Boden, einige Häuser/Bäume als glTF) mit Sonne, Schatten, Fackellicht und Nebel
bei ≥ 60 FPS auf Mittelklasse-Hardware.

## M3 – Asset-System & Pipeline
- [ ] Virtuelles Dateisystem: Mount-Punkte (Ordner, `.g7pak`), Priorität (Mods überschreiben)
- [ ] Asset-Handles (typisiert, referenzgezählt), Cache, asynchrones Laden auf Worker-Threads
- [ ] Hot-Reload für Texturen, Shader, Skripte im Entwicklungsmodus
- [ ] `g7-cook`: glTF → Laufzeit-Mesh/Skelett/Animation, PNG → KTX2 (BC7/BC5), OGG bleibt, Archiv packen
- [ ] Asset-Manifest mit Abhängigkeiten und Hashes (inkrementelles Kochen)

**DoD:** Spiel lädt ausschließlich aus `assets/cooked`, Änderung einer Textur wird ohne Neustart sichtbar.

## M4 – Welt & Szene (+ Editor-Grundlage)
- [ ] EnTT-Registry, Komponenten-Grundsatz, `VobId`, Transform-Hierarchie (ADR 0005)
- [ ] Weltformat `.g7world` (Text/JSON für Versionierbarkeit, binäre gekochte Variante)
- [ ] Statisches Welt-Mesh (Gelände + Architektur) mit Kollisionsgeometrie
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
