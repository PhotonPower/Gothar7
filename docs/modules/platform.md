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

class Window {   // PImpl um SDL_Window, hält eine Referenz auf das SDL-Video-Subsystem
public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc&);  // Größe 0 → Fehler
    bool pollEvents();                    // false = Beenden angefordert (bleibt false)
    Extent size() const;                  // Bildschirmkoordinaten
    Extent pixelSize() const;             // Framebuffer in Pixeln (HiDPI)
    bool resizedSinceLastPoll() const;    // Größe/Pixelgröße seit letztem pollEvents geändert
    void setSize(Extent);                 // ungültige Größen werden ignoriert (Warnung)
    void setMode(WindowMode); WindowMode mode() const;
    void setTitle(std::string_view); std::string title() const;
    void requestClose();                  // z. B. Menüpunkt „Beenden“
};
}
```
- Nur Hauptthread. Mehrere Fenster gleichzeitig sind möglich (Tests); das Video-Subsystem wird per
  Referenzzählung von SDL verwaltet.
- Vollbild ist **randlos in Desktop-Auflösung** (kein Moduswechsel, schnelles Alt+Tab).
  Größen- und Moduswechsel warten per `SDL_SyncWindow`, damit `size()` sofort stimmt.
- `resizedSinceLastPoll()` vergleicht mit dem Stand beim letzten Poll, deckt also Benutzer-Ziehen,
  `setSize` und Moduswechsel gleich ab.
- Der OpenGL-Kontext kommt in M2 (ADR 0003) hinzu.

### `Paths.hpp`
- `userDataDirectory(org, app) -> Result<fs::Path>` (`SDL_GetPrefPath`, legt das Verzeichnis an;
  Windows `%APPDATA%\<org>\<app>`, Linux `$XDG_DATA_HOME/<org>/<app>`). `game/src/main.cpp`
  setzt damit `fs::BaseDirectories::userDir` (Rückfall: `<spielverzeichnis>/userdata`).

### Engine-Anbindung
`EngineConfig::headless` (kein Fenster; `--smoke-test`) und `EngineConfig::window` (`WindowDesc`).
`Engine::run` ruft pro Frame `pollEvents()` auf und beendet sich, wenn es `false` liefert.
Kommandozeile: `--fullscreen`, `--frames=N`.

### Tests ohne Bildschirm
CTest setzt für die Suiten `platform` und `runtime` `SDL_VIDEO_DRIVER=offscreen`; die CI führt
zusätzlich `gothar --frames=10` mit diesem Treiber aus.

## Geplante API (Rest von M1)
```cpp
namespace g7::platform {
enum class Action : u16 { MoveForward, MoveBack, StrafeLeft, StrafeRight, TurnLeft, TurnRight,
    Run /*toggle*/, Sneak, Jump, Action /*Gothic: Aktionstaste*/, DrawWeapon, DrawMagic,
    Inventory, Log, Status, QuickSave, QuickLoad, Console, Pause, ... };
class Input {
public:
    void beginFrame();                     // edge detection
    bool isDown(Action) const; bool pressed(Action) const; bool released(Action) const;
    Vec2 mouseDelta() const; f32 axis(AxisAction) const;   // gamepad
    void loadBindings(const Config&);
};
// Window::pollEvents(Input&) füttert die Eingabe; Window::setRelativeMouse(bool) für die Kamera.
}
```

## Gothic-Bezug
Gothic 1 nutzt eine Aktionstaste (Strg) in Kombination mit Richtungstasten (Angriff, Aufheben,
Benutzen). Wir unterstützen **zwei Steuerungsschemata**: „Klassisch“ (Gothic-artig) und
„Modern“ (Maus-Angriff, E zum Benutzen). Das Mapping liegt in der Konfiguration, die
Spiel-Logik fragt nur Aktionen ab – niemals Tasten.
