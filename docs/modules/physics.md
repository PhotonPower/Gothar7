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

## Geplante API
```cpp
namespace g7::physics {
class PhysicsWorld { public: Result<void> init(); void step(f64 dt);
    BodyId addStaticMesh(const CollisionMesh&); BodyId addBody(const BodyDesc&);
    std::optional<RayHit> raycast(const Ray&, f32 maxDist, LayerMask) const; };
class CharacterController { public: void setDesiredVelocity(Vec3); void jump();
    MoveState state() const; std::optional<LedgeInfo> detectLedge() const; void beginClimb(const LedgeInfo&); };
}
```
Bewegung wird überwiegend **animationsgetrieben** (Root Motion bei Klettern/Interaktion), der
Controller sorgt für Kollision und Bodenhaftung.
