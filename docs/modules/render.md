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
void setScissor(optional<PixelRect>);       // Pixel, Ursprung unten links; gilt auch für clear; beginFrame schaltet ab
void clear(optional<Vec4> color, optional<f32> depth);
void bindPipeline(const Pipeline&);  void bindVertexBuffer(const Buffer&, offset);  void bindIndexBuffer(const Buffer&, IndexType);
void bindTexture(unit, const Texture&, const Sampler&);  void bindUniformBuffer(slot, const Buffer&);
void draw(count, first);  void drawIndexed(count, first, baseVertex = 0);   // baseVertex: mehrere Meshes, 16-Bit-Indizes
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

### Kamera – `Camera.hpp`
```cpp
struct Camera { Transform transform; f32 fovY = 70°, nearPlane = 0.1f, farPlane = 1500.0f, aspect;
                Mat4 view() const; Mat4 projection() const; Mat4 viewProjection() const; Frustum frustum() const; };
struct FreeFlyInput { Vec3 move; /* x rechts, y Welt-oben, z vorwärts */ f32 turn; Vec2 lookDelta; bool fast; };
class FreeFlyCamera { f32 speed, fastFactor, sensitivity, turnRate;
                      void attach(const Camera&); void update(Camera&, const FreeFlyInput&, f64 dt); };
```
- Blickrichtung −Z der Transform, +Y oben; Projektion **Reverse-Z** (ADR 0002): `Device` stellt
  `glClipControl(…, ZERO_TO_ONE)` ein, `beginFrame` löscht Tiefe mit 0, `PipelineDesc::depthCompare` ist
  standardmäßig `GreaterEqual`. Skalierung der Kamera-Transform wird ignoriert.
- `FreeFlyCamera` (Debug, bis zur Spielfigur in M5): fliegt in Blickrichtung, hoch/runter entlang Welt-Y,
  Diagonalen normiert, Pitch auf ±89° begrenzt, `attach` übernimmt die aktuelle Ausrichtung ohne Sprung.

### Statische Meshes – `Mesh.hpp`
```cpp
class Mesh { static Result<Mesh> create(Device&, const asset::MeshData&);
             static std::vector<rhi::VertexAttribute> vertexLayout();  static constexpr u32 kVertexStride = 48;
             void bind(Device&) const; void draw(Device&, usize submesh) const;
             std::span<const asset::Submesh> submeshes() const; const AABB& bounds() const; };
```
- Ein Vertex-Buffer (`asset::Vertex`, Attribute 0–3: Position, Normale, UV, Tangente), ein u32-Index-Buffer.
- Shader `mesh.vert/.frag` (siehe Materialien und Licht).

### Texturen – `TextureUpload.hpp`
```cpp
struct TextureUpload { bool srgb = true; bool mipmaps = true; };
Result<rhi::Texture> createTexture(Device&, const asset::ImageData&, TextureUpload = {});   // RGBA8(_SRGB), volle Mip-Kette
Result<rhi::Texture> createSolidTexture(Device&, u8 r, u8 g, u8 b, u8 a, bool srgb = true);  // 1x1, Ersatz
std::vector<u8> Device::readTexture(const rhi::Texture&, u32 level) const;                 // RGBA8, Tests/Debug
```
- Zeilen in Dateireihenfolge hochgeladen: UV (0,0) liest das Texel oben links – wie glTF, ohne Spiegeln.
- Farbtexturen sind sRGB (Hardware dekodiert beim Sampeln), Datentexturen (Normalen, Masken) linear.
- Mipmaps auf der GPU erzeugt; Materialsampler mit anisotroper Filterung aus `[render] anisotropy` (Standard 8).
- **Farbraum:** Alle Szenen-Shader rechnen und schreiben **linear** (HDR, Werte über 1 erlaubt) in das
  `SceneTarget`; erst der Post-Pass tonemappt und kodiert nach sRGB (siehe „Nebel, HDR und Tonemapping“).

### Materialien – `Material.hpp`
```cpp
struct Material { const rhi::Texture *baseColor, *normal, *emissive; Vec4 baseColorFactor; Vec3 emissiveFactor;
                  f32 normalScale, alphaCutoff; asset::AlphaMode alphaMode; bool doubleSided; };
class MaterialSet  { using ImageLookup = std::function<const asset::ImageData*(const asset::ImageSource&)>;
                     static Result<MaterialSet> create(Device&, const asset::MeshData&, const ImageLookup&);  // Engine: über das VFS
                     static Result<MaterialSet> create(Device&, const asset::MeshData&, const fs::Path& modelDir); // Werkzeuge/Tests
                     const Material& operator[](usize) const; usize size() const; };
class MeshRenderer { static Result<MeshRenderer> create(Device&, ShaderLibrary&, f32 anisotropy);
                     void draw(Device&, const Mesh&, const MaterialSet&, const Mat4& model, const Camera&); };
```
- **Bewusst schlicht/stilisiert** (kein PBR): Basisfarbe × Textur, Tangentenraum-Normal-Map mit `normalScale`,
  Emissive (nach dem Licht addiert), Alpha-Modi wie glTF.
- `MaterialSet` lädt jedes Bild einmal je Verwendung (Farbe sRGB, Normalen linear); fehlende/kaputte Bilder →
  neutrale 1×1-Ersatztexturen (weiß bzw. flache Normale) mit Warnung. Texturen bleiben beim Verschieben gültig.
- `MeshRenderer`: Pipelines für Opak / Alpha-Test / Blend × einseitig / beidseitig. **Alpha-Test** ist eine
  Shader-Variante (`#define ALPHA_TEST` über die ShaderLibrary – `discard` schaltet Early-Z ab, also nur wo nötig);
  **Blend** ohne Tiefenschreiben, nach allen opaken Submeshes (Sortierung nach Distanz mit der Render-Szene);
  **doubleSided** schaltet Culling ab und dreht die Normale der Rückseite. Texturen: Einheit 0 Basisfarbe, 1 Normale,
  2 Emissive.

### Licht – `Lighting.hpp`, `common/lighting.glsl`
```cpp
struct Environment { Vec3 sunDirection; /* zur Sonne */ Vec3 sunColor; f32 sunIntensity; Vec3 ambientSky, ambientGround; };
struct PointLight { Vec3 position; f32 radius; Vec3 color; f32 intensity; };
class LightList { static constexpr u32 kMaxPerFrame = 256, kMaxPerObject = 8;
                  void clear(); void add(const PointLight&); void selectFor(const AABB&, std::vector<u32>&) const; };
f32 pointLightAttenuation(f32 distance, f32 radius);   // = Shader-Formel
struct GpuLighting; GpuLighting packLighting(const Environment&, const LightList&);   // std140, UBO-Bindung 0
// MeshRenderer::setLighting(Device&, const Environment&, const LightList&) einmal pro Frame, dann draw(...)
```
- **Verfahren (entschieden, ehemals offene Frage):** einfaches Forward mit Licht-Limit pro Objekt. Die CPU wählt
  für jedes Objekt (Welt-Bounds) bis zu 8 Punktlichter, deren Reichweite es erreicht – nächste zuerst, bei
  Gleichstand Einfügereihenfolge. Alle Lichter des Frames (≤ 256, darüber Warnung) liegen in einem UBO, pro Draw
  gehen nur die Indizes mit. Clustered Forward erst, wenn > 32 Lichter gleichzeitig sichtbar sind oder große
  Gelände-Meshes (M4) es verlangen.
- **Lichtmodell (stilisiert):** Hemisphären-Ambient (Himmel/Boden nach Normalen-Y gemischt) + Lambert-Sonne +
  Punktlichter mit Abfall `saturate(1 − (d/r)⁴)² / (d² + 1)` (Meter; genau 0 am Radius). Keine Glanzlichter.
- Flackern von Fackeln ist Welt-/Spiellogik (ändert Lichtwerte pro Frame), kommt mit den Vobs (M4).
- Ohne `setLighting` gilt neutrales Licht (weißes Ambient, keine Sonne).

### Schatten – `Shadows.hpp`, `shadow.vert/.frag`
```cpp
struct ShadowSettings { u32 cascades = 4, resolution = 2048; f32 distance = 150, splitLambda = 0.75f,
                        casterExtension = 200, depthBias = 1, slopeBias = 2, normalOffset = 1.5f; };
struct Cascade { Mat4 viewProjection; f32 splitNear, splitFar, texelWorldSize; Vec4 atlasRect; };
std::vector<f32> cascadeSplits(near, distance, cascades, lambda);
std::vector<Cascade> computeCascades(const Camera&, const Vec3& sunDirection, const ShadowSettings&);
class ShadowMap { static Result<ShadowMap> create(Device&, const ShadowSettings&);
                  void begin(Device&); void beginCascade(Device&, u32 index) const; };
// MeshRenderer: drawShadow(Device&, mesh, materials, model, cascade); setLighting(..., const ShadowFrame*)
```
- **Cascaded Shadow Maps für die Sonne**, alle Kaskaden in **einem Tiefen-Atlas** (2×2 Kacheln, `Depth32F`,
  Standard 4 × 2048² = 64 MB), Schattentiefe nicht umgekehrt (0 = zur Sonne hin).
- **Aufteilung** praktisches Schema (λ = 0,75 zwischen logarithmisch und gleichmäßig) bis `distance`
  (Standard 150 m), Ausblenden über die letzten 10 %.
- **Kein Flimmern:** jede Kaskade umschließt ihren Frustum-Abschnitt mit einer Kugel (Größe unabhängig von der
  Kameradrehung), die Lichtmatrix ist auf ganze Texel ausgerichtet; die Lichtbox reicht `casterExtension` Meter
  zur Sonne, damit auch Werfer hinter der Kamera Schatten ins Bild werfen.
- **Gegen Akne:** Polygon-Offset im Schattenpass (`rhi::PipelineDesc::depthBias`, konstant + neigungsabhängig) und
  Normal-Offset beim Abtasten (in Schatten-Texeln der Kaskade); 3×3-PCF über Hardware-Vergleich
  (`sampler2DShadow`, Einheit 3), Kernel bleibt in seiner Kachel.
- Alpha-getestete Materialien werfen löchrige Schatten (gleiche Alpha-Test-Variante); transparente werfen keine.
- Konfiguration `[render] shadow_cascades`, `shadow_resolution`, `shadow_distance`, `shadow_debug` (Kaskaden einfärben).
- Schatten von Punktlichtern: nicht vorgesehen (Stil: Fackeln ohne Schatten), bei Bedarf später.

### Nebel, HDR und Tonemapping – `PostProcess.hpp`, `common/fog.glsl`, `post.vert/.frag`
```cpp
// Environment: Vec3 fogColor (linear); f32 fogStart = 30, fogDensity (0 = aus)
f32 fogFactor(f32 distance, f32 start, f32 density);          // 1 - exp(-((d - start) * density)²)
f32 fogDensityFor(f32 amount, f32 distance, f32 start);       // Dichte für z. B. 90 % bei 300 m
class SceneTarget { static Result<SceneTarget> create(Device&, u32 w, u32 h);   // RGBA16F + Depth32F
                    Result<void> resize(Device&, u32 w, u32 h); framebuffer(); color(); };
enum class Tonemapper : u8 { Aces, Reinhard, None };
struct PostSettings { Tonemapper tonemapper = Aces; f32 exposure = 1; };
Vec3 tonemap(const Vec3& linear, Tonemapper); Tonemapper tonemapperFromName(name, fallback);
class PostProcess { static Result<PostProcess> create(Device&, ShaderLibrary&);
                    void apply(Device&, const SceneTarget&, u32 w, u32 h, const PostSettings&); };
// Device::readTextureFloat(texture, level) liest Float-Ziele (Tests)
```
- **Frame:** Schattenpass → `SceneTarget` (linear, HDR; wächst mit dem Fenster, minimiert 1×1) mit Hintergrund
  und Meshes → `PostProcess` ins Fenster: Belichtung, Tonemapping, sRGB-Kodierung, leichtes Dithering gegen
  Banding.
- **Tonemapper:** ACES (Narkowicz-Näherung, Standard: filmischer Kontrast, Lichter laufen weich aus), Reinhard
  (neutral), None (abschneiden, zum Vergleich). `[render] tonemap = "aces"|"reinhard"|"none"`, `exposure`.
- **Distanznebel** pro Pixel im Forward-Shader (`applyFog`), damit auch transparente Flächen stimmen und der
  Himmel nicht doppelt vernebelt wird: kein Nebel bis `fogStart` (Standard 30 m), danach exponentiell-quadratisch
  (Standard 90 % bei 300 m). **Nebelfarbe = Horizontfarbe des Hintergrunds**, die Ferne geht in den Himmel über.
  `[render] fog_start`, `fog_density` (0 = aus). Ab M4 liefert die Tageszeit-Kurve Farbe und Dichte; Höhennebel
  (Sümpfe) folgt mit der Atmosphäre (M17).

### Debug-Draw – `DebugDraw.hpp`, `debug_line.*`, `debug_text.*`, `common/debug.glsl`
```cpp
struct DebugStyle { Vec4 color{1}; f32 duration = 0; bool depthTest = true; };   // duration 0 = nur dieser Frame
class DebugDraw {                     // sammelt auf der CPU, Immediate-Mode
    void line(a, b, style); arrow(from, to, style); cross(p, size, style);
    void box(const AABB&, style); box(const Mat4& transform, Vec3 halfExtents, style);
    void circle(centre, normal, radius, style); sphere(centre, radius, style);
    void axes(const Mat4&, f32 size); frustum(const Mat4& viewProjection, style); grid(centre, size, spacing, style);
    void text(const Vec3& world, sv, style, f32 scale = 1);                   // zentriert, feste Pixelgröße
    void screenText(Vec2 pixels, sv, Vec4 color, f32 scale = 1, f32 duration = 0);
    void advance(f32 dt); void clear(); bool enabled = true;
};
void layoutDebugText(const DebugDraw::Text&, const Mat4& viewProjection, u32 w, u32 h, std::vector<DebugGlyphQuad>&);
class DebugDrawRenderer { static Result<DebugDrawRenderer> create(Device&, ShaderLibrary&);
    void render(Device&, const DebugDraw&, const Camera&, const rhi::Texture* sceneDepth, u32 w, u32 h); };
```
- Gezeichnet wird **nach dem Post-Pass direkt ins Fenster**: Farben sind Anzeigewerte (sRGB), ohne Tonemapping
  und Nebel. Linien 1 px (`GL_LINES`), Text als Glyphen-Quads; dynamische Vertex-Buffer wachsen bei Bedarf.
- **Verdeckung:** Der Shader vergleicht seine Tiefe mit der Tiefe des HDR-Ziels (`SceneTarget::depth()`), mit
  relativer Toleranz, damit Linien auf Flächen sichtbar bleiben. Verdeckte Linien erscheinen gestrichelt und blass,
  verdeckter Text blass; `depthTest = false` zeichnet immer voll.
- **Text:** eingebaute 8×8-Bitmap-Schrift (font8x8, Public Domain, `assets/LICENSES.md`), ASCII; andere Zeichen
  erscheinen als `?`, `
` beginnt eine neue Zeile. Weltpunkte hinter der Kamera erzeugen keinen Text; jede Glyphe
  bekommt einen dunklen 1-px-Schatten zur Lesbarkeit.
- **Lebensdauer:** `advance(dt)` einmal pro Frame nach dem Zeichnen (Echtzeit, auch bei Pause): Einträge ohne
  Dauer verschwinden nach einem Frame, andere nach Ablauf.
- **Engine:** `Engine::debugDraw()` für alle Module; das Overlay (Aktion `debug_draw`, F2, Start über
  `[render] debug_draw`) zeigt FPS/Frame-Zeit, Draw-Calls, Dreiecke, Kameraposition, Weltachsen und bei
  `--view-mesh` Bodenraster, Bounds mit Dateinamen und Fackel-Radien. Nur bei aktivem Overlay wird gezeichnet.

### Szene und Culling (Engine, M2)
- Die Engine hält eine Liste platzierter Modelle (`--view-mesh` oder `--scene`); gleiche glTF-Dateien werden nur
  einmal geladen. **Frustum-Culling** pro Objekt mit seinen Welt-Bounds: im Hauptpass gegen die Kamera, im
  Schattenpass gegen das Lichtvolumen jeder Kaskade (es reicht `casterExtension` zur Sonne, Werfer hinter der
  Kamera bleiben also erhalten). Die Bodenplatte wirft keinen Schatten.
- Ab M4 übernimmt `world` die Szene (`RenderScene`), dann mit Sichtweite und später Instancing.
- **Hintergrund:** oberhalb des Horizonts Verlauf zur Zenitfarbe, unterhalb bleibt er in der Horizont- = Nebelfarbe
  (dort läge nur unendlich ferner, voll vernebelter Boden), so gibt es hinter dem Weltrand keine Kante.

### Engine-Anbindung
Mit Fenster und `EngineConfig::render` (Standard an) erzeugt die Engine `GlContext` → `Device` →
`ShaderLibrary`, setzt VSync aus `[window] vsync` und zeichnet pro Frame einen **Abendverlauf nach
Blickrichtung** als Hintergrund (Vollbild-Dreieck aus `gl_VertexID`, inverse View-Projection als Uniform;
Platzhalter für den Himmel in M4), dann Puffertausch. `--no-render` startet ein Fenster ohne OpenGL.
**Modell ansehen:** `--view-mesh=<pfad.gltf>` lädt ein glTF samt Materialien (`MaterialSet` + `MeshRenderer`),
zeigt es am Ursprung und richtet die Debug-Kamera so aus, dass das Modell im Bild ist (Fluggeschwindigkeit nach
Modellgröße).
Das Modell steht auf einer Bodenplatte (`asset::makePlane`, `--no-ground` lässt sie weg), die seine Schatten zeigt.
Beleuchtung dabei: tiefe warme Abendsonne, kühles Ambient und eine warme Test-„Fackel“ über dem Modell;
`--no-sun` schaltet die Sonne ab, um das Punktlicht allein zu beurteilen.
**Debug-Kamera:** `engine.camera()`, gesteuert über die Aktionen (Lauf-/Dreh-Aktionen, `jump`/`sneak` hoch/runter,
`run` schnell) und gehaltene rechte Maustaste (relativer Mausmodus); läuft in Echtzeit, auch bei Pause.
Werte aus `[camera]` (`fov`, `near`, `far`, `mouse_sensitivity`, `fly_speed`). Tests mit echter GPU: Suite
`render_gpu` (CTest-Label `gpu`).

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
- Bindless-Texturen (`GL_ARB_bindless_texture`) optional.
