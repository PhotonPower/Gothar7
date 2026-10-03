# 0017 – JSON-Bibliothek für das Weltformat `.g7world`

- **Status:** Akzeptiert (2026-10-03, Entscheidung Projektinhaber)
- **Datum:** 2026-10-03
- **Phase:** M4

## Kontext
`.g7world` ist laut `docs/modules/world.md` ein **JSON**-Textformat: versionierbar (lesbare Diffs), vom Editor
geschrieben und vom Welt-Assembler der Welt-Spur (W3, Python) erzeugt – Python kann JSON ohne Zusatzpakete
lesen und schreiben. Die Engine muss es **lesen und schreiben** (Editor, Round-Trip-Tests); stabile Ausgabe
(feste Schlüsselreihenfolge, gleiche Zahlenformatierung) ist für Diffs wichtig. Größe: einige 1000 Vobs pro Welt,
Laden einmal pro Weltwechsel – Geschwindigkeit ist zweitrangig, eine gekochte Binärvariante kann später folgen.
Vorhanden sind bereits toml++ (Konfiguration, ADR 0010) und simdjson (transitiv über fastgltf, nur lesend).

## Optionen
1. **nlohmann-json** (MIT, header-only, vcpkg `nlohmann-json`) – sehr verbreitet, bequeme API, liest und
   schreibt, `ordered_json` für stabile Schlüsselreihenfolge; langsamer und kompilierintensiv (wird nur in
   `world/src` eingebunden). In `docs/05-build.md` bereits für M4 eingeplant.
2. **simdjson** (Apache-2.0, schon transitiv da) – sehr schnell, aber **nur lesend**; Schreiben müssten wir selbst
   bauen.
3. **RapidJSON** (MIT) – schnell, liest und schreibt; umständliche API, Pflege stockt.
4. **yyjson** (MIT, C) – schnell, liest und schreibt; C-API, eigener Wrapper nötig.
5. **TOML statt JSON** über das vorhandene toml++ – keine neue Abhängigkeit; aber Abweichung von `world.md`,
   tiefe Strukturen (Vobs mit Komponenten) werden unhandlich, und Python braucht zum Schreiben ein Zusatzpaket.

## Entscheidung
**nlohmann-json**, `PRIVATE` im Modul `world` (öffentliche API ohne JSON-Typen). Ausgabe mit `ordered_json` und
fester Einrückung, Zahlen mit fester Genauigkeit, Vobs nach `id` sortiert – stabile Diffs. Version laut
vcpkg-Baseline: **3.12.0** (`"version>=": "3.12.0"` in `vcpkg.json`); `nodeps` lädt dieselbe Version per FetchContent.

## Konsequenzen
- Neue Abhängigkeit (`vcpkg.json`, Tabelle in `docs/05-build.md`), nur in `world/src` sichtbar.
- Liest der Editor später große Welten zu langsam, kommt die in `world.md` vorgesehene gekochte Binärvariante
  (`g7-cook`), ohne das Textformat zu ändern.
- Das Format `.g7world` bleibt Vertrag mit der Welt-Spur (`docs/coordination.md`).

## Nachtrag (M6)
Zweiter Nutzer: der Autopilot `tools/walk` (`g7_walk`) liest Routen und schreibt `walk.jsonl` /
`walk_summary.json` mit nlohmann-json, ebenfalls `PRIVATE`. Keine neue Abhängigkeit.
