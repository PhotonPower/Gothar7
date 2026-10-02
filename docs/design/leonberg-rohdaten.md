# Rohdaten für Leonberg beschaffen

Anleitung zu Welt-Spur W1. Sie beschreibt, welche Geodaten wo heruntergeladen und wo sie abgelegt werden.
Hintergrund und Lizenzen: `docs/design/leonberg-pipeline.md` (Abschnitte 1–2) und ADR 0012.

> Rohdaten kommen **nie ins Repo**. Sie liegen unter `DATA_ROOT` (außerhalb des Repos).

## 1. Vorbereitung

```cmd
cd tools\worldgen
copy config\local.example.toml config\local.toml     REM data_root eintragen, z. B. D:/GotharData
.venv\Scripts\activate
gothar-worldgen info leonberg                         REM zeigt Gebiet und fehlende Ordner
```

Erwartete Ordnerstruktur:
```
<DATA_ROOT>/
  geo/lgl/dgm1/      Geländemodell (entpackt)
  geo/lgl/lod2/      3D-Gebäude CityGML (entpackt)
  geo/lgl/dop/       Luftbilder (entpackt)
  geo/osm/           OpenStreetMap-Extrakt (.osm.pbf)
  geo/SOURCES.md     Wer, was, wann heruntergeladen (siehe Abschnitt 5)
```

## 2. Welche Kacheln?

Das Gebiet steht in `tools/worldgen/config/leonberg.toml`. Die nötigen Kacheln gibt das Werkzeug aus:

```cmd
gothar-worldgen tiles leonberg                    REM Umland, 1-km-Raster
gothar-worldgen tiles leonberg --area core        REM nur Altstadt
gothar-worldgen tiles leonberg --size 2000        REM falls ein Produkt 2-km-Kacheln hat
```

Das Kennzeichen `500_5405` ist die linke untere Ecke der Kachel in km (Ost_Nord, UTM 32N). Die
LGL-Dateinamen enthalten dieselben Zahlen, z. B. `…_32_500_5405_…`.
Stand der ersten Konfiguration: Umland E 499 933–501 933, N 5 404 056–5 406 056, also im 1-km-Raster
**E 499/500/501 × N 5404/5405/5406 = 9 Kacheln**. In WGS84 entspricht das etwa
48,790–48,808 °N und 8,999–9,026 °E.

## 3. LGL Open GeoData (DGM1, LoD2, DOP)

Portal: **https://opengeodata.lgl-bw.de** (Kartenansicht mit Kachelauswahl). Lizenz: Datenlizenz
Deutschland – Namensnennung 2.0.

| Produkt im Portal | Ablage | Umfang | Hinweis |
|---|---|---|---|
| Digitales Geländemodell **DGM1** (1 m) | `geo/lgl/dgm1/` | alle Kacheln des Umlands | GeoTIFF oder XYZ-ASCII. Der Import wird an das gelieferte Format angepasst. |
| **3D-Gebäudemodelle LoD2** (CityGML) | `geo/lgl/lod2/` | mindestens die Kacheln der Altstadt (`--area core`) | `.gml`; Umland ist optional |
| Digitale Orthophotos **DOP20** (RGB, 20 cm) | `geo/lgl/dop/` | Altstadt, Umland nach Wunsch | Nur als Referenz und für die Vorschau. Die Dateien sind groß. |

Vorgehen:
1. Im Portal das Produkt wählen und auf der Karte bis Leonberg zoomen.
2. Alle Kacheln markieren, die das Rechteck aus Abschnitt 2 berühren. Im Zweifel lieber eine
   Kachel mehr nehmen: das Werkzeug schneidet selbst zu.
3. Die ZIP-Dateien herunterladen und in den passenden Ordner **entpacken**. Die Original-Dateinamen
   bitte beibehalten, denn das Werkzeug liest die Kachelkoordinaten daraus.

Kachelgröße, Dateinamen und Formate legt das LGL fest und kann sie ändern. Weicht etwas von dieser
Anleitung ab, bitte die Dateiliste (`dir /s /b <DATA_ROOT>\geo`) melden. Dann wird der Import angepasst.

## 4. OpenStreetMap

Den Extrakt des Regierungsbezirks Stuttgart von Geofabrik laden:
**https://download.geofabrik.de/europe/germany/baden-wuerttemberg/stuttgart-regbez-latest.osm.pbf**
und als `geo/osm/stuttgart-regbez-latest.osm.pbf` ablegen. Den Ausschnitt Leonberg schneidet
`geo-import` selbst zu, ein vorheriges Zuschneiden ist nicht nötig. Lizenz: ODbL, Pflichtangabe
„© OpenStreetMap-Mitwirkende“.

## 5. Herkunft festhalten

Für Nachvollziehbarkeit und Credits bitte `<DATA_ROOT>/geo/SOURCES.md` anlegen:

```markdown
| Datum      | Produkt | Quelle                       | Kacheln / Datei                  |
|------------|---------|------------------------------|----------------------------------|
| 2026-10-xx | DGM1    | opengeodata.lgl-bw.de        | 499–501 × 5404–5406              |
| 2026-10-xx | OSM     | download.geofabrik.de        | stuttgart-regbez-latest.osm.pbf  |
```

Die Credits für Spiel und Repo stehen in `assets/LICENSES.md`.

## 6. Testdaten fürs Repo

Für die CI-Tests kommen später **kleine Ausschnitte** (wenige 100 m, wenige KB) unter
`tools/worldgen/tests/data/`, jeweils mit Quellenangabe. Das erledigen die Import-Schritte.
Ganze Kacheln werden nicht eingecheckt.
