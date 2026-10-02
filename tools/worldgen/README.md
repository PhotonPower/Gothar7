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
    cli.py                    Befehle info / tiles / import
    config.py                 Laden + Prüfen von <ort>.toml und local.toml, Datenpfade
    geo/                      Gebiete, Kachelraster; DGM1, CityGML LoD2, OSM → Zwischenformate (W1)
    facade/                   Insta360 → Fassadenansichten, Annotations-UI     (geplant, W4)
    assemble/                 Zwischendaten + .glb → .g7world, Wegnetz-Vorschlag (geplant, W3, W6)
  blender/gothar_buildings/   Blender-Add-on: Gebäude-Generator               (geplant, W3, W5)
  data/leonberg/buildings/    Annotationen/Overrides pro Gebäude (JSON, versioniert)
  tests/                      pytest (später mit kleinen LGL-Testdaten-Ausschnitten)
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
gothar-worldgen tiles leonberg             REM benötigte Kacheln (Download-Hilfe), --area core, --size 2000
gothar-worldgen import leonberg            REM Rohdaten → Zwischenformate (im Aufbau)
```
`--config-dir` bzw. `GOTHAR_WORLDGEN_CONFIG` wählen einen anderen Konfigurationsordner;
`GOTHAR_DATA_ROOT` überschreibt `data_root` aus `local.toml`.

## Entwicklung
```cmd
ruff format . && ruff check .
python -m pytest --cov=gothar_worldgen
```
Die CI (Job `worldgen` in `.github/workflows/ci.yml`) führt dieselben Schritte unter Linux (Python 3.11)
und Windows (Python 3.12) aus.
