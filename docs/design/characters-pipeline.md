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
- **Rolle aus dem ersten Wort des Materialnamens** (`beard_head`, `beard_stubble` → Rolle `beard`). Ein in den Kopf
  eingebauter Bart (`head_m_mid`, `head_m_old`) heißt `beard_head` (seit 2026-10-08), damit ein Kit-Bart `beard`
  im Zusammenbau sein eigenes Material behält (assemble führt Materialien nach Namen zusammen, der Kopf zuerst).
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

[clips."1h/t_attack_combo1"]
events = [
    { frame = 2,  event = "sound:swing_light" },
    { frame = 17, event = "hit_start" },
    { frame = 23, event = "combo_start" },
    { frame = 23, event = "hit_end" },
    { frame = 34, event = "combo_end" },
]
```
- **Kampf (M11, mit engine vereinbart 2026-10-05):** Jeder Angriff beginnt und endet in der Kampfhaltung seines
  Modus (`<modus>/s_idle`); engine blendet 0,1 s zum nächsten Schlag, jeder Clip taugt als Einzelschlag.
  - Angriffe tragen das Trefferfenster `hit_start`/`hit_end` (Fäuste: Aufprall der Faust) und das Kombo-Fenster
    `combo_start` (= `hit_end`) bis `combo_end` (ca. 80 % des Clips), danach Erholung.
  - Richtungshiebe `t_attack_l/r` nur mit `hit_*`.
  - `t_parry` ohne Events, ca. 0,6 s (Blockfenster setzt engine).
  - `t_dodge_back` mit Root Motion ca. 0,8 m rückwärts, ohne Events.
  - `t_ko` endet in der Pose von `s_ko`, `t_ko_getup` beginnt dort; `t_die_front/back` enden liegend ohne Schleife.
  - Talentstufen nur über die Abspielrate, je Name genau ein Clip.
  - Werkzeug: Rezept `framed` (Clip aus der Haltung ein- und in sie ausblenden, `stance_frame` für eine feste Pose,
    `soften` glättet Knochen, deren Quelle zu ruckartig ist), eigene Schläge über `none/s_idle` geschrieben
    (hängende Arme: die Weltachsen wirken wie notiert) und mit `framed` in die Waffenhaltung gesetzt.
  - Zweihänder: Rezept `two_hands` setzt die linke Hand je Bild auf den Griff unter der rechten (`grip` Meter gegen
    +Y von `socket_hand_r`, CCD über Ober- und Unterarm, Warmstart aus dem Vorbild – stetige Lösung) und dreht sie wie
    die rechte. Fernkampf: `s_aim` (Schleife), `t_shoot` mit Event `release`, `t_reload`; Bogen-Posen per Gittersuche
    (Bogenarm gestreckt, Bogen aufrecht mit der Sehne zum Körper, Sehnenhand an der Wange, Köcher hinter der Schulter).
- Event-Namen: `lower_snake_case`, optional mit Argument nach Doppelpunkt (`sound:<name>`).
  Bekannte Events siehe `docs/modules/animation.md` („Clip“); neue Events nach Absprache mit engine.
- Clips ohne Events und ohne `speed` werden weggelassen; die Datei ist optional.
- **Quelle der Events (Koordinator 2026-10-08):** die Specs (`data/clips/<set>.toml`, `markers`) bzw. die daraus
  erzeugte `<set>.events.toml`; erkannte Events (`footstep_*`, `land`) und `speed` misst der Bau aus der Bewegung.
  Ändern sich nur Marker, schreibt `gothar-chargen build-set --events-only <set>` die `events.toml` neu (ohne
  Blender; erkannte Events und `speed` bleiben). Die Pose-Marker in den `.blend` sind nur Hilfe und dürfen veralten,
  bis die `.blend` aus einem echten Grund neu gebaut wird; Validator und Tests stützen sich nicht auf sie (Test
  `test_events_files_hold_the_spec_markers` prüft Specs gegen `events.toml`). `.blend` nur committen, wenn sich
  Geometrie oder Keys ändern.
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
  Amboss Arbeitshöhe 0,8 m, Bett 2,0×0,9 m (Liegefläche 0,45 m), Türklinke 1,0 m, Bank Sitzhöhe 0,45 m,
  Tiefe 0,35 m, Länge 1,5 m (Slot 0,38 m vor der Mitte, Blick von der Bank weg). Das Bett liegt entlang X
  (Längsseite zu +Z, Slot davor), **Kopfende −X**: Die Figur liegt entlang X, Kopf bei −X (vom Slot aus links).
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

### 3.2 Additive Clips: Dialog-Gesten (Vertrag engine–figuren, Zusammenführung Koordinator 2026-10-04)

- **Namen:** Typ `a_` = additiv, Set `dlg`, Clips `dlg/a_<x>`; gehaltene Posen als `a_<x>_in` / `a_<x>` / `a_<x>_out`.
- **Referenz:** engine legt alle `dlg`-Clips additiv gegen **`dlg/a_neutral`** (1 Frame = erster Frame von
  `none/s_idle` ohne Atmung); `playOverlay` bekommt dafür einen Referenz-Clip. So halten Schleifen und `_out`
  ihre Pose. Ohne Referenz-Clip gilt Frame 0 des Clips als Referenz.
- **Maske ab `spine_02`** (mit Schlüsselbeinen, Armen, Händen, Hals, Kopf); Becken und Beine bleiben auf der
  Referenz (additiv null). **Nur Rotationen**, keine Translationen.
- **Anfang und Ende:** Einzelgesten und Redeschleifen beginnen und enden in der Referenzpose, `_in` beginnt und
  `_out` endet dort, gehaltene Schleifen sind geschlossen.
- **Kopf und Hals nur wenig:** Das Look-At dreht den Kopf zum Gesprächspartner; Ausnahmen sind Nicken und
  Kopfschütteln. Mund und Gesicht machen die Morphs (§6.1), nicht die Clips.
- **Längen:** Redeschleifen 2–4 s, Einzelgesten 0,8–2 s (Erklären, Drohen bis 2,7 s). Optionales Event `beat`
  auf der Betonung.
- **Werkzeug:** Clip-Option `additive = true` (nur `a_*`; alles außerhalb von `spine_02` hält Frame 0); Validator
  `anim.additive` prüft die Regeln oben gegen `<modus>/a_neutral`.
- **Hinweis für das Abspielen:** Die Armgesten sind auf freie Arme ausgelegt (Stehen, auf der Bank sitzen). Über
  Haltungen mit belegten Armen (verschränkt, Arme um die Knie) ergeben sie seltsame Posen; dort die Arme vorher
  lösen oder nur Kopfgesten spielen.

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
   Root-Motion-Extraktion bzw. In-Place-Bereinigung je Clip-Einstellung. Je Clip optional `in_place = true`
   (kein waagrechtes Wandern von root und Becken, für Quellen, die laufen) und `close = N` (nur Schleifen:
   die letzten N Frames blenden in den ersten über, für Quellen, die nicht geschlossen sind); `chain = [a, b, …]`
   hängt früher gebaute Clips des Sets hintereinander (z. B. zum Gürtel greifen, dann in die Haltung).
4. **Animationslisten-Abgleich** (F2, `gothar-chargen report`, umgesetzt): vergleicht `animation-list.md` mit den vorhandenen Clips →
   Fortschrittsbericht (fehlend / Platzhalter / fertig).
5. **Figuren-Baukasten** (F3, `gothar-chargen assemble`, umgesetzt): setzt Körper/Kleidung + Kopf + Haare
   aus einem Manifest zusammen, prüft Passform (Nähte, Gewichte), erzeugt LODs mit festen Rändern; Varianten über
   Farbpaletten (seed-basierte Varianten später).
6. **Gegenstände** (F6, `gothar-chargen build-items`, umgesetzt): Waffen und Handgegenstände per Code, §6.3.
7. **Haut durch Kleidung** (F3o, `gothar-chargen poke`, umgesetzt): misst in Bewegung, §6.2.

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
- **Stoppeln (eigene Arbeit, 2026-10-08, Entscheidung Projektinhaber):** `gothar-chargen stubble [köpfe]` baut für
  die 8 Männerköpfe `parts/hair_m_<kopf>/beard_stubble.glb` (Rolle `beard`; Blender-Skript `build_stubble.py`,
  Einstellungen und Textur `stubble.py`). Eine Hülle 0,8 mm über der Haut der Bartzone (aus der Nasenspitze: unter
  der Nase bis zur oberen Kehle, nach hinten bis zu den Koteletten, Lippen frei, weiche Ränder); je LOD aus der
  vollen Zone reduziert (600 / 300 / 120 Dreiecke). Textur `textures/hair/beard_stubble_neutral.png` (512², neutral
  grau, Mittel 0,55, 2×2-px-Härchen, Alpha MASK) mit vier Dichtebändern (13 / 9 / 5 / 2 %): jede Fläche nimmt
  die UVs aus dem Band ihrer Zonendichte, so dünnen die Stoppeln zum Rand hin aus. Gewichte und die 15 Morphs
  (nur lod0) vom nächsten Hautpunkt – die Stoppeln gehen mit `vis_aa` mit. **Material und Palettenschlüssel
  `beard_stubble`** (Rolle `beard` über das erste Wort): nicht `beard`, weil `head_m_mid` und `head_m_old` selbst
  ein Material `beard` tragen, das beim Zusammenbau gewinnt. Die Dichte ist für die Nahansicht abgestimmt; engine
  erhält die Alpha-Bedeckung über die Mip-Stufen (Castaño). Ein eigener **Vollbart** ist zurückgestellt
  (Projektinhaber 2026-10-08: später mit besserer Technik).
- **Haare/Bärte:** eigene Meshes, an Köpfe angepasst. **Frisur-Kits (F3v):** Rezept `humans/hair_<kopf>` mit
  `fit_to = "head_<kopf>"`, `parts = ["hair", "beard"]` und `[assets] hairs = [...]`, `beards = [...]` → je
  Stil ein Teil `parts/hair_<kopf>/hair_<stil>.glb` bzw. `beard_<stil>.glb` (Rollen `hair`, `beard` im Manifest;
  Knoten `hair_lod<n>` bzw. `beard_lod<n>`). Die Kits erben Makros, Form und Haut vom Kopf-Rezept, sitzen also
  genau auf dem Kopf (auch nach dem Anheben auf Halshöhe). Texturen **neutral grau** und je Stil geteilt
  (`textures/hair/<stil>_neutral.png`, Mittel 0,55): die Haarfarbe gibt die Palette (`hair`, `beard`). Budget je
  Frisur `triangles` (1200), je Bart 600; Bärte tragen die 15 Gesichts-Morphs (§6.1). 13 Köpfe (8 m, 5 w),
  Frisuren: Männer `hair_messy`, `hair_long_shaggy`, `hair_tousled`, `hair_buzz`, `hair_short`, `hair_cropped`,
  `hair_long`, `hair_ponytail`, `hair_curly`; Frauen dazu `hair_braid`, `hair_bob`; Bärte `beard_goatee`,
  `beard_moustache`, `beard_faun`, dazu eigene **Stoppeln** `beard_stubble` (unten). **Nur CC0 laut Datei-Kopf:** Viele Stücke der Pakete Hair 01 und Bodyparts 05
  tragen im `.mhclo`-Kopf AGPL3 oder CC BY und werden nicht verwendet (Paketseite allein genügt nicht).
  **Automatisch geprüft:** `gothar-chargen human` (vor dem Blender-Lauf) und `gothar-chargen licences` lesen die
  `license`-Zeile jedes Assets eines Rezepts (`.mhclo`, `.mhmat`, das vom `.mhclo` genannte Material) und lehnen
  alles außer CC0 ab. Dateien **ohne** Lizenzzeile (Systemassets, Cortu, ambientCG) gehen nur aus Ordnern, die in
  `tools/chargen/src/gothar_chargen/data/asset_licences.toml` mit ihrer CC0-Quelle (Zeile in `assets/LICENSES.md`)
  stehen; neue Ordner erst nach Prüfung des Pakets eintragen.
- **Kleidung/Rüstung:** je Gilde/Stand eine Linie (Lumpen → leicht → mittel → schwer). **Kleidungs-Kit (F3e):**
  jedes Kleidungsstück ist ein eigenes Teil, je Statur angepasst – Rezepte `humans/cloth_<m|f>_<statur>` mit
  `fit_to = "body_<…>"` (Makros, Haut, Augen vom Grundkörper) und `parts = ["cloth"]` → `parts/cloth_<…>/<stück>.glb`
  (je ~1,5 k Dreiecke). Das Figur-Manifest listet die Stücke unter `[parts] cloth = [...]` (Rolle `cloth_<stück>`,
  Knoten `cloth_<stück>_lod<n>`). `assemble` löscht die Körperflächen darunter (Strahl entlang der Normalen trifft das
  Stück innerhalb 3 cm von innen, oder der Körper ragt bis 1,5 cm heraus – bis 4 cm, wo der Körper selbst Kleidung
  trägt, z. B. die eingebaute Hose unter enger Rüstungshose; Löcher in zerrissener Kleidung und ein
  5-cm-Streifen an der Halsnaht bleiben). Weite Stücke mit `[inside]` im Kit-Rezept (Stück → Meter; der lange Rock:
  0,3) verdecken zusätzlich die Haut in ihrem Inneren – waagrechte Strahlen treffen das Stück von innen in mindestens
  zwei von vier Richtungen –, damit Oberschenkel beim Hocken nicht durch den weit abstehenden Rock stechen
  (2026-10-05; vorher bis 25 cm² an schlanken Frauen). Kit-Texturen sind **neutral grau** (halber Kontrast, Helligkeit 0,55) und
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
- **Alltagskleidung (F3v):** Rezepte `humans/garb_<m|f>_<statur>` → `parts/garb_<…>/<stück>.glb`, eigene Kits
  neben den Kleidungs-Kits (die bestehenden Teile bleiben unverändert). Stoffe über `data/fabrics.toml` gebacken
  (neutral, Farbe über die Palette).
  - CC0 laut Datei-Kopf: `shirt` (`toigo_basic_tucked_t-shirt`, sauber für Bürger), `wide_trousers`
    (`toigo_harem_pants`, Männer), `skirt_tiered` (`toigo_tiered_skirt`, Frauen, sauber).
  - Aus der langen CC0-Kutte (`donitz_monk_robe`, um 12 mm gelockert, sonst schneidet die reduzierte Kutte in Beine
    und Stiefel), nur Männer: `coat` (langer Rock bzw. Kutte; mit `hood` aus dem Kopf-Kit ein Kapuzenmantel),
    `tunic` (Saum unter dem Knie, mit der Kordel der Kutte gegürtet). Die Kutte folgt jedem Bein einzeln; als
    Kleid bei Frauen stachen deshalb die nackten Beine im Laufen durch (`poke` 91 cm²) – Bürgerinnen tragen Hemd
    oder Mieder mit dem sauberen Stufenrock.
  - Sauber für Leonberg: `coat_clean`, `tunic_clean` – dieselbe Geometrie (zweites `derive` mit gleichen Werten),
    anderes Bild (`wear` 0,25). Nur diese Stücke doppelt (Größe); Hemd und Stufenrock sind gleich sauber.
  - **Eigene Geometrie:** `apron` (Leinwand, ambientCG „Fabric 063“) und `apron_leather` (Handwerker, „Leather 014“)
    als Stoffbahn vor dem Körper vom Knie bis zur Taille (`panel = breite`): Auf dem Körper (Taille, Hüfte) ist sie so
    breit wie er und legt sich um die Hüften, darunter hängt sie gerade herab statt den Beinen zu folgen, wird zum
    Saum 15 % schmaler und steht etwas ab (über Röcken); leichte Wölbung, nach unten tiefer werdende Falten, die
    Seiten fallen zurück, der Saum hängt in den Falten länger (2026-10-05, vorher eine steife Tafel). Ränder mit `rim`.
    `belt`: Ring der Haut an der Taille, abgesetzt, mit Rand. `straw_hat`: Kuppel mit breiter Krempe wie
    `kettle_helm`, flacher (ambientCG „Wicker 013“), blendet das Haar aus.
  - Werkzeug: `band = [unten, oben]` mit `band_at = "<gelenk>"` oder `["<gelenk unten>", "<gelenk oben>"]` –
    saubere Schnitte in festen Höhen relativ zu Gelenken des Referenz-Rigs (Säume, Gürtel), statt nach
    Knochengewichten (zackig); `panel` (s. o.).
  - **Handwerker und Händler (Leonberg, 2026-10-08, Outfit-Liste vom Projektinhaber freigegeben):** eigene Teile je
    Statur, Texturen ambientCG (CC0):
    - Latzschürzen `apron_bib_flour` (Bäcker, m/w; „Fabric 066“, Mehlstaub), `apron_bib_clay` (Töpfer; „Fabric 045“,
      Tonflecken), `apron_bib_leather` (Metzger; „Leather 033 A“, zurückhaltende dunkle Flecken): `panel` bis zur
      Brust (`band_at = ["calf_l", "spine_03"]`) mit **`bib`** = Breite über der Taille (0,20 m). Der Latz liegt
      auf der Brust (folgt dem Körper bis zur Taille, dort gebunden), darunter hängt die Schürze wie `apron`.
    - `vest_cloth` (Schneider, Goldschmied; Wollstoff „Fabric 031“): das saubere CC0-Hemd (`shirt`) ohne Ärmel,
      darüber (das grobe Hemd hat Löcher in der Geometrie).
    - `purse` (Geldbeutel, m/w; „Leather 014“): **`pouch = [breite, höhe, tiefe]`** – eigene Geometrie, ein kleiner
      Beutel am Gürtel vorn rechts (oben voller, unten gerafft), steif auf `pelvis`; gut lesbar für den
      Taschendiebstahl.
    - Kopf-Kits, alle aus der CC0-Kapuze (`donitz_monk_robe_hood`), damit sie weich mit Falten fallen (die eigene
      Kuppel wirkt wie ein Helm): `cap_cloth` (Leinenmütze des Bäckers, m; Kapuze oben, `depth` 0,14, Leinen
      „Fabric 066“), `beret` (Barett, m; flache Filzkappe, `depth` 0,09, „Fabric 068“), `coif` (Haube bzw. Kopftuch, w;
      die Kapuze enger gezogen, „Fabric 066“). Alle blenden das Haar aus.
    - Texturen: abgeleitete Teile teilen ihre Textur über den Namen der Quelle, deshalb hat jede verschmutzte
      Schürze eine eigene ambientCG-Quelle (`fabric066_neutral.jpg`, `fabric045_neutral.jpg`,
      `leather033a_neutral.jpg`), gebacken mit `soil` (unten).
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
  `wear`. **Spuren des Handwerks** (2026-10-08): `soil = "flour" | "clay" | "dark"` mit `soil_amount` 0–1 –
  Mehlstaub (hell gesprenkelt), Ton (rötlich-grau), dunkle Flecken (zurückhaltend, kein Blutrot); nach der Alterung
  gezeichnet, Texturen ohne `soil` bleiben unverändert. Die Texturen behalten ihre Namen, die Teile ändern sich nicht. Kit-Texturen bleiben neutral grau (die
  Palette färbt), die Kleidung der Grundkörper getönt wie bisher.
- **Ausgefranste Säume (F3o, mit engine abgestimmt):** `fray` 0–1 je Textur in `fabrics.toml` schneidet ein
  unregelmäßiges, gezacktes Band entlang der **Säume** aus dem Stoff (Alpha 0; bis 14 px bei 512², etwa 4 cm
  am Hemd). Säume sind die offenen 3D-Kanten des Stücks (Saum, Ärmel, Kragen), nicht seine UV-Nähte. Solche
  Texturen sind PNG mit Alpha; `gothar-chargen fabrics` stellt die Materialien aller Teile, die sie nutzen, auf
  `alphaMode` `MASK`, `alphaCutoff` 0,5 und `doubleSided` um (kein BLEND). Damit man durch die Lücken Haut
  und nicht das Innere der Figur sieht, behalten die Abdeck-Masken solcher Stücke den Körper bis 5 cm um
  ihre Säume (§6.2). Fransen haben die groben Hemden (Lumpen, `fray` 0,7) und der lange Rock (0,5); Pullover,
  Mieder und Schuhe nicht. Nach `fabrics` → `part-data`.
- **Budget:** Körper+Kleidung 8–15 k Dreiecke, Kopf 3–5 k (mit Bart bis ~5,6 k), höchstens 20 k je Figur, Stufen
  `_lod1`/`_lod2` (Vertrag §2.2). Lose Teile (Haare, Bärte) werden ohne Randschutz reduziert; nur die Nahtränder von
  Körper und Kopf bleiben in allen Stufen gleich.

### 6.1 Gesichts-Morph-Targets (Vertrag mit engine, abgestimmt 2026-10-03, Zuordnung nach Namen seit M10)

- **15 Targets** (empfohlene Reihenfolge): Viseme `vis_aa`, `vis_ee`, `vis_ih`, `vis_oh`, `vis_ou`, `vis_mbp`, `vis_fv`,
  `vis_l`; Blinzeln `blink_l`, `blink_r`; Ausdrücke `expr_angry`, `expr_friendly`, `expr_fear`, `expr_pain`,
  `expr_sleep`. „Ruhe“ = alle Gewichte 0 (kein eigenes Target). Höchstens **16 Targets je Mesh** (Gewichts-Palette des
  Skinning-Shaders); mehr nur nach Absprache mit engine.
- **Ort:** nur auf `head_lod0` (LOD-Vertrag §2.2); in lod1/2 keine Mimik. **Jedes Mesh im Kopf-Teil** (Haut, Augen,
  Brauen, Wimpern, Zähne, Zunge – im glTF Primitive desselben Meshes) trägt **dieselbe vollständige Liste**, jeden
  Namen einmal, auch wenn ein Target es nicht bewegt. engine ordnet die Gewichte **nach Namen** zu (seit M10, #188);
  die Reihenfolge oben ist nur empfohlen (Validator: andere Reihenfolge `morph.order` als Warnung, fehlende oder
  doppelte Namen `morph.set` als Fehler).
- **Format:** Namen in `mesh.extras.targetNames`, Positionen **und Normalen**, keine Tangenten; sparse Accessoren;
  Standardgewichte 0. Gewichte 0–1, additiv, beliebig viele gleichzeitig (Lippensync + Blinzeln + Ausdruck).
- **Seiten:** `blink_l` = linkes Auge der Figur (+X), wie `*_l` im Rig.
- **„Mund offen“ für Lippensync nach Lautstärke (M13, mit engine 2026-10-08):** `vis_aa` – in allen Köpfen und in
  den Bärten gleich benannt (Bärte tragen dieselbe Target-Liste, der Bart geht mit dem Kiefer mit).
- **Halsnaht:** Morphs bewegen den Nahtring nicht (kein Spalt beim Sprechen).
- **Gilt für alle Teile mit Morphs**, z. B. `beard` (Frisur-Kits, F3v): dieselben 15 Namen; der Bart folgt so dem
  Kiefer. Bis M10 ordnete engine nach Index zu, seit #188 nach Namen (Absprache 2026-10-04).
- **Herkunft:** gemischt aus MPFB2-Gesichtszielen der CC0-Pakete „Visemes 02“ (Meta-Viseme) und „Faceunits 01“
  (ARKit-Einheiten); die Mischung steht als Daten in `tools/chargen/src/gothar_chargen/data/faces/morphs.toml`
  (z. B. `expr_friendly` = Lächeln + Wangen + leichtes Augenkneifen) und kann ohne Code angepasst werden.
  `gothar-chargen human` überträgt die Targets durch Rig-Anpassung und Reduktion (baryzentrisch vom unreduzierten
  Mesh); Köpfe haben dafür Zähne und Zunge. Der Validator prüft Namen, Vollständigkeit je Primitive, die Reihenfolge (Warnung)
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
  `cloth = [...]` (Rollen `cloth_<stück>`), `[palette]` Materialname → `#rrggbb` (wird `baseColorFactor`),
  optional `[anim] variant = "woman" | "military" | "old" | "relaxed"` (Gangart-Variante, mit engine
  2026-10-08: engine spielt `none/X_<v>` aus `anims/human/gait.glb` statt `none/X`, wenn es den Clip gibt). Die
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
    Bei ausgefransten Stücken (Material `MASK`) bleiben Körper-Dreiecke bis 5 cm um die Säume erhalten.
  - Grundkörper mit eigener Kleidung (Hose, Unterwäsche; F3o): `part-data` entfernt die Haut darunter aus dem
    Körper-Teil selbst (Regel wie bei den Masken, Abstand bis 6 cm, weil die Hose der dünnen Statur lockerer
    sitzt). Sie wird nie gebraucht und stach in Bewegung durch den groben Stoff. Kein Formatwechsel: Die
    Indexpuffer werden kürzer, der Laufzeit-Zusammenbau bleibt gleich.
  - **Prüfung in Bewegung** (`gothar-chargen poke`, Regel `fit.poke_motion`): Die Figur wird mit Clips des
    Referenz-Rigs gehäutet (reines numpy; Knochenlängen der Figur, Drehungen vom Clip), und gemessen wird die
    Haut, die in Ruhe unter Kleidung lag und im schlimmsten Frame frei liegt (Fläche in cm², Knochen).
    Säume zählen nicht. Fehler über 20 cm², Warnung über 5 cm² je Clip; Standard: die 5 Test-NPCs mit
    Idle, Gehen, Rennen, Schleichen, `1h`-Haltung und Aufheben.
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
  | `it_broom` (Besen, Ursprung im oberen Griff, +Y zum Reisig) | 1,4 m | Holz, Reisig (prozedural), Bindung |
  | `it_mug` (Krug, Ursprung am Henkel, +Y nach oben) | 12 cm | dunkles Holz (Wood 060) |
  | `it_axe` (Axt, Ursprung am unteren Stiel, Schneide zu +Z) | 0,74 m | Holz, Schmiedeeisen |
  | `it_arrow`, `it_bolt` (Pfeil, Armbrustbolzen; Ursprung in der Schaftmitte, +Y zur Spitze, +Z Federebene – Projektil, steckend: engine setzt um halbe Länge − 7 cm zurück; Nocke −0,375 bzw. −0,175 m; mit engine abgestimmt 2026-10-05) | 0,75 / 0,35 m | Holz, Schmiedeeisen, Federn (prozedural); 92 / 76 Dreiecke |
  | `it_sword_2h` (Zweihänder; Ursprung am Griffpunkt der rechten Hand unter der Parierstange, die linke Hand 10 cm darunter per `two_hands`, +Y zur Klinge, +Z Schneide) | 1,43 m | Schmiedeeisen, Ledergriff; 708 Dreiecke |
  | `it_rune_firebolt`, `it_rune_heal`, `it_rune_sleep`, `it_rune_transform_wolf`, `it_rune_summon_wolf` (Runen, M12: eine Geometrie, flacher Stein, Ursprung Mitte, +Y lange Achse, +Z Zeichen-Seite; je Rune eigenes Material `rune_<zauber>` mit eingeritztem Zeichen in der Farbe des Zaubers – Flamme, Kreuz, Mondsichel, Spirale, Ring mit Stern) | 6 cm | Stein, Zeichen (prozedural); 180 Dreiecke |
  | `it_scroll` (Spruchrolle, Ursprung Mitte, +Y Rollenachse, Siegel zu +Z) | 20 cm | Pergament, Kordel, Wachs |
  | `it_torch` (Fackel, F6 2026-10-08: krummer Holzstab, Kopf aus gewickelten Pechlumpen mit Schnur; Ursprung = Griff nahe dem unteren Ende, +Y zum Kopf; leerer Knoten `socket_flame` an der Kopfspitze (0, 0,60, 0), +Y nach oben, für Flamme und Licht der engine; linke Hand, Clips im Set `torch`) | 72 cm | Holz dunkel, Pech (prozedural), Kordel; 564 Dreiecke |
  | `it_potion_mana_small` (wie der Heiltrank, blaues Glas) | 17 cm | prozedural |
  | `it_crossbow` (Armbrust, Ursprung am Griff bzw. Abzug, Schaft +Y nach vorn, Bogen quer, +Z oben; an `socket_hand_r`, die `cbow`-Clips halten den Schaft in der Faust) | 0,8 m | Holz, Schmiedeeisen, Sehne |

  Alle Schlüssel-Items (`it_key_chest_hut`, …) nutzen dasselbe Modell `it_key.glb`; die Lua-Items und die Pfade
  legt engine an.
- **Validator:** `gothar-chargen validate` (auch in CI) prüft alles unter `assets/source/items/` mit den Regeln
  `item.skin`, `item.lod`, `item.budget`, `item.origin` (Griffpunkt im Gegenstand, ±2 cm), `item.axis` (längste
  Ausdehnung entlang +Y), `item.size` (Länge je Stück), `item.texture` (Textur vorhanden), `item.stale` (weicht vom
  frischen Bau ab) und `item.unknown` (Warnung: nicht von `build-items` gebaut).
- **Prüfung:** jedes Stück am Socket einer Testfigur in passender Haltung (`1h/s_idle`, `1h/s_run`, `bow/s_idle`,
  `none/t_eat`, `none/t_drink`, `mob/chest/s_picklock`, Gürtel `socket_hip_1h`, Rücken `socket_back_bow`).

### 6.4 Figuren-Sets und Gilden-Figuren (Vertrag mit engine, F3v, vereinbart 2026-10-04)

- **Datei:** `assets/source/data/figure_sets.toml`, gepflegt von figuren:
  `[sets] <name> = ["characters/figures/<manifest>.figure.toml", …]` (Pfade relativ zu `assets/source`).
- **Nutzung (engine):** Ein NPC mit `figure_set` und ohne eigenes `figure` bekommt eines der Manifeste, zufällig, aber je
  NPC stabil; `Npc.figure` hat Vorrang. Die Set-Namen sind unabhängig von den Gilden.
- **Je Geschlecht** (2026-10-05, Wunsch engine: `Npc` hat kein Geschlechtsfeld, eine Bäckerin bekam aus `citizen` eine
  Männerfigur): `<set>_m` und `<set>_f` enthalten nur Figuren eines Geschlechts; `<set>` bleibt als Vereinigung
  bestehen. `guard` und `hunter` sind nur Männer (`guard_m` = `guard`, kein `_f`).
- **Sets:** `citizen` (7) und `craftsman` (5) für Leonberg (sauber), `guard` (5, nur Männer wie in Gothic, mittlere
  Rüstung mit Helm), `farmer` (7, abgetragen, Strohhüte, Schürzen), `hunter` (4, nur Männer, Leder, gewickelte Hosen
  und Stiefel), `outcast` (5, Lumpen; barfuß oder in Stoffschuhen). Manifeste `figures/<set>_<m|f>_<n>.figure.toml`;
  sie mischen Kopf, Frisur und Bart, Statur, Kleidung und Palette.
- **Benannte Figuren** (je ein NPC, in keinem Set): `smith` (Lederschürze), `innkeeper` (sauber, Schürze),
  `market_woman` (Mieder über dem langen Rock), `gate_guard` (mittlere Rüstung, Kesselhelm), `guard_captain`
  (schwere Rüstung).
- **Handwerker und Händler Leonbergs** (benannte Figuren, 2026-10-08; Rollen, Geschlecht, Alter und Statur vom
  Koordinator, Personennamen vergibt engine im Inhalt): `baker` (m, kräftig; Latzschürze mit Mehl, Leinenmütze),
  `baker_wife` (w; Latzschürze mit Mehl, Haube), `butcher` (m, kräftig; Leder-Latzschürze, Stoppeln), `bather`
  (m; sauberes Hemd, Leinenschürze, Geldbeutel), `goldsmith` (m, alt, dünn; grüne Tunika, Weste, Barett,
  Geldbeutel), `cloth_merchant` (w; Mieder und langer Rock in gefärbtem Tuch, Haube, Geldbeutel), `tailor` (m,
  dünn; Hemd und Weste), `joiner` (m; Lederschürze, Lederkappe, Stoppeln), `potter` (m, dünn; Latzschürze mit
  Ton, Leinenmütze, Ziegenbart), `herbalist` (w, alt, dünn; Mieder in Moosgrün, brauner Rock, Kapuze,
  Geldbeutel). Gangart: Frauen `woman`, die Alten `old`.
- **Kombinationsregeln** (aus den Prüfbildern und `poke`):
  - Das Mieder liegt nur ohne Hemd über dem langen Rock sauber, nicht über Hemden oder dem Stufenrock.
  - Die Lederweste nur über dem groben Hemd oder dem Pullover (aus dem groben Hemd abgeleitet).
  - Der lange Rock passt seit `[inside]` auch schlanken Frauen (vorher stachen beim Hocken die Oberschenkel durch).
  - Keine Schürze über dem vollen langen Rock kräftiger Frauen.
- **Prüfung:** Jedes Manifest wird beim Bauen zusammengesetzt (`assemble`); `poke` bleibt für alle Gilden-Figuren unter
  der Schwelle. Der Test `test_figure_sets` prüft, dass jedes gelistete Manifest existiert, in genau einem
  Geschlechter-Set steht, jedes `<set>` die Vereinigung seiner Geschlechter-Sets ist und keine benannte Figur darin steht.

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
- **Kanäle:** Translation nur auf `root`/`pelvis`, keine Skalierung (wie §3). **Root Motion:** `s_walk`/`s_trot`/`s_run`
  bewegen `root` vorwärts (+Z, Geschwindigkeit = Schrittlänge, Füße stehen), `t_turn_l/r` drehen `root` um +Y (links
  positiv); alle anderen Clips bleiben am Ort. `wolf/s_trot` (Trab) ist ein mittlerer Blend-Punkt (mit engine
  vereinbart 2026-10-05); andere Arten haben nur `s_walk` und `s_run`.
- **Gangarten (F5, 2026-10-05):** eigene Zyklen mit dem Rezept `gait` (`gaits.py`) statt der zu langsamen Quell-Clips.
  - Jedes Bein setzt bei seiner `phase` auf und steht `duty` des Zyklus in der Welt still, während `root` mit genau
    `speed` vorwärts geht; Ober- und Unterschenkel per Zwei-Knochen-IK in der Seitenebene, der Fuß flach.
  - Gestreckte Beine (Wolf vorn, Laufvogel) erreichen weiter vorn bzw. hinten liegende Füße nur, wenn die Hüfte
    sinkt: Becken senken und kippen wird je Bild aus den Standbeinen berechnet und über den Zyklus geglättet
    (das Tier federt über das Standbein).
  - Tempo (m/s, = `speed` in `events.toml`, Blend-Punkte setzt engine): Wolf 1,2 / Trab 3,0 / 6,0, Keiler 1,0 / 5,0,
    Laufvogel 1,3 / 6,5 – alle schneller als der rennende Held (4,0). Füße rutschen höchstens 3 %.
  - `phase · period` und `duty · period` auf ganzen Bildern, sonst zählt ein halb gesetzter Fuß als Standbein und
    `clipfix` passt die Root-Geschwindigkeit falsch an (Standphase erkennt `clipfix` jetzt nur bei Bodenkontakt an
    beiden Enden eines Schritts: schnelle Gangarten stehen nur zwei, drei Bilder).
  **Geprüft** (M6-Durchsicht der Tiere, 2026-10-03; `clipfix.py`): die Root-Geschwindigkeit von `s_walk*`/`s_run*`
  muss der Schrittgeschwindigkeit entsprechen (Füße relativ zu `root` in der Standphase, ±0,1 m/s; `anim.slide`),
  und Liege-/Ruheposen (`t_die*`, `s_sleep*`) dürfen das gehäutete Referenz-Mesh nicht mehr als 2 cm unter den Boden
  bringen (`anim.ground`). `gothar-chargen repair-clips` (auch beim Export) setzt die Root-Geschwindigkeit auf die
  Schrittlänge und hebt `pelvis` je Key gerade so weit an; `speed` in `events.toml` folgt.
- **Events:** `footstep_front_l/r`, `footstep_back_l/r` (Vierbeiner; Zweibeiner `footstep_l/r`), `hit_start`/`hit_end`
  im Angriff, `sound:<name>`. **Tierlaute (M13, Wunsch engine 2026-10-08):** in allen Arten `sound:<art>_attack`
  am Angriffsbeginn (6 Bilder vor `hit_start`; beim Sprung des Bergleu bei `leap_start`), `sound:<art>_hit` im
  Treffer-Clip (Bild 1; Bergleu auch `t_stagger`), `sound:<art>_die` im Sterbe-Clip (Bild 2), `sound:<art>_threaten`
  im Drohen (erste Pose). Die vorhandenen `sound:<art>_call`, `_roar`, `_hiss`, `_grind` bleiben.
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

Keine mehr: Die CC0-Platzhalter `wolf`, `keiler` und `laufvogel` (Quaternius) sind seit 2026-10-08 durch eigene Arten
mit denselben Verträgen ersetzt (§7.4). `gothar-chargen monster` bleibt für spätere CC0-Importe erhalten.

### 7.4 Eigene Arten (`gothar-chargen creature`, ab 2026-10-07)

Die Arten aus `docs/design/monsters.md` entstehen ohne fremde Quellen aus einer Körperbeschreibung
`tools/chargen/src/gothar_chargen/data/monsters/<art>.creature.toml` (Koordinaten wie die Rig-TOMLs: x links,
−y vorn, z oben; `mirror = true` ergänzt die rechte Seite):

- **Knochen** (`[[bone]]`, Namen nach §7.1; `root` und Sockets setzt das Werkzeug) und `[sockets]`.
- **Formen** (`[[shape]]`): `ellipsoid`, `box`, `capsule`/`cone`, `chain` (Kapselkette), `ridge` (Ellipsoide entlang
  einer Linie, z. B. Borstenkamm); `cut` (Ellipsoid, vor dem zweiten Remesh abgezogen: Augenhöhlen, Ohrmuscheln),
  `cut_box` (dünner Spalt in die fertige Oberfläche: Maul); Einzelteile `eye`, `tooth` und `tongue` mit `bone`
  (eigene Geometrie und Materialien `eyes`/`teeth`/`tongue`, starr am Knochen; `eye_glow` macht die Augen
  emissiv); Panzerplatten `plate` (abgeschrägte
  Quader mit dem Körpermaterial, starr am Knochen) und `plate_shell` (Platten in Reihen auf einem Ellipsoid, je
  Platte am nächsten Knochen aus `bones`; `tilt` hebt die Hinterkante – Dachziegel, jede Reihe liegt über der
  nächsten); `tuft_shell` (dieselben Reihen als Haarsträhnen, die in den Körper verschmelzen, z. B. eine Mähne).
- **Farbzonen** (`[zone.<name>]`): Farbe, Zweitfarbe, Fellstrich, Querstreifen, Flecken, hellerer Bauch,
  Hautfalten, Sandstein (`strata`: Schichtung, Korn, Meißelspuren), Relief-Stärke.
- `[orientation]` überschreibt einzelne Paare von `[rig.orientation]` (Quaderbuckel: `up = ["root", "pelvis"]`, der
  Kopf hängt tief vorn).
- **Ablauf** (`blender/build_creature.py`): Formen vereinigen (Voxel-Remesh), Schnitte, glätten, aufs Budget
  reduzieren, UV; Fell-Textur aus der Körperbeschreibung (`creature.py`, 3D-Rauschen, nahtlos über UV-Nähte) mit
  eingebackener Umgebungsverdeckung; Normal-Map aus dem hochaufgelösten Mesh plus Fellstrich-Relief; automatische
  Gewichte (≤ 4), Kiefer entlang des Maul-Schnitts getrennt; LOD-Stufen; Rig-TOML, Kollision und Prüfung wie bei
  `gothar-chargen monster`. Texturen extern (§2.3): `textures/fur/<art>.jpg`, `<art>_normal.png`, geteilte
  `eyes_<rgb>.jpg`/`teeth_<rgb>.jpg`. Clip-Quelle `<art>_clips.blend` (lokal) mit der Ruhe-Aktion `rest`.
- **Clips:** Gangarten mit dem Rezept `gait` (neu: `pose` = feste Haltung, z. B. Kopf tief beim Schleichen;
  `speed = 0` = Treten am Ort als Basis der Drehungen; `in_place = true` = Lauf am Ort, die Eigengeschwindigkeit
  kommt aus den Füßen, z. B. `quaderbuckel/s_charge`), sonst `keyposes` über `<art>/s_rest`.
  Schritte (`phase · period`, `duty · period`) auf ganzen Bildern, sonst passt `repair-clips` das Tempo falsch an.

| Art | Stand |
|---|---|
| `bergleu` | Boss: Rig 41 Knochen (Katzen-Rig mit engine: `spine_01..03`, `neck_01/02`, `jaw`, Ohren, Mähne `mane_back/l/r`, `tail_01..05`, zehengängige Beine mit Schulterblättern und Zehen; Sockets `socket_mouth`, `socket_paw_l/r`, `socket_tail`), Schulter 1,6 m, lod0 11096 / 5488 / 2714 Dreiecke (Boss-Budget bis 12 k), Mähne aus Strähnen (`tuft_shell`); 19 Clips (Mindest-Set + `s_stalk`, `t_pounce` am Ort, `t_tail_lash`, `t_roar`, `t_rage`, `t_stagger`, `t_turn_back`); Tempo 1,5 / 8,0 / Anpirschen 1,0 m/s (mit engine 2026-10-08) |
| `glemsmahr` | Rig 37 Knochen (neues Rig mit engine: `spine_01..03`, `neck_01/02`, `jaw`, Ohren, Arme als Vorderbeine mit Schulter, Hand und je drei Fingern, zehengängige Hinterbeine mit Zehen; Sockets `socket_mouth`, `socket_eyes`, `socket_hand_l/r`), geduckt 1,53 m, Kapsel stehend, lod0 7636 / 3758 / 1848 Dreiecke, emissive Augen (`eye_glow`); 19 Clips (Mindest-Set + `s_sneak`, `t_rise`/`s_upright`/`t_lower`, `t_leap` und `t_jump_back` am Ort mit `leap_start`/`leap_land`, `t_recoil`); Tempo 1,4 / 6,5 / Schleichen 0,9 m/s (mit engine 2026-10-07) |
| `quaderbuckel` | Rig 29 Knochen (Keiler-Benennung mit Schulter-/Hüftknochen, dazu `jaw` und `brow_shield` unter `chest`; Sockets `socket_mouth`, `socket_shield` am Schild), 0,84 m, lod0 7956 / 3978 / 1988 Dreiecke (60 starre Sandstein-Platten), Fell 1024² + Normal-Map 512²; 17 Clips (Mindest-Set + `s_charge` am Ort, `t_warn`, `t_block_in`/`s_block`/`t_block_out`); Tempo 0,8 / 3,5 / Anrennen 4,5 m/s (mit engine 2026-10-07) |
| `wolf` (Rudeltier) | ersetzt den Quaternius-Platzhalter (2026-10-08): Rig 25 Knochen (bisherige 22 mit gleichen Namen, dazu `jaw` und `ear_l/r` mit engine), 0,80 m, lod0 7376 / 3688 / 1844 Dreiecke, Fell 1024² + Normal-Map 512²; graues Fell mit dunklem Sattel, Halskrause, buschiger Schwanz; 13/13 Clips mit denselben Namen, Events und Tempo (1,2 / 3,0 / 6,0 m/s), alle eigen; dazu `t_transform_in/out` für die Verwandlung (M12) |
| `keiler` | ersetzt den Quaternius-Platzhalter (2026-10-08): Rig 28 Knochen (bisherige 25 mit gleichen Namen und Hierarchie, dazu `jaw` und `ear_l/r` mit engine), 0,84 m mit Borstenkamm, lod0 7590 / 3734 / 1836 Dreiecke, Fell 1024² + Normal-Map 512²; dunkles Borstenfell, Borstenkamm (`tuft_shell`), Rüsselscheibe, gebogene Hauer am Unterkiefer, Maul mit Innenraum, kurze kräftige Läufe; 12/12 Clips mit denselben Namen, Events und Tempo (1,0 / 5,0 m/s), alle eigen |
| `laufvogel` | ersetzt den Quaternius-Platzhalter (2026-10-08): Rig 16 Knochen (bisherige 12 mit Vogel-Namen `thigh/calf/foot`, dazu `neck_02`, `jaw` (Schnabel) und `wing_l/r` mit engine), 1,66 m (Kopf mit Federkamm), lod0 6824 / 3412 / 1706 Dreiecke, Federn 1024² + Normal-Map 512²; eigene Silhouette: schieferblaues Gefieder, rostroter Halskragen, Federkamm, Hakenschnabel, Stummelflügel, Schwanzbusch, Schuppenläufe mit drei Krallenzehen; 12/12 Clips mit denselben Namen, Events und Tempo (1,3 / 6,5 m/s), alle eigen |
| `schinder` | Rig 26 Knochen, 0,92 m, lod0 7834 / 3916 / 1958 Dreiecke, Fell 1024² + Normal-Map 512²; 15 Clips (Mindest-Set + `s_sneak`, `t_call`, `s_cower`); Tempo 1,3 / 6,5 / Schleichen 0,7 m/s (mit engine 2026-10-07) |

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
