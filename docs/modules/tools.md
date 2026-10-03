# tools

## g7-cook (M3)
Kommandozeilen-Werkzeug zum Kochen und Packen von Assets (`tools/asset-cooker`, Bibliothek `g7_cook_lib`,
Tests in `tests/cook`). Stand und Optionen: `docs/06-asset-pipeline.md`; Formate: ADR 0016.

## Editor (M4 Grundlage, M16 vollständig)
Editor-Modus der Engine (`gothar --editor`), ImGui-basiert – Gegenstück zu Gothics **Spacer**.

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
  Speichern je Sitzung bleibt die vorige Fassung als `<datei>.bak` (noch kein Undo). Gelände, Wegnetz, Zonen und
  `generator`-Kopf der Welt bleiben erhalten.
- **Generator-Vobs:** Steht ein Vob im `generator.owned` der Welt (world.md), warnt der Inspektor: der nächste
  `gothar-worldgen assemble` überschreibt ihn – dort `locked` setzen.
- Nach jeder Änderung baut die Engine Instanzen und Lichter aus der Szene neu (`Engine::refreshScene`, Modelle
  bleiben im Cache).

| Funktion | Phase |
|---|---|
| Welt laden/speichern, Kamera frei fliegen | M4 |
| Vobs auswählen, verschieben/drehen/skalieren (Gizmo), aus Asset-Browser platzieren | M4 |
| Eigenschafts-Inspektor für Komponenten | M4 |
| Wegnetz: Punkte setzen, verbinden, benennen, Freepoints, Validierung (unverbundene Punkte) | M16 |
| Zonen (Musik, Ambient, Besitz, Innenraum) als Boxen editieren | M16 |
| Trigger mit Skript-Callbacks | M16 |
| Licht-Editor mit Vorschau je Tageszeit | M16 |
| Mobs: Slots, Zustände, Besitzer, Schloss | M16 |
| Routinen-Vorschau: Zeitregler, NPC-„Geister“ an Routinen-Positionen | M16 |
| Undo/Redo (Command-Pattern), Prefabs | M16 |
| Simulation im Editor starten/stoppen (Play-in-Editor) | M16 |

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
