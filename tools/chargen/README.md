# chargen – Werkzeuge für die Figuren-Spur

Python-Werkzeuge für Referenz-Rig, Rig-Validator und Blender-Export der Figuren und Animationen.
Spezifikation: `docs/design/characters-pipeline.md` · Skelett-Vertrag: `docs/modules/animation.md`
(„Referenz-Skelett“) · Roadmap: `docs/03-roadmap.md` → Figuren-Spur F1–F5.

## Struktur
```
tools/chargen/
  pyproject.toml                  Paket "gothar-chargen", CLI "gothar-chargen"
  src/gothar_chargen/
    data/human_reference.toml     Referenz-Skelett: Namen, Eltern, Sockets, Bind-Pose (T-Pose), Morph-Target-Namen;
                                  Körpergeometrie aus dem Quaternius-Rig (CC0)
    data/mappings/*.toml          Knochen-Zuordnung Quell-Rig → Referenz-Rig (quaternius_ual1, quaternius_ual2)
    data/clips/<set>.toml         Herkunft je Clip eines Animations-Sets (Format: clipspec.py)
    data/monsters/<art>.toml      Monster-Rig je Art (Vertrag §7.1); <art>.build.toml: wie die CC0-Quelle umgebaut wird
    skeleton.py, mapping.py       Laden der Skelett-Definition (spiegelt *_l → *_r) bzw. der Zuordnungen
    clipspec.py, report.py        Clip-Listen lesen/prüfen; Abgleich animation-list.md ↔ Clips
    figure.py                     Figur-Manifeste figures/<name>.figure.toml (Teile, LOD-Anteile, Palette)
    human.py                      Menschen-Rezepte humans/<name>.human.toml (MPFB-Makros, Assets, Tönungen)
    faces.py, data/faces/         Gesichts-Morph-Targets: Vertragsnamen und ihre MPFB-Quellziele (morphs.toml)
    images.py                     PNG/JPEG-Kopf lesen (Größe, Format, Alpha) für die Textur-Regeln
    meshdata.py, fit.py           Mesh-Daten (Ränder, Gewichte); Passform-Prüfung (Nähte, LOD-Ränder)
    gltf.py                       kleiner .glb-Leser/-Schreiber (Accessoren, Knoten-Transformationen)
    validate.py                   Rig-Validator (Prüfungen siehe unten)
    events.py, naming.py          <set>.events.toml und Namenskonvention der Clips
    postprocess.py                .glb-Nachbearbeitung: verbotene Animationskanäle entfernen, Puffer verdichten
    blender_run.py                Blender headless aufrufen (export = Blender-Export + Nachbearbeitung)
    blender/                      Skripte, die IN Blender laufen:
      settings.py                 glTF-Export-Einstellungen (verbindlich, siehe characters-pipeline.md)
      build_reference_rig.py      erzeugt human_reference.blend (Rig + Gliederpuppe mit Test-Morph-Targets)
      export_glb.py               .blend → .glb mit settings.py
      extract_rig.py              Ruhe-Geometrie eines Quell-Rigs als [[bone]]-Tabellen (Herkunft der Referenz)
      common.py                   gemeinsame Helfer (Import, Referenz-Armatur, Speichern)
      build_placeholder.py        Platzhalterfigur: Quaternius-Mannequin aufs Referenz-Rig
      build_set.py                Animations-Set aus data/clips/<set>.toml (Bibliothek, rückwärts, Überblendung,
                                  Verkettung, Keyframe-Rezept; Fußkontakt-/Lande-Events als Pose-Marker)
      curves.py                   Clip-Daten im Speicher, Posen, Überblenden/Verketten
      assemble_figure.py          Figuren-Baukasten: Teile → Referenz-Armatur, Palette, LODs (Decimate, Ränder fest)
      build_test_parts.py         Testteile (Box-Körper mit offenem Hals, passender Kopf, Lumpen, Haare)
      mpfb_human.py               baut einen Menschen mit MPFB2 aus einem Rezept (einziges Skript mit MPFB-Aufrufen)
      conform_human.py            MPFB-Rig → Referenz-Rig, Gesichts-Morphs mischen/übertragen, Kopf abtrennen,
                                  reduzieren (Morphs baryzentrisch), Materialien/Texturen, Teile
      keyframes.py                Keyframe-Platzhalter-Rezepte (strafe, turn, ladder, slide, keyposes, advance, ...)
      prepare_monster.py          CC0-Tier → Monster-Rig: Bewegung aufzeichnen, umbenennen, drehen, skalieren, neu keyen
  tests/                          pytest (synthetische Fehlerfälle auf Basis der Referenz-.glb)
```

## Einrichtung
```cmd
cd tools\chargen
uv venv --python 3.12 .venv
uv pip install --python .venv -e .[dev]
.venv\Scripts\activate
```
Blender 4.5 LTS wird für `build-rig`, `export` und das Prüfen von `.blend` gebraucht. Gesucht wird so:
`--blender <pfad>` → Umgebungsvariable `G7_BLENDER` → `PATH` → `C:\Program Files\Blender Foundation\Blender */`.

## Befehle
```cmd
gothar-chargen validate                       & REM alle .glb unter assets/source/characters (wie CI)
gothar-chargen validate figur.glb anims\      & REM einzelne Dateien/Ordner; .blend wird vorher exportiert
gothar-chargen validate --json --strict x.glb & REM maschinenlesbar, Warnungen = Fehler
gothar-chargen build-rig                      & REM human_reference.blend/.glb neu erzeugen + prüfen
gothar-chargen export figur.blend --out figur.glb  & REM danach Kanäle bereinigt (nur root/pelvis verschoben)
gothar-chargen build-placeholder --sources C:\GotharData\characters\quaternius
                                              & REM Platzhalterfigur neu erzeugen
gothar-chargen build-set all --sources C:\GotharData\characters\quaternius
                                              & REM Animations-Sets aus data/clips/<set>.toml (none, swim, dive, fist, 1h, 2h, bow, cbow, mag)
gothar-chargen report                         & REM Prio-A-Fortschritt: animation-list.md ↔ anims/
gothar-chargen speeds                         & REM Eigengeschwindigkeit der Fortbewegungs-Clips → events.toml (§3)
gothar-chargen assemble [figures\x.figure.toml] & REM Figuren aus Manifesten + Teilen (reines Python, ohne Blender;
                                              & REM nicht versioniert; ohne Argument alle, entfernt veraltete)
gothar-chargen part-data [parts\x]            & REM Zusammenbau-Daten der Teile (Halsring, Masken; §6.2)
gothar-chargen build-test-parts               & REM eigene einfache Testteile unter parts/test/
gothar-chargen human [humans\x.human.toml]    & REM MPFB2-Mensch → parts/<name>/ + textures/ (nur lokal, braucht MPFB);
                                              & REM Grundkörper (parts = ["body"]) und Köpfe (["head", "hair"]) werden
                                              & REM in figures/*.figure.toml kombiniert (assemble: Halsnaht, Haut des Kopfes);
                                              & REM Kleidungs-Kits (fit_to, parts = ["cloth"]): ein Teil je Stück und Statur;
                                              & REM Rüstungs-Kits zusätzlich neutral = false, [names], [budget], [derive.x],
                                              & REM [retouch]; Kopf-Kits [hides], derive from = "basemesh" + dome/nasal, heads
gothar-chargen monster wolf --sources C:\GotharData\characters\monsters
                                              & REM Monster-Rig + Referenz + Clip-Quelle wolf_clips.blend (§7.2);
                                              & REM meldet je Aktion Beckenabsenkung und Fuß-/Gelenkabweichung
gothar-chargen build-set wolf --sources C:\GotharData\characters\monsters
                                              & REM Monster-Clips → monsters/wolf/anims/wolf.glb
gothar-chargen collision [wolf]               & REM [rig.collision] aus der Referenz ableiten (ohne Blender)
```
Exit-Code 0 = alles in Ordnung, 1 = Fehler.

## Prüfungen des Validators
| Code | Prüft |
|---|---|
| `skeleton.*` | Knoten `root` vorhanden, Knochennamen und Eltern wie im Vertrag, keine fremden Knochen, ≤ 128 Knochen (Monster ≤ 64), Maßstab 1, `root` im Ursprung. Dateien unter `monsters/<art>/` prüft der Validator gegen das Rig der Art |
| `orientation.*` | Y oben, Figur blickt nach +Z, linke Seite (`*_l`) bei +X (Monster: Knochenpaare aus `[rig.orientation]`) |
| `pose.*` | Bind-Pose = Referenz-T-Pose: lokale Rotation je Knochen ≤ 5° (Warnung ab 1°), Knochenlänge ±15 % (Warnung ab 5 %) |
| `skin.*` | Knoten stehen in der Bind-Pose (inverse Bind-Matrizen), ≤ 4 Gewichte je Vertex, Summe 1, Sockets ohne Gewichte |
| `mesh.*` | Größe 1,50–2,10 m (Warnung außerhalb 1,65–1,95 m; Monster: ±30 %/±10 % der Rig-Höhe), Füße auf dem Boden |
| `morph.*` | nur Namen aus §6.1; ein Mesh mit Morphs trägt die vollständige Liste in Vertragsreihenfolge, jede Primitive alle Targets, höchstens 16 je Mesh |
| `anim.*` | Clip-Namen nach Konvention (§3), Kanäle nur auf Skelett-Knochen, Translation nur `root`/`pelvis`, keine Skalierung; `anim.jump`: kein Sprung zwischen zwei Frames (Fehler ab 120°/0,5 m, Warnung ab 90°); `anim.loop`: Schleifen `s_*` geschlossen (Warnung ab 5°); Monster: Clip-Modus = Art, `anim.root_motion`: `s_walk`/`s_run` ≥ 0,1 m/s vorwärts, `t_turn_l/r` ≥ 45° um +Y in die richtige Richtung; `collision.stale`: `[rig.collision]` passt nicht mehr zum Mesh (Warnung) |
| `lod.*` | LOD-Vertrag §2.2: Stufen lückenlos ab 0, gleicher Eltern-Knoten/Transformation/Skin, Morphs nur auf `_lod0`, Anteil lod1 ≤ 60 %, lod2 ≤ 30 % (Warnung) |
| `mesh.budget` | höchstens 20 k Dreiecke je Figur bei lod0 (nicht für Teile unter `parts/`) |
| `fit.*` | Figuren mit Rollen-Knoten (`body`, `head`, `hair`, `beard`): jeder offene Rand von body/head trifft einen Rand eines anderen Teils (≤ 5 mm) mit gleichen Gewichten; `lod.seam`: Nahtränder von body/head in allen Stufen wie bei lod0 (Haare/Bärte dürfen sich verändern) |
| `tex.*` | Textur-Vertrag §2.3: Höchstgröße je Rolle, Zweierpotenz, Normal-Map ≤ Basisfarbe, Masken-Rollen alphaMode MASK + PNG mit Alpha, Datei vorhanden; eingebettet = Warnung |
| `events.*` | `<set>.events.toml` neben der `.glb`: Format, Clips vorhanden, Frames im Clip (Schleifen `s_*`: vor dem letzten Frame) |

## Keyframe-Platzhalter
Clips ohne passende CC0-Quelle entstehen aus Rezepten in `blender/keyframes.py` (`keyframe = "<rezept>"` plus
`params` in der Clip-Liste). Posen werden als Drehungen um Weltachsen der T-Pose geschrieben (X = links der Figur,
−Y = vorn, Z = oben), z. B. `"upperarm_l": [("Z", -90), ("X", -80)]` = Arm nach vorn, dann hoch. Sie sind bewusst
grob und stehen in `animation-list.md` als `platzhalter-K`; F4 ersetzt sie durch Mocap. Hilfs-Clips
(`helper = true`) werden nur zum Bauen benutzt und nicht exportiert.

Für Monster (§7.2): `keyposes` setzt benannte Posen (`[clip.params.poses.<name>]` mit `rotate`/`move`) an
Schlüssel-Frames (`keys = [[0, "rest"], [8, "crouch"], ...]`) und überblendet weich, optional über einer
Basis-Schleife (`base`); `advance` macht aus einer Schleife am Ort Root Motion (`speed` in m/s, `time` < 1 =
schneller, `amplify` = weiter ausholen). Feste Events: `markers = { hit_start = 13, hit_end = 17 }`.

## Figuren-Baukasten (F3)
Eine Figur besteht aus Teilen (`.glb` auf dem Referenz-Rig, unter `assets/source/characters/parts/`): `body`
(Grundkörper oder Kleidung/Rüstung, die ihn ersetzt), `head`, optional `hair`/`beard`. Das Manifest
`figures/<name>.figure.toml` nennt die Teile und eine Farbpalette (Materialname → `#rrggbb`). Die Teile bringen
ihre LOD-Stufen mit (beim Teile-Bauen in Blender reduziert, Nahtränder fest) und Zusammenbau-Daten (Halsring,
Masken je Kleidungsstück, `part-data`). `gothar-chargen assemble` baut daraus in reinem Python `figures/<name>.glb`
mit Knoten `<rolle>_lod<n>` (Vertrag §2.2/§6.2) und prüft sie; die Figuren sind nicht versioniert.

## Schichtung (Waffenmodi)
`layer = ["none/s_walk", "1h/s_idle"]` nimmt Beine, Becken und Wirbelsäule aus dem ersten Clip und die Teilbäume
von `from_bone` (Vorgabe `spine_02`; Waffenmodi: `["clavicle_l", "clavicle_r", "neck"]`) aus dem zweiten. Bei
Schleifen wird die Haltung auf ganze Zyklen der Basis gestreckt, damit die Schleife nahtlos bleibt.
`depends = ["none"]` macht die Clips eines anderen Sets verfügbar (sie werden im Speicher mitgebaut).

## Quaternius-Quellen (für `build-placeholder`/`build-set`)
Die „Standard“-Pakete der Universal Animation Library 1 und 2 (CC0) liegen als ZIP direkt auf opengameart.org
(itch.io blockt automatische Downloads). Entpackt nach `DATA_ROOT\characters\quaternius\` (nicht ins Repo):
- https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip (UAL1, Rigify-Namen)
- https://opengameart.org/sites/default/files/universal_animation_library_2standard.zip (UAL2, Mannequin)

## Monster-Quellen (für `monster`)
Quaternius-Tierpakete (CC0, opengameart.org), entpackt nach `DATA_ROOT\characters\monsters\` (nicht ins Repo):
- https://opengameart.org/content/animated-animales-low-poly („Animal Pack Vol.2“: Wolf)
- https://opengameart.org/content/lowpoly-animated-farm-animal-pack (Schwein → `keiler`)
- https://opengameart.org/content/5-low-poly-animals (Küken → `laufvogel`)

## Tests
```cmd
ruff check . && ruff format --check . && python -m pytest
```
