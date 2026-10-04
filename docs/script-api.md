# Skript-API (Lua)

Erzeugt aus den Bindings der Engine (`gothar --script-api=docs/script-api.md`) – nicht von Hand bearbeiten.
Sprache, Sandbox, Lade-Reihenfolge und Instanzen: `docs/modules/script.md`.

## Ereignisse

### `on("assess_enter_room", fn(npc: string, owner: string, area: string))`
Der Spieler betritt einen privaten Bereich (Trigger mit `owner`) des NPCs bzw. seiner Gilde; der NPC sieht ihn oder ist in der Nähe.

### `on("assess_fighter", fn(npc: string, distance: number, what: string))`
Der NPC sieht den Spieler mit gezogener Waffe (`what`: `"weapon"` oder `"fists"`); einmal je Ziehen.

### `on("assess_noise", fn(npc: string, kind: string, x, y, z))`
Der NPC hört ein Geräusch (`kind`: `"run"`, `"lockpick"`, `"lock_broken"` … oder aus noise()).

### `on("assess_player", fn(npc: string, distance: number))`
Der NPC sieht den Spieler (neu, oder wieder nach `Perception.forget_seconds`).

### `on("assess_theft", fn(npc: string, owner: string, item: string, count: integer))`
Der NPC sieht, wie der Spieler etwas stiehlt bzw. beim Taschendiebstahl erwischt wird (`item` leer).

### `on("assess_use_mob", fn(npc: string, owner: string, mob: string))`
Der NPC sieht, wie der Spieler einen fremden Mob (Truhe, Tür …) benutzt oder knackt.

### `on("item_crafted", fn(recipe: string))`
Der Held hat an einem Mob etwas hergestellt (Amboss: nach seinen Schlägen).

### `on("item_taken", fn(item: string, count: integer))`
Der Held hat einen Gegenstand aus der Welt aufgehoben (Aktionstaste auf ein Item im Fokus).

### `on("item_used", fn(item: string))`
Der Held hat ein Item benutzt (nach seiner Wirkung).

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

### `on("npc_arrived", fn(npc: string, target: string))`
Ein NPC ist an seinem Ziel angekommen (npc_goto).

### `on("npc_blocked", fn(npc: string, target: string))`
Ein NPC kommt nicht weiter und hat aufgegeben (nach mehrfachem Neuplanen).

### `on("npc_said", fn(npc: string, text: string))`
Ein NPC hat etwas gesagt (npc_say).

### `on("pickpocket", fn(npc: string, item: string))`
Der Held hat einem NPC etwas aus der Tasche gezogen.

### `on("pickpocket_failed", fn(npc: string))`
Der NPC hat den Taschendiebstahl bemerkt (die Reaktion folgt mit M9).

### `on("scripts_reloaded", fn())`
Nachdem geänderte Skripte neu geladen wurden (Entwicklung); die Story-Variablen bleiben erhalten.

### `on("slept", fn(hour: integer))`
Der Held hat im Bett bis zu dieser Stunde geschlafen (8, 12, 20 oder 0); LP und Mana sind voll.

### `on("theft", fn(owner: string, item: string, count: integer))`
Der Held hat fremden Besitz genommen (Item-Vob mit owner, Truhe eines anderen).

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

### `use_item(item: string)`
Der Held benutzt ein Item aus dem Inventar (Nahrung, Trank, Schriftstück) – nur im Stand, sonst „Nicht jetzt.“. Die Wirkung (`effects`) kommt beim Event `use` des Clips.

## Mobs

### `mob_state(vob: string) -> {definition, type, name, locked, open}`
Zustand eines Mob-Vobs dieser Welt (Name des Vobs, z. B. "LAGER_TRUHE").

### `owned_by(vob: string) -> string | nil`
Wem ein Item- oder Mob-Vob dieser Welt gehört (Npc oder Gilde); nil, wenn niemandem.

### `unlock(vob: string)`
Schließt ein Mob-Vob auf (Truhe, Tür), etwa wenn eine Quest es öffnet.

## NPCs

### `insert_npc(npc: string, at?: string) -> boolean`
Setzt ein Npc an einen Wegpunkt oder Freepoint (ohne `at`: an den Ort des passenden Eintrags seines Tagesablaufs, `routine` der Instanz) und startet den Tagesablauf.

### `npc_clear(npc: string)`
Leert die Befehlsliste des NPCs (er bleibt, wo er ist).

### `npc_follow_player(npc: string, seconds?: number, distance?: number)`
Reiht ein: dem Spieler `seconds` Sekunden lang (Vorgabe 10) auf etwa `distance` Meter (Vorgabe 2) folgen und ihn ansehen (Drohen, Begleiten).

### `npc_goto(npc: string, target: string, run?: boolean)`
Reiht ein: Der NPC geht (oder rennt) über das Wegnetz zu einem Wegpunkt oder Freepoint (Name ohne Rücksicht auf Groß- und Kleinschreibung). Ankunft: Ereignis `npc_arrived`.

### `npc_goto_freepoint(npc: string, type: string, radius?: number, run?: boolean)`
Reiht ein: zum nächsten freien Freepoint dieses Typs (`"SIT"`, `"CAMPFIRE"` ...) im Umkreis (Vorgabe 10 m), reserviert ihn und dreht sich in seine Richtung. Gibt es keinen, bleibt er stehen.

### `npc_goto_player(npc: string, distance?: number, run?: boolean)`
Reiht ein: zum Spieler gehen (bzw. rennen), bis auf `distance` Meter (Vorgabe 1,5).

### `npc_play(npc: string, ambient: string, item?: string)`
Reiht ein: eine Tagesablauf-Animation (`"sit_ground"`, `"guard"` ... – Zustände amb_<x>_in, amb_<x>, amb_<x>_out des Menschen-Graphen; `"idle_look"` und `"react_warn"` usw. direkt), bis npc_stop oder Gehen sie beendet. `item` (`"it_broom"`) nimmt er bei `item_to_hand` in die rechte Hand und legt es bei `item_from_hand` bzw. am Ende weg.

### `npc_say(npc: string, text: string)`
Reiht ein: einen Satz sagen (bis zu den Dialogen in M10 eine Einblendung in der Nähe des Helden; Ereignis `npc_said`).

### `npc_start_state(npc: string, state: string, at?: string)`
Unterbricht: beendet den laufenden Zustand (finish) und startet einen anderen. Endet er ("done"), greift wieder der Tagesablauf.

### `npc_state(npc: string) -> {state, routine, ambient, at, commands, animation, walking, x, y, z}`
Zustand, Tagesablauf, Tagesablauf-Animation, Ort, Länge der Befehlsliste, Zustand des Animationsgraphen, ob er gerade geht, und seine Position.

### `npc_stop(npc: string)`
Reiht ein: die laufende Tagesablauf-Animation beenden (_out).

### `npc_turn(npc: string, point: string)`
Reiht ein: in die Richtung (`dir`) eines Wegpunkts oder Freepoints drehen.

### `npc_turn_to_player(npc: string)`
Reiht ein: sich zum Spieler drehen.

### `npc_wait(npc: string, seconds: number)`
Reiht ein: warten.

### `set_routine(npc: string, routine: string)`
Wechselt den Tagesablauf (Kapitelwechsel); der passende Eintrag beginnt sofort. `""` schaltet ihn ab (der NPC tut dann nur, was Skripte ihm auftragen).

## Wahrnehmung

### `draw_weapon() -> string`
Zieht die ausgerüstete Nahkampfwaffe (ohne sie die Fäuste) bzw. steckt sie weg, wie die Taste draw_weapon; gibt zurück, was danach gezogen ist (wie player_weapon).

### `noise(x: number, y: number, z: number, radius: number, kind?: string)`
Ein Geräusch: NPCs im Umkreis (mal ihrem Gehör) bekommen `assess_noise(npc, kind, x, y, z)`.

### `npc_distance_to_player(npc: string) -> number`
Abstand des NPCs zum Spieler in Metern.

### `npc_sees_player(npc: string) -> boolean`
Ob der NPC den Spieler gerade sieht (Sichtkegel, Reichweite, freie Sicht).

### `player_inside(area: string) -> boolean`
Ob der Spieler im Trigger `area` (Vob-Name, z. B. ein privater Bereich) steht.

### `player_weapon() -> string`
Was der Held gezogen hat: `"none"`, `"weapon"` (Nahkampfwaffe) oder `"fists"`.

## Welt

### `insert(instance: string, count?: integer) -> boolean`
Setzt ein Item (vor der Spielfigur auf den Boden, `count` Stück nebeneinander) oder einen NPC (vor die Spielfigur, ihr zugewandt) in die Welt. Ohne `mesh` erhält ein Item einen Platzhalter nach `category`.

### `teleport(start: string) | teleport(x: number, y: number, z: number)`
Setzt die Spielfigur (bzw. ohne Spielfigur die Kamera) auf einen Startpunkt der Welt oder an eine Position in Metern.

### `time(hour: integer, minute?: integer)`
Stellt die Uhrzeit des Spiels (der Tag bleibt).

### `where() -> {x, y, z, yaw, world, time}`
Position (Meter) und Blickrichtung (Grad) der Spielfigur bzw. der Kamera, die Welt und die Uhrzeit.
