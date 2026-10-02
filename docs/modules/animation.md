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
- **Attachments**: Sockets des Referenz-Skeletts (siehe unten). Rüstung = Mesh-Tausch des Körpers, Kopf separat (Gesichtsvarianten).
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

## Referenz-Skelett (Menschen) – verbindlich ab F1

Quelle der Wahrheit: `assets/source/characters/rig/human_reference.blend`, erzeugt aus
`tools/chargen/src/gothar_chargen/data/human_reference.toml`; Export `human_reference.glb` daneben
(60 Knochen, Gliederpuppe mit Skin und Test-Morph-Targets – als Testfigur für M6 geeignet).
Gelenkpositionen und Knochenachsen stammen aus dem Quaternius-Rig (CC0, Universal Animation Library 2).
Inhalts-Pipeline, Namenskonvention der Clips, Export-Einstellungen und `events.toml`-Format:
`docs/design/characters-pipeline.md` (§2, §2.1, §3). Prüfung: `gothar-chargen validate` (`tools/chargen`).

- **Bind-Pose: T-Pose**, Handflächen nach unten. glTF-Konvention: Y oben, Figur blickt nach +Z,
  linke Seite (`*_l`) bei +X; `root` im Ursprung, Maßstab 1 (1 Einheit = 1 m), Größe ca. 1,83 m.
- In der `.glb` hängt `root` unter einem Armatur-Knoten (`human_reference`) mit Einheitstransformation;
  die Y-oben-Umrechnung steckt in der lokalen Rotation von `root`.

```
root                                   Bodenhöhe, trägt Root Motion
└─ pelvis
   ├─ spine_01 ─ spine_02 ─ spine_03
   │  ├─ neck ─ head
   │  │         └─ socket_helmet
   │  ├─ clavicle_l ─ upperarm_l ─ lowerarm_l ─ hand_l
   │  │     hand_l: thumb_01..03_l, index_01..03_l, middle_01..03_l, ring_01..03_l, pinky_01..03_l,
   │  │             socket_hand_l (Fackel, Bogen, Zauber)
   │  ├─ clavicle_r … hand_r (spiegelbildlich), socket_hand_r (Waffe, Werkzeug)
   │  ├─ socket_back_2h   Zweihänder auf dem Rücken
   │  ├─ socket_back_bow  Bogen/Armbrust auf dem Rücken
   │  └─ socket_quiver    Köcher
   ├─ socket_hip_1h       Einhandwaffe am Gürtel (links)
   ├─ thigh_l ─ calf_l ─ foot_l ─ ball_l
   └─ thigh_r ─ calf_r ─ foot_r ─ ball_r
```
- Sockets haben keine Vertex-Gewichte; ihre Lage/Ausrichtung definiert den Griffpunkt.
- Max. 4 Knochengewichte pro Vertex, max. 128 Knochen pro Skelett (`kMaxBones`).
- Gesicht: Morph-Targets (Namen siehe characters-pipeline.md §6).
