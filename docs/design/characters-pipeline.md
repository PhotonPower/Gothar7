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

Clip-Namen prüft der Validator gegen das Muster oben (`[sta]_…`, nur `a-z0-9_`, Modus aus der Liste
oder `mob/<mobtyp>`).

## 4. Quellen & Lizenzen

| Quelle | Wofür | Lizenz / Hinweis |
|---|---|---|
| **Quaternius** (u. a. Universal Animation Library 1+2, Tiere, Platzhalter-Figuren) | Rig-Geometrie, Basis-Bewegungen, Platzhalterfigur (`figures/placeholder_mannequin`), F1-Test-Clips | CC0; UAL1/UAL2 „Standard“ direkt von opengameart.org (itch.io blockt automatische Downloads) |
| ~~Mixamo~~ | **wird nicht verwendet** (Entscheidung 2026-10-03) | Das Repo ist öffentlich; Adobe erlaubt die Nutzung in Spielen, aber keine Weitergabe der Animationsdateien – übertragene Clips im Repo wären genau das |
| **MPFB2** (MakeHuman für Blender) | Ausgangskörper für eigene Figuren | Ergebnis-Modelle frei nutzbar (vor Nutzung Lizenzhinweise prüfen) |
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
   (Herkunft je Clip in `data/clips/<set>.toml`: Bibliothek, rückwärts, Überblendung, Verkettung), Pose-Marker → `events.toml`,
   Root-Motion-Extraktion bzw. In-Place-Bereinigung je Clip-Einstellung.
4. **Animationslisten-Abgleich** (F2, `gothar-chargen report`, umgesetzt): vergleicht `animation-list.md` mit den vorhandenen Clips →
   Fortschrittsbericht (fehlend / Platzhalter / fertig).
5. **Figuren-Baukasten** (F3): setzt Körper + Kopf + Haare + Kleidung/Rüstung zusammen, prüft Passform,
   erzeugt LODs; Varianten über Seeds und Farbpaletten.

## 6. Figuren-Baukasten

- **Körper:** 2 Grundkörper (m/w) × 3 Statur-Varianten, aus MPFB2, stilisiert nachbearbeitet.
- **Köpfe:** separates Mesh (wie Gothic), Ziel 20+ Gesichter; gemeinsame Morph-Targets:
  Viseme (`vis_aa`, `vis_ee`, `vis_ih`, `vis_oh`, `vis_ou`, `vis_mbp`, `vis_fv`, `vis_l`, `vis_rest`),
  `blink_l`, `blink_r`, Ausdrücke (`expr_angry`, `expr_friendly`, `expr_fear`, `expr_pain`, `expr_sleep`).
- **Haare/Bärte:** eigene Meshes, an Köpfe angepasst.
- **Kleidung/Rüstung:** ersetzt den Körper (Mesh-Tausch); je Gilde/Stand eine Linie (Lumpen → leicht → mittel → schwer).
- **Texturen:** Trim-Sheets und Farbvarianten statt Unikat-Texturen; Stil passend zu den Häusern (W5).
- **Budget (Richtwert):** Körper+Kleidung 8–15 k Dreiecke, Kopf 3–5 k, 2 LOD-Stufen.

## 7. Monster

Erst CC0-Platzhalter, dann eigene Arten. Pro Art: Rig, Mindest-Set (s_idle, s_walk, s_run, t_attack ×2,
t_hit, t_die, s_eat, s_sleep, t_threaten, t_turn), dazu artspezifisches (Rudelruf, Sprung).
Startliste für den Vertical Slice: wolfsartiges Rudeltier, Keiler, großer Laufvogel (Arten und Namen frei erfunden,
Design in `docs/design/` festhalten).

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
