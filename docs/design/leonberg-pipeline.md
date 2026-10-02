# Welt-Pipeline: Von der Leonberger Altstadt zum Spielfeld

**Ziel:** Die Altstadt von Leonberg dient als Vorlage für einen Ort in der Spielwelt von Gothar.
Gelände, Straßenführung und Baukörper kommen aus amtlichen Geodaten, das Aussehen der Fassaden
aus eigenen 360°-Aufnahmen. Daraus erzeugen Werkzeuge eine **mittelalterlich-ländliche** Version,
die anschließend im Editor von Hand verfeinert wird.

Umsetzung als eigene Roadmap-Spur **W1–W7** (siehe `docs/03-roadmap.md`), parallel zu den
Engine-Phasen. Werkzeuge: Python unter `tools/worldgen/`; die Engine liest nur glTF, Heightmaps und JSON.

---

## 1. Datenquellen und Lizenzen (ADR 0012)

| Quelle | Inhalt | Verwendung | Lizenz / Pflicht |
|---|---|---|---|
| **LGL BW – DGM1** | Geländemodell, 1-m-Raster (GeoTIFF/ASCII) | Heightmap | Datenlizenz Deutschland – Namensnennung 2.0: „Datengrundlage: LGL, www.lgl-bw.de“ |
| **LGL BW – LoD2** | 3D-Gebäude mit Dachform (CityGML) | Baukörper für den Generator | wie oben |
| **LGL BW – DOP** (Luftbilder) | Orthofotos | Referenz beim Modellieren, Splatmap-Vorlage | wie oben |
| **OpenStreetMap** | Straßen, Plätze, Mauern, Gewässer, Nutzung | Straßen, Plätze, Wegnetz-Vorschlag | ODbL: Namensnennung „© OpenStreetMap-Mitwirkende“ |
| **Eigene Insta360-Aufnahmen** | 360°-Fotos/-Videos mit GPS | Fassaden-Referenz, Proportionen, Stimmung | eigene Rechte; Rohdaten **nicht** veröffentlichen (Personen, Kennzeichen) |
| Google Maps / Earth | – | **nur Anschauen**, keine Daten übernehmen | Nutzungsbedingungen erlauben keine Datenextraktion |

Bezug: LGL Open GeoData-Portal (Download DGM1, LoD2, DOP). Alle Credits sammelt `assets/LICENSES.md`.
Schritt-für-Schritt-Anleitung zum Herunterladen: `docs/design/leonberg-rohdaten.md`.

## 2. Ablage der Rohdaten

Rohdaten (GBs, personenbezogene Bilder) liegen **außerhalb des Repos** in einem Datenordner,
dessen Pfad in `tools/worldgen/config/local.toml` steht (nicht versioniert). Alternativ setzt die
Umgebungsvariable `GOTHAR_DATA_ROOT` den Pfad (z. B. in CI).

```
<DATA_ROOT>/                     z. B. D:\GotharData
  geo/lgl/dgm1/<kachel>/*.xyz    LGL-Downloads (2-km-Kacheln, je 4 Dateien à 1 km²)
  geo/lgl/lod2/<kachel>/*.gml
  geo/lgl/dop/<kachel>/
  geo/osm/*.osm.pbf              Geofabrik-Extrakt, Zuschnitt macht geo-import
  geo/SOURCES.md                 Herkunft und Download-Datum
  capture/insta360/<datum>/      .insv/.mp4 + GPS
  work/<ort>/                    Zwischenstände der Werkzeuge
```
`gothar-worldgen download leonberg` füllt `geo/`, `gothar-worldgen info leonberg` zeigt, welche Ordner fehlen.

Im Repo landen nur **abgeleitete, geprüfte Ergebnisse** (`assets/source/worlds/leonberg/…`) und
die Annotationen (`tools/worldgen/data/leonberg/…`, kleine JSON-Dateien).

## 3. Koordinatensystem

- Quelle: ETRS89 / UTM Zone 32N (**EPSG:25832**), Höhen in m über NHN.
- Engine: lokales System in Metern, **Ursprung am Marktplatz** (genauer Punkt in `leonberg.toml`),
  +X = Osten, +Y = oben, −Z = Norden (rechtshändig, passend zu `docs/modules/core.md`).
  Höhe: `y = NHN − Bezugshöhe` (Bezugshöhe = Marktplatzhöhe), damit Zahlen klein bleiben.
- Optional ein **Spielmaßstab** pro Achse (siehe Abschnitt 8) – die Umrechnung macht ausschließlich `geo-import`.

## 4. Datenformate (Zwischenschicht)

`geo-import` (`gothar-worldgen import leonberg`) schreibt nach `<DATA_ROOT>/work/leonberg/`:

```
terrain.r16 / terrain.png        16-Bit-Heightmap, 1 m/Pixel, + terrain.json (Größe, Ursprung, Höhenbereich)
buildings.json                   alle Gebäude
streets.json                     Straßenachsen, Breite, Typ; Plätze als Polygone
features.json                    Mauern, Gewässer, Bäume, Brunnen …
```

**Heightmap** (umgesetzt, Modul `geo/terrain.py`):
- Ein Sample pro DGM1-Rasterzelle, ohne Neuabtastung. Die Samples liegen auf den Zellmitten des DGM1
  (Koordinaten `…,5`) und decken das Gebiet `surroundings` ab, bei ±1000 m also 2000 × 2000 Samples.
- `uint16`, zeilenweise. **Zeile 0 = Norden (−Z), Spalte 0 = Westen (−X).** `terrain.r16` ist Little-Endian
  ohne Header, `terrain.png` ist dieselbe Karte als 16-Bit-Graustufen-PNG.
- Höhe: `y = minY + wert / 65535 · (maxY − minY)` in lokalen Metern über der Ursprungshöhe
  (`y = (NHN − origin.heightNHN) · gameScale.vertical`). `minY`/`maxY` sind der tatsächliche Höhenbereich,
  auf mm gerundet. Für Leonberg ergibt das etwa 2 mm pro Stufe.
- Die Ursprungshöhe ist das DGM1 bilinear am Ursprungspunkt, auf mm gerundet.
- `terrain.json` (Auszug):
  ```json
  { "format": "gothar-terrain", "version": 1, "width": 2000, "height": 2000, "cellSize": 1.0,
    "firstSample": { "x": -999.5, "z": -999.5 },
    "heightRange": { "minY": -31.62, "maxY": 110.15, "stepM": 0.002163272 },
    "areas": { "core": { "minX": -350, "minZ": -350, "maxX": 350, "maxZ": 350 }, "surroundings": { … } },
    "origin": { "crs": "EPSG:25832", "easting": …, "northing": …, "heightNHN": 371.59, "heightReference": "marktplatz" },
    "gameScale": { "horizontal": 1.0, "vertical": 1.0 },
    "source": { "product": "LGL DGM1", "heightDatum": "DHHN2016", "files": [ … ], "credit": "Datengrundlage: LGL, www.lgl-bw.de" } }
  ```
  `firstSample` ist die lokale Position der Mitte von Sample (0,0), also der Nordwest-Ecke.
  `areas` kennzeichnet Kern und Rand, z. B. für spätere Erweiterungen oder für den Editor.
- Der Maßstab `gameScale.horizontal` skaliert `cellSize`, `firstSample` und `areas`;
  `gameScale.vertical` skaliert die Höhen.

`buildings.json` (ein Eintrag pro Gebäude):
```json
{ "id": "DEBW_0010000abc", "footprint": [[x, z], ...], "groundY": 0.4,
  "roof": { "type": "saddle", "eaveY": 9.8, "ridgeY": 14.2, "ridgeDir": [1, 0] },
  "osm": { "building": "house", "levels": 3 }, "areaM2": 112.5 }
```

Annotationen/Overrides pro Gebäude (`tools/worldgen/data/leonberg/buildings/<id>.json`, versioniert):
```json
{ "id": "DEBW_0010000abc", "keep": true, "style": "buergerhaus",
  "storeys": [3.2, 2.9, 2.8], "jettyM": 0.35,
  "frontFacade": { "edge": 2, "openings": [ { "storey": 0, "type": "door", "x": 1.2, "w": 1.1, "h": 2.0 },
                                            { "storey": 1, "type": "window", "x": 0.8, "w": 0.7, "h": 0.9 } ],
                   "timber": "mann", "infill": "plaster_ochre" },
  "roofCover": "tiles_old", "notes": "Eckhaus am Marktplatz", "seed": 1234 }
```

## 5. Die Werkzeuge

### W-A `geo-import` (Python: GDAL/rasterio, pyproj, shapely, lxml, osmium)
- Gebiet aus `leonberg.toml` ausschneiden (Kernbereich Altstadt + Rand für Umland).
- DGM1-Kacheln mosaikieren → Heightmap; Ränder für die spätere Erweiterung kennzeichnen.
- CityGML LoD2 parsen → Grundrisse, Dachflächen → Dachtyp/Höhen klassifizieren → `buildings.json`.
- OSM → `streets.json`, `features.json` (Straßenbreite aus Tags, sonst Schätzung nach Typ).
- Vorschau-PNG (Heightmap + Grundrisse + Straßen) zur Kontrolle.

### W-B Terrain in der Engine (C++, Modul `world`/`render`)
- Heightmap-Terrain in Kacheln (z. B. 64×64 m) mit LOD (geomorphing oder CDLOD).
- **Splatmap** mit 4–8 Materialschichten (Kopfstein, Matsch, Gras, Waldboden, Fels, Acker).
- Löcher (Keller, Höhleneingänge), Kollision über physics (Heightfield-Shape).
- Editor: Sculpt- und Mal-Pinsel, um die reale Topografie spielgerecht zu verbiegen.

### W-C Gebäude-Generator (Blender-Add-on „Gothar Buildings“, Python/bpy)
- Eingabe: `buildings.json` + Overrides; Ausgabe: ein `.glb` pro Gebäude (+ LODs) und ein Sammel-Index.
- **Klötzchen-Modus** (W3): Baukörper + Dach als graue Massen – für Maßstabstests.
- **Mittelalter-Modus** (W5): Regeln statt Nachbau:
  - Stockwerke (Erdgeschoss Stein/verputzt, Obergeschosse Fachwerk), Auskragung pro Geschoss
  - Fachwerk-Muster aus einem Katalog (Mann, Andreaskreuz, Fußstreben, K-Streben), Gefache verputzt/Lehm
  - kleine Fenster mit Läden, Holztüren, Tore für Scheunen/Ställe, Treppen, Vordächer
  - Dach: Typ aus LoD2, Neigung idealisiert, Dachüberstand, Gauben, Schornsteine; Deckung nach Stand (Ziegel/Schindel/Stroh)
  - Alterung: Durchhang, Schiefstand (leicht!), Moos, Ausbesserungen – per Seed variiert
- **Modularer Baukasten + Trim-Sheets** (Balken, Putz, Stein, Holz, Dach) → einheitlicher Look, wenige Texturen, gute Performance.
- Jedes Haus bleibt in Blender von Hand nachbearbeitbar; erneutes Generieren überschreibt nur Häuser ohne `"locked": true`.

### W-D Fassaden-Werkzeug (Python + kleine Oberfläche, z. B. Dear PyGui oder Web-UI)
1. Insta360-Material exportieren (equirektangulär, mit GPS); Einzelbilder in festen Abständen extrahieren.
2. Kamera-Position pro Bild aus GPS; optional verfeinert über Structure-from-Motion (COLMAP) für Genauigkeit < 1 m.
3. Für ein Gebäude: Richtung von Kamera zur Fassade aus `buildings.json` → Ausschnitt aus dem 360°-Bild als Perspektivbild → Entzerrung (Homographie an Trauf- und Bodenlinie) → frontale Fassadenansicht.
4. Oberfläche: bestes Bild pro Fassade wählen, Stockwerkslinien, Öffnungen, Fachwerk-Typ, Materialien anklicken → Override-JSON.
5. Später: automatische Erkennung von Fenstern/Türen als Vorschlag.

Fotos dienen **nur als Referenz**, nicht als Textur (Moderne, Mischlicht, Schatten, Stilbruch).

### W-E Straßen & Plätze
OSM-Achsen + Breite → Splatmap-Schichten (Kopfstein in der Stadt, Matsch/Kies außerhalb), Mittelrinne,
Stufen und Stützmauern an Höhensprüngen; moderne Bordsteine/Markierungen entfallen.

### W-F Ausstattung & Vegetation
Regelbasiertes Verteilen von Requisiten (Fässer, Karren, Zäune, Holzstapel, Misthaufen, Marktstände)
und Vegetation (Bäume, Büsche, Gras) über Masken; Feinarbeit mit Pinseln im Editor (M16).

### W-G Welt-Assembler & Wegnetz-Vorschlag
- Terrain + Gebäude + Straßen + Ausstattung → `.g7world` (Zellen), Kollision, Validierung, Credits.
- **Wegnetz-Vorschlag** aus Straßenachsen: Wegpunkte an Kreuzungen, in festen Abständen und vor
  Haustüren (`WP_LEO_<STRASSE>_<NR>`); Freepoints auf Plätzen. Der Designer bessert im Editor nach.

### Vorhandene Werkzeuge (kein Eigenbau)
Blender (+ Add-on BlenderGIS zum Gegenprüfen), COLMAP / RealityScan / Postshot (Photogrammetrie bzw.
Gaussian Splatting für 3D-Referenzen einzelner Objekte wie Brunnen, Treppen, Mauerreste),
Material Maker / Substance für Trim-Sheets, QGIS zum Sichten der Geodaten.

## 6. Aufnahme-Leitfaden Insta360

- Früh morgens (Sonntag), bedeckter Himmel ideal (weiche Schatten, wenig Leute/Autos).
- Kamera am Selfie-Stick **über Kopfhöhe** (ca. 2,5–3 m) – weniger Verdeckung durch Autos, bessere Sicht auf Obergeschosse.
- GPS aktiv (App bzw. GPS-Fernbedienung), Videomodus mit höchster Auflösung oder Intervall-Fotos alle 1–2 m.
- Langsam und gleichmäßig gehen, jede Gasse in beide Richtungen, Plätze im Raster ablaufen.
- Zusätzlich normale Fotos von Details (Fachwerk-Knoten, Türen, Pflaster, Brunnen).
- Protokoll: Datum, Route, Besonderheiten → `capture/<datum>/notes.md`.

## 7. Mittelalterlicher „Rückbau“ – Gestaltungsregeln

- Alles nach ca. 1700 Erbaute ersetzen oder weglassen; Lücken werden Gärten, Höfe, Ställe, Misthaufen.
- **Stadtmauer** mit Toren ergänzen (Verlauf an historischen Resten und OSM orientieren, frei interpretiert).
- Straßen: in der Stadt Kopfstein mit Mittelrinne, außerhalb Lehm/Matsch; keine Gehwege.
- Dichte erhöhen, wo es atmosphärisch hilft; Sichtachsen auf Schloss/Kirche erhalten.
- Gebäudenutzungen für das Spiel festlegen (Schmiede, Taverne, Händler, Wache) → Grundlage für NPC-Routinen.

## 8. Spielbarkeit vor Realismus

- **1:1 ist meist nicht spielgerecht.** Enge Gassen stören die Third-Person-Kamera; Gothic-Welten sind
  stark verdichtet. Vorschlag: Gassen um 10–30 % verbreitern (Generator verschiebt Fassaden nach innen,
  nicht die Achsen), Abstände ins Umland verkürzen, Umland frei gestalten.
- Entscheidung über Maßstab erst nach dem **Klötzchen-Test** (W3) mit echter Spielfigur und Kamera.
- Leonberg ist **ein Ort** der Spielwelt, nicht die ganze Welt.

## 9. Offene Fragen

- Genaue Gebietsgrenze (Kern + Umland) und Ursprungspunkt.
- Maßstabsfaktoren nach dem Klötzchen-Test.
- Welche realen Bauten bleiben erkennbar (Schloss, Kirche, Marktplatz-Ensemble)?
- Rolle des Ortes in der Geschichte (Lager einer Fraktion? Handelsstadt?) → `docs/design/world.md`.
