# 0012 – Geodaten: LGL Open GeoData und OpenStreetMap statt Google

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** W1

## Kontext
Die Leonberger Altstadt soll Vorlage für einen Spielort werden (`docs/design/leonberg-pipeline.md`).
Benötigt werden Gelände, Gebäude-Baukörper und Straßen.

## Optionen
1. **Google Maps / Earth** – bekannt, detailreich; Nutzungsbedingungen erlauben keine Datenextraktion oder abgeleitete Datensätze, keine Rohdaten.
2. **LGL Baden-Württemberg Open GeoData** (DGM1, LoD2, DOP) – amtlich, genau, frei auch kommerziell nutzbar (Datenlizenz Deutschland – Namensnennung 2.0); LoD2-Dächer teils ungenau.
3. **OpenStreetMap** – Straßen, Nutzung, Details; ODbL (Namensnennung, Share-Alike nur für die Datenbank selbst).
4. **Eigene Vermessung/Photogrammetrie** – volle Kontrolle; aufwendig, für Gelände ungenau.

## Entscheidung
LGL Open GeoData für Gelände und Baukörper, OSM für Straßen und Nutzung, eigene Insta360-Aufnahmen
für Fassaden-Referenz. Google nur zum Anschauen.

## Konsequenzen
- Credits: „Datengrundlage: LGL, www.lgl-bw.de“ und „© OpenStreetMap-Mitwirkende“ in Spiel und `assets/LICENSES.md`.
- Werkzeuge müssen CityGML, GeoTIFF und OSM-PBF lesen (Python-Ökosystem: GDAL, pyproj, osmium).
