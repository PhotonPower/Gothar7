# 0015 – Debug- und Editor-UI: Dear ImGui mit eigenen Backends

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M2 (Editor ab M4)

## Kontext
Ab M2 brauchen wir Debug-Overlays (FPS, Frame-Statistik, Render-Einstellungen live ändern), ab M4 den
Editor-Modus (Gegenstück zu Gothics Spacer). Beides ist Entwicklerwerkzeug: Schnelligkeit beim Bauen von
Fenstern zählt mehr als Stil. Die Spiel-UI ist davon getrennt (ADR 0009).
Randbedingungen aus der Architektur: Plattform-Aufrufe (SDL) nur in `platform`, GL-Aufrufe nur in der RHI von
`render`, Drittbibliotheken `PRIVATE` im jeweiligen Modul.

## Optionen
1. **Dear ImGui mit den mitgelieferten Backends** (`imgui_impl_sdl3`, `imgui_impl_opengl3`) – kaum Aufwand;
   verletzt aber die Schichtregeln (SDL- und GL-Aufrufe im UI-Modul) und koppelt ImGui an SDL/GL.
2. **Dear ImGui mit eigenen schlanken Backends** – Eingabe aus `platform::Input`, Zeichnen über die RHI;
   ~400 Zeilen eigener Code, dafür regelkonform und unabhängig von einem späteren RHI-Wechsel.
3. **Eigene Debug-UI** – volle Kontrolle; viel Aufwand für Widgets, Layout, Text, Eingabe.
4. **Nuklear / andere Immediate-Mode-Bibliotheken** – kleiner, aber deutlich weniger verbreitet und
   ausgereift; kein ImGuizmo-Gegenstück für Editor-Gizmos.

## Entscheidung
Option 2: **Dear ImGui** (MIT, vcpkg-Port `imgui`, ohne Docking) im Modul **`ui`**, `PRIVATE` gelinkt, mit
**eigenen Backends**:
- **Eingabe:** `ui::DebugUi::beginFrame` übersetzt den Frame-Zustand von `platform::Input` (Tasten-/Knopfkanten,
  Mausposition, Rad, neue Texteingabe `Input::text()`) in ImGui-Events. Keine Tastatur-Navigation, damit ein
  fokussiertes Fenster der Kamera nicht die Tastatur wegnimmt.
- **Rendering:** über die RHI (`Device::setScissor`, `drawIndexed` mit Basis-Vertex, 16-Bit-Indizes), mit dem
  Textur-Protokoll von ImGui 1.92 (`RendererHasTextures`: Atlas anlegen, aktualisieren, freigeben).
- ImGui-Typen bleiben im Modul; andere Module bekommen Panels als Funktionen mit eigenen Datenstrukturen
  (z. B. `ui::EnginePanel`).

Docking (vcpkg-Feature `docking-experimental`) und ImGuizmo kommen bei Bedarf mit dem Editor (M4).

## Konsequenzen
- Neue Abhängigkeit `imgui` (vcpkg; `nodeps` lädt v1.92.9b per FetchContent).
- RHI erweitert um Scissor-Test und Basis-Vertex; `platform` um Texteingabe (`Window::setTextInput`,
  `Input::text()`) und `Window::displayScale()` für die DPI-Skalierung.
- Debug-UI ist in allen Builds enthalten und standardmäßig aus (F1); ein Schalter, sie aus Release-Builds
  herauszunehmen, kann später folgen.
