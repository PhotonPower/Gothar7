# tools

## g7-cook (M3)
Kommandozeilen-Werkzeug zum Kochen und Packen von Assets (`tools/asset-cooker`, Bibliothek `g7_cook_lib`,
Tests in `tests/cook`). Stand und Optionen: `docs/06-asset-pipeline.md`; Formate: ADR 0016.

## Editor (M4 Grundlage, M16 vollständig)
Editor-Modus der Engine (`gothar --editor`), ImGui-basiert – Gegenstück zu Gothics **Spacer**. Plan M16:
`docs/modules/editor.md` (Teile A–F, ADR 0022/0023).

**Stand M4 (umgesetzt):** `tools/editor` (Bibliothek `g7_editor`, im Spiel gelinkt). Die Engine kennt nur
`EngineTool` (`runtime/EngineTool.hpp`: `update` je Frame, `ui` im Debug-UI-Frame); das Spiel registriert den
Editor bei `--editor`. Fenster als `ui::EditorPanel` (ImGui bleibt in `ui`, ADR 0015).
- Start: Simulation pausiert, Debug-UI (F1) und Overlay (F2) an. Maus links = auswählen/Griffe ziehen, rechts =
  umsehen (Flugkamera).
- **Gizmos (eigene, ADR 0015-Nachtrag):** Verschieben mit Achsen-Pfeilen und Ebenen-Quadraten, Drehen mit Ringen,
  Skalieren mit Achsen und Mittelquadrat (gleichmäßig); Hover hebt den Griff gelb hervor. Treffertest in
  **Bildschirm-Pixeln** (Arme 90 px, Toleranz 8 px) – gleich gut greifbar in jeder Entfernung. Raster 0,5 m / 15°
  (Taste X), lokale oder Welt-Achsen (L), Modus 1/2/3. Mathematik in `Gizmo.hpp`, CPU-getestet.
- **Auswahl:** Strahl gegen die Bounds der gezeichneten Meshes und kleine Boxen um Startpunkte, Lichter, Sounds;
  Trigger über ihre (gedrehte) Box; Liste aller Vobs als Baum. Esc hebt sie auf.
- **Inspektor:** Name, Position/Drehung (Grad)/Skalierung in Weltkoordinaten (Eltern bleiben), je Typ Kategorie,
  Licht, Sound, Trigger (inkl. Levelwechsel), Mob-Definition.
- **Modelle:** alle `.glb`/`.g7mesh` im VFS, Filter; Klick setzt einen mesh-Vob 8 m vor die Kamera auf den Boden
  (Geländehöhe), Name aus dem Dateinamen, ID aus `nextVobId`. Duplizieren (Strg+D), Löschen (Entf; IDs bleiben
  verbraucht).
- **Speichern** (Strg+S): zurück in die Quelldatei, nur lose Dateien (aus `.g7pak` = Meldung). Beim ersten
  Speichern je Sitzung bleibt die vorige Fassung als `<datei>.bak` (Undo gilt nur in der Sitzung). Gelände, Wegnetz,
  Zonen und `generator`-Kopf der Welt bleiben erhalten.
- **Undo/Redo (M16 Teil A, ADR 0022):** Strg+Z, Strg+Y bzw. Strg+Umschalt+Z, dazu Knöpfe mit dem Namen des
  Schritts („move ROCK_LARGEA (171)“).
  - Jede Bearbeitung ist ein Befehl (`editor/Commands.hpp`). Er hält den Zustand der berührten Vobs vorher und
    nachher als ihre Zeile der Weltdatei (`writeVobJson`), Kinder eingeschlossen.
  - Undo und Redo setzen sie byte-genau zurück (`world::captureVob`/`restoreVob`). IDs bleiben verbraucht.
  - Ein Ziehen mit dem Gizmo ist ein Schritt. Tippen in denselben Feldern desselben Vobs ergibt einen Schritt.
  - Tiefe `[editor] undo_depth` (200).
  - „ungespeichert“ heißt: Die Welt weicht vom gespeicherten Stand ab. Führt Undo genau dorthin zurück, gilt sie
    wieder als gespeichert.
- **Generator-Vobs:** Steht ein Vob im `generator.owned` der Welt (world.md), warnt der Inspektor: der nächste
  `gothar-worldgen assemble` überschreibt ihn – dort `locked` setzen.
  - Die erste Bearbeitung eines solchen Vobs in der Sitzung fragt vorher (Ja/Nein, `Editor::confirmation`). Erst
    nach Ja wird sie ausgeführt; danach fragt der Editor für Generator-Vobs nicht mehr.
  - Speichern mit geänderten Generator-Vobs fragt ebenso und nennt sie („2 generated vob(s) or waynet entries changed (68, 70) …“),
    Nein lässt die Datei unverändert. Entscheidung 1 des M16-Plans.
- **Wegnetz-Modus (M16 Teil B, Taste G bzw. Fenster „Waynet“):** bearbeitet den `waynet`-Block der Welt
  (Vertrag world.md „Wegnetz“) als Arbeitskopie; jede Änderung geht sofort an die Engine (`Engine::setWaynet`,
  Pfadsuche neu gebaut) und ist ein Undo-Schritt (`editor/WaynetEdit.hpp`, Momentaufnahme der berührten Einträge
  und ihrer Kanten).
  - Klick wählt einen Punkt oder Freepoint, das Gizmo verschiebt ihn (ein Schritt je Ziehen). Strg+Klick auf den
    Boden setzt einen neuen Punkt, verbunden mit dem gewählten; Name aus dessen Stamm (`WP_CAMP_OUTSIDE_01`), sonst
    aus dem Weltnamen. Umschalt+Klick verbindet bzw. trennt den gewählten mit dem angeklickten Punkt. Entf löscht.
  - Fenster: Position, Blickrichtung (Freepoints), Umbenennen (Kanten folgen), Freepoint mit Typ (`FP_<TYP>_…`).
  - Gezeichnet werden Punkte und Kanten bis `[editor] waynet_radius` (80 m) um die Kamera, erzeugte blasser,
    Namen bis 25 m.
  - Erzeugte Einträge (`owner: worldgen`): die erste Änderung in der Sitzung fragt; Verschieben oder Umbenennen
    macht sie zu eigenen (Vertrag). Löschen gilt nur bis zum nächsten Generator-Lauf: der Editor zeigt die fertige
    Zeile für `tools/worldgen/data/<ort>/waynet.json` „remove“ zum Kopieren und schreibt welts Datei nicht
    (Entscheidung 2). Speichern mit geänderten erzeugten Einträgen fragt wie bei Generator-Vobs.
  - Prüfen (`editor/WaynetCheck.hpp`, Empfehlungen aus world.md): unverbundene Punkte, Inseln außerhalb des größten
    Netzes, Kanten unter 5 m oder über 20 m, Hindernis auf Hüfthöhe (Strahl gegen `COL_`-Welt), kein begehbarer
    Boden, Freepoint ohne Wegpunkt in 3 m. Klick auf einen Eintrag wählt ihn und fährt die Kamera hin.
  - Pfadprobe: A* der Engine zwischen zwei Punkten, als grüne Linie gezeichnet.
- **Zonen-Modus (M16 Teil C, Taste V bzw. Fenster „Zones & triggers“):** bearbeitet den `zones`-Block der Welt
  (world.md „Zonen“) als Arbeitskopie; jede Änderung ist ein Undo-Schritt und geht sofort an die Engine
  (`Engine::setZones`, geprüft wie beim Lesen). Räume, Musik und Ambiente folgen im nächsten Frame.
  - `indoor`, `music` und `ambient` werden als Boxen gezeichnet (Raum blau, Musik violett, Ambiente grün) und sind
    editierbar. Andere Typen stehen nur in der Liste und bleiben unverändert.
  - Klick wählt die Box, in die der Strahl zuerst eintritt; steht die Kamera in allen getroffenen Boxen, die kleinste.
    Das Gizmo verschiebt, dreht nur um die Hochachse (Ring Y) und ändert die Halbachsen (1/2/3 wie bei Vobs).
  - `value`: Musik-Themen aus `data/music.toml` (ohne `common`), Ambienten aus `data/ambient.toml`, Raum-Tags als
    freier Text. Neue Zone: Typ und Wert wählen, sie steht 6 m vor der Kamera auf dem Boden.
  - „here“ zeigt je Typ, welche Zone an der Kamera gilt (die kleinste Box, Regel aus world.md). Ohne Spielfigur
    misst auch die Musik an der Kamera.
- **Trigger (M16 Teil C):** Volumen aller Trigger im Umkreis von 120 m werden im Vob-Modus gezeichnet. Der Inspektor
  bietet Form und Filter sowie Listen für `onEnter`/`onLeave` (globale Lua-Funktionen der geladenen Skripte), die
  Zielwelt (`.g7world` im VFS) und ihre Startpunkte, dazu `owner` (privater Bereich, M9). Unbekannte Namen bleiben
  sichtbar. „check the triggers“ meldet
  unbekannte Funktionen, fehlende Zielwelten oder Startpunkte, Startpunkte ohne Welt und Trigger ohne Wirkung
  (keine Funktion, kein Levelwechsel, kein Besitzer). Ein Klick auf eine Meldung wählt den Trigger.
- **Lichter und Mobs (M16 Teil D, Abschnitt „Lights & mobs“ im Fenster „Editor“):**
  - „time of day“ stellt die Tageszeit nur für die Darstellung, damit sich Lichter bei Nacht beurteilen lassen. Die
    Weltdatei speichert keine Zeit.
  - Darunter steht, wie viele Lichter dieses Bild nimmt (höchstens `[render] max_lights`, E1) und wie viele Welt- und
    Fensterlichter es gibt.
  - „add light“ setzt ein Licht aus einer Vorlage (Feuer, Fackel, Kerze, Lampe, Fenster mit `daylight`) 4 m vor die
    Kamera, 2 m über den Boden (`LIGHT_<VORLAGE>_NN`). Es zählt sofort zur Lichtauswahl.
  - Der Inspektor eines Lichts bearbeitet Farbe, Reichweite, Stärke, Flackern und „daylight“ (Fensterlicht).
  - Der Inspektor eines Mobs wählt die Definition aus der Liste (Lua-`Mob`-Definitionen und Typen aus `mobs.toml`).
    Er zeigt Typ, Name, Besitzer, Schloss, Schlüssel und Inhalt der Definition; diese Werte gehören in Lua.
  - „door open“ gibt es nur bei Türen. Die Benutzer-Slots des Typs stehen als Marker an der Auswahl (Fußpunkt,
    Blickrichtung, Körperhöhe).
  - „check the mobs“ meldet unbekannte Definitionen, Typen ohne Eintrag in `mobs.toml`, Besitzer, die weder NPC noch
    Gilde sind, Schlüssel und Inhalt, die keine Items sind, Schlösser aus anderen Buchstaben als L und R und offen
    gespeicherte Nicht-Türen. Ein Klick auf eine Meldung wählt das Mob.
- **Routinen und Play-in-Editor (M16 Teil E, Fenster „Routines“):**
  - **Geister:** Für jedes NPC der Welt zeigt ein Marker mit Name und Zustand, wo seine Routine es zur gewählten
    Stunde hinstellt (Wegpunkt bzw. Freepoint des Eintrags). Das NPC der gewählten Routine steht dort als seine
    echte Figur in Ruhepose, zur Kamera gedreht. Eine Linie führt vom jetzigen Ort des NPCs dorthin. Der Regler
    „hour“ betrifft nur die Vorschau, nicht die
    Spielzeit; die KI läuft dafür nicht.
  - **Prüfung „Day plan check“:** für die Routinen der NPCs dieser Welt und der Editor-Datei. Sie meldet Zeiten, die
    nicht HH:MM sind, leere Zeitfenster, Lücken („nothing to do 20:00-24:00“), Überschneidungen (es gilt der erste
    Eintrag) sowie Orte ohne Zustand, ohne Ort oder unbekannt im Wegnetz.
  - **Bearbeiten (Entscheidung 4):** Routinen der Datei `game/scripts/routines/<ort>_editor.lua` (Ort = Name der
    Weltdatei) bearbeitet der Editor: „new routine“ (Name `rtn_…`, noch nicht vergeben), Einträge mit von/bis,
    Zustand aus der Liste der `zs_`-Zustände der Skripte und Ort (Feld oder „pick“: der nächste Klick wählt den
    Wegpunkt bzw. Freepoint). Jeder Schritt ist rückgängig zu machen.
  - „write the routines“ bzw. Strg+S schreibt die Datei (nur `Routine "…" { … }`, nach Name, Einträge nach Beginn;
    von Hand nicht anfassen) und lädt die Skripte neu. Routinen anderer Dateien zeigt der Editor nur an.
  - **Play-in-Editor (Entscheidung 7):** F5 startet die Simulation mit dem Helden auf dem Boden unter der Kamera,
    Umschalt+F5 am Startpunkt der Welt. F5 oder Esc hält an: Welt, NPCs, Story und Zeit stehen wieder wie vor dem
    Start (`Engine::startPlay`/`stopPlay` über den Spielstand im Speicher), die Kamera an ihrem Platz. Solange der
    Editor läuft, speichern F5/F9 nicht (Schnellspeichern); im Spiel aus dem Editor heraus sperrt `saveBlocker`
    jedes Speichern.
- **Prefabs (M16 Teil F, Entscheidung 5; Abschnitt „Prefabs“ im Fenster „Editor“):**
  - „save selection“ legt den gewählten Vob mit seinen Kindern als Vorlage `assets/source/prefabs/<name>.g7prefab`
    an (Name aus Kleinbuchstaben, Ziffern, `_`). Wegpunkte und Freepoints bis 2 m (waagrecht) neben einem ihrer Vobs
    kommen mit, ebenso die Kanten zwischen diesen Wegpunkten.
  - Die Datei ist eine kleine Welt im `.g7world`-Format, die Lagen sind relativ zum ersten Vob.
  - Ein Klick auf eine Vorlage setzt sie 8 m vor die Kamera auf den Boden. Es entstehen lose Kopien: normale Vobs
    mit neuen IDs, ohne Verknüpfung zur Vorlage. Wegpunkte und Freepoints bekommen die nächste freie Nummer ihres
    Stamms (`FP_SIT_CAMPFIRE_01` → `FP_SIT_CAMPFIRE_02`).
  - Das Setzen ist ein Undo-Schritt für Vobs und Wegnetz zusammen.
- **Vobs aus Blender (ADR 0023):** Hat eine `.glb` leere Knoten `VOB_LIGHT_…`, `VOB_START_…` oder `VOB_TRIGGER_…`,
  setzt der Editor sie beim Platzieren des Modells als Kind-Vobs ein (Vertrag in `asset.md` „Vobs aus Blender“).
- Nach jeder Änderung baut die Engine Instanzen und Lichter aus der Szene neu (`Engine::refreshScene`, Modelle
  bleiben im Cache).

| Funktion | Phase |
|---|---|
| Welt laden/speichern, Kamera frei fliegen | M4 |
| Vobs auswählen, verschieben/drehen/skalieren (Gizmo), aus Asset-Browser platzieren | M4 |
| Eigenschafts-Inspektor für Komponenten | M4 |
| Wegnetz: Punkte setzen, verbinden, benennen, Freepoints, Prüfungen, Pfadprobe | M16 B (umgesetzt) |
| Zonen (Musik, Ambient, Innenraum) als Boxen, Besitz als Trigger mit `owner` | M16 C (umgesetzt) |
| Trigger mit Skript-Callbacks (Auswahllisten, Prüfung) | M16 C (umgesetzt) |
| Licht-Editor mit Vorschau je Tageszeit, Lichter aus Vorlagen, Hinweis auf das Lichtbudget | M16 D (umgesetzt) |
| Mobs: Definition aus Listen, Slots als Marker, Tür offen; Besitzer, Schloss, Inhalt angezeigt und geprüft | M16 D (umgesetzt) |
| Routinen-Vorschau: Zeitregler, NPC-„Geister“ an Routinen-Positionen, Prüfung; Routinen in eigener Lua-Datei je Ort | M16 E (umgesetzt) |
| Undo/Redo (Befehle mit JSON-Zuständen, ADR 0022), Fragen vor Generator-Vobs | M16 A (umgesetzt) |
| Prefabs (lose Kopien, mit Wegnetz-Einträgen), Vobs aus `VOB_`-Knoten einer `.glb` | M16 F (umgesetzt) |
| Simulation im Editor starten/stoppen (Play-in-Editor, F5 an der Kamera, Umschalt+F5 am Startpunkt) | M16 E (umgesetzt) |

## Autopilot (`gothar --walk`, vorgezogen für die W3-Begehung; Vertrag mit welt)
Die Spielfigur läuft eine Route ab und protokolliert, was unterwegs passiert. `tools/walk` (`g7_walk`) ist wie der
Editor ein `EngineTool` im Spiel.
```
gothar --world=<welt> --walk=<route.json> [--walk-out=<ordner>] [--no-render]
```
- **Ablauf:**
  - Läuft mit fester Schrittweite (1/60 s), ohne Frame-Grenze und ohne VSync, also deterministisch und so schnell
    wie möglich.
  - Mit Fenster entstehen Screenshots aus der Third-Person-Kamera. Mit `--no-render` gibt es keine Bilder: Die Welt
    lädt dann ohne Grafikgerät (Szene, Kollision, Spielfigur).
  - Am Ende der Route bzw. beim Zeitlimit beendet sich das Spiel. Immer mit äußerem Timeout starten.
  - Ausgabe: Standardordner `walk/`.
- **`route.json` (Version 1):**
```json
{"version": 1, "start": "START_MARKTPLATZ", "gait": "run", "time": "11:00", "timeLimit": 900,
 "screenshot": "all", "screenshotEveryM": 25,
 "points": [
   {"name": "P01_MARKT", "pos": [12.5, -40.0], "gait": "walk", "radius": 2.5, "screenshot": true},
   {"name": "P02_TREPPE", "pos": [30.0, -55.0], "action": "climb"},
   {"name": "P03_GRABEN", "pos": [41.0, -60.0], "action": "jump"},
   {"name": "P04_SCHLOSS", "pos": [120.0, 80.0], "teleport": true, "y": 12.0}]}
```
  - **Route:**
    - `start`, `gait` (`run`|`walk`|`sneak`, Vorgabe run) und `time` sind optional.
    - `timeLimit` in Sekunden Spielzeit (Vorgabe 900).
    - `"screenshot": "all"` setzt Bilder an allen Punkten; `screenshotEveryM` macht zusätzlich alle N Meter ein Bild
      (`auto_<nnnn>.png`).
  - **Punkte:**
    - `pos` = x, z. Die Figur dreht sich zum Punkt und läuft geradeaus; erreicht ist er waagrecht innerhalb von
      `radius` (Vorgabe 1,0 m).
    - `action`:
      - `jump` drückt die Sprungtaste 2 m vor dem Punkt.
      - `climb` drückt sie, sobald die Figur in Reichweite 0,25 s ansteht (an der Wand). Ohne erreichbare Kante wird
        daraus ein Sprung.
    - `teleport`: setzt die Figur auf den Punkt, auf den höchsten Boden darunter bzw. `y`. Das geschieht erst nach
      der Landung eines laufenden Falls.
    - `screenshot`: Bild beim Erreichen (`<name>.png`).
- **Protokoll** `walk.jsonl`, eine JSON-Zeile je Ereignis, jeweils mit `t` (s Spielzeit):
  - `start`
  - `reached` mit `name`, `pos`, `state` (`ground`|`slide`|`air`|`swim`|`dive`|`climb`) und `teleport`
  - `stuck`: über 2 s weniger als 0,2 m näher. Mit `vob` = Name des Mesh-Vobs vor der Figur (bzw. `terrain`) und
    `state`; der Punkt wird übersprungen.
  - `fall` ab 1 m mit `height`, `damage`, `water` und `pos`
  - `slide_start`/`slide_end`, `swim_start`/`swim_end`, `climb`, `jump`/`climb_try`
  - `trigger` (`name`), `world_change`, `screenshot`, `teleport_failed`, `timeout`
- **Zusammenfassung** `walk_summary.json`: Punkte, erreicht, übersprungen (Namen), Hänger, Stürze mit höchstem und
  Schaden, Ertrinken, Sekunden, Meter, Zeitüberschreitung.
- **Tests:** `tests/walk` (Routen-Format) und `render_gpu` „Autopilot GPU …“: Lagerroute mit Klettern, 6-m-Fall,
  Schwimmen und Hänger am geschlossenen Tor (`vob: fence-gate`).

## Optional: Gothic-Import-Werkzeug (siehe ADR 0008)
Nur für Forschung/Vergleich mit eigener, legal erworbener Kopie; nicht Teil des Spiels.
