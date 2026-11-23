# dungeon – Mechanik für Schloss, Keller und Dungeon (Plan, wartet auf Freigabe)

Auftrag des Projektinhabers (über den Koordinator, 2026-11-23): Das Schloss wird begehbar, mit Keller und einem
versteckt zugänglichen Dungeon im Geist klassischer Grid-Dungeon-Crawler („wie Dungeon Master“). Übernommen werden
nur Mechanik und Spielgefühl, alle Inhalte sind eigen. Vorgesehen sind viele Ebenen, Falltüren, Teleporter, Türen
und Gatter, versteckte Depots in den Wänden, Schalter und Trittplatten, Monster und Schätze. welt plant parallel den
Bau (Schloss, Keller, Ebenen). Dieses Dokument plant die **Engine-Mechanik**. **Kein Code bis zur Freigabe.**

**Berührt Verträge mit welt:** neue Vob-Komponenten in `.g7world` und neue `VOB_`-Knoten aus Blender (ADR 0023).
Siehe „Vertrag mit welt“; welt schickt seine Wünsche, der Koordinator gleicht ab.

## Ausgangslage
- **Bewegung** frei wie in Gothic (M5), nicht in Feldern. Der Dungeon ist also *im Geist* ein Grid-Crawler: Die
  Räume folgen einem 3-m-Raster (Vorschlag für welt, Frage 1), die Steuerung bleibt Third-Person.
- **Vorhanden:**
  - Mobs mit Schloss und Schlüssel, Türen, die mit der Kollision drehen (M8);
  - Trigger-Vobs mit `onEnter`/`onLeave`, `filter`, `once` und einem **reservierten Feld `target`** (M4);
  - Weltwechsel-Trigger `changeWorld` mit Startpunkt (M4), Spielstände mit dem Zustand besuchter Welten (M15);
  - Wegnetz und Freepoints (M9), Tier- und Monster-KI mit Revier, Rudel und Tag/Nacht (M9 F, M12);
  - Fallschaden ab 4 m (M5), Fackel (M13);
  - Editor (M16) und Import von `VOB_<typ>_<name>`-Knoten aus Blender (ADR 0023).
- **Gothic-Bezug:** ZenGin hatte dafür `zCMover` (bewegte Teile mit Schlüsselstellungen), `zCTrigger`,
  `zCTriggerList` (Reihen und Verknüpfungen), Nachrichten an Ziel-Vobs per Name und `oCTriggerChangeLevel`. Der Plan
  folgt diesem Muster: **Auslöser schicken Ereignisse an benannte Ziele**, das Verhalten steht in den Daten.

## Bausteine und ihr Verhalten
Alle Bausteine sind Vobs mit einer Komponente in `.g7world` (Felder unten). Ihr Zustand wird gespeichert.

### Ereignisse und Ziele (das Netz)
- Jeder Auslöser hat `target` (ein Vob-Name oder eine Liste) und `event`:
  - `open`, `close`, `toggle` (Mover, Türen, Gatter);
  - `on`, `off` (Schalter, Lichter, Teleporter);
  - `trigger` (Relais, Lua).
- **Relais** (`relay`, wie `zCTriggerList`), ein unsichtbarer Vob:
  - `mode`: `any` (jedes Eingangsereignis weitergeben), `all` (erst wenn alle Eingänge an sind: Rätsel mit mehreren
    Platten) oder `count` (nach n Ereignissen);
  - `delay` (Sekunden), `once`;
  - `invert` (an wird aus: eine Platte *hält* ein Gatter offen).
- **Lua** für alles, was sich nicht verdrahten lässt:
  - Ereignis `dungeon_event(target, event, source)`;
  - Funktionen `send(target, event)`, `mover_state(name)`, `switch_state(name)`;
  - Hook `on_event` an Mob-Definitionen.
- Die Ereignisse laufen im festen Schritt. Ein Netz mit Zyklus wird beim Laden gemeldet (Warnung) und je Schritt nur
  einmal durchlaufen.

### Bewegte Teile – `mover` (Gatter, Geheimwand, Falltür, Plattform)
- Ein `mesh`- oder `mob`-Vob mit `mover`:
  - `kind` = `slide` (Verschiebung `offset` [x, y, z]) oder `rotate` (`axis`, `angle`, um den Ursprung);
  - `seconds` für den Weg, `open` (Startzustand), Klänge `sound_move` und `sound_stop`;
  - `crush` (was geschieht, wenn jemand im Weg ist, Frage 3).
- Die Kollision bewegt sich mit, je Schritt neu gesetzt wie bei den Türen heute.
- **Fallgatter:** `slide`, nach unten. **Geheimwand:** `slide` nach hinten oder zur Seite. **Falltür:** `rotate` nach
  unten, darunter ein Schacht (siehe Ebenenwechsel). **Hebebühne:** `slide` nach oben, später.

### Schalter – Mob-Typen `lever` und `wall_button`
- Benutzen wie jedes Mob (M8): Der Held geht hin, spielt `lever_use` bzw. `button_press` und schickt beim Clip-Event
  `use` `event` an `target`.
- Hebel haben zwei Stellungen (`toggle`). Knöpfe sind einmalig oder federn zurück (`once`).
- **Versteckter Knopf:** `hidden = true`. Er erscheint im Fokus erst aus der Nähe (1,2 m statt 3 m) und hat keinen
  Namen, bis er einmal gefunden ist („Loser Stein“), Frage 5.
- Für die Mob-Typen braucht es Zeilen in `mobs.toml` (Vertrag engine–figuren–welt) und zwei kurze Clips von figuren.

### Trittplatten – Trigger mit `plate`
- Ein `trigger`-Vob (flache Box) mit `plate = { weight = "any" | "creature" | "player" }`.
- **Gedrückt**, solange etwas darauf liegt bzw. steht:
  - der Held;
  - ein NPC oder Monster;
  - mit `any` auch **Gegenstände**: ein Item-Vob, das darauf liegt; auch ein fallen gelassenes, ein per Telekinese
    abgelegtes, ein geworfenes.
- Ereignisse `press` beim ersten Gewicht, `release` beim letzten (oder `event` und `release_event` frei wählbar).
- Klang und eine kleine Absenkung des Modells um 2 cm als Rückmeldung (ein kleiner `mover`).
- **Gewicht** in kg nur, falls Frage 4 es verlangt. Der Vorschlag ist „etwas liegt darauf“.

### Türen mit Schlüssel
- Die vorhandenen Tür-Mobs (Schloss, Schlüssel, Dietrich) bleiben.
- Neu:
  - Ereignisse `open`, `close`, `lock`, `unlock` an Türen (ein Hebel öffnet eine Tür);
  - `no_pick = true` (Dietrich hilft nicht, nur der Schlüssel; für Dungeon-Tore);
  - `one_way` (nur von einer Seite zu öffnen).

### Falltür und Grube
- Eine Falltür (`mover` rotate) über einem Schacht, ausgelöst von einer Platte oder einem Relais, oder offen von
  Anfang an.
- Wer in den Schacht fällt, landet in der **Ebene darunter** (siehe „Ebenenwechsel“).
- Gruben ohne Ebene darunter sind einfach tiefe Löcher mit Fallschaden (M5).

### Teleporter
- Ein `trigger`-Vob mit `teleport`:
  - Ziel `to` (ein Start- oder Marker-Vob in derselben Welt) oder `world` + `start` (eine andere Ebene);
  - `visible` (Lichtsäule als Partikel und Klang) oder unsichtbar;
  - `filter` `player` / `creature` / `items` / `any` (wie in Dungeon Master: auch Monster und Gegenstände);
  - `yaw` (Blickrichtung nach der Ankunft, sonst die des Ziels);
  - `active` (kann per Ereignis an- und ausgehen).
- **Ablauf:** kurzer Blitz (Weißblende 0,2 s, Effekt `teleport`), dann versetzt. In derselben Welt ohne Ladezeit, in
  eine andere Ebene als Weltwechsel.
- Kein Ping-Pong: Am Ziel gilt das Prinzip „schon drin“ wie bei den Weltwechsel-Triggern (`prime`).

### Geheimwände, Nischen und Depots
- Eine **Nische** ist ein Mob `niche` (Truhe ohne Deckel, Inhalt als Lua-`Mob`-Definition wie bei Truhen) hinter
  einer Geheimwand (`mover`).
- Oder eine offene Nische mit Item-Vobs.
- **Finden** (Frage 5): durch einen versteckten Knopf in der Nähe, durch eine Platte, durch ein Rätsel (Lua). Im
  Vorschlag gibt es *keine* Suchtaste und *keine* Wahrnehmungsprobe: Man findet sie durch Hinsehen (leicht andere
  Steine, ein Riss, ein Luftzug als Klangzone).

### Monster im Dungeon
- Die vorhandene KI (Tiere und Monster, M9 F/M12) mit Revier um den Ort ihres Erscheinens und dem Wegnetz der Ebene
  (welt).
- **Ruhe und Erwachen:** neuer Zustand `zs_dormant`. Das Monster liegt bzw. steht still und schläft, bis es den
  Helden wahrnimmt (Sicht oder Geräusch; schlafend mit halber Reichweite, M9-Wahrnehmung) oder Schaden nimmt. Erwacht
  bleibt es wach. Ein Ereignis kann ganze Räume wecken (`send("raum_3", "wake")`).
- **Erscheinen:**
  - Marker-Vobs `spawn` mit `npc` (Instanz), `count`, `respawn` (Frage 8);
  - oder Lua beim Laden der Ebene (`world_loaded`).
- Monster wechseln die Ebene nicht. Teleporter mit `creature` versetzen sie nur innerhalb der Welt (Frage 9).

### Schätze
- Inhalt in Lua (`game/scripts/dungeon/`): Truhen- und Nischen-Definitionen mit Inhalt, Schlüssel als Items, Funde
  als Item-Vobs.
- Wertvolles in Tiefe und Verstecken, keine Zufallsbeute (Gothic-Stil, Frage 10).

### Zustand speichern (M15)
- `WorldState` bekommt dazu:
  - `movers` (offen bzw. Stellung 0..1);
  - `switches` (Stellung, gefunden);
  - `relays` (Zähler, Eingänge an, `once` verbraucht);
  - `plates` (gedrückt, ergibt sich aus dem Gewicht beim Laden);
  - `teleporters` (an/aus);
  - Monster wie bisher die NPCs (lebend, tot, wach).
- Ältere Spielstände ohne diese Felder laden den Anfangszustand der Welt.

## Ebenenwechsel
| | **Eine `.g7world` je Ebene** (Vorschlag) | Alle Ebenen in einer Welt |
|---|---|---|
| Laden | jede Ebene einzeln, klein (Ziel ≤ 2 s im Release) | einmal alles |
| Fall durch Falltür | Weltwechsel mit Fallgeschwindigkeit (unten) | echter Fall ohne Unterbrechung |
| Speicher und Zeichnen | nur die aktuelle Ebene | alle Ebenen im Speicher, Culling über Ebenen nötig |
| Vorhandene Mechanik | Weltwechsel und Zustand besuchter Welten (M15) gibt es schon | neue Ebenen-Sichtbarkeit nötig |
| welt | Ebene für Ebene erzeugbar und testbar | eine große Datei |

- **Vorschlag:** Schloss und Keller als Teil von Leonberg (begehbar wie die Häuser) bzw. als eigene Welt
  `schloss_keller` (welt entscheidet nach Größe). Jede Dungeon-Ebene ist eine eigene Welt `dungeon_<n>.g7world`.
- **Fall als Weltwechsel:**
  - Unten im Schacht liegt ein `changeWorld`-Trigger mit `fall = true`.
  - Die Engine merkt sich die Fallgeschwindigkeit und lässt den Helden in der neuen Ebene am Startpunkt (oben im
    Ankunftsschacht) mit dieser Geschwindigkeit weiterfallen. Der Fallschaden zählt die ganze Höhe (Frage 6).
  - Statt des Ladebildschirms ein schwarzes Abblenden, solange das Laden unter etwa 2 s bleibt (Frage 7).
- Ebenen verbinden auch Treppen und Teleporter. Ein Rückweg nach oben muss es immer geben (Frage 11).

## Vertrag mit welt (Vorschlag, welt gleicht ab)
- **Neue Komponenten** in `.g7world`, je an Vobs der vorhandenen Typen:
  - `components.mover` (an `mesh`/`mob`);
  - `components.trigger.plate`;
  - `components.trigger.teleport`;
  - `components.relay` (neuer Vob-Typ `relay`);
  - `components.spawn` (neuer Vob-Typ `spawn`);
  - das schon reservierte `trigger.target` mit `event`;
  - `mob`-Definitionen `lever`, `wall_button`, `niche` in `mobs.toml`.
- **Blender (ADR 0023):** leere Knoten `VOB_<typ>_<name>`, also `VOB_plate_`, `VOB_teleport_`, `VOB_relay_`,
  `VOB_spawn_`, `VOB_start_` (Ziele). Die Felder kommen als glTF-`extras` am Knoten (`target`, `event`, `to`,
  `world`, `mode` …). Bewegte Teile sind Mesh-Knoten mit `extras.mover`. Die Engine liest beim Import alle
  `extras`-Felder in die Komponenten; unbekannte Felder werden gewarnt.
- **Raster (Frage 1):** Räume auf einem 3-m-Raster, Gänge 3 m breit, Decken 3–4 m. Das erleichtert Bau, Wegnetz und
  das Gefühl, ist aber keine Pflicht der Engine.
- **Wegnetz** je Ebene wie in Leonberg. Monster laufen darauf. Bewegte Teile, die Wege versperren, sperren die Kante
  (Kanten-Sperre über den `mover`-Zustand).
- **Licht:** dunkel, Fackeln an Wänden als Lichter (vorhanden), der Held mit Fackel.

## Editor (M16)
- Komponenten im Inspektor: Ziel-Auswahl per Name mit Liste. Linien vom Auslöser zum Ziel im Debug-Draw (Farbe je
  Ereignis). `mover` mit Vorschau „offen“/„zu“. Teleporter mit Pfeil zum Ziel.
- **Prüfen** beim Speichern: fehlende Ziele, Zyklen, Teleporter ohne Ziel, Platten ohne Ziel; Liste im Fenster.
- **Probelauf** im Editor: Ereignis an ein Ziel schicken (Knopf), Mover bewegen.

## Teile (je ein PR, in dieser Reihenfolge)
| Teil | Inhalt | Umfang |
|---|---|---|
| **D1** Netz | `target`/`event`, Relais, Lua `send`/`dungeon_event`, Speichern der Relais | mittel |
| **D2** Mover | Gatter, Geheimwand, Falltür; Kollision mit; Klemmen (`crush`); Speichern | mittel |
| **D3** Schalter und Platten | Mob-Typen `lever`, `wall_button` (versteckt), Platte mit Gewicht inkl. Items; figuren-Clips | mittel |
| **D4** Teleporter und Fall | Teleport in der Welt und in andere Ebenen, Fall als Weltwechsel mit Geschwindigkeit, Abblenden | mittel |
| **D5** Nischen und Türen | `niche`, Tür-Ereignisse, `no_pick`, `one_way` | klein |
| **D6** Monster | `zs_dormant`, Wecken, `spawn`-Marker, kein Respawn | mittel |
| **D7** Editor und Import | Inspektor, Linien, Prüfen, `extras` aus Blender | mittel |
| **D8** Testdungeon und Szenario | zwei kleine Ebenen der Engine (`testworld/dungeon_1/2`), DoD-Szenario | mittel |

Nach D1–D3 kann welt mit echten Räumen bauen. D8 ersetzt welts Inhalt nicht, er prüft nur die Mechanik.

## Tests
- **CPU:**
  - Relais (`any`, `all`, `count`, `delay`, `invert`, Zyklus);
  - Mover-Bahn und Klemmen;
  - Platten-Belegung (Held, Monster, Item darauf und weg);
  - Teleport-Ziel und `prime`; Lesen und Schreiben der Komponenten (`world_format`);
  - Spielstand mit allen Zuständen.
- **Szenario** (`tests/runtime/test_engine_dungeon_scenario.cpp`, Testdungeon, headless):
  - Ein Hebel öffnet ein Gatter.
  - Ein abgelegter Gegenstand hält eine Platte und damit eine Tür offen; nimmt man ihn weg, schließt sie.
  - Eine Falltür wirft den Helden in Ebene 2 (er kommt mit Fallgeschwindigkeit an).
  - Ein unsichtbarer Teleporter bringt ihn zurück.
  - Ein versteckter Knopf öffnet eine Geheimwand mit einer Nische und deren Schatz.
  - Ein schlafendes Monster erwacht beim Näherkommen.
  - Speichern mitten im Rätsel und Laden stellt alles wieder her.
- **Prüfbilder:** Gatter auf/zu, Geheimwand offen, Teleporter sichtbar, Falltür, Fackelschein im Gang.

## Gefühlsfragen an den Projektinhaber (je mit Vorschlag)
1. **Raster:** Räume und Gänge auf einem 3-m-Raster, Steuerung frei wie im Rest des Spiels. *Vorschlag: ja, Raster als
   Bauregel für welt, keine Feld-Bewegung.*
2. **Gatter, die jemanden treffen:** *Vorschlag: Das Gatter hält an und fährt wieder auf, kein Schaden (ein Spieler
   soll nicht durch Pech sterben). Als Falle eigens markierte Gatter (`crush = "damage"`) treffen mit 20 Schaden und
   fahren dann auf.*
3. **Geheimwände und bewegte Teile:** dieselbe Regel wie bei 2 (halten an, kein Zerquetschen).
4. **Trittplatten:** *Vorschlag: „Etwas liegt darauf“ genügt: Held, Monster oder ein Gegenstand. Kein Gewicht in kg.
   So lassen sich Platten mit einem abgelegten Stein oder Apfel überlisten, wie in Dungeon Master.*
5. **Verstecke finden:** *Vorschlag: durch Hinsehen. Versteckte Knöpfe erscheinen erst aus 1,2 m im Fokus, als
   „Loser Stein“, und die Wand sieht leicht anders aus. Keine Suchtaste, kein Talent.*
6. **Fall durch Falltür:** *Vorschlag: normaler Fallschaden über die ganze Höhe (ab 4 m wie überall), die Schächte
   sind so gebaut, dass es weh tut, aber nicht tötet (etwa 5–6 m).*
7. **Laden zwischen Ebenen:** *Vorschlag: kurzes Schwarz statt Ladebildschirm, wenn das Laden unter etwa 2 s bleibt,
   sonst der Ladebildschirm.*
8. **Monster nachwachsen:** *Vorschlag: nein, wie in Gothic 1. Einmal geleert, bleibt eine Ebene leer, bis auf
   ausdrücklich markierte Nester.*
9. **Monster und Teleporter bzw. Ebenen:** *Vorschlag: Monster folgen dem Helden nicht durch Teleporter oder Falltüren
   in andere Ebenen. Teleporter innerhalb einer Ebene können sie versetzen, wenn markiert.*
10. **Schätze:** *Vorschlag: feste Schätze an festen Orten (Lua), die besten tief unten und in Verstecken. Keine
    Zufallsbeute.*
11. **Rückweg:** *Vorschlag: Jede Ebene hat mindestens einen Weg nach oben (Treppe oder Teleporter). Keine Sackgasse,
    die das Spiel unspielbar macht; Falltüren dürfen in Bereiche führen, aus denen man nur durch die Ebene heraus
    kommt.*
12. **Zugang:** *Vorschlag: Der Dungeon-Eingang im Schlosskeller ist versteckt (Geheimwand mit verstecktem Knopf) und
    bekommt einen Hinweis im Spiel (Buch oder Gerücht von einem NPC). Wer ihn auslöst, entscheidet die Geschichte.*
13. **Dunkelheit:** *Vorschlag: ohne Fackel oder Licht-Zauber sehr dunkel (wie Gothic-Höhlen). Nicht völlig schwarz,
    damit man ohne Fackel nicht festsitzt.*
14. **Tageszeit:** *Vorschlag: im Dungeon keine Tageszeit zu sehen (Innenzone), die Spielzeit läuft weiter; Monster im
    Dungeon haben keinen Tag-Nacht-Rhythmus.*
