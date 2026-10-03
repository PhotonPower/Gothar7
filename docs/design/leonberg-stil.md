# Leonberg – Stil-Referenzblatt (W5)

**Entscheidung (Koordinator im Auftrag des Projektinhabers), 2026-10-03.** Alle Prozente und Maße sind Startwerte
und werden nach den ersten Bildern justiert. Maschinenlesbar stehen die Werte in
`tools/worldgen/data/building_rules.json` (Stilprofile, Palette, Muster) und
`tools/worldgen/data/facade_vocabulary.json` (Begriffe). Umsetzung: `tools/worldgen/src/gothar_worldgen/buildings/medieval.py`
(`gothar-worldgen buildings <ort> --mode medieval`), Rückbau siehe `leonberg-pipeline.md` §7.

## Leitbild
Leonberg um 1700, schwäbisch-alemannisches Fachwerk. Gedeckte, erdige, leicht entsättigte Farben (Gothic-Stimmung),
deutliche Alterung. Texturen und Trim-Sheets sind eigene Arbeit oder CC0.

## Stile und Bauweise
| Stil | Erdgeschoss | Fachwerk | Auskragung | Besonderheit |
|---|---|---|---|---|
| Bürgerhaus | zu 70 % massiv (Bruchstein), sonst Steinsockel | ja | an Straßenseiten üblich | am Marktplatz immer: massives Erdgeschoss, Auskragung |
| Amts-/Rathaus | zu 70 % massiv | Schwäbischer Mann, Rauten in den Brüstungen | ja | |
| Handwerkerhaus | Steinsockel | ja | selten (10 %) | |
| Ackerbürgerhaus | Steinsockel | ja | selten (10 %) | am Rand der Altstadt |
| Scheune / Stall | Steinsockel | ja | nein | großes Tor zur Straße |
| Steinhaus, Kirche, Stadtmauer | massiv | nein | nein | Mauer ohne Öffnungen |

**Zuweisung** (in dieser Reihenfolge):
1. Override `style`.
2. Marktplatz-Front: Bürgerhaus, außer Rathaus.
3. Ersatzhäuser aus dem Rückbau: an Hauptstraßen Bürgerhaus, sonst Handwerkerhaus.
4. ALKIS 51007: Mauer.
5. OSM-Kirche bzw. ALKIS 3041–3044: Kirche.
6. OSM-Schloss: Steinhaus.
7. ALKIS 3010–3016: Amtshaus.
8. ALKIS 27xx, 2461/2463 (Scheunen, Ställe, Schuppen, Garagen): Scheune/Stall.
9. An Hauptstraße oder Platz und mindestens 60 m²: Bürgerhaus.
10. Sonst Ackerbürgerhaus (mehr als 230 m² vom Marktbrunnen) bzw. Handwerkerhaus.

## Fachwerk-Muster (Häufigkeit je Stil)
- **Bürgerhaus:**
  - Schwäbischer Mann 35 %, Halber Mann 25 %, Ständer und Riegel 20 %, Andreaskreuz 10 %, Feuerbock 5 %.
  - Raute 5 %: Zierform, nur in Brüstungsfeldern; das Haus hat dann Ständer und Riegel.
- **Handwerker- und Ackerbürgerhaus:** Ständer und Riegel 40 %, Halber Mann 30 %, Schwäbischer Mann 20 %, Andreaskreuz 10 %.
- **Scheune / Stall:** Ständer und Riegel mit Kopf- und Fußstreben 70 %, Andreaskreuz 30 %.
- **Amts-/Rathaus:** Schwäbischer Mann mit Rauten in den Brüstungen.

Figuren wie Mann oder Kreuz stehen an den Fassadenenden und in jedem 3. Feld, dazwischen Ständer und Riegel; so war
es historische Praxis, und das Dreiecksbudget bleibt gewahrt. Riegel und Streben sind flache Bretter, Schwelle,
Rähm und Ständer Balken. Die Muster sind eigene Geometrie in normierten Feldkoordinaten.

## Materialien und Farben
- **Ausfachung:**
  - Kalkputz gebrochenes Weiß 50 %, ocker 25 %, grau-verwittert 10 %.
  - Lehm/Flechtwerk sichtbar 10 %, nur bei Scheunen und ärmeren Häusern.
  - Backstein 5 %. Putz ist nie reinweiß.
- **Sockel und Erdgeschoss:** Bruchstein.
- **Dachdeckung:**
  - Biberschwanz alt (dunkles Rotbraun, bemoost) 70 %, Biberschwanz rot 25 %, Holzschindeln 5 % (Schuppen, Anbauten).
  - Kein Schiefer (in Württemberg untypisch). Stroh nur bei Scheunen außerhalb der Mauer.
- **Dachneigung:** typisch 50–55°.
  - Im Modus medieval werden flache, Pult- und flach geneigte Dächer (unter 35°) zu Satteldächern mit 50–55°,
    die Traufe bleibt.
  - Dächer ab 35° behalten ihre gemessene Form.
- **Balken:** dunkelbraun bis schwarzbraun; etwa 15 % ochsenblutrot an repräsentativen Häusern (Bürger-, Amtshaus).
- **Fenster:** dunkle Holzrahmen, kleine Scheiben.
- **Palette:** 14 feste Materialwerte mit gleichem Wert in allen Häusern; die Engine bündelt danach.

## Alterung
Vorgesehen:
- Spritzwasser-Schmutz am Sockel
- Regenstreifen unter Fenstern und Traufen
- Moos auf nordseitigen Dachflächen
- leicht verzogene Linien bei alten Häusern (kleine Seed-Variation)

Stand (Schritt 3, Entscheidung Koordinator im Auftrag, 2026-10-03; Werte in `building_rules.json` → `aging`):
- **Moos** auf Nordseiten: eigener Paletteneintrag „Biberschwanz alt, bemoost“.
- **Alter je Haus** 0 (neu) bis 1 (alt), deterministisch aus ID und Seed, je Stil verteilt:
  - Scheune 0,5–1; Ackerbürger 0,4–1; Handwerker 0,3–0,95; Bürger 0,2–0,8; Amtshaus 0,1–0,6; Steinbauten 0–0,4.
  - Ersatzhäuser aus dem Rückbau 0,1–0,6.
  - Override `age` (0…1) korrigiert von Hand (Schema §4, Web-UI).
- **Durchhängender First:** bis 1,5 % der Firstlänge × Alter in der Mitte.
  - Nur die Firstlinie sackt, Traufen und Giebelenden bleiben. Nachbarhäuser an einer Brandwand klaffen an der
    Traufe nicht auseinander (Test).
  - Steinbauten: Faktor 0,1.
  - Das Dach ist dafür in 4 Bänder entlang des Firsts geteilt.
- **Schiefe Ständer:** innere Ständer bis 1,2° × Alter, nie in eine Öffnung oder aus dem Geschoss; Eckständer bleiben
  gerade. Steinbauten haben keine Ständer.
- **Unregelmäßige Fenster:** Brüstung ±5 cm und Breite ±5 % × Alter; annotierte Öffnungen bleiben exakt.
- **Leonberg-Kern:** Alter (Klassen) 0.0: 118, 0.2: 321, 0.4: 467, 0.6: 272, 0.8: 228; mittlerer Durchhang 7.6 cm; 1,86 Mio. Dreiecke.
- **Folgt mit Texturen bzw. Trim-Sheets:** Spritzwasser-Schmutz am Sockel, Regenstreifen.
- Vorher/Nachher bei 12:00: `PLATZHALTER_alterung_*` (nach engines Dunst-Anpassung #80).

## Schornsteine und Gauben
Stand (Entscheidung Koordinator im Auftrag, 2026-10-03; Werte in `building_rules.json` → `chimneys`, `dormers`):
- **Schornsteine**, gemauert (Bruchstein 70 %, Backstein 30 %):
  - Anzahl: Bürger-, Amts-, Handwerker- und Ackerbürgerhaus 1, ab 14 m Firstlänge 2. Steinhaus 1. Kirche und Mauer
    keinen; unter 4 m Firstlänge keinen.
  - Scheunen nur mit Herdstelle: angebaut an ein Wohnhaus (≤ 0,5 m) oder mit einer ALKIS-Funktion aus
    `hearthFunctions` (Ackerbürger-Nutzung, vorerst leer). Frei stehende Scheunen haben keinen (Brandschutz, Heu).
  - ALKIS-Garagen (2463, `noHearthFunctions`) bekommen nie einen, auch angebaut: Um 1700 wären das Schuppen oder
    Ställe ohne Herd.
  - Lage wie über einer Herdstelle in der Hausmitte: entlang des Firsts bei 30–70 % (bei zweien je einer in jeder
    Hälfte), bis 0,6 m neben dem First, mindestens 1,2 m von den Giebelenden.
  - Querschnitt 0,6 × 0,8 m, 0,8–1,2 m über dem First, Kopfplatte. Der Schaft beginnt unter der Dachfläche, auch beim
    durchhängenden First entsteht kein Spalt (Test).
- **Gauben**, wenige (historisch):
  - Schleppgaube 70 % (Dach 15–20°), kleine Giebelgaube 30 % (50°); Breite 1,4–2,0 m, Front 1,2 m hoch.
  - Häufigkeit je Haus: Amtshaus 50 %, Bürger 40 %, Ackerbürger 25 % (zur Hälfte als Ladeluke), Handwerker 20 %,
    Scheune 10 % (Ladeluke), Steinbauten keine.
  - Nur auf Satteldächern mit mindestens 4 m Dachtiefe (Traufe bis First) und 3 m Firsthöhe über der Traufe.
  - 1 je angefangene 6 m Traufe, höchstens 3 je Dachseite. Zu 70 % nur auf einer Seite:
    - Traufständig: die Seite zur Straße.
    - Giebelständig (beide oder keine Dachseite zur Straße): die sonnigere Seite, Süden vor Westen.
  - Abstände: 1 m zu den Giebeln, 0,8 m unter dem First, 0,6 m über der Traufe (entlang der Dachfläche), 1 m
    zueinander, 0,5 m zum Schornstein, 1 m zu anderen Baukörpern desselben Gebäudes (keine Gauben an Kehlen).
  - Die Gaube sitzt auf dem Hauptdach, das kein Loch bekommt: Front und Wangen reichen in die Dachfläche, das
    Gaubendach taucht hinten unter das Hauptdach. Front und Wangen in der Ausfachung des Hauses, Dach wie das Hausdach
    (nordseitig bemoost).
- **Budget:** Schornsteine sind immer dabei. Liegt ein Haus auch ohne Fachwerk (Stufe 3) über 2000 Dreiecken, fallen
  die Gauben weg (Stufe 4).
- **Handkorrektur:** Overrides `dormers` (0–6) und `chimneys` (0–4) als Anzahl, 0 = keine (Schema §4, Web-UI).
- **Leonberg-Kern:**
  - 1398 Schornsteine: Handwerker 787, Bürger 341, Ackerbürger 233, Scheunen 34 (angebaut, ohne Garagen; vorher 114),
    Steinhaus 2, Amtshaus 1.
  - 230 Gauben auf 106 Häusern: Bürger 75 (34 Häuser, 14 %), Handwerker 95 (45, 7 %), Ackerbürger 57 (25, 12 %),
    Scheunen 3.
  - Weniger als gewürfelt, weil viele Dächer zu klein sind, vor allem die schmalen Ersatzhäuser aus dem Rückbau
    (giebelständig, 3,5 m Dachtiefe).
  - 1,88 Mio. Dreiecke; 11 Häuser auf Stufe 4.
- Vorher/Nachher bei 12:00: `PLATZHALTER_gauben_*`.

## Stadtmauer und Mauerhäuser
Stand (Entscheidung Projektinhaber bzw. Koordinator im Auftrag, 2026-10-03; Werte in `building_rules.json` → `cityWall`):
- Bruchstein (Palette `stone`), 7 m bis zur Brustwehr, offener Wehrgang mit Zinnen, etwa 10 % der Zinnen fehlen.
- Viereckige Flankentürme und Tortürme mit Zeltdach (`roof_old`), Spitzbogen-Durchfahrt, offene Torflügel (`timber_dark`).
- **Mauerhäuser:** Häuser auf der Mauerlinie zeigen nach außen nur Bruchstein mit Schießscharten und kleinen Fenstern
  über der Mauerkrone. Niedrige Häuser verstecken ihr Dach hinter einer Schildmauer mit Zinnen. Zur Stadt hin bleiben
  sie Fachwerkhäuser ihres Stils.
- Leonberg: 29 Mauerhäuser, vor allem die äußere Zeile an der Grabenstraße.
- Bilder bei 12:00: `C:\GotharData\review\w6\PLATZHALTER_mauerhaeuser_*`.

## Erste Bilder und Justierung (Platzhalter, 2026-10-03)
Ablage `C:\GotharData\review\w5\` (nicht im Repo).
- **Beobachtung:** Im Engine-Licht wirkten die Dachfarben kräftiger orange-rot als „gedeckt, entsättigt“. Das Standardlicht
  der Engine ist bis zum Tag/Nacht-Zyklus eine tiefe, warme Abendsonne (Sonnenfarbe 1,0/0,72/0,5) vor einem Dämmerungshimmel.
  Das verstärkt Orange.
- **Palette justiert** (Entscheidung Koordinator im Auftrag des Projektinhabers):
  - Biberschwanz rot: Sättigung −35 %, Helligkeit −20 %, Richtung Braunrot. sRGB 155/72/50 → 124/78/70.
  - Biberschwanz alt: Sättigung −45 %, Helligkeit −20 %, braungrau-rot. 108/60/46 → 86/64/59.
  - Bemoost und Schindeln entsprechend dunkler.
  - Ocker-Putz gedämpft: 197/164/111 → 181/158/122.
  - Weißer Putz gebrochen, wärmer, einen Hauch dunkler: 218/209/192 → 205/196/180.
  - Balkenfarben unverändert.
- **Belege:** Vorher/Nachher in Engine-Screenshots (`PLATZHALTER_palette_*`) und als Farbtafel unter neutralem und unter
  Abendlicht (`PLATZHALTER_palette_vorher_nachher.png`).
- **Mittagslicht** (`--time=12:00`, engines Tag/Nacht #79): Die Dächer wirken gedeckt rotbraun bzw. braunrot, der Putz warm
  gebrochen. Die Palette passt. Leonberg wirkt mittags noch überstrahlt (heller Dunst, helle Fassaden); Licht und Nebel
  justiert engine nach der Abnahme durch den Projektinhaber, nicht die Palette.
- Bilder für 12:00, 19:30 und 23:00 liegen als `PLATZHALTER_{mittag,abend,nacht}_*.png` im selben Ordner.
