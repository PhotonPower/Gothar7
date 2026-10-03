# Leonberg – Begehung (W3)

Stand: 2026-10-03 · Sitzung welt · Welt `assets/source/worlds/leonberg/leonberg.g7world` (Klötzchen-Leonberg
mit Regelwerk-Häusern, Stadtmauer, Schloss, Glems/Parksee) auf `main` nach #108.

Die W3-Aufgabe „Maßstabstest: Begehung“ hat zwei Teile:

1. **Statische Begehung** (#110): Die Welt wird so gelesen, wie die Engine sie lädt, also Heightmap mit
   eingegrabenen Gewässern und `COL_`-Körper aller Mesh-Vobs. Sie wird gegen die Maße der Spielfigur geprüft.
2. **Echte Begehung** mit engines Autopilot `gothar --walk` (#109): Die Spielfigur läuft drei Routen ab, siehe
   Abschnitt „Teil 2“ unten.

Maßstabsfragen sind unten als **Entscheidungspunkte E1–E6** gesammelt. Entscheiden muss der Projektinhaber.

## Verfahren

`gothar-worldgen begehung leonberg` (Code: `tools/worldgen/src/gothar_worldgen/qa/begehung.py`, Tests
`test_qa_begehung.py`) schreibt `generated/begehung.json` (nicht versioniert) und braucht etwa 7 s.
- **Spielfigur:**
  - Zylinder r = 0,3 m, Höhe 1,8 m (physics, `Character.hpp`).
  - Aus `assets/source/data/movement.toml`: Stufe 0,4 m, Grenzwinkel 50°, Augenhöhe 1,62 m.
- **Hindernisse:** Jeder `COL_HULL_`-Körper wird auf das Höhenband der Figur geschnitten. Das Band reicht von 0,45 m
  (knapp über einer Stufe) bis 1,8 m über dem Gelände in der Körpermitte. Ergebnis ist eine 2D-Fläche.
  - Niedrige Körper wie Bordsteine oder Stufen fallen damit heraus.
  - In der Altstadt (Kernbereich ±350 m) sind es 2435 Körper.
- **Wege:** alle begehbaren OSM-Wege aus `streets.json`, ohne Tunnel, Brücken und Ebenen ≠ 0.
  - Abgetastet wird alle 1 m, insgesamt 18 831 Punkte, also rund 18,8 km Weg.
- Alles ist 2D und eine Schätzung, keine Physik. Die echte Begehung (Teil 2) bestätigt oder widerlegt die Funde.

## Ergebnisse

### a. Gassenbreiten

Quer zu jedem Weg wird nach beiden Seiten bis zum ersten Hindernis gemessen.

| Befund | Anzahl | Bemerkung |
|---|---|---|
| blockiert (< 0,7 m) | 1 | Pfad bei (−73,8 / −118,1), 0,63 m, auf 4 m Länge, Station `START_BG_GASSE_ENG` |
| eng (0,7–1,2 m) | 2 | derselbe Pfad (0,70 m, 8 m lang); Fußweg bei (1,6 / −88,6) mit 1,12 m, Station `START_BG_GASSE_SCHMAL` |
| Weg läuft durch einen Körper | 63 Abschnitte | 45 durch Häuser, 18 durch die Stadtmauer, siehe E2 |
| Spalten schmaler als die Figur | 62 (262 m²) | Lücken zwischen Häusern, in die man nicht hineinkommt, siehe E4; Station `START_BG_SPALT` |

Fazit: Im Maßstab 1:1 sind Leonbergs Gassen für die Figur fast überall breit genug. Die einzige echte Engstelle ist
ein schmaler Pfad.

### b. Gefälle und Türstufen

- **Wege:** Gemessen wird die Neigung entlang des Weges über 2 m.
  - Zu steil (> 50°): 4 Stellen an drei Pfaden, jeweils 1–2 m lang, die steilste bei
    (−235,6 / −101,9) mit 63°.
  - Steil (35–50°): 22 Stellen, darunter der Törlensweg bis 49° (Station `START_BG_STEIL`).
- **Treppen (OSM `steps`):** Sie sind im DGM Rampen.
  - Von 67 Treppen ist 1 zu steil: 62° auf 8,5 m Höhe bei (−48,9 / 197,9), Station `START_BG_TREPPE`.
  - 7 Treppen sind steil (35–50°).
- **Türen:** Die Türschwelle liegt auf dem Boden des Hauses, das ist die LoD2-Geländehöhe, meist der *tiefste* Punkt.
  Verglichen wird mit dem Gelände 0,6 m vor der vermuteten Türkante. Der Generator setzt die Tür auf die längste
  Straßenkante; die Prüfung nähert das mit „nächste Kante zu einem Weg“ an.
  - 746 Häuser geprüft.
  - **465 Türen liegen im Gelände** (mehr als 0,15 m darunter), im Median 0,64 m. Der Extremfall ist 4,7 m.
  - 8 Türen liegen mehr als 0,4 m über dem Gelände.
  - Bei 337 der 465 gäbe es eine andere Kante (mindestens 2 m lang), an der die Tür passen würde.
  - Sichtbar ist das auch an den Fenstern im Erdgeschoss, die das Gelände anschneidet (Station
    `START_BG_TUER_IM_BODEN`). Siehe E1.

### c. Stadtmauer, Tore, Treppen

Die Werte kommen aus `building_rules.json` (`cityWall`, `openings`). Die Mauer-Tests in `test_citywall.py` prüfen
Stufen, Wehrgang und offene Enden an der Geometrie.

| Maß | Wert | Bedarf | ok |
|---|---|---|---|
| Wehrgang Breite | 1,2 m | ≥ 0,8 m (Zylinder + 0,2) | ja |
| Brustwehr | 0,6 m (hohe 1,0 m) | ≥ 0,5 m | ja |
| Treppenstufe | 0,2 m | ≤ 0,4 m (Stufe des Controllers) | ja |
| Treppe Breite / Steigung | 1,2 m / 33,7° | ≥ 0,8 m / ≤ 50° | ja |
| Turmdurchgang Höhe | 2,2 m | ≥ 2,0 m (Figur + 0,2) | ja |
| Stadttor Breite / Kämpfer | 3,6 m / 2,6 m | ≥ 0,8 m / ≥ 2,0 m | ja |
| Pforte | 2,0 × 2,8 m | ≥ 0,8 × 2,0 m | ja |
| Haustür | 1,0 × 2,1 m | ≥ 0,8 × 2,0 m | ja (knapp, wie in Gothic) |
| offene Mauerenden | 0 | 0 | ja |

Alle Maße passen. Station `START_BG_TOR_OBEN` zeigt das Obere Tor mit der Figur davor.

### d. Maßstabsbilder

Die Bilder liegen nicht im Repo, sondern unter `C:\GotharData\review\w3\PLATZHALTER_begehung_<nn>_<station>_*.png`.
Jede Station gibt es zweimal, beide bei 12:00:
- `_augenhoehe`: Editor-Kamera in 1,62 m. Das ist ein Ausschnitt, weil der Editor seine Fenster immer zeigt; die
  grünen Marken sind Startpunkte.
- `_3p`: Third-Person-Kamera hinter der Figur (3 m Abstand, Blickpunkt 1,55 m).

| nn | Station | Was man sieht |
|---|---|---|
| 01 | `START_MARKTPLATZ` | Marktplatz Richtung Norden: Hausgrößen, Platzweite |
| 02 | `START_BG_GASSE_ENG` | engster Weg (0,63 m), zwischen Fachwerkhäusern |
| 03 | `START_BG_GASSE_SCHMAL` | schmaler Fußweg (1,12 m) |
| 04 | `START_BG_STEIL` | Törlensweg, steiler Hang |
| 05 | `START_BG_TREPPE` | OSM-Treppe als 62°-Rampe im Gelände |
| 06 | `START_BG_MAUERDURCHBRUCH` | Hinterer Zwinger: Straße endet an der Stadtmauer |
| 07 | `START_BG_WEG_DURCH_HAUS` | Im Zwinger: Weg führt in ein Haus |
| 08 | `START_BG_TOR_OBEN` | Oberes Tor, Durchgang 3,6 m |
| 09 | `START_BG_TUER_IM_BODEN` | Erdgeschoss 0,9 m im Gelände |
| 10 | `START_BG_SPALT` | Spalt zwischen zwei Häusern |

## Teil 2: Begehung mit dem Autopilot

`gothar-worldgen walk-routes leonberg` schreibt drei Routen nach `generated/walk/` (nicht versioniert). Code:
`qa/walk.py`, Tests `test_qa_walk.py`.
- **stations:** Für jede Station Teleport zum Startpunkt, dann zum Ziel (`target` in `starts.json`) und 6 m weiter.
- **ways:** alle begehbaren OSM-Wege der Altstadt, Stück für Stück außerhalb der Kollisionskörper.
  - Jedes Stück beginnt mit einem Teleport, dann folgt alle 8 m ein Punkt.
  - Insgesamt 466 Stücke und 2950 Punkte.
- **gates:** durch jedes Tor und jede Pforte und zurück.
  - Die Figur läuft quer zur Mauer gerade durch den Durchgang.
  - Steht ein Haus auf dieser Linie, kommt sie über die Straße der Pforte.

Gestartet werden die Routen mit
`gothar --world=worlds/leonberg/leonberg.g7world --walk=<route> --walk-out=<ordner> --no-render`. Ausgewertet wird
mit `gothar-worldgen walk-report leonberg --route <route> --run <ordner>`, das `walk_report.json` schreibt.
- Jedes Hängenbleiben, jeder Sturz und jedes Rutschen ab 0,5 s wird gegen die statische Begehung abgeglichen.
- Es gilt als bestätigt, wenn derselbe OSM-Weg gemeldet war oder ein Befund höchstens 3 m entfernt liegt.
- Kürzeres Rutschen ist nur Streifen an einer Wand.

| Route | Punkte | erreicht | Hänger | Stürze | Laufzeit (Spielzeit / echt) |
|---|---|---|---|---|---|
| stations | 27 | 15 | 12 | 0 | 40 s / 1 s |
| ways | 2950 | 2928 | 22 | 0 | 4532 s, 17,4 km / 6 s |
| gates | 24 | 23 | 1 | 0 | 50 s / 1 s |

- **Stationen:** Die Figur bleibt genau dort stehen, wo die statische Begehung es erwartet:
  - engste Gasse (0,63 m),
  - Hang hinter dem Törlensweg,
  - 62°-Treppe,
  - Stadtmauer am Hinteren Zwinger,
  - Haus am Ende von „Im Zwinger“,
  - Spalt.
  - Durch die schmale Gasse (1,12 m) und durch das Obere Tor kommt sie durch. Die Station „Tür im Boden“ endet
    erwartungsgemäß am Haus.
- **Wege:** 17,4 km ohne einen einzigen Sturz. Alle 22 Hänger kommen vom Gelände, nie von einem Haus.
  - 18 Hänger liegen an Stellen, die statisch schon als steil, zu steil oder Treppe gemeldet waren: die Treppen am
    Westhang (w20020128, w205276655), die 62°-Treppe, der Törlensweg, Pfade am Nordhang.
  - **Neu:** ein Pfad am Nordrand bei (−57,6 / 340,7), w968793394. Dazu kommen 5 kurze Rutscher (0,5–0,7 s) an
    Hängen.
  - Alle gehören zu E5 (glätten bzw. Treppen-Meshes).
- **Tore und Pforten:** Beide Tore und 3 von 4 Pforten sind in beide Richtungen durchgängig.
  - **Neu:** Bei der Pforte Zwerchstraße Nord ragt das Haus ZnA 0,4 m in die 2 m breite Öffnung; die Figur kommt nicht
    hinaus.
  - Das ist ein Fehler der Mauerplanung, kein Maßstabsproblem. Behoben wird er mit der nächsten Mauer-Arbeit: Pforte
    verschieben oder das Haus dort kürzen.
- **Maßstab (E3):** Die Läufe bestätigen den Maßstab 1:1. Kein Haus, keine Gasse und keine Mauer hält die Figur auf,
  außer an den bekannten Stellen.
  - Die Altstadt ist zu Fuß gut zu durchqueren: Vom Oberen zum Unteren Tor sind es etwa 285 m Luftlinie, also rund
    70 s Rennen (4 m/s).
  - Offen bleibt nur das Gefühl für Kamera und Enge in den Gassen; das zeigen die Bilder unter d.

## Entscheidungspunkte

**Entscheidungen** (Projektinhaber über den Koordinator, 2026-10-03):
- E1: A + B.
- E2: Wege enden am Haus, die Mauer bleibt geschlossen, Durchgänge nur an Toren und Pforten.
- E3: 1:1 vorerst, endgültig nach Teil 2. **Nach Teil 2 endgültig: 1:1, keine Gassenverbreiterung** (`leonberg.toml`).
- E4: A.
- E5: B jetzt, A mit W5.

**E1 – Türen und Erdgeschoss im Hang** (465 von 746 Türen im Gelände)
- A) Der Generator wählt unter den Straßenkanten die, an der das Gelände zur Türschwelle passt. Das löst etwa 337
  Häuser.
- B) Hanghaus-Regel: Die Türschwelle liegt auf dem Gelände vor der Tür. Talseitig zeigt sich das Sockelgeschoss als
  Bruchsteinsockel mit kleinen Fenstern, bergseitig steckt das Haus im Hang ohne Öffnungen unter dem Gelände. Wo die
  Schwelle über dem Gelände liegt, kommen Stufen davor (Freitreppe).
- C) So lassen bis W5.
- *Empfehlung welt:* A + B. Leonberg liegt auf einem Bergsporn, und Hanghäuser mit Sockel prägen die Altstadt; das
  passt zum Gothic-Gefühl (Stufen, Sockel, verwinkelt).

**E2 – Heutige Wege durch Häuser und Stadtmauer** (63 Abschnitte)
- Häuser (45): meist Fußwege in Hinterhöfen und Durchgängen. Heute gibt es dort Durchfahrten, in den Klötzchen nicht.
  - A) Durchgänge (Torbogen im Erdgeschoss) dort, wo ein Weg ein Haus quert.
  - B) Die Wege nur noch für die Splatmap nutzen; der Weg endet am Haus.
  - *Empfehlung:* B jetzt, A später für wenige, spielerisch wichtige Stellen per Override.
- Stadtmauer (18): heutige Durchbrüche (Hinterer Zwinger, Bahnhofstraße, Treppen am Westhang). Spitalhof und
  Graf-Eberhard-Straße laufen 1–4 m neben dem Tordurchgang.
  - A) Mauer geschlossen wie um 1700; hinein geht es nur durch Tore und Pforten.
  - B) Zusätzliche Pforten an den Durchbrüchen.
  - C) Lücken lassen.
  - *Empfehlung:* A. Die Tore sind durchgängig; die Straßen neben dem Tor sind kein Hindernis, die Figur geht durch
    den Torbogen.

**E3 – Spielmaßstab** (`config/leonberg.toml`: `game_scale.horizontal`, `vertical`, `alley_widen_factor`,
„wird nach dem Klötzchen-Test festgelegt“)
- Die Gassen reichen im Maßstab 1:1 (eine Engstelle auf 18,8 km Weg). Die Häuser wirken auf den Bildern
  stimmig zur Figur, und die Haustür passt mit 2,1 m.
- A) 1,0 / 1,0 / 1,0 beibehalten.
- B) Gassen verbreitern (z. B. `alley_widen_factor` 1,2), damit es mehr Platz für Kamera und Kämpfe gibt.
- C) Horizontal verkleinern, damit die Wege kürzer werden.
- *Empfehlung:* A. Endgültig erst nach der echten Begehung (Teil 2), denn Kamera in engen Gassen und Laufzeiten
  sieht man erst dort.
- *Nach Teil 2:* Die Läufe bestätigen 1:1, siehe oben. Vorschlag welt: 1,0 / 1,0 / 1,0 endgültig festlegen und in
  `leonberg.toml` als entschieden vermerken.

**E4 – Spalten zwischen Häusern** (62, die schmaler als die Figur sind)
- A) Die Kollision dort schließen (Füllkörper zwischen den Häusern); der Spalt bleibt sichtbar, aber die Figur und
  die Kamera bleiben nicht hängen.
- B) So lassen.
- *Empfehlung:* A, wie bei Gothics geschlossenen Häuserzeilen.

**E5 – Steile Wege und Treppen-Rampen**
- A) Treppen (`steps`) als eigene Treppen-Meshes mit Stufen zu 0,2 m (W5).
- B) Die Heightmap entlang der Wege auf höchstens 45° glätten. Damit weicht sie dort wie bei den Gewässern bewusst
  vom DGM ab.
- C) So lassen. Zu steile Stellen werden zu Hindernissen, man rutscht ab wie in Gothic am Hang.
- *Empfehlung:* B jetzt für die 4 zu steilen Wegstellen und die eine Treppe, A mit W5.

**E6 – Stadtmauer:** Alle Maße passen, es gibt nichts zu entscheiden. Der Wehrgang mit 1,2 m ist für eine Person
gedacht; zwei kommen nicht aneinander vorbei, das ist gewollt eng.

## Umsetzung E1, E4, E5

Nachgemessen mit `begehung` und den drei `--walk`-Routen. Bilder (nicht im Repo):
`C:\GotharData\review\w3e\PLATZHALTER_*`.

**E1 – Türen und Erdgeschoss im Hang** (`buildings/medieval.py`, Regeln `hillside` in `building_rules.json`)
- Der Generator liest das Gelände (DGM) an der Tür.
  - **A:** Unter den Straßenkanten nimmt er die, vor der das Gelände zum Boden passt.
  - **B:** Liegt das Gelände dort höher, steht der Boden darauf; darunter wird der Steinsockel des Hanghauses sichtbar.
  - Liegt es mehr als 0,4 m tiefer, kommt eine Freitreppe mit Stufen zu höchstens 0,2 m vor die Tür, mit
    Kollisionskörper.
  - Fenster, deren Brüstung im Gelände stecken würde, entfallen.
- **E1-C** (Koordinator im Auftrag): Passt bergseitig kein Erdgeschoss unter die Traufe, darf die Tür auf eine andere
  Kante ausweichen (Hof- oder Gassenseite).
  - Sonst **A:** Eingang im Obergeschoss. Die Stockwerke darunter werden so geteilt, dass ein Boden genau auf
    Geländehöhe liegt.
  - Sonst **B:** kurzer Abgang. `export-terrain` gräbt vor der Tür eine Rampe von 40° mit Böschungen
    (`export/descents.py`); ein Weg hat Vorrang, dort entfällt der Abgang.
- Die Türen stehen mit Position, Boden, Art und Richtung im Gebäude-Index (`doors`); die Begehung prüft sie direkt.
- **Leonberg** (1403 Häuser):

  | Art | Anzahl |
  |---|---|
  | Boden passt bzw. angehoben | 1143 |
  | Tür auf eine andere Kante ausgewichen (Hang) | 179 |
  | Freitreppe | 6 |
  | A: Eingang im Obergeschoss | 10 |
  | B: Abgang | 244 (141 gegraben, 11 wegen eines Weges ausgelassen, der Rest ohne nötige Tiefe) |

  - Türen im Gelände: **465 → 44**, meist nur wenige Zentimeter; Median aller Türen 0,0 m.
  - A ist selten, weil bei den meisten tief steckenden Häusern das Gelände an der Tür schon die Traufe erreicht. Für
    ein Obergeschoss ist dort kein Platz.

**E4 – Spalten** (`buildings/gaps.py`, Regeln `gapFill`): Freiraum, den eine Scheibe von 0,7 m Durchmesser nicht erreicht
und der überwiegend von Häusern umschlossen ist, bekommt unsichtbare `COL_HULL_`-Füllkörper am angrenzenden Haus. Der
Spalt bleibt sichtbar.
- Leonberg: 79 Füllkörper an 64 Häusern; Spalten schmaler als die Figur: **62 → 15**. Der Rest ist kleiner als
  0,3 m² oder offen zur Gasse.

**E5 – steile Wege** (`export/ways.py`, `data/leonberg/ways.json`): Das Längsprofil jedes begehbaren Weges der Altstadt
wird auf höchstens 42° begrenzt. Die Zellen am Weg folgen dem Profil, wiederholt bis keine Stelle mehr steiler ist.
- Abgänge werden vorher gegraben, damit die Glättung Schnitte in einen Weg ausgleicht.
- Die Routen folgen den Biegungen (Sehne höchstens 0,4 m neben dem Weg); sonst schnitten Serpentinen den Hang gerade
  hinauf.
- Leonberg: 16 Stellen, die steilste vorher 71°.
  - Statisch keine zu steile Stelle mehr, auch keine Treppe.
  - Der Autopilot bleibt auf **7 statt 11 Wegen** hängen. Der Rest sind Geländestufen von 1 m und mehr (Stützmauern,
    Terrassen neben OSM-Treppen) und bleibt Hindernis bis zu den Treppen-Meshes (E5-A, W5).

**Außerdem:**
- Startpunkte aus den Daten stehen nach jedem Export wieder auf der Heightmap, sofern sie nicht im Editor verschoben
  wurden.
- `assemble` nimmt dafür die exportierte Heightmap statt des DGM.

**Autopilot nachher:**
- Wege: 3229 von 3245 Punkten, 0 Stürze, Hänger nur am Gelände.
- Tore: unverändert; nur die Pforte Zwerchstraße Nord bleibt zu (Haus ZnA, folgt mit der Mauer-Arbeit).

## Selbst begehen (Anleitung für den Projektinhaber)

1. Engine bauen (Debug genügt): `cmake --preset debug` und dann `cmake --build --preset debug`.
2. Starten, z. B. an der engsten Gasse:
   `build\debug\game\gothar.exe --world=worlds/leonberg/leonberg.g7world --start=START_BG_GASSE_ENG --time=12:00`
3. Startpunkte: `START_MARKTPLATZ`, `START_UEBERSICHT` und die neun Stationen `START_BG_*` aus der Tabelle oben.
   Sie stehen in `tools/worldgen/data/leonberg/starts.json`.
4. Steuerung (Schema „classic“):
   - W/S vor/zurück, A/D drehen, Q/E seitwärts.
   - Shift halten = gehen, Alt = springen, X = schleichen bzw. tauchen.
   - F3 = freie Kamera, F1 = Debug-Fenster, F2 = Debug-Zeichnung, Esc = Pause.

## Nächste Schritte

- E1 und E4 im Gebäude-Generator umsetzen, E5 in der Heightmap; danach `begehung` und `walk-routes` erneut laufen
  lassen.
- Pforte Zwerchstraße Nord freimachen.
- Mauer-Wehrgang und Treppen als eigene Route, sobald die Mauerplanung ihre Treppen exportiert.
