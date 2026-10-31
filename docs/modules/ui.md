# ui

**Zweck:** Alle Bildschirmoberflächen.

## Zwei Welten
1. **Debug-/Editor-UI**: Dear ImGui (+ ImGuizmo ab M4). Zurzeit in allen Builds, standardmäßig aus (F1).
2. **Spiel-UI**: eigenes schlankes Retained-Mode-System (ADR 0009, angenommen 2026-10-31; „Plan M14“ unten) auf dem Renderer:
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

## Plan M14 (Vorschlag engine, 2026-11-01 – wartet auf Freigabe)
Grundlage: ADR 0009 (angenommen: eigenes Retained-Mode-UI, RmlUi verworfen, ImGui nur Debug/Editor) und die
Festlegungen des Projektinhabers vom 2026-10-31 (Inventar mit 3D-Vorschau je Zelle nach `inventory_views.toml` v2,
keine PNG-Symbole; Lautstärken im Optionsmenü). Offene Gestaltungsfragen stehen am Ende („Fragen an den
Projektinhaber“) und werden nicht von engine festgelegt; bis zur Antwort gelten die als Platzhalter markierten Werte.

### Grundsätze
- **Schichten:** Alles Neue liegt in `ui` (abhängig nur von `core`, `platform`, `render` – die Tabelle in
  `docs/02-architecture.md` bleibt unverändert). Bildschirme bekommen **Ansichtsmodelle** (reine Structs ohne
  `gameplay`- oder `world`-Typen), die `runtime` je Frame füllt, und melden **Aktionen** zurück (z. B.
  `InventoryAction{Equip, item}`), die `runtime` ausführt – dasselbe Muster wie die heutigen ImGui-Panels
  (`InventoryPanel` …), deren Felder der Ausgangspunkt sind. Klänge spielt `runtime` auf dem Bus `ui`; die UI meldet
  nur Ereignisnamen (`select`, `accept` …).
- **Zeichnen:** nach dem Post-Processing (Tonemapping) direkt ins Fenster, vor ImGui (Debug liegt oben). Ein
  Canvas sammelt alle Quads eines Frames in wenigen Draw-Calls (ein Shader für Farbe, Bild, 9-Slice und Schrift).
- **Auflösung:** Layout in **UI-Einheiten** bezogen auf 1080 px Höhe; Skalierung = Fensterhöhe / 1080 ×
  `[ui] scale` (Optionsmenü, 0,75–1,5). Text bleibt dank Distanzfeld-Schrift in jeder Größe scharf.
- **Eingabe:** Ein Bildschirm-Stapel entscheidet, wer die Eingabe bekommt. Offene blockierende Bildschirme
  (Inventar, Dialog, Menüs) nehmen der Spielsteuerung die Aktionen weg, das HUD nie. Neue Aktionen `ui_up`,
  `ui_down`, `ui_left`, `ui_right`, `ui_accept`, `ui_back`, `ui_prev_tab`, `ui_next_tab` in beiden Schemata
  (`engine.toml [bindings.*]`, Vorgaben in Teil B). Die vorhandenen Aktionen `inventory`, `log`, `status`, `map`,
  `pause` öffnen bzw. schließen die Bildschirme.
- **Daten statt Code** (alles im VFS, Hot-Reload wie bei Shadern):

  | Datei | Inhalt | gepflegt von |
  |---|---|---|
  | `assets/source/ui/style.toml` | Farben, Schriften und Größen je Textrolle, Rahmen (9-Slice), Balkenfarben, Klänge je UI-Ereignis, Licht der Item-Vorschau | engine (Werte nach Wunsch des Projektinhabers) |
  | `assets/source/ui/layout.toml` | je Bildschirm Anker, Größen, Abstände, Spalten, Zellgröße, Reiter des Inventars | engine |
  | `assets/source/ui/frames/*.png` | 9-Slice-Rahmen, Balken, Hintergründe (Platzhalter bis zur Stilentscheidung) | engine bzw. Projektinhaber |
  | `assets/source/fonts/<name>.ttf` + `<name>.font.toml` + `OFL.txt` | Schrift und Erzeugungsparameter (Zeichensatz, Größe, Distanzbereich) | engine |
  | `assets/source/text/ui.<sprache>.toml` | UI-Texte nach Schlüssel (`UI_INVENTORY_TITLE = "Inventar"`) | engine |
  | `assets/source/text/script.<sprache>.json` | Übersetzungen der Inhaltstexte aus Lua (Teil G, Vorschlag) | `gothar-voice scan` (engine) |
  | `engine.toml [ui]` | `language`, `scale`, `subtitles`, `nav_repeat_delay`, `nav_repeat_rate` | engine; das Optionsmenü schreibt in die Benutzer-Konfiguration |

  Beispiel `style.toml` (Format v1, Werte Platzhalter):
  ```toml
  version = 1
  [colors]
  text = "#e8dcc0"
  text_dim = "#a89c80"
  highlight = "#ffd36b"
  hp = "#b02020"
  mana = "#2040b0"
  [fonts]
  body = { font = "fonts/alegreya.g7font", size = 22 }   # size in UI-Einheiten (1080p)
  title = { font = "fonts/alegreya_sc.g7font", size = 34 }
  subtitle = { font = "fonts/alegreya.g7font", size = 26, outline = 0.15 }
  [frames.panel]
  image = "ui/frames/panel.png"
  slice = [24, 24, 24, 24]    # links, oben, rechts, unten in Pixeln des Bildes
  [sounds]
  select = "ui_select"        # Namen aus data/sounds.toml
  accept = "ui_accept"
  [item_preview]
  sun_direction = [-0.5, 0.6, 0.6]   # von links oben vorn (wie das Prüfblatt von figuren)
  sun_color = [1.0, 0.96, 0.9]
  sun_intensity = 3.0
  ambient_sky = [0.55, 0.55, 0.6]
  ambient_ground = [0.3, 0.28, 0.25]
  exposure = 1.0
  spin_seconds = 6.0                 # eine Umdrehung des gewählten Items
  ```
  Beispiel `layout.toml`:
  ```toml
  version = 1
  [hud.hp]
  anchor = "bottom_left"
  offset = [32, -32]
  size = [260, 22]
  [inventory]
  anchor = "right"
  columns = 5
  cell = 104                  # UI-Einheiten, quadratisch
  tabs = [                    # Platzhalter, Gruppierung siehe Frage 4
    { key = "UI_TAB_WEAPONS", categories = ["melee_1h", "melee_2h", "bow", "crossbow", "ammo"] },
    { key = "UI_TAB_ARMOR", categories = ["armor", "helmet"] },
    { key = "UI_TAB_MAGIC", categories = ["rune", "scroll"] },
    { key = "UI_TAB_ARTIFACTS", categories = ["ring", "amulet", "belt"] },
    { key = "UI_TAB_FOOD", categories = ["food", "potion"] },
    { key = "UI_TAB_DOCUMENTS", categories = ["document"] },
    { key = "UI_TAB_MISC", categories = ["key", "torch", "misc"] },
  ]
  ```
  Unbekannte Schlüssel → Warnung im Log; jede Kategorie aus `kItemCategories` muss in genau einem Reiter stehen
  (Test, wie `test_inventory.py` für `inventory_views.toml`).

### Teil A – Fundament: Canvas, Schrift, Stil, Texte
- **Dateien:** `engine/ui/include/g7/ui/{Canvas,Font,Text,Style,Strings}.hpp` + `src/`, Shader
  `ui.vert`/`ui.frag` (Farbe, Bild, 9-Slice, MSDF mit Kontur und Schatten), Cooker
  `tools/asset-cooker/src/CookFont.cpp`, CMake-Ziel `g7_fonts` (wie `g7_figures`), Schrift unter
  `assets/source/fonts/`.
- **API (Skizze):**
  ```cpp
  namespace g7::ui {
  struct Rect { Vec2 min, max; };
  class Font {                       // .g7font: Metriken, Glyphen, Kerning, MSDF-Atlas
  public:
      static Result<Font> load(std::span<const std::byte> g7font);
      f32 lineHeight(f32 size) const; f32 ascent(f32 size) const;
  };
  struct TextStyle { const Font* font; f32 size; Color color; f32 outline = 0; Align align = Align::Left; };
  struct TextLayout { std::vector<PlacedGlyph> glyphs; Vec2 size; u32 lines; bool truncated; };
  // Reine CPU-Funktion (ohne GPU testbar): UTF-8, Umbruch an Leerzeichen/Bindestrich, Kerning, "…" bei maxLines.
  TextLayout layoutText(const Font&, std::string_view utf8, const TextStyle&, f32 wrapWidth, u32 maxLines = 0);
  class Canvas {                     // sammelt die Quads eines Frames, zeichnet über die RHI
  public:
      static Result<Canvas> create(render::Device*, render::ShaderLibrary*);  // ohne Device: nur CPU (Tests)
      void begin(Vec2 framebufferPixels, f32 uiScale);
      void rect(const Rect&, Color);
      void image(const Rect&, const rhi::Texture&, Rect uv, Color tint);
      void frame(const Rect&, const NineSlice&);
      void text(Vec2 pos, const TextLayout&, const TextStyle&);
      void pushClip(const Rect&); void popClip();
      void end(render::Device*);
  };
  class Strings {                    // ui.<sprache>.toml; fehlt ein Schlüssel: Schlüssel sichtbar + einmal Warnung
  public:
      static Result<Strings> load(std::string_view language, std::string_view tomlText);
      std::string_view get(std::string_view key) const;
      std::string format(std::string_view key, std::initializer_list<Arg> args) const;  // "Erfahrung +{xp}"
  };
  std::string formatNumber(i64 value, std::string_view language);  // de 1.234 | en 1,234
  Result<Style> loadStyle(std::string_view tomlText);               // style.toml v1
  }
  ```
- **Schrift-Atlas (ADR 0020, Vorschlag):** Der MSDF-Atlas entsteht im Cooker mit **msdfgen (nur Kern, MIT)**;
  Umrisse und Kerning liest `stb_truetype`, gepackt wird mit `stb_rect_pack` (beide aus `stb`, schon Abhängigkeit).
  Ausgabe `fonts/<name>.g7font` (Atlas unkomprimiert + zstd; BC7/UASTC würde die Distanzwerte zerstören). Die
  Laufzeit braucht keine neue Bibliothek. Zeichensatz: Basis-Latein, Latin-1 (ä ö ü ß …), „ “ ‚ ‘ – — … · €;
  fehlende Glyphe → Ersatzzeichen und einmal Warnung. Wird ADR 0020 abgelehnt: Einkanal-SDF über
  `stbtt_GetGlyphSDF` (keine neue Abhängigkeit, etwas weichere Ecken in großen Überschriften), gleiches Format mit
  `kind = "sdf"`.
- **Schrift (Vorschlag, Entscheidung Projektinhaber, Frage 1):** **Alegreya** (Fließtext) und **Alegreya SC**
  (Überschriften), beide SIL Open Font License 1.1, Quelle github.com/huertatipografica/Alegreya. Lizenztext
  `assets/source/fonts/OFL.txt`, Eintrag in `assets/LICENSES.md` (neuer Abschnitt „Schriften“). Die OFL erlaubt das
  Bündeln mit dem Spiel; der erzeugte Atlas ist eine abgeleitete Fassung und bleibt unter OFL (kein reservierter
  Name betroffen, da die Schrift nicht umbenannt weitergegeben wird).
- **Tests** (`tests/ui/`): `test_text.cpp` (UTF-8, Umlaute, Umbruch, lange Wörter, Zeilenzahl, Ellipse,
  Kerning-Paar „AV“), `test_style.cpp` (Parser, Fehlermeldungen, Farben), `test_strings.cpp` (Fallback, Platzhalter,
  Zahlen de/en), Cooker-Test (`.g7font` deterministisch, alle Zeichen des Satzes vorhanden), GPU-Test in
  `render_gpu` (Text und 9-Slice in ein Ziel zeichnen, Pixel an festen Stellen prüfen).
- **DoD:** `gothar --ui-sample` (Fenster nur mit `--frames=N`) zeigt eine Musterseite mit allen Textrollen, Rahmen
  und Balken bei 720p, 1080p und 4K; Screenshots für den Projektinhaber; Tests grün.

### Teil B – Widgets, Layout, Navigation, Bildschirm-Stapel
- **Dateien:** `engine/ui/include/g7/ui/{Widget,Layout,Navigation,Screen}.hpp` + `src/`; `runtime/EngineUi.cpp`
  (besitzt den Stapel, verteilt die Eingabe, sperrt die Spielsteuerung); neue Aktionen in `game/config/engine.toml`.
- **API (Skizze):**
  ```cpp
  namespace g7::ui {
  enum class Anchor : u8 { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
  struct LayoutBox { Anchor anchor; Vec2 offset; Vec2 size; };      // aus layout.toml
  Rect place(const LayoutBox&, const Rect& parent);                   // rein, testbar
  class Widget { /* Rect, sichtbar, fokussierbar, Kinder; measure/arrange/draw */ };
  // Arten: Panel (Rahmen), Label, Image, Bar, Button, List (Scrollen, nur sichtbare Zeilen), Grid,
  // ItemCell (Teil D), Slider/Toggle/Choice (Teil F).
  struct UiInput { NavDir nav; bool accept, back, prevTab, nextTab; Vec2 pointer; bool click; f32 wheel;
                   InputDevice device; };                              // aus ui_*-Aktionen, Stick, Maus
  UiInput buildUiInput(const platform::Input&, const platform::ActionMap&, NavRepeat&, f32 dt);
  // Nächstes Ziel in Richtung dir: erst innerhalb von Liste/Raster, sonst räumlich
  // (Abstand der Mittelpunkte mit Winkelstrafe); rein, testbar.
  std::optional<usize> navigate(std::span<const Rect> focusables, usize current, NavDir dir);
  class Screen {
  public:
      virtual ~Screen() = default;
      virtual ScreenFlags flags() const = 0;   // blocksGameInput, pausesGame, hidesHud, closesOnBack
      virtual void update(const UiInput&, f32 dt) = 0;
      virtual void draw(Canvas&) = 0;
  };
  class ScreenStack {
  public:
      void push(std::unique_ptr<Screen>); void pop(); Screen* top();
      bool blocksGameInput() const; bool pausesGame() const;
  };
  }
  ```
- **Navigation:** Tastatur (Pfeile, im klassischen Schema auch WASD; Enter/Strg bestätigen, Esc/Rücktaste zurück,
  Bild↑/Bild↓ bzw. Q/E Reiter), Gamepad (Steuerkreuz und linker Stick mit `stick_deadzone`, Süd bestätigt, Ost
  zurück, Schultertasten Reiter, Start Menü), Maus (Zeigen setzt den Fokus, Klick bestätigt, Rad scrollt). Halten
  wiederholt nach `nav_repeat_delay` (0,35 s) alle `nav_repeat_rate` (0,08 s). Das zuletzt benutzte Gerät bestimmt
  die Tastenhinweise (zunächst Text wie „[Strg] Benutzen“ bzw. „(A) Benutzen“, Frage 11).
- **Tests:** `test_layout.cpp` (Anker, Skalierung, Clipping), `test_navigation.cpp` (Raster, Listen mit Scrollen,
  räumliche Suche, Wiederholung), `test_screens.cpp` (Stapel, Sperren der Spielsteuerung, Esc schließt den
  obersten Bildschirm).
- **DoD:** Die Musterseite ist mit Tastatur, Gamepad und Maus vollständig bedienbar; bei offenem
  Muster-Bildschirm bewegt sich der Held nicht.

### Teil C – HUD, Meldungen, Dialog und Untertitel, kleine Fenster
- **Dateien:** `engine/ui/include/g7/ui/screens/{Hud,Messages,Dialog,Subtitles,Document,Choice,Lockpick}.hpp`;
  `runtime/EngineUi*.cpp` füllt die Modelle; Skript-Bindung `screen_message` (von `runtime` registriert, in
  `docs/script-api.md` generiert).
- **Modelle (Skizze):**
  ```cpp
  struct HudModel { f32 hp, hpMax, mana, manaMax;
                    std::optional<f32> air;                // nur beim Tauchen
                    std::optional<EnemyBar> enemy;         // Name + Leben des Ziels im Kampf
                    std::optional<FocusLabel> focus; };    // Name + Bildschirmposition
  struct DialogModel { std::string speaker, line; std::vector<std::string> options; i32 selected; };
  struct SubtitleModel { std::string speaker, text; bool ambient; };   // ambient: Zurufe (SVM)
  ```
- **HUD:** Platzhalter-Lage nach Gothic 1 (Leben unten links, Mana unten rechts, Gegnerleben oben mittig, Luft
  über dem Lebensbalken; alles in `layout.toml`, Frage 3). Der Fokusname über dem Ziel ersetzt
  `DebugUi::focusLabel`.
- **Meldungen:** Warteschlange mit den Arten `center` („Erfahrung +50“, „Stufe 3“) und `log` („Apfel erhalten“);
  Lua: `screen_message(text: string, kind?: string, seconds?: number)`. Die Texte bleiben Inhalt in Lua
  (Ereignisse `level_up`, `item_taken` …), die Engine zeigt nur an (Frage 7).
- **Dialog:** ersetzt `DialogPanel`; Entscheidung E3 aus M10 bleibt (wie Gothic 1: Pfeiltasten oder Maus, die
  Aktion wählt). Die Auswahl steht als Liste unten, die gesprochene Zeile als Untertitel mit Sprechername.
  Dialogkamera, Gesten, Lippensync und das Absenken der Musik (M10/M13) bleiben unverändert; ein Untertitel steht so
  lange wie der Take, ohne Take wie heute nach Textlänge.
- **Untertitel:** `[ui] subtitles = true` (Optionsmenü); Text aus `lines.<sprache>.json`, sonst der Quelltext.
  Ob auch Zurufe und Umgebungsgespräche Untertitel bekommen, entscheidet der Projektinhaber (Frage 6).
- **Dokument, Auswahl, Dietrich:** ersetzen `DocumentPanel` (Pergament; Bücher zweiseitig mit Blättern),
  `ChoicePanel` (Amboss, Bett) und `LockpickPanel`.
- **Tests:** Modelle → Widgets ohne GPU (Balkenanteile, Gegnerbalken nur im Kampf, Luft nur unter Wasser),
  Meldungs-Warteschlange (Dauer, Höchstzahl), Dialog-Auswahl per `UiInput`; Runtime-Test: ein Gespräch im
  Testlager nur über `ui_down`/`ui_accept`.
- **DoD:** Im Testlager ein Gespräch mit Antwortwahl, einen Brief lesen, am Amboss schmieden und schlafen – ohne
  ImGui-Fenster.

### Teil D – Inventar mit 3D-Zellen, Handel, Truhe, Plündern
- **Dateien:** `engine/ui/include/g7/ui/{InventoryViews,ItemPreview}.hpp`, `screens/{Inventory,Trade}.hpp`;
  `render`: Kamera mit Bildfenster (s. u.); `runtime/EngineUiInventory.cpp`.
- **Vertrag `inventory_views.toml`:** engine **stimmt Version 2 zu** (Kontur-Regel aus #302), sobald #302 gemergt
  ist; die Vertragszeile in `docs/coordination.md` aktualisiert figuren dort. Anmerkungen von engine:
  1. engine rechnet `R`, `c`, `d`, `w`, `e` **beim Laden** aus den LOD-0-Positionen des gekochten Meshes (einmal je
     Modellpfad, im Cache) – keine Formatänderung an `.g7mesh`. Abweichungen durch die Quantisierung im Cooker
     bleiben unter einem Pixel.
  2. **Drehen des gewählten Items:** um die Bild-Y-Achse durch die Mitte `c`, eine Umdrehung in
     `style.toml [item_preview] spin_seconds` (Platzhalter 6 s), Start in der Ansicht. Das **Bildfenster der
     Startlage bleibt** (Vorschlag figuren übernommen); was darüber hinausragt, schneidet die Zelle ab (Clipping).
     Der Abstand `d` bleibt; alle Vertices liegen auch gedreht vor der Kamera (Test).
  3. **Licht:** feste Studio-Beleuchtung aus `style.toml [item_preview]` (Sonne von links oben vorn, helles
     Hemisphären-Umgebungslicht, keine Schatten, kein Nebel, feste Belichtung) – wie im Prüfblatt.
  4. **Zellgröße:** `layout.toml [inventory] cell` (Platzhalter 104 UI-Einheiten, in 1080p 104 px).
- **Rendering (Entscheidung engine): Render-to-Texture in einen Zellen-Atlas** statt Viewport und Scissor im
  Fensterbild. Gründe: Der Material-Shader schreibt lineares HDR, das erst das Tonemapping richtig macht – direkt
  ins fertige Bild gezeichnet stimmten Farben und Belichtung nicht; der Atlas wird nur bei Änderungen neu gezeichnet
  (Inventar oder Größe geändert), je Frame nur die Zelle des gedrehten Items; die Zellen lassen sich wie Bilder
  abdunkeln (nicht benutzbar), einblenden und beim Scrollen clippen. Ablauf je Kachel: kleines `SceneTarget` (HDR,
  Tiefe) → `MeshRenderer::draw` mit fester Beleuchtung und Kamera mit Bildfenster → Tonemapping mit fester
  Belichtung in die RGBA8-Kachel des Atlas, Alpha = Deckung (durchsichtiger Hintergrund; den Zellrahmen zeichnet
  der Stil). Kantenglättung durch doppelte Kachelgröße und Verkleinern (keine MSAA-Erweiterung der RHI nötig).
- **render-Erweiterung:** `Camera` bekommt ein optionales Bildfenster (`Vec2 windowCenter = {0, 0}`,
  `f32 windowHalfSize = 1`), angewandt nach der Perspektive: `x' = (x_ndc − w.x) / e`, `y' = (y_ndc − w.y) / e` –
  genau Schritt 4 der Kontur-Regel. Die Standardwerte lassen das bisherige Bild unverändert (Test).
- **API (Skizze):**
  ```cpp
  struct InventoryView { f32 yaw, pitch, roll, fill; };
  class InventoryViews {   // inventory_views.toml v2: [default] <- [category.<c>] <- [model."<mesh>"]
  public:
      static Result<InventoryViews> load(std::string_view tomlText);
      InventoryView resolve(std::string_view mesh, std::string_view category) const;
      f32 fovDegrees() const;
  };
  struct CellFrame { Mat3 rotation; Vec3 center; f32 distance; Vec2 window; f32 halfSize; };
  CellFrame computeCell(std::span<const Vec3> lod0Positions, const InventoryView&, f32 fovDegrees);  // rein
  class ItemPreviewAtlas {
  public:
      static Result<ItemPreviewAtlas> create(render::Device&, render::ShaderLibrary&, u32 cellPixels, u32 cells);
      u32 acquire(const PreviewModel&);        // Mesh, Materialien, CellFrame -> Kachel
      void spin(u32 tile, f32 angle);          // das gewählte Item
      void renderDirty(render::Device&, render::MeshRenderer&, const PreviewLight&);
      Rect uv(u32 tile) const;
      const rhi::Texture& texture() const;
  };
  ```
- **Inventar-Bildschirm** (wie Gothic): Raster mit Reitern nach Kategorie-Gruppen (`layout.toml`), Stapelzahl in
  der Zelle, angelegte Items markiert, darunter ein Infokasten (Name, Werte wie Schaden, Schutz, Wirkung, Wert in
  Gulden, Anzahl). Aktionen: Benutzen bzw. Anlegen (bestätigen), Ablegen auf den Boden (eigene Taste), Reiter
  wechseln. **Handel:** Händler links, Held rechts, Preis im Infokasten, Gulden beider Seiten. **Truhe** und
  **Plündern:** Behälter links, Held rechts, Nehmen und Hineinlegen. Die Logik (Preise, Besitz, Diebstahl) bleibt
  in `runtime`/`gameplay` wie in M8/M10.
- **Tests:** `test_inventory_views.cpp` (Auflösung default ← Kategorie ← Modell, Fehler bei fehlenden
  Pflichtfeldern, Version 2), `test_item_cell.cpp` gegen **Referenzwerte aus `inventory.py`** (Würfel, flacher
  Quader, Schwert diagonal: Mitte, Füllgrad, `d`, `w`, `e` auf 1e-4), gedreht bleiben alle Vertices vor der Kamera;
  Reiter decken alle `kItemCategories` ab; GPU-Test: drei echte Items in den Atlas zeichnen und die Deckung messen
  (Box der deckenden Pixel mittig ± 1 px, Füllgrad `fill` ± 2 px); Runtime-Test: im Testlager ein Item anlegen,
  benutzen, an einen Händler verkaufen, aus einer Truhe nehmen – nur über `UiInput`.
- **DoD:** Inventar, Handel, Truhe und Plündern im Testlager ohne ImGui; ein Prüfbild aller Item-Zellen aus der
  Engine neben dem Blender-Prüfblatt von figuren (gleiche Lage und Füllung).

### Teil E – Charakter, Tagebuch, Karte
- **Charakter** (`status`, B): Stufe, Erfahrung bis zur nächsten Stufe, Lernpunkte, Attribute, Talente mit Stufe,
  Schutzwerte, Gilde. **Tagebuch** (`log`, N): Reiter laufende, erledigte und gescheiterte Aufträge sowie
  Allgemeines, Einträge mit Umbruch und Scrollen (ersetzt `DiaryPanel`). **Karte** (`map`, M): nur, wenn der Held
  ein Karten-Item besitzt; das Item nennt ein Bild (Lua-Feld `map = "ui/maps/<name>.png"`) – Bild und Item fehlen
  noch (Frage 9).
- **Tests:** Modell → Zeilen (Talente nach `kTalents`, Reiter des Tagebuchs, Scrollen langer Einträge);
  Runtime-Test öffnet beide Bildschirme im Testlager.
- **DoD:** Charakter und Tagebuch im Testlager vollständig bedienbar; die Karte, sobald ein Karten-Item existiert.

### Teil F – Hauptmenü, Pause, Optionen, Ladebildschirm
- **Hauptmenü:** Neues Spiel, Fortsetzen (im Spiel), Laden und Speichern (bis M15 ausgegraut), Optionen, Beenden.
- **Optionen:** Grafik (Auflösung, Vollbild, VSync, Bildratenlimit, Sichtweite und Schatten, soweit einstellbar),
  **Audio** (Lautstärken `master`, `music`, `effects`, `voice`, `ambient`, `ui` aus `engine.toml [audio]`, wirken
  sofort), Steuerung (Schema classic/modern, Tasten neu belegen mit „Taste drücken“, Mausempfindlichkeit), Spiel
  (Sprache, Untertitel, UI-Skalierung). Gespeichert wird nur, was vom Standard abweicht, in die vorhandene
  Benutzer-Konfiguration (`%APPDATA%\Gothar\Gothar\config.toml`); dafür bekommt `core::Config` ein `set` und ein
  atomisches `save` (temporäre Datei + Umbenennen).
- **Ladebildschirm:** beim Start und beim Weltwechsel ein Bild mit Weltnamen und Fortschritt (offene Ladeaufträge
  des `AssetManager`); solange der Weltwechsel synchron lädt, wird vorher mindestens ein Frame gezeichnet.
- **Pause:** `pause` (Esc/Start) öffnet das Menü und pausiert die Simulation (`Engine::setPaused`), statt wie bis
  M13 direkt umzuschalten (Frage 8).
- **Tests:** Optionen schreiben und lesen (nur Abweichungen, atomisch, fremde Schlüssel bleiben erhalten),
  Lautstärke-Regler ändert den Bus (Mixer headless), Tastenbelegung mit Konflikterkennung, Menü-Stapel (Esc aus
  den Optionen zurück ins Menü, aus dem Menü zurück ins Spiel).
- **DoD:** Start ins Hauptmenü, Neues Spiel, Optionen ändern (bleiben nach einem Neustart erhalten), Beenden –
  alles ohne Debug-UI und mit dem Gamepad bedienbar.

### Teil G – Lokalisierung DE/EN, Aufräumen, DoD der Phase
- **UI-Texte:** `text/ui.de.toml` und `text/ui.en.toml` vollständig; Test: beide Tabellen haben dieselben
  Schlüssel, und jeder im C++-Code verwendete Schlüssel existiert.
- **Inhaltstexte aus Lua (Vorschlag):** Namen und Beschreibungen von Items und NPCs, Info-Beschreibungen
  (`description`), Tagebuch-Einträge und Meldungen stehen deutsch inline in Lua (E2). `gothar-voice scan` sammelt
  sie zusätzlich in `assets/source/text/script.<sprache>.json` (Schlüssel = Instanz und Feld bzw. Info und Nummer,
  wie bei den Sprechtexten); die Engine sucht bei Sprache ≠ `de` die Übersetzung, sonst bleibt der deutsche Text
  (einmal Warnung). Gesprochene Zeilen: Untertitel aus `lines.en.json`, die Stimmen bleiben deutsch, bis es
  englische Takes gibt. Das erweitert den Vertrag „Sprechtexte“ (Werkzeug von engine) und wird vor der Umsetzung
  in `docs/coordination.md` eingetragen.
- **Aufräumen:** Die ImGui-Spielfenster (`InventoryPanel`, `TradePanel`, `DialogPanel`, `DiaryPanel`,
  `DocumentPanel`, `ChoicePanel`, `LockpickPanel`, `focusLabel`) entfallen; Konsole, „AI“, „Creatures“,
  „Animation“ und Editor bleiben ImGui.
- **DoD der Phase (Roadmap):** Das Vertical Slice (Testlager, drei Aufträge, Handel, Lehrer, Kampf, Magie) ist
  vollständig ohne Debug-UI spielbar – geprüft durch einen Runtime-Szenariotest, der es nur über UI-Aktionen
  durchläuft, und durch eine Begehung des Projektinhabers (auf Deutsch und Englisch).

### Reihenfolge und Umfang
A → B → C, D, E, F (in dieser Reihenfolge; D ist der größte Teil) → G. Je Teil ein PR (D ggf. zwei: erst der
Zellen-Atlas, dann die Bildschirme). Jeder Teil hält das Spiel lauffähig: Bis ein Bildschirm ersetzt ist, bleibt
sein ImGui-Fenster.

### Neue Abhängigkeiten und ADRs
- **ADR 0020 (Vorschlag):** `msdfgen` (nur Kern, MIT) im Cooker für den MSDF-Atlas – braucht Freigabe; bei Annahme
  Eintrag in `vcpkg.json` (`{ "name": "msdfgen", "default-features": false }`) und in `docs/05-build.md`.
  Rückfallebene ohne Abhängigkeit: Einkanal-SDF mit `stb_truetype`.
- Sonst keine neue Bibliothek: Layout, Text, Navigation und Atlas sind eigener Code; TOML über toml++, Bilder über
  stb/KTX wie bisher.
- Neue Assets: eine OFL-Schrift (Lizenztext und `assets/LICENSES.md`), Platzhalter-Rahmen (eigene Arbeit),
  Platzhalter-Klänge über `tools/audio` (`ui_select`, `ui_accept`, `ui_back`, `ui_open`, `ui_close`).

### Fragen an den Projektinhaber
1. **Schrift:** Vorschlag Alegreya + Alegreya SC (OFL; lesbare Buchschrift mit kalligrafischem Zug). Alternativen:
   EB Garamond (klassisch), IM Fell English (alter Buchdruck, unruhiger), für Überschriften Uncial Antiqua
   (Unziale) oder UnifrakturMaguntia (Fraktur). Welche Richtung?
2. **Menü- und Rahmenstil:** Gothic-artig dunkel (Holz, Eisen, Pergament für Dokumente) oder heller? Woher die
   Rahmen: prozedurale Platzhalter von engine, ein CC0-Paket (z. B. Kenney „Fantasy UI Borders“) oder eigene
   Vorlagen des Projektinhabers?
3. **HUD:** Lage wie Gothic 1 (Leben unten links, Mana unten rechts, Gegnerleben oben mittig)? Immer sichtbar oder
   nur bei Bedarf (Mana nur mit Magie, Leben nur nach Schaden)? Zusätzlich Uhrzeit oder Kompass (Gothic 1: nein)?
4. **Inventar:** Reiter nach Kategorie-Gruppen (Vorschlag oben) oder eine durchgehende Liste? Spalten und
   Zellgröße? Das gewählte Item zusätzlich groß im Infokasten zeigen? Läuft die Welt bei offenem Inventar weiter
   (Gothic 1: ja; Vorschlag: ja)?
5. **Handel:** Kauf einzeln je Bestätigen (wie in M10) oder Mengenwahl bei Stapeln?
6. **Untertitel:** standardmäßig an? Mit Sprechername? Auch für Zurufe und Umgebungsgespräche der NPCs? Lage
   unten mittig oder oben?
7. **Bildschirmmeldungen:** „Erfahrung +50“ groß in der Mitte, „Apfel erhalten“ als kleine Zeile – wo und wie
   lange?
8. **Hauptmenü:** Hintergrund als langsame Kamerafahrt durch Leonberg oder als Standbild? Eigenes Musikthema?
   Öffnet Esc im Spiel das Hauptmenü mit „Fortsetzen“ und pausiert dabei?
9. **Karte:** Soll es ein Karten-Item für Leonberg geben (z. B. ein von welt aus den OSM-Daten gezeichneter
   Stadtplan, mit Nennung „© OpenStreetMap-Mitwirkende“)? Spielerposition anzeigen (Gothic 1: nein)?
10. **Englisch:** Englische Untertitel zu deutschen Stimmen vorerst in Ordnung? Übersetzung als Entwurf von Claude
    mit Abnahme durch den Projektinhaber?
11. **Tastenhinweise fürs Gamepad:** zunächst Text („(A)“, „[Strg]“) oder eigene Symbole?
12. **ADR 0020:** `msdfgen` (Kern) als Werkzeug-Abhängigkeit freigeben oder Einkanal-SDF ohne neue Abhängigkeit?

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
