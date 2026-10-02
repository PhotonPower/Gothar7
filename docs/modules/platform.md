# platform

**Zweck:** Betriebssystem-Abstraktion: Fenster, Eingabe, OpenGL-Kontext-Erzeugung, Zeitgeber.
Bibliothek: **SDL3** (privat).

## Geplante API
```cpp
namespace g7::platform {
struct WindowDesc { std::string title; u32 width = 1600, height = 900; bool fullscreen = false; bool vsync = true; };
class Window {             // owns SDL window + GL context
public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc&);
    void swapBuffers();
    Extent size() const; void setRelativeMouse(bool);
};
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
bool pollEvents(Input&);                  // false -> quit requested
}
```

## Gothic-Bezug
Gothic 1 nutzt eine Aktionstaste (Strg) in Kombination mit Richtungstasten (Angriff, Aufheben,
Benutzen). Wir unterstützen **zwei Steuerungsschemata**: „Klassisch“ (Gothic-artig) und
„Modern“ (Maus-Angriff, E zum Benutzen). Das Mapping liegt in der Konfiguration, die
Spiel-Logik fragt nur Aktionen ab – niemals Tasten.
