# 0002 – Mathe-Bibliothek: glm

- **Status:** Akzeptiert (2026-10-02)
- **Datum:** 2026-10-02
- **Phase:** M0

## Kontext
Vektoren, Quaternionen, Matrizen werden in fast jedem Modul benötigt und erscheinen in öffentlichen APIs.

## Optionen
1. **glm** – Standard in OpenGL-Projekten, GLSL-ähnlich, header-only, ausgereift; etwas langsame Debug-Builds.
2. **Eigene Mathe-Bibliothek** – volle Kontrolle, SIMD-optimierbar; viel Arbeit, Fehlerquelle.
3. **DirectXMath / RTM** – sehr schnell; ungewohnte API, weniger GL-nah.

## Entscheidung
glm, über Typ-Aliasse in `g7/core/Math.hpp` (`Vec3 = glm::vec3` …), damit ein späterer Austausch lokal bleibt.
Konfiguration: `GLM_FORCE_DEPTH_ZERO_TO_ONE` nur falls Reverse-Z eingesetzt wird (Entscheidung in M2).

## Konsequenzen
glm ist öffentliche Abhängigkeit von core.

## Umsetzung (M0)
- glm aus vcpkg (`glm`); das Preset `nodeps` lädt glm 1.0.3 per `FetchContent` nach.
- Öffentliche Compile-Definitionen an `g7_core`: `GLM_FORCE_CTOR_INIT` (Vektoren/Matrizen sind mit 0
  bzw. Identität initialisiert), `GLM_FORCE_SILENT_WARNINGS`, `GLM_ENABLE_EXPERIMENTAL`.

## Nachtrag (M2): Reverse-Z
Entschieden: **Reverse-Z** mit 0..1-Tiefenbereich (`glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`, setzt
`render::Device`). Projektionen kommen aus `g7::perspectiveReverseZ` (rechtshändig, Tiefe 1 am Near-, 0 am
Far-Plane), nicht aus `glm::perspective`; daher ist `GLM_FORCE_DEPTH_ZERO_TO_ONE` nicht nötig. Tiefe wird mit 0
gelöscht, Standardvergleich ist `GreaterEqual`. Grund: große Sichtweiten der offenen Welt bei gleichzeitig
nahen Objekten – mit Fließkomma-Tiefe verteilt Reverse-Z die Genauigkeit fast gleichmäßig über die Distanz.
