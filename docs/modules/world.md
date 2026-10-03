# world

**Zweck:** Die Spielwelt: Entities (Vobs) und Komponenten, Weltdateien, Spielzeit, Umgebung
(Tag/Nacht, Wetter), Zonen, Wegnetz-Daten, Weltwechsel.

## Szene (ADR 0005: EnTT)
### Umgesetzt (M4) – `Components.hpp`, `Scene.hpp`
```cpp
namespace g7::world {
struct VobId { u64 value; bool valid() const; bool runtime() const; };   // 0 = keine; >= kRuntimeVobIdBase = Laufzeit
struct Vob { VobId id; StringId name; };   struct WorldTransform { Mat4 matrix; };   // lokal: g7::Transform
struct VobDesc { StringId name; Transform transform; VobId parent; VobId id /*fest beim Laden*/; bool runtime; };
class Scene {   // entt::registry privat (ADR 0005)
    Result<entt::entity> spawnVob(const VobDesc&);  void destroyVob(entt::entity);   // mit allen Nachkommen
    bool valid(e); usize vobCount(); entt::entity findById(VobId); entt::entity findByName(StringId); VobId idOf(e);
    template <C> C& set(e, C); const C* get(e) const; C* get(e); bool has(e); void remove(e);
    template <C..., Fn> void each(Fn);                     // fn(entity, C&...)
    void setTransform(e, const Transform& local);  Result<void> setParent(child, parent /*null = Wurzel*/);
    entt::entity parent(e); std::vector<entt::entity> children(e); Mat4 worldMatrix(e);
    void updateTransforms();                                // nur geänderte Teilbäume
    u64 nextVobId(); Result<void> setNextVobId(u64);        // steigt nur
    u64 nextRuntimeVobId(); Result<void> setNextRuntimeVobId(u64);   // für den Spielstand (M15)
};
}
```
- **IDs:** Welt-Vobs aus `nextVobId` (ab 1), Laufzeit-Vobs aus einem eigenen Zähler ab `kRuntimeVobIdBase`. Gelöschte IDs
  kommen nie wieder. Beim Laden kann `VobDesc::id` eine feste ID setzen; doppelte IDs, Welt-IDs im Laufzeitbereich,
  feste IDs für Laufzeit-Vobs und unbekannte Eltern sind **Fehler** (`Result`, Daten). Die Zähler stehen danach
  mindestens bei max(ID) + 1; `setNextVobId`/`setNextRuntimeVobId` lehnen kleinere Werte ab.
- **Komponenten:** `Vob`, `Transform` (lokal), `WorldTransform` hat jeder Vob; andere Module setzen eigene
  Komponenten über `set`/`get`/`each`. Transformänderungen nur über `setTransform`/`set<Transform>` (setzt die
  Änderungsmarke), `get<Transform>` ist nur lesend.
- **Hierarchie** (intern `detail::Hierarchy`, verkettete Kindliste): `setParent` behält die Weltlage (neues Lokal =
  inverse(Eltern-Welt) × Welt; ohne Scherung) und lehnt Zyklen ab; `destroyVob` löscht den Teilbaum.
  `updateTransforms` berechnet `WorldTransform` ab dem obersten geänderten Vob jedes Teilbaums neu.
- `World` (geplant, unten) bündelt `Scene`, Spielzeit, Umgebung und Wegnetz.

### Geplant
```cpp
namespace g7::world {
struct VobId { u64 value; };                      // persistent, unique per world, never reused (ADR 0005); 0 = none
inline constexpr u64 kRuntimeVobIdBase = 0x8000'0000'0000'0000; // vobs spawned at runtime (saved in the save game)
struct Vob        { VobId id; StringId name; };   // every placed object
struct Transform  { Vec3 position; Quat rotation; Vec3 scale{1}; };
struct WorldTransform { Mat4 matrix; };           // computed by hierarchy system
struct Parent     { entt::entity entity; StringId boneName; }; // optional bone attachment
struct MeshRenderer { asset::Handle<render::Mesh> mesh; u32 materialOverride; };
struct PointLightC  { Vec3 color; f32 range; f32 flicker; };
struct TriggerVolume{ Shape shape; StringId onEnter, onLeave; };   // script callbacks
struct Zone       { ZoneType type /*Music, Ambient, Indoor, Owned*/; StringId value; AABB bounds; };

class World {
public:
    // The entt::registry is not part of the API (ADR 0005): upper modules use these functions.
    entt::entity spawnVob(const VobDesc&);  void destroyVob(entt::entity);
    template <class C> C& set(entt::entity, C);  template <class C> C* get(entt::entity);  template <class C> void remove(entt::entity);
    template <class... C, class Fn> void each(Fn&&);   // iterate entities with all of C...
    VobId idOf(entt::entity) const;
    entt::entity findByName(StringId) const; entt::entity findById(VobId) const;
    GameTime& time(); Environment& environment(); const Waynet& waynet() const;
    void fixedUpdate(f64 dt);
};
}
```

## Weltformat `.g7world` (JSON, vom Editor geschrieben)
**VobId-Vertrag** (ADR 0005, `docs/coordination.md`): Jeder Vob hat eine `id` (u64, ≥ 1), eindeutig in der Welt und nie
wiederverwendet; `nextVobId` ist der nächste freie Wert und steigt nur. Editor und Welt-Assembler (W3) vergeben IDs
daraus; beim Zusammenführen von Teilwelten werden IDs neu vergeben. Laufzeit-Vobs liegen ab `kRuntimeVobIdBase`.

```json
{ "version": 1, "name": "testworld", "nextVobId": 103,
  "staticMeshes": ["meshes/world/terrain.g7mesh", "meshes/world/camp.g7mesh"],
  "vobs": [
    {"id":101,"type":"mesh","name":"CAMPFIRE_01","pos":[1,0,2],"rot":[0,0,0,1],"mesh":"meshes/props/campfire.g7mesh"},
    {"id":102,"type":"light","name":"CAMPFIRE_01_LIGHT","parent":101,"pos":[0,1,0],"rot":[0,0,0,1],
     "components":{"light":{"color":[1,0.6,0.3],"range":8,"intensity":3,"flicker":0.3}}}
  ],
  "waynet": { "points": [ { "name": "WP_CAMP_ENTRANCE", "pos": [0,0,0], "dir": [0,0,1] } ],
              "edges": [ [0, 1] ],
              "freepoints": [ { "name": "FP_CAMPFIRE_SIT_01", "pos": [1,0,3], "dir": [0,0,-1] } ] },
  "zones": [ { "type": "music", "value": "CAMP", "bounds": [[-20,-5,-20],[20,10,20]] } ] }
```

**Version 1 – verbindlich (umgesetzt in `world/WorldFile.hpp`, ADR 0017):**
- Pflicht: `version` (= 1; andere Versionen werden abgelehnt). Optional: `name`, `nextVobId` (wird beim Lesen auf
  mindestens max(id) + 1 angehoben), `staticMeshes` (VFS-Pfade), `vobs`, `waynet`, `zones`.
- Vob: `id` (Pflicht, ≥ 1, eindeutig), `type` = `empty` (Gruppe, Vorgabe) | `mesh` | `light` | `start` | `sound` |
  `trigger` | `mob` (unten), `name`, `parent` (ID; Eltern dürfen
  in der Datei nach den Kindern stehen), `pos` [x,y,z] (Meter, relativ zum Elternteil), `rot` Quaternion **[x,y,z,w]**,
  `scale` [x,y,z] (Vorgabe 1). `mesh`-Vobs: `mesh` (VFS-Pfad ab Wurzel, `.g7mesh`; ein `.glb`-Pfad lädt die gekochte
  `.g7mesh`, wenn vorhanden). `light`-Vobs: `components.light` mit `color` (linear), `range` (> 0), `intensity` (Vorgabe 3),
  `flicker` (0–1, Vorgabe 0).
- Weitere Vob-Typen (M4, Erweiterung von v1 nach dem Muster `components`, Abschnitt „Vob-Typen“ unten).
- `waynet`/`zones` werden bis zu ihren Systemen unverändert gelesen und zurückgeschrieben. Unbekannte Schlüssel
  werden ignoriert (nicht zurückgeschrieben).
- **Schreiben** ist stabil: Kopf-Schlüssel je eine Zeile, dann **ein Vob pro Zeile** nach `id` sortiert, Zahlen auf
  1e-5 gerundet – gleiche Welt, gleiche Bytes; Laden und Speichern ändert nichts.
- Fehler nennen Datei und Eintrag (`camp.g7world: vobs[3].pos: must be a list of 3 numbers`); fehlende Eltern,
  Elternzyklen und doppelte IDs lassen die Szene beim Laden unverändert.

```cpp
Result<WorldFile> parseWorldFile(std::string_view json, std::string_view source);  Result<WorldFile> loadWorldFile(const asset::Vfs&, path);
std::string writeWorldFile(const WorldFile&);                                      // stabil
Result<void> spawnWorld(Scene&, const WorldFile&);  WorldFile captureWorld(const Scene&, std::string_view name);
// Komponenten: MeshRef { std::string path; }, LightSource { Vec3 color; f32 range, intensity, flicker; }
```
- **Engine (Vob-Typen):** `--start=<name>` bzw. der Startpunkt mit der kleinsten id setzt die Kamera; Trigger melden
  die Kamera (Log `trigger enter TRG_… -> FUNKTION`, ab M7 Lua); Debug-Draw (F2): Startpunkte grün mit Blickpfeil,
  Trigger blau (rot, solange die Kamera darin ist), Sound-Reichweite violett, Mob-Fokuspunkt mit Definition.
  Testwelt: START_LAGER, SND_CAMPFIRE, TRG_CAMP_GATE, Mob STOOL_CAMPFIRE.
- **Engine:** `--world=<vfs-pfad>` lädt eine Welt (Mesh-Vobs werden gerendert, Licht-Vobs zu Punktlichtern, bis zum
  Gelände eine Bodenplatte unter der Welt), `--save-world=<datei>` speichert die geladene Welt oder Testszene.
  Testwelt: `assets/source/testworld/camp.g7world` (das Lager der M2-Testszene, 172 Vobs).
- Eine gekochte Binärvariante folgt bei Bedarf (große Welten); das Textformat bleibt.

## Vob-Typen (v1, M4)
Für alle Typen gilt dieselbe Transform-Regel: `pos`/`rot`/`scale` relativ zum `parent`, ohne `parent` also
Weltkoordinaten. Namen sind Bezeichner (Gothic-Konvention, GROSS_MIT_UNTERSTRICH), keine Anzeigetexte – sichtbare
Texte kommen aus Inhalt/Definitionen (lokalisiert, M14). Konvention der Engine: rechtshändig, +Y oben, **−Z vorn**.

| Typ | Komponente (`components.…`) | Bedeutung |
|---|---|---|
| `start` | – | Startpunkt; `name` Pflicht und eindeutig (ohne Groß-/Kleinschreibung), sonst Ladefehler. `pos` = Füße, die Kamera steht **1,7 m** darüber (`kStartEyeHeight`), volle `rot`; die Spielfigur (M5) nimmt nur das Gieren. |
| `sound` | `sound`: `sound` (Pflicht, Name einer Sound-Definition), `range` m (> 0, Vorgabe 20), `volume` 0…1 (Vorgabe 1), `mode` `loop`|`random` (Vorgabe loop), `delay` [min, max] s (random; Vorgabe [5, 15]) | Geräuschquelle (wie zCVobSound); bis zur Audio-Phase nur Daten + Debug-Draw. |
| `trigger` | `trigger`: `shape` `box` (`halfExtents` [x,y,z] > 0, Vorgabe 1) | `sphere` (`radius` > 0, Vorgabe 1), `onEnter`/`onLeave` (Skriptfunktionsnamen, optional), `filter` `player`|`npc`|`any` (Vorgabe player), `once` (Vorgabe false), `target` (**reserviert**: Vob-ID oder Name, gelesen/geschrieben, noch ohne Wirkung) | Volumen, das Betreten/Verlassen meldet. Box dreht und skaliert mit dem Vob, Kugel: Radius × größte Skalierung. |
| `mob` | `mob`: `definition` (Pflicht, Name der Mob-Definition, M8) + `mesh` wie bei `mesh` | Interaktives Objekt (Bett, Truhe, Tür); bis M8 wie ein Mesh gezeichnet. Fokusname kommt aus der Definition. |

**Kategorie (`category`, M4 Sichtbarkeit):** `mesh`-Vobs haben optional `"category": "deco" | "gameplay"`, Vorgabe
`deco` – geschrieben wird es nur bei `gameplay` (bestehende Welten bleiben bytegleich). `deco` darf ausgeblendet werden,
wenn es auf dem Bildschirm klein wird (`size_cull`), `gameplay` nie wegen der Größe (Wegweiser, Questobjekte) – für
beide gilt die Sichtweite `view_distance` (engine.toml, render.md „Sichtbarkeit“). `mob`-Vobs sind immer `gameplay`;
`"category": "deco"` an einem Mob ist ein Ladefehler. Andere Typen haben kein Feld. Mit welt abgestimmt.

Beispiel (Leonberg, Ursprung = Marktbrunnen, Gelände dort y ≈ 0):
```json
{"id":1,"type":"start","name":"START_MARKTPLATZ","pos":[0,0,8],"rot":[0,0,0,1]}
{"id":2,"type":"start","name":"START_UEBERSICHT","pos":[0,40,60],"rot":[-0.258819,0,0,0.965926]}
```
- START_MARKTPLATZ: Füße 8 m südlich des Brunnens, Kamera bei (0, 1.7, 8), Blick nach Norden (−Z, Identität).
- START_UEBERSICHT: Kamera bei (0, 41.7, 60), um 30° nach unten geneigt: Drehung um +X um −30°,
  q = (sin(−15°), 0, 0, cos(15°)) = (−0.258819, 0, 0, 0.965926).
- Auswahl: `--start=<name>` (Groß-/Kleinschreibung egal; unbekannter Name = Fehler mit Liste), sonst der Startpunkt
  mit der **kleinsten id**. Ohne Startpunkt bleibt die bisherige Übersichtskamera.

```cpp
struct StartPoint {}; struct SoundEmitter { sound, range, volume, mode, delay }; struct MobRef { definition };
struct TriggerVolume { shape, halfExtents, radius, onEnter, onLeave, filter, once, targetId, targetName };
Result<entt::entity> findStartPoint(const Scene&, std::string_view name = {});          // StartPoints.hpp
class TriggerSystem { void setCallback(Callback); std::vector<TriggerEvent> update(const Scene&, span<const TriggerProbe>);
                      bool isInside(VobId trigger, VobId who) const; void reset(); };  // Triggers.hpp
```
- `TriggerSystem::update` prüft Proben (Spieler/NPC mit `VobId`, Position; in M4 die Kamera) gegen alle Trigger und
  meldet Änderungen **sortiert nach Trigger-ID, dann Proben-ID, Leave vor Enter** – deterministisch für Tests und
  Skripte. Eine fehlende Probe hat alle Trigger verlassen. `once`: nur das erste Update mit Eintritt meldet (alle
  Proben darin), danach nichts mehr bis `reset()`. `world` ruft keine Skripte: die Engine registriert den Callback.

## Gelände – `terrain`-Block (v1.x, Vertrag mit welt)
Optional in `.g7world`; fehlt er, hat die Welt kein Gelände (v1 bleibt gültig). Mit welt abgestimmt (passt zu
`tools/worldgen` `terrain.json`).
```json
"terrain": {"version":1,"heightmap":"worlds/leonberg/terrain.r16","width":2000,"height":2000,"cellSize":1.0,
            "firstSample":[-999.5,-999.5],"minY":-50.991,"maxY":94.85}
```
- `version` (Pflicht, = 1), `heightmap`: VFS-Pfad einer **rohen `.r16`**: `width·height` Werte **uint16 Little Endian**,
  zeilenweise. `width`/`height` ≥ 2, `cellSize` > 0 (Meter zwischen Sample-Mitten), `firstSample` [x, z] der Mitte von
  Spalte 0 / Zeile 0, `maxY` > `minY`. Optional: `splat`, `holes` (unten).
- **Lage:** Sample (Spalte c, Zeile r) liegt bei x = firstSample.x + c·cellSize, z = firstSample.z + r·cellSize.
  **Zeile 0 = kleinstes z (Norden, −Z), Spalte 0 = kleinstes x (Westen, −X)**, Y oben. Die Fläche reicht von Sample-Mitte
  zu Sample-Mitte; dazwischen bilinear, außerhalb gilt die Randhöhe.
  Beispiel (2000×2000, 1 m, firstSample −999.5): (c 0, r 0) → (−999.5, −999.5); (1999, 0) → (999.5, −999.5);
  (0, 1999) → (−999.5, 999.5); (1000, 500) → (0.5, −499.5).
- **Höhe:** y = minY + v / 65535 · (maxY − minY). Kodierung v = round((y − minY) / (maxY − minY) · 65535)
  (Halbwerte beliebig gerundet), auf 0 … 65535 begrenzt.
  Beispiel (minY −50.991, maxY 94.85, Auflösung 2,2254 mm): y 0.0 → v 22913 → y −0.000616; y 21.9 → v 32754 →
  y 21.899457; minY → 0; maxY → 65535.
### Splat-Schichten (`splat`, optional, M4 Teil B)
```json
"splat": {"maps": ["worlds/leonberg/generated/splat0.png"],
          "layers": [{"name": "Wiese", "albedo": "worlds/leonberg/wiese.png", "tile": 4.0},
                     {"name": "Fels", "albedo": "worlds/leonberg/fels.png", "tile": 6.0, "normal": "…"}]}
```
- `layers`: 1 … 8 Schichten; `name` und `albedo` (VFS-Pfad, sRGB-Farbe) Pflicht, `tile` = Meter je Texturwiederholung
  (> 0, Vorgabe 4), `normal` (VFS-Pfad) **reserviert**: gelesen und geschrieben, noch nicht gezeichnet. Alle Albedos
  müssen gleich groß und gleich formatiert sein (sonst Ladewarnung mit Meldung, dann Neigungsfärbung).
- `maps`: genau ⌈Schichten / 4⌉ (1 oder 2) RGBA-Bilder, **lineare Daten**: Kanal k von Karte m = Gewicht der Schicht
  4m + k (Alpha ist ein Gewicht, keine Transparenz). Je Punkt auf Summe 1 normiert; Summe 0 → Schicht 0. Alle Karten
  gleich groß (W×H, beliebig).
- **Lage:** Pixel-Mitten liegen auf Samples – Pixel (0, 0) auf Sample (0, 0), Pixel (W−1, H−1) auf dem letzten Sample;
  Zeile 0 = Norden wie die Heightmap; dazwischen bilinear. Pixel (i, j) liegt bei
  x = firstSample.x + i·(width−1)·cellSize / (W−1), z = firstSample.z + j·(height−1)·cellSize / (H−1).
  Beispiel Leonberg (2000×2000, 1 m, firstSample −999.5) mit 1000×1000 Splat: Pixel (0, 0) → (−999.5, −999.5);
  (999, 999) → (999.5, 999.5); (500, 0) → x = −999.5 + 500·1999 / 999 = 1.0005. Bei Heightmap-Auflösung
  (2000×2000) fällt jedes Pixel genau auf ein Sample; da 1999 prim ist, ergibt jede andere Auflösung einen krummen
  Pixelabstand (z. B. 2,001 m bei 1000 px) – mit der Formel unproblematisch.

### Löcher (`holes`, optional)
- `"holes": "worlds/leonberg/generated/holes.r8"` – rohe 8-Bit-Maske **je Zelle** (nicht je Sample):
  (width−1)·(height−1) Bytes, zeilenweise, Zeile 0 = Norden. Zelle (c, r) ist das Quadrat zwischen Sample (c, r) und
  (c+1, r+1): x ∈ [firstSample.x + c·cellSize, firstSample.x + (c+1)·cellSize], z entsprechend. **0 = Loch** (Zelle
  fehlt, auch im Schatten und später für Kollision), sonst Boden (worldgen schreibt 255).
  Beispiel Leonberg: 1999·1999 = 3 996 001 Bytes; Zelle (0, 0) = x −999.5 … −998.5, z −999.5 … −998.5; Byte-Index von
  Zelle (c, r) = r·1999 + c; Zelle (1000, 500) = x 0.5 … 1.5, z −499.5 … −498.5.
- Falsche Länge ist ein Ladefehler mit Dateiname; eine Maske nur aus Nullen wird geladen, aber gewarnt (sie entfernt
  das ganze Gelände).

- `world::Heightfield` (`Terrain.hpp`): `load(vfs, ref)` (Höhen + Löcher), `heightAt(x, z)`, `normalAt(x, z)`,
  `sampleHeight(c, r)`, `isHole(x, z)` (letzte Sample-Linie gehört zur letzten Zelle; außerhalb false), `holes()`,
  `bounds()`, `renderDesc()`; `encodeHeight`/`decodeHeight`. Gerendert von `render::TerrainRenderer` (render.md); die
  Engine lädt Splat-Karten und Albedos über den AssetManager (gekocht als `.ktx2`, siehe 06-asset-pipeline.md).
- Testwelt: `assets/source/testworld/` – `terrain.r16` (257×257, 2 m, −20 … 60 m), `splat0.png` (129×129: Gras,
  Erde, Fels, Weg), `layer_*.png` (128², prozedural), `holes.r8` (Grube östlich des Lagers); alles aus
  `make_terrain.py`.
- Generierte Gelände-Daten der Welt-Spur liegen unversioniert unter `assets/source/worlds/<ort>/generated/`
  (im Dev-Build gemountet, von g7-cook mitgekocht).

## Spielzeit & Umgebung
- `GameTime`: Tag + Minuten; Skalierung (Standard: 1 Spielminute = 4 Echtsekunden → 24 h ≈ 96 Min);
  `advanceTo(hour)` fürs Schlafen; Ereignis bei jeder neuen Spielminute (für Routinen).
- `Environment`: berechnet aus Uhrzeit + Wetter: Sonnenrichtung, Sonnen-/Ambientfarbe, Nebelfarbe/-dichte,
  Himmelsverlauf, Sternensichtbarkeit. Werte aus Daten-Kurven (`environment.toml`).
- Wetter (M17): Regenintensität als Zustandsautomat mit Übergängen.

## Gothic-Bezug
- Vob-Baum ≈ ZenGin `zCVob`-Hierarchie. Mobs, Trigger, Lichter, Startpunkte sind Vob-Typen.
- Weltwechsel: Zustand der verlassenen Welt wird gespeichert (siehe save) und beim Rückkehren wiederhergestellt.
