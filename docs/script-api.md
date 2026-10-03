# Skript-API (Lua)

Erzeugt aus den Bindings der Engine (`gothar --script-api=docs/script-api.md`) – nicht von Hand bearbeiten.
Sprache, Sandbox, Lade-Reihenfolge und Instanzen: `docs/modules/script.md`.

## Ereignisse

### `on("scripts_reloaded", fn())`
Nachdem geänderte Skripte neu geladen wurden (Entwicklung); die Story-Variablen bleiben erhalten.

### `on("world_loaded", fn(world: string))`
Nachdem eine Welt geladen ist (auch nach einem Weltwechsel); `world` ist ihr Pfad.

## Grundlagen

### `after(seconds: number, fn: function) -> integer`
Ruft `fn` einmal nach `seconds` Sekunden Spielzeit auf; gibt die Nummer des Timers zurück.

### `cancel(timer: integer) -> boolean`
Hält einen Timer von `after`/`every` an; `false`, wenn es ihn nicht (mehr) gibt.

### `emit(event: string, ...) -> integer`
Löst das Ereignis `event` mit den übrigen Argumenten aus; gibt die Zahl der aufgerufenen Funktionen zurück.

### `every(seconds: number, fn: function) -> integer`
Ruft `fn` alle `seconds` Sekunden Spielzeit auf (`seconds` > 0); gibt die Nummer des Timers zurück.

### `instance(kind: string, name: string) -> table | nil`
Die Felder einer Instanz (`instance("Item", "it_apple").value`), Funktionen darin aufrufbar; `nil`, wenn es sie nicht gibt.

### `instances(kind: string) -> {string}`
Die Namen aller Instanzen einer Art, alphabetisch (`instances("Npc")`).

### `on(event: string, fn: function)`
Ruft `fn(...)` bei jedem Ereignis `event` auf (z. B. `"world_loaded"`); die Engine nennt ihre Ereignisse in dieser Datei.

### `print(...)`
Schreibt die Werte, mit Tabulatoren getrennt, ins Log bzw. in die Konsole.

### `require(module: string) -> any`
Lädt `module` (Punkte trennen Ordner: `"lib.util"` = `lib/util.lua`) einmal und gibt sein Ergebnis zurück; nur unterhalb des Skript-Ordners.

### `Story`
Globale Tabelle der Story-Variablen (Zahlen, Strings, Wahrheitswerte, verschachtelte Tabellen); wird mit dem Spielstand gespeichert. Funktionen darin sind nicht erlaubt.

## Welt

### `insert(instance: string, count?: integer) -> boolean`
Setzt ein Item (vor der Spielfigur auf den Boden, `count` Stück nebeneinander) oder einen NPC (vor die Spielfigur, ihr zugewandt) in die Welt. Ohne `mesh` erhält ein Item einen Platzhalter nach `category`.

### `teleport(start: string) | teleport(x: number, y: number, z: number)`
Setzt die Spielfigur (bzw. ohne Spielfigur die Kamera) auf einen Startpunkt der Welt oder an eine Position in Metern.

### `time(hour: integer, minute?: integer)`
Stellt die Uhrzeit des Spiels (der Tag bleibt).

### `where() -> {x, y, z, yaw, world, time}`
Position (Meter) und Blickrichtung (Grad) der Spielfigur bzw. der Kamera, die Welt und die Uhrzeit.
