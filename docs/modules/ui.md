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
Kontur, Standardansicht je Kategorie, keine PNG-Symbole. figuren liefert die Daten, engine baut die
Darstellung. Status: **Vorschlag** – Vertrag figuren → engine, bis engine zustimmt (M14).

- **Datei:** `assets/source/data/inventory_views.toml` (gepflegt von figuren), Version 2:
  ```toml
  version = 2
  [camera]
  fov = 30.0                          # Grad, quadratische Zelle, perspektivisch
  [default]                           # alle vier Felder Pflicht
  yaw = 25.0
  pitch = 15.0
  roll = 0.0
  fill = 0.75
  [category.melee_1h]                 # je Item-Kategorie (kItemCategories), Felder optional
  yaw = 90.0
  roll = -45.0
  fill = 0.97
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
  oben rechts), `fill` (0,1–1: Anteil der Zelle, den die **projizierte Kontur** des Modells in ihrer größeren Richtung
  einnimmt – exakt, auch perspektivisch).
- **Zellen-Geometrie** (Achsen wie im glTF: +Y oben, +Z vorn; Bildraum: +X rechts, +Y oben, Kamera auf +Z blickt
  nach −Z; `t = tan(fov / 2)`):
  1. Drehung `R = Rz(roll) · Rx(pitch) · Ry(yaw)` (Spaltenvektoren) auf **alle Vertices** `v` von LOD 0,
  2. die gedrehten Vertices spannen die Box `[a, b]` auf; das Modell wird um `−c` verschoben, `c = (a + b) / 2`,
     also `p = R·v − c`,
  3. Kameraabstand `d = max über alle Vertices von (p.z + max(|p.x|, |p.y|) / t)`,
  4. Bildposition jedes Vertex `u = p.xy / ((d − p.z) · t)` (−1…1 = volles Sichtfeld `fov`); deren Box `[m, M]` ist
     die projizierte Kontur. Die Zelle zeigt das **Bildfenster** mit Mitte `w = (m + M) / 2` und halber Größe
     `e = max(M − m) / (2 · fill)` – eine Off-Axis-Projektion: Zellen-Koordinate `(u − w) / e`, in der Projektions-
     matrix also nach der Perspektive `x_ndc' = (x_ndc − w.x) / e`, `y_ndc' = (y_ndc − w.y) / e`.
  Damit ist die Kontur in der Zelle exakt zentriert und füllt genau `fill` ihrer größeren Richtung.
- **Warum so (statt Box-Ecken bzw. orthografisch mit Tiefen-Zuschlag):** Version 1 maß die 8 Ecken der gedrehten
  Bounding-Box; diagonal liegende Waffen mit Parierstange oder Bogen füllten so nur 55–80 % der Zelle. Die Vertices
  sind die echte Kontur (ein Segment vor der Kamera bildet sich auf ein Segment ab, also liegen die Extreme an
  Vertices). Schritt 3 hängt nicht von `fill` ab: Jedes Item wird mit derselben Perspektive gesehen (das äußerste
  Vertex liegt am Rand des vollen `fov`, alle liegen vor der Kamera), und `fill` skaliert nur das Bild in Schritt 4.
  Das ist geschlossen und deterministisch – keine Iteration, kein geschätzter Tiefen-Zuschlag, der je nach Form zu
  knapp (Anschneiden) oder zu großzügig wäre; die perspektivische Asymmetrie (nahe Teile wirken größer) gleicht das
  Fenster `w` exakt aus. Aufwand O(Vertices) einmal je Modell; engine kann `R`, `c`, `d`, `w`, `e` beim Laden oder
  im Cooker vorberechnen.
- Referenz-Implementierung: `tools/chargen/src/gothar_chargen/inventory.py` (`cell()`, `project()`); Tests in
  `tools/chargen/tests/test_inventory.py` (einfache Formen, Zentrierung, Füllgrad, Schwert diagonal ≥ 85 % der
  Zellen-Diagonale, jedes Modell unter `assets/source/items` hat eine Ansicht).
- **Offen für engine:** Licht der Zelle (Prüfblatt: Sonne von links oben vorn, helles Umgebungslicht), Drehen des
  gewählten Items (Gothic dreht es langsam; Vorschlag: um die Bild-Y-Achse, Ansicht als Startlage – beim Drehen
  ändert sich die Kontur, Vorschlag: Fenster der Startlage beibehalten), Größe der Zelle.
- **Prüfblatt:** `gothar-chargen inventory-sheet --out <ordner>` rendert jede Zelle mit dieser Regel (Blender,
  Cycles auf der CPU; Kamera-Shift = Fenster) und setzt ein Raster mit Beschriftung (Modell, Kategorie, Quelle der
  Ansicht) zusammen.
