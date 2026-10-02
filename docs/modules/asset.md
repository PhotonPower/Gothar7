# asset

**Zweck:** Laden, Cachen und Verwalten aller Inhalte.

## Bestand (M2)

### `MeshData.hpp` – glTF-Import (ADR 0013, fastgltf privat)
```cpp
namespace g7::asset {
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; Vec4 tangent; };          // 48 Byte, so lädt render hoch
struct MaterialInfo { std::string name; Vec4 baseColor; std::string baseColorTexture; };
struct Submesh { u32 firstIndex, indexCount, material; };
struct MeshData { std::vector<Vertex> vertices; std::vector<u32> indices; std::vector<Submesh> submeshes;
                  std::vector<MaterialInfo> materials; AABB bounds; };
Result<MeshData> loadGltf(const fs::Path&);                                    // .gltf (+ .bin / data:) oder .glb
Result<MeshData> loadGltf(std::span<const u8>, const fs::Path& baseDir, std::string_view debugName);
}
```
- Lädt die Standardszene als **ein statisches Mesh**: Knoten-Transformationen werden eingerechnet (Normalen mit
  der Normalenmatrix; bei Spiegelung wird die Wicklung getauscht, damit Vorderseiten gegen den Uhrzeigersinn
  bleiben), Primitive mit gleichem Material werden zu einem Submesh zusammengefasst; Primitive ohne Material
  bekommen ein angehängtes Material `default`.
- Koordinaten unverändert (glTF: +Y oben, rechtshändig, Meter; Modelle schauen nach +Z).
- Fehlende Normalen werden flächengewichtet berechnet; fehlende Tangenten bleiben 0 (MikkTSpace mit dem
  Material-Modell). Indizes immer u32; nur Dreiecke, andere Primitive werden mit Warnung übersprungen.
- Materialien vorerst: Name, `baseColorFactor`, URI der Basisfarb-Textur (eingebettete Bilder folgen mit den Texturen).
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
