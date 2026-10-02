# animation

**Zweck:** Skelettanimation für Menschen, Monster und animierte Objekte.

## Konzepte
- **Skeleton**: Knochenhierarchie, Bind-Pose. Ein **gemeinsames humanoides Rig** für alle Menschen
  (wie Gothic: alle Menschen teilen `HUMANS.MDS`) → Animationen sind wiederverwendbar.
- **Clip**: Tracks pro Knochen (T/R/S), Events (`footstep_l`, `hit_start`, `hit_end`, `combo_window`, `item_to_hand`, `sound:<name>`).
- **Animation Set**: Clip-Sammlung pro Rig, mit Varianten nach **Waffenmodus** und **Talentstufe**
  (z. B. `1h_attack_t1` vs. `1h_attack_t2`) und Gang-Stilen (Gothic: Männer/Frauen/„Babe“, Militär, Entspannt).
- **Layer**: Basis (Fortbewegung), Oberkörper (Waffe ziehen während Laufen), additive Gesten, Gesicht.
- **Zustandsautomat**: datengetrieben (`*.g7animgraph`, TOML/JSON), Zustände mit Clip/Blend-Space,
  Übergänge mit Bedingungen (Parameter von gameplay gesetzt: `speed`, `weaponMode`, `inAir` …).
- **Root Motion** für Interaktionen, Klettern, Kampfschritte.
- **Attachments**: benannte Knochen-Slots (`hand_r`, `hand_l`, `spine_weapon_1h`, `spine_weapon_2h`,
  `bow`, `head`, `torch`). Rüstung = Mesh-Tausch des Körpers, Kopf separat (Gesichtsvarianten).
- **Morph-Targets**: Gesicht (Sprechen, Blinzeln, Ausdrücke).
- **Look-At / IK**: Kopf zum Gesprächspartner, Füße auf Boden (optional später).

## Geplante API
```cpp
namespace g7::animation {
class Animator {                       // component on animated entities
public:
    void setGraph(asset::Handle<AnimGraph>);
    void setParam(StringId, f32); void setParam(StringId, bool); void trigger(StringId);
    void play(StringId clip, PlayDesc);          // direct override (interactions, dialog gestures)
    void update(f64 dt, EventSink&);             // fires clip events
    const Pose& pose() const; Transform rootMotionDelta() const;
    Mat4 boneWorld(StringId bone) const;
};
}
```

## Performance
Pose-Berechnung auf CPU (später parallel), Skinning auf GPU, Animations-LOD (weit entfernte
NPCs mit reduzierter Update-Rate).
