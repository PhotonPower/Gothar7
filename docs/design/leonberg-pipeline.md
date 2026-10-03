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
  "footprint": [[x, z], ...], "areaM2": 250.2, "groundY": -0.93, "heightM": 17.57,
  "roof": { "type": "saddle", "alkis": "3100", "eaveY": 9.61, "ridgeY": 16.64,
            "ridgeDir": [1.0, 0.017], "pitchDeg": 32.7 },
  "parts": [ { "id": "UUID_…", "footprint": …, "areaM2": …, "groundY": …, "heightM": …, "roof": { … } } ],
  "warnings": [ "…" ] }
```
- Aufgenommen werden alle Gebäude, deren Grundriss im Gebiet `surroundings` liegt (gemessen an einem
  repräsentativen Punkt). `inCore` markiert die Gebäude der Altstadt.
- Koordinaten sind lokal (x, z) in Metern, auf cm gerundet. Der Ring ist nicht geschlossen und läuft in der
  Draufsicht (Norden oben) gegen den Uhrzeigersinn. Innenhöfe ab 1 m² stehen in `holes`.
- `groundY`, `eaveY` und `ridgeY` sind **absolute lokale Höhen**, im selben System wie das Terrain.
  `eaveY` ist der tiefste Dachpunkt, `ridgeY` der höchste; `heightM` = höchster First − Boden.
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
- Ebenfalls optional ist `locked` (siehe W-C). **Unbekannte Schlüssel bleiben beim Speichern erhalten**, damit neuere
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
- **Gebäude auf dem Gelände (W3 beachten):** Das DGM1 hat am Hang **Stufen entlang von Häuserreihen**. Es sind in den
  Hang gebaute Häuser mit Geländesprung an der Hauswand; aufgefallen ist das in der Engine beim W2-Export (2026-10-03).
  - Insgesamt liegen Steilstellen nicht bevorzugt an Grundrissen (> 45°: 18,6 % nahe Grundrissen bei 35,7 %
    Flächenanteil). Örtlich am Hang aber schon.
  - Folge für Generator und Assembler:
    - `groundY` ist schon der **tiefste** Punkt der LoD2-Bodenfläche; der Baukörper beginnt also talseitig richtig.
      Bergseitig liegt das Gelände höher, die Wand läuft dort ins Gelände (gewollt, sichtbarer Sockel talseitig).
    - Prüfen, ob LoD2-`groundY` und DGM unter dem Grundriss zusammenpassen. Liegt das DGM talseitig tiefer als
      `groundY`, schwebt die Hausecke; dann den Sockel bis zum tiefsten DGM-Punkt verlängern.
    - Dafür beim Import `groundMinY`/`groundMaxY` aus der Heightmap je Gebäude ergänzen.
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

### W-F Ausstattung & Vegetation
Regelbasiertes Verteilen von Requisiten (Fässer, Karren, Zäune, Holzstapel, Misthaufen, Marktstände)
und Vegetation (Bäume, Büsche, Gras) über Masken; Feinarbeit mit Pinseln im Editor (M16).

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
- Zu Beginn jeder Aufnahme **10–20 s stehen bleiben**, dann losgehen. Zwischendurch an Ecken kurz anhalten.
  Das hilft dem automatischen Zeitabgleich zwischen Video und GPS (`facade frames`).
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

- ~~Genaue Gebietsgrenze (Kern + Umland) und Ursprungspunkt.~~ Festgelegt 2026-10-02: Ursprung Marktbrunnen,
  Kern ±350 m (umfasst Altstadt und Schloss), Umland ±1000 m (`leonberg.toml`).
- Maßstabsfaktoren nach dem Klötzchen-Test.
- Welche realen Bauten bleiben erkennbar (Schloss, Kirche, Marktplatz-Ensemble)?
- Rolle des Ortes in der Geschichte (Lager einer Fraktion? Handelsstadt?) → `docs/design/world.md`.
