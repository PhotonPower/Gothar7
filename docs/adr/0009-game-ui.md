# 0009 – Spiel-UI: eigenes leichtgewichtiges System

- **Status:** Angenommen (Projektinhaber, 2026-10-31)
- **Datum:** 2026-10-02 (Vorschlag), 2026-10-31 (Entscheidung)
- **Phase:** M14

## Kontext
Ab M14 braucht das Spiel eigene Bildschirme: HUD, Dialog mit Untertiteln, Inventar mit Handel und Truhe,
Charakter, Tagebuch, Dokumente, Hauptmenü, Optionen, Ladebildschirm, Bildschirmmeldungen. Bis M13 laufen alle
Spiel-Fenster als ImGui-Panels (`ui::DebugUi`, ADR 0015) – funktional, aber nicht stilisierbar, ohne
Gamepad-Navigation und ohne Lokalisierung. Gothics UI ist schlicht: wenige Bildschirme, Listen, Balken, Rahmen,
eine Schrift; die Besonderheit ist das Inventar mit 3D-Modell je Zelle.

Randbedingungen: `ui` darf nur von `core`, `platform` und `render` abhängen (`docs/02-architecture.md`), GL-Aufrufe
nur in der RHI von `render`, Drittbibliotheken `PRIVATE`.

## Optionen
1. **Eigenes Retained-Mode-UI** auf dem Renderer – genau zugeschnitten (Gothic-UI ist schlicht), volle Kontrolle über Stil; Aufwand für Layout/Text.
2. **RmlUi** (HTML/CSS-artig) – mächtig, Designer-freundlich; zusätzliche Abhängigkeit, Integrationsaufwand.
3. **Dear ImGui auch fürs Spiel** – schnell; schwer stilisierbar, Immediate-Mode passt nicht zu Animationen/Gamepad.

## Entscheidung
**Option 1** (Projektinhaber, 2026-10-31): ein eigenes, schlankes Retained-Mode-UI im Modul `ui`, gezeichnet über
die RHI von `render`. **RmlUi ist verworfen.** Dear ImGui bleibt Debug- und Editor-Werkzeugen vorbehalten (F1,
Editor, Konsole, Fenster „AI“ usw.); die bisherigen ImGui-Spielfenster werden durch die neuen Bildschirme ersetzt
und bleiben höchstens als Debug-Ansicht.

Präzisiert:
- **Umfang:** Widget-Baum mit wenigen Widget-Arten (Panel/Rahmen, Text, Bild, Liste, Raster, Balken, Knopf,
  Item-Zelle mit 3D-Vorschau), Layout über Anker und Stapel, 9-Slice-Rahmen, Clipping, Fokus-Navigation für
  Tastatur, Gamepad und Maus, Bildschirm-Stapel (modal/nicht modal). Kein allgemeines Layout-Sprachsystem, kein
  CSS, keine in Skripten definierten Widgets.
- **Mechanik in C++, Aussehen als Daten:** Welche Widgets ein Bildschirm hat, steht im C++-Code des Bildschirms;
  Farben, Schriften, Größen, Abstände, Anker, Rahmen-Texturen und UI-Klänge stehen in TOML-Dateien (Stil, Layout)
  und werden im laufenden Spiel neu geladen. Alle Texte über Schlüssel bzw. Übersetzungstabellen (DE/EN).
- **Schrift:** vorab erzeugter MSDF-Atlas (Multi-Channel Signed Distance Field), zur Laufzeit nur gelesen; wie der
  Atlas entsteht, regelt ADR 0020 (Vorschlag).
- **Inventar:** Jede Zelle zeigt das 3D-Modell des Items nach `assets/source/data/inventory_views.toml` (Vertrag
  figuren → engine, `docs/modules/ui.md` „Inventar-Ansichten“); keine PNG-Symbole.
- **Optionen:** Die Lautstärken der Busse aus `engine.toml [audio]` sind im Optionsmenü einstellbar.
- **Schichten:** `ui` kennt keine `gameplay`- oder `world`-Typen. Die Bildschirme bekommen reine Daten-Structs
  (Ansichtsmodelle), die `runtime` füllt, und melden Aktionen zurück – wie heute die ImGui-Panels.

Plan und Teile: `docs/modules/ui.md` „Plan M14“.

## Konsequenzen
- Eigener Code für Textsatz (UTF-8, Umbruch, Kerning), Layout und Navigation; dafür keine neue
  Laufzeit-Abhängigkeit und volle Kontrolle über Stil und Gamepad-Verhalten.
- Die Schrift braucht einen Erzeugungsschritt im Cooker (ADR 0020).
- Die ImGui-Spielfenster (`InventoryPanel`, `DialogPanel`, `TradePanel` …) werden schrittweise abgelöst; ihre
  Ansichtsmodelle dienen als Ausgangspunkt der neuen Bildschirme.
