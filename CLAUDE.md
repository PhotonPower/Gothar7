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

Hilfreiche Befehle: `/naechster-schritt` (nächste Roadmap-Aufgabe planen), `/adr <Thema>` (neue Entscheidung dokumentieren).

## Verboten
- Original-Gothic-Assets oder -Skripte einchecken oder laden (Urheberrecht, siehe ADR 0008, `assets/README.md`).
- Secrets/Tokens in Dateien oder Commits.
- Plattform-/GL-Aufrufe außerhalb von `platform` bzw. der RHI in `render`.
- Build-Artefakte, `assets/cooked/` committen.
- Tests deaktivieren, um CI grün zu bekommen.

## Gothic-Referenzwissen (für Verhaltensfragen)
Wenn unklar ist, wie sich etwas „wie in Gothic 1“ verhalten soll: `docs/01-vision.md` (Feature-Katalog)
und die Modul-Docs (`ai.md`, `gameplay.md`, `audio.md`) beschreiben das Zielverhalten. Bei echten
Lücken nachfragen statt raten – Spielgefühl-Entscheidungen trifft der Projektinhaber.

## Aktueller Stand
Phase **M0** abgeschlossen: core mit Log/Assert/Result/Clock, Dateisystem, Mathe (glm), StringId, Config (toml++),
Profiler-Hook; Engine-Hauptschleife headless; CI (Windows + Linux + Coverage ≥ 80 % für core).
Phase **M1**: alle Aufgaben umgesetzt (SDL3-Fenster, Eingabe, Aktions-Mapping aus `game/config/engine.toml`,
Frame-Limit, Zeitskalierung/Pause); DoD-Abnahme am echten Fenster steht aus. Danach M2 (Renderer).
Details siehe `docs/03-roadmap.md`.
