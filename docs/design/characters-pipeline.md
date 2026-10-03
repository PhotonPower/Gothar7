# Figuren & Animationen: Inhalts-Pipeline

**Ziel:** Menschen und Monster für Gothar – glaubwürdig animiert, stilistisch passend zur Welt
(stilisiert, nicht fotorealistisch), mit dem Animationsumfang, den ein Gothic-artiges Spiel braucht
(**mehrere hundert Menschen-Animationen**). Engine-Seite: `docs/modules/animation.md` (Phase M6).
Diese Datei beschreibt die **Inhaltsseite**: Rig, Konventionen, Quellen, Werkzeuge, Ablauf.
Umsetzung als Roadmap-Spur **F1–F5** (`docs/03-roadmap.md`), Animationsliste: `animation-list.md`.

---

## 1. Grundsätze

- **Ein Referenz-Rig für alle Menschen** (Held, NPCs, beide Geschlechter). Jede Animation läuft auf
  jeder Menschfigur. Körperproportionen variieren nur in engen Grenzen (Retargeting-frei).
- **Monster**: ein Rig pro Art bzw. Artenfamilie (Vierbeiner, Vogel, Humanoid-Monster).
- **Blender** ist das zentrale Werkzeug, Austausch über **glTF (.glb)**.
- **Stil vor Realismus:** Bewegungen dürfen etwas überzeichnet sein (lesbar aus der Third-Person-Kamera).
- **Figuren-Stil: realistisch** (Stufe A der Stilproben, Entscheidung des Projektinhabers vom 2026-10-03):
  MPFB2-Proportionen ohne Kopf-/Handvergrößerung, Oberflächen mit Texturen (Stoff, Leder, Haut). „Stil vor
  Realismus“ gilt weiter für die Animation, nicht für die Körperform.
  Mocap-Rohdaten werden immer nachbearbeitet (Timing, Posen, Schwung).
- **Platzhalter zuerst:** Die Engine (M6) startet mit einer CC0-Figur auf dem Referenz-Rig; eigene
  Figuren ersetzen sie später ohne Engine-Änderung.

## 2. Referenz-Rig (verbindlich ab F1, Details in `docs/modules/animation.md` → „Referenz-Skelett“)

- Maßstab 1 Einheit = 1 m, Standardgröße ca. 1,80 m, Y oben, Ausrichtung nach glTF-Konvention:
  Figur blickt nach **+Z**, ihre linke Seite (`*_l`) liegt bei **+X** (in Blender: Z oben, Blick nach −Y).
- **Bind-Pose: T-Pose** (Arme waagerecht, Handflächen nach unten, Daumen nach vorn) – Entscheidung
  des Projektinhabers vom 2026-10-03 (passt zu Quaternius und den meisten Mocap-Werkzeugen, einfacheres Retargeting).
- **Geometrie aus dem Quaternius-Rig** (Entscheidung 2026-10-03): Gelenkpositionen und Knochenachsen
  (Blender-„Roll“) der Körperknochen sind 1:1 vom Mannequin der *Universal Animation Library 2*
  (Quaternius, CC0) übernommen, Größe ca. 1,83 m. Damit laufen Quaternius-Clips (UAL1/UAL2, ~300) und
  -Figuren nach reiner Umbenennung der Knochen (`tools/chargen/.../data/mappings/quaternius_ual*.toml`).
  Namen, Hierarchie und Sockets sind unsere eigenen (Vertrag animation.md).
- `root` auf Bodenhöhe zwischen den Füßen (trägt Root Motion), darunter `pelvis`.
- Knochennamen in `lower_snake_case` mit Seitensuffix `_l`/`_r`.
- **Sockets** (Knochen ohne Gewichte) für Ausrüstung mit Präfix `socket_` – siehe animation.md.
- Gesicht über **Morph-Targets** (Blendshapes), nicht über Gesichtsknochen.
- Ein Blender-Referenzfile `assets/source/characters/rig/human_reference.blend` ist die Quelle der Wahrheit.
  Es wird aus `tools/chargen/src/gothar_chargen/data/human_reference.toml` erzeugt (`gothar-chargen build-rig`)
  und enthält neben dem Rig eine einfache Gliederpuppe (eigene Geometrie, starre Gewichte, alle
  Morph-Targets aus §6 als Test-Shape-Keys). Daneben liegt der Export `human_reference.glb`: gegen
  ihn vergleicht der Validator die Bind-Pose anderer Dateien; die Engine kann ihn als Testfigur nutzen.
- Knochen-Achsen: wie im Quaternius-Rig (in der TOML-Datei als `roll` je Knochen). Socket-Knochen:
  Y-Achse = Griffachse Richtung Klinge/Spitze (`socket_hand_*` nach vorn, `socket_hip_1h` nach unten).

### 2.1 glTF-Export (verbindlich für alle Figuren- und Animationsdateien)

Exportiert wird immer über `gothar-chargen export <datei.blend>` bzw. das Skript
`tools/chargen/src/gothar_chargen/blender/export_glb.py`; die Einstellungen stehen in
`gothar_chargen/blender/settings.py` (Blender 4.5 LTS, `bpy.ops.export_scene.gltf`). Wichtig:

| Einstellung | Wert | Grund |
|---|---|---|
| Format | `.glb` (binär, eingebettet) | eine Datei pro Figur/Set |
| `export_yup` | an | Blender Z-oben/−Y-vorn → glTF Y-oben/+Z-vorn |
| Maßstab | Objekt- und Armatur-Skalierung 1,0 (vorher anwenden) | Validator prüft Maßstab 1 |
| `export_apply` | aus | Modifikatoren nicht anwenden – würde Skin und Shape Keys zerstören |
| `export_def_bones` | aus | Sockets sind Nicht-Deform-Knochen und müssen erhalten bleiben |
| `export_influence_nb` | 4 | max. 4 Knochengewichte pro Vertex |
| `export_rest_position_armature` | an | Knoten tragen die Bind-Pose (T-Pose) |
| `export_morph` / `export_morph_normal` | an / an | Gesichts-Morph-Targets (Namen §6) |
| `export_animation_mode` | `ACTIONS` | eine glTF-Animation pro Blender-Action; Action-Name = Clipname (§3) |
| `export_force_sampling`, `export_frame_step` | an, 1 | jeder Frame gesampelt (30 fps) |
| Nachbearbeitung (`gothar-chargen export`) | Translation außer `root`/`pelvis` und alle Skalierungskanäle entfernen | Vertrag §3: Clips behalten die Knochenlängen der Figur |
| Kameras, Lichter, Extras | aus | gehören nicht in Figuren-Dateien |

Prüfen: `gothar-chargen validate [dateien|ordner]` (ohne Argument: alles unter
`assets/source/characters/`, so läuft es auch in CI). Prüfungen und Grenzwerte: `tools/chargen/README.md`.

### 2.2 LOD-Stufen (Vertrag mit engine, abgestimmt 2026-10-03)

Figuren-`.glb` enthalten ihre Detailstufen als **Geschwister-Knoten mit Suffix** (nicht `MSFT_lod`:
fastgltf kennt die Erweiterung nicht, der Blender-Exporter schreibt sie nicht):
1. Suffix genau `_lod0`, `_lod1`, `_lod2` (klein) am Knotennamen; der Name davor ist bei allen Stufen eines Teils
   identisch. Stufen **lückenlos ab 0** (`_lod0` + `_lod2` ohne `_lod1` ist ein Fehler). Fehlt bei einem Teil eine
   höhere Stufe, nimmt die Engine dessen gröbste vorhandene.
2. **Ohne Suffix** gilt ein Teil in allen Stufen (z. B. kleine Teile, die nicht reduziert werden). Mischen ist erlaubt.
3. Alle Stufen eines Teils: **gleicher Elternknoten, gleiche lokale Transformation, gleicher Skin** (Joints und
   inverse Bind-Matrizen identisch). Höhere Stufen dürfen weniger Materialien nutzen, aber nur aus der
   Materialliste der Datei (keine Materialien nur für LODs).
4. **Morph-Targets nur auf `_lod0`.** Die Engine schaltet Mimik/Viseme ab, sobald die Figur nicht `lod0` zeigt.
5. Die Engine wählt die Stufe **je Figur**, nicht je Teil (Kopf und Körper schalten gemeinsam), nach
   Bildschirmgröße/Entfernung mit Hysterese; die Werte stehen in der Engine, nicht in der Datei. Der Schattenpass
   nimmt die gröbste Stufe.
6. **Nähte** (Hals, Handgelenke, wo Teile aneinanderstoßen) bleiben in allen Stufen unverändert (Reduktion mit
   „Grenzen erhalten“), sonst entstehen Lücken.

**Dreiecks-Budget** (von engine gegen das Render-Budget geprüft): `lod0` Körper + Kleidung 8–15 k, Kopf 3–5 k,
**Obergrenze 20 k je Figur**; `lod1` ≈ 50 %, `lod2` ≈ 20 % von `lod0`. Der Validator prüft Stufen, Skin,
Materialien und Budget (`lod.*`, `mesh.budget`). Laufzeitauswahl folgt in M6; bis dahin ist das ein Datenvertrag,
den Validator und Cooker prüfen bzw. ablegen.

### 2.3 Texturen (Vertrag mit engine, abgestimmt 2026-10-03)

- **Externe Dateien statt eingebettet:** Figuren-Texturen liegen unter `assets/source/characters/textures/<kategorie>/`
  (`skin`, `face`, `hair`, `cloth`) und werden aus den `.glb` per relativer URI referenziert; der Cooker macht daraus
  den VFS-Pfad `characters/textures/…` und KTX2. So teilen sich Figuren Haut-, Stoff- und Ledertexturen auf der
  Platte und (mit dem GPU-Textur-Cache der Engine ab M6) im VRAM. Tönungen sind in den Dateinamen enthalten
  (`toigo_wool_pants_bf8559.jpg`).
- **Höchstgrößen je Rolle** (Material-Namen: `skin`, `cloth_<asset>`, `hair`, `beard`, `eyes`, `eyebrows`,
  `eyelashes`, `teeth`, `tongue`): Haut ≤ 2048², Kleidung ≤ 1024², Haare ≤ 1024², Augen/Brauen/Wimpern/Zähne/Zunge
  ≤ 256²; Normal-Maps ≤ der
  zugehörigen Basisfarbe; Seitenlängen Zweierpotenzen (quadratisch nicht nötig).
- **Formate:** Basisfarbe sRGB; Normal-Maps linear (Tangentenraum, OpenGL-Konvention +Y), bevorzugt PNG.
  Deckende Basisfarben dürfen JPEG sein. **Haare, Brauen, Wimpern:** glTF `alphaMode` MASK mit `alphaCutoff` 0,5
  (nicht BLEND – keine Sortierung, korrekte Schatten), Textur als PNG mit Alphakanal.
- **Budget:** alle Figuren-Texturen zusammen ≤ 512 MB VRAM (engine); je NPC ohne geteilte Haut ≈ 4–6 MB.
- Der Validator prüft das (`tex.*`); eingebettete Texturen sind eine Warnung.

## 3. Namenskonvention für Animationen

```
<modus>/<typ>_<aktion>[_<variante>][_t<talent>]

modus   none | fist | 1h | 2h | bow | cbow | mag | swim | dive | mob/<mobtyp> | dlg | amb
typ     s = Zustand/Schleife (loop)   t = Übergang/Einmal-Aktion   a = additiv/Overlay
talent  t0 (ungeübt) | t1 | t2       (nur wo das Talent die Animation ändert)
```
Beispiele: `none/s_walk`, `1h/s_run`, `1h/t_attack_combo1_t2`, `none/t_stand_2_sit`,
`mob/anvil/s_work`, `dlg/a_gesture_shrug`, `amb/s_guard_arms_crossed`.

Dateien: ein `.glb` pro **Set** (z. B. `anims/human/1h.glb`) mit allen Clips des Modus als glTF-Animationen.
**Events** stehen in einer Begleitdatei `<set>.events.toml` (Clipname → Liste `{frame, event}`),
weil glTF keine Standard-Events kennt. Quelle der Wahrheit sind **Pose-Marker** an den Blender-Actions
(Action Editor → „Show Pose Markers“, Marker-Name = Event-Name); `gothar-chargen export` schreibt daraus
`<set>.events.toml` (Frames relativ zum Action-Anfang). Ohne Marker bleibt eine vorhandene Datei unverändert.

Format `<set>.events.toml` (Version 1, Vertrag mit engine/M6; geprüft von `gothar-chargen validate`):
```toml
version = 1          # Pflicht; Formatversion
fps = 30             # optional (Vorgabe 30): Bildrate, auf die sich die Frame-Nummern beziehen

[clips."none/s_walk"]                       # Clipname = Name der glTF-Animation im Set
events = [
    { frame = 0,  event = "footstep_l" },   # Frame ab 0, ≤ letzter Frame des Clips
    { frame = 15, event = "footstep_r" },   # aufsteigend sortiert
]

[clips."1h/t_attack_combo1_t2"]
events = [
    { frame = 2,  event = "sound:swing_light" },
    { frame = 6,  event = "hit_start" },
    { frame = 11, event = "hit_end" },
    { frame = 14, event = "combo_window" },
]
```
- Event-Namen: `lower_snake_case`, optional mit Argument nach Doppelpunkt (`sound:<name>`).
  Bekannte Events siehe `docs/modules/animation.md` („Clip“); neue Events nach Absprache mit engine.
- Clips ohne Events werden weggelassen; die Datei ist optional.
- Zeit eines Events in Sekunden = `frame / fps`. Alle Sets werden mit 30 fps exportiert.
- **Frame-Bereich:** Schleifen-Clips (`s_*`): `0 ≤ frame < letzter Frame` (der letzte Frame gleicht Frame 0);
  `t_*`/`a_*`: `0 ≤ frame ≤ letzter Frame`. Der Validator prüft das.
- **Auslösung (engine):** Ein Event feuert, sobald die Wiedergabezeit `frame / fps` überschreitet; übersprungene
  Events werden in Reihenfolge nachgeholt. Mehrere Events auf demselben Frame sind erlaubt und feuern in
  Dateireihenfolge.
- **Unbekannte Event-Namen** erzeugen in der Engine eine Warnung, keinen Fehler – neue Events dürfen vorab
  eingetragen werden. Dateien mit `version` > 1 lehnt die Engine ab.

**Animationskanäle (Vertrag):** Clips enthalten **Rotation** für beliebige Knochen, **Translation nur für
`root` und `pelvis`** (Root Motion bzw. Hüfthöhe) und **keine Skalierung** – so behält jede Figur ihre eigenen
Knochenlängen. Root Motion wertet die Engine im Raum des Armatur-Knotens aus. `gothar-chargen export` entfernt
die übrigen Kanäle, die Blender beim Sampeln erzeugt; der Validator meldet Verstöße (`anim.channels`).

Abgestimmt mit engine am 2026-10-03 (Skelett, Kanäle, `events.toml` v1 inkl. der Präzisierungen oben).

Schleifen-Clips (`s_*`) müssen geschlossen sein (letzter Frame = erster; Validator `anim.loop`), und kein
Kanal darf zwischen zwei Frames springen (`anim.jump`: Fehler ab 120°, Warnung ab 90° je Frame).

Clip-Namen prüft der Validator gegen das Muster oben (`[sta]_…`, nur `a-z0-9_`, Modus aus der Liste
oder `mob/<mobtyp>`).

## 4. Quellen & Lizenzen

| Quelle | Wofür | Lizenz / Hinweis |
|---|---|---|
| **Quaternius** (u. a. Universal Animation Library 1+2, Tiere, Platzhalter-Figuren) | Rig-Geometrie, Basis-Bewegungen, Platzhalterfigur (`figures/placeholder_mannequin`), F1-Test-Clips | CC0; UAL1/UAL2 „Standard“ direkt von opengameart.org (itch.io blockt automatische Downloads) |
| ~~Mixamo~~ | **wird nicht verwendet** (Entscheidung 2026-10-03) | Das Repo ist öffentlich; Adobe erlaubt die Nutzung in Spielen, aber keine Weitergabe der Animationsdateien – übertragene Clips im Repo wären genau das |
| **MPFB2** (MakeHuman für Blender, ADR 0018) mit MakeHuman-Asset-Paketen | Körper, Köpfe, Kleidung, Haare, Texturen | Werkzeug GPL (nur lokal); Core-/System-Assets und die Pakete Shirts 01, Pants 01, Shoes 01, Hair 01 **CC0**; Community-Pakete nur nach Einzelprüfung |
| **Video-Mocap** (z. B. Rokoko Vision, Move.ai) | Gothic-spezifische Bewegungen, selbst vorgespielt | eigene Aufnahmen; Dienst-Bedingungen beachten |
| **Keyframe in Blender** | Kampf-Feinschliff, Mob-Interaktionen, Monster | eigene Arbeit |

Jede Fremdquelle → Eintrag in `assets/LICENSES.md`. Keine Animationen oder Figuren aus Gothic.

## 5. Werkzeuge

Python, Ordner `tools/chargen/` (Blender-Add-on + Kommandozeile), Tests mit pytest.

1. **Rig-Validator** (F1, `gothar-chargen validate`, umgesetzt): prüft `.glb`/`.blend` gegen das Referenz-Rig – Knochennamen, Hierarchie,
   Bind-Pose, Maßstab, Ausrichtung, Sockets, Gewichte ≤ 4 je Vertex, Morph-Target-Namen. Läuft auch in CI
   für alles unter `assets/source/characters/`.
2. **Retargeting-Hilfe** (F2/F4): Mapping-Dateien Quell-Rig → Referenz-Rig (`data/mappings/`: Quaternius UAL1/UAL2;
   Mocap-Exporte ab F4),
   Stapel-Retargeting in Blender, Korrektur-Offsets, Fußkontakt-Prüfung.
3. **Animations-Export** (F2, `gothar-chargen build-set`/`export`, umgesetzt): Clips nach Namenskonvention in Sets packen
   (Herkunft je Clip in `data/clips/<set>.toml`: Bibliothek, rückwärts, Überblendung, Verkettung oder
   **Keyframe-Rezept** für Platzhalter `platzhalter-K`, `blender/keyframes.py`, oder **Schichtung** `layer`:
   Beine aus einem Clip, Arme/Kopf aus einer Haltung; `depends` nutzt Clips anderer Sets), Pose-Marker → `events.toml`,
   Root-Motion-Extraktion bzw. In-Place-Bereinigung je Clip-Einstellung.
4. **Animationslisten-Abgleich** (F2, `gothar-chargen report`, umgesetzt): vergleicht `animation-list.md` mit den vorhandenen Clips →
   Fortschrittsbericht (fehlend / Platzhalter / fertig).
5. **Figuren-Baukasten** (F3, `gothar-chargen assemble`, umgesetzt): setzt Körper/Kleidung + Kopf + Haare
   aus einem Manifest zusammen, prüft Passform (Nähte, Gewichte), erzeugt LODs mit festen Rändern; Varianten über
   Farbpaletten (seed-basierte Varianten später).

## 6. Figuren-Baukasten

Technik (F3a): Teile als `.glb` auf dem Referenz-Rig unter `assets/source/characters/parts/`, Figuren als Manifest
`figures/<name>.figure.toml` (Rollen `body`, `head`, `hair`, `beard`; `body` ist Grundkörper oder die Kleidung,
die ihn ersetzt). Die zusammengesetzte Figur hat je Rolle die Knoten `<rolle>_lod0..2`. Teile, die aneinanderstoßen
(Hals), haben deckungsgleiche offene Ränder mit gleichen Gewichten; der Validator prüft das (`fit.*`).
Ausgangskörper und Köpfe kommen aus MPFB2 (ADR 0018, nur CC0-Core-/System-Assets).

**Menschen aus MPFB2** (`gothar-chargen human`, nur lokal): Ein Rezept `humans/<name>.human.toml` beschreibt den
Menschen als Daten (MPFB-Makrowerte, Haut, Augen, Kleidung, Haare, Tönungen, Ziel-Dreiecke). Das Werkzeug baut ihn
mit MPFB, passt das MPFB-Rig „game_engine“ (gleiche 53 Körperknochen) Gelenk für Gelenk an das Referenz-Rig an,
trennt den Kopf an der Gewichtsgrenze `head` ≥ 0,5 ab (Naht passend für `fit.*`), reduziert aufs Budget und schreibt
`parts/<name>/body.glb` (Körper + Kleidung), `head.glb` (Kopf, Augen, Brauen, Wimpern), `hair.glb` sowie die
Texturen nach `textures/` und ein Figur-Manifest. Erste Figur: `farmer` (Bauer, Stil A).

- **Körper:** 2 Grundkörper (m/w) × 3 Staturen (schlank, mittel, kräftig) als Rezepte `humans/body_<m|f>_<statur>`
  (`parts = ["body"]`) mit schlichter Grundbekleidung (Hose bzw. Unterwäsche, CC0). Alle Körper werden auf das
  gemeinsame Referenz-Skelett angepasst, damit jede Animation passt: Staturen unterscheiden sich im **Umfang**
  (MPFB-Makros Gewicht/Muskeln), **nicht in der Größe** (alle ≈ 1,70–1,75 m; Augenhöhe 1,62 m, Kapsel der Engine).
- **Köpfe:** separates Mesh (wie Gothic), Ziel 20+ Gesichter; gemeinsame Morph-Targets (§6.1). Rezepte
  `humans/head_<m|f>_<name>` (`parts = ["head", "hair"]`) mit Alter, Herkunft, Haut, Haaren, optional Bart (wird Teil
  des Kopf-Meshes und bewegt sich mit den Mund-Morphs) und MPFB-Formzielen (`[shape]`, z. B. `nose-hump-incr`).
  Ein Kopf passt auf jeden Grundkörper desselben Geschlechts: `assemble` zieht den Halsring des Körpers auf den des
  Kopfes (beide aus derselben MPFB-Topologie, Paarung über die Randkante; Hals und Kragen folgen weich bis 5 cm
  darunter) und verwendet die **Haut-Textur des Kopfes auch für den Körper** (eine Haut je Figur, gleiche UV).
- **Haare/Bärte:** eigene Meshes, an Köpfe angepasst.
- **Kleidung/Rüstung:** ersetzt den Körper (Mesh-Tausch); je Gilde/Stand eine Linie (Lumpen → leicht → mittel → schwer).
- **Texturen:** Trim-Sheets und Farbvarianten statt Unikat-Texturen; Stil passend zu den Häusern (W5).
- **Budget:** Körper+Kleidung 8–15 k Dreiecke, Kopf 3–5 k (mit Bart bis ~5,6 k), höchstens 20 k je Figur, Stufen
  `_lod1`/`_lod2` (Vertrag §2.2). Lose Teile (Haare, Bärte) werden ohne Randschutz reduziert; nur die Nahtränder von
  Körper und Kopf bleiben in allen Stufen gleich.

### 6.1 Gesichts-Morph-Targets (Vertrag mit engine, abgestimmt 2026-10-03)

- **15 Targets in fester Reihenfolge:** Viseme `vis_aa`, `vis_ee`, `vis_ih`, `vis_oh`, `vis_ou`, `vis_mbp`, `vis_fv`,
  `vis_l`; Blinzeln `blink_l`, `blink_r`; Ausdrücke `expr_angry`, `expr_friendly`, `expr_fear`, `expr_pain`,
  `expr_sleep`. „Ruhe“ = alle Gewichte 0 (kein eigenes Target). Höchstens **16 Targets je Mesh** (Gewichts-Palette des
  Skinning-Shaders); mehr nur nach Absprache mit engine.
- **Ort:** nur auf `head_lod0` (LOD-Vertrag §2.2); in lod1/2 keine Mimik. **Jedes Mesh im Kopf-Teil** (Haut, Augen,
  Brauen, Wimpern, Zähne, Zunge – im glTF Primitive desselben Meshes) trägt **dieselbe Liste in derselben
  Reihenfolge**, auch wenn ein Target es nicht bewegt; engine nutzt einen Gewichtsvektor für alle.
- **Format:** Namen in `mesh.extras.targetNames`, Positionen **und Normalen**, keine Tangenten; sparse Accessoren;
  Standardgewichte 0. Gewichte 0–1, additiv, beliebig viele gleichzeitig (Lippensync + Blinzeln + Ausdruck).
- **Seiten:** `blink_l` = linkes Auge der Figur (+X), wie `*_l` im Rig.
- **Halsnaht:** Morphs bewegen den Nahtring nicht (kein Spalt beim Sprechen).
- **Herkunft:** gemischt aus MPFB2-Gesichtszielen der CC0-Pakete „Visemes 02“ (Meta-Viseme) und „Faceunits 01“
  (ARKit-Einheiten); die Mischung steht als Daten in `tools/chargen/src/gothar_chargen/data/faces/morphs.toml`
  (z. B. `expr_friendly` = Lächeln + Wangen + leichtes Augenkneifen) und kann ohne Code angepasst werden.
  `gothar-chargen human` überträgt die Targets durch Rig-Anpassung und Reduktion (baryzentrisch vom unreduzierten
  Mesh); Köpfe haben dafür Zähne und Zunge. Der Validator prüft Namen, Reihenfolge, Vollständigkeit je Primitive
  und die 16er-Grenze (`morph.*`).

## 7. Monster

Erst CC0-Platzhalter, dann eigene Arten. Pro Art: Rig, Mindest-Set, dazu Artspezifisches (Rudelruf, Sprung).
Startliste für den Vertical Slice: wolfsartiges Rudeltier, Keiler, großer Laufvogel (Arten und Namen frei erfunden,
Design in `docs/design/` festhalten). Platzhalter tragen **generische Art-IDs** (`wolf`, `keiler`, `laufvogel`),
keine Gothic-Kreaturnamen (ADR 0008; Entscheidung des Projektinhabers 2026-10-03).

### 7.1 Monster-Rig (Vertrag mit engine, abgestimmt 2026-10-03)

- **Ein Rig je Art**, kein gemeinsames Monster-Skelett. Definition `tools/chargen/src/gothar_chargen/data/monsters/<art>.toml`
  (Namen, Eltern, Sockets, Bind-Pose; Format wie `human_reference.toml` plus `kind = "monster"`, `species`,
  `[rig.orientation]`) und Referenzdatei `assets/source/characters/monsters/<art>/rig/<art>_reference.glb`.
- **Art-ID:** ASCII, `lower_snake_case`; zugleich Ordnername und Clip-Modus.
- **Pflichtknochen:** `root` (am Boden unter dem Becken, im Ursprung), `pelvis`, `neck_01`, `head`, Socket `socket_mouth`
  (Biss-/Trefferpunkt); `jaw` optional. Höchstens **64 Knochen**, **≤ 4 Gewichte** je Vertex, Sockets ohne Gewichte.
- **Ausrichtung** wie bei Menschen: Y oben, Blick nach +Z, linke Seite +X, Maßstab 1 m, keine Skalierung.
  Bind-Pose ist die **natürliche Stand-Pose** der Art (keine T-Pose).
- **Knochennamen:** Vierbeiner `spine_01…`, `neck_01…`, `head`, `tail_01…`, Beine `front_upper/lower/foot_l/r` und
  `back_upper/lower/foot_l/r`; Vögel `thigh/calf/foot_l/r`, `wing_*_l/r`.
- **Clips:** `<art>/<typ>_<aktion>` (z. B. `wolf/s_walk`), Datei `monsters/<art>/anims/<art>.glb` + `<art>.events.toml`.
  Mindest-Set: `s_idle`, `s_walk`, `s_run`, `t_attack_1`, `t_attack_2`, `t_hit`, `t_die`, `s_eat`, `s_sleep`,
  `t_threaten`, `t_turn_l`, `t_turn_r`.
- **Kanäle:** Translation nur auf `root`/`pelvis`, keine Skalierung (wie §3). **Root Motion:** `s_walk`/`s_run` bewegen
  `root` vorwärts (+Z, Geschwindigkeit = Schrittlänge, Füße stehen), `t_turn_l/r` drehen `root` um +Y (links positiv);
  alle anderen Clips bleiben am Ort.
- **Events:** `footstep_front_l/r`, `footstep_back_l/r` (Vierbeiner; Zweibeiner `footstep_l/r`), `hit_start`/`hit_end`
  im Angriff, `sound:<name>`.
- **Material/Texturen:** Rolle `fur` (≤ 1024², §2.3); Platzhalter ohne Textur.
- **Kollision (engine M5/M9, abgestimmt 2026-10-03):** `[rig.collision]` im Rig-TOML mit `shape`
  (`capsule_upright` | `capsule_lying`, liegend = Achse entlang +Z), `radius`, `length` (ganze Kapsel inkl.
  Halbkugeln; stehend = Höhe) und `offset` (Kapselmitte relativ zu `root`, Rig-Raum: Y oben, +Z vorn), in Metern.
  `gothar-chargen collision` leitet sie aus der Referenz ab: Rumpf ohne Schwanz und Unterbeine (Skin-Gewichte),
  Schnauze knapp drin; lang (Länge > 1,3 × Höhe) → liegend, sonst stehend auf dem Boden. Pflicht für Monster-Rigs;
  der Validator warnt, wenn sie nicht mehr zum Mesh passt (`collision.stale`). Stand: Wolf liegend r 0,34 / l 1,21,
  Keiler liegend r 0,37 / l 2,03, Laufvogel stehend r 0,45 / h 1,6. Menschen: eine Kapsel für alle aus der Engine
  (r 0,3, h 1,8, Hüfte 0,9 m, Augenhöhe 1,62 m); Figuren-`.glb` haben keine `COL_`-Knoten.

### 7.2 Werkzeuge

- `gothar-chargen monster <art> --sources DATA_ROOT\characters\monsters` (nur lokal): baut aus einer CC0-Quelle nach
  `data/monsters/<art>.build.toml` (Quelldatei, Blickrichtung, Höhe, Knochen-Zuordnung, Sockets, Aktionen) das
  Vertrags-Rig. Bewegungen werden als Verformungen im Weltraum aufgezeichnet und exakt übertragen; verschiebt die Quelle
  Bein-Knochen (verboten), löst das Werkzeug die Beine als Zwei-Knochen-Kette (Füße landen auf der Quellposition;
  Schulter-/Hüftknochen darüber werden auf das Bein ausgerichtet) und senkt für Füße am Boden notfalls das Becken
  (höchstens 7 % der Tierhöhe). Weitere Schlüssel der Build-Konfiguration: `parents` (Knochen umhängen, z. B.
  IK-Ziel-Füße unter die Unterschenkel), `drop`, `colors` (Platzhalter-Farbe je Quell-Material), `orientation`.
  Das Werkzeug meldet je Aktion Beckenabsenkung und Abweichung von Füßen und Gelenken. Quell-Animationen mit
  dehnbarer IK (Beine werden länger) lassen sich mit starren Knochen nicht nachbilden – dann ableiten statt übernehmen.
  Ausgabe: Rig-TOML, Referenz-`.blend`/`.glb` und die Clip-Quelle `<art>_clips.blend` (bleibt unter `DATA_ROOT`).
- `gothar-chargen build-set <art>` baut die Clips aus `data/clips/<art>.toml` (`rig = "<art>"`). Rezepte für
  Platzhalter: `advance` (Schleife am Ort + Vorwärtsbewegung, optional schneller/weiter ausholend) und `keyposes`
  (benannte Posen an Schlüssel-Frames, weich überblendet, optional über einer Basis-Schleife). Feste Events stehen als
  `markers = { hit_start = 13, hit_end = 17 }` am Clip.
- Der Validator wählt das Rig nach dem Pfad (`monsters/<art>/…`) und prüft zusätzlich: Clip-Modus = Art, Größe
  ±30 % der Rig-Höhe (Warnung ab ±10 %), Ausrichtungs-Hinweise aus `[rig.orientation]`, `anim.root_motion`
  (s_walk/s_run ≥ 0,1 m/s vorwärts, t_turn_l/r ≥ 45° in die richtige Richtung).

### 7.3 Platzhalter-Arten

| Art | Quelle (CC0) | Stand |
|---|---|---|
| `wolf` (Rudeltier) | Quaternius „Animated Animales Low Poly“ (Animal Pack Vol.2), Wolf | Rig 22 Knochen, 0,85 m, 622 Dreiecke; 12/12 Clips (Idle/Walking aus der Quelle, Rest `platzhalter-K`) |
| `keiler` | Quaternius „Lowpoly Animated Farm Animal Pack“, Schwein (dunkel eingefärbt) | Rig 25 Knochen (mit Schulter-/Hüftknochen), 0,95 m, 562 Dreiecke; 12/12 Clips (Idle/Walk/Death aus der Quelle, Rest `platzhalter-K`) |
| `laufvogel` | Quaternius „5 Low poly animals“, Küken (auf 1,6 m vergrößert, eingefärbt) | Rig 12 Knochen (Vogel-Namen `thigh/calf/foot`), 1,6 m, 250 Dreiecke; 12/12 Clips (Gehen/Rennen aus dem Quell-Schritt am Ort, Rest `platzhalter-K`) |

## 8. Ablauf pro Animation

1. Eintrag in `animation-list.md` (Name, Zweck, Loop/Root Motion, Events, Quelle, Priorität).
2. Rohmaterial beschaffen (Bibliothek oder Mocap) → Retargeting auf das Referenz-Rig.
3. Nacharbeit in Blender (Timing, Posen, Schleifen sauber, Fußkontakt), Events als Timeline-Marker.
4. Export ins Set, Validator, `g7-cook`.
5. Sichtprüfung in der Engine (Debug-UI Animationszustände) → Status in der Liste auf „fertig“.

## 9. Offene Fragen

- Darf der Held eine abweichende Gangart/Proportion haben?
- Video-Mocap-Dienst: Testlauf mit 2–3 Diensten in F4 entscheiden.
- Grad der Stilisierung (Proportionen, Gesichter) – Stil-Referenzblatt gemeinsam mit W5 festlegen.
