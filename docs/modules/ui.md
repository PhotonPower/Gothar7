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

## Inventar-Ansichten (Vorschlag figuren, M14-Vorarbeit)
Festgelegt vom Projektinhaber: Inventar wie Gothic, **jede Zelle zeigt das 3D-Modell**, zentriert auf seine
Bounding-Box, Standardansicht je Kategorie, keine PNG-Symbole. figuren liefert die Daten, engine baut die
Darstellung. Status: **Vorschlag** – Vertrag figuren → engine, bis engine zustimmt.

- **Datei:** `assets/source/data/inventory_views.toml` (gepflegt von figuren), Version 1:
  ```toml
  version = 1
  [camera]
  fov = 30.0                          # Grad, quadratische Zelle, perspektivisch
  [default]                           # alle vier Felder Pflicht
  yaw = 25.0
  pitch = 15.0
  roll = 0.0
  fill = 0.85
  [category.melee_1h]                 # je Item-Kategorie (kItemCategories), Felder optional
  yaw = 90.0
  roll = -45.0
  [model."items/it_mug.glb"]          # je Modell = Lua-Feld `mesh`, Felder optional
  yaw = -70.0
  [model."items/it_lute.glb"]
  category = "misc"                   # optional: Kategorie der Ansicht (Modell ohne Lua-Item)
  ```
- **Auflösung je Feld:** `[default]` ← `[category.<c>]` ← `[model."<mesh>"]`. `<c>` ist das `category` des
  Modell-Eintrags, sonst die Kategorie des Lua-Items. Modelle teilen sich Ansichten über ihren Pfad (alle Schlüssel
  zeigen `items/it_key.glb`). Ohne `mesh` (Platzhalter) gilt die Kategorie-Ansicht.
- **Felder:** `yaw` (Grad um die eigene +Y-Achse = Griffachse; 90 dreht die +X-Seite, also die Klingenfläche, zum
  Betrachter und +Z, die Schneide, nach rechts), `pitch` (Grad um die Bild-X-Achse; positiv kippt die Oberseite zum
  Betrachter = Sicht von oben), `roll` (Grad in der Bildebene, positiv gegen den Uhrzeigersinn; −45 = diagonal, Spitze
  oben rechts), `fill` (0,1–1: Anteil der Zelle, den die Vorderseite der gedrehten Bounding-Box in ihrer größeren
  Richtung einnimmt).
- **Zellen-Geometrie** (Achsen wie im glTF: +Y oben, +Z vorn; Bildraum: +X rechts, +Y oben, Kamera auf +Z blickt
  nach −Z):
  1. Drehung `R = Rz(roll) · Rx(pitch) · Ry(yaw)` (Spaltenvektoren),
  2. die 8 Ecken der lokalen Bounding-Box (LOD 0) mit `R` drehen; deren achsparallele Box hat Mitte `c` und halbe
     Größen `h` – das Modell wird um `−c` verschoben (zentriert),
  3. Kameraabstand `d = h.z + max(h.x, h.y) / (fill · tan(fov / 2))`.
  Referenz-Implementierung: `tools/chargen/src/gothar_chargen/inventory.py` (`cell()`); Tests in
  `tools/chargen/tests/test_inventory.py` prüfen, dass jedes Modell unter `assets/source/items` eine Ansicht hat.
- **Offen für engine:** Licht der Zelle (Prüfblatt: Sonne von links oben vorn, helles Umgebungslicht), Drehen des
  gewählten Items (Gothic dreht es langsam; Vorschlag: um die Bild-Y-Achse, Ansicht als Startlage), Größe der Zelle.
- **Prüfblatt:** `gothar-chargen inventory-sheet --out <ordner>` rendert jede Zelle mit dieser Regel (Blender,
  Cycles auf der CPU) und setzt ein Raster mit Beschriftung (Modell, Kategorie, Quelle der Ansicht) zusammen.
