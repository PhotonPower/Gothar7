# worldgen – Werkzeuge für die Welt-Spur (Leonberg → Spielort)

Python-Werkzeuge, die aus Geodaten und 360°-Aufnahmen Spielwelt-Daten erzeugen.
Spezifikation: `docs/design/leonberg-pipeline.md` · Rohdaten beschaffen: `docs/design/leonberg-rohdaten.md` ·
Roadmap: `docs/03-roadmap.md` → Welt-Spur W1–W7.

## Struktur
```
tools/worldgen/
  pyproject.toml              Paket "gothar-worldgen", CLI "gothar-worldgen"
  config/leonberg.toml        Gebiet, Ursprung, Maßstab (versioniert)
  config/local.toml           DATA_ROOT usw. (lokal, NICHT versioniert; Vorlage: local.example.toml)
  src/gothar_worldgen/
    cli.py                    Befehle info / tiles / download / import / check
    download.py               LGL-Kacheln + OSM-Extrakt nach DATA_ROOT laden
    importer.py               Ablauf von "import" (Schritte nacheinander)
    config.py                 Laden + Prüfen von <ort>.toml und local.toml, Datenpfade
    geo/                      bbox (Gebiete, Kachelraster), dgm1 (XYZ → Höhenraster),
                              terrain (Heightmap-Export), lod2 (CityGML lesen),
                              buildings (→ buildings.json), osm (PBF lesen),
                              streets / features (→ streets.json, features.json),
                              frame (lokales Koordinatensystem), jsonio            (W1)
    qa/                       Vorschaubilder + Plausibilitätsprüfungen (check)  (W1)
    facade/                   360°-Bilder → Fassadenansichten (equirect, rectify), GPS-Posen (poses),
                              Override-JSON (overrides), Einzelbilder + Zeitabgleich (frames, sync,
                              capture), facade preview/frames, Web-UI (webui/)  (W4)
    export/                   terrain: Heightmap → .g7world mit terrain-Block, splat: Splat-Schichten
                              + Platzhalter-Albedos (export-terrain)  (W2)
    buildings/                massing (Klötzchen: Wände + Dächer), medieval (Fachwerk-Regelwerk, W5-Entwurf),
                              rueckbau (große Neubauten → schmale Fachwerkhäuser, §7),
                              gltf (.glb-Writer), batch (buildings: .glb je Gebäude/Zelle + Index)  (W3/W5)
    assemble/                 world: Terrain + Gebäude → <ort>.g7world, stabile VobIds (assemble)  (W3)
  blender/gothar_buildings/   Blender-Add-on: erzeugte Gebäude importieren/zurückschreiben (locked)  (W3)
  data/leonberg/vob_ids.json  stabile VobIds des Assemblers (versioniert, nie wiederverwenden)
  data/building_rules.json    Parameter des Fachwerk-Regelwerks (Entwurf, Festlegung durch den Projektinhaber)
  data/leonberg/buildings/    Annotationen/Overrides pro Gebäude (JSON, versioniert)
  tests/                      pytest; tests/data/ enthält kleine LGL-Ausschnitte (mit Quellenangabe)
```

## Einrichtung
Empfohlen mit [uv](https://docs.astral.sh/uv/), das bei Bedarf auch Python selbst installiert:
```cmd
cd tools\worldgen
uv venv --python 3.12 .venv
uv pip install --python .venv -e .[dev]
.venv\Scripts\activate
copy config\local.example.toml config\local.toml   &  REM DATA_ROOT eintragen
```
Alternativ mit vorhandenem Python ≥ 3.11: `py -3.12 -m venv .venv` und dann `pip install -e .[dev]`.
GDAL unter Windows: über die Wheels von `rasterio`/`pyogrio` (bringen GDAL mit), kein separates GDAL nötig.

## Befehle
```cmd
gothar-worldgen info leonberg              REM Konfiguration, Gebiet, vorhandene/fehlende Rohdaten-Ordner
gothar-worldgen tiles leonberg             REM benötigte LGL-Kacheln (2-km-Raster), --area core, --size 1000
gothar-worldgen download leonberg          REM LGL-Kacheln + OSM laden/entpacken, --only dgm1,osm, --force
gothar-worldgen import leonberg            REM Rohdaten → work/leonberg/ (terrain, buildings, streets, features) + check
gothar-worldgen check leonberg             REM preview.png, preview_core.png, report.json (Exit 1 bei "fail")
gothar-worldgen export-terrain leonberg    REM assets/source/worlds/leonberg/leonberg_terrain.g7world (+ generated/*.r16)
gothar-worldgen facade preview leonberg <id> --image pano.jpg --pose x,z --heading 90   REM entzerrte Fassaden
gothar-worldgen facade frames leonberg VID_0001.mp4 --gpx VID_0001.gpx   REM Einzelbilder + Posen (braucht ffmpeg)
gothar-worldgen facade ui leonberg         REM Annotations-Oberfläche im Browser (http://127.0.0.1:8765/)
gothar-worldgen buildings leonberg         REM Fachwerk-Stil + Rückbau (Vorgabe medieval), generated/buildings/, --area all
gothar-worldgen buildings leonberg --mode massing   REM echtes LoD2 als graue Klötzchen (Maßstabsreferenz)
gothar-worldgen assemble leonberg          REM assets/source/worlds/leonberg/leonberg.g7world + data/leonberg/vob_ids.json
```
Reihenfolge für die Welt: `import` → `export-terrain` → `buildings` → `assemble`; prüfen mit
`build\release\game\gothar.exe --world=worlds/leonberg/leonberg.g7world --start=START_UEBERSICHT --time=12:00`.

### Grenzen des glTF-Writers (`buildings/gltf.py`)
- Pro Datei genau ein Mesh und ein Knoten; ein oder mehrere Dreiecks-Primitives mit je einem untexturierten
  PBR-Material (gleiche Namen teilen sich ein Material; `medieval`: fünf Rollen).
- Indizes je Primitive.
- Attribute: `POSITION`, `NORMAL`, `TEXCOORD_0` (float32, UV in Metern); Indizes als uint16, ab 65 536 Vertices
  uint32.
- Keine Texturen, Skins, Animationen, Morph-Targets oder Extensions; Positionen auf 0,1 mm gerundet.
- Ausgabe byte-deterministisch. Die Dateien kocht `g7-cook` ohne Warnung (Test, wenn `g7-cook` gebaut ist
  oder `G7_COOK` gesetzt ist).

### Blender-Add-on
In Blender 4.5: *Edit → Preferences → Add-ons → Install from Disk*, Ordner `blender/gothar_buildings` als ZIP
(oder Symlink in den Add-on-Ordner). Danach im 3D-Viewport unter *Sidebar → Gothar*:
- Index (`assets/source/worlds/leonberg/generated/buildings_index.json`) wählen.
- IDs eintragen (oder leer lassen für den Umkreis des 3D-Cursors) → „Gebäude importieren“.
- Bearbeiten → „Auswahl zurückschreiben“. Das setzt `locked: true` im Override, damit `buildings` und `assemble`
  die Handarbeit behalten.
`--config-dir` bzw. `GOTHAR_WORLDGEN_CONFIG` wählen einen anderen Konfigurationsordner;
`GOTHAR_DATA_ROOT` überschreibt `data_root` aus `local.toml`.

## Entwicklung
```cmd
ruff format . && ruff check .
python -m pytest --cov=gothar_worldgen
```
Die CI (Job `worldgen` in `.github/workflows/ci.yml`) führt dieselben Schritte unter Linux (Python 3.11)
und Windows (Python 3.12) aus.
