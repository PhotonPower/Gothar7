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
| **Teleporter** | `trigger` mit **`teleport`**: Ziel in derselben Welt oder `world` + `start`; `filter`, `active`, `visible` | wie vorgeschlagen; Monster nur innerhalb der Welt |
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
