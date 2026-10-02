# ui

**Zweck:** Alle Bildschirmoberflächen.

## Zwei Welten
1. **Debug-/Editor-UI**: Dear ImGui (+ ImGuizmo). Nur in Entwicklungs-/Editor-Builds.
2. **Spiel-UI**: eigenes schlankes Retained-Mode-System (ADR 0009) auf dem Renderer:
   Widgets (Panel, Text, Bild, Liste, Balken, Item-Slot, 3D-Item-Vorschau), Layout (Anker, Stapel),
   9-Slice-Rahmen, MSDF-Schrift, Gamepad-/Tastatur-Navigation, Themes als Daten.

## Bildschirme
HUD (Leben, Mana, Gegnerleben, Fokusname, Luft), Dialog-Auswahl + Untertitel, Inventar, Handel,
Truhe, Charakter, Tagebuch, Dokument (Briefe/Bücher), Karte, Hauptmenü, Optionen, Laden/Speichern,
Ladebildschirm, Konsole (Debug), Bildschirmmeldungen.

## Lokalisierung
Alle Texte über Schlüssel (`DIA_Diego_Hello_11_01`, `UI_INVENTORY_TITLE`) aus Tabellen pro Sprache;
fehlende Übersetzung → Schlüssel sichtbar + Warnung im Log. Zahlen-/Datumsformat je Sprache.
