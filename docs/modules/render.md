# render

**Zweck:** Darstellung. OpenGL 4.5+ Core (ADR 0003; 4.6 bevorzugt) hinter einer dünnen RHI-Schicht, damit
später ein Vulkan-Backend möglich bleibt.

## Bestand (M2)

### `Device.hpp` – Wurzel der RHI
```cpp
namespace g7::render {
using GlProc = void (*)(); using GlLoader = GlProc (*)(const char*);   // = platform::GlContext::procAddress
struct DeviceInfo { std::string vendor, renderer, version; i32 major, minor; };
class Device {
public:
    static Result<std::unique_ptr<Device>> create(GlLoader, bool debugOutput);  // glad laden, >= 4.5 prüfen
    const DeviceInfo& info() const;
    void beginFrame(u32 width, u32 height, const Vec4& clearColor);  // Default-FB, Viewport, Clear
    std::vector<u8> readPixels(i32 x, i32 y, i32 w, i32 h) const;   // RGBA8, untere Zeile zuerst
    u32 debugMessageCount(DebugSeverity) const; u32 debugErrorCount() const;
};
}
```
- Setzt einen aktuellen GL-Kontext voraus (`platform::GlContext`), der das Device überlebt.
- **Debug-Output** (Debug-Build, synchron): Fehler und hoher Schweregrad → `ERROR`, sonstige mittlere/niedrige
  Meldungen → `WARN`, Performance-Hinweise und Notifications → `DEBUG`. Dieselbe Meldungs-ID wird höchstens
  5-mal geloggt (`DebugOutput.hpp`: `logLevelFor`, `DebugMessageFilter`, ohne GPU testbar).
- Ohne glad (Preset `nodeps`) baut das Modul ohne GL-Backend; `Device::create` meldet dann einen Fehler.

### Engine-Anbindung
Mit Fenster und `EngineConfig::render` (Standard an) erzeugt die Engine `GlContext` → `Device`, setzt VSync
aus `[window] vsync` und leert jeden Frame in einer Abendfarbe, dann Puffertausch. `--no-render` startet
ein Fenster ohne OpenGL. Tests mit echter GPU: Suite `render_gpu` (CTest-Label `gpu`).

## Schichten
1. **RHI** (`render/rhi/`): `Buffer`, `Texture`, `Sampler`, `ShaderProgram`, `PipelineState`,
   `Framebuffer`, `CommandList` (zunächst direkt ausgeführt). RAII-Wrapper um GL-Objekte, DSA-Stil.
2. **Renderer**: `Mesh`, `Material`, `Camera`, `Light`, `RenderScene` (Liste sichtbarer Objekte, von `world` befüllt), Passes.
3. **Features**: Himmel, Schatten, Nebel, Partikel (M12), Wasser/Post (M17), Debug-Draw, ImGui-Backend.

## Frame-Ablauf (Ziel)
```
cull (frustum + distance) → shadow pass (CSM, 3–4 Kaskaden) → depth prepass
→ opaque (forward+, clustered point lights) → alpha-tested (vegetation) → sky
→ transparent (particles, water) → post (fog, tonemap, bloom) → UI
```

## Stil-Ziel
Gothic lebt von Stimmung, nicht Realismus: starker **Distanznebel** passend zur Himmelsfarbe,
warme Punktlichter (Fackeln, Feuer) mit Flackern, dunkle Nächte, farbige Tageszeiten.
Wichtig ist ein **zeitabhängiges Farbschema** (Himmel, Nebel, Ambient, Sonne) als Daten-Kurve.

## Geplante API (Ausschnitt)
```cpp
namespace g7::render {
class Renderer {
public:
    Result<void> init(platform::Window&, const RenderConfig&);
    void beginFrame(const Camera&, const Environment&);   // Environment: sun dir/color, ambient, fog, sky colors
    void submit(const MeshDrawItem&);                     // mesh, material, world matrix, skinning palette (opt.)
    void submitLight(const PointLight&);
    void endFrame();                                      // executes passes, presents
    DebugDraw& debug();
};
}
```

## Offene Fragen
- Forward+ vs. Clustered Forward: beginnen mit einfachem Forward + Light-Limit pro Objekt, umstellen wenn > 32 Lichter sichtbar.
- Bindless-Texturen (`GL_ARB_bindless_texture`) optional.
