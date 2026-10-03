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

Stand:
- **Umgesetzt:** Moos auf Nordseiten (eigener Paletteneintrag „Biberschwanz alt, bemoost“).
- **Folgt ohne Texturen:** verzogene Linien.
- **Folgt mit Texturen bzw. Trim-Sheets:** Schmutz und Regenstreifen.

## Erste Bilder (Platzhalter, 2026-10-03)
Ablage `C:\GotharData\review\w5\` (nicht im Repo). Beobachtung zur Justierung: Im Engine-Licht wirken die Dachfarben
kräftiger orange-rot als „gedeckt, entsättigt“. Vorschlag zur nächsten Runde: Dachwerte dunkler und entsättigter.
