# physics

**Zweck:** Kollision, Abfragen und Bewegung. Bibliothek: **Jolt Physics** (ADR 0004), privat.

## Bestandteile
- Statische Welt-Kollision aus `COL_`-Geometrie bzw. Welt-Mesh (Mesh-Shape), Vobs mit Box/Konvex.
- Abfragen: `raycast`, `sphereCast`, `overlap` mit Layer-Filtern (`World`, `Npc`, `Item`, `Mob`, `Trigger`, `Water`).
- **Charakter-Controller** (Jolt `CharacterVirtual`):
  - Zustände: `Ground`, `Air`, `Swim`, `Dive`, `Climb` (Kante hochziehen), `Slide` (zu steiler Hang)
  - Parameter: Steigungsgrenze (~50°), Stufenhöhe, Sprunghöhe, Kantenhöhen für „hochziehen“ (niedrig/mittel/hoch → unterschiedliche Animationen)
  - Kantenerkennung: Shapecast nach vorne + nach unten auf der Kante, liefert Kantenhöhe und Kletterziel
  - Wasser: Volumen mit Oberfläche; Schwimmen ab Wassertiefe > Hüfthöhe, Tauchen mit Luftvorrat
  - Fallschaden ab Fallhöhe X (Skript-konfigurierbar)
- Trigger-Volumen → Ereignisse an `world`.
- Einfache Rigidbodies (fallende Items, Projektile optional kinematisch).

## Umgesetzt (M5 Teil B) – `Physics.hpp`
```cpp
namespace g7::physics {
enum class Layer : u8 { World, Npc, Item, Mob, Trigger, Water, Count };   // LayerMask = u32, layerBit(), kAllLayers
struct ShapePart { enum class Kind : u8 { Hull, Mesh }; Kind kind; std::vector<Vec3> points; std::vector<u32> indices; };
struct HeightfieldDesc { u32 width, height; f32 cellSize; Vec2 firstSample; span<const f32> heights; span<const u8> holes; };
struct RayHit { f32 distance; Vec3 position, normal; Layer layer; u64 userData; };
class PhysicsWorld {
    static Result<PhysicsWorld> create(const PhysicsSettings& = {});        // maxBodies, threads
    Result<ShapeId> createShape(span<const ShapePart>);                     // 1 Teil: die Form; mehrere: StaticCompound
    Result<BodyId> addStatic(ShapeId, pos, rot, scale, Layer, u64 userData);// Form geteilt, Skalierung auch ungleichmäßig
    Result<BodyId> addHeightfield(const HeightfieldDesc&, u64 userData = 0);
    void remove(BodyId); void clear(); void optimize(); void step(f64 seconds);
    std::optional<RayHit> raycast(origin, dir, maxDistance, LayerMask = kAllLayers) const;
    std::optional<RayHit> sphereCast(origin, radius, dir, maxDistance, LayerMask = kAllLayers) const;
    std::vector<u64> overlapSphere(centre, radius, LayerMask = kAllLayers) const;   // userData je Körper einmal
    PhysicsStats stats() const;                                             // Körper, Formen, Mesh-Dreiecke
};
}
```
- **Keine Jolt-Typen in der API** (PImpl). Jolts Registrierung (Allocator, Factory, Typen) ist prozessweit und
  gezählt, mehrere `PhysicsWorld` (Tests) sind möglich. Jolt rechnet auf eigenen Worker-Threads, die API wird nur vom
  Hauptthread benutzt.
- **Layer:** World, Trigger und Water sind statisch (zwei Broad-Phase-Schichten: statisch, beweglich; statisch gegen
  statisch wird nie geprüft). Abfragen filtern mit einer Maske.
- **Gelände:** Jolt-`HeightField`, quadratisch auf eine gerade Kantenlänge aufgefüllt (aufgefüllte Proben ohne
  Kollision). Jolt quantisiert Höhen (8 Bit je 2×2-Block relativ zu dessen Spanne, Fehler im cm-Bereich). **Löcher:**
  Eine Probe entfällt nur, wenn alle angrenzenden Zellen Löcher sind – das Kollisionsloch ist also nie größer als
  das gezeichnete, am Rand eines Lochs bleibt bis zu eine Zelle Boden.
- **Engine-Anbindung** (`runtime/src/EnginePhysics.cpp`): `Engine::physics()` liefert die Welt für Abfragen. Gelände,
  jede gerenderte Instanz (Form je Modell aus `COL_`-Teilen bzw. Render-Mesh, `asset.md` „Kollision in Modellen“) und
  die Bodenplatte (flacher Quader) werden zu statischen Körpern; `userData` = `VobId` (0 für Gelände/Bodenplatte).
  Neuaufbau nach dem Laden einer Welt und vor dem nächsten Simulationsschritt, wenn sich Instanzen geändert haben
  (Editor, Hot-Reload) – vorerst vollständig, gezielte Änderungen bei Bedarf.
- **Messung Leonberg-Kern** (Release, 2026-10-03, Gelände 2000 × 2000 + 1405 Häuser):
  - ohne `COL_` (Render-Meshes, 1,69 Mio. Dreiecke): Aufbau **1,13 s**;
  - mit den `COL_HULL_` von welt (#93: 2288 Hüllen, Median 20 Dreiecke je Haus, 3 Ersatznetze mit zusammen 355
    Dreiecken): Aufbau **0,25 s**. Die `COL_`-Knoten werden nicht gezeichnet (Screenshot geprüft).

## Umgesetzt (M5 Teil C) – Charakter-Controller, `Character.hpp`
```cpp
namespace g7::physics {
struct CharacterDesc { f32 radius = 0.3f, height = 1.8f, maxSlopeDegrees = 50, stepHeight = 0.4f, stickToFloor = 0.5f;
                       LayerMask collidesWith = World | Mob; u64 userData; };
enum class MoveState : u8 { Ground, Slide, Air };     // Swim/Dive/Climb folgen mit Teil D/E
class CharacterController {
    static Result<CharacterController> create(PhysicsWorld&, const CharacterDesc&, const Vec3& feet);
    void update(f32 seconds, const Vec3& horizontalVelocity);   // je festem Schritt
    void teleport(const Vec3& feet);  void setLimits(maxSlope, stepHeight, stickToFloor);   // Hot-Reload
    Vec3 feet() const; Vec3 visualFeet() const; Vec3 velocity() const; MoveState state() const; Vec3 groundNormal() const;
};
}
```
- Jolt `CharacterVirtual`, privat. Der Controller kennt **keine Eingabe**, nur die gewünschte waagrechte
  Geschwindigkeit; Gangarten, Drehen und Beschleunigen liegen in `gameplay` (`Movement.hpp`).
- **Boden:** Gewünschte Geschwindigkeit plus Bodengeschwindigkeit; Stufen bis `stepHeight` steigt Jolts
  `WalkStairs`, bergab hält `StickToFloor` den Kontakt (bis 0,5 m).
- **Zu steil** (> `maxSlopeDegrees`): Zustand `Slide`. Der bergauf gerichtete Anteil der Eingabe entfällt,
  die Schwerkraft zieht die Figur den Hang hinab.
- **Luft:** keine Steuerung, die waagrechte Geschwindigkeit bleibt, Schwerkraft 9,81 m/s².
- `teleport` und `create` bestimmen den Bodenzustand sofort neu (keine Steuerung in der Luft nach einem
  Teleport).
- **Form: aufrechter Zylinder statt Kapsel** (Maße wie abgestimmt: r 0,3 m, Höhe 1,8 m; Kantenrundung 0,01 m).
  Begründung, gemessen:
  - Jolt beurteilt die Steilheit eines Kontakts an der berührten Fläche. An der Oberkante einer Stufe ist das
    deren flache Oberseite. Die runde Unterseite einer Kapsel gleitet deshalb an Kanten hoch, je nachdem, wo
    im festen Schritt die Kante getroffen wird: Bei Stufenhöhe 0,4 m blieb sie an 0,31 m hängen, kam aber auf
    0,53 m.
  - Kalibrieren und ein Kontakt-Listener halfen nicht.
  - Mit flachem Boden hebt nur `WalkStairs`: Stufen bis zur Grenze (+ 1 cm) klappen immer, darüber nie,
    gehend wie rennend.
  - Die Kantenrundung 0,05 m ließ eine 7-cm-Kante beim Gehen scheitern, daher 0,01 m.
- **Am Hang** steht der Zylinder auf seinem Rand, die Mitte schwebt um r · tan(Hang) (0,25 m bei 40°).
  `visualFeet()` liefert den Boden unter der Mitte; dort zeichnet die Engine die Füße.
- **Stadtmauer (Entscheidung Projektinhaber, W6):** Die Brustwehr bleibt besteigbar (Gothic-typisch), ohne
  unsichtbare Sperrhülle. Den Fallschaden trägt der Spieler (Teil D).
- **Kapsel Mensch** (mit figuren abgestimmt): Radius 0,3 m, Gesamthöhe 1,8 m, Hüfthöhe 0,9 m (Schwimmen),
  Augenhöhe 1,62 m; eine Form für alle Menschen (als Zylinder umgesetzt, siehe oben).
- **Monster:** Kapsel je Art aus `data/monsters/<art>.toml`, `[rig.collision]` `shape` (`capsule_upright` |
  `capsule_lying` entlang +Z), `radius`, `length` (inkl. Halbkugeln), `offset` (zu root, Rig-Raum); liefert figuren.
- **Kantenklassen:** niedrig ≤ 1,0 m, mittel ≤ 1,6 m, hoch ≤ 2,2 m (Clips `t_climb_low/mid/high` auf diese
  Obergrenzen gebaut; die Engine skaliert die Root-Höhe nur herunter).
Geplant (Teil D/E): springen, Kanten hochziehen (`detectLedge`, `beginClimb`), Fallschaden, Schwimmen/Tauchen.
Bewegung wird dann teilweise **animationsgetrieben** (Root Motion bei Klettern/Interaktion), der
Controller sorgt für Kollision und Bodenhaftung.
