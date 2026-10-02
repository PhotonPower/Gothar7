# tools

## g7-cook (M3)
Kommandozeilen-Werkzeug zum Kochen und Packen von Assets. Siehe `docs/06-asset-pipeline.md`.

## Editor (M4 Grundlage, M16 vollständig)
Editor-Modus der Engine (`gothic7 --editor`), ImGui-basiert – Gegenstück zu Gothics **Spacer**.

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
