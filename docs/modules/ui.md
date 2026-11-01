# ui

**Zweck:** Alle Bildschirmoberflächen.

## Zwei Welten
1. **Debug-/Editor-UI**: Dear ImGui (+ ImGuizmo ab M4). Zurzeit in allen Builds, standardmäßig aus (F1). Seit M14
   Teil G nur noch Debug und Editor: Konsole, „Gothar“, „AI“, „Creatures“, „Animation“, Editor-Fenster – das Spiel
   selbst zeigt ausschließlich die Spiel-UI.
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
- **Entfallen (M14 Teil G):** die ImGui-Spielfenster der Phasen M8–M10 (`InventoryPanel`, `TradePanel`,
  `DialogPanel`, `DiaryPanel`, `DocumentPanel`, `ChoicePanel`, `LockpickPanel`, `focusLabel`) samt ihren
  Rückfallebenen in der Engine; ImGui öffnet nur noch mit F1 (`debug_ui`) oder der Konsole (^).

## Spiel-UI Teil G (umgesetzt, M14) – Lokalisierung DE/EN, Abbau der ImGui-Spielfenster, DoD
`asset/ScriptTexts.hpp`; Engine `runtime/EngineText.cpp`; Werkzeug `gothar-voice texts` (`tools/voice`).

```cpp
namespace g7::asset {
class ScriptTexts {                         // text/script.<sprache>.json, Schema 1
    static Result<ScriptTexts> parse(std::string_view json, std::string_view source);
    const std::string* find(std::string_view german) const;   // Übersetzung eines deutschen Textes; nullptr: keine
    const std::string* findKey(std::string_view key) const;   // "it_apple.name", "dia_gate_guard_hello_01"
};
}
namespace g7 {   // Engine
std::string language() const;                                  // [ui] language: "de", "en"
std::string uiText(std::string_view key, std::initializer_list<ui::TextArg> args = {}) const; // text/ui.<sprache>.toml
std::string contentText(std::string_view german) const;       // Inhaltstext der Skripte in der UI-Sprache
std::string castBlockedText(const gameplay::CastBlock&) const; // Z5 in Worten (gameplay liefert nur den Grund)
Engine::DiaryData diaryData() const;                           // ersetzt diaryPanelData() / ui::DiaryPanel
bool debugUiFrame() const;                                     // hat ImGui in diesem Frame gezeichnet?
}
```
- **UI-Texte:** Alle Texte der Engine stehen in `text/ui.de.toml`/`ui.en.toml`, auch die Meldungen (Schloss,
  Dietrich, Handel, Taschendiebstahl, Fernkampf, Magie, Schlaf, Schmieden), „Ende“ im Dialog, Tagebuch-Zeilen
  („Tag {day}, {time}: {text}“, „Kapitel {n}“) und die Antworten des Lehrers. `Engine::uiText` liest die Tabelle der
  eingestellten Sprache auch ohne Fenster (Tests ohne Spiel-UI sehen die deutschen Meldungen wie bisher).
  `gameplay::castBlocked` gibt statt eines deutschen Satzes `CastBlock{reason, circle}` zurück. Scheitert eine
  Aktion im Inventar oder Handel, zeigt der Bildschirm die Meldung der Aktion bzw. „Das geht nicht.“; fehlt Stärke
  oder Geschick zum Anlegen, „Benötigt: …“ – die englischen Fehlertexte der Engine bleiben dem Log und Lua.
- **Inhaltstexte:** Die deutschen Texte bleiben inline in Lua (E2), ebenso alle Spieldaten (Story, Dialogmenüs,
  Item-Namen, Spielstände). Die Bildschirme übersetzen beim Anzeigen mit `contentText`: Item-, NPC-, Mob-,
  Rezept- und Auftragsnamen, Fokusnamen, Dialogzeilen und -antworten (Untertitel), Zurufe, Schriftstücke,
  Tagebuch-Einträge und Lua-Meldungen (`notice`). Die Übersetzung steht in `text/script.<sprache>.json`
  (Vertrag „Inhaltstexte“ in `docs/coordination.md`); gefunden wird über den deutschen Text, Einträge `offen`
  oder `veraltet` zählen nicht. Fehlt sie, bleibt der deutsche Text (Debug-Log einmal je Text). Bei Deutsch wird
  nichts geladen.
- **Lua:** `ui_text(key, args?)` (UI-Text mit Platzhaltern, für Texte, die erst zur Laufzeit entstehen) und
  `tr(text)` (Inhaltstext in der UI-Sprache). `lib/diary.lua` (Meldungen zu Aufträgen und Kapiteln) und
  `data/teaching.lua` (Antworten des Lehrers) bauen ihre Texte damit.
- **Sprache umschalten:** Optionen → Spiel → Sprache wirkt sofort (UI-Texte, Inhaltstexte, Schriften neu geladen)
  und wird in der Benutzer-Konfiguration gespeichert. Die Stimmen bleiben deutsch; Untertitel kommen in der
  UI-Sprache (F10). Abweichung vom Plan: Die gesprochenen Zeilen stehen mit ihrem Sprach-Schlüssel ebenfalls in
  `script.<sprache>.json`; eine `voice/lines.en.json` braucht es erst für englische Aufnahmen.
- **Englisch:** Entwurf von Claude (Status `entwurf`, F10); der Projektinhaber nimmt ihn ab (Status `abgenommen`).
  Britische Schreibung („Armour“, „Jewellery“), Währung „Gulden“ auch im Englischen.
- **Debug-UI:** siehe oben („Entfallen“). `MobInfo` hat dazu `position` (Szenariotest).
- **Tests:** `tests/ui/test_strings.cpp` (DE und EN mit denselben Schlüsseln; jeder Schlüssel, den Code, UI-Daten
  und Skripte nennen, steht in der Tabelle), `tests/asset/test_script_texts.cpp` (Parser; die englische Datei hat
  nichts offen), `tools/voice/tests/test_texts.py` (Scan, Abgleich, Übernahme bei neuem Schlüssel, die Datei des
  Repos passt zu den Skripten), CI `gothar-voice check`. **DoD:** `tests/runtime/test_engine_vertical_slice.cpp`
  (ctest `runtime_slice`, ~10 s im Debug-Build): Hauptmenü → Neues Spiel → Dialog → Auftrag → Botengang → Handel
  (Mengenwahl) → Inventar (Schwert anlegen, Apfel essen) → Truhe → Kampf mit Wolf → Tagebuch → Pause, nur über
  SDL-Tasten der ausgelieferten Belegung; ein zweiter Fall stellt in den Optionen auf Englisch um und spricht mit
  der Wache (Fokusname, Antworten, Meldung, Tagebuch englisch; Story bleibt deutsch).

## Spiel-UI Teil A (umgesetzt, M14) – Schrift, Text, Stil, Texte, Canvas
`Font.hpp`, `Text.hpp`, `Style.hpp`, `Strings.hpp`, `Canvas.hpp`, `UiResources.hpp`; Shader
`ui.vert`/`ui.frag`; Schriften nach ADR 0020 (`asset/FontFile.hpp`, `g7-cook --font`). `ui` hängt dafür zusätzlich von
`asset` ab (`.g7font`, Bilder dekodieren; Tabelle in `docs/02-architecture.md`).

```cpp
namespace g7::ui {
class Font {                                   // .g7font: Glyphen, Kerning, MSDF-Atlas
    static Result<Font> load(std::span<const u8> g7font, std::string name);
    static Result<Font> fromData(asset::FontData, std::string name);
    const asset::FontGlyph* find(u32 codepoint) const;     // nullptr: fehlt
    const asset::FontGlyph& glyph(u32 codepoint) const;    // fehlt: U+FFFD, '?', erste Glyphe; einmal Warnung
    f32 kerning(u32 left, u32 right) const;                 // em
    f32 ascender() const, descender() const, lineHeight() const;          // em
    f32 screenPixelRange(f32 pixelSize) const;              // Distanzbereich in Bildschirm-Pixeln, >= 1
    Result<void> upload(render::Device&); const rhi::Texture* texture() const;
};
struct TextOptions { f32 size; f32 wrapWidth = 0; u32 maxLines = 0; Align align = Align::Left; };
TextLayout layoutText(const Font&, std::string_view utf8, const TextOptions&);  // rein, ohne GPU
f32 measureText(const Font&, std::string_view utf8, f32 size);
Result<Style> loadStyle(std::string_view toml, std::string_view source);       // ui/style.toml v1
class Strings { static Result<Strings> load(std::string language, std::string_view toml, std::string_view source);
                std::string_view get(key) const; std::string format(key, {{"xp", "50"}}) const; };
std::string formatNumber(i64, std::string_view language);  // de 1.234.567, en 1,234,567
class Canvas {                                  // Rechtecke, Bilder, 9-Slice, Text; Pixel, Ursprung oben links
    static Result<Canvas> create(render::Device*, render::ShaderLibrary*);   // ohne Device: nur sammeln (Tests)
    void begin(Vec2 pixels, f32 uiScale); void rect(...); void image(...); void frame(Rect, NineSlice, Color);
    void text(Vec2 topLeft, const TextLayout&, const TextStyle&); void pushClip(Rect); void popClip();
    void end(render::Device*);
};
class UiResources {                             // Stil, Texte einer Sprache, Schriften, Rahmenbilder aus dem VFS
    static Result<UiResources> load(const ReadFile&, std::string language, render::Device*);
    TextStyle textStyle(role, uiScale) const; f32 textSize(role, uiScale) const;
    TextLayout layout(role, utf8, uiScale, TextOptions = {}) const; NineSlice frame(name, uiScale) const;
};
}
```
- **Koordinaten:** Canvas in Framebuffer-Pixeln, +Y unten. Bildschirme rechnen in **UI-Einheiten** (Pixel bei
  1080 px Höhe) und multiplizieren mit `uiScale` = Fensterhöhe / 1080 × `[ui] scale` (0,5–2).
- **Zeichnen:** alle Quads eines Frames in einem Vertex-/Index-Puffer (32-Byte-Vertex: Position, UV, Farbe,
  Distanzbereich, Kontur, Konturfarbe), ein Draw je Folge mit gleicher Textur und gleichem Clip; Alpha-Blending, ohne
  Tiefe, nach dem Tonemapping und vor ImGui. Farben sind Anzeigewerte. Bilder (Rahmen, später Symbole) werden als
  sRGB-Texturen gelesen und im Shader zurück in Anzeigewerte kodiert – PNG in der Entwicklung und BC7-KTX2 aus dem Pak
  sehen gleich aus. Der Schriftatlas ist linear (Distanzwerte).
- **Text:** UTF-8 (`core::nextCodepoint`), Umbruch an `\n`, nach Leerzeichen und Bindestrichen bzw. Gedankenstrichen,
  zu lange Wörter zwischen Zeichen; Leerzeichen am Zeilenende und nach automatischem Umbruch entfallen; Ausrichtung
  links/mittig/rechts innerhalb der Umbruchbreite; `maxLines` kürzt mit „…“. Kerning paarweise aus der Schrift.
- **MSDF-Shader:** Median der drei Kanäle, Kante bei 0,5, Kantenglättung über `screenPixelRange` (Distanzbereich ×
  Größe / Atlasgröße). Kontur in Pixeln (`outline` der Textrolle × `uiScale`); sie ist durch den halben Distanzbereich
  begrenzt (Schriften: `range = 8` bei 48 px je em).
- **9-Slice:** Ränder in Bildpixeln × `scale` der Rahmen-Definition × `uiScale`; Ränder breiter als das Rechteck
  schrumpfen anteilig. Ohne Bild wird der Rahmen eine Fläche in seiner Tönung.
- **Fehlende Daten:** fehlende Schrift oder fehlendes Rahmenbild → Warnung, Text wird nicht bzw. Rahmen als Fläche
  gezeichnet; fehlender Text-Schlüssel → der Schlüssel erscheint (einmal Warnung); unbekannte Textrolle, Farbe oder
  Rahmen → Warnung, Magenta bzw. Vorgabe. Ein kaputter Stil oder eine kaputte Texttabelle → keine Spiel-UI, Fehler im Log.
- **Daten:** `ui/style.toml` (Farben, Textrollen `body`, `small`, `title`, `subtitle`, `message`, `document`, Rahmen
  `panel` und `parchment`), `text/ui.de.toml` und `text/ui.en.toml` (Test: gleiche Schlüssel), Schriften
  `fonts/alegreya.g7font` und `fonts/alegreya_sc.g7font` (Alegreya bzw. Alegreya SC, OFL, F1), Platzhalter-Rahmen
  `ui/frames/panel.png` (dunkles Holz mit Eisenrand) und `parchment.png` (F2).
- **Engine** (`runtime/EngineUi.cpp`): `initGameUi()` lädt Canvas und Ressourcen beim Start, `drawGameUi()` zeichnet
  nach der Szene; `engine.toml [ui]` `language` (de/en) und `scale`. Bildschirme, Eingabe und Neu laden: Teil B.
- **Abweichung vom Plan:** Alegreya liegt als statische Instanz (Gewicht 400) bei, wie Google Fonts sie ausliefert:
  stb_truetype liest das Kerning der variablen Schrift nicht (0 Paare), das der statischen Instanz schon (6205 Paare).
- **Tests:** `tests/ui/test_text.cpp`, `test_style.cpp`, `test_strings.cpp`, `test_canvas.cpp` (synthetische Schrift,
  ohne GPU), `tests/asset/test_font_file.cpp`, `tests/cook/test_fonts.cpp` (Atlas von Alegreya: Stamm innen, Punze
  außen, Kerning „AV“, deterministisch), `tests/core/test_stringutil.cpp` (UTF-8), GPU: `test_ui_canvas.cpp` (Farben,
  Clip, sRGB-Bild, Text aus der ausgelieferten Schrift).

## Spiel-UI Teil B (umgesetzt, M14) – Layout, Widgets, Navigation, Bildschirm-Stapel, Hot-Reload
`Layout.hpp`, `Navigation.hpp`, `Widget.hpp`, `Screen.hpp`, `SamplePage.hpp` (jetzt `SampleScreen`); Daten
`ui/layout.toml`; Aktionen `ui_*` in `platform::Action` und `engine.toml`; Engine `runtime/EngineUi.cpp`.

```cpp
namespace g7::ui {
enum class Anchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
struct LayoutBox { Anchor anchor; Vec2 offset; Vec2 size; };        // UI-Einheiten, +Y unten; Größe <= 0: Eltern + Wert
Rect place(const LayoutBox&, const Rect& parent, f32 scale);        // derselbe Punkt von Eltern und Kasten
class Layout { static Result<Layout> load(toml, source); LayoutBox box(key, fallback) const; f32 number(key, fallback) const; };
struct UiInput { NavDir nav; bool accept, back, prevTab, nextTab; Vec2 pointer; bool pointerMoved, click; f32 wheel;
                 InputDevice device; };                              // Keyboard, Mouse, Gamepad (zuletzt benutzt)
UiInput buildUiInput(const platform::Input&, const platform::ActionMap&, NavRepeat&, f32 dt, Vec2 pointerScale);
std::optional<usize> navigate(std::span<const Rect>, usize current, NavDir);   // räumliche Fokus-Suche
class Widget { LayoutBox box; std::string layoutKey; bool visible; template <T> T& add(...); const Rect& rect();
               virtual bool focusable(), navigate(NavDir), accept(), scroll(f32), pointerAt(Vec2); };
// Panel (Rahmen), Label (Textrolle, UiText, Umbruch), Bar (Füllung, Farbe, Rahmen "bar"), Button, Stack (vertikal/
// horizontal), List (Zeilen, Auswahl, Scrollen), Grid (Zellen, Auswahl, Scrollen, drawCell-Rückruf)
struct UiText { std::string value; bool isKey; std::vector<std::pair<std::string, std::string>> args; };
class Screen { virtual ScreenFlags flags() const; Widget& root(); Widget* focus();
               void update(const UiInput*, f32 dt, const Rect& area, f32 scale, const Layout*);
               void draw(Canvas&, const UiResources&) const; void close();
protected: virtual bool onBack(); virtual bool onTab(int); virtual void onUpdate(f32); };
struct ScreenFlags { bool blocksGameInput = true, pausesGame = false, hidesHud = false; };
class ScreenStack { push, pop, top, find(name), blocksGameInput(), pausesGame(), hidesHud(), update(...), draw(...),
                    takeEvents(); };
}
```
- **Layout:** Ein Widget nimmt seinen Kasten aus `layout.toml`, wenn `layoutKey` gesetzt ist (`hud.hp`,
  `sample.list` …), sonst seinen eigenen `box` als Vorgabe; fehlende oder ungültige Einträge melden sich einmal im Log.
  Der Kasten wird jeden Frame neu ausgewertet, ein geändertes `layout.toml` wirkt also sofort.
- **Texte:** `UiText` ist ein Schlüssel (mit `{platzhaltern}`) oder ein wörtlicher Text (Namen und Werte aus dem
  Spiel); Schlüssel werden beim Zeichnen nachgeschlagen.
- **Navigation:** Ein Schritt geht zuerst in das fokussierte Widget (nächste Zeile oder Zelle); an dessen Rand
  wandert der Fokus räumlich zum nächsten fokussierbaren Widget in der Richtung (Abstand längs + 2 × quer).
  Gehaltene Richtungen wiederholen nach `[ui] nav_repeat_delay` (0,35 s) alle `nav_repeat_rate` (0,08 s). Der linke
  Stick zählt ab halbem Ausschlag. Maus: Zeigen fokussiert (und wählt Zeile oder Zelle), Klick bestätigt, das Rad
  scrollt, was unter dem Zeiger liegt. `UiInput::device` merkt sich das zuletzt benutzte Gerät für Tastenhinweise (F11).
- **Aktionen** (beide Schemata, `engine.toml`): `ui_up`/`ui_down`/`ui_left`/`ui_right` (Pfeile, WASD, Steuerkreuz),
  `ui_accept` (Enter, Strg, Süd), `ui_back` (Esc, Rücktaste, Ost), `ui_prev_tab`/`ui_next_tab` (Q/E, Bild↑/Bild↓,
  Schultertasten). Sie wirken nur, solange ein Bildschirm offen ist.
- **Stapel und Spielsteuerung:** Nur der oberste Bildschirm bekommt die Eingabe, die anderen werden nur angeordnet
  und gezeichnet (von unten nach oben). Ist zu Beginn eines Frames ein blockierender Bildschirm offen, bekommt das
  Spiel in diesem Frame keine Tasten und keine Maus – die Taste, die den Bildschirm schließt (Esc), pausiert also nicht
  zugleich das Spiel. `pausesGame` hält die Simulation an. Geschlossene Bildschirme verschwinden nach dem Update.
- **UI-Ereignisse:** Bildschirme melden `select`, `accept`, `back`; die Engine spielt dazu `style.toml [sounds]`
  (noch leer, Platzhalter-Klänge folgen).
- **Hot-Reload:** Mit `[assets] hot_reload` prüft die Engine alle 0,5 s die Zeitstempel aller UI-Dateien (Stil,
  Layout, Texte, Schriften, Rahmenbilder) und lädt bei einer Änderung alles neu; ein Fehler behält die alte Fassung.
- **Rahmen-Namen (stabil, Vertrag mit figuren):** Die Rahmenbilder unter `ui/frames/` macht figuren
  (`gothar-chargen ui-frames`, #305; 2 Bildpixel je UI-Einheit, daher `scale = 0.5`, `slice` in Bildpixeln):
  `panel`, `parchment`, `leather`, `tooltip`, `cell`, `cell_selected`, `cell_equipped`, `tab`, `tab_active`, `button`,
  `button_focus`, `button_pressed`, `bar` (Rand über der Füllung), `bar_hp`, `bar_mana`, `bar_enemy`, `bar_air`
  (eingefärbte Füllungen), `subtitle`, `highlight`, `slider_track`, `slider_knob` und die Kacheln `bg_wood`,
  `bg_leather`, `bg_parchment` (noch ungenutzt: die Canvas kachelt noch nicht). Ein Test prüft Namen und Bilder;
  neue Rahmen trägt engine früh in `style.toml` ein. Balken: 260 × 24 UI-Einheiten, Füllung um 7 × 5 eingerückt.
- **Musterseite** (`--ui-sample`): Fenster mit Liste, Zellenraster und zwei Knöpfen, dazu HUD-Balken, Meldung und
  Untertitel an ihren Plätzen; mit Tastatur, Gamepad und Maus bedienbar, Esc oder „Schließen“ schließt sie.
- **Engine:** Die Spiel-UI lädt jetzt auch ohne Renderer (Layout und Eingabe in Tests); ohne Framebuffer gilt
  1920 × 1080.
- **Tests:** `tests/ui/test_layout.cpp` (Anker, Größen, `layout.toml`, ausgelieferte Daten und Rahmen),
  `test_navigation.cpp` (Wiederholung, Geräte, Stick, Maus, räumliche Suche), `test_widgets.cpp` (Stack, Liste, Raster,
  Fokus mit Tasten und Maus, Stapel), `tests/runtime/test_engine_ui.cpp` (Musterseite über SDL-Tasten; Esc schließt
  sie, bevor es pausiert).

## Spiel-UI Teil C (umgesetzt, M14) – HUD, Meldungen, Untertitel, Dialog, Dokument, Auswahl, Dietrich
`Hud.hpp` (HUD, `MessageQueue`, `SubtitleQueue`), `GameScreens.hpp` (`DialogScreen`, `DocumentScreen`,
`ChoiceScreen`, `LockpickScreen`); Engine `runtime/EngineUi.cpp` (`buildHudModel`, `syncGameScreens`,
`bindUiFunctions`).

```cpp
namespace g7::ui {
struct BarValue { f32 value, max; f32 fraction() const; };
struct HudModel { bool visible; BarValue health; std::optional<BarValue> mana;   // Mana nur mit Magie (F3)
                  std::optional<Enemy> enemy; std::optional<f32> air; std::optional<Focus> focus; };
class MessageQueue { void push(std::string text, f32 seconds = 3); void update(f32 dt); };      // F7
class SubtitleQueue { void push(std::string speaker, std::string text, f32 seconds); void update(f32 dt); };
f32 readingSeconds(std::string_view text);                     // Zeilen ohne Take: 1,5 s + 0,06 s je Zeichen, 2-10 s
class Hud { void update(const HudModel&, const MessageQueue&, std::span<const SubtitleQueue::Line>, Rect, f32, const Layout*);
            void draw(Canvas&, const UiResources&) const; };
struct DialogView { bool menu; std::vector<std::string> options; usize selected; bool hidden; };
class DialogScreen { struct Actions { view, skip, choose, select }; };          // blockiert, verbirgt das HUD
class DocumentScreen { DocumentScreen(title, text, onClose); };                 // Bestätigen oder Zurück schließt
class ChoiceScreen { struct Actions { view, choose, leave }; };                 // Amboss, Bett
class LockpickScreen { struct Actions { view, turn(bool left), leave }; };      // links/rechts drehen
}
```
- **HUD (F3):** Leben unten links, Mana unten rechts nur, wenn der Held Magie hat (Magiekreis ≥ 1 oder eine Rune
  bzw. Spruchrolle auf einem Runenplatz), Gegnerleben oben mittig mit Namen darunter (der im Kampf erfasste Gegner,
  K5), Luft über dem Lebensbalken beim Tauchen; keine Uhr, kein Kompass. Balken mit figurens Füllungen `bar_hp`,
  `bar_mana`, `bar_enemy`, `bar_air` und dem Rand `bar`. Der Fokusname steht über dem Ziel (ersetzt
  `DebugUi::focusLabel`), nicht bei offenem Inventar, im Dialog oder unter einem blockierenden Bildschirm.
  Bildschirme mit `hidesHud` (der Dialog) blenden die Balken aus.
- **Meldungen (F7):** oben mittig, je 3 s (die letzte halbe Sekunde verblassend), höchstens vier, die älteste oben.
  Lua `screen_message(text, seconds?)`; die bisherigen Hinweise der Engine (`notice`: „Verschlossen.“, „Der Dietrich
  ist abgebrochen.“ …) erscheinen ebenfalls hier. Ein Lade-Ruckler zählt höchstens 0,1 s.
- **Untertitel (F6):** unten mittig auf dem Hintergrund `subtitle`, der Sprechername (Rolle `subtitle_name`) über dem
  Text (Rolle `subtitle`, ohne Kontur mit dezentem Schatten – Rückmeldung des Koordinators zu Teil A). Quellen: die
  gesprochene Dialogzeile (solange sie läuft; Überspringen nimmt sie sofort weg) und Zurufe von NPCs im Umkreis von
  15 m (`npc_shout`, `npc_say`; Dauer = Take oder Lesezeit). Höchstens zwei Zeilen, die neueste unten; ein neuer
  Satz desselben Sprechers ersetzt seinen alten. `[ui] subtitles = true` (Optionsmenü in Teil F).
- **Dialog** (ersetzt `DialogPanel`, E3 wie Gothic 1): Während eine Zeile gesprochen wird, überspringt Bestätigen sie
  (E4). Danach stehen die Antworten auf Pergament unten mittig (`dialog.box`, so hoch wie die Antworten, höchstens
  sechs Zeilen sichtbar), Auswahl mit Hoch/Runter, Stick oder Maus, Bestätigen wählt. Esc verlässt kein Gespräch.
  Beim Handeln ist der Dialog unsichtbar (Handel bis Teil D im ImGui-Fenster).
- **Dokument:** Titel und Text auf Pergament (`document.*`), Bestätigen oder Zurück schließt. Zweiseitige Bücher und
  Scrollen langer Texte folgen, wenn es solche Schriftstücke gibt.
- **Auswahl an einem Mob** (Amboss, Bett): Liste der Möglichkeiten, darunter die Meldung (Schläge, Ergebnis), Zurück
  hört auf. **Dietrich:** die Schritte der Kombination als Zellen (geschaffte hervorgehoben), Dietriche übrig, das
  letzte Ergebnis; Links/Rechts drehen, Zurück hört auf.
- **Engine:** `syncGameScreens()` öffnet und schließt die Bildschirme jeden Frame nach dem Zustand (Dialog, Dokument,
  Mob-Phase); sie lesen ihre Daten über Rückrufe und handeln über die bestehenden Engine-Funktionen (`dialogSkip`,
  `dialogChoose`, `chooseMobOption`, `mobCommand`). Ohne geladene Spiel-UI bleiben die ImGui-Fenster von M8–M10 als
  Rückfallebene; Inventar, Truhe, Handel und Tagebuch bleiben bis Teil D/E ImGui.
- **Stil, Layout, Texte:** neue Textrollen `subtitle_name`, `focus`, `dialog`, `document_title`, `document_hint`;
  Farben `ink_highlight`, `air`; Kästen `hud.air`, `dialog.*`, `document.*`, `choice.*`, `lockpick.*`; Texte
  `UI_DOCUMENT_CLOSE`, `UI_CHOICE_LEAVE`, `UI_LOCKPICK_PICKS`, `UI_LOCKPICK_HINT`, `UI_ANVIL_STRIKES` (DE/EN).
  Die Canvas zeichnet Text auf Wunsch mit Schlagschatten (`shadow`, `shadow_color` je Textrolle).
- **Tests:** `tests/ui/test_game_screens.cpp` (Meldungen, Untertitel, HUD-Lage und -Inhalt, Dialog, Dokument,
  Auswahl, Dietrich mit Rückrufen), `tests/runtime/test_engine_ui.cpp` (`screen_message`; im Testlager Dietrich per
  Pfeiltasten und Esc, Brief per Enter schließen, Dialog verbirgt das HUD und zeigt die Antworten).

## Spiel-UI Teil D (umgesetzt, M14) – Inventar mit 3D-Zellen, Handel, Truhe, Plündern
`ItemCell.hpp` (`InventoryViews`, `computeCell`), `ItemPreview.hpp` (`ItemPreviews`, Shader `item_preview.frag`),
`ItemScreen.hpp` (`ItemScreen` für Inventar, Handel, Behälter); `render::Camera` mit Bildfenster; Engine
`runtime/EngineUiItems.cpp`; `core::Config::child` (Tabellen mit Punkten im Namen).

```cpp
namespace g7::ui {
class InventoryViews { static Result<InventoryViews> load(toml, source);            // inventory_views.toml v2
                       InventoryView resolve(std::string_view mesh, std::string_view category) const; f32 fovDegrees(); };
struct CellFrame { Mat3 rotation; Vec3 centre; f32 distance, fovDegrees; Vec2 window; f32 halfSize;
                   Mat4 modelMatrix(f32 spinRadians = 0) const; };
Result<CellFrame> computeCell(std::span<const Vec3> lod0Positions, const InventoryView&, f32 fovDegrees);
class ItemPreviews { static Result<ItemPreviews> create(Device&, ShaderLibrary&, u32 tilePixels, u32 columns);
                     void render(Device&, MeshRenderer&, u32 tile, const Mesh&, const MaterialSet&, const CellFrame&,
                                 f32 spinRadians, const PreviewLight&);
                     Rect uv(u32 tile) const; const rhi::Texture& texture() const; };
enum class ItemScreenMode { Inventory, Trade, Container };
class ItemScreen { struct Actions { view, use, drop, transfer(fromOther, entry, count), maxTransfer, close,
                                    preview(item, large, spinning) }; };
std::vector<ItemTab> itemTabs(const Layout&);                                       // layout.toml [inventory] tabs
}
```
- **Vertrag `inventory_views.toml` v2:** engine hat zugestimmt und setzt ihn um. Die Zellen-Geometrie (Schritte 1–4)
  rechnet `computeCell` aus den LOD-0-Vertices des geladenen Modells (einmal je Item, im Speicher); Tests vergleichen
  mit Werten aus figurens `inventory.py`. Modelle ohne CPU-Daten (Platzhalter) nehmen die Ecken ihrer Bounding-Box.
- **Bilder der Zellen (Render-to-Texture):** Ein Atlas mit 8 × 8 Kacheln zu 128 Pixeln für die Zellen und eine
  Kachel zu 384 Pixeln für das große Bild im Infokasten. Jede Kachel wird in doppelter Größe in ein HDR-Ziel
  gezeichnet (feste Studio-Beleuchtung aus `style.toml [item_preview]`, keine Schatten, kein Nebel, Kamera mit
  Bildfenster `render::Camera::windowCenter/windowHalfSize`) und mit ACES und sRGB in die Kachel aufgelöst; Alpha =
  Deckung, der Zellrahmen scheint durch. Eine Kachel wird einmal gezeichnet, das gewählte Item jeden Frame gedreht
  (um die Bild-Y-Achse, eine Umdrehung in `spin_seconds`, Bildfenster der Startlage), danach wieder aufrecht.
  Die Canvas zeichnet solche Bilder als Anzeigewerte (`Canvas::image(..., displayValues = true)`).
- **Inventar (F4):** rechts eine Leder-Tafel (`leather`) mit Titel, Kategorie-Reitern wie Gothic 1 (`tab`,
  `tab_active`; Reiter und Kategorien in `layout.toml [inventory] tabs`), Raster (Spalten, Zellgröße, Abstand in
  `[inventory]`), Zellen `cell`, `cell_selected`, `cell_equipped` mit Stapelzahl, darunter der Infokasten (`tooltip`)
  mit dem gewählten Item groß und drehend, Name, Schaden, Schutz, Voraussetzungen, Wert, Anzahl. Bestätigen benutzt
  bzw. legt an oder ab, `ui_drop` (Entf, Nord) legt eines auf den Boden, Reiter-Tasten wechseln den Reiter (auch per
  Klick). Die Welt läuft weiter (`pausesGame = false`), die Spielsteuerung ist gesperrt; die Inventar-Taste (Tab)
  schließt es wieder. Das Inventar öffnet auf dem ersten Reiter mit Inhalt.
- **Handel (F5):** links die Waren des Händlers, rechts die des Helden, Gulden beider Seiten unten, der Preis je Stück
  im Infokasten. Bei Stapeln fragt eine Mengenwahl (Links/Rechts ±1, Reiter-Tasten ±10, Rad ±1, Bestätigen, Zurück
  bricht ab) mit Gesamtpreis; das Höchste ist durch das Geld begrenzt.
- **Truhe und Plündern:** links der Behälter bzw. der liegende NPC (Name, „tot“/„bewusstlos“), rechts der Held;
  Bestätigen nimmt bzw. legt hinein, bei Stapeln mit Mengenwahl (bei Behältern vorgewählt: alles). Zurück verlässt
  die Truhe (sie schließt sich) bzw. das Plündern.
- **Engine:** `syncItemScreens()` öffnet die Bildschirme nach dem Zustand (`m_inventoryOpen`, offene Truhe, Plündern,
  Handel) und handelt über die bestehenden Funktionen (`equip`, `useItem`, `dropItem`, `tradeBuy/Sell`,
  `takeFromMob`, `putIntoMob`, `loot`). Die ImGui-Fenster Inventar und Handel bleiben nur als Rückfallebene ohne
  Spiel-UI. Neue Lua-Funktionen für Konsole und Tests: `inventory_open(open?)`, `use_mob(vob)`.
- **Vorschläge (rückbaubar, Projektinhaber abwesend):** Handel und Behälter haben zusätzlich einen ersten Reiter
  „Alles“; das Inventar öffnet auf dem ersten Reiter mit Inhalt; die Mengenwahl beginnt beim Handel bei 1, bei
  Behältern bei allem; Ablegen legt ein Stück ab (Taste `ui_drop` = Entf, Gamepad Nord); das HUD bleibt sichtbar.
- **Tests:** `tests/ui/test_item_cell.cpp` (Geometrie gegen `inventory.py`, Drehen, `inventory_views.toml`),
  `test_item_screen.cpp` (Reiter decken jede Kategorie einmal ab, Benutzen, Ablegen, Reiter, Mengenwahl, Behälter),
  GPU `test_item_preview.cpp` (Deckung in der Kachel mittig und `fill`), `tests/runtime/test_engine_ui.cpp` (Inventar
  per Tab schließen, Truhe per Enter ausräumen).

## Spiel-UI Teil F (umgesetzt, M14) – Hauptmenü, Pause, Optionen, Ladebildschirm
`MenuScreens.hpp` (`MenuScreen`, `OptionsScreen`, `LoadingScreen`, `stepOption`, `optionText`, `assignKey`),
`runtime/MenuFlight.hpp`; Engine `runtime/EngineMenu.cpp`; `core::Config::erase`, `sameValue`, `copyValue`.
- **Start:** Ohne `--world`, `--scene` und `--view-mesh` (und ohne `--editor`, `--benchmark`, `--ui-sample`,
  `--no-menu`) startet das Spiel ins Hauptmenü (`EngineConfig::mainMenu`, `[menu] enabled`). Erst steht der
  Ladebildschirm (mindestens ein Bild), dann lädt die Menü-Welt `[menu] world` (Leonberg) ohne Spielfigur über den
  Weltwechsel. Dahinter fährt die Kamera `ui/menu_flight.toml` ab (Catmull-Rom durch Positionen und Blickziele,
  `speed` m/s, `loop`), die Simulation steht, die Musik ist auf das Thema `[menu] music` (`MENU`, Platzhalter
  `music/menu_std.wav` aus `gothar-audio music`) festgelegt.
- **Hauptmenü:** Neues Spiel, Spiel laden (ausgegraut bis M15), Optionen, Spiel beenden; Titel „Gothar“ darüber.
  Zurück tut hier nichts. **Neues Spiel** lädt `[game] world` an `[game] start` mit Spielfigur hinter dem
  Ladebildschirm (`[game] title`); die Menü-Welt wird danach nicht aufbewahrt.
- **Pausenmenü:** `pause` (Esc/Start) öffnet es im Spiel (pausiert, verbirgt das HUD): Weiterspielen, Neues Spiel,
  Laden, Speichern (diese drei ausgegraut bis M15), Optionen, Spiel beenden. Zurück spielt weiter. Ohne Spiel-UI
  schaltet `pause` nur die Pause um (Spiel-UI nicht geladen: Fehler im Log).
- **Optionen:** Reiter Grafik (Auflösung, Vollbild, VSync, Bildratenlimit, Sichtweite), Klang (Gesamt, Musik,
  Effekte, Stimmen, Umgebung, Menüklänge als Schieberegler mit den Rahmen `slider_track`/`slider_knob` in
  5-%-Schritten; wirken sofort auf die Busse), Steuerung (Schema
  klassisch/modern, Mausempfindlichkeit `[input] mouse_sensitivity` als Faktor, Tasten der Spielaktionen), Spiel
  (Sprache, Untertitel, Anzeigegröße). Hoch/Runter wählt, Links/Rechts (oder Bestätigen) ändert, Q/E bzw.
  Schultertasten wechseln den Reiter, Zurück verlässt und speichert. **Taste belegen:** Bestätigen, dann die neue
  Taste drücken (Esc bricht ab); sie wird die erste Taste der Aktion, die bisherige erste fällt weg, eine zweite
  Taste, Maus- und Gamepad-Belegungen bleiben. Hatte eine andere Spielaktion die Taste, verliert sie sie und eine
  Meldung nennt sie (Menü- und Entwicklertasten zählen nicht). Gamepad-Tasten sind vorerst nicht belegbar.
- **Speichern:** In `%APPDATA%\Gothar\Gothar\config.toml` (`EngineConfig::userConfig`) wird jeder geänderte Schlüssel
  geschrieben, wenn er vom Standard (`EngineConfig::defaultSettings`, engine.toml) abweicht, sonst entfernt; andere
  Schlüssel der Datei bleiben, das Schreiben ist atomar.
- **Ladebildschirm:** Lederhintergrund, „<Welt> wird geladen …“ und ein Balken (0,1 vor dem Laden, danach die noch
  offenen Ladeaufträge des `AssetManager`, höchstens 8 s Warten).
- **Lua:** `menu_open(page?, tab?)` öffnet das Pausenmenü, mit `"options"` die Optionen darüber, auf Reiter `tab`
  (ab 1; Konsole, Prüfbilder).
- **Tests:** `tests/ui/test_menu_screens.cpp` (Schritte, Texte, Tastenbelegung mit Konflikt, Menü mit ausgegrauten
  Einträgen, Optionen mit Reitern und Tastenfang, Ladebildschirm), `tests/runtime/test_menu_flight.cpp`,
  `tests/core/test_config.cpp` (erase/sameValue/copyValue), `tests/runtime/test_engine_ui.cpp` (Hauptmenü mit dem
  Lager als Menü-Welt, Optionen ändern und speichern, Neues Spiel, Pausenmenü hält die Simulation an).

## Spiel-UI Teil E (umgesetzt, M14) – Charakter, Tagebuch, Karte
`JournalScreens.hpp` (`StatusScreen`, `DiaryScreen`, `MapScreen`, Widget `TextView`); Engine
`runtime/EngineUiJournal.cpp`; Lua-Feld `map` an Items.

- **Charakter** (Taste `status`, B): Name und Gilde, vier Blöcke – Allgemein (Stufe, Erfahrung / nächste Stufe,
  Lernpunkte), Attribute (Leben, Mana, Stärke, Geschick), Talente (alle aus `kTalents`, „–“ = nicht gelernt), Schutz
  (Klinge, Wucht, Pfeile, Feuer, Magie). Kästen `status.*`, Überschriften in der neuen Textrolle `heading`.
- **Tagebuch** (Taste `log`, N; ersetzt `DiaryPanel`): Pergament mit dem Kapitel, Reitern Laufende Aufträge /
  Erledigt / Gescheitert / Allgemeines (Reiter-Tasten oder Klick), links die Aufträge bzw. Themen, rechts ihre
  Einträge als scrollender Text (`TextView`: Hoch/Runter, Rad). Daten aus `Story` (`diaryData`, seit Teil G).
- **Karte** (Taste `map`, M; F9): nur mit einem Karten-Item – ein Item der Kategorie `document` mit dem Feld
  `map = "ui/maps/<name>.png"`; Benutzen oder M zeigt sein Bild auf Pergament, eingepasst, ohne Spielerposition.
  Ohne Karte meldet M „Ich habe keine Karte.“. Stadtplan: `it_map_leonberg` (50 Gulden, verkauft Agnes vom
  Krämerladen) zeigt welts `ui/maps/leonberg.png` (2048 × 1446, Ausschnitt in `ui/maps/leonberg.toml`); dazu
  `ui/maps/placeholder.png` (eigene Arbeit, nur für Tests und Konsole: `map_open(image, title?)`).
- Die drei Tasten schließen ihren Bildschirm wieder, Zurück ebenso. Lua für Konsole und Tests: `status_open(open?)`,
  `map_open(image, title?)`, `diary_open(open?)` (seit M10).
- **Nacharbeit Teil D:** Reiter-Titel in der neuen Textrolle `tab` (16 statt 19): acht Reiter passen ungekürzt.
- **Tests:** `tests/ui/test_journal_screens.cpp` (Charakter, Tagebuch-Abschnitte und -Einträge, Scrollen, Karte
  eingepasst), `tests/runtime/test_engine_ui.cpp` (B, N, M öffnen und schließen; ohne Karten-Item eine Meldung).

## Plan M14 (freigegeben, 2026-11-01)
Grundlage: ADR 0009 (angenommen: eigenes Retained-Mode-UI, RmlUi verworfen, ImGui nur Debug/Editor), ADR 0020
(angenommen: MSDF-Atlas mit msdfgen im Cooker) und die Festlegungen des Projektinhabers vom 2026-10-31
(Inventar mit 3D-Vorschau je Zelle nach `inventory_views.toml` v2, keine PNG-Symbole; Lautstärken im Optionsmenü)
und vom 2026-11-01 (Gestaltung, siehe „Festlegungen des Projektinhabers“ am Ende; im Text als F1–F12 zitiert).
Zahlenwerte in den Beispielen (Farben, Größen, Zellgröße) sind Startwerte von engine und bleiben in den Daten
einstellbar.

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
  | `assets/source/ui/frames/*.png` | 9-Slice-Rahmen, Balken, Hintergründe – dunkel wie Gothic (Holz, Leder, Pergament; F2): erst Platzhalter von engine, später eigene Vorlagen | engine, später Projektinhaber |
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
  tabs = [                    # Reiter wie Gothic 1 (F4); Gruppierung als Startwert
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
  fehlende Glyphe → Ersatzzeichen und einmal Warnung. ADR 0020 ist angenommen (F12); `msdfgen` kommt in
  `vcpkg.json` und `docs/05-build.md`, gelinkt nur an `g7-cook`.
- **Schrift (F1, festgelegt):** **Alegreya** (Fließtext) und **Alegreya SC**
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
  die Tastenhinweise (vorerst als Text wie „[Strg] Benutzen“ bzw. „(A) Benutzen“, F11).
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
  struct HudModel { f32 hp, hpMax; std::optional<Vec2> mana;  // (mana, max), nur mit Magie (F3)
                    std::optional<f32> air;                // nur beim Tauchen
                    std::optional<EnemyBar> enemy;         // Name + Leben des Ziels im Kampf
                    std::optional<FocusLabel> focus; };    // Name + Bildschirmposition
  struct DialogModel { std::string speaker, line; std::vector<std::string> options; i32 selected; };
  struct SubtitleModel { std::string speaker, text; bool ambient; };   // ambient: Zurufe (SVM)
  ```
- **HUD (F3):** wie Gothic 1 – Leben unten links, Mana unten rechts **nur, wenn der Held Magie hat**
  (Magiekreis ≥ 1 oder eine Rune bzw. Spruchrolle; das Modell bekommt `std::optional` für Mana), Gegnerleben oben
  mittig; Luft beim Tauchen über dem Lebensbalken; **keine Uhr, kein Kompass**. Lage in `layout.toml`. Der
  Fokusname über dem Ziel ersetzt `DebugUi::focusLabel`.
- **Meldungen (F7):** **oben mittig, 3 s** je Meldung (`layout.toml`/`style.toml`), mehrere untereinander, die
  älteste zuerst ausgeblendet („Erfahrung +50“, „Stufe 3“, „Apfel erhalten“). Lua:
  `screen_message(text: string, seconds?: number)` (Vorgabe 3 s). Die Texte bleiben Inhalt in Lua (Ereignisse
  `level_up`, `item_taken` …), die Engine zeigt nur an.
- **Dialog:** ersetzt `DialogPanel`; Entscheidung E3 aus M10 bleibt (wie Gothic 1: Pfeiltasten oder Maus, die
  Aktion wählt). Die Auswahl steht als Liste unten, die gesprochene Zeile als Untertitel mit Sprechername.
  Dialogkamera, Gesten, Lippensync und das Absenken der Musik (M10/M13) bleiben unverändert; ein Untertitel steht so
  lange wie der Take, ohne Take wie heute nach Textlänge.
- **Untertitel (F6):** **standardmäßig an** (`[ui] subtitles = true`, Optionsmenü), **mit Sprechername**,
  **unten mittig**, **auch für Zurufe** (SVM) und Umgebungsgespräche in Hörweite; Text aus `lines.<sprache>.json`,
  sonst der Quelltext. Liegt die Dialogauswahl unten, steht der Untertitel darüber.
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
     `style.toml [item_preview] spin_seconds` (Startwert 6 s), Start in der Ansicht. Das **Bildfenster der
     Startlage bleibt** (Vorschlag figuren übernommen); was darüber hinausragt, schneidet die Zelle ab (Clipping).
     Der Abstand `d` bleibt; alle Vertices liegen auch gedreht vor der Kamera (Test).
  3. **Licht:** feste Studio-Beleuchtung aus `style.toml [item_preview]` (Sonne von links oben vorn, helles
     Hemisphären-Umgebungslicht, keine Schatten, kein Nebel, feste Belichtung) – wie im Prüfblatt.
  4. **Zellgröße:** `layout.toml [inventory] cell` (Startwert 104 UI-Einheiten, in 1080p 104 px). Dazu
     eine **große Vorschau im Infokasten** (F4): dieselbe Ansicht und Drehung in einer größeren Kachel
     (`layout.toml [inventory] preview`).
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
- **Inventar-Bildschirm** (wie Gothic 1, F4): Raster mit Kategorie-Reitern (`layout.toml`), Stapelzahl in der
  Zelle, angelegte Items markiert, darunter ein Infokasten mit dem **gewählten Item groß** (drehend) sowie Name,
  Werten (Schaden, Schutz, Wirkung), Wert in Gulden und Anzahl. Aktionen: Benutzen bzw. Anlegen (bestätigen),
  Ablegen auf den Boden (eigene Taste), Reiter wechseln. **Die Welt läuft weiter** (kein Pausieren, Flag
  `pausesGame = false`). **Handel (F5):** Händler links, Held rechts, Preis im Infokasten, Gulden beider Seiten;
  bei Stapeln eine **Mengenwahl** (links/rechts ±1, Reiter-Tasten ±10, bestätigen kauft bzw. verkauft die Menge,
  Gesamtpreis sichtbar, begrenzt durch Gulden und Vorrat). **Truhe** und
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
  Allgemeines, Einträge mit Umbruch und Scrollen (ersetzt `DiaryPanel`). **Karte** (`map`, M, F9): nur, wenn der
  Held ein Karten-Item besitzt; das Item nennt ein Bild (Lua-Feld `map = "ui/maps/<name>.png"`). Für Leonberg
  erzeugt welt einen Stadtplan aus den OSM-Daten (Auftrag über den Koordinator; Nennung
  „© OpenStreetMap-Mitwirkende“ in `assets/LICENSES.md`, steht auf dem Bild); **ohne Spielerposition**, wie Gothic 1.
  Umgesetzt: welt #328 (`gothar-worldgen citymap`), Item `it_map_leonberg` bei Agnes vom Krämerladen.
- **Tests:** Modell → Zeilen (Talente nach `kTalents`, Reiter des Tagebuchs, Scrollen langer Einträge);
  Runtime-Test öffnet beide Bildschirme im Testlager.
- **DoD:** Charakter und Tagebuch im Testlager vollständig bedienbar; die Karte, sobald ein Karten-Item existiert.

### Teil F – Hauptmenü, Pause, Optionen, Ladebildschirm
- **Hauptmenü (F8):** Neues Spiel, Fortsetzen (im Spiel), Laden und Speichern (bis M15 ausgegraut), Optionen,
  Beenden. Hintergrund: **langsame Kamerafahrt durch Leonberg** (Wegpunkte als Daten, `ui/menu_flight.toml`, Welt
  ohne Held, Simulation steht), dazu ein **eigenes Musikthema** (`data/music.toml`, Thema `menu`; Platzhalter über
  `gothar-audio music`, bis es eigene Musik gibt).
- **Optionen:** Grafik (Auflösung, Vollbild, VSync, Bildratenlimit, Sichtweite und Schatten, soweit einstellbar),
  **Audio** (Lautstärken `master`, `music`, `effects`, `voice`, `ambient`, `ui` aus `engine.toml [audio]`, wirken
  sofort), Steuerung (Schema classic/modern, Tasten neu belegen mit „Taste drücken“, Mausempfindlichkeit), Spiel
  (Sprache, Untertitel, UI-Skalierung). Gespeichert wird nur, was vom Standard abweicht, in die vorhandene
  Benutzer-Konfiguration (`%APPDATA%\Gothar\Gothar\config.toml`); dafür bekommt `core::Config` ein `set` und ein
  atomisches `save` (temporäre Datei + Umbenennen).
- **Ladebildschirm:** beim Start und beim Weltwechsel ein Bild mit Weltnamen und Fortschritt (offene Ladeaufträge
  des `AssetManager`); solange der Weltwechsel synchron lädt, wird vorher mindestens ein Frame gezeichnet.
- **Pause:** `pause` (Esc/Start) öffnet das Menü und pausiert die Simulation (`Engine::setPaused`), statt wie bis
  M13 direkt umzuschalten (F8).
- **Tests:** Optionen schreiben und lesen (nur Abweichungen, atomisch, fremde Schlüssel bleiben erhalten),
  Lautstärke-Regler ändert den Bus (Mixer headless), Tastenbelegung mit Konflikterkennung, Menü-Stapel (Esc aus
  den Optionen zurück ins Menü, aus dem Menü zurück ins Spiel).
- **DoD:** Start ins Hauptmenü, Neues Spiel, Optionen ändern (bleiben nach einem Neustart erhalten), Beenden –
  alles ohne Debug-UI und mit dem Gamepad bedienbar.

### Teil G – Lokalisierung DE/EN, Aufräumen, DoD der Phase (umgesetzt, siehe „Spiel-UI Teil G“ oben)
- **UI-Texte:** `text/ui.de.toml` und `text/ui.en.toml` vollständig; Test: beide Tabellen haben dieselben
  Schlüssel, und jeder im C++-Code verwendete Schlüssel existiert.
- **Inhaltstexte aus Lua (Vorschlag):** Namen und Beschreibungen von Items und NPCs, Info-Beschreibungen
  (`description`), Tagebuch-Einträge und Meldungen stehen deutsch inline in Lua (E2). `gothar-voice scan` sammelt
  sie zusätzlich in `assets/source/text/script.<sprache>.json` (Schlüssel = Instanz und Feld bzw. Info und Nummer,
  wie bei den Sprechtexten); die Engine sucht bei Sprache ≠ `de` die Übersetzung, sonst bleibt der deutsche Text
  (einmal Warnung). Gesprochene Zeilen: Untertitel aus `lines.en.json`, die Stimmen bleiben deutsch (F10). Die
  englischen Texte entwirft Claude, der Projektinhaber nimmt sie ab. Das erweitert den Vertrag „Sprechtexte“ (Werkzeug von engine) und wird vor der Umsetzung
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
- **ADR 0020 (angenommen, 2026-11-01):** `msdfgen` (nur Kern, MIT) im Cooker für den MSDF-Atlas; Eintrag in
  `vcpkg.json` (`{ "name": "msdfgen", "default-features": false }`) und in `docs/05-build.md` mit Teil A.
- Sonst keine neue Bibliothek: Layout, Text, Navigation und Atlas sind eigener Code; TOML über toml++, Bilder über
  stb/KTX wie bisher.
- Neue Assets: Alegreya und Alegreya SC (Lizenztext und `assets/LICENSES.md`), Platzhalter-Rahmen (eigene Arbeit),
  Platzhalter-Klänge über `tools/audio` (`ui_select`, `ui_accept`, `ui_back`, `ui_open`, `ui_close`).

### Festlegungen des Projektinhabers (2026-11-01, über den Koordinator)
1. **F1 Schrift:** Alegreya (Fließtext) und Alegreya SC (Überschriften), SIL OFL 1.1.
2. **F2 Rahmenstil:** dunkel wie Gothic (Holz, Leder, Pergament); zuerst Platzhalter von engine, später eigene
   Vorlagen des Projektinhabers.
3. **F3 HUD:** wie Gothic 1 – Leben unten links, Mana unten rechts (nur, wenn der Held Magie hat), Gegnerleben oben
   mittig; keine Uhr, kein Kompass.
4. **F4 Inventar:** Kategorie-Reiter wie Gothic 1 mit Raster; das gewählte Item groß im Infokasten; die Welt läuft
   bei offenem Inventar weiter.
5. **F5 Handel:** Mengenwahl bei Stapeln.
6. **F6 Untertitel:** standardmäßig an, mit Sprechername, unten mittig, auch für Zurufe.
7. **F7 Bildschirmmeldungen:** oben mittig, 3 s.
8. **F8 Hauptmenü:** langsame Kamerafahrt durch Leonberg mit eigener Musik; Esc öffnet im Spiel das Menü und
   pausiert.
9. **F9 Karte:** Stadtplan-Item, das welt aus OSM erzeugt (Auftrag an welt über den Koordinator), ohne
   Spielerposition.
10. **F10 Englisch:** englische Untertitel zu deutschen Stimmen sind in Ordnung; die Übersetzung entwirft Claude,
    der Projektinhaber nimmt sie ab.
11. **F11 Gamepad-Hinweise:** vorerst als Text.
12. **F12 ADR 0020:** angenommen (msdfgen, nur im Cooker).

## Bildschirme
HUD (Leben, Mana, Gegnerleben, Fokusname, Luft), Dialog-Auswahl + Untertitel, Inventar, Handel,
Truhe, Charakter, Tagebuch, Dokument (Briefe/Bücher), Karte, Hauptmenü, Optionen, Laden/Speichern,
Ladebildschirm, Konsole (Debug), Bildschirmmeldungen.

## Lokalisierung
UI-Texte über Schlüssel (`UI_INVENTORY_TITLE`) aus `text/ui.<sprache>.toml` – fehlender Schlüssel → Schlüssel
sichtbar + Warnung im Log; Inhaltstexte deutsch inline in Lua, Übersetzungen in `text/script.<sprache>.json` nach
dem deutschen Text (fehlt sie, bleibt der deutsche Text). Zahlenformat je Sprache (`formatNumber`). Einzelheiten:
„Spiel-UI Teil G“.

## Inventar-Ansichten (Vorschlag figuren, M14-Vorarbeit)
Festgelegt vom Projektinhaber: Inventar wie Gothic, **jede Zelle zeigt das 3D-Modell**, zentriert auf seine
Kontur, Standardansicht je Kategorie, keine PNG-Symbole. figuren liefert die Daten, engine baut die
Darstellung. Status: **vereinbart** (engine stimmt v2 zu, umgesetzt in M14 Teil D) – Vertrag figuren → engine.

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
