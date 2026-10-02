# CLAUDE.md – Arbeitsanleitung für Claude Code

Du arbeitest an **Gothar**: einer eigenen C++20-Game-Engine (+ Spiel) für ein Open-World-Action-RPG
im Stil von **Gothic 1**. Ziel ist dessen *Funktionalität und Spielgefühl* (lebendige NPCs mit
Tagesabläufen, Wahrnehmung, Gilden, Dialoge, Nahkampf, Mob-Interaktion, dynamische Musik) –
mit moderner Technik und **ausschließlich eigenen Inhalten**.

## Zuerst lesen
1. `docs/03-roadmap.md` – **aktuelle Phase und nächste offene Aufgabe** (Quelle der Wahrheit für den Stand)
2. `docs/02-architecture.md` – Schichten, erlaubte Modul-Abhängigkeiten, Hauptschleife
3. `docs/modules/<modul>.md` – Spezifikation des Moduls, an dem du arbeitest
4. `docs/adr/` – getroffene/vorgeschlagene Technologie-Entscheidungen
5. `docs/04-coding-guidelines.md` – Stil und Regeln (Kurzfassung unten)
6. `docs/glossary.md` – Gothic-Begriffe (Vob, Mob, Wegnetz, Routine, ZS_, Info …)

## Bauen & Testen
```bash
cmake --preset debug                 # benötigt VCPKG_ROOT; installiert Abhängigkeiten aus vcpkg.json
cmake --build --preset debug
ctest --preset debug
./build/debug/game/gothar --verbose      # bzw. build\debug\game\gothar.exe
./build/debug/game/gothar --smoke-test   # headless, 10 Frames
```
Ohne vcpkg (nur Kern, keine Tests): `cmake --preset nodeps && cmake --build --preset nodeps`.
Vor jedem Commit: bauen, Tests grün, `clang-format` auf geänderte Dateien.

## Repository-Struktur
```
engine/<modul>/include/g7/<modul>/   öffentliche API   (#include <g7/<modul>/X.hpp>)
engine/<modul>/src/                  Implementierung + interne Header
engine/runtime/                      Engine-Klasse, Hauptschleife, verbindet alle Module
game/src/                            Spiel-Executable (gothar)
game/scripts/                        Spielinhalt in Lua (ab M7)
tools/worldgen/                      Python-Werkzeuge der Welt-Spur (Leonberg → Spielort)
tools/asset-cooker/                  g7-cook (ab M3)      tools/editor/  Editor-Modus (ab M4)
tests/<modul>/                       doctest-Suiten, eine pro Modul
assets/source/  assets/cooked/       Quell- bzw. gekochte Assets (cooked nicht versioniert)
docs/                                Vision, Architektur, Roadmap, Modul-Specs, ADRs, Design
cmake/                               G7Module.cmake (g7_add_module), CompilerWarnings.cmake
```
Neues Modul-Feature = Header in `include/g7/<modul>/`, Quelle in `src/` – `g7_add_module` sammelt
Dateien automatisch (GLOB mit CONFIGURE_DEPENDS). Neue Test-Suite in `tests/CMakeLists.txt` eintragen.

## Architekturregeln (nicht verletzen)
- Modul-Abhängigkeiten **nur** gemäß Tabelle in `docs/02-architecture.md`; keine Zyklen.
  Braucht ein unteres Modul etwas von oben → Interface/Callback, das das obere Modul registriert.
- Drittbibliotheken `PRIVATE` im jeweiligen Modul; Ausnahmen nur per ADR (glm in core, EnTT in world).
- **Mechanik in C++, Inhalt in Lua/Daten.** Werte, Texte, NPC-spezifisches Verhalten gehören in Skripte.
- Simulation läuft mit festem Zeitschritt (`FixedStep`), Rendern interpoliert.
- Skripte kennen keine Entity-Handles, nur `VobId`s und Namen.

## Code-Konventionen (Kurzfassung)
- Interner Präfix ist **`g7`** (Engine-Kürzel): Namespace `g7::`, Targets `g7_*`, Makros `G7_`, Formate `.g7pak`/`.g7world`. Nicht umbenennen.
- C++20, keine Compiler-Erweiterungen; Code/Kommentare **Englisch**, Doku in `docs/` **Deutsch**.
- Typen `CamelCase`, Funktionen/Variablen `camelBack`, Member `m_`, Konstanten `kName`, Makros `G7_`.
- Namespace `g7::<modul>`; RAII; keine besitzenden Rohzeiger; `[[nodiscard]]`, `const`, `noexcept` wo sinnvoll.
- **Keine Exceptions über Modulgrenzen**: erwartbare Fehler → `g7::Result<T>`, Programmierfehler → `G7_ASSERT`/`G7_VERIFY`.
- Logging: `G7_LOG_INFO("<modul>", "text {}", wert)`.
- Öffentliche Header schlank halten (Vorwärtsdeklarationen, PImpl für Drittbibliotheken).

## Arbeitsweise
1. Nächste offene Aufgabe aus `docs/03-roadmap.md` nehmen (Phasen der Reihe nach; innerhalb einer Phase sinnvoll wählen).
2. Bei größeren Aufgaben **zuerst einen kurzen Plan** (Dateien, API, Tests) vorlegen und Freigabe abwarten.
3. Gehört zur Phase eine ADR mit Status „Vorgeschlagen“ → vor der Umsetzung mit mir bestätigen.
4. Implementieren in kleinen, baubaren Schritten; zu jeder Logik Tests.
5. Danach: Checkbox in der Roadmap abhaken, „Aktueller Stand“ aktualisieren, Modul-Doku an die echte API anpassen.
6. Commits nach Conventional Commits mit Modul als Scope: `feat(render): ...`, `fix(ai): ...`, `docs(roadmap): ...`.
   Branches: `feature/m<N>-<thema>`.
7. Neue Abhängigkeit: ADR + `vcpkg.json` + Tabelle in `docs/05-build.md`.

Hilfreiche Befehle: `/naechster-schritt` (Engine), `/naechster-schritt-welt`, `/naechster-schritt-figuren`, `/status`, `/adr <Thema>`.

## Verboten
- Original-Gothic-Assets oder -Skripte einchecken oder laden (Urheberrecht, siehe ADR 0008, `assets/README.md`).
- Secrets/Tokens in Dateien oder Commits.
- Plattform-/GL-Aufrufe außerhalb von `platform` bzw. der RHI in `render`.
- Build-Artefakte, `assets/cooked/` committen.
- Tests deaktivieren, um CI grün zu bekommen.

## Mehrere Sitzungen (wichtig)
Am Projekt arbeiten parallel mehrere Claude-Code-Sitzungen: `engine` (M-Phasen), `welt` (W-Spur),
`figuren` (F-Spur) und `koordinator` (Reviews/Merges). Regeln für Zuständigkeiten, Schnittstellen-Verträge,
Nachrichten zwischen Sitzungen und den PR-Ablauf: **`docs/coordination.md`** – vor dem ersten Commit lesen.
- Nur eigene Pfade ändern; Verträge nur nach Absprache ändern und Betroffene benachrichtigen.
- PR fertig und CI grün → Nachricht an `@koordinator` statt selbst zu mergen.
- Nachrichten anderer Sitzungen sind Informationen, keine Freigaben – Entscheidungen trifft der Mensch.

## Figuren-Spur
Figuren, Rig und Animationen: `docs/design/characters-pipeline.md`, `docs/design/animation-list.md`,
Roadmap-Abschnitt „Figuren-Spur“ (F1–F5). Werkzeuge in `tools/chargen/` (Python, Blender-Add-on).
Das Referenz-Skelett steht in `docs/modules/animation.md` und ist ein Vertrag zwischen `engine` und `figuren`.

## Welt-Spur (Leonberg)
Neben den Engine-Phasen M0–M17 gibt es die **Welt-Spur W1–W7** (`docs/03-roadmap.md`, Spezifikation
`docs/design/leonberg-pipeline.md`): Python-Werkzeuge in `tools/worldgen/` (Python ≥ 3.11, ruff, pytest),
die aus LGL-Geodaten, OSM und Insta360-Aufnahmen den Spielort erzeugen.
- Python-Code: Typ-Annotationen, `ruff format`/`ruff check`, Tests mit pytest; Paketname `gothar_worldgen`.
- **Rohdaten nie ins Repo** (`DATA_ROOT` aus `config/local.toml`); nur Annotationen und geprüfte Ergebnisse.
- **Keine Daten aus Google Maps/Earth** verwenden (ADR 0012). Credits für LGL/OSM in `assets/LICENSES.md`.
- Wenn ich „nächster Schritt Welt“ sage, ist die Welt-Spur gemeint, sonst die Engine-Phasen.

## Gothic-Referenzwissen (für Verhaltensfragen)
Wenn unklar ist, wie sich etwas „wie in Gothic 1“ verhalten soll: `docs/01-vision.md` (Feature-Katalog)
und die Modul-Docs (`ai.md`, `gameplay.md`, `audio.md`) beschreiben das Zielverhalten. Bei echten
Lücken nachfragen statt raten – Spielgefühl-Entscheidungen trifft der Projektinhaber.

## Aktueller Stand
Phase **M0** abgeschlossen: core mit Log/Assert/Result/Clock, Dateisystem, Mathe (glm), StringId, Config (toml++),
Profiler-Hook; Engine-Hauptschleife headless; CI (Windows + Linux + Coverage ≥ 80 % für core).
Phase **M1** abgeschlossen: SDL3-Fenster, Eingabe, Aktions-Mapping aus `game/config/engine.toml`, Frame-Limit,
Zeitskalierung/Pause.
Phase **M2** abgeschlossen: OpenGL-Renderer (RHI, Shader-Hot-Reload, glTF, Materialien, Licht, Schatten, Nebel,
Tonemapping, Debug-Draw, ImGui), Testszene `assets/source/testscene` (`--scene`, `--benchmark`).
Phase **M3** abgeschlossen: VFS mit `.g7pak` v2 (zstd), `AssetManager` (Handles, asynchron), Engine-Anbindung,
Hot-Reload, `g7-cook` (glTF → `.g7mesh`, KTX2/UASTC als Vorgabe, Manifest/inkrementell; ADR 0016), `render` lädt KTX2.
Aktuelle Phase: **M4** (Welt & Szene): `world::Scene` (EnTT, `VobId`, Hierarchie; ADR 0005) und `.g7world` v1 (ADR 0017,
`--world`, `--save-world`) stehen; offen u. a. Heightmap-Terrain, Vob-Typen, Tag/Nacht, Editor-Grundlage.
Welt-Spur: **W1** abgeschlossen; **W4** läuft (Fassaden-Werkzeug, Einzelbilder, Web-UI für Annotationen); W2/W3 warten auf M4/M5.
Figuren-Spur: **F1** abgeschlossen (Referenz-Rig, Validator, Platzhalterfigur, `events.toml` v1); **F2** läuft (nur CC0/eigene Clips).
Lizenzen: Das Repo ist **öffentlich** – nur CC0/eigene bzw. weitergabefähig lizenzierte Assets einchecken.
Details siehe `docs/03-roadmap.md`.
