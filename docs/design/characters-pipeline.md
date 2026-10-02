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

- Maßstab 1 Einheit = 1 m, Standardgröße ca. 1,80 m, Y oben, Ausrichtung nach glTF-Konvention.
- `root` auf Bodenhöhe zwischen den Füßen (trägt Root Motion), darunter `pelvis`.
- Knochennamen in `lower_snake_case` mit Seitensuffix `_l`/`_r`.
- **Sockets** (Knochen ohne Gewichte) für Ausrüstung mit Präfix `socket_` – siehe animation.md.
- Gesicht über **Morph-Targets** (Blendshapes), nicht über Gesichtsknochen.
- Ein Blender-Referenzfile `assets/source/characters/rig/human_reference.blend` ist die Quelle der Wahrheit.

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
weil glTF keine Standard-Events kennt; das Werkzeug in F2 erzeugt sie aus Blender-Timeline-Markern.

## 4. Quellen & Lizenzen

| Quelle | Wofür | Lizenz / Hinweis |
|---|---|---|
| **Quaternius** (u. a. Universal Animation Library, Tiere, Platzhalter-Figuren) | Basis-Bewegungen, Platzhalter | CC0 |
| **Mixamo** | ergänzende Basis-Bewegungen | kostenlos, Nutzung in Spielen erlaubt; Bedingungen vor Nutzung prüfen, Rohdateien nicht weitergeben |
| **MPFB2** (MakeHuman für Blender) | Ausgangskörper für eigene Figuren | Ergebnis-Modelle frei nutzbar (vor Nutzung Lizenzhinweise prüfen) |
| **Video-Mocap** (z. B. Rokoko Vision, Move.ai) | Gothic-spezifische Bewegungen, selbst vorgespielt | eigene Aufnahmen; Dienst-Bedingungen beachten |
| **Keyframe in Blender** | Kampf-Feinschliff, Mob-Interaktionen, Monster | eigene Arbeit |

Jede Fremdquelle → Eintrag in `assets/LICENSES.md`. Keine Animationen oder Figuren aus Gothic.

## 5. Werkzeuge (zu entwickeln)

Python, Ordner `tools/chargen/` (Blender-Add-on + Kommandozeile), Tests mit pytest.

1. **Rig-Validator** (F1): prüft `.glb`/`.blend` gegen das Referenz-Rig – Knochennamen, Hierarchie,
   Bind-Pose, Maßstab, Ausrichtung, Sockets, Gewichte ≤ 4 je Vertex, Morph-Target-Namen. Läuft auch in CI
   für alles unter `assets/source/characters/`.
2. **Retargeting-Hilfe** (F2): Mapping-Dateien Quell-Rig → Referenz-Rig (Quaternius, Mixamo, Mocap-Exporte),
   Stapel-Retargeting in Blender, Korrektur-Offsets, Fußkontakt-Prüfung.
3. **Animations-Export** (F2): Clips nach Namenskonvention in Sets packen, Timeline-Marker → `events.toml`,
   Root-Motion-Extraktion bzw. In-Place-Bereinigung je Clip-Einstellung.
4. **Animationslisten-Abgleich** (F2): vergleicht `animation-list.md` mit den vorhandenen Clips →
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
