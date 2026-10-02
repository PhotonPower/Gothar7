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

### RHI – `rhi/Types.hpp`, `rhi/Resources.hpp`, Erweiterungen von `Device`
Keine GL-Typen in öffentlichen Headern; die Abbildung auf GL liegt in `src/rhi/GlMapping.hpp`.
```cpp
namespace g7::render::rhi {
enum class Format { R8, RG8, RGBA8, RGBA8_SRGB, RGBA16F, R32F, Depth24Stencil8, Depth32F };
enum class VertexFormat { Float1..Float4, UNorm8x4 };  enum class IndexType { U16, U32 };
enum class Topology { Triangles, Lines };  enum class CullMode { None, Back, Front };
enum class CompareOp { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };
enum class BlendMode { Opaque, Alpha, Additive };  enum class Filter { Nearest, Linear };  enum class Wrap { Repeat, Clamp, Mirror };
u32 bytesPerPixel(Format); bool isDepthFormat(Format); bool hasStencil(Format); u32 vertexFormatSize(VertexFormat);
u32 indexSize(IndexType); u32 mipLevelCount(w, h); u32 mipSize(size, level);

class Buffer        { Result<void> update(offset, span); usize size(); BufferUsage usage(); };      // BufferDesc{size, Static|Dynamic, initialData}
class Texture       { Result<void> upload(level, span); void generateMipmaps(); const TextureDesc& desc(); };  // TextureDesc{w, h, format, mipLevels (0 = Kette)}
class Sampler       {};   // SamplerDesc{min/mag/mip-Filter, wrapU/V, maxAnisotropy, optional compare (Schatten)}
class ShaderProgram { void setUniform(name, i32|f32|Vec2|Vec3|Vec4|Mat4); };                      // ShaderDesc{vertex-, fragmentSource, debugName}
class Pipeline      {};   // PipelineDesc{program, attributes{location, format, offset}, vertexStride, topology, cull, depthTest/Write/Compare, blend}
class Framebuffer   { u32 width(), height(); };  // FramebufferDesc{colors, depth}; auch nur Tiefe (Schatten)
}
// Device:
Result<Buffer|Texture|Sampler|ShaderProgram|Pipeline|Framebuffer> create…(desc);
void bindFramebuffer(const Framebuffer*);  /* nullptr = Fenster */  void setViewport(x, y, w, h);
void clear(optional<Vec4> color, optional<f32> depth);
void bindPipeline(const Pipeline&);  void bindVertexBuffer(const Buffer&, offset);  void bindIndexBuffer(const Buffer&, IndexType);
void bindTexture(unit, const Texture&, const Sampler&);  void bindUniformBuffer(slot, const Buffer&);
void draw(count, first);  void drawIndexed(count, first);
std::vector<u8> readPixels(x, y, w, h, const Framebuffer* = nullptr);  std::vector<u8> readBuffer(const Buffer&, offset, size);
const FrameStats& stats();   // drawCalls, triangles, pipelineChanges, textureBinds – Reset in beginFrame
```
- **RAII, nur verschiebbar**, erzeugt über das `Device` (wie in Vulkan). Jedes GL-Objekt steckt in einem
  `rhi::Handle` mit prozessweit eindeutiger `uid`; der Zustands-Cache vergleicht uids, nie GL-Namen
  (gelöschte Namen werden vom Treiber wiederverwendet).
- **Unmittelbarer Kontext statt CommandList:** `Device` führt Binds/Draws direkt aus und überspringt
  redundante Zustandswechsel (Pipeline, Programm, Cull/Depth/Blend, Textur/Sampler pro Einheit).
  Eine aufzeichnende `CommandList` erst mit Multithreading (M17).
- **Fehler:** erwartbare Fehler als `Result` (Shader-Compiler-/Linker-Log mit Zeilen, unvollständiger
  Framebuffer, Größe 0, Update auf statischen Buffer, falsche Upload-Größe); Programmierfehler `G7_ASSERT`.
- **Konventionen:** Vorderseiten gegen den Uhrzeigersinn; ein Vertex-Buffer pro Pipeline (interleaved,
  Bindung 0); Uniform-Blöcke/Sampler über GLSL `layout(binding = N)`; anisotrope Filterung wird auf den
  Treiberwert begrenzt (`DeviceInfo::maxAnisotropy`, 1 = nicht verfügbar) und nur bei linearem Min-Filter
  gesetzt (bei Nearest wäre das Ergebnis treiberabhängig – Mesa filtert dann trotzdem).
- Shader-Compilerausgaben aus dem Debug-Callback laufen nur auf Debug-Level, weil
  `createShaderProgram` das vollständige Log als Fehler zurückgibt.

### Shader-System – `ShaderPreprocessor.hpp`, `ShaderLibrary.hpp`
```cpp
namespace g7::render {
using ShaderFileReader = std::function<Result<std::string>(std::string_view path)>;
struct PreprocessedShader { std::string source; std::vector<std::string> files; };   // files[i] = #line-Quelle i
Result<PreprocessedShader> preprocessShader(path, const ShaderFileReader&, std::span<const std::string> defines = {});
std::string mapShaderLog(std::string_view log, std::span<const std::string> files);   // "0:12(5)" -> "datei:12(5)"

struct ShaderKey { std::string vertexPath, fragmentPath; std::vector<std::string> defines; };
class ShaderLibrary {
public:
    ShaderLibrary(Device&, fs::Path shaderRoot);
    Result<rhi::ShaderProgram*> load(std::string_view name, const ShaderKey&);   // stabiler Zeiger
    rhi::ShaderProgram* find(std::string_view name);
    u32 reloadChanged();                                  // Zeitstempel prüfen, geänderte neu kompilieren
    void setHotReload(bool enabled, f64 pollIntervalSeconds = 0.5);  void update(f64 now);
};
}
```
- **Präprozessor** (ohne GL, mit In-Memory-Dateien testbar): `#include "pfad"` relativ zum Shader-Verzeichnis,
  jede Datei höchstens einmal, Zyklen sind Fehler; `#version` muss in Zeile 1 der Hauptdatei stehen,
  Defines (`"NAME"` / `"NAME WERT"`) werden direkt dahinter eingefügt. `#line Zeile Quelle` vor und nach jedem
  Include; `mapShaderLog` übersetzt Treiber-Logs (Mesa `0:12(5)`, Intel/AMD `0:12:`, NVIDIA `0(12)`) zurück auf
  `datei:zeile`.
- **Hot-Reload:** `ShaderLibrary` merkt sich alle beteiligten Dateien (inkl. Includes) mit Änderungszeit und
  prüft sie höchstens alle 0,5 s. Ein Programm behält über Reloads hinweg seine Adresse; **Pipelines verweisen
  auf ihr `ShaderProgram`** (statt dessen GL-Namen zu kopieren) und nutzen beim nächsten Bind die neue Version.
  Ein fehlerhafter Reload behält die alte Version und loggt den Fehler mit Datei und Zeile.
  Dateiüberwachung durch das Betriebssystem folgt mit dem Asset-System (M3).
- **Engine-Shader** liegen in `engine/render/shaders/` (`background.vert/.frag`, `common/color.glsl`) und werden
  beim Bauen nach `<exe>/shaders/` kopiert. `[render] shader_hot_reload` (Standard: an im Debug-Build) und
  `[render] shader_dir` (z. B. auf das Quellverzeichnis zeigen, damit Änderungen nicht beim nächsten Build
  überschrieben werden).

### Engine-Anbindung
Mit Fenster und `EngineConfig::render` (Standard an) erzeugt die Engine `GlContext` → `Device` →
`ShaderLibrary`, setzt VSync aus `[window] vsync` und zeichnet pro Frame einen **Abendverlauf** als Hintergrund
(Vollbild-Dreieck aus `gl_VertexID`, Platzhalter für den Himmel in M4), dann Puffertausch. `--no-render`
startet ein Fenster ohne OpenGL. Tests mit echter GPU: Suite `render_gpu` (CTest-Label `gpu`).

## Schichten
1. **RHI** (`render/rhi/`, siehe oben): `Buffer`, `Texture`, `Sampler`, `ShaderProgram`, `Pipeline`,
   `Framebuffer` (umgesetzt); `CommandList` erst mit Multithreading (M17). RAII-Wrapper um GL-Objekte, DSA-Stil.
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
