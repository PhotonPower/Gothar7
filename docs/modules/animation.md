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

## Daten (M6 Teil A, umgesetzt)
Das Laden liegt in `asset` (`SkinnedModel.hpp`, `docs/modules/asset.md`): Skelett, Skin-Teile mit LOD und
Morph-Targets, Clips und Events aus glTF. Die Laufzeit ist eigene Implementierung (ADR 0019). Figuren-Teile
setzt `gothar-chargen assemble` zusammen. Das CMake-Ziel dafür ist `g7_figures` (optional, Python mit numpy;
`docs/05-build.md`).

## Laufzeit (M6 Teil B, umgesetzt; ADR 0019)
```cpp
namespace g7::animation {
struct BoneTransform { Vec3 translation; Quat rotation; Vec3 scale; Mat4 matrix() const; };
using Pose = std::vector<BoneTransform>;                       // lokal, je Knochen
class Skeleton { static Result<Skeleton> create(const asset::SkeletonData&); find(name); restPose();
                 void modelSpace(const Pose&, span<Mat4>) const; std::vector<f32> maskBelow(bone) const; };
void blendPose(Pose& a, const Pose& b, f32 weight, span<const f32> mask = {});      // lerp/nlerp, kürzester Bogen
void addPose(Pose& a, const Pose& b, const Pose& reference, f32 weight, span<const f32> mask = {}); // additiv
class Clip { Clip(const asset::ClipData&, const Skeleton&); sample(time, Pose&); rootTranslation(time);
             fireEvents(from, to, EventCallback); loops(); duration(); };   // Spuren unbekannter Knochen fallen weg
struct AnimGraph { sets; start; states; transitions; static Result<AnimGraph> parse(toml, source); };
class Animator { static Result<Animator> create(const AnimGraph&, const Skeleton&, span<const asset::AnimationSetData*>);
    void setFloat(name, v); void setBool(name, b); void enter(state, blend);
    void update(f32 seconds, const EventCallback& = {});
    void playOverlay(clip, maskBone, blendIn, additive = false); void stopOverlay(blendOut);
    const Pose& pose() const; Vec3 rootMotion() const; state(); previousState(); fadeWeight(); stateProgress();
    stateEnded(); std::vector<ClipWeight> activeClips() const; };     // activeClips: Debug-UI
}
```
- **Abtasten:** Translation und Skalierung linear, Rotation slerp auf dem kürzesten Bogen, Schritt-Schlüssel halten.
  Schleifen (`s_*`) laufen um, Einmal-Clips klemmen am Ende. Unbewegte Knochen behalten die Ruhepose.
- **Events (Vertrag §3):** feuern für (von, bis].
  - Eine Schleife, die umläuft, feuert erst den Rest des Zyklus, dann den Anfang; über mehrere Zyklen jedes Event
    einmal je Zyklus.
  - Einmal-Clips feuern auch auf dem letzten Frame. Gleiche Zeit: Dateireihenfolge.
  - Bei Blend-Zuständen feuert der Clip mit dem größten Gewicht.
- **Zustandsautomat** `data/anim/<rig>.animgraph.toml` (Version 1; Menschen: `human.animgraph.toml`):
  - `[[state]]` mit `clip` oder `blend = "<param>"` und `points = [{ value, clip }, …]` (aufsteigend; 1D, die zwei
    Nachbarn werden gemischt), `speed`, `root_motion`.
  - `[[transition]]` mit `from` (Zustand oder `*` = jeder andere), `to`, `when = [...]` (alle müssen gelten),
    `blend` (Sekunden Überblendung).
  - Bedingungen: `name` (≠ 0), `!name`, `name < <= > >= == != Zahl`, `end` (der Clip ist einmal durch).
  - Je Update höchstens ein Übergang; Vorrang in Dateireihenfolge.
  - Blend-Zustände teilen eine Phase (Zyklen), damit Schritte von Gehen und Rennen im Takt bleiben; die
    Zykluslänge folgt den Gewichten.
- **Überblenden:** Der alte Zustand läuft weiter und wird linear über `blend` Sekunden ausgeblendet. Ein neuer
  Übergang ersetzt ihn.
- **Root Motion** (`root_motion = true`): `rootMotion()` meldet die Bewegung des Knochens `root` im letzten
  Update; gezeichnet bleibt `root` in Ruhe. Die Engine bewegt damit die Figur (Klettern, mit Teil C) und skaliert
  auf die Kantenhöhe. Fortbewegungs-Clips sind In-Place (`root` bleibt bei 0, geprüft).
- **Overlay:** ein Clip über einer Knochenmaske (`maskBelow("spine_02")` = Oberkörper), normal oder additiv
  (Änderung gegen Frame 0), ein- und ausgeblendet. Einmal-Clips blenden am Ende selbst aus.
- **Morph-Targets** gibt es nur auf `head_lod0` (Vertrag §6.1). Gewichte für LOD 1 und 2 werden still
  ignoriert (Hinweis figuren, M6 A).
- **Graph der Menschen** (`human.animgraph.toml`):
  - Parameter `speed` (vorwärts m/s, rückwärts negativ), `strafe`, `turn` (Drehen im Stand mit
    `t_turn_l/r` wie Gothic, Entscheidung Projektinhaber), `air`, `fall`, `landed`/`hard`, `climb` (1/2/3),
    `swim`, `dive`, `slide`, `sneak`.
  - Zustände: Fortbewegung (rückwärts, Stand, gehen, rennen als Blend), schleichen, seitwärts, drehen, springen
    (aus Stand bzw. Lauf), Luft, Fall, Landungen, Klettern (3 Klassen, root motion), rutschen, schwimmen, tauchen.
  - Teil C setzt die Parameter aus dem Gameplay.

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
