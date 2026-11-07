# 0022 – Editor: Undo/Redo als Befehle mit JSON-Zuständen

- **Status:** Vorgeschlagen (engine2, 2026-11-07)
- **Datum:** 2026-11-07
- **Phase:** M16 (Teil A, `docs/modules/editor.md`)

## Kontext
Der Editor aus M4 ändert `world::Scene` direkt und hat kein Undo; beim ersten Speichern bleibt nur eine `.bak`.
M16 bringt Wegnetz, Zonen, Trigger, Licht, Mobs, Routinen, Prefabs und Gelände dazu. Alle diese Bearbeitungen
brauchen Undo/Redo. Erzeugte Daten (welt, `owner`/`generator.owned`) dürfen nicht unbemerkt überschrieben werden.
Ziel: einfach, testbar, byte-genaues Zurückstellen, keine neue Bibliothek.

## Optionen
1. **Befehle mit eigenem Do/Undo je Operation** (klassisches Command-Pattern, jede Operation kennt ihre Umkehrung):
   flexibel und klein im Speicher. Aber jede Operation braucht von Hand geschriebenen Rückweg-Code, eine Fehlerquelle
   je Art (Komponenten, Kinder, IDs).
2. **Befehle mit Vorher-/Nachher-Zustand als JSON** in der Form des Weltformats (`writeVobJson`/`parseVobJson`,
   Wegnetz- und Zonen-Einträge): Undo setzt den gespeicherten Zustand wieder ein. Ein Mechanismus für alle Arten,
   byte-genau, nutzt vorhandene und getestete Serialisierer (auch vom Spielstand-Delta M15 benutzt). Kosten: etwas
   mehr Speicher je Schritt (ein Vob ≈ 200–500 Byte JSON), Gelände braucht eine eigene Form (Höhen-Delta).
3. **Ganze Welt als Schnappschuss je Schritt** – trivial, aber bei Leonberg (12 000 Vobs, mehrere MB) zu groß und
   zu langsam.
4. **Bibliothek** (z. B. ein Undo-Framework) – für C++ nichts Verbreitetes, das zu EnTT/unseren Formaten passt;
   neue Abhängigkeit.

## Entscheidung (Vorschlag)
**Option 2**: `Command` mit `apply`/`revert` (und `merge` für Ziehschritte), der Zustand eines Befehls ist der
JSON-Zustand des Betroffenen vorher und nachher (Vob mit Kindern, Wegnetz-Eintrag, Zone, Trigger); eigene
Unterklassen nur dort, wo JSON nicht passt (Gelände-Pinsel: Rechteck der Höhen vorher/nachher). `History` mit
begrenzter Tiefe (`[editor] undo_depth`, Vorschlag 200), Markierung des gespeicherten Standes. Eigentumsprüfung
(`Ownership`) sitzt vor dem Ausführen eines Befehls.

## Konsequenzen
- Neue Dateien in `tools/editor` (`Commands`, `Ownership`); keine neue Abhängigkeit, kein neues Modul.
- Jede Editor-Operation wird als Befehl geschrieben; Tests prüfen je Art „tun, rückgängig → gleiche Bytes“.
- Play-in-Editor (Teil E) nutzt denselben Mechanismus für seinen Schnappschuss vor dem Start.
- Undo gilt je Sitzung, nicht über Speichern/Neustart hinaus (Frage 3 im Plan).
