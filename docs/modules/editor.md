# editor – Plan M16 (Editor und Werkzeuge)

Stand: **Plan freigegeben** (Projektinhaber, 2026-11-07; ADR 0022 und 0023 angenommen, Fragen 1–8 wie vorgeschlagen
entschieden). Grundlage ist der Editor-Modus aus M4 (`gothar --editor`,
`tools/editor`, Beschreibung in `docs/modules/tools.md` „Editor“). M16 macht daraus ein Werkzeug, mit dem sich ein
neuer Ort samt Wegnetz, Zonen und NPC-Routinen **ohne Texteditor** bauen lässt (DoD der Roadmap).

## Umsetzungsstand
| Teil | PR | Stand |
|---|---|---|
| A – Befehle, Undo/Redo, Schutz erzeugter Daten | #355 | umgesetzt |
| B – Wegnetz-Editor | #359 | umgesetzt |
| C – Zonen- und Trigger-Editor | #361 | umgesetzt |
| D – Licht- und Mob-Editor | #362 | umgesetzt |
| E – Routinen-Vorschau und Play-in-Editor | #367 | umgesetzt |
| F1 – Prefabs, Vobs aus Blender (`VOB_`) | #371 | umgesetzt |
| F2 – Gelände (Höhen-Pinsel, Mesh → Heightmap) | dieser PR | umgesetzt |

Abweichungen vom Plan unten (Bedienung: `docs/modules/tools.md` „Editor“):
- **A:** Befehle sind Daten statt Klassen mit `apply`/`revert`: `editor::Command` hält `VobState` (Zeile der
  Weltdatei je berührtem Vob, Kinder eingeschlossen) vorher und nachher; `History::undo/redo` setzt sie ein
  (`world::captureVob`/`restoreVob`). Es gibt kein `Ownership.hpp`: Vobs prüft `world::isGenerated`, das Wegnetz
  `isGeneratedEntry`; die Fragen stellt der Editor (`Editor::confirmation`). Die Eigentumsfälle stehen in den
  GPU-Tests statt in `test_ownership.cpp`.
- **B:** Taste G (N ist das Tagebuch). Befehle halten Momentaufnahmen der berührten Wegnetz-Einträge und ihrer
  Kanten (`WaynetSnapshot`); jede Änderung geht über `Engine::setWaynet`. Das Gizmo verschiebt nur, die
  Blickrichtung eines Freepoints steht im Fenster. Doppelte oder ungültige Namen weist schon die Bearbeitung ab
  (`normalizeWaynet`), sie stehen deshalb nicht in der Prüfliste. Die Prüfungen sind in `test_waynet_edit.cpp`
  getestet. Gelöschte erzeugte Punkte: Der Editor zeigt die Zeile für welts `waynet.json` „remove“ zum Kopieren und
  schreibt die Datei nicht (Entscheidung 2).
- **C:** Taste V. Ein Zonen-Befehl hält den ganzen `zones`-Block vorher und nachher (wenige hundert Boxen); jede
  Änderung geht über `Engine::setZones` (geprüft wie beim Lesen), Räume, Musik und Ambiente folgen im nächsten
  Frame. Die Musik misst ohne Spielfigur jetzt an der Kamera (wie in `world.md` beschrieben, galt bisher nur für
  das Ambiente). Trigger sind Vobs: Gizmo und Inspektor des Vob-Modus; neu sind Auswahllisten (Form, Filter,
  Skriptfunktionen aus `Engine::scriptFunctions`, Welten, Startpunkte der Zielwelt) und die Prüfung im Fenster
  „Zones & triggers“ (unbekannte Funktion, fehlende Welt oder fehlender Startpunkt, Trigger ohne Wirkung).
- **D:** Neue Lichter kommen aus Vorlagen (`LightEdit.hpp`: Feuer, Fackel, Kerze, Lampe, Fenster). Nach
  `refreshScene` sind sie sofort in der Lichtauswahl des nächsten Bildes (E1, im GPU-Test geprüft). Der
  Tageszeit-Regler setzt die Spielzeit nur für die Darstellung (`GameTime::setTime`, die Welt speichert keine Zeit).
  Der Hinweis zeigt die Lichter im Bild gegen `max_lights` sowie Welt- und Fensterlichter.
  - Mobs: Die Definition kommt nur aus der Liste (Lua-`Mob`-Definitionen und Typen aus `mobs.toml`). Besitzer,
    Schloss, Schlüssel und Inhalt gehören der Lua-Definition (Mechanik in C++, Inhalt in Lua). Der Editor zeigt sie
    und prüft sie gegen NPCs, Gilden (`Guilds`) und Items, statt sie zu bearbeiten.
  - „Tür offen“ gibt es nur für Türen. Die Slots des Typs stehen als Marker an der Auswahl.
  - Die Listen kommen über die Engine (`Engine::mobDefinitions`, `mobTypeNames`, `mobTypeSlots`,
    `scriptInstanceNames`, `guildNames`, `lightBudget`).
- **E:** Die Engine liefert reine Abfragen ohne Nebenwirkung: `Engine::routines()` (die `Routine`-Instanzen mit
  Datei), `npcRoutines()`, `routinePlace(ort)`, `scriptGlobalNames("zs_")`. Der Eintrag zu einer Minute steht im
  Editor (`RoutinePreview.hpp`, `entryAt`), nicht als `Engine::routineAt`.
  - Geister: Marker mit Name und Zustand. Das NPC der gewählten Routine steht als seine echte Figur in Ruhepose
    da (`Engine::setGhostFigure`: dieselbe Figur wie beim Einsetzen, mit den Kreaturen gezeichnet, ohne KI, kein
    Teil des Spiels), zur Kamera gedreht. Den Clip des Zustands gibt es nicht: Die `zs_`-Zustände spielen ihre
    Animationen in Lua, die Engine kennt keine feste Zuordnung Zustand → Clip.
  - Geprüft werden nur die Routinen, denen NPCs dieser Welt folgen, und die der Editor-Datei.
  - Routinen bearbeiten: Die Datei heißt nach der Weltdatei (`routines/<ort>_editor.lua`). Ein NPC bekommt seine
    Routine weiter in seiner `Npc`-Instanz (`routine = "rtn_…"`). Der Editor legt nur die Routinen an und füllt sie.
    Lesen und Schreiben sind reine Funktionen (`RoutineEdit.hpp`, Rundreise im Test). `Engine::writeScript`
    schreibt die Datei in den Skriptordner (`EngineConfig::extraScriptDirectory` für Tests) und lädt die Skripte
    neu. Routinen-Schritte sind Befehle wie alle anderen (Momentaufnahme der Editor-Routinen).
  - Play-in-Editor: Es gibt kein `PlayInEditor.hpp`. Der Schnappschuss ist der Spielstand aus M15
    (`captureGame`/`applyGame`) im Speicher der Engine (`Engine::startPlay`/`stopPlay`), nicht die Befehls-JSON
    aus A: So kommen auch NPCs, Story und Zeit zurück. Wegnetz und Zonen des Editors gibt der Editor danach wieder
    an die Engine.
  - Weil F5 auch Schnellspeichern ist: Mit Editor speichert und lädt F5/F9 nicht, und während Play-in-Editor sperrt
    `saveBlocker` jedes Speichern (die Spielstände gehören dem Projektinhaber).
- **F1:** Teil F ist geteilt, wie der Koordinator es erlaubt: Prefabs und Blender-Vobs in einem PR, das Gelände im
  nächsten.
  - Eine Vorlage ist der gewählte Vob mit seinen Kindern; der Editor hat eine Einzelauswahl, Gruppen sind Eltern
    mit Kindern. Die Datei `.g7prefab` ist das `.g7world`-Format (eine kleine Welt), es gibt kein eigenes JSON.
  - Wegnetz-Einträge kommen mit, wenn sie bis 2 m (waagrecht) neben einem Vob der Vorlage liegen.
  - Der Ordner ist `assets/source/prefabs` auf der Platte (`Editor::setPrefabDirectory` für Tests). Er wird direkt
    gelesen, nicht über das VFS.
  - `VOB_`-Knoten: Licht, Startpunkt, Trigger; Vertrag in `asset.md`. Die Engine reicht die leeren Knoten als
    einfache Werte durch (`Engine::modelMarkers`, `asset::loadGltfMarkers`), da der Editor `asset` nicht direkt
    nutzen darf.
- **F2:**
  - Die Engine bietet `editableHeightfield()` (null bei Sperre), `terrainHeightsChanged()` (lädt die Höhentextur
    neu und berechnet die Kachel-Grenzen, `TerrainRenderer::updateHeights`), `terrainStrokeFinished()` (baut die
    Kollision sofort neu, der Editor pausiert die Simulation), `terrainSplat(map)` (RGBA-Bytes der Splat-Karte auf
    der CPU, dieselben wie für die Fußschritte), `terrainSplatChanged()` und `saveTerrain()`.
  - Statt eines Teil-Updates der Kacheln lädt `updateHeights` die ganze Höhentextur neu: Eigene Orte sind klein,
    und Leonberg ist gesperrt.
  - Gesperrt ist jede Welt mit `generator`-Kopf, nicht nur Leonberg.
  - Splat-Malen schreibt PNG. Gekochte `.ktx2`-Kopien gelten weiter, bis neu gekocht wird; die Engine warnt dann.
  - Die Pinsel-Mathematik steht in `TerrainBrush.hpp`, ohne Fenster getestet.

## Ausgangslage (M4)
- `g7_editor` (Bibliothek in `tools/editor`) ist ein `EngineTool`. Die Engine kennt nur `update`/`ui`. Fenster sind
  `ui::EditorPanel` (ImGui bleibt in `ui`, ADR 0015).
- Operationen sind freie Funktionen auf `world::Scene` (`Operations.hpp`), ohne Fenster getestet (`tests/editor`).
  Ende-zu-Ende-Tests laufen in `render_gpu` („Editor GPU …“).
- Speichern schreibt die ganze Welt (`captureWorld` + `writeWorldFile`) byte-gleich zum Generator. Beim ersten
  Speichern bleibt eine `.bak`. Es gibt kein Undo.
- Eigentum (Verträge mit welt, `world.md`):
  - Vobs im `generator.owned` schreibt `gothar-worldgen assemble` bei jedem Lauf neu, der Editor warnt nur.
  - Wegpunkte, Freepoints und Kanten mit `owner: "worldgen"` ersetzt der Generator. Ohne `owner` gehören sie dem
    Menschen; verschiebt der Editor einen erzeugten Punkt, entfällt `owner`.
  - Erzeugte Punkte dauerhaft entfernen geht nur über welts Annotationen (`waynet.json` `remove`).
  - Leonberg (`assets/source/worlds/leonberg/`) gehört **welt**.

## Leitlinien
1. **Mechanik in `g7_editor`, Darstellung in `ui`.** Neue Fenster werden neue `ui::…Panel`-Structs (Daten rein,
   Aktionen raus) wie `EditorPanel`. Alle Bearbeitungen laufen als **Befehle** (Teil A), so werden Undo, Tests und
   Schutz der erzeugten Daten an einer Stelle erledigt.
2. **Keine stille Überschreibung erzeugter Daten.**
   - Jede Bearbeitung an etwas, das der Generator besitzt, braucht eine Bestätigung je Sitzung und Objektart.
   - Das Speichern einer Welt mit `generator`-Kopf zeigt vorher eine Zusammenfassung: was erzeugt ist und beim
     nächsten `assemble` zurückkommt, was dauerhaft bleibt.
3. **Gleiche Bytes.** Laden und unverändertes Speichern ergibt dieselbe Datei. Das gilt schon für Vobs, Wegnetz und
   Zonen und bleibt Pflicht für alle neuen Blöcke.
4. **Werte als Daten.** Raster, Farben, Griffgrößen und Vorschau-Einstellungen stehen in `game/config/engine.toml`
   `[editor]`.

---

## Teil A – Befehle, Undo/Redo, Schutz erzeugter Daten (Grundlage für B–F)
- **Befehlsschicht** (`tools/editor/include/g7/editor/Commands.hpp`, `src/Commands.cpp`):
  - `struct Command { std::string label; virtual void apply(EditContext&) = 0; virtual void revert(EditContext&) = 0;
    virtual bool merge(const Command&) { return false; } }`.
  - `EditContext` bündelt `world::Scene&`, den Wegnetz- und Zonen-Stand der Welt (`world::WorldFile`-Teile) und das
    Eigentum (`GeneratorHead`).
  - `class History { push(std::unique_ptr<Command>); undo(); redo(); bool canUndo/canRedo; usize savedAt; }`.
    Die Tiefe ist begrenzt (`[editor] undo_depth`, 200, Entscheidung 3).
  - Aufeinanderfolgende Gizmo-Ziehschritte desselben Vobs werden ein Eintrag (`merge`).
- **Zustand je Befehl** (ADR 0022): Ein Befehl speichert vorher und nachher das, was er ändert, als JSON in der Form
  des Weltformats. Bei Vobs ist das `writeVobJson`/`parseVobJson` des Weltformats (wie das Welt-Delta der
  Spielstände, M15), bei Wegnetz und Zonen ihr JSON-Eintrag.
  Undo setzt den alten Zustand wieder ein. So braucht es keinen zweiten Serialisierer, und Undo stellt
  byte-genau wieder her.
- **Alte Operationen umstellen:** `place`, `duplicate`, `delete` und `setSelectionTransform` werden zu Befehlen;
  `Operations.hpp` bleibt die Mechanik darunter.
- **Tasten:** Strg+Z / Strg+Y (auch Strg+Umschalt+Z). „Geändert“ zeigt einen Stern, wenn sich die Historie vom
  gespeicherten Stand unterscheidet.
- **Eigentum:** `Ownership` (`Ownership.hpp`) beantwortet für Vob, Wegpunkt, Kante, Freepoint und Zone, ob etwas
  erzeugt ist.
  - Bearbeitet ein Befehl Erzeugtes, fragt der Editor einmal je Sitzung und Art nach („erzeugt – nächster
    `assemble` überschreibt; trotzdem?“).
  - Im Wegnetz entfernt der Befehl dabei `owner`, wie im Vertrag festgelegt.
  - Speichern einer Welt mit `generator`-Kopf zeigt die Zusammenfassung (Leitlinie 2).
- **Dateien:** `Commands.hpp/.cpp`, `Ownership.hpp/.cpp` in `tools/editor`, `ui::EditorPanel` um Undo/Redo und
  Bestätigung erweitert, `Editor.cpp` umgestellt.
- **Tests:**
  - `tests/editor/test_commands.cpp`: Undo/Redo über alle Vob-Befehle; byte-gleiche Welt nach Tun und Rückgängig;
    Zusammenfassen von Ziehschritten; Tiefe; gespeicherter Stand.
  - `tests/editor/test_ownership.cpp`: erzeugt/eigen für alle Arten; Verschieben eines erzeugten Wegpunkts entfernt
    `owner`, Undo stellt ihn wieder her.
  - `render_gpu` „Editor GPU“ erweitert um Strg+Z/Y.

## Teil B – Wegnetz-Editor
- **Anzeige:**
  - Wegpunkte als Kugeln, Freepoints als Pfeile in Blickrichtung, gefärbt nach Typ; Kanten als Linien.
  - Erzeugtes ist blasser gezeichnet als Eigenes.
  - Nur im Umkreis (`[editor] waynet_radius`, Vorschlag 80 m), Leonberg hat rund 3800 Punkte.
- **Bearbeiten (Befehle aus A):**
  - Punkt setzen: Klick auf den Boden; Name wird vorgeschlagen (`WP_<ORT>_<NN>`), editierbar.
  - Verschieben mit dem Gizmo, nur Verschiebung und Gieren.
  - Verbinden: Shift+Klick von Punkt zu Punkt; Kante lösen.
  - Freepoint setzen (Typ aus der Liste in `world.md`, Richtung mit Drehring); Umbenennen; Löschen.
  - Löschen eines erzeugten Punkts bekommt den Hinweis auf welts Annotation (`waynet.json` `remove`), so wie es
    im Vertrag steht.
- **Prüfen** (`WaynetCheck.hpp`, CPU, wiederverwendbar), angezeigt als Liste mit Sprung zum Fehler:
  - unverbundene Punkte und Inseln (Komponenten wie im Wegnetz-Bericht);
  - Kanten über 20 m oder unter 5 m;
  - Gerade in Hüfthöhe durch Wände (Strahl gegen die Kollision);
  - Punkte nicht auf begehbarem Boden;
  - Freepoints ohne erreichbaren Punkt in 3 m;
  - doppelte oder ungültige Namen.
  Grundlage sind die „Empfehlungen“ in `world.md`.
- **Pfadprobe:** zwei Punkte anklicken, die Pfadsuche (`ai::Waynet`) zeichnet den Weg ein.
- **Dateien:** `WaynetEdit.hpp/.cpp`, `WaynetCheck.hpp/.cpp` (`tools/editor`), `ui::WaynetPanel`. Das Wegnetz in der
  Engine wird nach jedem Befehl neu gebaut (wie `refreshScene`).
- **Tests:**
  - `tests/editor/test_waynet_edit.cpp`: alle Befehle mit Undo, Namensvorschlag, Eigentumsregeln.
  - `tests/editor/test_waynet_check.cpp`: jede Prüfung an einem kleinen Wegnetz.
  - `render_gpu`: im Testlager Punkt setzen, verbinden, Pfadprobe, speichern; die Welt lädt mit dem neuen Punkt.

## Teil C – Trigger- und Zonen-Editor
- **Zonen** (`indoor`, `music`, `ambient` und unbekannte Typen nur anzeigen):
  - Boxen zeichnen, wählen, mit Gizmo verschieben, drehen (nur Gieren) und skalieren (Halbachsen).
  - `value` aus einer Liste: Themen aus `data/music.toml`, Ambienten aus `data/ambient.toml`, Raum-Tags frei.
  - „Hörer hier“: zeigt, welche Zone an der Kamera gilt; bei Musik und Ambiente die kleinste Box (Regel aus
    `world.md`).
- **Trigger:**
  - Box bzw. Kugel mit Gizmo; `filter`, `once`, `onEnter`/`onLeave` (Lua-Funktionsnamen aus einer Liste der
    geladenen Skripte), Levelwechsel (Welt und Startpunkt aus Listen).
  - Prüfen: unbekannte Funktion, Startpunkt fehlt in der Zielwelt.
- **Dateien:** `ZoneEdit.hpp/.cpp`, `TriggerEdit.hpp/.cpp`, `ui::ZonePanel`. Die Zonen werden über das vorhandene
  Lesen und Schreiben des `zones`-Blocks gespeichert, sortiert wie festgelegt.
- **Tests:**
  - `tests/editor/test_zone_edit.cpp`: Befehle mit Undo; byte-gleiches Speichern; kleinste Zone am Punkt.
  - `tests/editor/test_trigger_edit.cpp`: Komponenten, Prüfungen.
  - `render_gpu`: Musikzone anlegen, Kamera hinein, das Thema wechselt.

## Teil D – Licht- und Mob-Editor
- **Licht:**
  - Farbe, Stärke, Reichweite und Flackern mit Live-Vorschau; Fensterlicht (`daylight`) an oder aus.
  - **Tageszeit-Regler** im Editor (Spielzeit nur für die Darstellung), damit sich Licht bei Nacht beurteilen lässt.
  - Hinweis aus E1: Lichter je Bild höchstens `max_lights`; zeigen, wie viele um die Kamera gezählt werden.
- **Mob:**
  - `definition` aus der Liste der `Mob`-Instanzen bzw. Typen (`data/mobs.toml`), Besitzer (NPC oder Gilde aus
    Listen), Schloss und Schlüssel, Inhalt (Items aus der Liste), Tür offen oder zu.
  - Slots des Typs als Marker zum Prüfen; keine Bearbeitung, die Slots gehören `mobs.toml`.
- **Dateien:** `LightEdit.hpp`, `MobEdit.hpp` (`tools/editor`), Erweiterung von `EditorPanel.fields` um Farbwähler,
  Listen und Zeitregler.
- **Tests:**
  - `tests/editor/test_light_mob_edit.cpp`: Befehle mit Undo, gültige Listen, Tür offen gespeichert wie
    festgelegt.
  - `render_gpu`: Licht ändern, das Bild ändert sich; Zeitregler.

## Teil E – Routinen-Vorschau und Play-in-Editor
- **Routinen-Vorschau:**
  - Zeitregler 0–24 Uhr. Für jedes NPC der Welt zeigt ein „Geist“ (halbdurchsichtige Figur bzw. Marker mit
    Namen), wo seine Routine es zu dieser Stunde hinstellt: Wegpunkt bzw. Freepoint seines Eintrags, mit Zustand.
  - Eine Liste nennt Routinen-Orte, die das Wegnetz nicht kennt, und Lücken oder Überschneidungen im Tagesplan.
  - Die Daten kommen aus den geladenen Skripten (`Routine`-Instanzen); die KI läuft dafür nicht.
- **Routinen bearbeiten ohne Texteditor (DoD):** Entscheidung 4:
  - Ein Routinen-Fenster legt Einträge an (von, bis, Zustand aus der Liste, Ort per Klick auf Wegpunkt oder
    Freepoint).
  - Es schreibt eine **eigene Lua-Datei je Ort** (`game/scripts/routines/<ort>_editor.lua`, nur `Routine { … }`,
    geordnet), die von Hand nicht angefasst wird.
  - Handgeschriebene Routinen anderer Dateien zeigt der Editor nur an.
- **Play-in-Editor:**
  - Taste F5 startet die Simulation ab der Kamera (Held dort, wie `--player`).
  - Esc bzw. F5 hält an und stellt den Zustand **vor** dem Start wieder her (Szene und Wegnetz als Schnappschuss
    über die Befehls-JSON aus A).
- **Dateien:** `RoutinePreview.hpp/.cpp`, `RoutineEdit.hpp/.cpp`, `PlayInEditor.hpp/.cpp` (`tools/editor`),
  `ui::RoutinePanel`. Die Engine liefert `routineAt(npc, minute)` als reine Abfrage (ohne Nebenwirkung).
- **Tests:**
  - `tests/editor/test_routine_preview.cpp`: Ort je Stunde im Testlager, Lücken und Überschneidungen,
    unbekannte Orte.
  - `tests/editor/test_routine_edit.cpp`: geschriebene Lua-Datei lädt und ergibt dieselben Einträge (Rundreise).
  - `render_gpu`: Play-in-Editor ändert die Welt nicht dauerhaft.

## Teil F – Prefabs und Gelände bzw. Blender-Import
- **Prefabs** (Vob-Vorlagen):
  - Eine Gruppe ausgewählter Vobs (mit Kindern) lässt sich als Vorlage speichern (`assets/source/prefabs/<name>.g7prefab`,
    JSON im Vob-Format, Positionen relativ zum ersten Vob).
  - Setzen wie ein Modell aus dem Asset-Browser. Die Kopien sind normale Vobs ohne Verknüpfung zur Vorlage
    (Frage 5).
  - Gedacht für Lagerfeuer mit Bänken, Marktstand mit Waren, Wachposten mit Freepoints.
  - Freepoints und Wegpunkte einer Vorlage kommen mit (umbenannt mit fortlaufender Nummer).
- **Gelände** (ADR 0023):
  - Leonbergs Gelände kommt aus LGL-Daten über welt und wird im Editor **nicht** bearbeitet.
  - Für eigene Orte ein **einfacher Höhen-Pinsel** im Editor: anheben, senken, glätten, ebnen auf Höhe, Malen der
    Splat-Schichten. Er schreibt die `.r16`-Heightmap und Splat-Karten des `terrain`-Blocks.
  - Gröbere Formen kommen aus Blender: ein Mesh-Import, der ein Mesh in eine Heightmap rastert
    (`g7-cook --heightmap-from-mesh`).
- **Blender-Import für Vobs:**
  - Eine `.glb` mit leeren Knoten `VOB_<typ>_<name>` (Licht, Startpunkt, Trigger) lässt sich als Vob-Gruppe
    einsetzen.
  - Gedacht für Innenräume, die in Blender gebaut werden. Es braucht kein neues Format, der Vertrag
    „Kollision in `.glb`“ gilt weiter.
- **Dateien:**
  - `Prefabs.hpp/.cpp`, `TerrainBrush.hpp/.cpp`, `GlbVobImport.hpp/.cpp` (`tools/editor`).
  - `g7-cook` bekommt die Option `--heightmap-from-mesh`.
  - `world::Heightfield` bekommt Schreiben (`.r16`) und ein Teil-Update der Geländekacheln in `render`.
- **Tests:**
  - `tests/editor/test_prefabs.cpp`: Rundreise, Namen, IDs aus `nextVobId`.
  - `tests/editor/test_terrain_brush.cpp`: Pinsel auf kleinem Raster, Undo als Höhen-Delta.
  - `tests/cook`: Rastern eines schiefen Quaders.
  - `render_gpu`: Pinsel ändert Gelände und Kollision.

## Reihenfolge und Umfang
A zuerst (alles hängt an den Befehlen), dann B und C (DoD), D, E, F. Je Teil ein PR. Geschätzt:

| Teil | Inhalt | Umfang |
|---|---|---|
| A | Befehle, Undo/Redo, Eigentum | mittel |
| B | Wegnetz | groß |
| C | Zonen, Trigger | mittel |
| D | Licht, Mob | klein bis mittel |
| E | Routinen-Vorschau und -Bearbeitung, Play-in-Editor | groß |
| F | Prefabs, Gelände-Pinsel, Mesh→Heightmap, `.glb`-Vob-Import | groß |

ADR 0022 (Undo/Redo, angenommen) gehört zu A, ADR 0023 (Gelände und Blender-Import, angenommen) zu F. Neue Bibliotheken sind nicht nötig.

## Entscheidungen des Projektinhabers (2026-11-07, alle wie vorgeschlagen)
1. **Leonberg im Editor:**
   - Darf der Editor die Leonberg-Welt (gehört welt) überhaupt speichern?
   - **Entschieden:** ja, aber nur Eigenes (ohne `owner`, nicht in `generator.owned`) bleibt dauerhaft. Erzeugtes
     ändern geht nur nach Bestätigung, der Speichern-Dialog fasst zusammen, was der nächste `assemble`
     überschreibt.
   - Größere Arbeiten an Leonberg bleiben bei welts Annotationen.
2. **Erzeugte Wegpunkte löschen:**
   - Soll der Editor statt nur zu warnen gleich einen Eintrag in welts `tools/worldgen/data/<ort>/waynet.json`
     (`remove`) schreiben?
   - **Entschieden:** nein, das ist welts Datei. Der Editor zeigt den fertigen Eintrag zum Kopieren an, welt übernimmt
     ihn.
3. **Undo-Tiefe und -Umfang:**
   - **Entschieden:** 200 Schritte je Sitzung, nicht über Speichern und Neustart hinaus; Gelände-Pinsel als ein Schritt
     je Strich.
4. **Routinen ohne Texteditor:**
   - Wie sollen im Editor angelegte Routinen gespeichert werden?
   - **Entschieden:** eine eigene, vom Editor geschriebene Lua-Datei je Ort (`routines/<ort>_editor.lua`).
     Handgeschriebene Routinen bleiben unangetastet und werden nur angezeigt.
   - Alternative: eine Datendatei (TOML), die Lua liest. Sie wäre sauberer getrennt, ist aber eine weitere
     Formatstelle.
5. **Prefabs:**
   - Kopien verknüpft (ändert man die Vorlage, ändern sich alle) oder lose?
   - **Entschieden:** lose, wie Gothics Spacer-Vorlagen. Verknüpfung kostet ein eigenes Format im Weltblock.
6. **Gelände:**
   - Höhen-Pinsel im Editor plus Mesh→Heightmap aus Blender (ADR 0023), oder nur einer davon?
   - **Entschieden:** beides, der Pinsel für Feinarbeit, Blender für Großformen.
   - Leonbergs Gelände bleibt beim Pinsel gesperrt.
7. **Play-in-Editor:**
   - Startet die Simulation an der Kamera oder am Startpunkt der Welt?
   - **Entschieden:** an der Kamera (schneller zum Prüfen). Startpunkt ist eine zweite Taste (Umschalt+F5).
8. **Routinen-Geister:**
   - Figur oder einfacher Marker?
   - **Entschieden:** einfacher Marker mit Namen und Zustand (billig, auch bei 60 NPCs). Die Figur nur für das
     ausgewählte NPC.
