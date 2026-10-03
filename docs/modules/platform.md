# platform

**Zweck:** Betriebssystem-Abstraktion: Fenster, Eingabe, OpenGL-Kontext-Erzeugung, Benutzerpfade.
Bibliothek: **SDL3** (privat, ADR 0011). Kein anderes Modul bindet SDL-Header ein.

## Bestand (M1)

### `Window.hpp`
```cpp
namespace g7::platform {
struct Extent { u32 width = 0, height = 0; };
enum class WindowMode : u8 { Windowed, Fullscreen /* randlos in Desktop-Auflösung */ };
struct WindowDesc { std::string title = "Gothar"; Extent size{1600, 900};
                    WindowMode mode = WindowMode::Windowed; bool resizable = true;
                    GraphicsApi graphics = GraphicsApi::None; bool vsync = true; };

class Window {   // PImpl um SDL_Window, hält Referenzen auf SDL-Video- und Gamepad-Subsystem
public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc&);  // Größe 0 → Fehler
    bool pollEvents(Input&);              // füttert Input; false = Beenden angefordert (bleibt false)
    bool pollEvents();                    // dasselbe, Eingaben werden verworfen (Tools, Tests)
    Extent size() const;                  // Bildschirmkoordinaten
    Extent pixelSize() const;             // Framebuffer in Pixeln (HiDPI)
    bool resizedSinceLastPoll() const;    // Größe/Pixelgröße seit letztem pollEvents geändert
    void setSize(Extent);                 // ungültige Größen werden ignoriert (Warnung)
    void setMode(WindowMode); WindowMode mode() const;
    void setTitle(std::string_view); std::string title() const;
    f32 displayScale() const;             // Inhaltsskalierung des Bildschirms (1,5 bei 150 %), für UI-Größen
    bool setRelativeMouse(bool); bool relativeMouse() const;  // Kamera; false, wenn nicht verfügbar
    void setTextInput(bool); bool textInput() const;  // Texteingabe (IME) an/aus, Standard aus
    void requestClose();                  // z. B. Menüpunkt „Beenden“
};
}
```
- Nur Hauptthread. Mehrere Fenster gleichzeitig sind möglich (Tests); die SDL-Subsysteme werden per
  Referenzzählung von SDL verwaltet.
- Vollbild ist **randlos in Desktop-Auflösung** (kein Moduswechsel, schnelles Alt+Tab).
  Größen- und Moduswechsel warten per `SDL_SyncWindow`, damit `size()` sofort stimmt.
- `resizedSinceLastPoll()` vergleicht mit dem Stand beim letzten Poll, deckt also Benutzer-Ziehen,
  `setSize` und Moduswechsel gleich ab.
- Fokusverlust (Alt+Tab) ruft `Input::releaseAll()` – keine „hängenden“ Tasten.
- Gamepad: das erste angeschlossene Pad wird geöffnet; wird es abgezogen, übernimmt ein weiteres.
- Der OpenGL-Kontext kommt in M2 (ADR 0003) hinzu.

### `Input.hpp`
```cpp
namespace g7::platform {
enum class Key : u16 { Unknown, A…Z, Num0…Num9, F1…F12, Escape, Enter, Space, Tab, Backspace, Insert,
    Delete, Home, End, PageUp, PageDown, Up, Down, Left, Right, LeftShift, RightShift, LeftCtrl,
    RightCtrl, LeftAlt, RightAlt, CapsLock, Grave, Minus, Equals, …, Keypad0…Keypad9, …, Count };
enum class MouseButton : u8 { Left, Right, Middle, X1, X2, Count };
enum class GamepadButton : u8 { South, East, West, North, LeftShoulder, RightShoulder, LeftStick,
    RightStick, Back, Start, DpadUp, DpadDown, DpadLeft, DpadRight, Count };
enum class GamepadAxis : u8 { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

class Input {   // SDL-frei, direkt testbar
public:
    void beginFrame();                                   // Flanken + Deltas zurücksetzen
    bool isDown(X) const; bool pressed(X) const; bool released(X) const;  // X = Key/MouseButton/GamepadButton
    Vec2 mousePosition() const; Vec2 mouseDelta() const; f32 wheelDelta() const;
    std::string_view text() const;                       // in diesem Frame getippter Text (UTF-8), nur bei setTextInput(true)
    bool gamepadConnected() const; f32 axis(GamepadAxis) const;
    void setStickDeadzone(f32); f32 stickDeadzone() const;
    // Einspeisen (Window::pollEvents, Tests):
    void onKey(Key, bool); void onMouseButton(MouseButton, bool); void onMouseMotion(Vec2 pos, Vec2 delta);
    void onText(std::string_view utf8);
    void onWheel(f32); void onGamepadButton(GamepadButton, bool); void onGamepadAxis(GamepadAxis, f32);
    void onGamepadConnected(bool); void releaseAll();
};
std::string_view name(Key / MouseButton / GamepadButton);
std::optional<Key> keyFromName(sv); std::optional<MouseButton> mouseButtonFromName(sv);
std::optional<GamepadButton> gamepadButtonFromName(sv);
}
```
- **Physische Tasten (Scancodes):** WASD bleibt auf AZERTY/QWERTZ an derselben Stelle; Namen nach US-Layout.
- `pressed()` ist auch wahr, wenn die Taste im selben Frame wieder losgelassen wurde (kurzes Antippen geht
  nicht verloren). Auto-Repeat-Ereignisse werden ignoriert.
- **Koordinaten:** Maus in Bildschirmraum (+Y unten); Sticks −1..1 mit **+Y oben** (nach vorn drücken = positiv);
  Trigger 0..1. Mausrad: + = vom Benutzer weg.
- **Totzone** radial pro Stick (Standard 0,2), danach umskaliert, damit die Bewegung sanft bei 0 beginnt;
  Diagonalen sind auf Länge 1 begrenzt. Abziehen des Pads setzt dessen Zustand zurück.
- **Namen** für die Konfiguration (Bindings): Tasten `"W"`, `"LeftCtrl"`, `"Grave"`, `"1"`, `"F5"`;
  Maus mit Präfix `"MouseLeft"`…; Gamepad `"PadSouth"`… – die Namensräume überschneiden sich nicht,
  Suche ohne Groß-/Kleinschreibung.

### `GlContext.hpp`
- `GraphicsApi::OpenGL` in `WindowDesc` erzeugt ein GL-fähiges Fenster (Default-Framebuffer: Doppelpuffer,
  Tiefe 24, Stencil 8).
- `GlContext::create(window, {major 4, minor 6, minMinor 5, debug})` – Core-Kontext, versucht 4.6 und fällt auf
  4.5 zurück; `swapBuffers()`, `setVSync(bool)` (adaptiv, sonst klassisch; loggt die Bildwiederholrate),
  `procAddress(name)` als Loader für `render`. Muss alle GL-Objekte überleben.

### `GpuPreference.hpp`
- `G7_REQUEST_HIGH_PERFORMANCE_GPU();` – exportiert unter Windows `NvOptimusEnablement` und
  `AmdPowerXpressRequestHighPerformance`, damit Laptops mit zwei GPUs die dedizierte GPU nehmen (sonst startet
  OpenGL auf der integrierten). Muss einmal global in einer Quelldatei **jeder Executable** stehen (nicht in einer
  Bibliothek): `game/src/main.cpp`, `tests/render_gpu/gpu_preference.cpp`. Umschalten auf die sparsame GPU ggf.
  später mit den Grafik-Optionen (M14).

### `Time.hpp`
- `nowSeconds()` – monotone Uhr (`SDL_GetTicksNS`), `sleepPrecise(seconds)` – genaues Warten
  (`SDL_DelayPrecise`; normale OS-Sleeps sind unter Windows 1–15 ms grob). Nutzt die Engine für das Frame-Limit.

### `Paths.hpp`
- `userDataDirectory(org, app) -> Result<fs::Path>` (`SDL_GetPrefPath`, legt das Verzeichnis an;
  Windows `%APPDATA%\<org>\<app>`, Linux `$XDG_DATA_HOME/<org>/<app>`). `game/src/main.cpp`
  setzt damit `fs::BaseDirectories::userDir` (Rückfall: `<spielverzeichnis>/userdata`).

### Engine-Anbindung
`EngineConfig::headless` (kein Fenster; `--smoke-test`) und `EngineConfig::window` (`WindowDesc`).
`Engine::run` ruft pro Frame `input.beginFrame()` und `pollEvents(input)` auf und beendet sich, wenn es
`false` liefert; `engine.input()` gibt den Zustand frei. Mit `--verbose` werden gedrückte **Aktionen**
geloggt (siehe Aktions-Mapping). Kommandozeile: `--fullscreen`, `--frames=N`.

### Tests ohne Bildschirm
CTest setzt für die Suiten `platform` und `runtime` `SDL_VIDEO_DRIVER=offscreen`; die CI führt
zusätzlich `gothar --frames=10` mit diesem Treiber aus. `tests/platform/test_input_sdl.cpp` linkt als
einzige Stelle außerhalb von `platform` SDL direkt: synthetische Ereignisse (`SDL_PushEvent`) und ein
virtuelles Gamepad (`SDL_AttachVirtualJoystick`). Der Offscreen-Treiber kennt keinen relativen Mausmodus;
der Test meldet das und prüft ihn nur mit echtem Treiber.

### `Actions.hpp` – Aktions-Mapping
```cpp
namespace g7::platform {
enum class Action : u16 { MoveForward, MoveBack, StrafeLeft, StrafeRight, TurnLeft, TurnRight,
    Walk /*gehalten: gehen statt rennen*/, Sneak, Jump, Action /*Gothic-Aktionstaste*/, Attack, Use, DrawWeapon, DrawMagic,
    Inventory, Log, Status, Map, QuickSave, QuickLoad, Console, Pause, DebugDraw /*Entwicklung, F2*/, DebugUi /*Entwicklung, F1*/, DebugFly /*Entwicklung, F3*/,
    FlyForward, FlyBack, FlyLeft, FlyRight, FlyUp, FlyDown, FlyFast /*freie Kamera*/, CopyPosition /*Entwicklung, F6*/, Count };
std::string_view name(Action);  std::optional<Action> actionFromName(sv);      // "move_forward" …
using InputBinding = std::variant<Key, MouseButton, GamepadButton>;
std::optional<InputBinding> bindingFromName(sv);  std::string_view name(const InputBinding&);

class ActionMap {
public:
    static ActionMap fromConfig(const Config&, std::string_view scheme);   // [bindings.<scheme>]
    void writeTo(Config&, std::string_view scheme) const;                 // Optionsmenü (M14)
    void bind(Action, InputBinding); void clear(Action);
    std::span<const InputBinding> bindings(Action) const;
    bool isDown(const Input&, Action) const;    // irgendeine gebundene Eingabe gehalten
    bool pressed(const Input&, Action) const;   // irgendeine in diesem Frame gedrückt
    bool released(const Input&, Action) const;  // eine losgelassen und keine mehr gehalten
};
}
```
- Spiel-Logik fragt **nur Aktionen** ab, nie Tasten. Mehrere Eingaben pro Aktion (Tastatur + Gamepad).
- `fromConfig` überspringt unbekannte Aktionen, unbekannte Eingabenamen und falsche Typen mit einer
  Warnung im Log – ein Tippfehler in der Config verhindert nie den Spielstart.
- **Zwei Schemata** in `game/config/engine.toml`: `classic` (Gothic-artig: Aktionstaste + Richtung,
  `attack`/`use` leer) und `modern` (Maus-Angriff, E zum Benutzen, `action`/`turn_*` leer).
  Auswahl über `[input] scheme`, Stick-Totzone über `[input] stick_deadzone`.
- Seit M5: `walk` (vorher `run`) wird gehalten, um zu **gehen**; Standard ist Rennen wie in Gothic
  (Entscheidung Projektinhaber). `debug_fly` (F3) schaltet den Flugmodus (freie Kamera) ein und aus.
- Freie Kamera mit eigenen Aktionen `fly_forward/back/left/right/up/down/fast`, in beiden Schemata gleich
  (W/A/S/D bzw. Pfeile, Leertaste/E, Strg/Q, Shift) – überlappen mit Spielaktionen, die im Flugmodus ruhen.
  `copy_position` (F6) kopiert die Ansicht als Startoptionen (`runtime/StartView.hpp`).
  Die Maus dreht die Figur (`gameplay.md`). Analoge Bewegung mit dem Stick folgt mit dem Gamepad-Feinschliff.

### `Clipboard.hpp` – Zwischenablage
`Result<void> setClipboardText(std::string_view)` (SDL, braucht das Video-Subsystem): `copy_position`.

### Konfiguration
`game/src/main.cpp` lädt `gamePath("config/engine.toml")` (wird beim Bauen neben die Executable kopiert)
und legt `userPath("config.toml")` per `Config::merge` darüber; Kommandozeilen-Schalter gewinnen.
`[window]` (`width`, `height`, `fullscreen`) setzt der Aufrufer in `EngineConfig::window`, die Engine
liest `[input]` und `[bindings.<scheme>]` aus `EngineConfig::settings`, stellt `engine.actions()` bereit
und loggt mit `--verbose` jede gedrückte Aktion (`action draw_weapon`).

## Gothic-Bezug
Gothic 1 nutzt eine Aktionstaste (Strg) in Kombination mit Richtungstasten (Angriff, Aufheben,
Benutzen). Wir unterstützen **zwei Steuerungsschemata**: „Klassisch“ (Gothic-artig) und
„Modern“ (Maus-Angriff, E zum Benutzen). Das Mapping liegt in der Konfiguration, die
Spiel-Logik fragt nur Aktionen ab – niemals Tasten.
