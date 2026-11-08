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
- **Detailstufen statischer Modelle:** siehe unten „Detailstufen (LOD) statischer Modelle“.
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

### `FontFile.hpp` – gekochte Schriften `.g7font` (M14, ADR 0020)
```cpp
struct FontGlyph { u32 codepoint; f32 advance, planeLeft, planeBottom, planeRight, planeTop; u16 atlasX, atlasY, atlasWidth, atlasHeight; };
struct FontData { FontKind kind; f32 emPixels, distanceRange, ascender, descender, lineGap; u32 atlasWidth, atlasHeight;
                  std::vector<u8> pixels; std::vector<FontGlyph> glyphs; std::vector<FontKerning> kerning; };
std::vector<u8> serializeFont(const FontData&);
Result<FontData> deserializeFont(std::span<const u8>, std::string_view debugName = "<memory>");
```
- Version 1: Kopf (`G7FN`, Art msdf/sdf, Metriken in em), Glyphen nach Codepoint sortiert (Ebene in em relativ zum
  Stift auf der Grundlinie, +Y oben; Atlas-Rechteck in Pixeln, oben links), Kerning-Paare sortiert, Atlas RGBA8 mit zstd.
  Geschrieben von `g7-cook --font`, gelesen von `ui::Font`; geprüft werden Größen, Sortierung und Atlas-Grenzen.

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

### `SkinnedModel.hpp` – Figuren und Animationen aus glTF (M6 Teil A, ADR 0019)
```cpp
namespace g7::asset {
inline constexpr u32 kMaxBones = 128;
struct SkeletonData { names, parents (-1 = Wurzel), translations, rotations, scales (Ruhepose lokal), rootParent;
                      i32 find(name) const; };   // Eltern vor Kindern
struct SkinnedPartData { node, role, lod; vertices; joints (4 x u16 je Vertex); weights (Summe 1); indices; submeshes;
                         morphs (Positions-/Normalen-Versätze je Vertex) };
struct SkinnedModelData { skeleton; inverseBind (je Knochen); parts; materials; images; bounds;
                          partsForLod(lod); lodCount(); };
struct ClipEvent { f32 time; std::string name; };
struct TrackData { bone; Path { Translation, Rotation, Scale }; step; times; values (xyz bzw. Quaternion xyzw) };
struct ClipData { name; duration; tracks; events; bool loops() const; };   // loops: Aktion beginnt mit "s_"
struct AnimationSetData { skeleton; clips; const ClipData* find(name) const; };
Result<SkinnedModelData> loadSkinnedGltf(bytes, baseDir, name);
Result<AnimationSetData> loadAnimationGltf(bytes, baseDir, name);
Result<void> applyClipEvents(AnimationSetData&, toml, source);     // <set>.events.toml (Vertrag §3)
}
```
- **Skelett:** aus dem ersten Skin.
  - Gelenke nach Tiefe sortiert (Eltern zuerst), Eltern = nächster Vorfahre, der Gelenk ist.
  - `rootParent` = Transformation der Knoten über der Wurzel (Armatur).
  - Die inversen Bind-Matrizen werden auf diese Reihenfolge umsortiert. Test: globale Ruhepose × inverse
    Bind-Matrix = Einheit für alle 60 Knochen des Referenz-Rigs.
- **Teile:** jeder Knoten mit Mesh und Skin. Nach glTF gilt dessen Knoten-Transformation für Skins nicht.
  - Rolle und LOD aus `<rolle>_lod<n>` (Vertrag §2.2). `partsForLod(n)` wählt je Rolle Stufe n bzw. die nächste
    darunter.
  - JOINTS_0 und WEIGHTS_0 sind Pflicht; Gewichte werden auf 1 normiert.
  - Morph-Targets in Dateireihenfolge, die Namen gibt die feste Liste aus §6.1.
- **Clips:** je glTF-Animation, Kanäle per Knotenname auf Knochen.
  - Linear bzw. Schritt; kubische Splines werden abgelehnt, Gewichts-Animationen übersprungen.
  - `duration` = letzter Schlüssel.
- **Events:** Der `AssetManager` liest `<set>.events.toml` neben dem Set automatisch, sofern vorhanden (Hot-Reload
  beobachtet sie mit). Fehler: unbekannter Clip, Frame außerhalb (Schleifen: < letzter Frame), nicht
  aufsteigend, fehlende Felder, `speed` ≤ 0.
- **Eigengeschwindigkeit** (M6, additiv in Version 1): optionales Feld `speed` (m/s) je Clip →
  `ClipData::speed` (0 = unbekannt); auch ohne `events`. Liefert figuren (`gothar-chargen report`).
- **Zusammenbau-Daten** (M6 D2): `SkinnedPartData::primitives` nennt je glTF-Primitive, wo seine Vertices und
  Dreiecke im Teil liegen (Dreiecke sind nach Material gruppiert, die eines Primitives bleiben zusammen);
  `SkinnedModelData::assembly` enthält `asset.extras.gothar` der Figuren-Teile (`neck`, `falloff`, `covers`,
  `hides`; Format v1, §6.2), gelesen mit nlohmann-json (privat, ADR 0017). Zusammenbau: `FigureAssembly.hpp`
  (`animation.md` „Figuren zur Laufzeit zusammensetzen“).
- **Lader im `AssetManager`:** `SkinnedModelData` aus `.g7skin` oder glTF/GLB, `AnimationSetData` aus `.g7anim` oder
  glTF/GLB (ADR 0025).
  - `events.toml` liest der Lader in beiden Fällen neben dem Set.
  - Ist eine gekochte Datei von anderer Version oder beschädigt, gibt es eine Warnung, und der Lader nimmt das
    `.glb` daneben. Ohne `.glb` ist es ein Fehler.
  - Welche Datei geladen wird, entscheidet die Engine mit `preferCookedFigure(vfs, path, skin)`
    (`runtime/AssetMounts.hpp`): `x.glb` wird zu `x.g7skin` bzw. `x.g7anim`, wenn die Datei im VFS liegt.
    Manifeste, Skripte und `animgraph.toml` nennen weiter `.glb`.

### `SkinFile.hpp` – gekochte Figuren `.g7skin` (M17, ADR 0025, Plan Teil B)
```cpp
namespace g7::asset {
inline constexpr char kSkinMagic[4] = {'G', '7', 'S', 'K'};
inline constexpr u32 kSkinVersion = 1;
std::vector<u8> serializeSkin(const SkinnedModelData&);
Result<SkinnedModelData> deserializeSkin(bytes, debugName);
}
```
- Bildet `SkinnedModelData` 1:1 ab: Skelett, inverse Bind-Matrizen, Bounds, Materialien (mit Wind), Bilder, Teile
  (Vertices, Joints, Gewichte, Indizes, Submeshes, Morphs, Primitive-Tabelle) und die Zusammenbau-Daten
  (`extras.gothar`). Den Aufbau beschreibt der Kommentar im Header.
- Die Reihenfolge der Vertices und Dreiecke bleibt wie aus glTF, Bildverweise bleiben relativ zum Teil. So
  ergibt `assembleFigure` aus gekochten Teilen dieselbe Figur (Test).
- Der Leser prüft Anzahlen gegen die Dateigröße vor dem Anlegen, Eltern vor Kindern, Joints, Indizes,
  Submesh-/Primitive-Bereiche, Bildverweise und Reste. Eine andere Version ist ein Fehler mit dem Hinweis
  „cook again“.

### `AnimFile.hpp` – gekochte Clip-Sets `.g7anim` (M17, ADR 0025, Plan Teil C)
```cpp
namespace g7::asset {
inline constexpr char kAnimMagic[4] = {'G', '7', 'A', 'N'};
inline constexpr u32 kAnimVersion = 1;
std::vector<u8> serializeAnim(const AnimationSetData&);
Result<AnimationSetData> deserializeAnim(bytes, debugName);
}
```
- Skelett des Sets und Clips mit Spuren (Knochenname, Kanal, Schritt/linear, Zeiten, Werte als f32).
- Events und Eigengeschwindigkeiten speichert die Datei nicht. `<set>.events.toml` bleibt eine eigene Datei und
  wird nach dem Laden angewendet (Entscheidung 2, Test).
- Der Leser prüft Anzahlen, Eltern vor Kindern, bekannte Kanäle, steigende Zeiten und Reste.

### Detailstufen (LOD) statischer Modelle (Vertrag mit welt und figuren, 2026-10-04)
- **Knoten:** Ein `.glb` enthält bis zu drei Render-Stufen mit **gleichem Ursprung**: Stufe 0 als `<name>` oder
  `<name>_lod0`, gröber `<name>_lod1`, `<name>_lod2` (`lodLevel`, `isCoarserLod`). Materialien werden geteilt; jeder
  Knoten darf mehrere Teile/Materialien haben. Stufen über 2 lässt der Loader weg.
- **Kollision:** `COL_*` gibt es **einmal**, unabhängig von der Stufe. Ohne `COL_` kollidiert nur Stufe 0.
- **Ohne `_lod`-Knoten** ist alles wie zuvor (Schloss, Kirche, Mauer, Mobs, Testwelt).
- **Daten:** `MeshData`-Submeshes tragen ihre Stufe (`Submesh::lod`, nach Stufe sortiert), `maxLod(mesh)`; `.g7mesh`
  v3 speichert sie (v1/v2 werden weiter gelesen, ihre Submeshes sind Stufe 0); `g7-cook` Version 3 kocht neu.
- **Auswahl** (render/engine): je Vob nach der Entfernung Kamera – Mitte der Bounds, `render::selectLod` mit
  Hysterese: Stufe 1 ab `lod1_distance` (60 m), Stufe 2 ab `lod2_distance` (150 m), zurück erst `lod_hysteresis`
  (10 %) darunter (engine.toml `[render]`). Hat ein Modell weniger Stufen, zeichnet es seine gröbste (`Mesh::inLod`).
  Der Schattenpass nimmt die Stufe des Hauptpasses. Alle Stufen liegen in einem Arena-Mesh; die Auswahl wählt nur die
  Submeshes, das Multi-Draw-Batching bleibt. Kleine Dinge (Props, Begrenzungsradius unter `lod_small_radius` 1,5 m)
  wechseln früher: ihre Schwellen mal `lod_small_scale` 0,35, also Stufe 1 ab 21 m, Stufe 2 ab 52 m (Phase 3, E4;
  welts Props mit `_lod1` seit #336). `size_cull` gilt für sie wie für alle Deko-Modelle.
- **Prüfen:** `--lod=0|1|2` bzw. Debug-UI (Engine → Render → LOD) erzwingen eine Stufe; dort und im Benchmark-Log
  steht, wie viele Objekte je Stufe gezeichnet wurden. Das Debug-Overlay (F2) umrandet Modelle mit Stufen nach der
  gezeichneten: grün 0, gelb 1, rot 2.

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

### Vobs aus Blender – `VOB_`-Knoten (M16 Teil F, ADR 0023; Vertrag, betrifft welt und figuren nur bei Nutzung)
- Leere Knoten (Blender „Empty“, ohne Mesh), deren **Name mit `VOB_` beginnt**, setzt der Editor beim Platzieren
  des Modells als **Kind-Vobs** des Modell-Vobs ein. Gerendert und kollidiert werden sie nicht.
- Name: `VOB_<TYP>_<NAME>`; der Vob heißt `<NAME>` in Großbuchstaben. Blenders Endung `.001` usw. fällt weg.
  - `VOB_LIGHT_<NAME>`: Licht. Die Reichweite ist die größte Skalierung des Empty × 5 m (Skalierung 1 → 5 m).
  - `VOB_START_<NAME>`: Startpunkt; die Drehung des Empty ist die Blickrichtung.
  - `VOB_TRIGGER_<NAME>`: Box-Trigger. Die Halbachsen sind die Skalierung des Empty (ein Würfel-Empty der Größe 1
    hat Halbachsen 1 m). Funktionen und Levelwechsel setzt man danach im Inspektor.
  - Andere Typen meldet der Editor (`Editor::importWarnings`) und lässt sie weg.
- Lage und Drehung kommen aus dem Knoten im Modellraum (Eltern-Knoten eingerechnet), die Skalierung bemisst nur Reichweite
  bzw. Volumen. Lesen: `asset::loadGltfMarkers` (nur `.glb`; gekochte `.g7mesh` haben keine leeren Knoten).

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

## Plan M17: gekochte Figuren und Animationen (`.g7skin`/`.g7anim`)
Stand: **Plan freigegeben** (Projektinhaber, 2026-11-08; alle Vorschläge übernommen, ADR 0025 angenommen). Roadmap M17:
„spätestens vor dem ersten reinen `.g7pak`-Release“. ADR 0019 hat das eigene Format als Folgearbeit vorgesehen. Die
Entscheidungen stehen am Ende, nummeriert. Reihenfolge: Teil 0 nach dem Merge von #384, danach A–F, je Teil ein PR.
**Teil 0 umgesetzt** (`fix/m17-package-figures`): glTF-Kopien im Archiv, strenger Paket-Test, Figurenbilder über `.ktx2`.

### Ausgangslage (gemessen bzw. geprüft am 2026-11-08)
- **Laufzeit:** Figuren entstehen zur Laufzeit aus Teilen. `figures/<name>.figure.toml` nennt die Teile
  (`parts/**/*.glb`), `Engine::assembleFigureParts` lädt sie über den `AssetManager` (`SkinnedModelData`, im Cache)
  und `asset::assembleFigure` setzt sie zusammen (Hals, verdeckte Körperdreiecke, Paletten). Clips liegen in
  `anims/human/<set>.glb` (16 Sets) mit `<set>.events.toml`, Tiere in `monsters/<art>/rig/*.glb` und
  `monsters/<art>/anims/*.glb`. Gelesen wird alles mit fastgltf aus losem glTF.
- **Mengen:** 311 Teile mit 61 MB, 16 Sets mit 15 MB, Monster mit 8,5 MB (nur `.glb`). Die Teile haben keine
  eingebetteten Bilder, ihre Texturen liegen unter `characters/textures/`.
- **Cooker:** `g7-cook` macht aus einem geskinnten `.glb` heute ein statisches `.g7mesh` ohne Skin. Sets ohne Mesh
  überspringt er mit „no mesh in this glTF, skipped“. Die `.glb` selbst kommen nicht ins Archiv.
- **Befund zum Paket (#384):** Das reine Paket lädt **keine Figuren**. Ein Probelauf aus dem entpackten ZIP
  (`--smoke-test --world=testworld/camp.g7world --exec=camp_people()`) meldet
  `cannot load 'characters/parts/body_m_average/body.glb': file not found in VFS`. Danach läuft er ohne Spielerfigur,
  und `insert_npc` scheitert für alle Lager-NPCs. Der Paket-Test ist trotzdem grün, denn das sind nur `WARN`, ein
  `ERROR` kommt nicht vor, und der Rückgabewert ist 0. release.md Teil C setzt dagegen voraus, dass der Test das
  Laden der Figuren belegt.
- **Ladezeit:**
  - Leonberg (`docs/perf/2026-11-07-erste-messung.md`): Die Phase „figures and clips“ braucht 1420 ms von 3850 ms bis
    zum ersten Bild. Das ist der größte Posten.
  - Lager (Release, warmer Datei-Cache): Der Held mit allen 16 Sets braucht ~68 ms, jede weitere Figur 23–60 ms je
    nach Anzahl neuer Teile.
  - Wie sich das auf glTF-Parsen, Zusammenbau, Texturen und GPU-Upload verteilt, ist noch nicht gemessen (Teil A).

### Ziel
Ein reines `.g7pak` enthält Figurenteile und Clips in einem eigenen Binärformat. Die Engine lädt sie ohne glTF-Parser,
der Zusammenbau zur Laufzeit bleibt unverändert. In der Entwicklung bleibt glTF die Quelle und der Rückfall.
`animation` und `render` sehen dieselben CPU-Strukturen wie heute (`SkinnedModelData`, `AnimationSetData`).

### Teil 0 – Sofort: Figuren überhaupt im Paket (klein, eigener Fix-PR nach #384)
- `g7-cook` kopiert geskinnte `.glb` und reine Clip-Sets **zusätzlich unverändert** ins Archiv. Sein `.g7mesh` behält
  ein geskinntes `.glb` vorerst. Der Loader liest glTF schon heute aus Bytes über das VFS, die Teile sind
  eigenständig (keine externen `.bin`).
- Der Paket-Test wird streng:
  - `cannot load` und Skriptfehler (`[sol2]`, `--exec ...:`) gelten als Fehler.
  - Mindestanzahl erfolgreich gebauter Figuren (`figure ...: 60 bones` im Log, Lager: Held + 6).
- Teil 0 macht den Release mit glTF lauffähig. Teil D ersetzt die Kopie durch `.g7skin`/`.g7anim`.

### Teil A – Messen, wohin die 1,4 s gehen
- Tracy-Zonen (Preset `profile`, #387) in `AssetManager` und `EngineFigure`: `skin parse (glTF)`, `anim parse (glTF)`,
  `figure assemble`, `figure textures`, `figure upload`. Lauf `--perf=markt,markt_voll` auf ruhiger Maschine.
- Ergebnis in `docs/perf/` mit Anteilen je Zone. Erst damit wird der Gewinn belastbar (siehe „Erwarteter Gewinn“).
- **Umgesetzt** (2026-11-08):
  - Die Zonen heißen `figure load`, `figure parts`, `figure clip sets`, `figure assemble`,
    `figure images` und `figure upload`.
  - Engine und `--perf` schreiben die Summen zusätzlich als Ladephasen `figures: …`. Das gilt in jedem Build, nicht
    nur im Preset `profile`.
  - Ergebnis in `docs/perf/2026-11-08-figuren-laden.md`: In Leonberg (25 Figuren, ~1,3 s) braucht glTF-Parsen
    ~720–770 ms (Clip-Sets ~460, Teile ~260), Zusammenbau ~30 ms, Bilder ~200 ms und Upload ~160 ms.

### Teil B – Format `.g7skin` (asset, `SkinFile.hpp`)
- **Umgesetzt** (2026-11-08): `serializeSkin`/`deserializeSkin`, Tests in `tests/asset/test_skin_file.cpp`.
- Eine Datei je geskinntem `.glb` (Teil, Monster-Rig, Platzhalter). Sie bildet `SkinnedModelData` 1:1 ab: Skelett
  (Namen, Eltern, Ruhepose, `rootParent`), inverse Bind-Matrizen, Teile (Vertices, Joints u16×4, Gewichte, Indizes,
  Submeshes, Morph-Targets mit Namen, **Primitive-Tabelle**), Materialien und Bildverweise (VFS-Pfade ab der Wurzel
  wie bei `.g7mesh`), Bounds und die Zusammenbau-Daten aus `asset.extras.gothar` (Hals-Ringe, Falloff, `covers`,
  `hides`, `coversBody`).
- **Pflicht:** Vertex- und Dreiecksreihenfolge exakt wie aus glTF geladen (kein Umsortieren, kein Zusammenfassen). Die
  Zusammenbau-Daten zeigen per [Primitive, Vertex] in diese Reihenfolge.
- Aufbau nach ADR 0025: Kopf `G7SK`, Version u32, little-endian, Abschnitte mit Länge. Der Leser prüft Grenzen,
  Indizes (Joint < Knochenanzahl, Index < Vertexanzahl), Reste und Version. Fehler kommen als `Result`, ohne Absturz.
- `writeSkinFile(const SkinnedModelData&) -> std::vector<u8>`, `readSkinFile(bytes, name) -> Result<SkinnedModelData>`.

### Teil C – Format `.g7anim` (asset, `AnimFile.hpp`)
- **Umgesetzt** (2026-11-08): `serializeAnim`/`deserializeAnim`, Tests in `tests/asset/test_anim_file.cpp`.
- Eine Datei je Set-`.glb`: Skelett des Sets, Clips mit Name, Dauer und Spuren (Knochenname, Kanal, Schritt/linear,
  Zeiten, Werte). Version 1 speichert f32 unverändert, das Archiv komprimiert mit zstd (Frage 3).
- **`events.toml` bleibt eine eigene Datei** (Frage 2). Der Loader liest sie neben dem Set wie heute
  (`applyClipEvents`), auch neben `.g7anim`. Hot-Reload der Events funktioniert so weiter.
- `writeAnimFile`, `readAnimFile` wie in Teil B.

### Teil D – Cooker
- **Umgesetzt** (2026-11-09):
  - `cookSkinned` in `g7-cook` (`kCookerVersion` 7) erzeugt `.g7skin`/`.g7anim` statt `.g7mesh` und der
    glTF-Kopie aus Teil 0.
  - Der Bericht zählt `skins` und `anims`.
  - Tests in `tests/cook/test_cook.cpp`.
- Ein `.glb` mit Skin ergibt `.g7skin`, eines mit Animationen `.g7anim`. Eine Datei mit beidem ergibt beide.
- Das statische `.g7mesh` eines geskinnten `.glb` entfällt (Frage 5). Die Kopie aus Teil 0 entfällt ebenfalls.
- Bilder der Materialien laufen wie bei `.g7mesh` durch die Textur-Erkennung (KTX2, Farbe/Normal/Daten).
- `kCookerVersion` 4 → 5: Das Manifest kocht beim ersten Lauf alles neu.
- Prüfung beim Kochen: Fehler, die heute erst zur Laufzeit kommen (mehr als 128 Knochen, fehlende JOINTS/WEIGHTS,
  kubische Splines), lassen den Kochlauf scheitern. Die figuren-CI (`gothar-chargen validate`) prüft dasselbe schon
  vorher.

### Teil E – Laden, Rückfall, Hot-Reload (asset, runtime)
- **Umgesetzt** (2026-11-09):
  - Lader für `.g7skin`/`.g7anim` mit Rückfall auf das `.glb`.
  - `preferCookedFigure` an allen drei Ladestellen in `EngineFigure` (Teile, Figuren-`.glb`, Clip-Sets).
  - Tests in `test_skin_file.cpp` und `test_asset_mounts.cpp`.
- Die Loader für `SkinnedModelData`/`AnimationSetData` erkennen `.g7skin`/`.g7anim`. „Gekochtes gewinnt“ wie bei
  `.g7mesh`: Für `x.glb` lädt der Manager `x.g7skin` bzw. `x.g7anim`, wenn die Datei im VFS liegt. Skripte, Manifeste
  und `animgraph.toml` nennen weiter `.glb`. **Kein Pfad in Inhalten ändert sich.**
- **Rückfall auf glTF:**
  - Fehlt die gekochte Datei, lädt der Manager das `.glb` (Entwicklung, Tests, figuren-Arbeitsablauf).
  - Hat eine gekochte Datei eine falsche Version oder ist beschädigt, gibt es eine Warnung, und der Manager lädt das
    `.glb`, falls vorhanden. Sonst ist es ein Fehler.
- **Zusammenbau:** `assembleFigure` bekommt dieselben `SkinnedModelData`. Test: gekochte und glTF-Teile ergeben eine
  byte-gleiche Figur.
- **Hot-Reload:** wie bei `.g7mesh`. Lose `.glb` werden beobachtet und neu geladen, solange keine gekochte Datei sie
  verdeckt. Im Ordner `assets/cooked` lädt eine neu gekochte `.g7skin` neu (Änderungszeit). `events.toml` wird
  weiter als Abhängigkeit beobachtet.
- `animation` und `render` bleiben unverändert. Ein GPU-fertiges Vertex-Layout (direkter Upload) wäre ein späterer
  Schritt und gehört nicht in diesen Plan.

### Teil F – Paket-Integration (mit #384)
- Ins Archiv kommen `.g7skin`, `.g7anim`, `events.toml` und `figure.toml` (kopiert). Die `.glb`-Kopien aus Teil 0
  fallen weg (die Größe des ZIP misst der PR).
- Der strenge Paket-Test aus Teil 0 belegt Figuren und Clips aus dem Archiv. Dazu kommt ein Lauf mit
  `--exec=camp_people()` und einem Kampf-Clip (Events).
- Danach: release.md Teil D abschließen, Roadmap-Punkt abhaken, Messung wiederholen (`--perf`, Ladephasen).
- **Umgesetzt** (2026-11-09):
  - Der Paket-Test (`make_release`) lädt die Lager-Figuren aus `.g7skin`/`.g7anim` in `data.g7pak`, ohne `.glb`.
  - Der Leser liest Arrays am Stück.
  - Messung im Nachtrag von `docs/perf/2026-11-08-figuren-laden.md`.
  - release.md und Roadmap sind nachgeführt.

### Tests
- **asset:**
  - Rundreise `.g7skin` für einen Kopf mit Morphs, einen Körper mit Hals-Daten und ein Kleidungsstück mit `covers`.
  - Rundreise `.g7anim` mit Schritt- und linearen Spuren, danach `applyClipEvents`.
  - Beschädigte, abgeschnittene Dateien und Dateien mit falscher Version ergeben Fehler, keinen Absturz.
  - `assembleFigure` aus gekochten Teilen ist byte-gleich zu dem aus glTF (farmer, guard).
- **cook:** Ein Figurenteil und ein Set kochen zu `.g7skin`/`.g7anim`, kein `.g7mesh` für Skins, Manifest
  inkrementell, Fehler bei kubischer Spline.
- **AssetManager:** „Gekochtes gewinnt“ und der Rückfall auf `.glb` bei falscher Version.
- **runtime:** Das Lager mit NPCs aus einem VFS nur mit `.g7pak` (Temp-Ordner) läuft, alle Figuren werden gebaut.
- **package:** strenger Test (Teil 0/F).
- Animationsergebnisse (Pose nach t Sekunden) für gekochte und glTF-Sets gleich.

### Erwarteter Ladezeit-Gewinn
- **Schätzung, bis Teil A misst:**
  - Der glTF-Weg (JSON, Accessoren, Umrechnung je Vertex und je Schlüssel) wird zu einem Einlesen fester Arrays. Für
    diesen Anteil sind 5–10× realistisch.
  - Zusammenbau, Texturen und GPU-Upload bleiben gleich.
  - Macht das Parsen die Hälfte der 1,4 s aus, spart Leonberg ~0,5–0,7 s (≈ 15 % der Zeit bis zum ersten Bild).
  - Mit mehr Bewohnern und mehr Teilen wächst der Anteil.
- **Belegt wird der Gewinn** mit `--perf` (Ladephasen) vorher und nachher, im selben Bericht wie
  `2026-11-07-erste-messung.md`.

### Vertrag mit figuren
- **Unverändert:** Figuren-`.glb` (Teile, Rigs, Sets), `figure.toml`, `events.toml`, Knoten-/LOD-Namen (§2.2),
  Morph-Namen (§6.1) und `asset.extras.gothar` (§6.2). figuren liefert weiter glTF, gekocht wird nur im Cooker.
- **Neu für figuren sichtbar:**
  - Ein Teil, das zur Laufzeit scheitern würde, lässt jetzt schon `g7-cook` scheitern (Teil D).
  - Ändert figuren das Format von `asset.extras.gothar` oder die Morph-Liste, muss der Cooker mitziehen (Version
    erhöhen). Das läuft wie bisher über die Absprache zu §6.2.
- Die gebauten `figures/<name>.glb` (`g7_figures`) braucht die Laufzeit nicht, nur Platzhalter und Tests. Sie werden
  gekocht wie jedes geskinnte `.glb`.

### Reihenfolge und Aufwand
| Teil | Inhalt | Aufwand |
|---|---|---|
| 0 | Figuren-glTF ins Paket, strenger Paket-Test | ½ Tag |
| A | Messung mit Zonen | ½ Tag |
| B | `.g7skin` lesen/schreiben + Tests | 1 Tag |
| C | `.g7anim` lesen/schreiben + Tests | ½ Tag |
| D | Cooker | ½–1 Tag |
| E | Loader, Rückfall, Hot-Reload, Zusammenbau-Test | 1 Tag |
| F | Paket, Doku, Messung | ½ Tag |

### Entscheidungen des Projektinhabers (2026-11-08, alle nach Vorschlag)
1. **Teil 0 jetzt?** Das Paket aus #384 lädt heute keine Figuren, der Test merkt es nicht. **Entschieden:** ja, als
   kleiner Fix-PR direkt nach #384: glTF ins Archiv, Test streng. Dann ist jeder Release bis Teil F lauffähig.
2. **`events.toml` beim Kochen in `.g7anim` einbauen oder getrennt lassen?** **Entschieden:** getrennt. Der Vertrag
   bleibt gleich, Hot-Reload der Events bleibt, der Unterschied in der Ladezeit ist klein (TOML je Set).
3. **Schlüssel komprimieren (Quantisierung, z. B. Quaternion 3×16 bit)?** **Entschieden:** nicht in Version 1. f32 und
   zstd im Archiv, die Sets haben heute 15 MB. Quantisierung kommt später mit eigener Version, wenn Größe oder
   Speicher drücken.
4. **Ganze Figuren (`figure.toml`) vorab zusammenbauen und kochen?** **Entschieden:** nein. Der Zusammenbau zur
   Laufzeit bleibt (Rüstung/Kleidung tauschen, Cache der Teile). Erst wenn Teil A ihn als großen Posten zeigt, wird
   über einen Cache der fertigen Figuren entschieden.
5. **Statisches `.g7mesh` für geskinnte `.glb` weglassen?** **Entschieden:** ja, nur noch `.g7skin`, sofern Teil D
   prüft, dass nichts ein Figurenteil statisch lädt (heute nicht der Fall). Das spart Platz im Archiv.
6. **Morph-Targets dicht oder dünn speichern?** **Entschieden:** dicht in Version 1 (wie geladen). Dünn (nur bewegte
   Vertices) später, falls Köpfe im Archiv groß werden.
7. **Reihenfolge:** **Entschieden:** 0 sofort. A–F danach am Stück, unabhängig vom Messergebnis, weil die Roadmap das
   Format vor dem reinen `.g7pak`-Release verlangt. Teil A legt nur fest, ob danach noch der Zusammenbau oder der
   Upload dran ist.
