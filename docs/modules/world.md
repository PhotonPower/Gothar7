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
- Vob: `id` (Pflicht, ≥ 1, eindeutig), `type` = `empty` (Gruppe, Vorgabe) | `mesh` | `light`, `name`, `parent` (ID; Eltern dürfen
  in der Datei nach den Kindern stehen), `pos` [x,y,z] (Meter, relativ zum Elternteil), `rot` Quaternion **[x,y,z,w]**,
  `scale` [x,y,z] (Vorgabe 1). `mesh`-Vobs: `mesh` (VFS-Pfad ab Wurzel, `.g7mesh`; ein `.glb`-Pfad lädt die gekochte
  `.g7mesh`, wenn vorhanden). `light`-Vobs: `components.light` mit `color` (linear), `range` (> 0), `intensity` (Vorgabe 3),
  `flicker` (0–1, Vorgabe 0).
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
- **Engine:** `--world=<vfs-pfad>` lädt eine Welt (Mesh-Vobs werden gerendert, Licht-Vobs zu Punktlichtern, bis zum
  Gelände eine Bodenplatte unter der Welt), `--save-world=<datei>` speichert die geladene Welt oder Testszene.
  Testwelt: `assets/source/testworld/camp.g7world` (das Lager der M2-Testszene, 169 Vobs).
- Eine gekochte Binärvariante folgt bei Bedarf (große Welten); das Textformat bleibt.

## Gelände – `terrain`-Block (v1.x, Vertrag mit welt)
Optional in `.g7world`; fehlt er, hat die Welt kein Gelände (v1 bleibt gültig). Mit welt abgestimmt (passt zu
`tools/worldgen` `terrain.json`).
```json
"terrain": {"version":1,"heightmap":"worlds/leonberg/terrain.r16","width":2000,"height":2000,"cellSize":1.0,
            "firstSample":[-999.5,-999.5],"minY":-50.991,"maxY":94.85}
```
- `version` (Pflicht, = 1), `heightmap`: VFS-Pfad einer **rohen `.r16`**: `width·height` Werte **uint16 Little Endian**,
  zeilenweise. `width`/`height` ≥ 2, `cellSize` > 0 (Meter zwischen Sample-Mitten), `firstSample` [x, z] der Mitte von
  Spalte 0 / Zeile 0, `maxY` > `minY`. Reserviert (noch ignoriert): `splat`, `holes`.
- **Lage:** Sample (Spalte c, Zeile r) liegt bei x = firstSample.x + c·cellSize, z = firstSample.z + r·cellSize.
  **Zeile 0 = kleinstes z (Norden, −Z), Spalte 0 = kleinstes x (Westen, −X)**, Y oben. Die Fläche reicht von Sample-Mitte
  zu Sample-Mitte; dazwischen bilinear, außerhalb gilt die Randhöhe.
  Beispiel (2000×2000, 1 m, firstSample −999.5): (c 0, r 0) → (−999.5, −999.5); (1999, 0) → (999.5, −999.5);
  (0, 1999) → (−999.5, 999.5); (1000, 500) → (0.5, −499.5).
- **Höhe:** y = minY + v / 65535 · (maxY − minY). Kodierung v = round((y − minY) / (maxY − minY) · 65535)
  (Halbwerte beliebig gerundet), auf 0 … 65535 begrenzt.
  Beispiel (minY −50.991, maxY 94.85, Auflösung 2,2254 mm): y 0.0 → v 22913 → y −0.000616; y 21.9 → v 32754 →
  y 21.899457; minY → 0; maxY → 65535.
- `world::Heightfield` (`Terrain.hpp`): `load(vfs, ref)`, `heightAt(x, z)`, `normalAt(x, z)`, `sampleHeight(c, r)`,
  `bounds()`, `renderDesc()`; `encodeHeight`/`decodeHeight`. Gerendert von `render::TerrainRenderer` (render.md).
- Testwelt: `assets/source/testworld/terrain.r16` (257×257, 2 m, −20 … 60 m; erzeugt von `make_terrain.py`).

## Spielzeit & Umgebung
- `GameTime`: Tag + Minuten; Skalierung (Standard: 1 Spielminute = 4 Echtsekunden → 24 h ≈ 96 Min);
  `advanceTo(hour)` fürs Schlafen; Ereignis bei jeder neuen Spielminute (für Routinen).
- `Environment`: berechnet aus Uhrzeit + Wetter: Sonnenrichtung, Sonnen-/Ambientfarbe, Nebelfarbe/-dichte,
  Himmelsverlauf, Sternensichtbarkeit. Werte aus Daten-Kurven (`environment.toml`).
- Wetter (M17): Regenintensität als Zustandsautomat mit Übergängen.

## Gothic-Bezug
- Vob-Baum ≈ ZenGin `zCVob`-Hierarchie. Mobs, Trigger, Lichter, Startpunkte sind Vob-Typen.
- Weltwechsel: Zustand der verlassenen Welt wird gespeichert (siehe save) und beim Rückkehren wiederhergestellt.
