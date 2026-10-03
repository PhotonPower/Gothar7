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
