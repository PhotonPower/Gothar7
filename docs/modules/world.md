# world

**Zweck:** Die Spielwelt: Entities (Vobs) und Komponenten, Weltdateien, Spielzeit, Umgebung
(Tag/Nacht, Wetter), Zonen, Wegnetz-Daten, Weltwechsel.

## Szene (ADR 0005: EnTT)
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
{ "version": 1, "name": "testworld", "nextVobId": 102,
  "staticMeshes": ["meshes/world/terrain.g7mesh", "meshes/world/camp.g7mesh"],
  "vobs": [ { "id": 101, "type": "mesh", "name": "CAMPFIRE_01", "pos": [1,0,2], "rot": [0,0,0,1],
              "mesh": "meshes/props/campfire.g7mesh", "components": { "light": { "color": [1,0.6,0.3], "range": 8, "flicker": 0.3 } } } ],
  "waynet": { "points": [ { "name": "WP_CAMP_ENTRANCE", "pos": [0,0,0], "dir": [0,0,1] } ],
              "edges": [ [0, 1] ],
              "freepoints": [ { "name": "FP_CAMPFIRE_SIT_01", "pos": [1,0,3], "dir": [0,0,-1] } ] },
  "zones": [ { "type": "music", "value": "CAMP", "bounds": [[-20,-5,-20],[20,10,20]] } ] }
```

## Spielzeit & Umgebung
- `GameTime`: Tag + Minuten; Skalierung (Standard: 1 Spielminute = 4 Echtsekunden → 24 h ≈ 96 Min);
  `advanceTo(hour)` fürs Schlafen; Ereignis bei jeder neuen Spielminute (für Routinen).
- `Environment`: berechnet aus Uhrzeit + Wetter: Sonnenrichtung, Sonnen-/Ambientfarbe, Nebelfarbe/-dichte,
  Himmelsverlauf, Sternensichtbarkeit. Werte aus Daten-Kurven (`environment.toml`).
- Wetter (M17): Regenintensität als Zustandsautomat mit Übergängen.

## Gothic-Bezug
- Vob-Baum ≈ ZenGin `zCVob`-Hierarchie. Mobs, Trigger, Lichter, Startpunkte sind Vob-Typen.
- Weltwechsel: Zustand der verlassenen Welt wird gespeichert (siehe save) und beim Rückkehren wiederhergestellt.
