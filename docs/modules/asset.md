# asset

**Zweck:** Laden, Cachen und Verwalten aller Inhalte.

## Bestand (M2)

### `ImageData.hpp` – Bilder (ADR 0014, stb_image privat)
```cpp
struct ImageData { u32 width, height; std::vector<u8> rgba8; };   // immer RGBA8, erste Zeile = oben
Result<ImageData> decodeImage(std::span<const u8> bytes, std::string_view debugName = "<memory>");  // PNG/JPEG/TGA/BMP
Result<ImageData> loadImage(const fs::Path&);
Result<std::vector<u8>> encodePng(const ImageData&);  Result<void> savePng(const fs::Path&, const ImageData&);  // atomisch
```
- Grau/RGB werden zu RGBA erweitert; Zeilen in Dateireihenfolge (passt zur glTF-UV-Konvention v = 0 oben).
- KTX2 (vorkomprimiert, BC7/BC5) kommt mit dem Cooker in M3.
- PNG-Schreiben (Screenshots, Werkzeuge) über `stb_image_write` mit `STB_IMAGE_WRITE_STATIC`, damit Tests eine eigene
  Kopie zum Erzeugen von Testbildern einbinden können.

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
struct CollisionPart { enum class Kind : u8 { Hull, Mesh }; Kind kind; std::vector<Vec3> points; std::vector<u32> indices; };
struct MeshData { std::vector<Vertex> vertices; std::vector<u32> indices; std::vector<Submesh> submeshes;
                  std::vector<MaterialInfo> materials; std::vector<ImageSource> images; AABB bounds;
                  std::vector<CollisionPart> collision; };                     // aus COL_-Knoten (M5)
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
- **Kollisionsgeometrie `COL_` (M5, Vertrag mit welt und figuren):** siehe unten „Kollision in Modellen“.
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

### `Pak.hpp` – `.g7pak`-Archive (Version 2, zstd; ADR 0016)
```
Header (32 B, little-endian): "G7PK", version u32 = 2, entryCount u32, reserved u32, tocOffset u64, tocSize u64
Daten  Dateiinhalte (roh oder ein zstd-Frame), jeweils ab einem 16-Byte-ausgerichteten Offset
TOC    je Eintrag: pfadHash u64 (StringId::hashOf), offset u64, size u64 (gespeichert), rawSize u64 (entpackt),
       flags u32 (Bit 0 = zstd), pfadLänge u16, pfad (UTF-8, normalisiert)
```
- `PakWriter` (öffentlich, für Tests und `g7-cook`):
  - `add(path, data, PakCompression = Auto)` normalisiert den Pfad und lehnt Duplikate ab (ohne Rücksicht auf
    Groß-/Kleinschreibung). Komprimiert wird schon beim Hinzufügen.
  - `Auto` nutzt zstd, außer bei bereits komprimierten Formaten (`.ktx2`, `.ogg`, `.png`, `.jpg`, `.jpeg`) oder wenn
    die Datei nicht um mindestens 5 % schrumpft. `None` speichert immer roh, `Zstd` komprimiert immer.
  - `setCompressionLevel(1..22)` stellt die Stufe ein (Vorgabe 19). Die Entpackgeschwindigkeit hängt nicht von der Stufe ab.
  - `serialize()` sortiert die Einträge nach Pfad, gleiche Eingaben ergeben also byte-gleiche Archive.
    `write(target)` schreibt atomar.
- **Lesen:** Version 1 (immer roh) und 2. `Vfs::read` entpackt transparent; `VfsFileInfo::size` ist die entpackte Größe.
- **Beim Mounten wird geprüft:** Magic, Version, Inhaltsverzeichnis und Daten innerhalb der Datei, Pfad normalisiert,
  Hash passend, keine doppelten Pfade, keine unbekannten Flags, zstd nur ab Version 2, entpackte Größe höchstens
  `kPakMaxEntrySize` (2 GiB). Ein beschädigter zstd-Frame oder eine falsche `rawSize` fällt erst beim Lesen des
  Eintrags auf und liefert dann einen `Result`-Fehler.
- Der Leser (`src/PakArchive.hpp`) ist intern. Daten werden bei Bedarf gelesen, jeder Aufruf öffnet einen eigenen Stream.

### `MeshFile.hpp` – gekochte Meshes `.g7mesh` (ADR 0016)
```cpp
std::vector<u8> serializeMesh(const MeshData&);
Result<MeshData> deserializeMesh(std::span<const u8>, std::string_view debugName = "<memory>");
```
- Version 2 (M5): Header (`G7MS`, Zähler, AABB), Vertices im `Vertex`-Layout, u32-Indizes, Submeshes, Materialien, Bilder,
  **Kollisionsteile** (Format im Header-Kommentar; die Anzahl steht im früher reservierten Header-Feld). Version 1
  (ohne Kollision) wird weiter gelesen. Gleiche Daten ergeben gleiche Bytes. Der Cooker (`kCookerVersion` 2) kocht
  dadurch einmal alles neu.
- Beim Lesen wird geprüft: Zähler passen in die Datei (vor dem Anlegen von Speicher), Indizes im Vertex-Bereich,
  Submeshes im Index-Bereich, Material- und Bildverweise gültig, Alpha-Modus bekannt, keine Rest-Bytes.
- Bildverweise in gekochten Meshes sind VFS-Pfade ab der Wurzel (`textures/wood.png`), eingebettete Bytes leer.

### Kollision in Modellen – `COL_`-Knoten (M5, Vertrag mit welt und figuren)
- Mesh-Knoten, deren **Name mit `COL_` beginnt**, sind Kollisionsgeometrie: nicht gerendert, Material egal, nicht in
  `bounds`. Sie landen in `MeshData::collision`, im Modellraum wie die Vertices (Knoten-Transformationen eingerechnet).
- Hat ein Modell **mindestens einen** `COL_`-Knoten, kollidiert **nur** diese Geometrie; ohne `COL_` kollidiert das
  Render-Mesh (alle Dreiecke).
- Formen nach Namen:
  - `COL_BOX_*`: Box aus den Grenzen der Punkte **im Knotenraum** (ein gedrehter Knoten ergibt eine gedrehte Box), als
    8 Ecken einer Hülle gespeichert;
  - `COL_HULL_*`: konvexe Hülle der Punkte. Indizes werden ignoriert, nicht konvexe Punktwolken werden zur Hülle
    „gerundet“;
  - jedes andere `COL_*`: Dreiecksnetz (Positionen + Indizes).
- Hüllen und Boxen sind deutlich schneller als Dreiecksnetze.
- **Budget:** ≤ 200 Kollisions-Dreiecke je Haus (Hüllen zählen ihre Dreiecke); mehr ist kein Fehler.
- **Häuser (welt):** eine `COL_HULL_` je konvexem Baukörper (Erdgeschoss-Grundriss bis Traufe + Dachprisma);
  nicht konvexe Grundrisse in konvexe Teile zerlegt. Ersatzweise ist ein `COL_`-Dreiecksnetz der Grundform erlaubt.
  Dachüberstand, Auskragung, Gauben, Schornsteine, Balken und Fenster kollidieren nicht.
- **Figuren (figuren):** keine `COL_`-Knoten; Spieler und NPCs kollidieren als Kapsel des Controllers (physics.md).
  Mobs und Requisiten (Truhe, Bett) bekommen `COL_` wie Häuser.
- Viele `COL_`-Knoten je Datei (Zellen des Umlands) sind unkritisch: die Engine baut je Modell **eine** Form (bei
  mehreren Teilen eine statische Verbundform) und teilt sie zwischen allen Instanzen.

### `TextureData.hpp` – Texturen für den Upload (ADR 0016)
```cpp
enum class TextureFormat : u8 { RGBA8, BC7, BC5 };   // BC5: Normal-Map mit zwei Kanälen (X, Y)
struct TextureLevel { u32 width, height; std::vector<u8> data; };
struct TextureData { TextureFormat format; bool srgb; std::vector<TextureLevel> levels;   // levels[0] = volle Größe
                     u32 width() const; u32 height() const; };
TextureData textureFromImage(ImageData image, bool srgb = true);     // PNG/JPEG: RGBA8, eine Ebene
Result<TextureData> decodeKtx2(std::span<const u8>, std::string_view debugName = "<memory>");
bool hasKtx2Support();
```
- `decodeKtx2` liest die von `g7-cook` geschriebenen KTX2-Dateien mit libktx. UASTC wird nach **BC7**
  umgewandelt, zweikanalige Daten nach **BC5**; sRGB kommt aus der Transferfunktion der Datei. Die vollständige
  Mip-Kette wird übernommen. Unkomprimiertes RGBA8 wird direkt akzeptiert.
- **Normal-Maps (BC5)** enthalten nur X und Y. Der Shader rekonstruiert Z: `z = sqrt(max(0, 1 − x² − y²))`.
- Der eingebaute `AssetManager`-Lader für `Handle<TextureData>` liest `.ktx2` über `decodeKtx2` und alles andere über
  `decodeImage` + `textureFromImage`, als sRGB markiert. Wer eine **ungekochte** PNG-Normal-Map lädt, muss
  `srgb` anhand der Verwendung im Material zurücksetzen.
- **Ohne libktx** (Preset `nodeps`) meldet `decodeKtx2` einen Fehler, und `hasKtx2Support()` liefert `false`.

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
                     std::vector<std::string>* dependencies;   // vom Manager gesetzt, read() trägt ein
                     Result<std::vector<u8>> read(std::string_view) const;
                     std::string sibling(std::string_view relative) const;
                     std::optional<fs::Path> diskPath() const; };
template <class T> using Loader = std::function<Result<T>(const LoadContext&)>;
struct AssetManagerDesc { u32 workerThreads = 2;          // 0 = synchron in update()
                          bool hotReload = false; f64 pollSeconds = 0.5; };

class AssetManager {
public:
    AssetManager(const Vfs& vfs, AssetManagerDesc desc = {});
    template <class T> void registerLoader(Loader<T>);
    template <class T> Handle<T> load(std::string_view path);
    void update();  void waitAll();
    usize reload(std::string_view path);      // alle gecachten Typen des Pfads neu laden (asynchron)
    u32 checkForChanges(f64 now);             // Hot-Reload: geänderte lose Dateien neu laden
    void setHotReload(bool); bool hotReload() const;
    usize pendingCount() const;  usize cachedCount() const;
};
}
```
- **Ablauf:** `load` kehrt sofort zurück. Worker-Threads lesen die Datei über das `Vfs` und rufen den Lader des Typs
  auf. Erst **`update()` im Hauptthread** schaltet fertige Ladevorgänge auf `Ready` bzw. `Failed`. Ein Asset ändert
  sich also nie mitten in einem Frame oder Simulationsschritt, sondern erscheint frühestens im nächsten Frame.
  `waitAll()` wartet auf alle offenen Ladevorgänge und veröffentlicht sie (Ladebildschirm, Tests).
  Mit `workerThreads = 0` laufen die Lader synchron in `update()` (Werkzeuge, deterministische Tests).
  `update()` läuft jeden Frame und muss deshalb linear bleiben: Freigegebene Assets werden dort aus dem Cache
  entfernt, ihre Beobachtung (Hot-Reload) nur dann neu abgeglichen, wenn etwas freigegeben wurde (über eine
  Pfadmenge; der frühere Vergleich jeder Beobachtung mit jedem Cache-Eintrag kostete bei 5400 Modellen 80 ms je Frame).
- **Cache und Referenzzählung:** Der Schlüssel ist Typ plus normalisierter Pfad, ohne Rücksicht auf
  Groß-/Kleinschreibung. Solange ein Handle (oder ein laufender Ladevorgang) lebt, liefert `load` denselben Slot
  ohne neues Laden. Nach dem letzten Handle wird das Asset freigegeben, ein späteres `load` lädt neu.
  Auch ein fehlgeschlagener Slot bleibt bestehen, solange Handles darauf zeigen; `reload` kann ihn retten.
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
- **Gelöst – Hänger in Testläufen (2026-10-03):** Lokal und in der Windows-CI (Suite `cook`) hing ein Lauf
  gelegentlich bis zum Timeout. Die Ursache lag nicht im `AssetManager`, sondern in **libktx 4.4.2 / basisu**:
  `basisu::job_pool::~job_pool()` setzt `m_kill_flag`, ohne `m_mutex` zu halten, und ruft dann `notify_all()`.
  Ein Worker, der unter dem Mutex gerade „keine Arbeit, nicht beendet“ festgestellt hat, legt sich danach schlafen
  und verpasst das Signal (Lost-Wakeup). `join()` wartet dann ewig. Jede UASTC-Kodierung mit `threadCount` > 1
  erzeugt und zerstört so einen Pool.
  - **Nachweis:** Ein Stresstest mit Kodierungen einer 8×8-Textur hing nach 1000–2000 Läufen. Im Prozess blieben
    2 Threads ohne CPU-Zeit übrig: der Hauptthread im `join`, ein Worker im `wait`.
  - **Abhilfe** (`g7-cook`): basisu einthreadig (`threadCount = 1`, kein Pool). Stattdessen kodiert der Cooker
    mehrere Texturen parallel auf eigenen Workern (`std::jthread`). Die Reihenfolge der Ausgaben bleibt gleich,
    die Ergebnisse sind bytegleich. Die erste Kodierung im Prozess läuft serialisiert, weil libktx die
    basisu-Initialisierung mit einem einfachen `bool` absichert. Danach: 5000 Kodierungen ohne Hänger.
  - **Dauer**, voller KTX2-Kochlauf aller Quell-Assets (Release, 1749 Ausgaben, davon 56 KTX2-Bilder): vorher
    48 s (basisu-Pool), nur einthreadig 294–305 s, mit eigener Parallelisierung 54–57 s; inkrementell 1,4 s.
  - **Upstream-Hinweis:** In `basisu_enc.cpp` (`job_pool::~job_pool`) muss `m_kill_flag` unter `m_mutex` gesetzt
    werden. Prüfen, ob neuere KTX-Software- bzw. basis_universal-Versionen das beheben. Dann kann `threadCount`
    wieder steigen; eine Meldung an KTX-Software/basis_universal steht noch aus.
  - Die ctest-Timeouts je Suite (300 s, GPU 900 s; `G7_TEST_TIMEOUT`) bleiben als Schutz. Hängt künftig etwas:
    **Stacks aller Threads sichern** (Visual Studio „Anhalten“ bzw. `procdump -ma`, unter Linux
    `gdb -p <pid> -batch -ex "thread apply all bt"`).

### Hot-Reload (umgesetzt)
- **`reload(path)`** lädt alle gecachten Typen eines Pfads über die Worker neu. Erst `update()` tauscht die Daten aus:
  das **Handle bleibt gültig**, `version()` zählt hoch. Rohzeiger aus `get()` gelten deshalb nur bis zum nächsten
  `update()`. Ein fehlgeschlagener Reload (etwa eine halb gespeicherte Datei) **behält die alte Version** und meldet
  den Fehler; ein `Failed`-Asset wird `Ready`, sobald es lädt.
- **`checkForChanges(now)`** (höchstens alle `pollSeconds`): Die Worker merken sich vor dem Lesen Datei und
  Änderungszeit jeder losen Datei eines Assets, auch der über `LoadContext::read` gelesenen **Abhängigkeiten**.
  Ändert sich eine davon, wird das Asset einmal neu geladen. Inhalte von `.g7pak`-Archiven werden nicht überwacht,
  neue Dateien in Ordner-Mounts erfordern `Vfs::rescan` (kein automatisches Scannen). Externe glTF-Puffer
  (`.bin`, von fastgltf direkt gelesen) zählen noch nicht als Abhängigkeit.
- **Engine:** `[assets] hot_reload` (Standard an im Debug-, aus im Release-Build) ruft pro Frame
  `checkForChanges` vor `update()`. Danach lädt `refreshReloadedModels` jedes Modell neu auf die GPU, dessen Mesh
  oder Bild eine neue Version hat; alle Instanzen nutzen es sofort, ihre Bounds werden aktualisiert. Shader laden
  wie bisher über die `ShaderLibrary` neu (`[render] shader_hot_reload`), Skripte (ab M7) nutzen dieselben Handles.

### Engine-Anbindung (umgesetzt) – `runtime/AssetMounts.hpp`, `Engine`
```cpp
struct MountSpec { fs::Path source; i32 priority; std::string mountPoint; };
Result<std::vector<MountSpec>> assetMounts(const Config&, const fs::Path& gameDir, const fs::Path& devRoot);
std::vector<std::string> imageCandidates(std::string_view meshPath, std::string_view uri);  // Wurzel, dann relativ
std::string vfsSibling(std::string_view base, std::string_view relative);                   // löst . und .. auf
// Engine: asset::Vfs& vfs(); asset::AssetManager& assets(); const LoadedModel* model(std::string_view vfsPath) const;
```
- `Engine` besitzt `Vfs` und `AssetManager` (der Manager wird vor dem VFS zerstört) und ruft `update()` einmal pro
  Frame im Hauptthread.
- **Mounts** aus `[assets]` in `engine.toml`: `dev_mounts = true` mountet in Entwicklungs-Builds (CMake-Option
  `G7_DEV_ASSETS`, Standard an) `<repo>/assets/source` (Priorität 0) und, falls vorhanden, `<repo>/assets/cooked`
  (Priorität 10 – Gekochtes gewinnt) sowie jedes Archiv `<repo>/assets/cooked/*.g7pak` (Priorität 11, z. B. `data.g7pak`
  aus `g7-cook --pack`; ein Ordner-Mount zeigt ein Archiv nur als Datei). Dazu beliebige `[[assets.mount]]` mit `path` (relativ zum Spielordner),
  `priority` und `mount_point`; `path = "data/*.g7pak"` mountet alle Archive des Ordners in Namensreihenfolge.
  Fehlende Ordner werden mit Warnung übersprungen.
- **`--scene`/`--view-mesh`** nehmen einen VFS-Pfad (`testscene/scene.toml`). Liegt die Datei nur auf der Festplatte,
  wird ihr Ordner mit Priorität 1000 unter `local/` gemountet (`local/Lantern.glb`). Modellpfade einer Szene sind
  relativ zur Szenendatei.
- **Gekochtes gewinnt:** Für ein Modell `x.glb`/`x.gltf` lädt die Engine `x.g7mesh`, wenn es im VFS liegt
  (`preferCooked`) – Szenen und `--view-mesh` dürfen Quelldateien nennen und laufen auch nur mit `data.g7pak`.
- **Laden** in einem Durchgang: alle Meshes parallel auf den Workern (`Handle<MeshData>`), dann deren externe Bilder
  (`Handle<TextureData>`: KTX2 → BC7/BC5, PNG/JPEG → RGBA8; gleiche Pfade teilt der Cache), dann Upload im Hauptthread. Bild-URIs werden zuerst als Pfad
  ab der VFS-Wurzel versucht (gekochte `.g7mesh`), dann relativ zum Mesh (glTF). `MaterialSet::create` bekommt dafür
  eine `ImageLookup`-Funktion; nicht gefundene Bilder ergeben neutrale Ersatztexturen mit Warnung.
- `LoadedModel` behält seine Handles (Grundlage für Hot-Reload).

## Bestandteile (geplant)
- **Weitere Loader** pro Typ (`Texture`, `Skeleton`, `AnimationClip`, `Sound`, `WorldData`, `Material`) mit den
  jeweiligen Modulen. Eingetragen werden sie vom höheren Modul über `registerLoader`, `asset` kennt sie nicht.
