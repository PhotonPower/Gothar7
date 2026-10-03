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
  - **Abspielrate gegen Fußgleiten** (`rate = "<parameter>"` je Zustand): Rate = |Parameter| /
    Eigengeschwindigkeit der Clips (`speed` aus `<set>.events.toml`; bei Blends nach Gewicht gemittelt, Clips ohne
    `speed` wie `s_idle` zählen nicht), begrenzt durch `rate_range = [min, max]` (Graph, Vorgabe 0,6–1,8). Ohne
    `speed`-Daten bleibt die Rate 1. Nur visuell: `movement.toml` bestimmt weiter die Bewegung.
    `Animator::playbackRate()`; im Debug-Fenster „Animation“ als `rate`.
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
    `t_turn_l/r` wie Gothic, Entscheidung Projektinhaber), `jump` (Absprung, einen Schritt), `air` (s in der
    Luft; von einer Kante gelaufen erst ab 0,25 s Sprunghaltung), `fall` (s sinkend), `landed` (Fallhöhe in m,
    einen Schritt; Landeanimation ab 0,3 m) / `hard` (die Landung kostete LP), `climb` (1/2/3), `swim`, `dive`,
    `slide`, `sneak`.
  - Zustände: Fortbewegung (rückwärts, Stand, gehen, rennen als Blend), schleichen, seitwärts, drehen, springen
    (aus Stand bzw. Lauf), Luft, Fall, Landungen, Klettern (3 Klassen, root motion), rutschen, schwimmen, tauchen.
  - Teil C setzt die Parameter aus dem Gameplay (siehe unten).

## Held in der Engine (M6 Teil C, umgesetzt)
- Figur aus `[game] hero` (engine.toml): ein Manifest `*.figure.toml` (Vorgabe `characters/figures/farmer.figure.toml`,
  beim Start aus Teilen zusammengesetzt, Teil D2) oder eine fertige `.glb` (z. B. aus `g7_figures`); fehlt sie,
  die Gliederpuppe `placeholder_mannequin.glb` (versioniert, gleiches Skelett). Graph aus `[game] hero_graph`
  (Vorgabe `data/anim/human.animgraph.toml`). Gezeichnet wird LOD 0.
- Je festem Schritt nach der Bewegung: Parameter aus der gezeichneten Bewegung (`speed`/`strafe` aus der
  Verschiebung der Füße, gilt an Land, im Wasser und beim Rutschen gleich), dann `Animator::update`, dann die
  Look-At, dann das Gesicht. Gezeichnet wird zwischen den Posen der letzten beiden Schritte interpoliert
  (`blendPose` nach `FixedStep::alpha`, einmal je Frame für Schatten und Hauptbild), daraus die
  Skinning-Matrizen `modelSpace(pose) · inverseBind`.
- **Klettern:** Dauer = Länge des Kletter-Clips; die Füße folgen der aufsummierten Root Motion, getrennt nach
  Höhe (y) und Weg nach vorn (z) auf Kantenhöhe und Standpunkt skaliert. Ohne Kletter-Clip gilt der Pfad aus M5.
- **Debug-UI** (F1), Fenster „Animation“: Figur, Graph, Zustand, Überblendung, Fortschritt, Clips mit Gewichten,
  Parameter, die letzten Events.
- `Engine::playerAnimationState()` / `playerFigurePath()` für Tests (render_gpu `Player GPU`).
- Offen: Tiere (D3); Dual-Quaternion-Skinning gegen die Schulterbeule in extremen
  Posen (offener Punkt in render.md, Entscheidung mit echten Clips F4).

## Figuren zur Laufzeit zusammensetzen (M6 Teil D2, umgesetzt)
- `asset/FigureAssembly.hpp`: `FigureManifest::parse` (Format v1, Rollen in fester Reihenfolge body, head, hair,
  beard, dann die Kleidungsstücke), `assembleFigure(manifest, parts)` – derselbe Algorithmus wie
  `gothar-chargen assemble` (`characters-pipeline.md` §6.2): Skelett vom Körper, Teile als `<rolle>_lod<n>`,
  verdeckte Körper-Dreiecke entfernt, Halsring auf den des Kopfes gelegt (in double), Materialien nach Namen
  zusammengeführt (Haut des Kopfes zuerst), Palette, `hides`. Bildpfade werden zu VFS-Pfaden.
- Gleichheitstest gegen die Python-Ausgabe für **alle** Figuren-Manifeste (Positionen < 1e-6, gleiche Dreiecke je
  Material, gleiche Materialien und Bilder); die CI baut dafür vorher `g7_figures` (`-DG7_REQUIRE_FIGURES=ON`).
- Engine: Ist `[game] hero` ein Manifest, entsteht der Held beim Start aus den Teilen (Debug ~0,35 s inkl. Clips).
  `setPlayerPart(rolle, pfad)` (body, head, hair, beard; leer entfernt Haar/Bart) und `setPlayerCloth(stücke)`
  bauen ihn neu, die Animation läuft weiter; Fehler (fehlendes Teil, Stück für einen anderen Körper, anderes
  Skelett) lassen die Figur unverändert. `playerFigureManifest()`, `playerFigureTriangles()`.
- Debug-UI (F1, „Animation“ → „Outfit“): Kopf (gleiches Geschlecht, mit Haar/Bart seines Ordners) und die Stücke der
  Kits zur Statur des Körpers (`cloth_`, `armor_`, `headgear_<g>_<statur>`) an- und ablegen.
- Grenzen: `body_hash` der Masken prüft erst `gothar-chargen` (die Engine vergleicht nur den Pfad des Körpers);
  die Rollen-Reihenfolge (body, head, hair, beard, Kleidung) ist fest, in `gothar-chargen assemble` ebenso (#123).

## Attachments, Gesicht, Look-At (M6 Teil D1, umgesetzt)
- **Attachments:** `Engine::attachToPlayer(socket, modelPath)` bzw. mit einem in Code erzeugten `MeshData`
  (Tests, Debug-UI), `detachFromPlayer(socket)`; je Socket ein Modell. Gezeichnet mit der Figur (Weltmatrix =
  Figur · Modellraum des Sockets, interpoliert), mit Schatten; bleiben über Weltwechsel erhalten.
  `playerSocketTransform(socket)` liefert die Lage im Pose des letzten Schritts. Die Socket-Achsen gelten wie im
  Vertrag (Y = Griffachse). Skripte bekommen das mit M7, Gegenstände mit M10.
- **Gesicht** (`animation/Face.hpp`, `FaceAnimator`): Gewichte der 15 Morph-Targets in Vertragsreihenfolge (§6.1,
  `FaceMorph`, `faceMorphName`).
  - Blinzeln: zufällig alle `blink_min`–`blink_max` s, `blink_seconds` lang; der Zufall hat einen Seed je Figur
    (Pfad), ist also reproduzierbar. Mit `expr_sleep` > 0,5 blinzelt die Figur nicht.
  - Ausdrücke `angry`, `friendly`, `fear`, `pain`, `sleep`: `setExpression(name, gewicht)`; eingeblendet über
    `expression_seconds`, andere ausgeblendet.
  - Sprechen (grob): `setTalking(true)` → zufällige Viseme mit `visemes_per_second`, überblendet, Stärke
    `talk_weight`. Lippensynchronisation nach Audio mit M13.
  - Engine: `setPlayerExpression`, `setPlayerTalking`, `playerFaceWeights`; die Gewichte gehen per
    `SkinnedMesh::setMorphWeights` nur bei Änderung auf die GPU (nur LOD 0 hat Targets).
- **Look-At** (`animation/LookAt.hpp`): dreht eine Knochenkette (Vorgabe `neck` 40 %, `head` 60 %) im Modellraum
  zum Ziel; Gier bis `max_yaw`, Nicken bis `max_pitch`, Geschwindigkeit `degrees_per_second`. Liegt das Ziel
  weiter seitlich als `max_yaw + behind`, geht der Kopf zur Mitte. Nach dem Zustandsautomaten, vor dem Skinning.
  Engine: `setPlayerLookTarget(weltpunkt)`, `playerLookYawDegrees()`.
- **Graph-Datei:** `[face]` (`blink_min`, `blink_max`, `blink_seconds`, `visemes_per_second`, `talk_weight`,
  `expression_seconds`) und `[look_at]` (`bones`, `shares`, `max_yaw`, `max_pitch`, `behind`,
  `degrees_per_second`), beide optional mit den Vorgaben oben.
- **Debug-UI** (F1, „Animation“ → „Try out“): Sockets anzeigen (Achsen X rot, Y grün, Z blau), Teststab an
  einen Socket hängen, Ausdruck mit Gewicht, „talking“, Kopf zur Kamera drehen; Anzeige der Kopfdrehung.

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
