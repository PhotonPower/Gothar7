# Schloss innen, Keller und Dungeon (W7/W8)

Status: **freigegeben 2026-11-23** (Projektinhaber über den Koordinator: alle Fragen F1–F12 nach Vorschlag; Vertrag
mit engine abgeglichen, #422). Auftrag des Projektinhabers über den Koordinator: Das Schloss wird begehbar und
bekommt einen Keller. Im Keller liegt ein versteckter Zugang zu einem Dungeon im Geist klassischer
Grid-Dungeon-Crawler: viele Ebenen tief, Falltüren, Teleporter, Türen und Gatter, versteckte Nischen in den Wänden,
Schalter und Trittplatten, Monster und Schätze.

Übernommen wird **nur das Spielgefühl** (enge Gänge, Rätsel aus Schaltern und Platten, Abstieg Ebene um Ebene).
Karten, Namen, Gegner und Grafik bekannter Spiele bilden wir nicht nach; alles sind eigene Inhalte (ADR 0008,
`assets/README.md`).

Die Mechanik (Falltür, Teleporter, Gatter, Schalter, Trittplatte, Geheimwand, Ebenenwechsel) baut **engine**. welt
baut die Räume und Ebenen und setzt die Vobs. Was welt dafür in die Welt schreibt, steht in Abschnitt 5;
er ist mit engines Mechanik-Plan (#422, `docs/modules/dungeon.md`) abgeglichen (Entscheidung Koordinator, 2026-11-23).

---

## 1. Schloss innen

### 1.1 Bestand
Das Schloss ist ein Handmodell aus dem Blender-Skript (W-E3, `schloss.json`):
- **Hauptbau** 76 × 14 m, 4 Geschosse, Traufe 14,6 m über Grund.
- **Verbindungsbau** mit 2 Geschossen.
- **Ostflügel** 43 × 20 m.
- achteckiger Treppenturm, Erker, Portal (2,6 × 3,6 m) auf der Hofseite des Hauptbaus.

Bisher ist das Schloss außen geschlossen: Die Kollision besteht aus 4 konvexen Körpern, und es gibt keine Innenräume.

### 1.2 Vorschlag: was begehbar wird
Begehbar werden Erdgeschoss, ein Obergeschoss und der Keller. Weitere Geschosse und der Dachboden bleiben vorerst zu.
Die Fenster sind von innen sichtbar, aber nicht begehbar.

| Geschoss | Hauptbau | Verbindungsbau / Ostflügel |
|---|---|---|
| **Keller** (neu, ~3 m unter dem Hof) | Gewölbekeller: Weinkeller mit großem Fass, Vorratsgewölbe, Verlies (zwei Zellen) | – (Fundament) |
| **Erdgeschoss** | Eingangshalle hinter dem Portal, Wachstube, Schreibstube des Vogts, Küche mit Herd, Speisekammer | Durchgang; Ostflügel EG: Wirtschaftsräume (Waschküche, Gesindestube) |
| **1. Obergeschoss** | Festsaal (der größte Raum, über die halbe Länge), Gemach des Vogts, Bibliothek, Kapelle mit Altar | Ostflügel: Gästezimmer (Schlafkammern) |

- **Stil:** passend zu `leonberg-stil.md` „Schloss“, mit der vorhandenen Palette. Unten verputzte Wände mit Steinboden
  und Holzbalken-Decken, im Festsaal eine Kassettendecke (vereinfacht). Der Keller hat Tonnengewölbe aus Bruchstein und
  Lehm- bzw. Steinboden.
- **Treppen:**
  - Der achteckige **Treppenturm** verbindet die Geschosse als Wendeltreppe. Sie kollidiert als glatte Rampenstücke
    mit höchstens 35° (wie die Treppen der Häuser) und hat eine Mittelsäule.
  - Vom Keller führt eine gerade Kellertreppe unter der Eingangshalle hinauf.
- **Weg hinein:**
  - Vom Schlosshof durch das vorhandene Portal (als Tür-Mob, offen).
  - Der Hof ist über die vorhandenen Wege an Schlossberg und Garten erreichbar (im 40-m-Mauergürtel, #403).
  - Ein zweiter Zugang über den Garten bleibt zu (Gartentür verschlossen; Schlüssel als Inhalt).
- **Einrichtung und Leben:**
  - Wie in den begehbaren Häusern (W7): Mobs (Tisch, Bank, Bett, Truhe, Amboss nur in der Schmiede), Hausrat, Licht,
    Innenzonen, Wegnetz mit Freepoints.
  - Bewohner und Routinen nach Entscheidung des Projektinhabers (Frage F1): Vogt, Wachen, Köchin, Schreiber.
- **Im Spiel, nicht in einer eigenen Welt:**
  - Das Schloss bleibt in `leonberg.g7world`, wie die Häuser.
  - Durch die Fenster sieht man die Stadt, und es gibt keine Ladezeiten.
  - Unter dem Grundriss des Schlosses bekommt das Gelände **Löcher** (`terrain.holes`), damit der Keller nicht im
    Gelände steckt. Das Modell bringt dafür Kellerwände und einen eigenen Boden mit.

### 1.3 Bauweise
- **Schloss-Skript:**
  - `schloss_geometry.py` bekommt einen Innenteil: Geschossböden, Innenwände, Durchgänge, Treppenturm innen,
    Kellergewölbe.
  - Die Außenhülle wird hohl: Die Kollision besteht dann aus Wänden, Böden und Rampen statt der 4 Blöcke.
  - Budget: außen weiter ≤ 15 Tsd. Dreiecke. Innen ein eigener Mesh-Vob je Raum (Licht-Budget wie bei den Häusern),
    zusammen ≤ 25 Tsd.
- **Index:** Das Skript schreibt für das Schloss einen Eintrag im Format der begehbaren Häuser (`interior` mit Räumen,
  Durchgängen, Treppen, `upper`) nach `handmade.json`. Damit laufen Einrichtung, Wegnetz, Innenzonen und Probe
  (`uses/inside.py`) ohne eigenen Code für das Schloss. Wo das Schloss nicht passt (Wendeltreppe, mehr als zwei
  Geschosse), wird der Index erweitert.
- **Prüfung:**
  - Rundgang mit Autopilot durch alle Räume.
  - NPC-Probe für die Bewohner.
  - Bodenregel (Handmodell nicht im Gelände).
  - Bilder innen und außen zur Abnahme.

---

## 2. Der versteckte Zugang

**Vorschlag (Frage F3):** Im Weinkeller steht ein **Riesenfass**, 4 m hoch, eigenes Modell. Dahinter liegt der Zugang.
- **Hinweise:**
  - In der Bibliothek liegt ein Schriftstück, etwa „Rechnung des Küfers über ein Fass, das kein Wein füllt“ (Text:
    Projektinhaber bzw. engine-Inhalt).
  - Im Weinkeller zieht es: Ambiente-Zone mit Luftzug und leisem Tropfen.
  - Der Fackelhalter neben dem Fass ist schief.
- **Öffnen:**
  - Der schiefe Fackelhalter ist ein **Hebel** (Mob `lever`, „benutzen“, `LEVER_SCHLOSS_FASS`). Er öffnet die
    Vorderwand des Fasses, einen `mover` vom Typ `secret_wall` (`WALL_SCHLOSS_FASS`), die wie eine Tür aufschwenkt.
  - Ohne den Hebel gibt es keinen Weg hinein: Schlösser knacken geht nicht, Gewalt auch nicht.
- **Dahinter:**
  - eine enge Wendeltreppe hinab;
  - am Fuß eine Vorkammer mit dem ersten Raster-Gang;
  - an deren Ende der **Abstieg** (Levelwechsel-Trigger) nach Ebene 1.
- Die Geheimtür bleibt offen, sobald sie gefunden ist (Zustand im Spielstand). Der Rückweg führt durch dieselbe Tür.

Alternativen, falls gewünscht: eine verschiebbare Wand (eine Steinplatte gleitet zur Seite), oder ein Regal, das als
Ganzes aufschwingt.

---

## 3. Der Dungeon

### 3.1 Welten: eine Datei je Ebene (Vorschlag)
| | **eine `.g7world` je Ebene** (Vorschlag) | eine Welt, Ebenen übereinander | unter Leonberg in `leonberg.g7world` |
|---|---|---|---|
| Laden | kurz (eine Ebene mit ~2–4 Tsd. Vobs); Ladebildschirm bei jedem Ebenenwechsel | einmal | gar nicht |
| Falltür | Levelwechsel auf eine Ankunftszelle der Ebene darunter | echter Fall (8–10 m, Fallschaden ab 4 m: Sonderregel nötig) | wie links |
| Monster folgen | nein (wie im Vorbild üblich) | ja | ja |
| Licht/Nebel/Musik | je Welt eigene Umgebung (keine Sonne, Nebel kurz) | Zonen je Ebene | Zonen; die Sonne müsste unter der Erde aus sein |
| Editor/Arbeit | eine Ebene = eine Datei, parallel bearbeitbar | große Datei | Leonberg wird riesig, jeder Lauf erzeugt alles neu |
| Speichern | Zustand je Welt (vorhanden: Weltwechsel behält ihn) | eine Welt | eine Welt |

- **Vorschlag:** eine Welt je Ebene unter `worlds/leonberg/dungeon/ebene_NN.g7world`.
- Übergänge laufen über den vorhandenen Levelwechsel (`changeWorld`): Treppen hinab und hinauf, Falltüren,
  Teleporter zwischen Ebenen.
- Innerhalb einer Ebene teleportiert die Engine ohne Laden.
- Die Zustände bleiben beim Wechsel erhalten (geöffnete Gatter, genommene Schätze, tote Monster).

### 3.2 Raster, Maße, Tiefe (Vorschlag, Frage F4/F5)
- **Raster:**
  - Zelle 3 × 3 m, lichte Höhe 3,5 m. Gänge sind eine Zelle breit, Räume mehrere.
  - Die Steuerung bleibt **frei wie in Gothic** (Third Person, Kampf, Ausweichen); das Raster ist nur Bauweise.
  - Die Spielfigur (Zylinder r 0,3 m) passt bequem; zwei Figuren können in einem Gang kämpfen.
- **Ebenen:** **8 Ebenen**, jede 24 × 24 bis 32 × 32 Zellen (72–96 m Kantenlänge), etwa 40–60 % davon begehbar.
  Ebene 8 ist der Hort mit dem Endgegner. Mehr Ebenen gehen später.
- **Themen** (Vorschlag, eigene Erfindung):
  1. Alte Gewölbe: Übergang aus dem Keller, leicht, führt die Mechaniken ein: Schalter, Tür, Platte.
  2. Verlies: Zellen, Schlüssel, Gatter.
  3. Zisterne: Wasser, Stege, Brunnenschächte.
  4. Grabkammern: Nischen, Totenwächter.
  5. Werkstätten der Erbauer: Teleporter, Uhrwerke.
  6. Labyrinth: Geheimwände, Drehteleporter.
  7. Kristallgrotte: Höhle statt Mauerwerk, Licht der Kristalle.
  8. Hort: Boss, Schatz, Rückweg nach oben.
- **Rückweg (F6):**
  - Jede Ebene hat eine Treppe hinauf.
  - Ab Ebene 3 öffnet sich zusätzlich alle zwei Ebenen eine **Abkürzung** nach oben, sobald sie von unten entriegelt
    ist (ein Gatter oder ein Teleporter zur Vorkammer im Keller).

### 3.3 Bauweise: Kartendateien + Generator + Baukasten
- **Karten als Text** (Quelle der Wahrheit), z. B. `tools/worldgen/data/leonberg/dungeon/ebene_01.txt`:
  - ein Zeichen je Zelle: `#` Wand, `.` Boden, `D` Tür, `G` Gatter, `P` Falltür, `T` Teleporter, `S` Wandschalter,
    `O` Trittplatte, `N` Nische, `H` Geheimwand, `<` Treppe hinauf, `>` Treppe hinab, `~` Wasser, `*` Schatz,
    `m` Monsterplatz;
  - dazu `ebene_01.json` mit allem, was ein Zeichen nicht sagt: Ziele der Teleporter, Verknüpfungen (Schalter → Gatter),
    Schlösser und Schlüssel, Inhalt der Nischen, Monsterart, Licht, Thema.
  - Ein Diff zeigt genau, was sich an einer Ebene geändert hat. Der Projektinhaber kann Karten lesen und Änderungen
    wünschen, ohne Blender.
- **Generator** `gothar-worldgen dungeon <ort>`:
  - liest die Karten;
  - setzt Module aus dem **Baukasten** zusammen und fasst sie je 8 × 8 Zellen zu Mesh-Vobs zusammen (wenig Draw-Calls,
    LOD nicht nötig);
  - Kollision: Wände als Kästen je Wandzug, Boden und Decke je Block;
  - schreibt die Mechanik-Vobs, das Wegnetz, Zonen, Lichter und Startpunkte.
  - Gleiche Karte, gleiche Bytes; feste VobIds wie bei Leonberg (`vob_ids.json` je Ebene).
- **Baukasten:**
  - Module in Python wie die Mobs und Requisiten (`mobs.py`): Wandstück, Ecke, Boden, Decke, Pfeiler, Türrahmen, Gatter,
    Falltür, Nische, Schalter, Trittplatte, Treppe, Grotte.
  - Texturen aus dem Textur-Werkzeug (Bruchstein, Quader, Lehmboden, Holz, Eisen).
  - Kein Blender, damit der Generator deterministisch und testbar bleibt.
  - Das Riesenfass und besondere Stücke (Thron, Altar) darf auch figuren als `.glb` liefern.
- **Handarbeit** im Editor (M16): Feinschliff als gesperrte (locked) Vobs, wie bei Leonberg. Die Karte bleibt die
  Quelle; der Generator lässt gesperrte Vobs stehen.
- **Prüfungen** (wie bei Leonberg):
  - Erreichbarkeit: jede Zelle über Wege, Teleporter und Treppen (der Generator rechnet die Logik der Schalter durch;
    nichts darf unlösbar werden);
  - Rundgang mit Autopilot je Ebene;
  - NPC-Probe für Monster-Wege;
  - Bench (Draws, Dreiecke, Lichter);
  - Bilder.

### 3.4 Licht, Nebel, Musik, Klang
- **Licht:**
  - keine Sonne, kein Himmel;
  - Grundlicht sehr dunkel, Fackeln an Wänden als Punktlichter (≤ 8 je Block aus 8 × 8 Zellen; die Engine wählt die
    nächsten);
  - Ebene 7 bekommt Kristall-Licht (bläulich);
  - die Fackel des Helden ist wichtig (vorhanden, M13).
- **Nebel:** kurz und dicht (15–25 m), je Ebene eine Farbe. Dafür braucht es eine Umgebungs-Einstellung je Welt
  (Abschnitt 5).
- **Zonen:**
  - eine `indoor`-Zone je Raum bzw. Block;
  - Musik `DUNGEON` (ruhig, bedrohlich) und `DUNGEON_KAMPF` über die dynamische Musik;
  - Ambiente `gewoelbe` (Tropfen, fernes Grollen), `zisterne`, `grotte`.
  - Neue Themen und Ambienten liefert engine bzw. der Projektinhaber (Klänge).
- **Wegnetz für Monster:**
  - ein Wegpunkt `WP_DNG_E01_<x>_<y>` in der Mitte jeder begehbaren Zelle, Kanten zu den vier Nachbarn;
  - Kanten durch Türen und Gatter sind mit diesen verknüpft (geschlossen = gesperrt, Abschnitt 5);
  - Freepoints `FP_ROAM_DNG_…` für umherstreifende Monster;
  - Monsterplätze aus der Karte als Spawn-Punkte.

### 3.5 Boden und Decke (Entscheidung Projektinhaber 2026-11-26)
„Mach den Boden und die Decke anders als die Wände.“ Die Wände bleiben Bruchstein. Alle Texturen sind prozedural
(`textures/procedural.py`, `DUNGEON_KINDS`) und liegen in `assets/source/worlds/leonberg/dungeon/textures/` (nicht im gemeinsamen Ordner der Mobs).
- **Boden:** große, abgetretene Steinplatten unterschiedlicher Größe und Form (`flagstone`, Kachel 3 m, Platten etwa
  40–100 cm) mit dunklen offenen Fugen, einigen Rissen und ausgetretenen Mulden; dunkler und glatter als die Wand.
  Je Thema:
  - Zisterne: `flagstone_damp`, feucht und dunkler, mit schwarzgrünen Algen in den Fugen;
  - Beinhaus: `flagstone_dust`, staubgrau, Staub in den Mulden;
  - Werkstatt: einzelne Zellen mit Dielen (`oak`) oder eingelassener Eisenplatte (`plate`).
- **Decke:** deutlich anders als die Wand, mit Gurtbögen bzw. Balken alle 6 m (zwei Zellen) quer zu jedem Gang; in
  Räumen ergibt das ein Raster. Wo ein Bogen auf eine Wand trifft, trägt ihn eine Konsole.
  - kleine Ziegel (`brick`, heller und röter als die Wand, Ruß in Flecken) mit Quader-Gurtbögen: Gewölbe (E1),
    Zisterne (E3), Labyrinth (E6); mit Eichenbalken: Werkstatt (E5);
  - rauer dunkler Naturstein (`rock`, Klüfte, Rußflecken) mit Eichenbalken: Verlies (E2); mit Quader-Gurtbögen:
    Beinhaus (E4).
- Ein echtes Tonnengewölbe wäre bei freien Raumformen viel Geometrie; die flache Decke mit Bögen ist die vereinbarte
  Mindestlösung. Rußflecken als eigene Flächen über den Fackeln wirkten wie Löcher und entfallen; der Ruß steckt in
  den Decken-Texturen.

---

## 4. Inhalte: Monster und Schätze
- **Monster:** Die vorhandenen eigenen Arten passen nur zum Teil: Schinder und Quaderbuckel ja, der Glemsmahr ist ein
  Wald-Jäger, der Bergleu ist als Boss für den Engelberg gedacht. Vorschlag: **4–6 neue Dungeon-Arten** von figuren
  (Frage F7), zum Beispiel:
  - ein Gewölbe-Kriecher (klein, Rudel);
  - ein Steinwächter (langsam, gepanzert, bewacht Gatter);
  - ein Nischenbewohner (springt aus Nischen);
  - ein Zisternen-Schleicher (Wasser);
  - ein Grabhüter;
  - ein Endgegner für Ebene 8.

  Namen und Gestalt bestimmen Projektinhaber und figuren.
- **Wahl des Projektinhabers (Monster je Ebene):**

  | Monster | Ebenen |
  |---|---|
  | Mauerkriecher | 1–3 |
  | Gatterbock | 2, 5 |
  | Brunnenmolch | 3, 7 |
  | Beinhauswart | 4 |
  | Mauerschlupfer | 4–6 |
  | Kristallkrebs | 7 |
  | Kellermutter (Endgegner) | 8, Raum mindestens 4 m hoch (sie ist 5,5 m lang) |
- **Schätze:** Waffen, Rüstungsteile, Runen und Tränke aus dem vorhandenen Bestand (F6); dazu Schlüssel je Ebene und
  einzigartige Stücke im Hort. Die Werte macht engine/Inhalt (Lua).
- **Verteilung:** Schätze in Nischen und Truhen; Monster an Monsterplätzen und auf Freepoints. Die Dichte steigt mit
  der Tiefe.

---

## 5. Vertrag mit engine (abgeglichen mit #422, Entscheidung Koordinator 2026-11-23)
Ziel ist, **alles in der Welt-Datei** zu beschreiben, damit Generator und Editor dieselben Daten schreiben. Die Logik
(was mit was verbunden ist) steht in Feldern, nicht in Lua. Lua bleibt für Inhalt (Texte, Belohnungen, Sonderfälle).
Die Mechanik und das genaue Format beschreibt engine in `docs/modules/dungeon.md` (#422). Hier steht, was welt
schreibt.

| Was | Vob und Felder, die welt schreibt | Bemerkung |
|---|---|---|
| **Auslöser → Ziele** | an jedem Auslöser (Hebel, Knopf, Platte, Teleporter, Relais): `targets: ["<Vob-Name>", …]` und `action` (`open`, `close`, `toggle`; für Teleporter und Relais auch `on`/`off`) | Ziele über den Namen, Namen eindeutig je Welt; mehrere Auslöser für ein Ziel verknüpft engines **`relay`** (`mode` `any`/`all`/`count`, `delay`, `once`, `invert`) |
| **Bewegte Teile** | `mesh`- bzw. `mob`-Vob mit Komponente **`mover`**, `kind` = `gate` \| `secret_wall` \| `trapdoor`, `open` (Startzustand); Bahn, Zeit und Klänge nach engines Feldern | **keine eigenen Mob-Typen**; die Kollision bewegt sich mit |
| ├ Fallgatter | `mover`, `kind: gate`, Gitter fährt nach oben | sperrt geschlossen die Wegnetz-Kanten durch die Zelle (Kanten-Sperre über den `mover`-Zustand) |
| ├ Geheimwand | `mover`, `kind: secret_wall` (gleitet nach hinten oder zur Seite bzw. schwenkt) | auch die **Fassfront im Weinkeller** |
| └ Falltür | `mover`, `kind: trapdoor` über einem Schacht; unten im Schacht der Fall als Weltwechsel (`changeWorld` mit `fall`, engine) | Ankunft oben im Schacht der Ebene darunter; die Schachthöhe hält welt klein, also kein Fallschaden (Frage F12) |
| **Hebel** | Mob-Typ **`lever`** (`mobs.toml`), `targets`, `action` | der schiefe Fackelhalter am Riesenfass ist ein `lever` |
| **Versteckter Knopf** | Mob-Typ **`wall_button`**, `hidden: true`: im Fokus erst ab 1,2 m als „Loser Stein“ | öffnet Nischen und Geheimwände |
| **Nische / Depot** | Mob-Typ **`niche`** (Behälter ohne Deckel, Inhalt aus der Mob-Instanz in Lua), meist hinter einer `secret_wall` | |
| **Trittplatte** | `trigger` (flache Box) mit **`plate`**, `targets`, `action`; Gewicht nach engine (`any` = auch Gegenstände) | wie vorgeschlagen |
| **Teleporter** | `trigger` mit **`teleport`**: Ziel in derselben Welt oder `world` + `start`; `filter`, `active`, `visible`; optional `silent` (kein Effekt, Klang, Blitz) und `keep_velocity` (der Held behält seine Geschwindigkeit, z. B. unter einer Falltür; Landung unverletzt wie `changeWorld.fall`) | wie vorgeschlagen; Monster nur innerhalb der Welt |
| **Tür** | vorhandener Tür-Mob; dazu engines Ereignisse (`open`/`close`/`lock`/`unlock`), `no_pick`, `one_way` | Wegnetz-Sperre wie beim Gatter |
| **Treppe auf/ab** | vorhandener Weltwechsel-Trigger (`changeWorld`) + Startpunkte `START_E02_VON_OBEN` usw. | unverändert |
| **Monster** | Vob-Typ **`spawn`** (Instanz, `count`), auf den Monsterplätzen der Karte | Ruhezustand und Wecken macht engine |
| **Umgebung je Welt** | Block **`environment`** (ohne Sonne und Himmel, Grundlicht, Nebel); Format engine | engine übernimmt |
| **Nebel je Zone** | `zones`-Typ **`fog`**; Format engine | engine übernimmt |
| **Wasser** | vorhandener `water`-Vob | Zisterne |

- **Namen der Vobs** (Vorschlag welt, eindeutig je Ebene): `GATE_E01_<x>_<y>`, `WALL_E01_…`, `TRAP_E01_…`,
  `LEVER_E01_…`, `BUTTON_E01_…`, `PLATE_E01_…`, `TELE_E01_…`, `RELAY_E01_…`, `NICHE_E01_…`, `SPAWN_E01_…`; im Keller
  `LEVER_SCHLOSS_FASS` und `WALL_SCHLOSS_FASS`.
- Der Generator schreibt diese Vobs aus der Karten-JSON. Später kommen dieselben Felder aus Blender-Knoten
  (`VOB_…`, ADR 0023), falls Teile von Hand gebaut werden.
- Weitere Wünsche an engine:
  - Abblenden bzw. Ladebildschirm beim Ebenenwechsel;
  - Speichern im Dungeon wie sonst (Frage F8).

---

## 6. Was welt von figuren braucht
- **Monster:** 4–6 neue Dungeon-Arten mit Rig, Clips und Kollisionsmaßen (`monsters.md`, F5-Vertrag).
- **Clips:**
  - Hebel ziehen und versteckten Knopf drücken (`mob/lever`, `mob/wall_button`; engine D3);
  - auf eine Platte treten bzw. sich bücken (optional);
  - Sturz durch eine Falltür und Landen (Ankunft);
  - Nische durchsuchen (wie Truhe).
- **Modelle (optional):** Riesenfass (die Front ist ein eigener Knoten für den `mover`), Thron/Hort-Schatz, besondere Schätze (Items, F6). Den
  Baukasten (Wände, Böden, Gatter, Falltüren …) baut welt selbst.

---

## 7. Phasen
1. **D1 Schloss innen:**
   - Erdgeschoss, 1. OG und Keller begehbar, eingerichtet, mit Wegnetz;
   - Riesenfass mit (vorerst geschlossener) Geheimtür;
   - Geländelöcher.
   - Prüfung: Rundgang und Probe.
2. **D2 Baukasten + Generator + Ebene 1:**
   - alle Mechaniken auf einer Testebene nach dem Vertrag aus Abschnitt 5;
   - engine baut parallel die Mechanik.
3. **D3 Ebenen 2–8** nach Themen, dazu Monster und Schätze; Abnahme Ebene für Ebene.
4. **D4 Feinschliff:** Licht, Musik, Klang, Bench, Editor-Nacharbeit.

---

## 7a. Stand D2 (welt, umgesetzt)
- **Karten** `tools/worldgen/data/leonberg/dungeon/ebene_NN.txt/.json` (Zeichen wie in 3.3). Zusätze in der JSON:
  - Treppen (`<`, `>`) mit `facing` (die Seite, von der man kommt); der Treppenlauf führt in die **Wandzelle
    dahinter** (1,8 m hinauf bzw. hinab, 31°, als Stufen sichtbar, als glatte Rampe kollidierend). Am Ende des Laufs
    liegt der Weltwechsel `STAIRS_E01_<x>_<y>`, auf der Treppenzelle der Ankunftspunkt `arrive` (Blick vom Lauf weg).
  - `starts`: weitere Ankunftspunkte, mit `shaft: true` oben in einem Schacht über der Decke (Ankunft nach dem Fall
    durch eine Falltür, `START_E02_SCHACHT`).
  - optional `environment` (überschreibt die Vorgabe des Themas), `fog` (Nebelzonen über Zellbereichen), `music`,
    `ambient`, `torches` ([x, y, Seite]), `torchGapCells`, `noAutoTorches`.
  - Türen und Gatter brauchen Boden auf zwei gegenüberliegenden Seiten und Wände auf den anderen (sonst ließen sie
    sich umgehen); die Generator-Prüfung bricht sonst ab.
- **Generator** `gothar-worldgen dungeon leonberg [--level N]` (`gothar_worldgen/dungeon/generate.py`):
  - je 8 × 8 Zellen ein Mesh-Vob `BLOCK_E01_<bx>_<by>` (`worlds/leonberg/dungeon/ebene_01/block_<bx>_<by>.glb`):
    Boden, Bruchsteinwände nur dort, wo eine begehbare Zelle an eine feste grenzt, Gewölbe, Treppenläufe,
    Falltürschächte (3 m), Ankunftsschächte, Türwand mit Türöffnung, Nischenöffnung;
  - Kollision: feste Zellen an den Wänden als Kästen je Zeilenlauf, Boden und Decke je Zeilenlauf, Rampe je Lauf;
  - Baukasten `dungeon/kit.py` → `worlds/leonberg/dungeon/kit/*.glb`: `gate`, `secret_wall` (ein Steinblock, der die
    Zelle füllt und **im Boden versinkt**: `offset` [0, −4,1, 0]), `trapdoor` (Angel am Ursprung, Blatt +Z), `lever`,
    `wall_button`, `niche`, `plate`, `teleport` (Steinring); Texturen der Mobs (`furniture/textures`);
  - Vobs nach Abschnitt 5: `mover` (Gatter, Geheimwand, Falltür), Mobs `door`/`lever`/`wall_button`/`niche`/`chest`
    mit `targets`/`action`/`one_way`/`no_pick`, Platten (`plate`), Teleporter mit Ziel-Marker `TELE_…_ZIEL`
    (`empty`), unter jeder Falltür der Fall `FALL_E01_<x>_<y>` (`changeWorld` mit `fall`), über einer Falltür mit
    `plate` die Platte `PLATE_E01_<x>_<y>` (reicht 1 m in den Schacht, damit sie beim Aufklappen gedrückt bleibt),
    `spawn`, Wandfackeln mit Licht (höchstens 8 je Block, mindestens 3 Zellen auseinander);
  - Wegnetz `WP_DNG_E01_<x>_<y>` mit Kanten zu den vier Nachbarn, `FP_ROAM_DNG_E01_<x>_<y>` an Monsterplätzen und in
    Räumen; Zonen `music` `DUNGEON` und `ambient` (Thema, Vorgabe `gewoelbe`) über die Ebene; Block `environment`
    (ohne Himmel). **Keine `indoor`-Zonen:** ohne Himmel ist das Grundlicht der Welt das Licht.
  - Erreichbarkeit (`dungeon/logic.py`) vor dem Schreiben; gleiche Karte, gleiche Bytes; VobIds je Ebene in
    `ebene_NN_vob_ids.json`; Vobs, die dem Generator nicht gehören (Editor), bleiben stehen.
- **Ebene 1** (20 × 15, alle Mechaniken). Ebene 2 ist seit D3 ausgebaut (Abschnitt 7b). Der Zugang: die
  dunkle Öffnung im hinteren Boden des Riesenfasses (`STAIRS_SCHLOSS_FASS` → `START_E01_VON_OBEN`), zurück über die
  Treppe von Ebene 1 auf `START_SCHLOSS_FASS` vor dem Fass. Die Vorkammer ist der erste Raum von Ebene 1.
- **Offen für engine:** Musik-Thema `DUNGEON`, Ambiente `gewoelbe`, Instanz `mon_mauerkriecher` (#440).

## 7b. Stand D3 (welt): Ebene 2 „Verlies“, Ebene 3 als Stub
- **Neu in der Karten-JSON:**
  - `relays`: unsichtbare Relais `RELAY_E02_<NAME>` (`mode` `any`/`all`/`count`, `count`, `targets`, `action`,
    `once`, `invert`, `delay`, optional `at` für die Lage); Schalter und Platten dürfen sie als Ziel nennen. Die
    Erreichbarkeit rechnet sie wie die Engine (Eingänge, Zähler und „verbraucht“ gehören zum Suchzustand).
  - `items`: Item-Vobs `ITEM_E02_<x>_<y>_<k>` (`at`, `instance`, `count`) auf Bodenzellen, nebeneinander je Zelle;
    die Funde der Depots. Truhen und Nischen bleiben vorerst leer (bloßer Typ); ihren Inhalt als Lua-`Mob`-Definition
    liefert engine/Inhalt, welt trägt dann den Namen als `definition` ein.
- **Ebene 2** (24 × 24, 232 begehbare Zellen = 40 %): Ankunft mit Treppe hinauf, Wachstube (Gatterbock; Hebel zum
  Gatter der Osthalle), Osthalle (Mauerkriecher, Truhe, sichtbarer Teleporter in Zelle 3, Hebel A), Zellenflur mit
  vier Zellen im Norden und einer im Süden (Gatter und Hebel am Flur; Zelle 1 Mauerkriecher, Zelle 2 Nische und
  Truhe, Zelle 3 nur über den Teleporter mit Hebel B und einem Hebel für ihr Gatter, Zelle 4 Truhe), Kerkerhalle mit
  Pfeilern (Mauerkriecher, Nische, Truhe), **zwei versteckte Depots** hinter Geheimwänden (lose Steine am Flurende und
  in der Kerkerhalle; Ring, Tränke, Gulden bzw. Amulett und Trank).
  - **Rätsel 1:** Die Platte vor der Schatzkammer hält das Gatter nur offen, solange etwas darauf liegt: einen
    Gegenstand ablegen, dann durchgehen.
  - **Rätsel 2:** Hebel A (Osthalle) und Hebel B (Zelle 3) öffnen gemeinsam über das Relais `all` das Gatter zur
    Treppe hinab.
  - Schatzkammer mit Gatterbock und Truhe, Falltür (Platte `player`) nach Ebene 3; Treppe hinab nach Ebene 3.
  - Erreichbarkeit grün (keine Zelle unerreicht, keine ohne Rückweg).
- **Ebene 3** siehe 7c.

## 7c. Stand D3 (welt): Ebene 3 „Zisterne“, Ebene 4 als Stub
- **Neu in der Karten-JSON:**
  - `water`: `depthM` (Beckenboden unter dem Umgang, Vorgabe 1,0) und `edgeM` (Wasserspiegel unter dem Rand,
    Vorgabe 0,2); je Zeilenlauf ein Wasser-Vob statt je Zelle.
  - `spawns`: Monsterplätze `SPAWN_E03_<x>_<y>` auf beliebigen Boden- und Wasserzellen (der Brunnenmolch lauert im
    Wasser).
  - Teleporter mit `active: false` lassen sich per Ereignis einschalten; die Erreichbarkeit rechnet das mit, dazu
    `release_action` der Platten und `once` der Schalter. `check_reach(level, water=False)` prüft zusätzlich ohne
    Waten: Die Rätsel müssen jede trockene Zelle öffnen.
- **Ebene 3** (28 × 26, 341 begehbare Zellen = 47 %, davon 109 Wasser):
  - Zisternenhalle mit Umgang um ein flaches Becken (Boden 0,9 m tief, Wasser **0,45 m unter dem Rand**, also gut
    0,45 m tief), Steg ins Becken, Insel mit Brunnenschacht (Pfeiler mit Hebel, drei Fackeln). Drei Brunnenmolche
    im Wasser (schwimmen nicht, schnappen vom Wasser nach dem Umgang); Mauerkriecher in Ankunft, Ostkammer und
    Schleusenkammer.
  - **Rätsel 1 (neu, Teleporter einschalten):** Der Hebel in der Pumpenkammer schaltet den Teleporter dort ein; er
    bringt zur Insel. Der Hebel am Brunnenschacht öffnet das Gatter zur Schleusenkammer. Zurück über den Teleporter
    der Insel auf den Umgang. Wer durchs Wasser watet und auf die Insel klettert, kommt auch hin – an den Molchen
    vorbei.
  - **Rätsel 2 (neu, Zählrelais):** drei lose Steine an den Hallenwänden (je einmal), ein Relais `count` 3 öffnet
    erst beim dritten die Geheimwand zum Depot (Goldring, Manatränke, Gulden).
  - Brunnenstube (Truhe, Nische, Trank), Ostkammer (Tür, Truhe), Pumpenkammer (Tür).
  - Schleusenkammer: Treppe hinab und Falltür (Platte `player`) nach Ebene 4; Treppe hinauf nach Ebene 2.
- **Ebene 4** siehe 7d.

## 7d. Stand D3 (welt): Ebene 4 „Beinhaus“, Ebene 5 als Stub
- **Neu in der Karten-JSON:**
  - `props`: Requisiten aus dem Baukasten auf einer freien Bodenzelle `.` (`at`, `kind`, `facing`), Mesh-Vobs
    `PROP_E04_<x>_<y>` mit Kollision im glb. `sarcophagus` (Steinsarg mit Deckel und Kreuz, 2,2 × 0,95 m, längs zu
    `facing`) steht in der Zellmitte und **füllt die Zelle**: kein Wegpunkt, kein Weg hindurch, nicht auf Ankünften,
    Teleporter-Zielen, Funden, Monsterplätzen oder vor Schaltern. `bone_niche` (Beinhaus-Regal mit Schädeln und
    Knochen, 1,9 m breit, 0,42 m tief) steht an der Wand hinter der Zelle (gegenüber `facing`) und lässt sie frei;
    an dieser Wand hängt keine Fackel.
  - Teleporter mit `trap: true`: unsichtbar und ohne Steinring (Falle).
  - Relais mit `delay` rechnet die Erreichbarkeit als **Zeitfenster**: Das verzögerte Ereignis kommt an, nachdem der
    Held `(delay − 2 s) × 4 m/s ÷ 3 m` Zellen gelaufen ist (2 s für den Knopfdruck und die Fahrt des Gatters). Was
    weiter weg liegt, bleibt zu.
  - Ein Wandknopf schickt nur seine `action` (kein Gegenstück wie Hebel oder Platte), wie in der Engine.
- **Ebene 4** (28 × 22, 283 begehbare Zellen = 46 %, davon 10 mit Sarkophag):
  - Ankunft mit Treppe hinauf; Gang nach Osten zur Gräberkammer (Sarkophage, Mauerschlupfer, Truhe,
    Schacht-Ankunft `START_E04_SCHACHT` aus der Falltür von Ebene 3).
  - **Rätsel 1 (neu, Teleport-Falle):** Mitten im Gang liegt ein unsichtbarer Teleporter. Er setzt in die
    Vergessene Gruft (zwei Sarkophage, Mauerschlupfer, eine Fackel als Fund). Hinaus führt nur ein loser Stein; er
    öffnet die Geheimwand zum Gruftgang.
  - Gruftgang mit Knochennischen; daran Gruft 1 (zwei Sarkophage, Nische, Truhe, Tür zur Totengräberkammer mit
    Sarkophagen, Mauerschlupfern und Truhe) und Gruft 3 (Sarkophag, Nische, Truhe, Mauerschlupfer).
  - **Verstecktes Depot** hinter einer Geheimwand in Gruft 3 (loser Stein): altes Amulett, Goldkette, Heiltränke,
    Gulden.
  - **Beinhaus** (8 × 8 mit vier Pfeilern, großer Sarkophag, Knochennischen an allen Wänden, Nische): der
    **Beinhauswart genau einmal** als Zwischengegner.
  - **Rätsel 2 (neu, Zeitgatter):** Der Knopf an der Westwand des Beinhauses öffnet das Gatter zur Treppenkammer;
    das Relais `RELAY_E04_ZEITGATTER` (`any`, `delay` 8 s, `close`) schließt es wieder. Der Weg führt quer durch das
    Beinhaus am Wart vorbei (6 Zellen, Fenster 8). Ein Knopf in der Treppenkammer öffnet es für den Rückweg.
  - Treppenkammer: Treppe hinab nach Ebene 5 (`START_E05_VON_OBEN`), Truhe, Mauerschlupfer, Manatrank.
  - Erreichbarkeit grün; mit 4 s Verzögerung bleibt die Treppenkammer zu, ohne den losen Stein ist die Gruft eine
    Sackgasse (Tests).
- **Ebene 5** siehe 7e.

## 7e. Stand D3 (welt): Ebene 5 „Werkstätten der Erbauer“, Ebene 6 als Stub
- **Neu im Baukasten:** `workbench` (Eichen-Werkbank mit Schraubstock und Werkzeug, füllt ihre Zelle wie ein
  Sarkophag) und `cogwheel` (eisernes Zahnrad des Uhrwerks, 2 m Durchmesser, Nabe in 1,75 m Höhe, hängt an der Wand
  wie eine Knochennische). Allgemein gilt jetzt: Requisiten, die ihre Zelle füllen, stehen in der Mitte; alle anderen
  hängen an der Wand hinter der Zelle.
- **Ebene 5** (Ambiente `werkstatt`; 28 × 24, 324 begehbare Zellen = 48 %, davon 5 mit Werkbank):
  - Ankunft mit Treppe hinauf; Gang nach Osten zur Zahnradhalle (10 × 8, vier Pfeiler, Zahnräder) und zum Lager
    (Mauerschlupfer, Truhe, Hammer).
  - **Rätsel 1 (neu, Rücksetz-Falle):** Im Gang liegt ein unsichtbarer Teleporter (`trap`), der zurück in die Ankunft
    setzt. Ein loser Stein kurz davor schaltet ihn ab (`off`, einmalig).
  - Schmiede (Werkbänke, Nische, Truhe, Mauerschlupfer) südlich der Ankunft; das Gatter zur Halle öffnet nur ein
    Hebel auf der Hallenseite (Abkürzung zurück).
  - **Rätsel 2 (Gewicht und Hebel):** Das Gatter zur Uhrwerkskammer öffnet das Relais `RELAY_E05_GEWICHT` (`all`):
    Ein Gegenstand muss auf der Platte in der Halle liegen (`weight any`), und der Hebel in der Schmiede muss
    umgelegt sein. Ein **Gatterbock** steht als Statue neben dem Gatter.
  - **Verstecktes Depot** hinter einer Geheimwand der Halle (loser Stein): Schutzring, Stärketrank, 200 Gulden,
    Dietriche, Nische.
  - Uhrwerkskammer (Zahnräder, Werkbank, Mauerschlupfer, Truhe, zweiter **Gatterbock** an der Tür zur
    Treppenkammer), Gießerei (Werkbänke, Mauerschlupfer, Nische, Truhe).
  - **Abkürzung nach oben (F6):** In der Treppenkammer schaltet ein Hebel den Teleporter zur Vorkammer von Ebene 1
    (`START_E01_VON_OBEN`) ein; zu Beginn ist er aus.
  - Treppe hinab nach Ebene 6 (`START_E06_VON_OBEN`).
  - Erreichbarkeit grün; ohne den losen Stein bleiben Halle und Treppe unerreichbar, ohne den Hebel der Schmiede die
    Uhrwerkskammer, ohne den Depot-Stein das Depot (Tests).
- **Ebene 6** siehe 7g.

## 7f. Truhen, Nischen und Ambienten (welt, nach engine #491/#494/#496/#502)
- Alle Truhen und Nischen der Ebenen 1–5 tragen ihre Mob-Definition aus `game/scripts/mobs/dungeon.lua`
  (`definition` in der Karten-JSON, z. B. `mob_dng_e2_chest_osthalle`, `mob_dng_e4_niche_beinhaus`); der Inhalt kommt
  aus Lua. Die Zellen stimmen mit engines Liste überein.
- Ambienten je Thema: Ebene 3 `zisterne`, Ebene 4 `beinhaus`, Ebene 5 `werkstatt` (Ebene 1, 2 und 6 weiter
  `gewoelbe`).
- Damit sind die engine-Punkte unter 7a–7e erledigt (Instanzen `mon_brunnenmolch` und `mon_beinhauswart` sind auf
  main, die Ebenen laden ohne Warnung).

## 7g. Stand D3 (welt): Ebene 6 „Labyrinth“, Ebene 7 als Stub
- **Neu in der Karten-JSON:** Teleporter mit `spin: true` (**Drehfalle**): Der Ziel-Marker liegt in der eigenen Zelle
  und blickt nach `facing`; der Held bleibt stehen und ist danach in eine andere Richtung gedreht. Mit `trap: true`
  ist sie unsichtbar. Die Engine setzt ihn „schon drin“, er springt also nicht erneut. Für die Erreichbarkeit ist sie
  Boden.
- **Ebene 6** (31 × 29, 467 begehbare Zellen = 52 %): ein Irrgarten aus einem festen Zufallsbaum (Startwert 6) mit
  22 zusätzlichen Durchbrüchen als Schleifen; in der Karte bleibt er Text wie jede Ebene.
  - Ankunft (Treppe hinauf), Mittelhalle mit Pfeiler.
  - **Teleporter-Türen:** zwei sichtbare Teleporter verbinden eine Sackgasse im Nordwesten mit einer im Südosten;
    ein dritter führt aus der Mittelhalle in die abgeschlossene **Hebelkammer** (Südwesten), ein vierter von dort
    zurück.
  - **Drehfallen:** vier unsichtbare an Kreuzungen.
  - Der Hebel der Hebelkammer öffnet das **Gatter zum Treppenraum** (Treppe hinab nach Ebene 7).
  - **Geheimwände:** Ein loser Stein öffnet die Wand zu einer Sackgasse mit Truhe; ein zweiter öffnet das
    **versteckte Depot** im Nordosten (Goldring, Heiltränke, Gulden, Pfeile, Nische).
  - **Mauerschlupfer** in Sackgassen, Mittelhalle, Hebelkammer und Treppenraum (6 Plätze, 9 Tiere).
  - Erreichbarkeit grün; ohne den Hebel bleibt die Treppe, ohne die losen Steine bleiben Depot und Truhen-Sackgasse
    zu (Tests).
- **Ebene 7** als Stub (Thema `grotte`, vorerst Ambiente `gewoelbe`).
- Truhen und Nischen tragen ihre Definitionen `mob_dng_e6_…`, Ambiente `labyrinth` (engine #514). Offen für
  engine: Ambiente `grotte` (Ebene 7).

## 7h. Stand D3 (welt): Ebene 7 „Kristallgrotte“, Ebene 8 als Stub
- **Höhle statt Mauerwerk** (Thema `grotte`, nur dort; Ebenen 1–6 bleiben bytegleich):
  - Wände aus Fels (`cave_wall`, eigene Textur `cave_rock`: Knollen in mehreren Größen, Risse, Bänder, Poren,
    helle Mineralflecken, kräftige Normalen): jede Wandfläche ist ein feines Raster (10 × 12), dessen Punkte bis
    0,7 m in die feste Zelle zurückweichen. Das Maß ist Rauschen aus zwei weichen Gittern (1,4 m und 0,55 m)
    und etwas Körnung, also Wölbungen, Mulden und Überhänge statt großer Dreiecke; Normalen geglättet. Fester
    Ganzzahl-Hash der Position, gleiche Bytes auf jedem System. Am Boden, an der Decke, an Ecken, Wasserkanten
    und Nischen läuft der Fels über 0,6 m auf die Zellkante aus, damit keine Spalten entstehen. Die Kollision
    bleibt der Kasten der Zelle; der Fels ragt nie in den Weg.
  - Felsdecke (`cave_ceiling`) ohne Gurtbögen, je Zelle 8 × 8 Felder, bis 0,9 m über der Deckenhöhe; zu Wänden,
    Schächten und Treppen hin läuft sie auf die Deckenhöhe aus.
  - Felsboden (`cave_floor`). Grundlicht kühl blau (`ambient` 0,1/0,14/0,2, `ambient_ground` 0,05/0,075/0,1),
    Nebel ab 4 m bis 30 m: hell genug, um die Höhle ohne Fackel zu lesen, deutlich heller als Ebene 1–6.
- **Neu in der Karten-JSON** (für alle Themen nutzbar):
  - `heights`: Rechtecke (`from`, `to`, `dy`, −1,5 bis +0,6 m) heben oder senken den Boden; die Ecken nehmen den
    Mittelwert der Zellen ringsum, der Boden ist also eine stetige Fläche aus zwei Dreiecken je Zelle (Kollision
    als Hülle je Zelle, gleiche Diagonale). Nachbarn unterscheiden sich um höchstens 0,6 m. Zellen mit Mechanik
    (Tür, Gatter, Falltür, Teleporter, Platte, Geheimwand, Treppe, Truhe, Standplatz vor Schalter und Nische)
    liegen eben mit allen acht Nachbarn; Tür, Gatter, Falltür, Geheimwand und Treppe auf 0, ebenso alles am Wasser.
  - `water.shallow` (Rechtecke) mit `shallowM`/`shallowEdgeM`: flaches Wasser neben dem tiefen Becken; je Tiefe
    eigene Wasser-Vobs.
  - `jetty`: Bodenzellen als Steg (Dielen, Pfosten im Wasser).
  - Requisiten `stalagmite` (füllt die Zelle), `stalactite` (hängt über der Zellmitte, wurzelt 1 m im Fels, Spitze
    0,95 m unter der Decke, die Zelle bleibt frei), `crystal_cluster` (bis 1,8 m hoch, an der Wand),
    `crystal_ceiling` (Kristalle hängen von der Decke, die Zelle bleibt frei) und `crystal_spire` (auf dem Grund
    einer Wasserzelle, die Spitzen ragen aus dem Wasser, ohne Kollision). Jede Kristall-Requisite bringt ein
    blau-türkises Licht `LIGHT_E07_<x>_<y>` mit (Farbe 0,35/0,8/1,0, Reichweite 10 m, Stärke 9, ohne Flackern;
    am See knapp über dem Wasser). Höchstens 8 Lichter je Block aus 8 × 8 Zellen (Fackeln und Kristalle
    zusammen), sonst bricht der Generator ab.
  - `look: "crystal"` an einer Geheimwand (Modell `crystal_wall`: Fels mit Kristallen) und an einem Wandknopf
    (`crystal_switch`: Kristall in einer Felsfassung, sichtbar, nicht versteckt).
  - **Falltür in dieselbe Ebene:** `fall` nur mit `start` (einem `starts`-Eintrag mit `shaft`). Statt des
    Weltwechsels liegt unten im Schacht ein Teleporter `FALL_E07_<x>_<y>` mit `to` = Ankunft, `visible: false`,
    `silent: true` und `keep_velocity: true` (neue Felder, Entscheidung Koordinator 2026-11-27: engine2 baut sie;
    bis dahin ein gewöhnlicher Teleport). Die Ankunft liegt 3,3 m über dem Boden unter dem Ankunftsschacht
    (höchstens 0,2 m unter der Decke), der Sturz bleibt also unter 4 m und kostet nichts. Grund: ein `changeWorld`
    in die eigene Welt lädt die Ebene neu und verliert ihren Zustand (engine behebt das getrennt).
    `check_reach` macht aus der Falltür eine Kante zur Ankunftszelle; von dort braucht es einen Rückweg.
- **Ebene 7** (30 × 25, 310 begehbare Zellen = 41 %, davon 36 Wasser; keine Fackeln, 28 Kristall-Lichter: 16 an
  Wänden, 8 an der Decke, 4 im See; über jeder Ankunft hängen Kristalle):
  - Ankunft (Treppe hinauf), Gang zur **Kristallhalle** (Stalagmiten, Stalaktiten, Bodenwellen, Kristallkrebse).
  - **Rätsel 1 (Kristallwand):** Ein loser Stein in der Halle senkt die Kristallwand; dahinter die
    **Kristallkammer** mit Depot (Nische; Schutzring, Manatrank, 250 Gulden) und dem **Kristallschalter**. Er
    öffnet das Gatter zur Treppenkammer.
  - **See-Grotte:** See (1,4 m Becken, Wasser 1,1 m tief) mit flachem Wasser am Westufer, Steg zur Insel (Truhe,
    Pfeile), zwei **Brunnenmolche** im Wasser, Kristallkrebs und Truhe am Nordufer.
  - **Rätsel 2 (Falle mit Rückweg):** Der Westgang der Halle endet an einer Falltür (Platte `player`), hinter der
    Kristalle leuchten. Der Sturz endet in der **Tiefen Grotte**, 1,2 m tiefer (Schacht-Ankunft
    `START_E07_SCHACHT_UNTEN`; Truhe, Heiltrank, Kristallkrebs, Mauerschlupfer). Zurück geht es den Hang hinauf;
    das Gatter zur See-Grotte öffnet ein Hebel auf der Seite der Tiefen Grotte. Wer den Hebel einmal umgelegt hat,
    kommt später auch ohne Sturz hinein.
  - **Treppenkammer:** Truhe, Mauerschlupfer, **Abkürzung** (Teleporter zur Vorkammer von Ebene 1,
    `START_E01_VON_OBEN`, zu Beginn aus, Hebel daneben) und Treppe hinab nach Ebene 8 (`START_E08_VON_OBEN`).
  - Erreichbarkeit grün, auch ohne Waten; ohne den losen Stein bleiben Kammer, Gatter und Treppe zu, ohne den
    Kristallschalter die Treppenkammer, ohne den Hebel unten hat die Tiefe Grotte keinen Rückweg (Tests).
  - Truhen und Nische mit ihren Definitionen (engine #521): [1,20] `mob_dng_e7_chest_absturz` (Tiefe Grotte),
    [20,13] `mob_dng_e7_chest_seeufer`, [18,17] `mob_dng_e7_chest_insel`, [25,21] `mob_dng_e7_chest_treppe`,
    Nische [24,1] `mob_dng_e7_niche_kristall` (Kristallkammer). Ambiente `grotte` (engine #517).
- **Ebene 8** als Stub (Thema `hort`, Raumhöhe 5 m für die Kellermutter, vorerst Ambiente `gewoelbe`).
- Offen für engine: Felder `silent`/`keep_velocity` am Teleporter.

## 8. Entscheidungen des Projektinhabers (2026-11-23: alle nach Vorschlag)
| Nr. | Frage | Entscheidung |
|---|---|---|
| F1 | Wer wohnt im Schloss? | Vogt, zwei Wachen, Koch, Schreiber mit Routinen; der Vogt mit Dialogen (Inhalt engine/Lua) |
| F2 | Welche Geschosse begehbar? | EG, 1. OG, Keller; der Rest später |
| F3 | Wie wird der Zugang gefunden? | Riesenfass im Weinkeller, Hebel = schiefer Fackelhalter, Hinweis in der Bibliothek, Luftzug |
| F4 | Wie tief? | 8 Ebenen zu je 24–32 × 24–32 Zellen, später erweiterbar |
| F5 | Raster-Steuerung oder frei? | **frei** wie in Gothic (Third Person, Kampf); das Raster ist nur die Bauweise |
| F6 | Rückweg? | Treppe hinauf auf jeder Ebene, ab Ebene 3 alle zwei Ebenen eine Abkürzung, die man von unten öffnet |
| F7 | Monster? | 4–6 neue eigene Dungeon-Arten (figuren), dazu Schinder/Quaderbuckel; ein eigener Endgegner auf Ebene 8 |
| F8 | Speichern im Dungeon? | überall wie draußen (Gothic) |
| F9 | Stimmung? | dunkel, still, bedrohlich; Fackel nötig; wenige, gefährliche Gegner statt vieler; Rätsel aus Schaltern und Platten wichtiger als Kampf |
| F10 | Welten: eine je Ebene oder übereinander? | eine `.g7world` je Ebene (Ladebildschirm, Monster folgen nicht) |
| F11 | Respawn? | nein (wie Gothic): Tote bleiben tot, Schätze einmalig |
| F12 | Fallschaden beim Sturz durch eine Falltür? | nein: kurzer Schacht, man landet unversehrt eine Ebene tiefer; die Falle kostet den Weg zurück, nicht Lebenspunkte |
