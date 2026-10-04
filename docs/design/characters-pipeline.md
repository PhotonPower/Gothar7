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
speed = 0.98                                # optional: Eigengeschwindigkeit in m/s (siehe unten)
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
- Clips ohne Events und ohne `speed` werden weggelassen; die Datei ist optional.
- **`speed` (Eigengeschwindigkeit, additiv, abgestimmt mit engine 2026-10-03):** für Fortbewegungs-Clips
  (`s_walk*`, `s_run*`, `s_sneak*`, `s_strafe*`) die Geschwindigkeit in m/s, bei der die Füße nicht gleiten –
  Clips am Ort (Menschen): Geschwindigkeit des Standfußes relativ zur Figur; Clips mit Root Motion (Monster, §7):
  horizontale Geschwindigkeit von `root`. Gemessen von `gothar-chargen speeds` (reines Python; auch `export` und
  `build-set` schreiben es), der Validator meldet Abweichungen über 3 % (`events.speed`). Die Engine setzt die
  Abspielrate = Bewegungsparameter / `speed` (begrenzt auf 0,6–1,8; in Blend-Zuständen aus der gewichteten
  Eigengeschwindigkeit der beteiligten Clips); fehlt `speed`, bleibt die Rate 1, ein Wert ≤ 0 ist ein Ladefehler.
- Zeit eines Events in Sekunden = `frame / fps`. Alle Sets werden mit 30 fps exportiert.
- **Frame-Bereich:** Schleifen-Clips (`s_*`): `0 ≤ frame < letzter Frame` (der letzte Frame gleicht Frame 0);
  `t_*`/`a_*`: `0 ≤ frame ≤ letzter Frame`. Der Validator prüft das.
- **Auslösung (engine):** Ein Event feuert, sobald die Wiedergabezeit `frame / fps` überschreitet; übersprungene
  Events werden in Reihenfolge nachgeholt. Mehrere Events auf demselben Frame sind erlaubt und feuern in
  Dateireihenfolge.
- **Unbekannte Event-Namen** erzeugen in der Engine eine Warnung, keinen Fehler – neue Events dürfen vorab
  eingetragen werden. Dateien mit `version` > 1 lehnt die Engine ab.

### 3.1 Mobs: Slots, Clips, Events (Vertrag engine–figuren–welt, Schiedsentscheidung Koordinator 2026-10-04)

- **Datei** `assets/source/data/mobs.toml` (Version 1, gehört engine; figuren legt sie an und pflegt die Slots,
  welt kann Werte vorschlagen). Je Mob-Typ (= Name der Mob-Definition, `components.mob.definition` im Vob):
  `clips = "mob/<typ>"`, `enter`/`loop`/`leave` (Clipnamen ohne Präfix, leer = keiner), `extra` (weitere Schleifen),
  `[[mobs.<typ>.slots]]` mit `name`, `pos` (Fußpunkt der Figur in Mob-Metern) und `facing` (waagrechter
  Richtungsvektor der Blickrichtung). Die Engine wählt den nächsten freien Slot, richtet die Figur aus
  (Toleranz 5 cm / 10°, dann exakt) und spielt `enter`.
- **Achsen:** Y oben, Mob-Ursprung am Boden, Vorderseite des Mobs +Z – die Figur steht davor und blickt nach −Z.
- **Clips** (`anims/human/mob.glb`, Set `mob`): Truhe `t_open`/`s_open`/`t_close` + `extra.picklock = s_picklock`;
  Amboss `t_start`/`s_work`/`t_stop`; Bett `t_lie_down`/`s_lie`/`t_stand_up` – **Root Motion nur hier** (aufs
  Bett und zurück, im Mob-Raum; die Engine schaltet die Kollision der Figur dabei ab); Tür `t_open` (auch zum
  Schließen, Slots `front`/`back`). Ohne Mob (Set `none`): `t_pickup_ground`, `t_pickup_high`, `t_eat`,
  `t_drink`, `t_read_scroll`, `t_pickpocket`.
- **Events:** `pickup` (Hand am Gegenstand: die Engine nimmt ihn aus der Welt an `hand_r`), `use` (Wirkmoment
  Essen/Trinken/Lesen), `open`/`close` (Deckel oder Tür bewegt sich), `hit_anvil` (Hammer trifft) +
  `sound:anvil_hit`, `lie`/`stand` (Bett), `item_from_hand` (Gegenstand verschwindet), optional `picklock_l/r`.
  Fehlt ein Event, nimmt die Engine die Mitte des `t_`-Clips.
- **Mob-Modelle (welt):** Tür = Türblatt, Ursprung an der Angel unten, die Engine dreht den ganzen Mob um +Y;
  Truhe mit Knoten `MOB_LID`, Pivot am Scharnier (Drehung um seine X-Achse). Testmaße: Truhe 0,9×0,6×0,6 m,
  Amboss Arbeitshöhe 0,8 m, Bett 2,0×0,9 m (Liegefläche 0,45 m), Türklinke 1,0 m.
- **Gegenstände (`items/<id>.glb`, F6):** Ursprung = Griffpunkt; Item-+Y auf Socket-+Y (aus der Faust zur Klinge
  bzw. Spitze), Item-+Z auf Socket-+Z; die Engine übernimmt die volle Drehung des Sockets. Einzelheiten,
  Stücke und Prüfregeln: §6.3.

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
   für alles unter `assets/source/characters/` und `assets/source/items/` (Regeln `item.*`, §6.3).
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
6. **Gegenstände** (F6, `gothar-chargen build-items`, umgesetzt): Waffen und Handgegenstände per Code, §6.3.

## 6. Figuren-Baukasten

Technik (F3a): Teile als `.glb` auf dem Referenz-Rig unter `assets/source/characters/parts/`, Figuren als Manifest
`figures/<name>.figure.toml` (Rollen `body`, `head`, `hair`, `beard`; `body` ist Grundkörper oder die Kleidung,
die ihn ersetzt). Die zusammengesetzte Figur (beim Bauen erzeugt, §6.2) hat je Rolle die Knoten `<rolle>_lod0..2`. Teile, die aneinanderstoßen
(Hals), haben deckungsgleiche offene Ränder mit gleichen Gewichten; der Validator prüft das (`fit.*`).
Ausgangskörper und Köpfe kommen aus MPFB2 (ADR 0018, nur CC0-Core-/System-Assets).

**Menschen aus MPFB2** (`gothar-chargen human`, nur lokal): Ein Rezept `humans/<name>.human.toml` beschreibt den
Menschen als Daten (MPFB-Makrowerte, Haut, Augen, Kleidung, Haare, Tönungen, Ziel-Dreiecke). Das Werkzeug baut ihn
mit MPFB, passt das MPFB-Rig „game_engine“ (gleiche 53 Körperknochen) Gelenk für Gelenk an das Referenz-Rig an,
trennt den Kopf an der Gewichtsgrenze `head` ≥ 0,5 ab (Naht passend für `fit.*`), reduziert aufs Budget und schreibt
`parts/<name>/body.glb` (Körper + Kleidung), `head.glb` (Kopf, Augen, Brauen, Wimpern), `hair.glb` sowie die
Texturen nach `textures/` und ein Figur-Manifest. Erste Figur: `farmer` (Bauer, Stil A); seit F3j wie alle Figuren aus Teilen (Grundkörper, Kopf `head_m_farmer`,
Kleidungs-Kit) – mit eingebauter Kleidung ließ die reduzierte Hose in Bewegung die Haut durchscheinen.

- **Körper:** 2 Grundkörper (m/w) × 3 Staturen (schlank, mittel, kräftig) als Rezepte `humans/body_<m|f>_<statur>`
  (`parts = ["body"]`) mit schlichter Grundbekleidung (Hose bzw. Unterwäsche, CC0). Alle Körper werden auf das
  gemeinsame Referenz-Skelett angepasst, damit jede Animation passt: Staturen unterscheiden sich im **Umfang**
  (MPFB-Makros Gewicht/Muskeln), **nicht in der Größe** (alle ≈ 1,70–1,75 m; Augenhöhe 1,62 m, Kapsel der Engine).
- **Köpfe:** separates Mesh (wie Gothic), Ziel 20+ Gesichter; gemeinsame Morph-Targets (§6.1). Rezepte
  `humans/head_<m|f>_<name>` (`parts = ["head", "hair"]`) mit Alter, Herkunft, Haut, Haaren, optional Bart (wird Teil
  des Kopf-Meshes und bewegt sich mit den Mund-Morphs) und MPFB-Formzielen (`[shape]`, z. B. `nose-hump-incr`).
  **Köpfe werden auf Halshöhe der Grundkörper gebaut:** Das Alter-Makro ändert in MakeHuman die Körpergröße, deshalb
  verschiebt `gothar-chargen human` Kopf, Gesichtsteile und Haare senkrecht, bis die Halsnaht auf der mittleren
  Ringhöhe der Teile `parts/body_*` liegt (F3i). `assemble` meldet einen Fehler, wenn die Naht beim Zusammenbau
  mehr als 6 mm auf- oder absteigen müsste; der Validator prüft die Augenhöhe (`head.eyes`: 1,62 m ± 2,5 cm, Kapsel
  der Engine – Frauen liegen bei gleicher Halshöhe ~2 cm tiefer). Neue Grundkörper → Köpfe neu bauen.
  Ein Kopf passt auf jeden Grundkörper desselben Geschlechts: `assemble` zieht den Halsring des Körpers auf den des
  Kopfes (beide aus derselben MPFB-Topologie, Paarung über die Randkante; Hals und Kragen folgen weich bis 5 cm
  darunter) und verwendet die **Haut-Textur des Kopfes auch für den Körper** (eine Haut je Figur, gleiche UV).
- **Haare/Bärte:** eigene Meshes, an Köpfe angepasst.
- **Kleidung/Rüstung:** je Gilde/Stand eine Linie (Lumpen → leicht → mittel → schwer). **Kleidungs-Kit (F3e):**
  jedes Kleidungsstück ist ein eigenes Teil, je Statur angepasst – Rezepte `humans/cloth_<m|f>_<statur>` mit
  `fit_to = "body_<…>"` (Makros, Haut, Augen vom Grundkörper) und `parts = ["cloth"]` → `parts/cloth_<…>/<stück>.glb`
  (je ~1,5 k Dreiecke). Das Figur-Manifest listet die Stücke unter `[parts] cloth = [...]` (Rolle `cloth_<stück>`,
  Knoten `cloth_<stück>_lod<n>`). `assemble` löscht die Körperflächen darunter (Strahl entlang der Normalen trifft das
  Stück innerhalb 3 cm von innen, oder der Körper ragt bis 1,5 cm heraus – bis 4 cm, wo der Körper selbst Kleidung
  trägt, z. B. die eingebaute Hose unter enger Rüstungshose; Löcher in zerrissener Kleidung und ein
  5-cm-Streifen an der Halsnaht bleiben). Kit-Texturen sind **neutral grau** (halber Kontrast, Helligkeit 0,55) und
  von allen Staturen geteilt; die Farbe gibt die Palette der Figur als glTF `baseColorFactor`.
- **Schultern in Bewegung (geprüft in M6, 2026-10-03; Bilder `DATA_ROOT\review\f3k-shoulders`):** Die frühere Falte
  an den Schulterblättern in der T-Pose ist seit den neu berechneten Masken (F3g) weg; beim Gehen und Rennen sitzen
  die Schultern sauber. In Extremposen (Kletter-Platzhalter, Arm weit nach hinten oben) wölbt sich die Schulter zu
  einem Buckel – **auch die nackte Haut**: Ursache ist das lineare Skinning des Körpers bei so großer Armbewegung aus
  der T-Pose, die Kleidung folgt ihm nur. Versuche ohne Erfolg: Gewichte der Kleidung vom unmaskierten Körper
  übertragen (praktisch identisch, die MPFB-Gewichte stammen schon vom Körper); Schulter-Gewichte glätten (kaum
  Wirkung, reißt die Halsnaht auf). Entscheidung: akzeptiert (Kletter-Clips sind Platzhalter, Mocap in F4). Optionen
  bei Bedarf: (b) Dual-Quaternion-Skinning in der Engine (Hinweis an engine), (c) pose-abhängige Korrektur-Morphs.
- **Rüstungs-Kit (F3g):** leichte und mittlere Linie nach demselben Prinzip, Rezepte `humans/armor_<m|f>_<statur>`
  → `parts/armor_<…>/<stück>.glb`, jedes Stück auf jeder Statur geprüft. Rüstung **behält ihre eigene Farbtextur**
  (Entscheidung Projektinhaber 2026-10-03; Rezept `neutral = false`, 512 px, Normal-Map erlaubt); die Palette tönt
  optional. Stücke tragen **neutrale Namen** (`[names]`: Quell-Asset → unser Name), Dreiecke je Stück über `[budget]`.
  - leicht: `leather_vest` (eigenes Teil, s. u.), `gloves_short`; dazu Hemd, Hose, Stiefel aus dem Kleidungs-Kit
  - mittel: `mail_tunic` (Kettenhemd über Tunika, 3 k Dreiecke), `wrapped_trousers`, `wrapped_boots`, `gloves_medium`
  - **Eigene einfache Teile** (`[derive.<name>]`), wo es kein CC0-Stück gibt: Kopie eines angepassten CC0-Stücks,
    Vertices überwiegend an Knochen mit den Präfixen `cut` werden entfernt (Ärmel), der Rest um `offset` entlang der
    Normalen nach außen geschoben, eigene kachelnde Textur (ambientCG, CC0) mit `uv_scale`. Das Lederwams entsteht
    so aus dem groben Hemd (ohne Ärmel, 8 mm darüber, Leder 033 A). Ohne `texture` bleibt das Material der Quelle.
    Abgeleitete Stücke mit eigener Textur teilen sie über den Namen der Quelle (`textures/cloth/metal021.jpg`).
  - **Retusche** (`[retouch]`): Rechtecke in Texturkoordinaten bekommen einen versetzten Bereich derselben Textur –
    so ist das Zeichen der Quelle auf der Brust des Kettenhemds entfernt (Entscheidung Projektinhaber 2026-10-03),
    reproduzierbar bei jedem Neubau.
  - Mittlere Figur ≈ 15–16 k, leichte ≈ 16–17 k Dreiecke (Testfiguren `test_armor_{light,medium}_{m,f}`, nur zur
    Prüfung – wer was trägt, entscheidet der Projektinhaber).
  - **schwer (F3l, Entscheidung Projektinhaber 2026-10-03):** Gothic-Richtung – die mittlere Linie plus Platten,
    kein voller Harnisch; dunkles, leicht rostiges Eisen (ambientCG „Metal 021“, wie die Eisenhaube). Alle Platten
    sind **eigene Teile**: `breastplate` (eigene Schale aus der Rumpfhaut des Grundkörpers, `from = "basemesh"`,
    `keep = ["spine"]`, stark geglättet, 3,5 cm darüber – über dem Kettenhemd – mit nach innen umgeschlagenem Rand
    `rim` als sichtbare Kante; nachgebessert auf Wunsch des Projektinhabers: keine Stoff-Falten mehr; bei Frauen
    **neutrale Form** – `flatten = 1.0` zieht die Front an eine glatte Hülle des Rumpfes, kein anatomischer
    Harnisch), `pauldrons`
    (Schale aus der Schulterhaut um die Schultergelenke, gewölbt `bulge`, mit Rand, steif auf `clavicle`/`upperarm`),
    `greaves` (Schaft der gewickelten Stiefel, steif auf `calf`), `gauntlets` (die Handschuhe in Eisen); dazu
    `kettle_helm` im Kopf-Kit. Werkzeug: `keep`, `near`/`radius`, `smooth` (Taubin-Glättung ohne Schrumpfen,
    offene Ränder entlang des Randes – Stoff-Falten werden Platten, Ränder rund), `bones` (Gewichte nur auf diese
    Knochen), `brim` (eigene Krempe), `bulge` (Wölbung zur Mitte), `rim` (umgeschlagener Rand), `flatten` (neutrale Front). Schwere Figur ≈ 16–17 k Dreiecke; in Bewegung mit Falschfarben geprüft
    (Platten gelb, Kettenhemd türkis). Der Renderer kennt bewusst kein Metallic/Roughness – der Metall-Look kommt
    aus der Textur. Testfiguren `test_armor_heavy_{m,f}`.
- **Kopfbedeckungen (F3h):** Rezepte `humans/headgear_<m|f>_<statur>` → `parts/headgear_<…>/<stück>.glb`, wie die
  Rüstung **pro Statur angepasst, nicht pro Kopf**: Die Stücke passen auf jeden Kopf desselben Geschlechts (in
  Renderings von vorn, seitlich und hinten geprüft), Kopfwechsel bleiben frei. Getragene Stücke blenden das Haar aus
  (`[hides]` → `hides` in §6.2); der Bart bleibt.
  - `hood`: CC0-Kapuze (MakeHuman „Suits 02“, Donitz) ohne ihren verdeckten Innenteil bis zur Brust (`cut`)
  - `leather_cap`, `iron_cap`, `nasal_helmet`, `kettle_helm` (F3l: tiefer, mit Krempe `brim`): **eigene Geometrie** –
    eine Kuppel (`dome`) um den Schädel des
    Grundkörpers (`from = "basemesh"`, Breite/Tiefe aus einem Band über den Ohren, 12 cm hoch, vergrößert bis
    alle Schädelpunkte innen liegen), geschnitten von einer nach vorn ansteigenden Ebene (`depth`, `tilt`), um
    `offset` abgesetzt; der Nasal (`nasal = [breite, länge]`) ist ein eigener Steg vom vorderen Rand über den
    Nasenrücken, oben in die Kuppel gesteckt. Texturen ambientCG „Leather 014“ bzw. „Metal 021“ (CC0). Kein
    Fremd-Helm als Vorlage (der Helm aus „Hats 02“ trägt im Datei-Kopf AGPL3).
  - `heads = "head_<m|f>_*"`: Beim Bauen müssen alle Köpfe des Geschlechts unter das Stück passen – Kuppeln
    wachsen, bis auch deren Schädelpunkte innen liegen; die Kapuze wird nur dort weich nach außen gedrückt, wo ein
    Scheitel durchstechen würde (radial vom Kopfmittelpunkt gemessen, unabhängig von den Flächennormalen der Quelle).
    Neue Köpfe → Kopf-Kits neu bauen.
- **Texturen – abgetragene Stoffe (F3n, Entscheidung Projektinhaber 2026-10-04: Gothic-Kolonie, abgetragen und
  schmutzig):** Ein echtes Trim-Sheet (mehrere kachelnde Stoffe in einer Textur) kann der Renderer nicht
  (Wiederholung innerhalb einer Kachel bräuchte UV-Versatz je Material im Shader), und Flecken würden sich auf
  kachelnden Stoffen sichtbar wiederholen. Deshalb **eine Stoff-Bibliothek plus gebackene Kleidungstexturen**:
  `gothar-chargen fabrics` (Kern in reinem numpy, `fabrics.py`; Bild-Ein/Ausgabe in Blender) backt je
  Kleidungsstück eine 512²-Textur aus einer kachelnden Stoffkachel (ambientCG, CC0; Quellen nur in
  `DATA_ROOT\characters\ambientcg\fabric`) – im UV des Stücks so oft wiederholt, dass die Fadendichte auf allen
  Stücken gleich ist (Wiederholungen je UV-Einheit = √(3D-Fläche ÷ UV-Fläche) ÷ Kachelgröße in m) – plus
  **Alterung `wear` 0–1**: verblichen (weniger Kontrast), Flecken (Rauschen), schmutzige, unregelmäßig breite Säume
  und Nähte entlang der UV-Inselränder. Daten in `tools/chargen/src/gothar_chargen/data/fabrics.toml` (Kachel,
  Größe, je Textur Teil/Material/Kachel/`wear`/optional `tint`); saubere Figuren (Bürger) später mit kleinerem
  `wear`. Die Texturen behalten ihre Namen, die Teile ändern sich nicht. Kit-Texturen bleiben neutral grau (die
  Palette färbt), die Kleidung der Grundkörper getönt wie bisher. Ausgefranste Säume mit Alpha-Test (`MASK`,
  doppelseitig, nur auf Stücken mit Fransen; von engine bestätigt) folgen als Zusatz.
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

### 6.2 Figuren beim Bauen (Vertrag mit engine, abgestimmt 2026-10-03)

Entscheidung des Projektinhabers: **Zusammengesetzte Figuren werden nicht eingecheckt.** Im Repo liegen nur Teile,
Rezepte und Manifeste; `figures/<name>.glb` entsteht beim Bauen und ist git-ignoriert
(`assets/source/characters/.gitignore`; Ausnahme `placeholder_mannequin.glb`, die M6-Testfigur ohne Manifest).

- **Bauen:** `gothar-chargen assemble` (reines Python + numpy, kein Blender, kein DATA_ROOT, Sekunden) baut alle
  Manifeste, prüft sie mit dem strikten Validator und entfernt Figuren ohne Manifest. Aufruf für engine/CMake:
  im Repo-Wurzelordner `PYTHONPATH=tools/chargen/src python -m gothar_chargen assemble`. Die Ausgabe ist
  deterministisch (gleiche Teile + Manifest = bytegleiche `.glb`); `asset.generator = "gothar-chargen assemble"`,
  `asset.extras.gothar = {figure, inputs}` mit einem Hash über Manifest und Teile. Die CI (Job `chargen`) baut
  und validiert alle Figuren; engines CMake-Ziel `g7_figures` (optional, nicht in ALL) ruft denselben Befehl.
- **Manifest** `figures/<name>.figure.toml` (Format v1): `[parts]` mit `body`, `head`, optional `hair`/`beard` und
  `cloth = [...]` (Rollen `cloth_<stück>`), `[palette]` Materialname → `#rrggbb` (wird `baseColorFactor`). Die
  LOD-Stufen bringen die Teile mit (kein `lods` mehr im Manifest).
- **Teile** tragen ihre LOD-Stufen (`<rolle>_lod0..2`, Nahtränder fest; Haare und Kleidung frei reduziert) und in
  `asset.extras.gothar` (Format v1, `partdata.py`) die Daten für den Zusammenbau – damit kann engine später
  **zur Laufzeit** zusammensetzen (Rüstungs-/Kopfwechsel, Mods ohne Python), ohne Formatänderung:
  - Körper und Kopf: `neck` je LOD-Knoten = Halsring (offener Rand der Haut) in Randreihenfolge, je Ringpunkt die
    glTF-Vertices `[primitive, vertex]`; Start am vordersten Punkt (größtes +Z), Lauf Richtung +X. Der Körper hat
    zusätzlich `falloff` = `[primitive, vertex, ringpunkt, gewicht]` für Vertices bis 5 cm vom Ring.
  - Kleidungsstück: `covers` = `body` (Pfad des Grundkörper-Teils, auf das es angepasst ist), `body_hash` (Hash der
    Körper-Geometrie; passt er nicht, ist die Maske veraltet → `gothar-chargen part-data`) und je Körper-LOD die
    verdeckten Dreiecke als Bereiche `[primitive, erstes, ende)` – Bezug sind die **glTF-Primitive** des Teils (der
    Cooker fasst Primitive je Material zusammen und rechnet die Bereiche dann um; Hinweis engine 2026-10-03).
  - Kleidungsstück, optional (F3h, abgestimmt mit engine 2026-10-03): `hides` = Liste von Rollen, die beim Tragen
    ganz entfallen (`hair`, `beard`; Helme und Kapuzen: `["hair"]`). Es betrifft nur ganze Rollen, keine einzelnen
    Stücke; bei mehreren getragenen Stücken gilt die **Vereinigung**. Lücken unter dem Stück sind Sache von figuren.
  - Berechnet von `gothar-chargen part-data` (reines Python; `gothar-chargen human` ruft es nach dem Bauen auf).
- **Zusammenbau-Algorithmus** (assemble.py; in C++ umgesetzt von engine für den Laufzeit-Zusammenbau, M6 D2 / #122,
  in der CI gegen die Python-Ausgabe aller Manifeste verglichen): **feste Rollen-Reihenfolge** body, head, hair,
  beard, dann die Kleidung in Listenreihenfolge (unabhängig von der Schlüsselreihenfolge im Manifest; Materialien:
  head zuerst, dann dieselbe Reihenfolge); Skelett und Skin vom Körper,
  Joints der anderen Teile über die Knochennamen umgehängt; Körper-Dreiecke aus allen `covers` des jeweiligen
  LOD entfernen; Halsring des Körpers auf den des Kopfes legen (Paarung: zyklische Verschiebung und Richtung mit
  kleinster Summe der Abstände), `falloff`-Vertices folgen mit Gewicht; Materialien nach Namen zusammenführen
  (`skin.001` → `skin`), die Haut des Kopfes gilt für die ganze Figur; Palette als `baseColorFactor`; Rollen aus
  der Vereinigung aller `hides` entfallen.

### 6.3 Gegenstände (F6)

`gothar-chargen build-items --sources DATA_ROOT/characters/ambientcg/items` erzeugt `assets/source/items/<id>.glb`
und `items/textures/*.jpg` (`--skip-textures`: nur Geometrie, ohne Blender; `--only <id> …`). Die Geometrie
entsteht per Code (`items.py`, eigene Arbeit ohne Vorlage): Querschnitte entlang eines Pfads (Klingen, Griffe,
Wurfarme, Ringe) und Drehkörper (Obst, Brot, Flasche). Die `.glb` schreibt reines Python (deterministisch),
Blender verkleinert nur die Bildtexturen; Apfel, Brot, rotes Glas, Kork und Schmiedeeisen sind prozedural.

- **Datei:** statisches Mesh ohne Skin, Knoten `<id>_lod0..2` (weniger Segmente je Stufe), höchstens
  1500 Dreiecke in Stufe 0, je Teil ein Material mit Basisfarb-Textur (`textures/<name>.jpg`), ohne Alpha.
- **Achsen (Vertrag §3.1):** Ursprung = Griffpunkt (Mitte der Faust), +Y aus der Faust zur Klinge bzw. Spitze
  (Daumenseite), +Z = Schneide bzw. Vorderseite. In der T-Pose zeigt `socket_hand_r`/`_l` mit +Y nach vorn, +Z nach
  oben, +X am Unterarm entlang zur Schulter. Der **Bogen** hat die Wurfarme entlang ±Y und die **Sehne auf +X**
  (zum Schützen); am Rücken (`socket_back_bow`) liegt er flach an. **Apfel und Brot** sitzen über der Faust
  (zwischen Daumen und Fingern bzw. an einem Ende gehalten), damit der Bissen den Mund erreicht; die **Flasche**
  zeigt mit dem Hals entlang +Y.
- **Stücke:**

  | ID | Länge | Material |
  |---|---|---|
  | `it_sword_old` (alt: rostig, schartig) | 1,0 m | Rost-Metall (Metal 021), Lederwicklung |
  | `it_sword_crude` (Amboss-Rezept: frisch geschmiedet, grob) | 1,0 m | dunkles, ungleichmäßiges Schmiedeeisen, Leder |
  | `it_club` (knorriger Ast mit Astknoten) | 0,74 m | Rinde (Bark 012, dunkler getönt) |
  | `it_bow_short` | 1,2 m | Holz (Wood 049), Ledergriff, Sehne |
  | `it_apple`, `it_bread`, `it_potion_heal_small` | 7 / 18 / 17 cm | prozedural (Glas undurchsichtig) |
  | `it_lockpick`, `it_key` | 15 / 10 cm | Schmiedeeisen |

  Alle Schlüssel-Items (`it_key_chest_hut`, …) nutzen dasselbe Modell `it_key.glb`; die Lua-Items und die Pfade
  legt engine an.
- **Validator:** `gothar-chargen validate` (auch in CI) prüft alles unter `assets/source/items/` mit den Regeln
  `item.skin`, `item.lod`, `item.budget`, `item.origin` (Griffpunkt im Gegenstand, ±2 cm), `item.axis` (längste
  Ausdehnung entlang +Y), `item.size` (Länge je Stück), `item.texture` (Textur vorhanden), `item.stale` (weicht vom
  frischen Bau ab) und `item.unknown` (Warnung: nicht von `build-items` gebaut).
- **Prüfung:** jedes Stück am Socket einer Testfigur in passender Haltung (`1h/s_idle`, `1h/s_run`, `bow/s_idle`,
  `none/t_eat`, `none/t_drink`, `mob/chest/s_picklock`, Gürtel `socket_hip_1h`, Rücken `socket_back_bow`).

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
  **Geprüft** (M6-Durchsicht der Tiere, 2026-10-03; `clipfix.py`): die Root-Geschwindigkeit von `s_walk*`/`s_run*`
  muss der Schrittgeschwindigkeit entsprechen (Füße relativ zu `root` in der Standphase, ±0,1 m/s; `anim.slide`),
  und Liege-/Ruheposen (`t_die*`, `s_sleep*`) dürfen das gehäutete Referenz-Mesh nicht mehr als 2 cm unter den Boden
  bringen (`anim.ground`). `gothar-chargen repair-clips` (auch beim Export) setzt die Root-Geschwindigkeit auf die
  Schrittlänge und hebt `pelvis` je Key gerade so weit an; `speed` in `events.toml` folgt.
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
  (benannte Posen an Schlüssel-Frames, weich überblendet, optional über einer Basis-Schleife; je Pose
  `rotate` um Ruhe-Weltachsen, `move` für root/pelvis und `twist` = Drehung eines Knochens um die eigene
  Längsachse nach der Basis, z. B. Unterarm, damit der Daumen beim Trinken oben ist). Feste Events stehen als
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
