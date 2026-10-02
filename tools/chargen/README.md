# chargen – Werkzeuge für die Figuren-Spur

Python-Werkzeuge für Referenz-Rig, Rig-Validator und Blender-Export der Figuren und Animationen.
Spezifikation: `docs/design/characters-pipeline.md` · Skelett-Vertrag: `docs/modules/animation.md`
(„Referenz-Skelett“) · Roadmap: `docs/03-roadmap.md` → Figuren-Spur F1–F5.

## Struktur
```
tools/chargen/
  pyproject.toml                  Paket "gothar-chargen", CLI "gothar-chargen"
  src/gothar_chargen/
    data/human_reference.toml     Referenz-Skelett: Namen, Eltern, Sockets, Bind-Pose (T-Pose), Morph-Target-Namen
    skeleton.py                   Laden der Skelett-Definition (spiegelt *_l → *_r)
    gltf.py                       kleiner .glb-Leser/-Schreiber (Accessoren, Knoten-Transformationen)
    validate.py                   Rig-Validator (Prüfungen siehe unten)
    events.py, naming.py          <set>.events.toml und Namenskonvention der Clips
    blender_run.py                Blender headless aufrufen
    blender/                      Skripte, die IN Blender laufen:
      settings.py                 glTF-Export-Einstellungen (verbindlich, siehe characters-pipeline.md)
      build_reference_rig.py      erzeugt human_reference.blend (Rig + Gliederpuppe mit Test-Morph-Targets)
      export_glb.py               .blend → .glb mit settings.py
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
gothar-chargen export figur.blend --out figur.glb
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
| `anim.*` | Clip-Namen nach Konvention (§3), Kanäle nur auf Skelett-Knochen |
| `events.*` | `<set>.events.toml` neben der `.glb`: Format, Clips vorhanden, Frames innerhalb des Clips |

## Tests
```cmd
ruff check . && ruff format --check . && python -m pytest
```
