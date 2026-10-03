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

## Optional: Gothic-Import-Werkzeug (siehe ADR 0008)
Nur für Forschung/Vergleich mit eigener, legal erworbener Kopie; nicht Teil des Spiels.
