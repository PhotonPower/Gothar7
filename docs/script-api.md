# Skript-API (Lua)

Erzeugt aus den Bindings der Engine (`gothar --script-api=docs/script-api.md`) – nicht von Hand bearbeiten.
Sprache, Sandbox, Lade-Reihenfolge und Instanzen: `docs/modules/script.md`.

## Ereignisse

### `on("item_crafted", fn(recipe: string))`
Der Held hat an einem Mob etwas hergestellt (Amboss: nach seinen Schlägen).

### `on("item_taken", fn(item: string, count: integer))`
Der Held hat einen Gegenstand aus der Welt aufgehoben (Aktionstaste auf ein Item im Fokus).

### `on("level_up", fn(level: integer))`
Der Held hat eine neue Stufe erreicht.

### `on("lock_picked", fn(mob: string))`
Ein Schloss wurde mit dem Dietrich geknackt.

### `on("lockpick_broken", fn(mob: string))`
Beim Knacken ist ein Dietrich abgebrochen.

### `on("mob_locked", fn(mob: string))`
Der Held wollte ein verschlossenes Mob benutzen, ohne Schlüssel und Dietrich.

### `on("mob_used", fn(mob: string, type: string))`
Der Held hat ein Mob benutzt (Truhe offen, Tür bewegt); `mob` ist die Mob-Instanz.

### `on("scripts_reloaded", fn())`
Nachdem geänderte Skripte neu geladen wurden (Entwicklung); die Story-Variablen bleiben erhalten.

### `on("slept", fn(hour: integer))`
Der Held hat im Bett bis zu dieser Stunde geschlafen (8, 12, 20 oder 0); LP und Mana sind voll.

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

## Held

### `add_xp(amount: integer) -> integer`
Gibt dem Helden Erfahrung; gibt die Zahl der neuen Stufen zurück. Jede Stufe bringt `Progression.learn_points_per_level` Lernpunkte und löst `level_up` aus.

### `drop_item(item: string, count?: integer)`
Legt `count` (Vorgabe 1) Stück aus dem Inventar des Helden vor ihm auf den Boden; Ausgerüstetes wird dabei abgelegt.

### `equip(item: string) -> string`
Rüstet ein Item aus dem Inventar aus; gibt den Platz zurück (`melee`, `ranged`, `armor`, `helmet`, `ring1`/`ring2`, `amulet`, `belt`, `rune1`–`rune7`). Fehler, wenn Bedingungen (`requires`) fehlen.

### `equipped(slot: string) -> string | nil`
Was auf dem Platz `slot` ausgerüstet ist.

### `give_item(item: string, count?: integer) -> integer`
Gibt dem Helden `count` (Vorgabe 1) Stück eines Items; gibt die neue Anzahl zurück.

### `hero() -> {name, guild, level, xp, next_xp, learn_points}`
Name, Gilde, Stufe, Erfahrung, Erfahrung bis zur nächsten Stufe und Lernpunkte des Helden.

### `inventory() -> {{item, count, name, category}}`
Das Inventar des Helden, nach Kategorie sortiert (Waffen zuerst, wie in Gothic), ohne Gewichtsgrenze.

### `item_count(item: string) -> integer`
Wie viele Stück eines Items der Held hat.

### `remove_item(item: string, count?: integer) -> boolean`
Nimmt dem Helden `count` Stück weg; `false` (und nichts weggenommen), wenn er weniger hat. Ausgerüstetes wird dabei abgelegt.

### `set_stat(name: string, value: integer)`
Setzt ein Attribut des Helden; `hp`/`mana` bleiben zwischen 0 und dem Maximum.

### `set_talent(name: string, level: integer)`
Setzt die Stufe eines Talents des Helden.

### `stat(name: string) -> integer`
Ein Attribut des Helden (`hp`, `hp_max`, `mana`, `mana_max`, `str`, `dex`) oder ein Schutzwert (`protection_edge`, `_blunt`, `_point`, `_fire`, `_magic`, `_fall`, mit Ausrüstung).

### `talent(name: string) -> integer`
Stufe eines Talents des Helden (0 = nicht gelernt): `melee_1h`, `melee_2h`, `bow`, `crossbow`, `sneak`, `picklock`, `pickpocket`, `acrobatics`, `magic_circle`.

### `unequip(slot: string)`
Legt ab, was auf dem Platz `slot` ausgerüstet ist.

## Mobs

### `mob_state(vob: string) -> {definition, type, name, locked, open}`
Zustand eines Mob-Vobs dieser Welt (Name des Vobs, z. B. "LAGER_TRUHE").

### `unlock(vob: string)`
Schließt ein Mob-Vob auf (Truhe, Tür), etwa wenn eine Quest es öffnet.

## Welt

### `insert(instance: string, count?: integer) -> boolean`
Setzt ein Item (vor der Spielfigur auf den Boden, `count` Stück nebeneinander) oder einen NPC (vor die Spielfigur, ihr zugewandt) in die Welt. Ohne `mesh` erhält ein Item einen Platzhalter nach `category`.

### `teleport(start: string) | teleport(x: number, y: number, z: number)`
Setzt die Spielfigur (bzw. ohne Spielfigur die Kamera) auf einen Startpunkt der Welt oder an eine Position in Metern.

### `time(hour: integer, minute?: integer)`
Stellt die Uhrzeit des Spiels (der Tag bleibt).

### `where() -> {x, y, z, yaw, world, time}`
Position (Meter) und Blickrichtung (Grad) der Spielfigur bzw. der Kamera, die Welt und die Uhrzeit.
