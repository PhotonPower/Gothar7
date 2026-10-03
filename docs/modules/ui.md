# ui

**Zweck:** Alle Bildschirmoberflächen.

## Zwei Welten
1. **Debug-/Editor-UI**: Dear ImGui (+ ImGuizmo ab M4). Zurzeit in allen Builds, standardmäßig aus (F1).
2. **Spiel-UI**: eigenes schlankes Retained-Mode-System (ADR 0009) auf dem Renderer:
   Widgets (Panel, Text, Bild, Liste, Balken, Item-Slot, 3D-Item-Vorschau), Layout (Anker, Stapel),
   9-Slice-Rahmen, MSDF-Schrift, Gamepad-/Tastatur-Navigation, Themes als Daten.

## Debug-UI (umgesetzt, M2) – `DebugUi.hpp`
```cpp
namespace g7::ui {
struct EnginePanel { render::FrameStats frame; Vec3 cameraPosition; u32 width, height; u64 simulationTicks;  // angezeigt
                     f32 fovDegrees, flySpeed; render::Tonemapper tonemapper; f32 exposure, fogStart, fogDensity;
                     bool sun, shadowDebug, debugDraw, paused; f32 timeScale; };                         // bearbeitet
std::optional<render::PixelRect> scissorFromClip(const Vec4& clip, u32 fbWidth, u32 fbHeight);
class DebugUi {
public:
    static Result<DebugUi> create(render::Device*, render::ShaderLibrary*, f32 scale);  // ohne Device: nur CPU (Tests)
    void beginFrame(const platform::Input&, Vec2 size, Vec2 pixels, f32 dt);
    void enginePanel(EnginePanel&);       // Fenster „Gothar“; Änderungen werden zurückgeschrieben
    void endFrame(render::Device*);       // zeichnet in das gebundene Ziel (Fenster)
    bool wantsMouse() const; bool wantsKeyboard() const; bool wantsText() const;
};
}
```
- **Dear ImGui 1.92** (ADR 0015) privat in `ui`, ohne Docking; **eigene Backends**: Eingabe aus
  `platform::Input` (Kanten werden zu ImGui-Events, Texteingabe als UTF-8), Rendering über die RHI
  (`imgui.vert/.frag`, Scissor pro Draw-Befehl, 16-Bit-Indizes mit Basis-Vertex, Textur-Protokoll für den
  dynamischen Font-Atlas). ImGui-Typen verlassen das Modul nicht.
- Skalierbare Standardschrift (`AddFontDefaultVector`), Größen mit `Window::displayScale()` skaliert; keine
  `imgui.ini`.
- Keine Tastatur-Navigation: ImGui beansprucht die Tastatur nur, solange ein Eingabefeld aktiv ist.
- **Engine:** Aktion `debug_ui` (F1) bzw. `[render] debug_ui`; das Fenster „Gothar“ zeigt FPS (Mittel und
  schlechtester Frame der letzten 240 Frames, Verlaufsgrafik), Draw-Calls, Dreiecke, Pipeline-/Texturwechsel,
  Kamera und erlaubt live: FOV, Fluggeschwindigkeit, Tonemapper, Belichtung, Nebel, Sonne, Kaskadenfarben,
  Debug-Draw, Pause, Zeitskalierung; dazu die ImGui-Demo. Liegt die Maus über ImGui, startet kein Mausblick;
  hat ein Eingabefeld den Fokus, bewegt sich die Kamera nicht und Pause/F2 greifen nicht.
- **Editor-Fenster** (`EditorPanel.hpp`, M4): `DebugUi::editorPanel(EditorPanel&)` mit Werten ohne `world`-Typen –
  Vob-Liste, generischer Inspektor aus `EditorField`s, Modell-Liste, Gizmo-Einstellungen, Aktionen; der Editor
  (`tools/editor`) füllt und liest sie.

## Bildschirme
HUD (Leben, Mana, Gegnerleben, Fokusname, Luft), Dialog-Auswahl + Untertitel, Inventar, Handel,
Truhe, Charakter, Tagebuch, Dokument (Briefe/Bücher), Karte, Hauptmenü, Optionen, Laden/Speichern,
Ladebildschirm, Konsole (Debug), Bildschirmmeldungen.

## Lokalisierung
Alle Texte über Schlüssel (`DIA_Ruvin_Hello_11_01`, `UI_INVENTORY_TITLE`) aus Tabellen pro Sprache;
fehlende Übersetzung → Schlüssel sichtbar + Warnung im Log. Zahlen-/Datumsformat je Sprache.
