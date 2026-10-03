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
    skeleton.py, mapping.py       Laden der Skelett-Definition (spiegelt *_l → *_r) bzw. der Zuordnungen
    clipspec.py, report.py        Clip-Listen lesen/prüfen; Abgleich animation-list.md ↔ Clips
    figure.py                     Figur-Manifeste figures/<name>.figure.toml (Teile, LOD-Anteile, Palette)
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
      keyframes.py                Keyframe-Platzhalter-Rezepte (strafe, turn, ladder, slide, ...; „platzhalter-K“)
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
gothar-chargen assemble [figures\x.figure.toml] & REM Figuren aus Manifesten bauen (alle ohne Argument)
gothar-chargen build-test-parts               & REM eigene einfache Testteile unter parts/test/
```
Exit-Code 0 = alles in Ordnung, 1 = Fehler.

## Prüfungen des Validators
| Code | Prüft |
|---|---|
| `skeleton.*` | Knoten `root` vorhanden, Knochennamen und Eltern wie im Vertrag, keine fremden Knochen, ≤ 128 Knochen, Maßstab 1, `root` im Ursprung |
| `orientation.*` | Y oben, Figur blickt nach +Z, linke Seite (`*_l`) bei +X |
| `pose.*` | Bind-Pose = Referenz-T-Pose: lokale Rotation je Knochen ≤ 5° (Warnung ab 1°), Knochenlänge ±15 % (Warnung ab 5 %) |
| `skin.*` | Knoten stehen in der Bind-Pose (inverse Bind-Matrizen), ≤ 4 Gewichte je Vertex, Summe 1, Sockets ohne Gewichte |
| `mesh.*` | Größe 1,50–2,10 m (Warnung außerhalb 1,65–1,95 m), Füße auf dem Boden |
| `morph.name` | nur Morph-Target-Namen aus characters-pipeline.md §6 |
| `anim.*` | Clip-Namen nach Konvention (§3), Kanäle nur auf Skelett-Knochen, Translation nur `root`/`pelvis`, keine Skalierung; `anim.jump`: kein Sprung zwischen zwei Frames (Fehler ab 120°/0,5 m, Warnung ab 90°); `anim.loop`: Schleifen `s_*` geschlossen (Warnung ab 5°) |
| `lod.*` | LOD-Vertrag §2.2: Stufen lückenlos ab 0, gleicher Eltern-Knoten/Transformation/Skin, Morphs nur auf `_lod0`, Anteil lod1 ≤ 60 %, lod2 ≤ 30 % (Warnung) |
| `mesh.budget` | höchstens 20 k Dreiecke je Figur bei lod0 (nicht für Teile unter `parts/`) |
| `fit.*` | Figuren mit Rollen-Knoten (`body`, `head`, `hair`, `beard`): jeder offene Rand von body/head trifft einen Rand eines anderen Teils (≤ 5 mm) mit gleichen Gewichten; `lod.seam`: Ränder in allen Stufen wie bei lod0 |
| `events.*` | `<set>.events.toml` neben der `.glb`: Format, Clips vorhanden, Frames im Clip (Schleifen `s_*`: vor dem letzten Frame) |

## Keyframe-Platzhalter
Clips ohne passende CC0-Quelle entstehen aus Rezepten in `blender/keyframes.py` (`keyframe = "<rezept>"` plus
`params` in der Clip-Liste). Posen werden als Drehungen um Weltachsen der T-Pose geschrieben (X = links der Figur,
−Y = vorn, Z = oben), z. B. `"upperarm_l": [("Z", -90), ("X", -80)]` = Arm nach vorn, dann hoch. Sie sind bewusst
grob und stehen in `animation-list.md` als `platzhalter-K`; F4 ersetzt sie durch Mocap. Hilfs-Clips
(`helper = true`) werden nur zum Bauen benutzt und nicht exportiert.

## Figuren-Baukasten (F3)
Eine Figur besteht aus Teilen (`.glb` auf dem Referenz-Rig, unter `assets/source/characters/parts/`): `body`
(Grundkörper oder Kleidung/Rüstung, die ihn ersetzt), `head`, optional `hair`/`beard`. Das Manifest
`figures/<name>.figure.toml` nennt die Teile, die LOD-Anteile und eine Farbpalette (Materialname → `#rrggbb`).
`gothar-chargen assemble` baut daraus `figures/<name>.glb` mit Knoten `<rolle>_lod<n>` (Vertrag §2.2) und prüft
sie. Teile werden beim Zusammenbau verschweißt (glTF trennt Vertices an harten Kanten); offene Ränder (Nähte)
bekommen Gewicht 0 für Decimate und bleiben in allen Stufen erhalten. Der Validator prüft Passform und LODs.

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

## Tests
```cmd
ruff check . && ruff format --check . && python -m pytest
```
