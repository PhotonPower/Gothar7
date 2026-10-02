# asset

**Zweck:** Laden, Cachen und Verwalten aller Inhalte.

## Bestand (M2)

### `ImageData.hpp` – Bilder (ADR 0014, stb_image privat)
```cpp
struct ImageData { u32 width, height; std::vector<u8> rgba8; };   // immer RGBA8, erste Zeile = oben
Result<ImageData> decodeImage(std::span<const u8> bytes, std::string_view debugName = "<memory>");  // PNG/JPEG/TGA/BMP
Result<ImageData> loadImage(const fs::Path&);
```
- Grau/RGB werden zu RGBA erweitert; Zeilen in Dateireihenfolge (passt zur glTF-UV-Konvention v = 0 oben).
- KTX2 (vorkomprimiert, BC7/BC5) kommt mit dem Cooker in M3.

### `Procedural.hpp`
- `makePlane(size, uvTileSize, color)` – Platte in XZ (Normale +Y), `makeBox(halfExtents, color)` – Quader mit
  flachen Flächen; beide mit UVs, Tangenten und einem Material. Für Vorschau-Boden und Tests.

### `MeshData.hpp` – glTF-Import (ADR 0013, fastgltf privat)
```cpp
namespace g7::asset {
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; Vec4 tangent; };          // 48 Byte, so lädt render hoch
struct ImageSource { std::string uri; std::vector<u8> encoded; std::string mimeType; };   // Datei oder eingebettet
enum class AlphaMode : u8 { Opaque, Mask, Blend };
struct MaterialInfo { std::string name; Vec4 baseColor; i32 baseColorImage = -1; i32 normalImage = -1; f32 normalScale;
                      Vec3 emissive; i32 emissiveImage = -1; AlphaMode alphaMode; f32 alphaCutoff; bool doubleSided; };
struct Submesh { u32 firstIndex, indexCount, material; };
struct MeshData { std::vector<Vertex> vertices; std::vector<u32> indices; std::vector<Submesh> submeshes;
                  std::vector<MaterialInfo> materials; std::vector<ImageSource> images; AABB bounds; };
Result<MeshData> loadGltf(const fs::Path&);                                    // .gltf (+ .bin / data:) oder .glb
Result<MeshData> loadGltf(std::span<const u8>, const fs::Path& baseDir, std::string_view debugName);
}
```
- Lädt die Standardszene als **ein statisches Mesh**: Knoten-Transformationen werden eingerechnet (Normalen mit
  der Normalenmatrix; bei Spiegelung wird die Wicklung getauscht, damit Vorderseiten gegen den Uhrzeigersinn
  bleiben), Primitive mit gleichem Material werden zu einem Submesh zusammengefasst; Primitive ohne Material
  bekommen ein angehängtes Material `default`.
- Koordinaten unverändert (glTF: +Y oben, rechtshändig, Meter; Modelle schauen nach +Z).
- Fehlende Normalen werden flächengewichtet berechnet. **Tangenten:** aus der Datei, sonst für Primitive mit
  Normal-Map aus den UVs berechnet (gemittelt, orthogonalisiert; w = −1 bei gespiegelten UVs; Bitangente =
  cross(n, t) · w zeigt zum Bild-oben, passend zur glTF-Konvention) – nicht bit-exakt MikkTSpace, das folgt im
  Cooker (M3); ohne Normal-Map bleiben Tangenten 0. Indizes immer u32; nur Dreiecke, andere Primitive werden mit Warnung übersprungen.
- Materialien (bewusst ohne Metallic/Roughness): Basisfarbe (Faktor linear, Bild sRGB), Normal-Map (linear, `scale`),
  Emissive (Faktor linear, Bild sRGB), `alphaMode`/`alphaCutoff`/`doubleSided` wie in glTF. Bilder als `ImageSource`: URI relativ
  zur Modelldatei oder eingebettete Bytes (`.glb`-bufferView, data:-URI) – Dekodieren mit `decodeImage`/`loadImage`.
- Skins/Animationen: M6. Ab M3 kocht `g7-cook` glTF in ein Laufzeitformat, das dieselbe `MeshData` liefert.

## Bestandteile
- **VFS**: Mount-Liste aus Ordnern und `.g7pak`-Archiven; höhere Priorität überschreibt
  (Mods/Patches, wie Gothics VDF-Mechanik). Pfade case-insensitive, `/` als Trenner.
- **Asset-Handles**: `Handle<T>` mit Referenzzählung; `AssetManager::load<T>(path)` liefert
  sofort ein Handle, Laden läuft asynchron; `isReady()`, Platzhalter bis fertig.
- **Loader-Registry**: pro Typ (`Texture`, `Mesh`, `Skeleton`, `AnimationClip`, `Sound`, `WorldData`, `Material`).
- **Hot-Reload**: Dateiüberwachung im Entwicklungsmodus → Loader lädt neu, Handle bleibt gültig.

## .g7pak-Format (Entwurf)
```
Header { magic "G7PK", version u32, entryCount u32, tocOffset u64 }
Daten  { Blöcke, optional LZ4/Zstd-komprimiert }
TOC    { pfadHash u64, pfad (UTF-8), offset u64, size u64, rawSize u64, flags u32 }
```

## Geplante API
```cpp
namespace g7::asset {
class Vfs { public: Result<void> mount(std::string_view source, i32 priority);
                    Result<std::vector<u8>> read(std::string_view path) const; bool exists(...) const; };
template <class T> class Handle { ... const T* get() const; bool isReady() const; };
class AssetManager { public: template <class T> Handle<T> load(std::string_view path); void update(); };
}
```
