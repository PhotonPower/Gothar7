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
  work/<ort>/captures/<name>/    Einzelbilder (frames/*.jpg) + frames.json je Aufnahme (facade frames)
```
`gothar-worldgen download leonberg` füllt `geo/`, `gothar-worldgen info leonberg` zeigt, welche Ordner fehlen.

Im Repo landen nur **abgeleitete, geprüfte Ergebnisse** (`assets/source/worlds/leonberg/…`) und
die Annotationen (`tools/worldgen/data/leonberg/…`, kleine JSON-Dateien).

## 3. Koordinatensystem

- Quelle: ETRS89 / UTM Zone 32N (**EPSG:25832**), Höhen in m über NHN.
- Engine: lokales System in Metern, **Ursprung am Marktplatz**: Marktbrunnen, E 501115 / N 5405347,
  ca. 386,9 m NHN (am Luftbild festgelegt, siehe `leonberg.toml`),
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
    "heightRange": { "minY": -50.991, "maxY": 94.85, "stepM": 0.002225391 },
    "areas": { "core": { "minX": -350, "minZ": -350, "maxX": 350, "maxZ": 350 }, "surroundings": { … } },
    "origin": { "crs": "EPSG:25832", "easting": 501115.0, "northing": 5405347.0, "heightNHN": 386.89, "heightReference": "marktplatz" },
    "gameScale": { "horizontal": 1.0, "vertical": 1.0 },
    "source": { "product": "LGL DGM1", "heightDatum": "DHHN2016", "files": [ … ], "credit": "Datengrundlage: LGL, www.lgl-bw.de" } }
  ```
  `firstSample` ist die lokale Position der Mitte von Sample (0,0), also der Nordwest-Ecke.
  `areas` kennzeichnet Kern und Rand, z. B. für spätere Erweiterungen oder für den Editor.
- Der Maßstab `gameScale.horizontal` skaliert `cellSize`, `firstSample` und `areas`;
  `gameScale.vertical` skaliert die Höhen.

**`buildings.json`** (umgesetzt, Module `geo/lod2.py` + `geo/buildings.py`): Kopf mit `format`
(`gothar-buildings`), `version`, `origin` (dieselbe Bezugshöhe wie `terrain.json`), `gameScale`,
`source` (LoD2-Dateien, Credit) und `count`, danach `buildings` mit **einem Gebäude pro Zeile**:
```json
{ "id": "DEBW_00100061Zl2", "function": "31001_1123", "inCore": true,
  "footprint": [[x, z], ...], "areaM2": 250.2, "groundY": -0.93, "groundMinY": -1.12, "groundMaxY": 0.4,
  "heightM": 17.57,
  "roof": { "type": "saddle", "alkis": "3100", "eaveY": 9.61, "ridgeY": 16.64,
            "ridgeDir": [1.0, 0.017], "pitchDeg": 32.7 },
  "parts": [ { "id": "UUID_…", "footprint": …, "areaM2": …, "groundY": …, "groundMinY": …, "groundMaxY": …,
               "heightM": …, "roof": { … } } ],
  "warnings": [ "…" ] }
```
- Aufgenommen werden alle Gebäude, deren Grundriss im Gebiet `surroundings` liegt (gemessen an einem
  repräsentativen Punkt). `inCore` markiert die Gebäude der Altstadt.
- Koordinaten sind lokal (x, z) in Metern, auf cm gerundet. Der Ring ist nicht geschlossen und läuft in der
  Draufsicht (Norden oben) gegen den Uhrzeigersinn. Innenhöfe ab 1 m² stehen in `holes`.
- `groundY`, `eaveY` und `ridgeY` sind **absolute lokale Höhen**, im selben System wie das Terrain.
  `eaveY` ist der tiefste Dachpunkt, `ridgeY` der höchste; `heightM` = höchster First − Boden.
- `groundMinY`/`groundMaxY` (seit W3): tiefstes und höchstes Heightmap-Sample unter dem Grundriss und auf seinen
  Eckpunkten (auf cm gerundet). Gebäude und Teile außerhalb der Heightmap haben die Felder nicht. Der Generator
  setzt den Sockel auf min(`groundY`, `groundMinY`) − 0,3 m (W-C, Hanghäuser).
- `roof.type` stammt aus der ALKIS-Dachform (`alkis`): `flat` 1000, `shed` 2100, `offset_shed` 2200,
  `saddle` 3100, `hip` 3200, `half_hip` 3300, `mansard` 3400, `tent` 3500, `cone` 3600, `dome` 3700,
  `sawtooth` 3800, `arch` 3900, `tower` 4000, `mixed` 5000, `other` 9999.
  `ridgeDir` ist die Richtung der längsten Kante am höchsten Punkt (vorzeichenfrei, x ≥ 0). Bei
  Flach-, Zelt-, Kegel-, Kuppel- und Turmdächern ist sie `null`. `pitchDeg` ist die flächengewichtete mittlere Dachneigung.
- Gebäude mit LoD2-Gebäudeteilen bekommen `parts`, jeweils mit eigenem Dach. Der Grundriss ist die Vereinigung
  der Teile, `roof` ist das Dach des größten Teils.
- `function` ist der ALKIS-Gebäudefunktionscode (z. B. `31001_1010` Wohnhaus, `31001_2463` Garage;
  `51009_*` sind Bauwerke wie Überdachungen). Eine Zuordnung für das Spiel folgt mit den Annotationen.
- `warnings` (optional) nennt Auffälligkeiten: getrennte Grundrissteile (der größte bleibt erhalten),
  ein fehlendes Dach oder eine Abweichung von mehr als 1 m zur LoD2-`measuredHeight`.
- **Offen:** OSM-Gebäudeangaben (`building`, `building:levels`) sind noch nicht zugeordnet. Geplant ist eine
  Zuordnung über die größte Grundriss-Überlappung, sobald der Generator Stockwerkszahlen braucht (W5).

**`streets.json`** (umgesetzt, Module `geo/osm.py` + `geo/streets.py`): Kopf wie oben, `source` mit
OSM-Datei, Stand (`timestamp`), Credit „© OpenStreetMap-Mitwirkende“ und Lizenz ODbL 1.0; danach zwei Listen:
```json
"streets": [ { "osmId": "w4711", "highway": "residential", "class": "road", "name": "Marktplatz",
               "widthM": 5.5, "widthSource": "default", "surface": "sett", "layer": 1, "bridge": true,
               "points": [[x, z], ...] } ],
"squares": [ { "osmId": "w53012023", "kind": "square", "name": "Marktplatz", "surface": "sett",
               "polygon": [[x, z], ...], "holes": [...], "areaM2": 2787.4 } ]
```
- `streets` enthält Mittellinien aller `highway`-Wege, zugeschnitten auf das Gebiet. Wird ein Weg durch den
  Zuschnitt geteilt, entstehen mehrere Einträge mit derselben `osmId`.
- `class` fasst die `highway`-Werte zusammen: `road` (Fahrstraßen), `pedestrian`, `track`, `path`
  (Fuß-, Rad- und Reitwege) und `steps`. Daraus leitet W6 Belag und Breite ab: Kopfstein in der Stadt,
  Matsch/Kies außerhalb.
- `widthM` stammt aus `width`/`est_width` (Meterangaben), sonst aus `lanes` × 3 m, sonst aus einem
  Standardwert je Typ (z. B. residential 5,5 m, footway 2 m, path 1,5 m). Woher der Wert kommt, steht in
  `widthSource` (`tag`, `lanes`, `default`).
- `squares` sind Flächen mit `place=square`, `amenity=marketplace` oder `highway=pedestrian|footway|…`
  zusammen mit `area=yes`.

**`features.json`** (umgesetzt, Modul `geo/features.py`): eine Liste `features` mit `osmId`, `type`, `kind`
(OSM-Wert), optional `name` und `tags` (Auswahl, z. B. `height`, `material`, `species`, `start_date`), sowie
der Geometrie: `"geometry": "point"` + `position`, `"line"` + `points` oder `"polygon"` + `polygon`/`holes`/`areaM2`.

| `type` | aus OSM | Geometrie |
|---|---|---|
| `wall` | `barrier=wall/city_wall/retaining_wall`, `historic=city_wall` | Linie |
| `hedge` | `barrier=hedge` | Linie |
| `waterway` | `waterway=river/stream/canal/ditch/drain` | Linie |
| `water` | `natural=water`, `waterway=riverbank`, `landuse=reservoir/basin` | Fläche |
| `tree` / `tree_row` | `natural=tree` / `natural=tree_row` | Punkt / Linie |
| `landuse` | `landuse=*` (Wald, Wiese, Acker, Obstwiese, Wohngebiet …), `natural=wood/scrub/…`, `leisure=park/garden/…` | Fläche |
| `fountain` | `amenity=fountain`, `man_made=water_well` | Punkt oder Fläche |
| `landmark` | `amenity=place_of_worship`, `historic=castle/city_gate/monument/…` | Punkt oder Fläche |
| `railway` | `railway=rail/light_rail/tram/narrow_gauge` (modern, nur zur Orientierung) | Linie |

OSM bezieht sich auf WGS84, die LGL-Daten auf ETRS89. Der Unterschied liegt unter 1 m und wird ignoriert.
Zur Kontrolle: Der OSM-Marktbrunnen liegt 3,7 m neben dem am Luftbild gewählten Ursprung.

Annotationen/Overrides pro Gebäude (`tools/worldgen/data/leonberg/buildings/<id>.json`, versioniert):
```json
{ "id": "DEBW_0010000abc", "keep": true, "style": "buergerhaus",
  "storeys": [3.2, 2.9, 2.8], "jettyM": 0.35,
  "frontFacade": { "edge": 2, "openings": [ { "storey": 0, "type": "door", "x": 1.2, "w": 1.1, "h": 2.0 },
                                            { "storey": 1, "type": "window", "x": 0.8, "w": 0.7, "h": 0.9, "y": 0.9 } ],
                   "timber": "mann", "infill": "plaster_ochre" },
  "roofCover": "tiles_old", "notes": "Eckhaus am Marktplatz", "seed": 1234 }
```
Umgesetzt in `facade/overrides.py` (lesen, prüfen, schreiben):
- `edge` ist der Index der Grundriss-Kante aus `buildings.json`: von Punkt `edge` zu Punkt `edge + 1`.
- Öffnungen haben `storey` (0 = Erdgeschoss) und `type` (`door`, `window` oder `gate`). `x`, `w` und `h` sind in Metern;
  `x` wird von der linken Fassadenkante gemessen, von außen gesehen.
- Neu und optional ist `y`, die Brüstungshöhe über dem Stockwerksboden. Fehlt sie, sitzt die Öffnung auf dem Boden
  (Türen, Tore).
- Optional ist `age` (0 neu … 1 alt), die Handkorrektur der Alterung (W5, `leonberg-stil.md`).
- Optional sind `dormers` (0–6) und `chimneys` (0–4): die Zahl der Gauben bzw. Schornsteine, 0 = keine; fehlt der
  Schlüssel, entscheidet der Generator.
- Optional ist `wallHouse` (true/false): Haus auf der Stadtmauerlinie, dessen Außenseite die Mauer ist (W-E2); fehlt
  der Schlüssel, wird es erkannt.
- Optional ist `passages`: Durchgänge durch das Erdgeschoss, z. B. dort, wo ein OSM-Weg als `tunnel` durch das Haus
  führt. Je Durchgang gibt es `axis` (zwei Punkte [x, z], über beide Fassaden hinaus), `w` (lichte Breite, Vorgabe
  2,5 m), `h` (lichte Höhe über dem höheren Ende, Vorgabe 3 m) und optional `note`.
  - Der Generator öffnet beide Fassaden; Fenster und Türen dort entfallen.
  - Darüber liegt ein Sturz im Stil des Hauses: ein Balken im Fachwerk, ein Steinsturz am massiven Erdgeschoss und an
    Mauerhaus-Außenseiten.
  - Innen gibt es Seitenwände und eine Decke.
  - Die Kollision wird geteilt: Über dem Durchgang liegt das Haus, unterhalb Körper neben dem Gang.
  - Ist das Erdgeschoss zu niedrig, wird der Durchgang abgesenkt (Hinweis); ein hohes Erdgeschoss gibt `storeys`.
  - Leonberg: DEBW_00100061ZnA, die Zwerchstraße zur Pforte Zwerchstraße Nord (Entscheidung Projektinhaber
    2026-10-03).
- Ebenfalls optional sind `locked` (siehe W-C) und `rueckbau` (§7): `auto` (Vorgabe), `none` (nie ersetzen) oder
  `split` (immer durch Fachwerkhäuser ersetzen). **Unbekannte Schlüssel bleiben beim Speichern erhalten**, damit neuere
  Werkzeuge Felder ergänzen können.
- Die Datei heißt wie die Gebäude-ID. `validate_against(override, gebäude)` prüft Kante und Öffnungsbreiten gegen
  den Grundriss.

## 5. Die Werkzeuge

### W-A `geo-import` (Python: GDAL/rasterio, pyproj, shapely, lxml, osmium)
- Gebiet aus `leonberg.toml` ausschneiden (Kernbereich Altstadt + Rand für Umland).
- DGM1-Kacheln mosaikieren → Heightmap; Ränder für die spätere Erweiterung kennzeichnen.
- CityGML LoD2 parsen → Grundrisse, Dachflächen → Dachtyp (ALKIS-Code)/Höhen/Firstrichtung → `buildings.json`.
- OSM → `streets.json`, `features.json` (Straßenbreite aus Tags, sonst Schätzung nach Typ; Plätze, Mauern, Wasser, Bäume, Landnutzung, Brunnen, Landmarken).
- Vorschau und Plausibilitätsprüfung (`gothar-worldgen check <ort>`, läuft am Ende von `import` mit):
  `preview.png` (ganzes Gebiet, 1 m/px) und `preview_core.png` (Altstadt, 0,5 m/px) zeigen das schattierte
  Gelände mit Gebäuden (Kern rot, Rest grau), Straßen nach Klasse und Breite, Plätzen, Wasser, Mauern,
  Bäumen, Brunnen und Landmarken, außerdem Legende, Maßstab, Kernrahmen und Ursprungskreuz (Norden oben).
  `report.json` enthält die Prüfungen mit `ok`/`warn`/`fail`. Bei `fail` endet der Befehl mit Exit-Code 1.

  | Prüfung | Kriterium |
  |---|---|
  | `terrain.resolution` | Höhenstufe ≤ 10 mm |
  | `terrain.originInRange` | Ursprungshöhe liegt im Geländebereich (sonst `fail`) |
  | `layers.sameOrigin` | alle Dateien haben denselben Ursprung und dieselbe Bezugshöhe (sonst `fail`) |
  | `buildings.count` | Gebäude vorhanden, davon welche im Kern |
  | `buildings.groundVsTerrain` | Median \|Gebäudeboden − Gelände an den Grundrissecken\| ≤ 0,5 m (`warn` bis 2 m) |
  | `buildings.groundOutliers` | ≤ 5 % der Gebäude weichen > 2 m ab (Hanglagen) |
  | `buildings.heights` / `.warnings` | ≤ 1 % mit Höhe außerhalb 0,5–120 m bzw. mit Umwandlungswarnungen |
  | `streets.count`, `streets.widths` | Straßen vorhanden; Breiten 0,5–40 m |
  | `osm.withinArea` | alle OSM-Punkte im Gebiet (Zuschnitt) |
  | `alignment.roadsInBuildings` | ≤ 3 % der Straßenlänge durch Gebäude (`warn` bis 10 %): prüft, ob OSM und LGL deckungsgleich sind |

  Stand Leonberg: alle Prüfungen `ok`. Die Abweichung von Boden zu Gelände liegt im Median bei 0,03 m,
  1,6 % der Straßenlänge verläuft durch Gebäude (Durchfahrten). Zwei Läufe von `import` liefern byte-identische Dateien.

### W-B Terrain in der Engine (C++, Modul `world`/`render`)
- Heightmap-Terrain in Kacheln (z. B. 64×64 m) mit LOD (geomorphing oder CDLOD).
- **Splatmap** mit 4–8 Materialschichten (Kopfstein, Matsch, Gras, Waldboden, Fels, Acker).
- Löcher (Keller, Höhleneingänge), Kollision über physics (Heightfield-Shape).
- Editor: Sculpt- und Mal-Pinsel, um die reale Topografie spielgerecht zu verbiegen.
- **Gewässer (W2, nach M5 Teil E): Die Heightmap weicht dort bewusst vom DGM ab.** Das DGM hat kein Bachbett (Laserpunkte
  auf Wasser werden überbrückt); `export-terrain` gräbt deshalb nach `tools/worldgen/data/<ort>/water.json` ein Bett bzw.
  Becken ein und schreibt die Wasserkörper nach `generated/water_index.json`; `assemble` setzt sie als `water`-Vobs
  (world.md) unter `WORLDGEN_WATER`, IDs `water:<name>_<n>`. Code: `export/water.py`.
  - **Flüsse** (OSM-Linie nach Name, ohne Durchlässe, aneinanderhängende Stücke verbunden): Bett 4 m breit, 1,2 m unter
    dem geglätteten DGM (`smoothM` 40 m), Böschungen 2 m bis zum ursprünglichen Gelände.
    - Wasserspiegel 0,3 m unter dem geglätteten DGM, höchstens 0,1 m unter dem tiefsten Gelände des Querschnitts (so
      ragt keine Box über das Ufer), flussabwärts nie steigend; das Bett liegt 0,9 m unter dem Spiegel.
    - Boxen bis 15 m lang, geteilt an Biegungen (> 20° bzw. > 0,75 m Abweichung von der Sehne) und wo der Spiegel um
      > 0,25 m fällt; 0,5 m Überlappung, kleine Stufen an den Überlappungen; Oberkante = Spiegel, Boden 0,3 m unter dem
      Bett.
  - **Seen** (OSM-Fläche nach Name): Spiegel = Median des DGM im Umriss minus 0,2 m, Becken 1,5 m unter dem Median,
    erreicht 3 m vom Ufer; je konvexem Teil des Umrisses eine Box.
  - Die Heightmap wird nur abgesenkt, nie angehoben; Gebäude bleiben unberührt (sie nutzen das DGM der Arbeitsdaten).
  - **Leonberg:** Glems 163 Boxen (16 936 Zellen abgesenkt), Parksee 23 Boxen (11 513 Zellen); tiefster Punkt der
    Heightmap jetzt −52,09 m (vorher −50,99 m).
  - Die Wasserfläche zeichnet die Engine bis M17 als durchscheinende Box (Debug-Darstellung), dabei sind auch die
    Seiten unter der Oberfläche zu sehen.
- **Wege und Abgänge (W3-Entscheidungen E5 und E1-C, `leonberg-begehung.md`): Die Heightmap weicht auch dort bewusst
  vom DGM ab.** `export-terrain` gräbt erst die Abgänge vor Hanghaus-Türen (`export/descents.py`, aus den `doors` des
  Gebäude-Index, also nach `buildings`) und begrenzt dann das Längsprofil der begehbaren Wege der Altstadt auf 42°
  (`export/ways.py`, `data/<ort>/ways.json`). Reihenfolge: `buildings` → `export-terrain` → `assemble`.
  - **Offen für M17 (Wasser-Rendering):** Bei 5 von 186 Boxen ragt in engen Biegungen der Glems (steiler Prallhang)
    eine Kante mehr als 0,3 m über das Gelände (Koordinator 2026-10-03: bleibt bis M17 so). Mit echtem
    Wasser-Rendering prüfen; mögliche Abhilfe: kürzere Abschnitte in Biegungen oder Spiegel je Abschnitt am Ufer
    begrenzen.

**Umgesetzt (W2 Teil 1):** `gothar-worldgen export-terrain <ort> [--area surroundings|core] [--step n] [--name …]`
- **Eingabe:** `terrain.json` und `terrain.r16` aus dem Work-Ordner.
- **Zuschnitt und Ausdünnung:** Optional wird auf ein Gebiet zugeschnitten (es zählen die Sample-Mitten)
  und nur jedes n-te Sample behalten.
- **Kodierung:** Über den neuen Höhenbereich wird neu kodiert, gleiche Regel wie beim Import (auf Millimeter nach
  außen gerundet). Ein Export ohne Zuschnitt ergibt eine **byte-gleiche** `.r16`.
- **Ablage**, abgestimmt mit engine und Koordinator 2026-10-03:
  - `assets/source/worlds/<ort>/<ort>_terrain.g7world`: versioniert, etwa 300 Byte. Weltdatei v1 ohne Vobs,
    mit `terrain`-Block nach `docs/modules/world.md`. Geschrieben im Layout des Engine-Writers, damit Diffs
    sauber bleiben. Eine vorhandene Datei behält Vobs, `nextVobId` und weitere Schlüssel; nur der `terrain`-Block
    wird ersetzt.
  - `assets/source/worlds/<ort>/generated/<name>.r16`: **nicht versioniert**, über
    `assets/source/worlds/.gitignore` mit dem Muster `*/generated/`. Bei Leonberg (2000 × 2000) sind das 8 MB.
    Der Dev-Build mountet `assets/source` ohnehin, und `g7-cook` kopiert `.r16` unverändert.
  - Ein frischer Checkout hat die Heightmap erst nach `import` und `export-terrain`, beides braucht DATA_ROOT.
- **Leonberg:** 2000 × 2000 Samples zu 1 m, x und z jeweils −999,5 … 999,5, Höhen −50,991 … 94,85 m,
  Auflösung 2,23 mm.
- **Splatmap (W2 Teil 2, `export/splat.py`):** `export-terrain` schreibt zusätzlich den `splat`-Block
  (abschalten mit `--no-splat`). Das ist eine Grundbelegung als Ausgangspunkt für die Mal-Pinsel im Editor.
  - **7 Schichten** (Kanalreihenfolge = Schichtreihenfolge):
    - Wiese (Rückfall)
    - Kopfstein: Straßen und Plätze im Kernbereich
    - Kies: Straßen und Plätze außerhalb, Bahnlinien
    - Matsch: unter Gebäuden plus 1,5 m Rand, an Bächen und Gräben
    - Waldboden: Wald, Gebüsch, Einzelbäume mit 3 m Radius
    - Acker: Ackerland, Kleingärten, Gärten
    - Fels: Neigung über 35–45°, weich eingeblendet
    - Autobahnen hinterlassen keine Spur.
  - **Raster:** Die Masken werden auf dem Heightmap-Raster gezeichnet, 1 Pixel = 1 Sample.
    Danach 1,5 m weichgezeichnet und nach Priorität übereinandergelegt
    (Acker < Waldboden < Fels < Matsch < Kies < Kopfstein); die Gewichte ergeben je Pixel 1.
  - **Ablage:** Zwei RGBA-Karten `generated/<name>_splat0/1.png` (linear, ohne gAMA/iCCP, nicht versioniert,
    bei Leonberg zusammen 4,4 MB). Der Cooker erkennt sie am `terrain`-Block und kocht sie linear.
  - **Platzhalter-Albedos:** `assets/source/worlds/<ort>/layers/*.png`, 128 × 128, kachelbar, prozedural und
    deterministisch, versioniert, zusammen etwa 35 KB. Die endgültigen Gelände-Texturen sind eine
    Gestaltungsfrage für später (Stil-Referenzblatt W5).
  - **Leonberg** (ganze 2 km): Wiese 41 %, Matsch 23 % (alle Gebäude einschließlich Neubaugebiete),
    Acker 15 %, Waldboden 9 %, Kies 8 %, Kopfstein 2 %, Fels 2 %.
  - `holes` wird noch nicht geschrieben (keine Löcher nötig); Keller und Höhleneingänge kommen später.

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
- **Texturen umgesetzt (W5, Entscheidung Projektinhaber 2026-10-04):** eigene, prozedural erzeugte Texturen in
  `gothar_worldgen/textures/` (`procedural.py`, `apply.py`), kachelbar, je 1024² als Albedo + Normal-Map.
  - Arten: Putz, Putz-Fußband, Putz mit Regenschliere, Schichtmauerwerk (Bruchstein), Balken (Maserung, Risse,
    Äste), Biberschwanz-Ziegel, Ziegel mit Moos (Schattenseite), Bretter (Läden, Türen).
  - Sockel (Rückmeldung Projektinhaber 2026-10-04: in Gassen zu hell und gleichförmig): Der Bruchstein liegt bei
    45 % der Palettenhelligkeit, mit Schmutz und Moos in Flecken. Jedes Haus verschiebt die sich wiederholenden
    Texturen (Putz, Stein, Ziegel, Bretter) um einen festen Versatz aus seiner ID, damit Nachbarhäuser nicht dieselben
    Steine zeigen.
  - Die Palette bleibt der Materialfaktor (Tönung). Die Bilder speichern 1,0 tiefer, damit hellere Details bleiben
    (dunkles Holz noch tiefer); der Materialfaktor gleicht das aus.
  - Häuser verweisen per relativer URI (`../textures/*.png`, glTF-üblich) auf `worlds/<ort>/generated/textures/`; Engine
    und Cooker finden sie, die Engine lädt jede Textur nur einmal (#138,
    Log „N image textures on the GPU“).
  - Texturkoordinaten: Wände in Fassadenmetern, Balken entlang des Balkens, Dächer aus der Fläche neu berechnet
    (Ziegelreihen parallel zur Traufe).
  - Alterung ohne Engine-Änderung (Variante A), als Teile der Putzwand:
    - Fußband: Erdgeschoss-Putz bis 0,8 m über dem Gelände (`*_low`).
    - Regenschlieren: unter etwa zwei Dritteln der Fenster, vier Varianten, kurz und weich auslaufend (`*_streak`).
    - Diese Teilungen zählen nicht ins Fachwerk-Budget.
  - `building_rules.json` → `textures`: `probe` (Probe-Häuser), `all` (alle Häuser), `size`, `dirtM`.
- **Detailstufen umgesetzt (W5, Plan freigegeben 2026-10-04, Vertrag mit engine in coordination.md):** Jedes Haus
  des Kerns enthält drei Render-Knoten mit gleichem Ursprung und geteilten Materialien; die Kollision gibt es nur einmal.
  - `<name>` (lod0): das volle Haus.
  - `<name>_lod1`, ca. 31 %: geschlossene Wände, Fenster und Türen als dunkle Flächen davor, Fachwerk nur als
    Frontflächen ohne Figuren (nie mehr Fachwerk als lod0), keine Gauben, keine Verwitterungs-Teilung.
  - `<name>_lod2`, ca. 5 %: die Baukörper mit Dach in den Hausfarben, dazu die Fensterflächen von lod1.
  - Leonberg: 1,76 Mio. Dreiecke in lod0, 0,54 Mio. in lod1, 0,09 Mio. in lod2.
  - Die Engine wählt je Vob nach Entfernung (60/150 m); ohne Auswahl zeichnet sie lod0.
  - `building_rules.json` → `lod`: `enabled`, `suffixes`; `preview` (1 oder 2) zeichnet für Prüfbilder diese Stufe als
    lod0.
  - Gelände: Die Schicht „Kopfstein“ ist runde, unregelmäßige Kopfsteine (eingebackene Rundung, das Gelände hat keine
    Normal-Maps). Alle Gelände-Schichten haben 512², weil die Engine gleich große Bilder verlangt.
- Jedes Haus bleibt in Blender von Hand nachbearbeitbar; erneutes Generieren überschreibt nur Häuser ohne `"locked": true`.
- **Umgesetzt (W3 Teil 1, Klötzchen):** `gothar-worldgen buildings <ort> [--area core|all]`
  - **Geometrie-Kern** `buildings/massing.py`, reines Python:
    - Wände werden aus dem Grundriss bis zur Dachfläche extrudiert; Giebel entstehen von selbst.
    - Dächer sind Höhenfunktionen über beliebigen Polygonen: flach; Satteldach mit First entlang `ridgeDir`
      durch die Grundrissmitte (der Grundriss wird an der Firstlinie geteilt und trianguliert); Pultdach.
    - Walm- und Zeltdach werden zum Satteldach. LoD2 `mixed`/`other` wird zum Satteldach, wenn `ridgeDir`
      vorhanden ist, sonst flach auf halber Höhe.
    - Gebäude mit LoD2-Teilen werden Teil für Teil gebaut.
    - Leonberg-Kern: 905 Häuser, 23 271 Dreiecke, 1,1 s. Ersatzformen: mixed→saddle 273, other→flat 44,
      mixed→flat 33, other→saddle 13, hip/tent→saddle 5.
  - **Sockel:** Er beginnt beim niedrigeren Wert aus LoD2-`groundY` und dem tiefsten Heightmap-Sample unter dem
    Grundriss, minus 0,3 m. Der Bericht nennt Häuser, deren LoD2-Boden mehr als 0,5 m über dem DGM liegt
    (Leonberg: 27, höchstens 1,7 m).
  - **Ausgabe:** Je Altstadthaus eine `.glb` in `assets/source/worlds/<ort>/generated/buildings/`, nicht versioniert.
    - Ursprung = Grundriss-Schwerpunkt auf Sockelhöhe; der Vob setzt die Lage.
    - Dazu `generated/buildings_index.json` mit Mesh-Pfad, Lage, Dreiecken und DGM-Bereich.
    - **Dateinamen** sind kleingeschrieben und tragen einen Hash der ID (`debw_…_1a2b3c4d.glb`). LoD2-IDs
      unterscheiden sich teils nur in Groß-/Kleinschreibung, was NTFS und der Cooker als gleiche Datei werten.
    - Gleiche Geometrie teilt sich eine Datei. Veraltete Dateien werden entfernt.
  - **Umland** (`--area all`): je 64-m-Zelle eine zusammengefasste `.glb`, bis Distanz-Culling und Batching da sind.
    Engine-Messung 2026-10-03: 905 Einzel-Vobs 3,5 ms, 5400 Einzel-Vobs 79 ms.
  - **Overrides:** `locked` behält die vorhandene Datei; `keep: false` lässt das Haus weg.
  - **glTF-Writer** `buildings/gltf.py`: Grenzen siehe Worldgen-README. Die Dateien kocht `g7-cook` ohne Warnung;
    ein Test kocht eine erzeugte Datei, sofern `g7-cook` gebaut ist.
  - **Blender-Add-on** `tools/worldgen/blender/gothar_buildings` (nur für Handarbeit; Blenders Python hat kein
    shapely, der Generator läuft außerhalb):
    - „Gebäude importieren“ holt die `.glb` nach ID oder im Umkreis des 3D-Cursors an ihre Weltposition.
    - „Auswahl zurückschreiben“ exportiert an denselben Pfad (Ursprung und Achsen wie erzeugt) und setzt
      **`locked: true`** im Override.
    - Ein Headless-Rundlauf mit Blender 4.5 ist getestet, wo Blender installiert ist.
- **Vorgabe `--mode medieval`** (seit W5, Entscheidung 2026-10-03): `buildings` erzeugt standardmäßig die Fachwerkhäuser
  mit Rückbau; `--mode massing` liefert weiterhin echtes LoD2 als graue Klötzchen.
  - Die versionierte `assets/source/worlds/leonberg/leonberg.g7world` ist der medieval-Stand: 1538 Vobs, darunter
    1405 Häuser.
  - Die Ersatzhäuser haben neue VobIds; die IDs der 132 ersetzten Originale bleiben in `vob_ids.json` reserviert.
- **W5-Stil (Schritt 2, festgelegt 2026-10-03)**, Stil-Referenzblatt `docs/design/leonberg-stil.md`:
  - **Stilzuweisung** je Haus (Override, Marktplatz, Rückbau, ALKIS, OSM, Lage). Danach eine gewichtete, deterministische
    Wahl von Muster, Ausfachung, Dachdeckung und Balkenfarbe aus `hash(id, seed)`.
  - **Override-Vorrang:** `style`, `frontFacade.timber`/`infill` und `roofCover`, über `vocabulary` auf
    Generator-Namen abgebildet.
  - **Muster-Katalog** als Segmentdaten:
    - Schwäbischer Mann, Halber Mann, Ständer und Riegel (auch mit Kopf- und Fußstreben), Andreaskreuz, Feuerbock.
    - Raute nur in Brüstungsfeldern, also unter Fenstern.
    - Figuren an den Fassadenenden und in jedem 3. Feld.
  - **Erdgeschoss:** massiv (Bruchstein) oder Fachwerk auf 0,6 m Steinsockel. Steinhaus, Kirche und Mauer ohne
    Fachwerk; die Mauer ohne Öffnungen. Scheunen mit Tor.
  - **Flache und flach geneigte Dächer** (unter 35°) werden Satteldächer mit 50–55°, die Traufe bleibt (485 Dachmassen).
    Kein Stroh innerhalb der Mauer.
  - **Palette mit 14 Werten** statt 5 fester Rollen; Moos (eigener Wert) auf nordseitigen Dachflächen mit
    Biberschwanz alt.
  - **Kern:**
    - 1405 Häuser, 1,81 Mio. Dreiecke (je Haus Median 1394).
    - Stufen: 972 mit vollem Muster, 145 ohne Muster, 147 ohne Feldständer, 142 ohne Fachwerk; 10 Häuser über dem
      Budget, alle geschützt.
    - Stile: Handwerker 658, Scheune/Stall 294, Bürger 241, Ackerbürger 203, Mauer 6, Steinhaus 2, Kirche 1,
      Amtshaus 1.
    - Statistik in `buildings_index.json` → `stats.style`.
- **W5-Vorarbeit: Regelwerk (Mechanik)**, `gothar-worldgen buildings <ort> --mode medieval`. Vorgabe bleibt
  `massing`, bis der Stil festgelegt ist. Code: `buildings/medieval.py`.
  - **Trennung:** Der Generator kennt nur abstrakte Parameter und Material-Rollen. Alle Zahlenwerte und die Bedeutung
    der Begriffe stehen in `tools/worldgen/data/building_rules.json`. Das ist ein **Entwurf mit neutralen
    Platzhaltern; die Festlegung trifft der Projektinhaber**. Die Zuordnung `vocabulary` (Begriffe aus
    `facade_vocabulary.json` → Parameter) ist leer, katalogisierte Fachwerk-Muster sind noch nicht nachgebildet.
  - **Stockwerke:** aus `storeys` der Annotation (zu wenig → oberstes gestreckt, zu viel → anteilig gestaucht),
    sonst Aufteilung der Wandhöhe (Boden bis Traufe) nach Erd- und Obergeschoss-Höhe mit Mindest- und Höchstzahl.
    Das Erdgeschoss reicht bis zum Sockel hinunter.
  - **Auskragung:** Jedes Obergeschoss springt auf den gewählten Seiten um `jettyM` (Annotation, sonst Vorgabe) vor.
    Vorgabe sind die **Straßenseiten**: Die Kante zeigt innerhalb von 12 m auf eine Straße aus `streets.json`,
    oder sie ist `frontFacade.edge`. Die Unterseite wird geschlossen. Ist das Versetzen ungültig, gibt es keine
    Auskragung (Hinweis im Bericht).
  - **Öffnungen:** An `frontFacade.edge` exakt aus der Annotation (`storey`, `x` von links von außen, `y`, `w`,
    `h`), sonst prozedural: ein Fensterraster je Geschoss und eine Tür im Erdgeschoss der längsten Straßenseite.
  - **Türwand (W6 „Türen verlegen“, Entscheidung Koordinator 2026-10-04):** Eine Tür kommt nur an eine Wand, vor
    der 1,2 m und 2,2 m vor der Mitte kein anderes Haus, kein anderer Baukörper desselben Gebäudes und nicht die
    Stadtmauer näher als 0,5 m steht (Raumindex aller Grundrisse in `batch.py`, Rückbau-Häuser ersetzen ihr
    Ausgangsgebäude). Reihenfolge: freie Straßenwand mit Weg zu einer Straße (gerade Linie bis 30 m, durch kein
    Haus, kein 0,5-m-Stück steiler als 35°), freie Seiten- oder Rückwand (≥ 2 m) mit Weg, die zur nächsten Straße
    zuerst (Hinweis „door moved: street side blocked“), dann freie Wände ohne Weg. Hat ein Baukörper keine freie
    Wand, bestimmt die Straßenwand nur die Fußbodenhöhe; eine Tür wird nicht gezeichnet (kein Rahmen in der
    gemeinsamen Wand), im Index `blocked`, im Wegnetz-Bericht `doorsWithoutAccess` („ohne Zugang“). Wichtige
    Häuser bekommen später gezielt einen Zugang per `passages` bzw. Hof-Override.
    Öffnungen sind ausgeschnitten, mit Laibung und zurückgesetzter Füllung (Rolle `frame`). Bei Gebäuden mit
    LoD2-Teilen wird `frontFacade` ignoriert (Hinweis), weil sich die Kanten auf den Gesamtgrundriss beziehen.
  - **Fachwerk in den Obergeschossen:** Schwelle und Rähm laufen über die ganze Breite. Ständer stehen an den Ecken,
    neben jeder Öffnung und im Feldraster. Ein Muster ist eine Liste von Balkensegmenten in normierten
    Feldkoordinaten (Daten); mitgeliefert ist nur `std_platzhalter` (ein Riegel).
    - Öffnungen schneiden die Balken.
    - Balken sind Quader mit drei sichtbaren Flächen (Front, zwei Längsseiten); Rück- und Stirnflächen liegen an
      Wand bzw. Nachbarbalken.
    - Die Balkenarten liegen in leicht unterschiedlicher Tiefe, damit es keine deckungsgleichen Flächen gibt
      (Test: keine doppelten Dreiecke).
  - **Dach:** Höhenfunktionen wie bei `massing`, über dem obersten Geschoss mit Dachüberstand (Traufe und Ortgang),
    Dicke, Unterseite im Überstand und Stirnbrett. Schornsteine nahe dem First und wenige Schlepp- bzw. Giebelgauben
    auf dem größten Satteldach (Regeln in `leonberg-stil.md`).
  - **Seeds:** Variation (Erdgeschoss-Höhe, Fensterraster) ist deterministisch aus `hash(id, seed)`;
    `seed` der Annotation überschreibt. `locked` und `keep` wie bei `massing`.
  - **Material-Rollen** `wall_ground`, `infill`, `timber`, `roof`, `roof_north`, `frame`, `chimney` mit **exakt
    gleichen Werten in allen Häusern**. Die Engine bündelt nach Materialwerten (Multi-Draw, M4 Teil B); Vertexfarben verwirft sie.
    Je `.glb` gibt es ein Primitive pro Rolle.
  - **Budget:** 2000 Dreiecke je Haus, im Kern ≤ 2 Mio. (mit engine abgestimmt). Darüber wird das Fachwerk stufenweise
    reduziert: ohne Muster, dann ohne Feldständer, dann ohne Fachwerk; der Bericht nennt die Stufe.
  - **Kollision** (`buildings/collision.py`, Vertrag `COL_` in `docs/modules/asset.md`, M5 Teil B):
    - Je Baukörper der Grundriss des Erdgeschosses (ohne Auskragung). Ist er nahezu konvex (2 %), wird er ein Teil.
      Sonst wird er an einspringenden Ecken entlang der Wände geschnitten (L → 2, U → 3 Rechtecke) oder, wenn das
      weniger Teile ergibt, aus verschmolzenen Dreiecken zerlegt. Splitter unter 0,5 m² kommen zum Nachbarn.
    - Je Teil ein geschlossener konvexer Körper `COL_HULL_<i>`: Boden auf der Basis, Wände bis zur Dachfläche, die
      Dachflächen der (steilen) Dachform ohne Überstand und ohne Durchhang, First an den Kanten eingefügt. Ein Rechteck
      ergibt 20 Dreiecke.
    - Gauben, Schornsteine, Balken, Fenster, Dachüberstand und Auskragung kollidieren nicht.
    - Mehr als 8 Teile oder mehr als 200 Dreiecke: stattdessen ein Dreiecksnetz `COL_0` der Grundform.
    - Die Knoten liegen auf Wurzelebene neben dem Render-Knoten, mit demselben Ursprung, ohne Material. In den
      Umland-Zellen werden sie wie die Primitives in den Zellursprung verschoben. Das Blender-Add-on hängt sie beim
      Import an das Haus (Drahtgitter) und schreibt sie mit zurück.
    - Index: `collisionTriangles` je Haus; Statistik `collision` (Hüllen, geschnittene Grundrisse, Ersatznetze,
      Median, Maximum, über Budget).
    - **Möglicher Folgepunkt:** Die Spielfigur (Kapsel, 1,8 m) bleibt unter dem Erdgeschoss, die Hüllen reichen für
      sie. Die Third-Person-Kamera (Kugel-Abfrage) kann aber in auskragende Obergeschosse und den Dachüberstand
      eintauchen. Sieht engine beim Kameratest Clipping, kommt eine optionale Stufenhülle je Geschoss dazu
      (Entscheidung nach dem Test).
  - **Leonberg-Kern** (Platzhalterwerte):
    - 1,03 Mio. Dreiecke, je Haus Median 1114, p90 1966, max 10 891.
    - Stufen: 599 Häuser voll, 65 ohne Muster, 45 ohne Feldständer, 196 ohne Fachwerk.
    - 71 große Gebäude liegen auch ohne Fachwerk über dem Budget, vor allem wegen vieler Fenster.
    - Laufzeit 91 s, 85 MB `.glb` (generiert).
  - Die Engine lädt `leonberg.g7world` mit diesen Häusern ohne Warnungen; `g7-cook` kocht sie mit fünf
    Materialien ohne Warnung (Test).
  - **Offen für den Projektinhaber:** Begriffe und Muster-Katalog, Zahlenwerte mit Gestaltungswirkung, Farben und
    Texturen (Trim-Sheets), Stil-Referenzblatt. Außerdem: Sollen große Neubauten (über Budget, viele Fenster)
    überhaupt Fachwerk bekommen oder beim Rückbau (§7) weichen?
- **Gebäude auf dem Gelände (W3 beachten):** Das DGM1 hat am Hang **Stufen entlang von Häuserreihen**. Es sind in den
  Hang gebaute Häuser mit Geländesprung an der Hauswand; aufgefallen ist das in der Engine beim W2-Export (2026-10-03).
  - Insgesamt liegen Steilstellen nicht bevorzugt an Grundrissen (> 45°: 18,6 % nahe Grundrissen bei 35,7 %
    Flächenanteil). Örtlich am Hang aber schon.
  - Folge für Generator und Assembler:
    - `groundY` ist schon der **tiefste** Punkt der LoD2-Bodenfläche; der Baukörper beginnt also talseitig richtig.
      Bergseitig liegt das Gelände höher, die Wand läuft dort ins Gelände (gewollt, sichtbarer Sockel talseitig).
    - Prüfen, ob LoD2-`groundY` und DGM unter dem Grundriss zusammenpassen. Liegt das DGM talseitig tiefer als
      `groundY`, schwebt die Hausecke; dann den Sockel bis zum tiefsten DGM-Punkt verlängern.
    - Dafür ergänzt `import` je Gebäude und Teil `groundMinY`/`groundMaxY` aus der Heightmap (siehe §4).
    - In der Begehung (W3) gezielt die Hangreihen ansehen.

### W-D Fassaden-Werkzeug (Python + Web-UI)
1. Insta360-Material exportieren: in Insta360 Studio als equirektanguläres 360°-MP4 (2:1) und den GPS-Track als GPX.
   Dann Einzelbilder in festen Abständen extrahieren (ffmpeg, siehe unten).
2. Kamera-Position pro Bild aus GPS; optional verfeinert über Structure-from-Motion (COLMAP) für Genauigkeit < 1 m.
3. Für ein Gebäude wird die Fassade direkt aus dem 360°-Bild **auf ihre Ebene projiziert**. Jeder Pixel der
   frontalen Ansicht ist ein Punkt auf der Fassade: Grundriss-Kante plus Boden- bis Traufhöhe aus `buildings.json`.
   Dessen Richtung von der Kamera wird im equirektangulären Bild abgetastet. Das ist genauer als eine Homographie
   aus Klickpunkten und braucht keine Handarbeit.
4. Oberfläche (**Web-UI**, Entscheidung des Projektinhabers 2026-10-03): bestes Bild pro Fassade wählen,
   Stockwerkslinien, Öffnungen, Fachwerk-Typ und Materialien anklicken → Override-JSON.
5. Später: automatische Erkennung von Fenstern/Türen als Vorschlag.

Fotos dienen **nur als Referenz**, nicht als Textur (Moderne, Mischlicht, Schatten, Stilbruch).

**Umgesetzt (W4 Schritt 1, mit synthetischen 360°-Bildern geprüft)**, Paket `tools/worldgen/src/gothar_worldgen/facade/`,
unabhängig von der Oberfläche:
- **`equirect.py`:** Pixel ↔ Richtung im lokalen System, bilineares Abtasten mit Umlauf, perspektivische Ausschnitte.
  - Bildmitte = Blickrichtung `heading_deg` (Kompass, im Uhrzeigersinn ab Nord). Positive Länge liegt rechts, Zeile 0 ist oben.
  - `CameraPose` erlaubt Nick- und Rollwinkel; bei stabilisiertem Horizont sind beide 0.
- **`rectify.py`:** Fassade aus Grundriss-Kante (`facade_from_footprint`) und frontale Ansicht (`rectify`).
  - Von außen gesehen liegt der Startpunkt einer Kante (Grundriss gegen den Uhrzeigersinn) links.
  - Ausgegeben werden außerdem Entfernung, Winkel zur Fassadennormale und Detaildichte des 360°-Bildes in px/m.
  - Daraus ergibt sich ein Qualitätswert von 0 bis 1 für die Auswahl.
  - Liegt die Kamera hinter der Fassade, gibt es eine Fehlermeldung.
- **`poses.py`:** GPX-Track (Insta360-Studio-Export) → lokale Positionen (pyproj + `LocalFrame` wie in W1).
  - Zeitliche Interpolation, Blickrichtung aus der Gehrichtung plus fester Versatz.
  - Kamerahöhe: Gelände plus 2,7 m (Stab über dem Kopf, §6).
  - `rank_views` wählt für eine Fassade die besten Aufnahmen (nah und frontal; von innen oder zu weit weg wird verworfen).
- **`overrides.py`:** Schema der Override-JSON (siehe §4).
- **`preview.py` und CLI** `gothar-worldgen facade preview <ort> <gebäude-id> --image <360.jpg> --pose x,z[,y] --heading <grad>`:
  - Schreibt `<work>/<ort>/facades/<id>_edge<n>.png` für alle Fassaden, die die Kamera von außen sieht,
    und meldet Entfernung, Winkel und Qualität.
- **Tests:** Eine per Raycasting gerenderte Schachbrett-Fassade wird zu über 99 % pixelgenau zurückgewonnen,
  bei jeder Kamerarichtung. Auch schräge Ansichten mit 50° lassen sich entzerren, bekommen aber einen niedrigeren
  Qualitätswert. GPX, Ranking und Schema sind ebenfalls abgedeckt.
- **Einzelbilder und Zeitabgleich (W4 Schritt 2)**, `frames.py`, `sync.py`, `capture.py`:
  - CLI `gothar-worldgen facade frames <ort> <video.mp4> --gpx <track.gpx> [--every 2] [--start <ISO-Zeit>]
    [--heading-offset <grad>]`.
  - Extrahiert alle `--every` Sekunden ein JPEG nach `<work>/<ort>/captures/<name>/frames/`. Schon vorhandene
    Bilder werden wiederverwendet, ein erneuter Lauf mit anderem Zeitversatz ist daher schnell.
  - Schreibt `frames.json` mit Videozeit, UTC-Zeit, Position (x, y, z) und Blickrichtung je Bild.
    Diese Posen nutzen `rank_views` und die Web-UI.
  - **Zeitabgleich:** Die Erstellungszeit im Video ist unzuverlässig: Sie kann die Exportzeit sein, und die
    Kamerauhr kann abweichen. Deshalb wird die Startzeit geschätzt.
    - Grundlage ist die Bildänderung zwischen aufeinanderfolgenden Bildern. Gemessen wird im Horizontband,
      weil Zenit und Nadir mit Stab und Träger sich beim Gehen kaum ändern.
    - Diese Bildänderung wird mit der GPS-Geschwindigkeit korreliert, denn Stehenbleiben ist in beiden zu sehen.
      Die Startzeit mit der besten Korrelation gewinnt.
    - Als zuverlässig gilt die Schätzung bei einer Korrelation ≥ 0,5 und einem Abstand ≥ 0,1 zum besten Wert,
      der mehr als 10 s entfernt liegt.
    - Ist die Schätzung unzuverlässig, wird die Video-Metadatenzeit genommen, sofern sie im Track liegt,
      sonst die Schätzung mit Warnung. `--start` hat immer Vorrang.
    - Für einen sauberen Abgleich: zu Beginn der Aufnahme 10–20 s stehen bleiben, dann losgehen (§6).
  - **Blickrichtung:** Gehrichtung aus dem Track plus `--heading-offset`. Das gilt für Exporte, deren Bildmitte
    der Kamerafront folgt, also die übliche Einstellung beim Gehen.
  - Kamerahöhe: Gelände aus `terrain.r16`, falls `import` gelaufen ist, plus 2,7 m.
  - **ffmpeg:** externes Programm, siehe `docs/05-build.md` (Version, Installation, Suche).
  - Getestet mit einem synthetischen 360°-Video, das ffmpeg selbst erzeugt: Die Startzeit wird auf ±1 s genau
    wiedergefunden. Die Logik ist ohne ffmpeg getestet; die ffmpeg-Tests werden übersprungen, wenn es fehlt.
- **Annotations-Web-UI (W4 Schritt 3a)**, `facade/webui/`: `gothar-worldgen facade ui <ort> [--port 8765]`
  öffnet den Browser.
  - **Technik:** Python-stdlib-Server (`http.server`) und eine statische Seite (HTML, Vanilla-JS, Canvas).
    Es gibt keine neuen Abhängigkeiten und keinen Build-Schritt; die Logik liegt in `webui/api.py`
    ohne HTTP-Code.
  - **Karte:** Grundrisse aus `buildings.json`, gefärbt nach Status (offen, annotiert, gesperrt, fehlerhaft),
    dazu die Kamerapunkte aus allen `captures/*/frames.json`. Es gibt keine Kartenkacheln aus dem Netz (ADR 0012).
  - **Gebäude:** Pro Fassaden-Kante (ab 1 m Breite) werden die drei besten Aufnahmen als entzerrte Vorschau
    angezeigt (`rank_views`). Man kann auch ohne Bild annotieren.
  - **Editor:**
    - Stockwerkslinien setzen und verschieben (→ `storeys`).
    - Öffnungen als Rechtecke aufziehen, verschieben und löschen (→ `storey`, `type`, `x`, `w`, `h`, `y`).
    - Auswahlfelder für Stil, Fachwerk, Ausfachung und Dachdeckung; Felder für Auskragung, Notizen,
      `keep` und `locked`.
    - Unbekannte Schlüssel und `seed` einer vorhandenen Datei bleiben erhalten.
  - **Speichern:** Geschrieben wird nach `tools/worldgen/data/<ort>/buildings/<id>.json` (versioniert).
    Die Prüfung entspricht `overrides.from_json` und `validate_against`; Fehler zeigt die Seite an.
    Die entzerrten Bilder liegen als Cache in `<work>/<ort>/facade_cache/` und nicht im Repo.
  - **Auswahllisten:** `tools/worldgen/data/facade_vocabulary.json`. Das ist ein **Entwurf; die Festlegung
    trifft der Projektinhaber vor W5.**
  - **Sicherheit:**
    - Der Server ist nur an 127.0.0.1 gebunden.
    - Fremde `Host`-Header (DNS-Rebinding) und fremde `Origin`-Header bei schreibenden Anfragen werden abgewiesen.
    - Pfade mit `..`, Backslash oder kodierten Varianten (`%2e%2e`, `%5c`, `%2f`, doppelt kodiert) werden
      vor dem Routing abgewiesen.
    - Statische Dateien kommen nur aus einer festen Liste. Gebäude-IDs und Aufnahmenamen sind auf
      `[A-Za-z0-9_.-]` ohne `..` beschränkt.
    - Requests sind auf 1 MiB begrenzt. Fehlerseiten sind JSON, ohne Stacktrace und ohne lokale Pfade.
  - **Tests:** API und Server mit synthetischen Daten, darunter alle genannten Pfad-Varianten.
    Den Editor habe ich zusätzlich im Browser (headless Edge) durchgespielt: Linien und Öffnungen zeichnen,
    speichern, neu laden.
  - **Folgt in 3b:**
    - Kamerapose pro Bild nachjustieren (`pose_fixes.json` im Work-Ordner).
    - Mehrere Bilder einer Fassade vergleichen.
- **Noch offen:**
  - SfM-Verfeinerung.
  - Prüfung mit echten Aufnahmen.

### W-E Straßen & Plätze
OSM-Achsen + Breite → Splatmap-Schichten (Kopfstein in der Stadt, Matsch/Kies außerhalb), Mittelrinne,
Stufen und Stützmauern an Höhensprüngen; moderne Bordsteine/Markierungen entfallen.

### W-E2 Stadtmauer (`gothar-worldgen citywall <ort>`, W6)
Entscheidung Koordinator im Auftrag des Projektinhabers (2026-10-03); Werte in `building_rules.json` → `cityWall`,
Verlauf in `tools/worldgen/data/<ort>/city_wall.json` (versioniert, von Hand korrigierbar). Code: `walls/citywall.py`.
- **Verlauf:** geschlossener Ring aus OSM-Stücken (`barrier=city_wall`, Richtung wird angepasst) und Stützpunkten, frei
  interpretiert. In Leonberg:
  - Süd- und Westmauer aus OSM.
  - Im Osten durch die äußere Häuserzeile zwischen „Im Zwinger“ und „Grabenstraße“ (äußere Zwingermauer am Graben),
    0,9 m hinter der Fassadenflucht, sodass die Mauer bündig mit den Mauerhäusern abschließt.
  - Im Norden 3–5 m innerhalb von „Hinterer Zwinger“, von dort am Hangrand zum Schloss.
  - Dazu offene Zwingermauern (`zwinger`, 4 m, ohne Wehrgang): die innere OSM-Linie im Schlossgarten.
- **Häuser auf der Linie:** Wo die Mittellinie durch ein Haus läuft, entsteht keine Mauer; die Enden reichen
  `thicknessM / 2 + stossM` (1,2 m) ins Haus. Häuser, die die Linie nur berühren, stehen an der Mauer.
  - Bauten, die schmaler als die Mauer sind (Mauerreste), werden überbrückt. Endet eine Mauer sonst im Freien, wird
    sie bis zu 6 m weiter ins Haus geführt.
  - Der Bericht nennt verbleibende offene Enden (Leonberg: keine). Grundlage sind die Häuser nach dem Rückbau.
- **Querschnitt:** 1,8 m dick. Der Wehrgang (1,2 m, Wunsch engine: zwei Figuren kommen aneinander vorbei) liegt 6 m
  über dem geglätteten Gelände (`smoothM` 12 m). Außen Brustwehr 0,6 m dick und 1,0 m hoch, Zinnen 1,5 m breit und
  0,8 m hoch mit 0,9 m Lücke; etwa 10 % fehlen (Alterung). Fuß 1 m unter dem tiefsten Gelände des Stücks.
- **Flankentürme:** 5 × 5 m, 2,5 m nach außen vorspringend, 11 m, Zeltdach, Schießscharten.
  - An Knicken über 35° und sonst etwa alle 50 m, nur auf freien Stücken mit höchstens 15 % Gefälle.
  - Der Wehrgang führt in einem Durchgang (2,2 m hoch) durch sie hindurch.
- **Tortürme** (Arbeitsnamen „Oberes Tor“ Ost, „Unteres Tor“ Nord; die Namen im Spiel legt der Projektinhaber fest):
  8 × 8 m, 17 m, Zeltdach, Spitzbogen-Durchfahrt 3,6 m breit (Kämpfer 2,6 m, Scheitel 4,6 m), offene Torflügel.
- **Pforten** an den übrigen Straßen: 2,0 × 2,8 m mit Sturz. Wo Fußwege oder Treppen kreuzen, läuft die Mauer durch.
- **Treppen:** je Torturm eine Steintreppe innen an der Mauer auf den Wehrgang, auf einem freien, geraden Stück bis
  40 m vom Tor. Stufen ≤ 0,2 m hoch, 0,3 m tief, 1,2 m breit.
- **Kollision** (`COL_HULL_`, Vertrag in `docs/modules/asset.md`):
  - Mauerkörper bis zum Wehrgang und Brustwehr bis zu ihrer Oberkante, je nahezu geradem Abschnitt bis 12 m.
    Zinnen ohne Kollision: Die Brustwehr ist besteigbar (Entscheidung Projektinhaber, Gothic-typisch).
  - Türme außerhalb der Mauer und über dem Durchgang. Tortürme: zwei Pfeiler und der Block über dem Kämpfer.
  - Die Durchfahrten bleiben frei (Test). Treppen als Rampe über die Stufenkanten, oben bündig mit dem Wehrgang,
    unten eine Stufe (Charakter-Controller: Stufe ≤ 0,3–0,4 m, Steigung ≤ 50°, Kapsel 0,3 × 1,8 m).
- **Pforten in Zwingermauern:** `gates` mit `"wall": "zwinger"` (nur `pforte`, optional `w`) sitzen am nächsten Punkt
  der Zwingermauer, mit Sturz und freiem Durchgang. Leonberg: `pforte_garten_west`, 2,8 m breit, vor dem Westtor des
  Pomeranzengartens; die Gartentreppe führt hindurch (Entscheidung Projektinhaber 2026-10-03).
- **Ausgabe:** `generated/citywall/*.glb` (Abschnitte ≤ 40 m, auch die Zwingermauern, Tortürme mit Treppe einzeln) und
  `citywall_index.json`. Der Assembler hängt sie als Mesh-Vobs `CITYWALL_*` unter `WORLDGEN_CITYWALL`, Kategorie
  `gameplay`, IDs `citywall:<abschnitt>` in `vob_ids.json`.
- **Mauerhäuser** (Entscheidung Projektinhaber 2026-10-03): Häuser auf der Mauerlinie bilden die Mauer, der Ring
  wirkt von außen geschlossen.
  - Erkennung: Die Mittellinie läuft mindestens 0,5 m durch den Grundriss (Häuser nach dem Rückbau). `buildings` und
    `citywall` nutzen dieselbe Funktion (`walls.citywall.wall_houses` bzw. `wall_context`); Override `wallHouse`.
  - Außenseiten: Grundrisskanten, hinter denen (1,5 m) die Stadt endet und die höchstens 8 m von der Linie liegen.
  - Dort: Bruchstein über alle Geschosse, kein Fachwerk, keine Auskragung, keine Tür, kein Dachüberstand.
    Kleine Fenster (0,4 × 0,6 m) nur über der Mauerkrone, darunter Schießscharten (0,25 × 1,0 m, ab dem 1. OG).
  - Ist die Traufe niedriger als die Mauerkrone (dieselbe Höhe wie die Nachbarmauer), steigt die Außenwand als
    Schildmauer (0,6 m dick) bis zur Krone, mit Zinnen und eigener Kollisionshülle. Höhere Häuser: die Steinwand
    geht bis zum Dach.
  - Gauben nur auf der Stadtseite. Der Wehrgang endet an den Mauerhäusern.
  - Werte in `building_rules.json` → `cityWall.wallHouse`.
- **Leonberg:** Ring 1167 m, davon 933 m Mauer und 234 m Mauerhäuser (29 Häuser), Zwinger 252 m. 17 Türme,
  2 Tortürme, 4 Pforten im Ring und 1 in der Zwingermauer, 2 Treppen, 8454 Dreiecke, Kollision höchstens 192 je Datei.
  Mauerstücke der Länge 0 an Knicken erzeugen keine flachen Körper mehr (Test).
- **Schloss:** eigenes Modell statt der LoD2-Blöcke, siehe W-E3.
  - Gedeckte Wehrgang-Abschnitte und Uhr- bzw. Wappenfelder an den Tortürmen bleiben spätere Optionen.

### W-E3 Schloss (`gothar-worldgen schloss <ort>`, W6)
Entscheidung Projektinhaber 2026-10-03: ein eigenes, einzeln modelliertes Schloss statt der LoD2-Blöcke, gebaut in
Blender. **Einzige Quelle ist das Blender-Python-Skript**; der Projektinhaber arbeitet nicht mit Blender, sein Feedback
zu den Bildern wird im Skript umgesetzt. Keine fremden Modelle oder Fotos; Vorlage sind nur LoD2, DOP und später die
eigenen Aufnahmen.
- **Dateien:**
  - `tools/worldgen/data/<ort>/schloss.json`: Maße der Bauteile, aus LoD2 vereinfacht (Flügel als Rechtecke mit Grund,
    Traufe, First), Gelände je Flügelseite (DGM), Treppenturm, Erker, Portal, Schornsteine, ersetzte Gebäude-IDs.
  - `tools/worldgen/blender/schloss/schloss_geometry.py`: Geometrie aus benannten Bauteilen, reines Python (läuft in
    Blender und in den Tests).
  - `tools/worldgen/blender/schloss/build_schloss.py`: Blender-Skript. Es baut daraus ein Objekt mit Paletten-
    Materialien und `COL_HULL_`-Objekte, exportiert die `.glb` und speichert die `.blend`.
  - Ausgabe: `assets/source/worlds/<ort>/handmade/schloss/schloss.glb` (versioniert, nur bei echter Änderung neu
    einchecken); `generated/schloss/schloss.blend` ist ein reproduzierbares Nebenprodukt (nicht versioniert).
  - `tools/worldgen/data/<ort>/handmade.json`: Liste der Handmodelle (Schlüssel, Mesh, Position, ersetzte IDs,
    Grundflächen für die Stadtmauer); der Befehl `schloss` schreibt den Eintrag.
- **Bauteile** (eigene Interpretation, Renaissance um 1700): Putzbau mit Sockel, Eckquadern, Gesimsen und Kranzgesims
  aus Stein; Fensterreihen mit Steingewänden, je Seite nach dem Gelände; Ziergiebel (gestuft, Voluten, Obelisken) an
  den markierten Giebeln; achteckiger Treppenturm mit welscher Haube und Laterne; Erker; Portal mit Verdachung;
  Gauben mit Fenster; Schornsteine. Das Schlossdach bleibt ohne Moos (gepflegter Bau; Hinweis Koordinator).
- **Garten** (Feedback Projektinhaber): der Pomeranzengarten als Parterre, Lage aus dem DOP (`garden` in schloss.json):
  zwei Hälften mit je 4 × 2 Rasenbeeten, Kieswegen (1,6 m) und einem Randweg, niedrige Buchs-Einfassungen (0,45 m,
  Palette `hedge`), in der Mitte zunächst ein einfaches Brunnenbecken (Palette `water`), inzwischen durch den Obeliskbrunnen des Projektinhabers ersetzt (W-E5). Der Boden ist eine an das DGM angepasste Ebene.
  - Splat: Der Befehl `schloss` schreibt die Flächen nach `handmade.json` (`splat.gravel`, `splat.lawn`);
    `export-terrain` malt Kies über das Parterre, lässt die Beete Wiese und behandelt OSM-Gartenflächen, die ein
    Parterre enthalten, als Rasen statt als Acker.
- **Budget** (mit engine abgestimmt): ≤ 15 Tsd. Dreiecke, keine LOD-Stufen (Mesh-LOD für Vobs frühestens M17,
  Namensregel dann `_lod1`). Kollision: ein geschlossener konvexer Körper je Bauteil und einer für den Turm.
- **Einbindung:** Die LoD2-Gebäude bekommen Overrides `keep: false`; der Assembler setzt `HANDMADE_<KEY>` unter
  `WORLDGEN_HANDMADE`, Kategorie `gameplay` (Landmarke), VobId `handmade:<key>`. `citywall` behandelt die Grundflächen
  aus `handmade.json` wie Häuser.
- **Leonberg:** Hauptbau 76 × 14 m (Traufe 14,6 m, First 25,5 m über Grund), Verbindungsbau, Ostflügel 43 × 20 m mit
  sehr steilem Dach, Pomeranzengarten; 8212 Dreiecke, 4 Kollisionskörper.

### W-E4 Marktbrunnen (`gothar-worldgen marktbrunnen <ort>`, W6)
Entscheidung Koordinator im Auftrag des Projektinhabers (2026-10-03): Das Modell, das der Projektinhaber selbst aus
seinem Foto erstellt hat (mit Claude Design), wird übernommen und an Leonberg um 1700 angepasst.
- **Quelle und Lizenz:** Die Quelle ist `tools/worldgen/data/<ort>/marktbrunnen/marktbrunnen_quelle.glb`. Sie ist
  versioniert und bleibt unverändert.
  - Maßstab und Ursprung passen: Boden-Mitte, y oben; der Ursprung ist der Welt-Ursprung, der Marktbrunnen.
  - In `assets/LICENSES.md` steht sie als „eigene Arbeit des Projektinhabers“. Das Foto liegt nicht im Repo.
- **Bearbeitung nur per Skript:** `tools/worldgen/blender/marktbrunnen/build_marktbrunnen.py` liest
  `data/<ort>/marktbrunnen.json`.
  1. Entfernen nach Knotennamen (`remove`): Koniferen, Büsche und die fein gebauten alten Röhren.
  2. Reduzieren (`decimate`, Collapse): Ritterfigur, Säulenschaft und Voluten.
  3. Materialien (`materials`): Granit und Sandstein werden zur Palette `stone`, Beckenwasser wird `water`. Grün,
     Salbei, Gold und Eisen bleiben die Farben des Projektinhabers.
  4. Alles wird zu einem Objekt zusammengefügt. Dazu kommen 4 einfache Brunnenröhren an der Säule mit Strahl ins
     Becken (`spouts`, Materialien `bronze_pipe`/`water_jet`) und die `COL_HULL_`-Körper (`collision`).
  - Röhren und Kollision berechnet `marktbrunnen_geometry.py` in reinem Python; es läuft in Blender und in den Tests.
- **Ausgabe und Einbindung:**
  - Ausgabe: `assets/source/worlds/<ort>/handmade/marktbrunnen/marktbrunnen.glb` (versioniert, nur bei echter Änderung
    neu einchecken).
  - Eintrag in `handmade.json`: am Ursprung, Höhe = tiefster Boden unter der unteren Stufe minus `sinkM`, Grundfläche
    = untere Stufe.
  - Der Assembler setzt `HANDMADE_MARKTBRUNNEN` (Kategorie `gameplay`, stabile VobId `handmade:marktbrunnen`).
  - Ein LoD2-Gebäude gibt es dort nicht, und die OSM-Fläche „Marktbrunnen“ wird nicht verwendet.
- **Budget:** etwa 8–10 Tsd. Dreiecke, keine LOD-Stufe (Mesh-LOD für Vobs frühestens M17, wie beim Schloss).
  Kollision etwa 200 Dreiecke: 2 Stufen (je 0,18 m, begehbar), 8 Trogwände, Pflanzschale, Säule mit Figur.
- **Leonberg:** 22 360 → 8636 Dreiecke, 12 Kollisionskörper (208 Dreiecke).
  - Stufen r 3,56/3,06 m, Trogrand 1,28 m, Wasserspiegel 1,08 m, Figur bis 6,1 m.
  - Der Platz fällt unter dem Brunnen um 0,26 m; talseitig stehen die Stufen frei.

### W-E5 Pomeranzengarten (`gothar-worldgen garten <ort>`, W6)
Entscheidung Koordinator im Auftrag des Projektinhabers (2026-10-03): drei eigene Modelle des Projektinhabers (Claude
Design) bilden den Garten.
- Geländer mit vier Eckpavillons.
- Obeliskbrunnen in der Mitte.
- Zwei kleine Gartenbrunnen, ein Modell, zweimal eingesetzt.
- Wie beim Marktbrunnen gilt: Die Quellen liegen unverändert unter `tools/worldgen/data/<ort>/<schlüssel>/`, bearbeitet
  wird nur per Skript, in `assets/LICENSES.md` stehen sie als eigene Arbeit des Projektinhabers.
- **Werkzeug für Modelle des Projektinhabers:** `gothar_worldgen/owner_models.py` mit `data/<ort>/<schlüssel>.json`
  (Format `gothar-owner-model`) und das allgemeine Blender-Skript `blender/owner_models/build_owner_model.py`.
  - Entfernen und Reduzieren nach Knotennamen; geteilte Meshes werden einmal reduziert.
  - `thinFaces`: Bei dünnen Teilen wie Latten bleiben nur die großen Flächen.
  - Materialien auf die Palette, wo sinnvoll; die übrigen Farben des Projektinhabers bleiben.
  - Kollision aus Knotengruppen (`box` je Instanz, `prism`), berechnet in Python und getestet ohne Blender.
  - `shear` legt Zäune auf ein Gefälle (die Senkrechten bleiben senkrecht); `rigid`-Gruppen wie Pavillons heben sich als
    Ganzes und bleiben waagrecht.
- **Parterre nach dem Geländer:** Das Geländer gibt die Maße vor.
  - Zwei Hälften je 28 × 16,5 m, dazwischen ein Mittelfeld von 10 m mit dem Obeliskbrunnen.
  - Tore in allen vier Achsen jeder Hälfte; sie liegen auf den Mittelwegen.
  - Das Schloss-Skript baut die Beete danach (`schloss.json` garden: `parts`, `t`). In der Mitte jeder Hälfte schneidet
    es einen Rundplatz (`plazaR` 2,6 m) für die Gartenbrunnen; Beete sind dafür Polygone, die Hecken folgen jeder Kante.
  - Das einfache Becken aus #100 entfällt.
- **Lage:** Der Garten hat einen eigenen Rahmen (Mitte und `rotDeg` gegen den Hauptbau). Angepasst ist er an die vier
  LoD2-Eckpavillons DEBW_001000623WX/WY/WZ/X0; deren Overrides `keep: false` ersetzen sie durch die Pavillons des
  Geländers.
  - Gedreht ist der Garten um 5,05° gegen den Hauptbau, die Mitte liegt 1,15 m weiter zum Schloss.
  - Rest je Ecke 1,8 m: Das Geländer misst 67 × 17,5 m, die echten Pavillons stehen 70 × 15,7 m auseinander.
  - Die Zwingermauer (OSM) läuft 0,7 m am südwestlichen Pavillon vorbei; der Garten liegt an ihr.
- **Terrassen** (Bodenregel, Entscheidung Projektinhaber 2026-10-03, ersetzt die schräge Gartenebene): Der Garten liegt
  wie ein Barockgarten auf drei waagrechten Terrassen, `gothar_worldgen/garden_terraces.py`; `gothar-worldgen schloss`
  plant sie bei jedem Lauf aus dem DGM.
  - Westhälfte, Mittelfeld und Osthälfte, je auf dem DGM-Median darin (Leonberg −9,30 / −8,35 / −7,50 m).
  - **Stützmauern** an den Außenkanten als 3 m breite Steinsimse auf der höheren Seite, oben bündig mit ihr: innen, wo
    das Gelände außen abfällt (Süden, Westende), außen, wo es ansteigt (Norden, Ostende). Die Heightmap (1-m-Zellen,
    gegen den Garten um 35° gedreht) kann nicht springen, sie fällt über bis zu 1,4 m; der Sims deckt diesen Hang.
  - Zwischen den Terrassen ist nur Platz für 1,5 m (Obeliskbrunnen, Beete). Dort bleibt rechnerisch höchstens 18 % der
    Stufe (rund 17 cm) als Hang sichtbar.
  - **Treppen** an den Toren des Geländers (Norden und Außenenden), mit einem 3 m langen Absatz auf dem Sims. Zwischen
    den Terrassen gibt es je einen Absatz am Tor und eine schmale Seitentreppe an der Mauer, neben dem Obeliskbrunnen.
  - **Pads:** `export-terrain` setzt die Heightmap auf jede Terrasse und schiebt mit Streifen (`clampBelow`) den Hang der
    Zellen unter die Simse (`gothar_worldgen/export/pads.py`). Nach dem Glätten der Wege wird das wiederholt.
  - Das Geländer steht auf dem Mittelfeld; seine Hälften (`lifts`) und die Gartenbrunnen stehen auf ihrer Terrasse.
  - `schloss_built.json` neben `schloss.glb` (versioniert): die Spezifikation mit dem gemessenen Boden, aus der das
    Modell gebaut ist.
- **Budgets** (Dreiecke je gezeichnetem Knoten; die Quellen teilen Meshes):

  | Modell | vorher | nachher | Kollision |
  |---|---|---|---|
  | Geländer | 116 592 | 12 500 | 20 Kästen, 240 Dreiecke, Tore frei |
  | Obeliskbrunnen | 70 256 | 7928 | 4 Prismen |
  | Gartenbrunnen | 13 516 | 4008 je Brunnen | 3 Prismen |

- **Vobs:** `HANDMADE_GARTEN_GELAENDER`, `HANDMADE_OBELISKBRUNNEN`, `HANDMADE_GARTENBRUNNEN_W` und `_O`, jeweils mit
  `rot`, Kategorie `gameplay` und stabiler VobId.

### W-E6 Stadtkirche (`gothar-worldgen kirche <ort>`, W6)
Entscheidung Koordinator im Auftrag des Projektinhabers (2026-10-03): Das eigene Modell des Projektinhabers (Claude
Design) ersetzt das geschützte LoD2-Gebäude DEBW_00100061Zjs (Override `keep: false`). Bearbeitet wird es mit dem
Werkzeug aus W-E5 (`data/<ort>/kirche.json`); die Quelle liegt unverändert unter `data/<ort>/kirche/`.
- **Reduziert:** 48 208 → 14 802 Dreiecke. Gekürzt werden Maßwerk, Fensterrahmen und Glas (Regeln mit `*`), Uhrring,
  Galeriebaluster, Fialen und Turmquader; Turm und Silhouette bleiben.
- **Palette:** `sandstone` → `stone`, `plaster` → `plaster_white`; die übrigen Farben des Projektinhabers bleiben.
- **Kollision:** 17 Körper, 220 Dreiecke.
  - Kästen für Schiff, Chor, Seitenschiff, Vorhalle und Turm, ein Prisma für das Oktogon.
  - Ein Kasten je Strebepfeiler.
- **Lage** (`placement` in kirche.json): Der Grundriss des Modells (Wände unter 1,5 m) ist auf den LoD2-Grundriss
  gedreht und mittig gesetzt (Entscheidung K1: beide Enden je rund 5 m kürzer als das LoD2-Gebäude, das 50,4 × 21,6 m
  misst; das Modell misst 39,3 × 18,9 m).
  - **Höhe nach der Bodenregel:** Der Boden liegt auf dem höchsten Gelände unter dem Grundriss. Ein Steinfundament
    (`foundation` in kirche.json: Grundriss der Wandknoten) reicht 0,3 m unter den tiefsten Punkt.
  - Gläser bleiben, das Dach bleibt orange (Entscheidungen des Projektinhabers).
- **Anachronismen für 1700** (nur gemeldet, nichts entfernt): verglaste Türen (`door_aisle_glass`,
  `porch_arch_glass`, `tower_door_glass`), verglaste Schallöffnungen im Glockengeschoss (`belfry_opening_glass`,
  `bell_louvre_glass`) und verglaste Turmschlitze. Die Turmuhr mit Minutenzeiger ist um 1700 möglich.

### Bodenregel für handgemachte Modelle (W6)
Entscheidung Projektinhaber (2026-10-03): Kein sichtbarer Teil eines handgemachten Modells darf im Gelände stecken.
- Ein Modell steht auf dem höchsten Gelände unter seinem Grundriss. Talseitig gibt es Sockel oder Fundament aus Stein,
  oder die Heightmap wird unter dem Grundriss geebnet (Pad).
  - **Schloss:** `schloss` misst den Boden jedes Flügels frisch aus dem DGM, auch an den Flügelenden (je Seite der
    höchste Wert 0,5–1,5 m vor der Wand). Der Erker am Hauptbau sitzt mindestens 1,6 m über dem Gelände.
  - **Marktbrunnen:** auf dem höchsten Gelände unter der unteren Stufe. Ein Pad ebnet den Platz darunter und läuft über
    3 m (`padFadeM`) in den Platz aus.
  - **Garten:** Terrassen, siehe W-E5.
  - **Kirche:** Fundament, siehe W-E6.
- **Prüfung** `gothar_worldgen/qa/grounding.py`: Sie liest die Welt so, wie die Engine sie lädt (Render-Meshes der
  `HANDMADE_*`-Vobs, Heightmap).
  - Ein Punkt gilt als versunken, wenn das Gelände mehr als 0,1 m über ihm liegt. Ausgenommen ist das unterste
    0,35-m-Band (Fuß).
  - Beim Schloss sind Stein (Sockel, Stützmauern) und Hecken ausgenommen.
  - Ausgabe in `begehung` („hand-made models: …“); ein Test hält die erzeugte Welt bei 0 versunkenen Punkten.

### W-F Ausstattung & Vegetation
Regelbasiertes Verteilen von Requisiten (Fässer, Karren, Zäune, Holzstapel, Misthaufen, Marktstände)
und Vegetation (Bäume, Büsche, Gras) über Masken; Feinarbeit mit Pinseln im Editor (M16).

- **Mob-Modelle umgesetzt (M8-Vertrag, characters-pipeline.md §3.1, `mobs.toml` v1):** `gothar-worldgen mobs` →
  `assets/source/mobs/{chest,anvil,bed,door}.glb`, Texturen daneben unter `textures/` (ortsunabhängig, 512²,
  relativ verlinkt; Eiche, Eisen, Stroh, Wolle aus dem Textur-Werkzeug). Werkzeug `gothar_worldgen/mobs.py`.
  - Ursprung am Boden, Vorderseite +Z, Meter.
  - Truhe 0,9 × 0,6 × 0,6 m, Vorderseite z = +0,3, Korpus 0,5 m. Der Deckel ist der Knoten `MOB_LID` mit Pivot am
    hinteren Scharnier (0; 0,5; −0,3); sein Kollisionskörper hängt als Kind daran und dreht mit.
  - Amboss: Arbeitsfläche 0,8 m, Horn zu +X.
  - Bett 2,0 × 0,9 m entlang X, Liegefläche 0,45 m, Kopfende bei −X (bestätigt von figuren).
  - Tür: nur das Türblatt, 1,0 × 2,0 m entlang +X ab der Angel im Ursprung, Klinke beidseitig auf 1,0 m.
  - Bank (M9): Brettbank 1,5 × 0,35 m, Sitzhöhe 0,45 m, Ursprung mittig am Boden (Testmaße von figuren).
  - Tisch (W7, Mob-Typ `table` von engine): Bocktisch, Platte 1,6 × 0,8 m in 0,75 m Höhe, Ursprung mittig am
    Boden; Sitzplätze an beiden Längsseiten, die Bänke stehen mit ihrer Mitte ca. 0,62 m neben der Tischachse.
  - Je 1–2 Kollisionskästen; ca. 70–300 Dreiecke je Modell (Budget 1500).
  - Platzierung in Leonberg folgt mit Ausstattung bzw. Innenräumen.

### W-G Welt-Assembler & Wegnetz-Vorschlag
- Terrain + Gebäude + Straßen + Ausstattung → `.g7world` (Zellen), Kollision, Validierung, Credits.
- **Umgesetzt (W3 Teil 1):** `gothar-worldgen assemble <ort>` erzeugt `assets/source/worlds/<ort>/<ort>.g7world`
  (versioniert) aus dem `terrain`-Block von `<ort>_terrain.g7world` und `buildings_index.json`.
  - **Aufbau:** Wurzelgruppe `WORLDGEN_BUILDINGS` (`empty`), darunter je 64-m-Zelle eine Gruppe `CELL_<i>_<j>`
    (negative Indizes als `M`) und darin je Gebäude ein `mesh`-Vob `BLD_<lod2-id>`. Die Gruppen liegen im
    Ursprung, `pos` ist also auch absolut.
  - **Startpunkte** `START_MARKTPLATZ` und `START_UEBERSICHT` (world.md); die Füße stehen auf der Heightmap.
  - Leonberg: 1029 Vobs (1 + 121 Gruppen + 905 Häuser + 2 Startpunkte), Engine ohne Warnungen.
  - **VobIds** (ADR 0005): `tools/worldgen/data/<ort>/vob_ids.json` (versioniert) ordnet jedem Schlüssel
    (`building:<id>`, `group:cell_…`, `start:<name>`) eine feste ID zu.
    - Schlüssel werden nie gelöscht, IDs nie wiederverwendet.
    - Neue IDs liegen über dem größten Wert aus `nextVobId` der Welt, `nextVobId` der Datei und der höchsten
      vorhandenen ID. So gibt es keine Kollision mit IDs, die der Editor vergeben hat.
    - Ein erneuter Lauf mit gleichen Daten ändert nichts.
- **Regel für Handarbeit aus dem Editor (M4):** Der Assembler überschreibt keine Editor-Arbeit.
  1. Er **besitzt nur die Vobs, deren ID in `vob_ids.json` steht**. Alle anderen Vobs (im Editor angelegt), ihre
     Eltern-Verknüpfungen und weitere Schlüssel der Weltdatei bleiben unverändert.
  2. Eigene Gruppen- und Gebäude-Vobs spiegeln die Daten und werden bei jedem Lauf neu geschrieben. Ausnahme:
     Gebäude mit `"locked": true` im Override. Deren Vob bleibt genau so, wie er in der Datei steht, und ihre
     `.glb` wird nicht neu erzeugt.
     Wer ein Haus im Editor verschieben oder ersetzen will, setzt also `locked` (Annotations-Oberfläche,
     Blender-Add-on oder Datei). Soll ein erzeugtes Haus ganz weg, gilt `keep: false`.
  3. Startpunkte werden **einmal** angelegt und danach nie neu geschrieben. Ein im Editor gelöschter Startpunkt
     wird nicht wieder angelegt.
  4. Eigene Vobs, deren Quelle verschwunden ist, werden entfernt; ihre ID bleibt in `vob_ids.json` und wird nie
     wieder vergeben. Eine eigene Gruppe, an der noch Editor-Vobs hängen, bleibt erhalten.
  5. Der `terrain`-Block wird jedes Mal aus `<ort>_terrain.g7world` übernommen.
- **Wegnetz-Vorschlag** aus Straßenachsen (`gothar-worldgen waynet <ort>` nach `assemble`,
  `gothar_worldgen/waynet/`, Vertrag world.md „Wegnetz“); schreibt den `waynet`-Block in die Welt und
  `generated/waynet_report.json`:
  - **Punkte** `WP_LEO_<STRASSE>_<NNN>` entlang jeder begehbaren OSM-Achse im Kern (höchstens 12 m
    Abstand, dazu Knicke über 30°, Kreuzungen, Enden); ohne Tiefgaragen (`tunnel` mit `layer` < 0)
    und Schnellstraßen. Jeder Punkt liegt auf freiem Boden (Radius der Figur + 0,2 m Abstand zu
    Kollisionskörpern), sonst bis 3 m quer zur Achse verschoben oder ausgelassen.
  - **Türpunkte** `WP_LEO_<STRASSE>_<KÜRZEL>` (Kürzel aus der LoD2-ID) 0,6 m vor der Tür bzw. am
    Fuß von Treppe oder Abgang, mit `dir` zur Tür; angebunden an den nächsten erreichbaren Punkt
    oder an eine Kante (die dafür geteilt wird). Häuser ohne Tür (`blocked`, siehe Türwand) führt der
    Bericht als `doorsWithoutAccess`; steht trotzdem etwas vor einer Tür, `doorsUnusable`.
  - **Kanten** nur, wenn die Gerade frei von Kollisionskörpern ist und **kein 0,5-m-Stück steiler
    als 35°** (Autopilot: die Figur bleibt an kurzen steilen Stücken weit unter der Grenze des
    Controllers hängen; Treppenwege sind im Gelände nur Rampen und nicht ausgenommen); sonst ein
    Umweg-Punkt daneben oder die Kante entfällt (Bericht `droppedEdges`, steile Stücke als Liste
    `steep`). Inseln werden untereinander und ans Hauptnetz gebunden, wo eine freie, begehbare
    Gerade bis 30 m sie erreicht; ausdrücklich verbunden werden die Gartentreppen und -tore des
    Schlosses und die Zwinger-Pforte davor (Treppenrampen sind Kollisionskörper).
  - **Freepoints:** `FP_SIT` auf dem Brunnenrand (nach außen), `FP_DRINK` an den Brunnen,
    `FP_SMALLTALK` paarweise und `FP_ROAM` auf dem Marktplatz, `FP_STAND` innen an den Toren,
    `FP_WATER` (Pflanzen gießen) an den Beeten des Pomeranzengartens.
  - **Eigentum:** alles Erzeugte mit `owner: "worldgen"`; Hand-Punkte, -Freepoints und -Kanten
    bleiben (ihre Namen vergibt der Generator nicht, er bindet sie an). Dauerhafte Korrekturen in
    `data/leonberg/waynet.json` (`remove`, `add`).
  - **Schreiben** im Layout der Engine (eine Zeile je Eintrag, sortiert, Zahlen wie die Engine sie
    rundet; Richtungen als Fixpunkt ihres Normierens), Lauf → `assemble` → Lauf ist byte-gleich.
  - **Prüfung:** Bericht mit Komponenten (Inseln mit Grund); Autopilot über 30 zufällige Wege A→B
    (Spieler mit `--walk` entlang der A*-Pfade, NPCs mit `npc_goto` über die Pfadsuche der Engine).

### W7 Gebäudenutzungen (Vorschlag, Plan freigegeben 2026-10-04)
- `gothar-worldgen uses-suggest <ort>` (nach `assemble`): schlägt ca. 30 Häuser mit mittelalterlicher Nutzung vor
  (Gasthaus, Bäcker, Metzger, Händler, Schmiede, Werkstatt, Bader, Kräuterhändler, Amtshaus, Wache, Pfarrhaus,
  Bauernhof, Wohnhaus) – `gothar_worldgen/uses/suggest.py`.
- Quellen: heutige Läden, Gaststätten und Handwerk aus OSM (`amenity`, `shop`, `craft`, `tourism`, `office`; ein
  Punkt bis 4 m am Grundriss gehört zum Haus) und die ALKIS-Gebäudefunktion (z. B. 2081 Gaststätte, 2120 Werkstatt,
  2050 Geschäftsgebäude, 2721/2724 Scheune/Stall, 3012 Rathaus). Gewicht: OSM 3 (Dienstleistungen und Cafés 2,5),
  ALKIS 2, dazu bis 1 für die Nähe zu Markt, Kirche und Toren; Läden und Handwerk nur bis 250 m um Markt und Kirche.
- Höchstzahlen je Nutzung (`USES`), eine Nutzung je Haus. Ohne Hinweis aus den Daten: die Schmiede am Tor, je Tor
  eine Wache (die heutige Polizei liegt außerhalb), das Pfarrhaus an der Kirche; der Rest Wohnhäuser nahe Markt und
  Kirche mit Bewohnerzahl (ca. ein Bewohner je 60 m² Geschossfläche).
- Ausgabe (nicht versioniert): `generated/uses_suggest.json`, Draufsicht `DATA_ROOT/review/w7-nutzungen/vorschlag.png`
  (Nummer, Kürzel mit Groß-/Kleinschreibung, Nutzung) und Tabelle `vorschlag.md` mit dem Grund je Haus. Die echten
  Geschäftsnamen stehen nur dort als Grund; im Spiel tragen die Häuser eigene, mittelalterliche Namen (keine echten,
  keine an Gothic angelehnten).
- Die Auswahl des Koordinators wird `data/<ort>/uses.json` (PR B: Routinen-Orte, Freepoints, Mobs).
- **`data/<ort>/uses.json`** (versioniert): `houses` mit `id`, `use`, optional `trade` (Gewerk, z. B. `schuster`;
  bestimmt den Namensteil), `name` (eigener mittelalterlicher Name), `residents`, `inside` (Kandidat für die begehbaren
  Häuser) und `owner` (Npc-Instanz bzw. Gilde, setzen engine/figuren mit den Routinen); `uses` je Nutzung mit
  `freepoints` (`TYP:Anzahl`, Typen nach world.md) und `mobs` (`typ:Anzahl`, Typen aus `data/mobs.toml`). Fehler mit
  Datei und Eintrag (unbekannte Nutzung, doppelte ID, falsches `TYP:N`).
- **Routinen-Orte** (`gothar_worldgen/uses/places.py`), von `assemble` geplant (zweiter Durchgang auf der fertigen
  Welt): je Haus ein Routinen-Wegpunkt `WP_LEO_<NUTZUNG bzw. GEWERK>_<KÜRZEL>` 1,8 m vor der Tür, Mobs mit dem Rücken
  zur Wand neben der Tür (nicht im Türbereich, Ecken und Slot frei, ein Gang davor bleibt frei, mindestens halbe
  Breite + 1,2 m Abstand zur Achse eines begehbaren Wegs), Freepoints nach Typ in Reihen vor der Fassade
  (`FP_<TYP>_LEO_<…>_<KÜRZEL>_<NN>`; REPAIR am Slot des Ambosses, SMALLTALK paarweise einander zugewandt), alles auf
  freiem Boden, höchstens 0,6 m über bzw. unter dem Routinen-Punkt, 0,9 m Abstand untereinander. Was nicht passt,
  steht im Bericht (`failed`). `assemble` schreibt die Mobs als `mob`-Vobs in die Gruppe `WORLDGEN_USES` (stabile IDs
  `use:<name>`), dazu `generated/uses_places.json` und `generated/uses_table.md`; `waynet` übernimmt Routinen-Punkte
  (angebunden wie Türpunkte, Bericht `usesUnconnected`) und Freepoints. Die Begehung und das Wegnetz behandeln Mobs
  als Hindernisse.
- Übergabe an engine und figuren: Tabelle der Routinen-Orte in `docs/design/leonberg-routinen-orte.md`.

### W7 Begehbare Häuser (C1: Raum, Tür, Kollision; Plan freigegeben 2026-10-04)
- Häuser mit `inside: true` in `uses.json` (Leonberg: Gasthaus ZhE, Schmiede ZnP, Krämer Zkx, Wohnhäuser Zl2-T2 und
  ZjV) bekommen im Baukörper mit der Tür das **Erdgeschoss als einen Raum** (`medieval._room`, nur lod0; lod1/lod2
  bleiben außen geschlossen): Grundriss um `interior.wallM` (0,3 m) eingerückt, Innenwände verputzt, Boden aus
  Holz bzw. Stein (`interior.stoneFloors`: Schmiede, Gasthaus), Decke `interior.ceilingM` unter dem 1. Obergeschoss
  mit Balken quer zur kurzen Seite. Materialien aus der vorhandenen Palette (höchstens 16, die Engine bündelt nach
  Material): Wände `plaster_white`, Boden/Decke `timber_dark`, Steinboden `stone`. Mindestgröße 8 m², lichte Höhe
  2,4 m, sonst Hinweis „room skipped“.
- **Tür:** echte Öffnung mit Laibung über die ganze Wandstärke (keine Füllung); `assemble` setzt einen **Tür-Mob**
  (`mobs/door.glb`, Name `MOB_LEO_TUER_<KÜRZEL>`) an die Angel in der Laibung, Vorderseite nach außen.
  `interior.doorsOpen` (Vorgabe `true`, Entscheidung Koordinator): offen, d. h. 90° in den Raum gedreht, bis die
  Engine NPCs Türen öffnen lässt; dann `false`.
- **Kollision:** der Raum samt Türdurchgang wird aus dem Hüllkörper geschnitten (wie bei Durchgängen: Körper über
  der Decke, Wandprismen daneben), dazu eine feste Bodenplatte, wenn der Boden über der Basis liegt.
- **Gelände unter dem Raum:** `export-terrain` senkt die Heightmap unter jedem Raum (Index `interior.ring`, um eine
  Zelle vergrößert) auf 2 cm unter den Boden, nur absenkend (`export/pads.py` `room_pads`); sonst ragt am Hang das
  Gelände bergseitig durch den Boden (vorher bis 1,1 m, Schmiede). Außen läuft die Senke über 1,5 m ins Gelände aus
  (sanfte Mulde am Wandfuß statt einer Stufe). Löcher im Gelände (`holes`) wurden verworfen: bei 1-m-Zellen bliebe
  das Gelände in den Zellen entlang der Wände im Raum stehen.
- **Budget:** Innenraum-Dreiecke (Rollen `room_*`) zählen nicht gegen das Hausbudget; außen bleibt das Fachwerk.
- Der Index-Eintrag bekommt `interior` (Boden, Decke, Raum-Ring, Tür mit Angeln, Achse, Normale, Maßen); daraus
  baut C2 Innen-Mobs, Möbel, Freepoints, Licht, Wegnetz und `trigger.owner`.
- **C2 – Einrichtung** (`gothar_worldgen/uses/inside.py`, von `assemble` geplant; Angaben je Nutzung in
  `uses.<nutzung>.inside`: `mobs` `typ:N` bzw. `bed:R` = Bewohner, höchstens 3; `freepoints`; `hearth`):
  Betten und Truhen mit dem Rücken zur Wand (Slot frei, in den Raum blickend), ein Tisch (`mobs/table.glb`) mit je
  einer Bank an beiden Längsseiten (Mitte ±0,62 m, Vorderseite vom Tisch weg) in der freien Fläche nahe der
  Raummitte, eine Feuerstelle (`props/hearth.glb`, kein Mob: gemauerter Herd 1,2 × 0,9 × 0,45 m mit Rückwand,
  glühender Glut und Flammen über `emissiveFactor`, Rauchfang 1,9–2,5 m; Kollision nur der Block) mit warmem,
  flackerndem `light`-Vob 0,9 m über dem Boden. Der Herd wird zuerst gesetzt, an die Wandstelle, deren Vorderseite am
  meisten zur Tür zeigt und deren Feuerstelle vom Raum-Wegpunkt aus gerade erreichbar ist; die Gasse dorthin bleibt
  frei (beim Eintreten sichtbar). In großen Räumen (> 60 m²) ein zweites Licht über dem Tisch bzw. der Mitte. Der Weg von der Tür zur
  Raummitte (1,3 m breit), 1,4 m hinter der Tür und der Schwenkbereich des Türblatts bleiben frei; das offene
  Türblatt zählt beim Prüfen der Wege als Hindernis. Freepoints im Raum: CAMPFIRE vor der Feuerstelle,
  STAND am Slot einer Truhe (Ladentheke), LEAN an Wänden, SMALLTALK paarweise; jeder vom Raum-Wegpunkt aus in
  gerader Linie erreichbar. Wegpunkte: `WP_…_VOR` 0,9 m vor der Tür auf dem Gelände (verbunden mit dem
  Routinen-Wegpunkt: der Weg geht gerade durch die Öffnung statt schräg an der Laibung vorbei), `WP_…_TUER` in der
  Mitte der Türöffnung (verbunden mit `…_VOR`) und `WP_…_INNEN` 1,2 m im Raum (verbunden mit `…_TUER`): die Figur richtet sich vor dem schmalen Durchgang aus.
  Innen-Punkte stehen auf dem Raumboden (`y`), nicht auf dem Gelände darunter. Mit `owner` in `uses.json` kommt ein
  Box-Trigger über den Raum (`trigger.owner`, privater Bereich); ohne owner keiner. Ragt ein fremder Körper in den Raum (Schmiede
  ZnP: die Stadtmauer, an die sie gebaut ist), endet der Raum für die Einrichtung an dessen Wand; der Herd steht dort an
  der Mauer. `assemble` schreibt allgemeine
  Vob-Angaben (mob, mesh, light, trigger) in die Gruppe `WORLDGEN_USES`.

### Vorhandene Werkzeuge (kein Eigenbau)
Blender (+ Add-on BlenderGIS zum Gegenprüfen), COLMAP / RealityScan / Postshot (Photogrammetrie bzw.
Gaussian Splatting für 3D-Referenzen einzelner Objekte wie Brunnen, Treppen, Mauerreste),
Material Maker / Substance für Trim-Sheets, QGIS zum Sichten der Geodaten.

## 6. Aufnahme-Leitfaden Insta360

- Früh morgens (Sonntag), bedeckter Himmel ideal (weiche Schatten, wenig Leute/Autos).
- Kamera am Selfie-Stick **über Kopfhöhe** (ca. 2,5–3 m) – weniger Verdeckung durch Autos, bessere Sicht auf Obergeschosse.
- GPS aktiv (App bzw. GPS-Fernbedienung), Videomodus mit höchster Auflösung oder Intervall-Fotos alle 1–2 m.
- Zu Beginn jeder Aufnahme **10–20 s stehen bleiben**, dann losgehen. Zwischendurch an Ecken kurz anhalten.
  Das hilft dem automatischen Zeitabgleich zwischen Video und GPS (`facade frames`).
- Langsam und gleichmäßig gehen, jede Gasse in beide Richtungen, Plätze im Raster ablaufen.
- Zusätzlich normale Fotos von Details (Fachwerk-Knoten, Türen, Pflaster, Brunnen).
- Protokoll: Datum, Route, Besonderheiten → `capture/<datum>/notes.md`.

## 7. Mittelalterlicher „Rückbau“ – Gestaltungsregeln

**Entscheidung des Projektinhabers (2026-10-03):** Große neuere Gebäude werden **verkleinert und durch
Fachwerkhäuser ersetzt** – weder ganz entfernt noch als großes Haus mit Fachwerk. Die Zahlenwerte hat der
Koordinator im Auftrag des Projektinhabers festgelegt; sie sind nach den ersten Bildern justierbar.
Sie stehen in `tools/worldgen/data/building_rules.json` unter `rueckbau`.

Umgesetzt in `buildings/rueckbau.py` (`gothar-worldgen buildings <ort> --mode medieval`; `massing` bleibt echtes
LoD2, mit `--rueckbau` als Vorschau):
- **Auswahl:** Gebäude im Kern mit Grundfläche > 200 m², längster Seite > 22 m oder Traufhöhe > 12 m.
  Zusätzlich jedes Haus, das im Fachwerk-Modus das Dreiecksbudget sprengt; es wird im selben Lauf geteilt.
- **Ausnahmen:**
  - **Steile historische Dächer** (Entscheidung Koordinator, 2026-10-03): Sattel-, Walm- oder gemischtes Dach mit
    LoD2-Neigung ≥ 45° und Traufe ≤ 12 m. Solche Gebäude werden weder über Fläche oder Länge noch über das Budget
    ersetzt; sie verlieren über die Budget-Stufen nur Fachwerk-Details. Der Bericht listet sie mit Neigung und Traufe.
  - **Rathaus** ALKIS 31001_3012 mit Traufe ≤ 12 m (das Alte Rathaus am Marktplatz). Das große Neue Rathaus
    (Traufe 20 m) läuft durch den Rückbau.
  - **ALKIS 51007** (historische Bauwerke, z. B. 51007_1510 Stadtmauer): nie ersetzt.
  - Gebäude, die eine OSM-Fläche schneiden: Kirche (`place_of_worship`), Schloss (`castle`), Rathaus, Denkmal
    oder `historic`, und Gebäude an einem historischen OSM-Punkt.
  - Die OSM-Stadtmauer (`city_wall`) schützt ein Gebäude nur, wenn sie mindestens 2 m durch den Grundriss läuft
    (gemessen 0,25 m innerhalb). Bloßes Berühren reicht nicht; große Bauten an der Mauer sind meist jünger
    (Entscheidung Koordinator, 2026-10-03).
  - Override `rueckbau: none` und Häuser mit `locked`.
  - `rueckbau: split` erzwingt den Ersatz; `keep: false` entfernt ein Gebäude ganz.
- **Parzellen:** Schnitte senkrecht zur Hauptstraßenseite, 7 m ± 1,5 m breit (deterministisch je Gebäude);
  Reste unter 25 m² gehen an den Nachbarn.
- **Tiefe:** Parzellen tiefer als 14 m werden parallel zur Straße geteilt. Ein Rest ab 8 m Tiefe wird ein
  Hinterhaus (1–2 Geschosse), sonst Hof (kein Gebäude; die Splatmap zeigt dort Matsch).
- **Höhe und Dach:** höchstens Erdgeschoss plus 2 Obergeschosse. Satteldach mit 50° ± 4°, je Parzelle zu 70 %
  giebelständig und zu 30 % traufständig, damit Straßenzüge nicht uniform wirken.
- **IDs:** Die Ersatzhäuser heißen `<lod2-id>-T<n>` (vorne, von links nach rechts von der Straße aus gesehen) und
  `<lod2-id>-H<n>` (Hinterhaus). Sie bekommen neue VobIds; die VobId des Originals bleibt in `vob_ids.json`
  reserviert.
  - Ändert sich die Zahl der Teile, behalten die bestehenden Teile ihre ID.
  - Ersatzhäuser können eigene Overrides haben (`<id>-T<n>.json`, auch `locked`).
- **Bericht:** `generated/rueckbau_report.json` nennt ersetzte Gebäude mit Grund, Zahl der Ersatzhäuser und
  Hoffläche, die Ausnahmen und die Budget-Ersetzungen.
- **Leonberg** (Stand mit Stil, 2026-10-03):
  - 132 Gebäude werden durch 633 Häuser ersetzt, 2448 m² werden Hof.
  - 38 sind geschützt: Schloss, Stadtkirche, Altes Rathaus, 4 Mauerstücke (ALKIS 51007) und 31 Häuser mit
    steilem Dach, darunter die historischen Häuser am Marktplatz.
  - Im Kern stehen 1405 Häuser mit 1,81 Mio. Dreiecken.

Weitere Regeln:

- Alles nach ca. 1700 Erbaute ersetzen oder weglassen; Lücken werden Gärten, Höfe, Ställe, Misthaufen.
- **Stadtmauer** mit Toren ergänzen (Verlauf an historischen Resten und OSM orientieren, frei interpretiert;
  umgesetzt in W-E2).
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

- ~~Genaue Gebietsgrenze (Kern + Umland) und Ursprungspunkt.~~ Festgelegt 2026-10-02: Ursprung Marktbrunnen,
  Kern ±350 m (umfasst Altstadt und Schloss), Umland ±1000 m (`leonberg.toml`).
- Maßstabsfaktoren nach dem Klötzchen-Test.
- Welche realen Bauten bleiben erkennbar (Schloss, Kirche, Marktplatz-Ensemble)?
- Rolle des Ortes in der Geschichte (Lager einer Fraktion? Handelsstadt?) → `docs/design/world.md`.
