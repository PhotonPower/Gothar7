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
                    WindowMode mode = WindowMode::Windowed; bool resizable = true; };

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
    bool setRelativeMouse(bool); bool relativeMouse() const;  // Kamera; false, wenn nicht verfügbar
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
    bool gamepadConnected() const; f32 axis(GamepadAxis) const;
    void setStickDeadzone(f32); f32 stickDeadzone() const;
    // Einspeisen (Window::pollEvents, Tests):
    void onKey(Key, bool); void onMouseButton(MouseButton, bool); void onMouseMotion(Vec2 pos, Vec2 delta);
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

### `Paths.hpp`
- `userDataDirectory(org, app) -> Result<fs::Path>` (`SDL_GetPrefPath`, legt das Verzeichnis an;
  Windows `%APPDATA%\<org>\<app>`, Linux `$XDG_DATA_HOME/<org>/<app>`). `game/src/main.cpp`
  setzt damit `fs::BaseDirectories::userDir` (Rückfall: `<spielverzeichnis>/userdata`).

### Engine-Anbindung
`EngineConfig::headless` (kein Fenster; `--smoke-test`) und `EngineConfig::window` (`WindowDesc`).
`Engine::run` ruft pro Frame `input.beginFrame()` und `pollEvents(input)` auf und beendet sich, wenn es
`false` liefert; `engine.input()` gibt den Zustand frei. Mit `--verbose` werden gedrückte Tasten/Knöpfe
geloggt (bis das Aktions-Mapping existiert). Kommandozeile: `--fullscreen`, `--frames=N`.

### Tests ohne Bildschirm
CTest setzt für die Suiten `platform` und `runtime` `SDL_VIDEO_DRIVER=offscreen`; die CI führt
zusätzlich `gothar --frames=10` mit diesem Treiber aus. `tests/platform/test_input_sdl.cpp` linkt als
einzige Stelle außerhalb von `platform` SDL direkt: synthetische Ereignisse (`SDL_PushEvent`) und ein
virtuelles Gamepad (`SDL_AttachVirtualJoystick`). Der Offscreen-Treiber kennt keinen relativen Mausmodus;
der Test meldet das und prüft ihn nur mit echtem Treiber.

## Geplante API (Rest von M1)
```cpp
namespace g7::platform {
enum class Action : u16 { MoveForward, MoveBack, StrafeLeft, StrafeRight, TurnLeft, TurnRight,
    Run /*toggle*/, Sneak, Jump, Action /*Gothic: Aktionstaste*/, DrawWeapon, DrawMagic,
    Inventory, Log, Status, QuickSave, QuickLoad, Console, Pause, ... };
// Aktions-Mapping: Action → Liste von Key/MouseButton/GamepadButton-Namen aus [bindings] der Config;
// isDown/pressed/released(Action) über Input.
}
```

## Gothic-Bezug
Gothic 1 nutzt eine Aktionstaste (Strg) in Kombination mit Richtungstasten (Angriff, Aufheben,
Benutzen). Wir unterstützen **zwei Steuerungsschemata**: „Klassisch“ (Gothic-artig) und
„Modern“ (Maus-Angriff, E zum Benutzen). Das Mapping liegt in der Konfiguration, die
Spiel-Logik fragt nur Aktionen ab – niemals Tasten.
