# Rohdaten für Leonberg beschaffen

Anleitung zu Welt-Spur W1: welche Geodaten gebraucht werden, wie sie heruntergeladen und wo sie abgelegt werden.
Hintergrund und Lizenzen: `docs/design/leonberg-pipeline.md` (Abschnitte 1–2) und ADR 0012.

> Rohdaten kommen **nie ins Repo**. Sie liegen unter `DATA_ROOT` (außerhalb des Repos).

## 1. Kurzfassung

```cmd
cd tools\worldgen
copy config\local.example.toml config\local.toml     REM data_root eintragen, z. B. C:/GotharData
.venv\Scripts\activate
gothar-worldgen download leonberg                     REM LGL-Kacheln + OSM laden und entpacken
gothar-worldgen info leonberg                         REM prüfen: alle Ordner "ok"
```

`download` lädt nur, was noch fehlt (`--force` lädt neu). Mit `--only dgm1,lod2,dop,osm` wählt man
eine Teilmenge, mit `--area core` nur die Altstadt. Jede neu geladene Datei wird mit Datum und URL in
`<DATA_ROOT>/geo/SOURCES.md` protokolliert.

Ergebnis:
```
<DATA_ROOT>/
  geo/lgl/dgm1/dgm1_32_<E>_<N>_2_bw/     je 4 × XYZ-ASCII (1 km², 1-m-Raster, Höhen NHN/DHHN2016)
  geo/lgl/lod2/LoD2_32_<E>_<N>_2_bw/     je 4 × CityGML (.gml, 1 km²)
  geo/lgl/dop/dop20rgb_32_<E>_<N>_2_bw/  Luftbild 20 cm, RGB
  geo/osm/stuttgart-regbez-latest.osm.pbf
  geo/SOURCES.md                          Herkunft und Download-Datum
```

## 2. Welche Kacheln?

Das Gebiet steht in `tools/worldgen/config/leonberg.toml`.

```cmd
gothar-worldgen tiles leonberg                 REM Umland, LGL-Downloadraster
gothar-worldgen tiles leonberg --area core     REM nur Altstadt
gothar-worldgen tiles leonberg --size 1000     REM einfaches 1-km-Raster, z. B. für die Dateien in den ZIPs
```

Das LGL liefert DGM1, LoD2 und DOP20 in **2 km × 2 km-Kacheln**. Das Raster beginnt bei
**ungeraden Ost-km** und geraden Nord-km. Das Kennzeichen `501_5404` steht für die linke untere Ecke in km
(UTM 32N) und deckt E 501–503 km und N 5404–5406 km ab.
Das Umland von Leonberg (±1000 m um den Marktbrunnen: E 500 115–502 115, N 5 404 347–5 406 347)
braucht **4 Kacheln: 499_5404, 501_5404, 499_5406, 501_5406**. Die Altstadt (±350 m) braucht 499_5404 und 501_5404.

## 3. LGL Open GeoData (DGM1, LoD2, DOP20)

Portal: **https://opengeodata.lgl-bw.de**. Lizenz: Datenlizenz Deutschland – Namensnennung 2.0
(„Datengrundlage: LGL, www.lgl-bw.de“).

| Produkt | Download-URL (Schema) | Größe je Kachel |
|---|---|---|
| DGM1 | `https://opengeodata.lgl-bw.de/data/dgm/dgm1_32_<E>_<N>_2_bw.zip` | ca. 14 MB (entpackt 116 MB) |
| LoD2 (CityGML) | `https://opengeodata.lgl-bw.de/data/lod2/LoD2_32_<E>_<N>_2_bw.zip` | ca. 11 MB |
| DOP20 RGB | `https://opengeodata.lgl-bw.de/data/dop20/dop20rgb_32_<E>_<N>_2_bw.zip` | ca. 250 MB |

Das Portal hängt an Downloads den freiwilligen Statistik-Parameter `customerGroup` an; das Werkzeug
verwendet den Portal-Standard `keine-angabe`. Stand des Schemas: Oktober 2026 (aus der Portal-Konfiguration
`assets/config/local/odp-products.json`). Ändert das LGL die Pfade, bricht `download` mit HTTP-Fehler ab.
Dann `LGL_PRODUCTS` in `tools/worldgen/src/gothar_worldgen/download.py` anpassen. Manuell
geht es weiterhin über die Kartenauswahl im Portal; die ZIPs werden dann in die Ordner aus Abschnitt 1 entpackt.

## 4. OpenStreetMap

Geofabrik-Extrakt des Regierungsbezirks Stuttgart:
`https://download.geofabrik.de/europe/germany/baden-wuerttemberg/stuttgart-regbez-latest.osm.pbf`.
Den Ausschnitt Leonberg schneidet `geo-import` selbst zu. Lizenz: ODbL, Pflichtangabe
„© OpenStreetMap-Mitwirkende“. Für einen neueren Stand: `gothar-worldgen download leonberg --only osm --force`.

## 5. Testdaten fürs Repo

Für die CI-Tests kommen später **kleine Ausschnitte** (wenige 100 m, wenige KB) unter
`tools/worldgen/tests/data/`, jeweils mit Quellenangabe. Das erledigen die Import-Schritte.
Ganze Kacheln werden nicht eingecheckt.
