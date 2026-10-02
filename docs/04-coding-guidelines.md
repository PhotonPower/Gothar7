# 04 – Coding-Richtlinien

## Sprache & Standard
- **C++20**, keine Compiler-Erweiterungen. Unterstützte Compiler: MSVC 19.38+, GCC 13+, Clang 17+.
- **Code, Bezeichner und Code-Kommentare auf Englisch**, Dokumentation unter `docs/` auf Deutsch.

## Benennung
| Element | Stil | Beispiel |
|---|---|---|
| Namespace | `lower_case` | `g7::render` |
| Typen | `CamelCase` | `MeshRenderer` |
| Funktionen/Methoden | `camelBack` | `loadTexture()` |
| Variablen/Parameter | `camelBack` | `frameCount` |
| Private Member | `m_` + camelBack | `m_registry` |
| Konstanten (`constexpr`) | `k` + CamelCase | `kMaxBones` |
| Makros | `G7_UPPER_CASE` | `G7_ASSERT` |
| Dateien | wie Hauptklasse | `MeshRenderer.hpp/.cpp` |

## Dateien & Includes
- Öffentliche Header: `engine/<modul>/include/g7/<modul>/X.hpp`, eingebunden als `<g7/modul/X.hpp>`.
- Interne Header: `engine/<modul>/src/` – nie aus anderen Modulen einbinden.
- `#pragma once`. Include-Reihenfolge wird von `.clang-format` erzwungen.
- Öffentliche Header so schlank wie möglich: Vorwärtsdeklarationen, PImpl für Drittbibliotheken.

## Regeln
- RAII für alle Ressourcen (GPU-Objekte, Dateien, Sounds).
- Keine besitzenden Rohzeiger, kein `new`/`delete` außerhalb von Allokatoren.
- Keine Exceptions über Modulgrenzen, Fehler über `g7::Result<T>`.
- Keine globalen veränderlichen Zustände außer explizit dokumentierten Subsystem-Singletons.
- `[[nodiscard]]` für Funktionen, deren Rückgabe nicht ignoriert werden darf.
- `const` und `noexcept` wo korrekt.
- Logging über `G7_LOG_*` mit Kanal = Modulname.
- Keine `using namespace` in Headern.
- Kommentare erklären das **Warum**, nicht das Was. Öffentliche API mit `///` dokumentieren.

## Tests
- Jedes Modul hat eine Test-Suite `tests/<modul>/` (doctest), eingetragen in `tests/CMakeLists.txt`.
- Neue Logik → neue Tests. GPU-/Fenster-abhängiger Code wird über Headless-Pfade oder
  Logik-Trennung testbar gemacht.
- KI, Dialog- und Quest-Logik bekommen Szenario-Tests (Welt laden, Zeit simulieren, Zustand prüfen).
- **Testabdeckung:** Der CI-Job `coverage` (Linux, gcovr) misst `engine/core/` und schlägt unter 80 % Zeilenabdeckung fehl.
  Bericht: Job-Übersicht bzw. Artefakt `coverage-core`. Lokal (GCC/Clang): `cmake --preset coverage`, bauen, `ctest --preset coverage`,
  dann `gcovr --root . --object-directory build/coverage --filter engine/core/`. Weitere Module kommen mit eigener Logik hinzu.

## Git
- Branch `main` ist immer baubar. Arbeit in Feature-Branches `feature/<phase>-<thema>`, z. B. `feature/m1-window`.
- Commit-Nachrichten nach **Conventional Commits**, Scope = Modul:
  `feat(render): add shadow map pass`, `fix(ai): routine switch at midnight`, `docs(roadmap): …`.
- Keine Secrets, keine Original-Gothic-Assets, keine Build-Artefakte.

## Formatierung & Analyse
- `clang-format` (Konfiguration im Repo) vor jedem Commit.
- `clang-tidy` mit `.clang-tidy`; neue Warnungen beheben statt unterdrücken.
