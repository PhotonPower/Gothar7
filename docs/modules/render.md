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
enum class Format { R8, RG8, RGBA8, RGBA8_SRGB, RGBA16F, R32F, Depth24Stencil8, Depth32F,
                    BC7, BC7_SRGB, BC5 };   // Blockformate: 4x4 Pixel je 16 Byte, BC5 = RG (Normal-Maps)
enum class VertexFormat { Float1..Float4, UNorm8x4 };  enum class IndexType { U16, U32 };
enum class Topology { Triangles, Lines };  enum class CullMode { None, Back, Front };
enum class CompareOp { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };
enum class BlendMode { Opaque, Alpha, Additive };  enum class Filter { Nearest, Linear };  enum class Wrap { Repeat, Clamp, Mirror };
u32 bytesPerPixel(Format); bool isDepthFormat(Format); bool hasStencil(Format); u32 vertexFormatSize(VertexFormat);
bool isCompressed(Format); usize imageSize(Format, w, h);   // Bytes einer Ebene (bei Blockformaten ganze Blöcke)
u32 indexSize(IndexType); u32 mipLevelCount(w, h); u32 mipSize(size, level);

class Buffer        { Result<void> update(offset, span); usize size(); BufferUsage usage(); };      // BufferDesc{size, Static|Dynamic, initialData}
class Texture       { Result<void> upload(level, span); Result<void> generateMipmaps(); const TextureDesc& desc(); };  // TextureDesc{w, h, format, mipLevels (0 = Kette)}
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
- **Komprimierte Texturen (M3):** BC7 (Farbe, linear oder sRGB) und BC5 (zweikanalige Normal-Maps) werden je
  Mip-Ebene fertig hochgeladen (`glCompressedTextureSubImage2D`); `generateMipmaps()` lehnt sie ab, als Render-Ziel
  sind sie nicht erlaubt. Ist die Normal-Textur eines Materials BC5, setzt `MaterialSet` `Material::normalTwoChannel`
  und `mesh.frag` rekonstruiert `z = sqrt(max(0, 1 − x² − y²))`.
- **`createTexture(Device&, const asset::TextureData&, bool colour)`** (`TextureUpload.hpp`): RGBA8 (ungekochte
  PNG/JPEG) sRGB nach Verwendung (Farbe ja, Normalen/Daten nein) mit Mip-Kette von der GPU; BC7 sRGB laut Daten (aus
  KTX2); BC5 linear; gekochte Formate mit allen mitgelieferten Ebenen. `MaterialSet::ImageLookup` liefert
  `const asset::TextureData*`; die Pfad-Variante lädt `.ktx2` über `decodeKtx2`, sonst PNG/JPEG.
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

### Skinned Meshes – `SkinnedMesh.hpp`, `common/skinning.glsl` (M6 Teil C)
```cpp
class SkinnedMesh { static Result<SkinnedMesh> create(Device&, const asset::SkinnedModelData&, u32 lod);
                    void bind(Device&) const; void draw(Device&, usize submesh) const;
                    usize morphCount() const; void setMorphWeights(std::span<const f32>); };
void MeshRenderer::drawSkinned(Device&, const SkinnedMesh&, const MaterialSet&, const Mat4& model,
                               std::span<const Mat4> bones, const Camera&);
void MeshRenderer::drawShadowSkinned(..., std::span<const Mat4> bones, const Cascade&);
```
- Alle Teile einer LOD-Stufe in einem Vertex-/Index-Buffer; Vertex = `asset::Vertex` + 4 Knochenindizes (u16,
  Attribut 5, `VertexFormat::UInt16x4` → `uvec4`) + 4 Gewichte (Attribut 6), 72 Byte.
- Knochen: Uniform-Block 2 (`uBones[128]`, `asset::kMaxBones`), Skinning-Matrix = Modellraum-Knochen ·
  Inverse-Bind. Shader-Varianten mit `SKINNED` (`mesh_skinned`, `mesh_alpha_test_skinned`, `shadow_skinned`, …).
- Morph-Targets auf der CPU: geänderte Gewichte → nur der betroffene Vertex-Bereich wird hochgeladen
  (Puffer `Dynamic`); LOD-Stufen ohne Targets ignorieren Gewichte.
- Nicht im Multi-Draw (einzelne Draws; für viele NPCs später Instancing/Knochen-Puffer, M9).

### Texturen – `TextureUpload.hpp`
```cpp
struct TextureUpload { bool srgb = true; bool mipmaps = true; std::optional<f32> alphaCutoff; };
Result<rhi::Texture> createTexture(Device&, const asset::ImageData&, TextureUpload = {});   // RGBA8(_SRGB), volle Mip-Kette
Result<rhi::Texture> createSolidTexture(Device&, u8 r, u8 g, u8 b, u8 a, bool srgb = true);  // 1x1, Ersatz
std::vector<u8> Device::readTexture(const rhi::Texture&, u32 level) const;                 // RGBA8, Tests/Debug
```
- Zeilen in Dateireihenfolge hochgeladen: UV (0,0) liest das Texel oben links – wie glTF, ohne Spiegeln.
- Farbtexturen sind sRGB (Hardware dekodiert beim Sampeln), Datentexturen (Normalen, Masken) linear.
- Mipmaps auf der GPU erzeugt; Materialsampler mit anisotroper Filterung aus `[render] anisotropy` (Standard 8).
- **Alpha-Test mit erhaltener Bedeckung** (Hinweis figuren zu Stoppel-Bärten, 2026-10-08): Die Farbtextur eines
  Materials mit `alphaMode` MASK bekommt ihre Mip-Kette auf der CPU (`asset::buildMipChain`). Jede Stufe skaliert ihr
  Alpha so, dass der Anteil der Texel über `alphaCutoff` dem der Stufe 0 gleicht (`asset::preserveAlphaCoverage`,
  nach Castaño). So dünnen Bärte, Haare und Blätter in der Ferne nicht aus; feine Punkte verschmelzen dabei zu einer
  gleich dichten Fläche.
  - Gilt für ungekochte Texturen (`MaterialSet` → `TextureUpload::alphaCutoff`, Cache-Schlüssel mit Cutoff) und für
    gekochte (`g7-cook` Version 4 schreibt die Kette so ins KTX2).
  - Ohne MSAA gibt es kein alpha-to-coverage.
- **Farbraum:** Alle Szenen-Shader rechnen und schreiben **linear** (HDR, Werte über 1 erlaubt) in das
  `SceneTarget`; erst der Post-Pass tonemappt und kodiert nach sRGB (siehe „Nebel, HDR und Tonemapping“).

### Materialien – `Material.hpp`
```cpp
struct Material { const rhi::Texture *baseColor, *normal, *emissive; Vec4 baseColorFactor; Vec3 emissiveFactor;
                  f32 normalScale, alphaCutoff; asset::AlphaMode alphaMode; bool doubleSided; };
struct ExternalImage { const asset::TextureData* data; std::string cacheKey; u64 version; }; // aus Zeiger implizit
class MaterialSet  { using ImageLookup = std::function<ExternalImage(const asset::ImageSource&)>;
                     static Result<MaterialSet> create(Device&, const asset::MeshData&, const ImageLookup&,
                                                       std::shared_ptr<const MaterialDefaults> = {},
                                                       TextureCache* = nullptr);                   // Engine: über das VFS
                     static Result<MaterialSet> create(Device&, const asset::MeshData&, const fs::Path& modelDir); // Werkzeuge/Tests
                     const Material& operator[](usize) const; usize size() const; };
class MeshRenderer { static Result<MeshRenderer> create(Device&, ShaderLibrary&, f32 anisotropy);
                     void draw(Device&, const Mesh&, const MaterialSet&, const Mat4& model, const Camera&); };
```
- **Bewusst schlicht/stilisiert** (kein PBR): Basisfarbe × Textur, Tangentenraum-Normal-Map mit `normalScale`,
  Emissive (nach dem Licht addiert), Alpha-Modi wie glTF.
- `MaterialSet` lädt jedes Bild einmal je Verwendung (Farbe sRGB, Normalen linear); fehlende/kaputte Bilder →
  neutrale 1×1-Ersatztexturen (weiß bzw. flache Normale) mit Warnung. Texturen bleiben beim Verschieben gültig.
- **Textur-Cache** (`TextureCache`, `MeshRenderer::textureCache()`): Bilder mit Cache-Schlüssel (VFS-Pfad + Version
  aus dem `AssetManager`) lädt die Engine **einmal für alle Modelle** hoch – je Pfad, sRGB/linear und Version eine
  Textur. Die `MaterialSet`s halten sie per `shared_ptr` (Referenzzählung), der Cache nur schwach: Sobald das letzte
  Modell sie freigibt (Weltwechsel), ist sie aus dem VRAM. Hot-Reload: Die neue Version ist eine neue Textur, Modelle
  wechseln beim Neu-Hochladen; die alte geht mit dem letzten. Viele Häuser auf wenigen Trim-Sheets kosten so die
  Sheets einmal. Ohne Schlüssel (Werkzeuge, Tests mit Zeiger-Lookup) lädt jedes Set eigene Texturen. Das Log nennt
  nach dem Laden die Zahl der Bild-Texturen auf der GPU.
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
- **Räume** (Zonen `indoor`, world.md „Zonen“): Das Ambient wird je Fläche in Räumen mit `[indoor] ambient`
  multipliziert (`indoorAmount` in `common/lighting.glsl`, gleiche Formel `render::indoorAmount`); bis zu 32 gedrehte
  Boxen im Lighting-UBO (`GpuLighting::indoorBoxes`).
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
- **Seltener neu gezeichnet** (`Engine::shadowRedraw`): Kaskade 1 und 2 jedes Bild, Kaskade 3 jedes 2. und Kaskade 4
  jedes 4. Bild (nie im selben). Dazwischen behält die Kachel ihre Tiefe, schattiert wird mit der Matrix, mit der sie
  gezeichnet wurde (`ShadowMap::beginCascade` löscht nur die eigene Kachel). Hat sich die frische Anpassung um mehr
  als 5 % der Kachel verschoben (Kamera gegangen oder gedreht), wird sofort neu gezeichnet; ebenso nach Weltwechsel
  oder neuer Schattenkarte. Nachteil: Bewegte Werfer in der Ferne (Tiere, NPCs) und der Sonnenstand hängen dort bis
  zu 3 Bilder nach – bei schnellem Flug höchstens in der Ferne sichtbar. Leonberg-Überblick: 31,7 → 23,5 ms je Bild.
- Konfiguration `[render] shadow_cascades`, `shadow_resolution`, `shadow_distance`, `shadow_far_cadence` (seltener
  neu zeichnen, Vorgabe an), `shadow_debug` (Kaskaden einfärben).
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

### Sichtbarkeit – `Visibility.hpp` (M4)
```cpp
struct CullSettings { f32 viewDistance; f32 sizeCull; };          // engine.toml [render], 0 = aus
CullResult cullByDistance(const AABB&, const Vec3& eye, const CullSettings&, bool sizeCullable);
class CullGrid { void build(span<const AABB>, f32 cellSize = 64); void query(const Frustum&, eye, maxDistance, out); };
```
- Die Engine sortiert alle Instanzen in ein **Raster** (64-m-Zellen über x/z; eine Zelle wächst auf die Bounds ihrer
  Objekte, große Objekte gehen nicht verloren). Haupt- und Schattenpass fragen erst Zellen ab (Frustum, Sichtweite),
  dann die Objekte darin. Neuaufbau, wenn sich Instanzen ändern.
- **Sichtweite** `view_distance` (Vorgabe 400 m; Nebel deckt dort 98,7 %): Abstand zum nächsten Punkt der Bounds.
  **Größe** `size_cull` (Vorgabe 0.005 ≈ 4 Pixel Radius bei 1600×900): nur Deko (`category` deco, world.md); Mobs und
  `gameplay` nie. Was der Hauptpass ausblendet, wirft auch keinen Schatten.
- Begründung der Vorgaben: Leonberg-Kern mit Fachwerk (905 Häuser, 1,6 Mio. Dreiecke) von START_UEBERSICHT –
  Bild ohne sichtbaren Unterschied (nur ferne Häuser im dichten Nebel), 4,16 → 2,91 ms (RTX 3080). Aufploppen an
  der Grenze verdeckt der Nebel; fällt es auf, ist es ein offener Punkt (kein Überblenden in M4).
- Overlay (F2): „hidden: N far, M small“.

### Himmel – `background.frag`, `render::Sky` (M4)
- Verlauf vom Horizont (= Nebelfarbe, damit Fernes in den Himmel übergeht) zum Zenit, Sonnenscheibe mit Schein,
  Mondscheibe, prozedurale Sterne (feste Zellen der Blickrichtung, zum Zenit hin eingeblendet). Unter dem Horizont
  bleibt die Nebelfarbe. Werte je Uhrzeit aus `world::DayCycle` (world.md „Spielzeit & Umgebung“); die Engine setzt
  `Environment` und `Sky` jeden Frame aus der Spielzeit.

### Multi-Draw – `MeshRenderer::drawBatched` (M4, B2)
```cpp
struct MeshDrawItem { const Mesh* mesh; const MaterialSet* materials; Mat4 model; AABB bounds; };
void drawBatched(Device&, span<const MeshDrawItem>, const Camera&);     // Hauptpass
void drawShadowBatched(Device&, span<const MeshDrawItem>, const Cascade&); // je Kaskade
void beginFrame();  BatchStats lastBatch();  shared_ptr<const MaterialDefaults> defaults();
```
- Deckende und Alpha-Test-Submeshes von Arena-Meshes werden nach (Pipeline, Material-**Werten**, Geometrie-Block)
  gruppiert, je Gruppe **ein** `glMultiDrawElementsIndirect`. Modellmatrix, Normalenmatrix und Punktlichter je Draw
  liegen in einem SSBO (`common/draws.glsl`); jeder Draw trägt seinen Index als `baseInstance`, ein Instanz-Attribut
  (Divisor 1) liefert ihn dem Shader – **kein `gl_DrawID`/`ARB_shader_draw_parameters` nötig** (GL 4.3-Kern, auch
  Mesa llvmpipe in der CI). Durchsichtige Submeshes und Meshes außerhalb einer Arena: einzeln, danach.
- Gleiche Werte in verschiedenen Modellen bündeln, weil alle `MaterialSet`s die neutralen Texturen des Renderers teilen
  (`MaterialSet::create(…, defaults())`), und Texturen aus Dateien über den Textur-Cache dieselben Objekte sind:
  Modelle auf demselben Trim-Sheet bündeln miteinander.
- Pufferung: drei Puffersätze im Wechsel je Frame, innerhalb eines Frames hängt jeder Pass hinten an (kein
  Überschreiben, solange die GPU liest); Wachstum in Zweierpotenzen.
- **`[render] multi_draw`**: `"auto"` (Vorgabe) = an, außer auf Intel-GPUs; `"on"`/`"off"` erzwingen.
- Messung Leonberg-Kern mit Fachwerk (905 Häuser, 1,6 Mio. Dreiecke, Release, 1600×900):

  | GPU | einzeln | Multi-Draw |
  |---|---|---|
  | RTX 3080 Laptop, Übersicht | 3,06 ms, 5477 Draws | **2,17 ms**, 477 Draws (Hauptpass: 15 Bündel für 1649 Submeshes; Rest = 336 Gelände-Kacheln) |
  | RTX 3080 Laptop, Marktplatz | 3,06 ms | **2,05 ms** |
  | Intel UHD, Übersicht | **22,2 ms** (45 FPS) | 25,0 ms |

  **Offener Punkt (Intel):** Dort begrenzt die GPU, nicht die CPU; Multi-Draw kostet ~10 %. Geprüft und nicht die
  Ursache: Puffer-Stalls (Rotation ohne Wirkung), Normalenmatrix je Vertex (vorberechnet ohne Wirkung). Vermutung:
  indirekte Draws und SSBO-Zugriff je Vertex auf Intel teurer; Gelände-Kacheln (336 Draws) sind der nächste Hebel.
- Z-Fighting: Wo Geometrie deckungsgleich überlappt (modulare Wände der Testszene), gewinnt bei gleicher Tiefe der
  zuletzt gezeichnete Teil – Multi-Draw zeichnet in anderer Reihenfolge, das Bild unterscheidet sich dort in einzelnen
  Pixeln. Inhalte sollen keine doppelten Flächen haben (Budget-Tabelle).

### Geometrie-Arena – `GeometryArena.hpp` (M4)
```cpp
class GeometryArena { Result<GeometrySlice> allocate(Device&, span<const asset::Vertex>, span<const u32>);
                      void bind(Device&, u32 block) const; usize blockCount(), usedVertices(), usedIndices(); };
static Result<Mesh> Mesh::create(Device&, GeometryArena&, const asset::MeshData&);   // Mesh in der Arena
```
- Statische Meshes der Engine liegen in gemeinsamen Blöcken (je 1 Mi Vertices / 4 Mi Indices, größere Meshes in
  eigenem Block), First-Fit mit Freiliste; freigegebene Bereiche werden zusammengelegt und wiederverwendet (Hot-Reload).
  `GeometrySlice` gibt seinen Bereich im Destruktor zurück; die Arena muss alle Meshes überleben.
- Gezeichnet wird mit `firstIndex`/`baseVertex`; das Device merkt sich je Vertex-Array (uid) die angehängten Puffer
  und hängt gleiche nicht erneut an (`FrameStats::bufferBinds`). Grundlage für Instancing (Teil B).
- Messung (RTX 3080, 5400 Modelle mit je eigener `.glb`): Die frühere Überlinearität lag **nicht** an den Puffern
  (Hypothese widerlegt: mit Arena 0 Bindungen, Zeit unverändert), sondern an `AssetManager::pruneCache` (O(n²) je
  Frame); behoben, 83 → ~10 ms.

### Gelände – `Terrain.hpp`, `terrain.vert/.frag` (M4)
```cpp
struct HeightfieldDesc { u32 width, height; f32 cellSize; Vec2 firstSample; f32 minY, maxY; std::span<const u16> samples; };
struct TerrainSurfaceDesc { std::vector<const asset::TextureData*> splatMaps; std::vector<Layer{albedo, tile}> layers;
                             std::span<const u8> holes; };   // max. 8 Schichten, Löcher je Zelle
class TerrainRenderer { static Result<TerrainRenderer> create(Device&, ShaderLibrary&, const HeightfieldDesc&, const ShadowSettings&);
    Result<void> setSurface(Device&, const TerrainSurfaceDesc&); u32 layerCount(); bool hasHoles();
    void drawShadow(Device&, const Cascade&); void draw(Device&, const Camera&, const LightList*);
    static u32 lodFor(f32 distance, f32 lodDistance); f32 lodDistance = 96; u32 drawnChunks(); };
// MeshRenderer::bindLighting(Device&): Licht-Block + Schatten-Atlas für andere Renderer
```
- `render` kennt nur `HeightfieldDesc` (keine `world`-Typen); `world::Heightfield` liefert ihn. Höhen liegen als
  **R16**-Textur (neues RHI-Format) vor und werden im Vertex-Shader per `texelFetch` gelesen; Normalen aus
  Nachbar-Samples.
- **Kacheln** zu 64×64 Zellen aus vier gemeinsamen Gittern (1/2/4/8 Samples Schrittweite); die Stufe wählt die
  Entfernung zur Kachel (`lodFor`: volle Auflösung bis `lodDistance`, dann eine Stufe je Verdopplung). **Schürzen**
  am Kachelrand verdecken Risse zwischen Stufen. Frustum-Culling je Kachel, Punktlichter je Kachel.
- Schatten: Gelände wirft und empfängt; im Schattenpass mit der gröbsten Stufe. Licht und Nebel wie Meshes;
  ohne Splat-Schichten Einfärbung nach Hangneigung und Höhe.
- **Splat** (Teil B): bis zu 8 Schichten. Gewichte aus 1–2 RGBA-Karten (2D-Array, linear, Pixel-Mitten auf Samples),
  Albedos als 2D-Array (sRGB, Kachelung `tile` über Welt-xz, anisotrop). Im Shader werden alle Schichten gelesen
  (gleichförmiger Kontrollfluss) und nach normierten Gewichten gemischt. `setSurface` prüft Anzahl, Größen und
  Formate und meldet Fehler mit Text; die bisherige Oberfläche bleibt dann.
- **Löcher**: R8-Textur je Zelle, `discard` im Haupt- und Schattenpass (der Schattenpass nutzt `terrain.frag` mit
  `SHADOW`). Texture-Units: 4 Höhen, 5 Splat, 6 Schichten, 7 Löcher; ohne Splat/Löcher 1×1-Platzhalter.
- RHI: `TextureDesc::layers`/`array` für 2D-Array-Texturen, `Texture::upload(level, data, layer)`;
  `createTextureArray(device, layers, TextureArrayUsage::Colour|Data)` (TextureUpload.hpp) prüft gleiche Größe,
  Format und Mip-Zahl mit klarer Meldung; `ShaderProgram::setUniform(name, span<const f32>)`.

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

## Leistungsbudgets (verbindlich für Inhalte)
Mit den Inhalts-Spuren abgestimmt; Messungen auf RTX 3080 Laptop (Release, 1600×900, Schatten an), Ziel bleibt
spielbar auf Intel UHD. Änderungen nur nach Absprache (`docs/coordination.md`).

| Bereich | Budget | Spur / Vertrag |
|---|---|---|
| Figuren-Texturen gesamt | **≤ 512 MB VRAM** (BC7/BC5, mit Mips); je NPC ≈ 4–6 MB ohne geteilte Haut | figuren, `characters-pipeline.md` §2.3 |
| Figuren-Texturgrößen | Haut ≤ 2048², Kleidung und Haare ≤ 1024², Augen/Brauen/Wimpern ≤ 256², Normal-Map ≤ Basisfarbe, Zweierpotenzen; geteilte Texturen als externe Dateien (sonst kein Teilen) | figuren |
| Figuren-Geometrie | lod0 ≤ **20 000** Dreiecke je Figur (Körper+Kleidung 8–15 k, Kopf 3–5 k), lod1 ≈ 50 %, lod2 ≈ 20 % | figuren, §2.2 (LOD-Vertrag) |
| Fachwerk-Häuser (W5) | ≤ **2 000** Dreiecke je Haus, Kern ≤ 2 Mio.; 5 Materialien mit gleichen Werten in allen Häusern; Balken als einfache Quader, keine doppelten Flächen | welt |
| Gelände | Heightmap bis 2000×2000 Samples (Leonberg: 667 FPS), bis 8 Splat-Schichten | welt, `world.md` „Gelände“ |
| Vobs je Welt | 5400 Einzel-Vobs mit je eigenem Modell ≈ 10 ms je Frame (nach #65); mehr erst mit Teil B (Multi-Draw, Distanz-Culling) | welt |

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
- **Dual-Quaternion-Skinning** (Hinweis figuren, 2026-10-03): Lineares Blend-Skinning verliert in extremen Posen
  Volumen an Schulter und Ellbogen (Beule bei Arm nach hinten oben, z. B. Klettern; Gewichte helfen nicht).
  Abhilfe: DQS im Skinning-Shader (Knochen als Dual-Quaternionen, Variante `SKINNED_DQ`), ggf. nur für
  Menschen. Entscheidung, sobald echte Clips (F4) es noch zeigen.
- **Spiegelnde Fenster** (Wunsch Projektinhaber, „Raytracing“; M17, nicht jetzt). Optionen:
  - **Reflexions-Proben bzw. Cubemaps je Zone** (vorberechnet oder selten aktualisiert, mit Parallaxen-Korrektur
    für Räume/Straßenzüge): günstig, auf jeder Hardware.
  - **Screen-Space-Reflections:** dynamisch, aber nur was auf dem Bild ist, Lücken am Rand; gut in Kombination mit
    Proben als Rückfall.
  - **Echtes Raytracing** nur mit dem optionalen Vulkan-Backend (eigene ADR); OpenGL bietet kein Hardware-Raytracing.
  - Dazu ein Material-Merkmal „spiegelnd“ (Glas) in den Modellen der Welt-Spur.

## Partikel (M12 Teil A, umgesetzt) – `Particles.hpp`
- **Emitter als Daten:** `data/fx/<name>.toml` (Version 1): `sprite` (`soft`, `smoke`, `spark`), `blend` (`additive`,
  `alpha`), `rate` je Sekunde und/oder `burst`, `duration` (0: bis gestoppt), `lifetime`, `speed` ([min, max]),
  `direction` und `spread` (Kegel, Grad), `radius` (Startkugel), `gravity` (m/s² nach unten; negativ: steigt), `drag`,
  `size` ([bei Geburt, beim Tod]), `color_start`/`color_end` (sRGB, Alpha); optional `[light]` (`color`, `range`,
  `intensity`, `flicker`) – das Licht folgt dem Emitter und geht in die Punktlichter des Bildes ein. Fehler nennen
  Datei und Schlüssel.
- **Simulation** auf der CPU (`ParticleSystem`, im festen Schritt): Emitter starten (`spawn`), wandern (`move`, die
  Teilchen bleiben), hören auf (`stop`; weg, wenn das letzte Teilchen vergeht); höchstens 2048 Teilchen je Emitter.
- **Darstellung** (`ParticleRenderer`): ein Quad je Teilchen per Instancing (`Device::drawInstanced`, Ecken aus
  `gl_VertexID`), zur Kamera gedreht; Funken längs ihrer Bewegung gestreckt. Erst Alpha-Teilchen (Rauch, von hinten
  nach vorn sortiert), dann additive; tiefengetestet, ohne Tiefe zu schreiben, ins HDR-Ziel nach den Modellen.
- **Sprites prozedural** (keine Textur-Assets): weicher Punkt, Rauch aus Rauschen, dünner Funke – ein Textur-Array.
- **Engine/Lua:** `fx(name, x, y, z[, dx, dy, dz])` → Nummer, `fx_move`, `fx_stop`, `fx_alive`;
  `Engine::startEffect`. Mitgeliefert: `fire`, `smoke`, `firebolt`, `impact_fire`, `heal`, `sleep`.

