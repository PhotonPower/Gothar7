# worldgen – Werkzeuge für die Welt-Spur (Leonberg → Spielort)

Python-Werkzeuge, die aus Geodaten und 360°-Aufnahmen Spielwelt-Daten erzeugen.
Spezifikation: `docs/design/leonberg-pipeline.md` · Roadmap: `docs/03-roadmap.md` → Welt-Spur W1–W7.

## Geplante Struktur
```
tools/worldgen/
  pyproject.toml              Paket "gothar-worldgen", CLI "gothar-worldgen"   (W1)
  config/leonberg.toml        Gebiet, Ursprung, Maßstab (versioniert)
  config/local.toml           DATA_ROOT usw. (lokal, NICHT versioniert; Vorlage: local.example.toml)
  src/gothar_worldgen/
    geo_import/               DGM1, CityGML LoD2, OSM → Zwischenformate        (W1)
    facade/                   Insta360 → Fassadenansichten, Annotations-UI     (W4)
    assemble/                 Zwischendaten + .glb → .g7world, Wegnetz-Vorschlag (W3, W6)
  blender/gothar_buildings/   Blender-Add-on: Gebäude-Generator               (W3, W5)
  data/leonberg/buildings/    Annotationen/Overrides pro Gebäude (JSON, versioniert)
  tests/                      pytest, mit LGL-Testdaten-Ausschnitten
```

## Einrichtung (ab W1)
```cmd
cd tools\worldgen
py -3.12 -m venv .venv
.venv\Scripts\activate
pip install -e .[dev]
copy config\local.example.toml config\local.toml   &  REM DATA_ROOT eintragen
gothar-worldgen import leonberg
```
GDAL unter Windows: über die Wheels von `rasterio`/`pyogrio` (bringen GDAL mit) – kein separates GDAL nötig.
