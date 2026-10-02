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
Result<MeshData> loadGltf(std::span<const u8>, const fs::Path& baseDir, std::string_view debugName);   // baseDir leer: nur eigenständige Daten
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

## Bestand (M3)

### `Vfs.hpp` – virtuelles Dateisystem
```cpp
namespace g7::asset {
using MountId = u32;                                         // 0 wird nie vergeben
struct VfsFileInfo { std::string path; u64 size; };
Result<std::string> normalizeVfsPath(std::string_view path);

class Vfs {                                                  // PImpl, verschiebbar, nicht kopierbar
public:
    Result<MountId> mount(const fs::Path& source, i32 priority, std::string_view mountPoint = {});
    bool unmount(MountId);
    Result<void> rescan(MountId);
    Result<std::vector<u8>> read(std::string_view path) const;
    bool exists(std::string_view path) const;
    std::optional<VfsFileInfo> stat(std::string_view path) const;
    std::vector<VfsFileInfo> list(std::string_view directory = {}, std::string_view extension = {}) const;
    std::optional<fs::Path> diskPath(std::string_view path) const;
    usize mountCount() const;
};
}
```
- **Quellen:** Ordner oder `.g7pak`-Archiv. Was davon vorliegt, erkennt `mount` am Dateityp. Optional werden alle
  Dateien einer Quelle unter einem Mount-Punkt eingehängt (z. B. `mods/leonberg/…`).
- **Vorrang:** Höhere Priorität gewinnt. Bei gleicher Priorität gewinnt der **später** gemountete Ordner oder das
  später gemountete Archiv. So überlagern Mods und Patches die Basisdaten wie Gothics VDF-Archive.
  Nach `unmount` ist wieder die darunterliegende Datei sichtbar.
- **Pfade:** `/` als Trenner (`\` wird umgewandelt). Leere und `.`-Abschnitte sowie ein führender `/` entfallen.
  `..`, `:` (Laufwerksbuchstaben) und Steuerzeichen werden abgewiesen. Die Schreibweise bleibt erhalten, Vergleiche
  ignorieren Groß- und Kleinschreibung (ASCII), **auf allen Plattformen**: Ordner werden beim Mounten eingelesen,
  `rescan` übernimmt spätere Änderungen. Unterscheiden sich zwei Dateien eines Linux-Ordners nur in der
  Schreibweise, gilt die alphabetisch erste, mit Warnung.
- **`list`** liefert alle sichtbaren Dateien unterhalb eines Ordners rekursiv, optional nach Endung gefiltert
  (mit oder ohne Punkt), sortiert ohne Rücksicht auf Groß- und Kleinschreibung, je Pfad nur den Gewinner.
- **`diskPath`** liefert den Ort auf der Platte, wenn der Gewinner eine lose Datei ist. Das ist für Hot-Reload gedacht.
- **Threads:** `read`/`exists`/`stat`/`list`/`diskPath` laufen parallel (Asset-Worker). `mount`, `unmount` und
  `rescan` sperren exklusiv. Gelesen wird außerhalb der Sperre; ein Archiv bleibt gültig, auch wenn es während
  des Lesens ausgehängt wird.

### `Pak.hpp` – `.g7pak`-Archive (Version 1)
```
Header (32 B, little-endian): "G7PK", version u32 = 1, entryCount u32, reserved u32, tocOffset u64, tocSize u64
Daten  Dateiinhalte, jeweils ab einem 16-Byte-ausgerichteten Offset
TOC    je Eintrag: pfadHash u64 (StringId::hashOf), offset u64, size u64, rawSize u64, flags u32,
       pfadLänge u16, pfad (UTF-8, normalisiert)
```
- `PakWriter` (öffentlich, für Tests und `g7-cook`): `add(path, data)` normalisiert den Pfad und lehnt Duplikate ab
  (ohne Rücksicht auf Groß-/Kleinschreibung). `serialize()` sortiert die Einträge nach Pfad, gleiche Eingaben
  ergeben also byte-gleiche Archive. `write(target)` schreibt atomar.
- **Ohne Kompression in Version 1** (`flags = 0`, `rawSize = size`). Das Flag `kPakFlagCompressed` ist reserviert.
  Die Entscheidung LZ4 oder Zstd fällt mit dem Cooker (ADR); bis dahin werden solche Einträge mit Fehler abgelehnt.
- **Beim Mounten wird geprüft:** Magic, Version, Inhaltsverzeichnis und Daten innerhalb der Datei, Pfad normalisiert,
  Hash passend, keine doppelten Pfade. Ein beschädigtes Archiv liefert einen `Result`-Fehler.
- Der Leser (`src/PakArchive.hpp`) ist intern. Daten werden bei Bedarf gelesen, jeder Aufruf öffnet einen eigenen Stream.

### `MeshFile.hpp` – gekochte Meshes `.g7mesh` (ADR 0016)
```cpp
std::vector<u8> serializeMesh(const MeshData&);
Result<MeshData> deserializeMesh(std::span<const u8>, std::string_view debugName = "<memory>");
```
- Version 1: Header (`G7MS`, Zähler, AABB), Vertices im `Vertex`-Layout, u32-Indizes, Submeshes, Materialien, Bilder
  (Format im Header-Kommentar). Gleiche Daten ergeben gleiche Bytes.
- Beim Lesen wird geprüft: Zähler passen in die Datei (vor dem Anlegen von Speicher), Indizes im Vertex-Bereich,
  Submeshes im Index-Bereich, Material- und Bildverweise gültig, Alpha-Modus bekannt, keine Rest-Bytes.
- Bildverweise in gekochten Meshes sind VFS-Pfade ab der Wurzel (`textures/wood.png`), eingebettete Bytes leer.

### `AssetManager.hpp` – Handles, Cache, asynchrones Laden
```cpp
namespace g7::asset {
enum class AssetState : u8 { Loading, Ready, Failed };
template <class T> class Handle {                 // kopierbar, teilt einen Cache-Slot (Referenzzählung)
    bool valid() const; AssetState state() const; bool isReady() const; bool failed() const;
    const T* get() const;  const T* operator->() const;   // nullptr bis Ready
    const std::string& path() const; const std::string& error() const; u32 version() const; long useCount() const;
};
struct LoadContext { std::string_view path; std::span<const u8> bytes; const Vfs* vfs;
                     Result<std::vector<u8>> read(std::string_view) const;
                     std::string sibling(std::string_view relative) const;
                     std::optional<fs::Path> diskPath() const; };
template <class T> using Loader = std::function<Result<T>(const LoadContext&)>;
struct AssetManagerDesc { u32 workerThreads = 2; };      // 0 = synchron in update()

class AssetManager {
public:
    AssetManager(const Vfs& vfs, AssetManagerDesc desc = {});
    template <class T> void registerLoader(Loader<T>);
    template <class T> Handle<T> load(std::string_view path);
    void update();  void waitAll();
    usize pendingCount() const;  usize cachedCount() const;
};
}
```
- **Ablauf:** `load` kehrt sofort zurück. Worker-Threads lesen die Datei über das `Vfs` und rufen den Lader des Typs
  auf. Erst **`update()` im Hauptthread** schaltet fertige Ladevorgänge auf `Ready` bzw. `Failed`. Ein Asset ändert
  sich also nie mitten in einem Frame oder Simulationsschritt, sondern erscheint frühestens im nächsten Frame.
  `waitAll()` wartet auf alle offenen Ladevorgänge und veröffentlicht sie (Ladebildschirm, Tests).
  Mit `workerThreads = 0` laufen die Lader synchron in `update()` (Werkzeuge, deterministische Tests).
- **Cache und Referenzzählung:** Der Schlüssel ist Typ plus normalisierter Pfad, ohne Rücksicht auf
  Groß-/Kleinschreibung. Solange ein Handle (oder ein laufender Ladevorgang) lebt, liefert `load` denselben Slot
  ohne neues Laden. Nach dem letzten Handle wird das Asset freigegeben, ein späteres `load` lädt neu.
  Auch ein fehlgeschlagener Slot bleibt bestehen, solange Handles darauf zeigen; erneutes Laden kommt mit Hot-Reload.
- **Fehler:** Fehlende Datei, Fehler des Laders → `Failed` mit Meldung (und Log-Warnung). Ein ungültiger Pfad oder ein
  Typ ohne Lader ist **sofort** `Failed`. Wird der Manager zerstört, enden nicht veröffentlichte Ladevorgänge
  als `Failed` („shut down“); ein gerade laufender Lader wird noch zu Ende ausgeführt.
- **Eingebaute Lader:** `ImageData` (`decodeImage`) und `MeshData`: `.g7mesh` über `deserializeMesh`, alles
  andere über `loadGltf` (Entwicklung, lose Dateien). Externe glTF-Puffer gehen nur
  bei losen Dateien (Verzeichnis über `diskPath`). In Archiven müssen Meshes eigenständig sein (`.glb`, data:-URIs),
  sonst gibt es eine Fehlermeldung. Bild-URIs eines Meshes bleiben in `ImageSource::uri`; das Laden über das VFS
  kommt mit der Engine-Anbindung.
- **Threads:** `load`, `update`, `waitAll`, `registerLoader` und Handle-Zugriffe gehören in den Hauptthread.
  Lader laufen auf Workern und nutzen nur ihren `LoadContext`. Hochladen auf die Grafikkarte bleibt in `render`
  (Hauptthread mit GL-Kontext). Das `Vfs` muss den Manager überleben.

## Bestandteile (geplant)
- **Engine-Anbindung** (nächster Schritt): `Engine` besitzt `Vfs` und `AssetManager` und ruft `update()` einmal pro
  Frame. Mounts kommen aus `engine.toml` (z. B. `assets/cooked/*.g7pak`, im Entwicklungsmodus zusätzlich
  `assets/source`). `--view-mesh` und die Testszene laden über Handles, `MaterialSet` liest Bilder über das VFS.
- **Weitere Loader** pro Typ (`Texture`, `Skeleton`, `AnimationClip`, `Sound`, `WorldData`, `Material`) mit den
  jeweiligen Modulen. Eingetragen werden sie vom höheren Modul über `registerLoader`, `asset` kennt sie nicht.
- **Hot-Reload**: Dateiüberwachung im Entwicklungsmodus (über `Vfs::diskPath`/`rescan`) → Lader lädt neu,
  das Handle bleibt gültig, `version()` zählt hoch.
