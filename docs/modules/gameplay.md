# gameplay

**Zweck:** Spielregeln und -systeme. Werte und Formeln möglichst in Skripten; C++ stellt Mechanik bereit.

## Bewegung & Kamera (M5 Teil C, umgesetzt) – `Movement.hpp`
```cpp
namespace g7::gameplay {
struct MovementSettings { runSpeed, walkSpeed, sneakSpeed, backwardSpeed, strafeSpeed, acceleration, deceleration,
                          turnSpeedDegrees, mouseTurnPerPixel, stepHeight, maxSlopeDegrees, stickToFloor; CameraSettings camera;
                          static Result<MovementSettings> parse(std::string_view toml, std::string_view source); };
struct MoveInput { f32 forward, strafe, turn, mouseTurn; bool walk, sneak; };   // aus den Aktionen (runtime)
class PlayerMovement { Vec3 step(const MoveInput&, f32 seconds, const MovementSettings&); void reset(f32 yaw); f32 yaw() const; };
class ThirdPersonCamera { void reset(feet, yaw, const CameraSettings&);
    void update(seconds, feet, yaw, mousePitchPixels, const CameraSettings&, const Obstruction&);   // je Bild
    Vec3 position() const; Quat rotation() const; };   // Obstruction = sphereCast der Engine
}
```
- **Werte: `assets/source/data/movement.toml`** (Spielgefühl; Startwerte vom Projektinhaber freigegeben).
  Die Engine lädt die Datei über den `AssetManager` (`[game] movement`, Hot-Reload im laufenden Spiel).
  Fehlende Schlüssel behalten die Vorgabe; falsche Typen oder Werte sind Fehler, dann gelten die eingebauten
  Vorgaben mit einer Warnung.
- **Gangart wie Gothic** (Entscheidung Projektinhaber): Standard ist **Rennen** (4,0 m/s). Gehalten
  `walk` (Shift) heißt Gehen (1,6), `sneak` Schleichen (1,1). Rückwärts (1,4) und seitwärts (2,0) sind nie
  schneller als die Gangart, diagonal nicht schneller als der schnellere Anteil. Beschleunigen mit 12 m/s²,
  Bremsen mit 16 m/s².
- **Drehen:** klassisch A/D mit 180°/s, die Maus in beiden Schemata (0,15°/Pixel). Gieren 0 = Blick nach −Z.
- **Kamera:**
  - Hinter der Figur, Blickpunkt 1,55 m über den Füßen, Abstand 3 m, Grundneigung 12° nach unten.
  - Die Maus neigt zwischen −40° und +60°.
  - Position und Gieren folgen exponentiell gedämpft (0,12 s bzw. 0,25 s; kein Überschwingen; Drehen den
    kurzen Weg).
  - **Wände:** Ein `sphereCast` (r 0,2 m) vom Blickpunkt nach hinten verkürzt den Abstand, nie unter 0,6 m.
    Hinein geht es sofort (nie durch die Wand), heraus weich mit `return_lag` (0,25 s), damit die Kamera an
    Hauswänden und Türen nicht springt.
  - **Drinnen** (`[camera.indoor]`, für welts begehbare Häuser): Liegt an mindestens vier von fünf Punkten (über
    dem Helden und 1 m um ihn) ein Dach tiefer als 4 m (`ceiling`), wird weich (`blend_seconds` 0,5 s) auf das
    Innenprofil überblendet: Abstand 1,9 m, Blickpunkt 1,5 m, Mindestabstand 0,4 m, Kugel 0,15 m. Die Neigung
    bleibt die des Spielers. Unter einem vorkragenden Obergeschoss (0,5 m über der Straße) bleibt es draußen.
    In einem Raum der Weltdatei (Zone `indoor`, world.md) bleibt es auch ohne Dach-Treffer drinnen, etwa auf
    einer Treppe unter dem Deckenausschnitt, wo das schräge Dach höher liegt (welt, W7).
    Werte zum Nachstellen durch den Projektinhaber.
  - Modi: drinnen `[camera.indoor]`, Kampf `[camera.combat]` (M11), Dialog `data/dialog.lua` (M10).
- **Springen, Klettern, Fallen (Teil D; Werte `[jump]`, `[climb]`, `[fall]`, Entscheidungen Projektinhaber):**
  - **Sprung** (Taste `jump`) aus Stand bzw. Gehen 0,9 m hoch, aus dem Rennen 1,1 m und damit weiter
    (~3,8 m). Keine Luftsteuerung. Nach der Landung 0,2 s Sperre.
    `jumpSpeed(h) = √(2 g h)` mit `kGravity` = 9,81.
  - **Kanten:** `jump` vor einer Kante (Wand höchstens 0,6 m von der Figur, Oberseite mit Platz für die Figur)
    klettert statt zu springen.
    - Klassen (`classifyLedge`): niedrig ≤ 1,0 m, mittel ≤ 1,6 m, hoch ≤ 2,2 m; darüber wird gesprungen.
    - Kanten bis zur Stufenhöhe (0,4 m) geht man hinauf.
    - Bis zu den Animationen (M6) gleitet die Figur entlang `ClimbPath`: 70 % der Zeit senkrecht, dann nach vorn,
      geglättet. Dauer 0,6 / 1,0 / 1,4 s. Die Eingabe ruht während des Kletterns.
    - Mit M6 geben die Clips `t_climb_low/mid/high` Bahn und Dauer vor; die Root-Höhe wird auf die Kante
      herunterskaliert.
  - **Fallschaden** `fallDamage(h)`: bis 4 m nichts, darüber 10 Lebenspunkte je Meter (Stadtmauer außen 7 m
    → 30). Lebenspunkte kommen mit M8; bis dahin meldet das Log `fall damage N` und `Engine::lastFallDamage()`
    zeigt den Wert. Wasser fängt jeden Fall ab (Teil E).
  - **Testwelt, Kletterplatz** (`testworld/camp.g7world`, Startpunkt `START_KLETTERPLATZ` bei (46, 0, 2), Blick
    nach Süden):
    - Einzelblöcke 0,9 / 1,5 / 2,1 / 2,6 m bei x 40/44/48/52, z 5–7: je eine Klasse, der letzte ist zu hoch.
    - Treppe aus Blöcken 1–6 m bei x 39–51, z 11–13 (immer „niedrig“). Von oben 6 m hinunter kostet 20.
    - Block: `testworld/block.gltf` aus `make_block.py` (eigener Inhalt).
- **Schwimmen und Tauchen (Teil E; Werte `[swim]`, Entscheidungen Projektinhaber):** `gameplay::Swimmer`.
  - **Modi:** Land, Schwimmen, Tauchen. Schwimmen ab 0,9 m Wasser über den Füßen (Hüfte); zurück an Land erst
    unter 0,75 m (kein Flackern am Ufer).
  - **An der Oberfläche:** Auftrieb hält die Augen 0,2 m über Wasser (Füße 1,42 m darunter). Schwimmen 1,6 m/s,
    mit `walk` 0,9 m/s; Drehen wie an Land; kein Rennen und kein Springen.
  - **Tauchen:** `sneak` (X) gehalten 1,0 m/s hinab; `jump` gehalten 1,2 m/s hinauf; nichts gedrückt: 0,5 m/s
    hinauf. Oben wird wieder geschwommen.
  - **Luft** 30 s, nur mit den Augen unter Wasser. Leer: 10 Lebenspunkte je Sekunde (`Engine::drownDamage()`, Log
    je 10 LP bis M8). An der Oberfläche füllt sie sich in 3 s auf.
  - **Fälle ins Wasser** kosten nichts (Füße unter der Oberfläche bei der Landung, oder Schwimmen beginnt vorher).
  - **Ufer:** Flaches Ufer geht man hinauf. `jump` an der Oberfläche vor einer Kante klettert hinaus (Klassen wie an
    Land, gemessen ab den Füßen).
  - **Kamera:** Sie bleibt beim Schwimmen 0,3 m über der Oberfläche, beim Tauchen folgt sie darunter. Eine
    Unterwasser-Darstellung (Färbung, Nebel) kommt mit M17.
  - **Darstellung bis M17:** je Wasser-Vob eine halbdurchsichtige blaue Fläche auf der Oberkante (beidseitig,
    ohne Kollision und Schatten; `SceneInstance::solid = false`); F2 zeigt die Boxen. Die Figur wird vor dem
    durchscheinenden Durchgang gezeichnet, damit ihr Unterwasserteil unter der Fläche liegt.
  - **Testwelt:** Teich `WASSER_TEICH` im Becken nordwestlich des Lagers (Oberfläche −4 m, bis ~7 m tief),
    Startpunkt `START_TEICH` am flachen Ostufer mit Blick aufs Wasser.
- **Engine (`runtime/src/EnginePlayer.cpp`):**
  - Die Spielfigur entsteht beim Laden einer Welt auf dem Startpunkt (`--start` bzw. kleinste id; Füße = `pos`,
    nur das Gieren).
  - Keine Figur gibt es ohne Startpunkt, mit `--editor` und mit `--benchmark`.
  - Die Bewegung läuft je festem Schritt.
  - Die Darstellung interpoliert die Füße zwischen den Schritten; die Kamera wird je Bild nachgeführt.
  - Trigger melden die Figur (Hüfthöhe) statt der Kamera.
  - **F3** (`debug_fly`) schaltet auf die freie Debug-Kamera und zurück. `teleportPlayer(feet, yaw)` zum
    Debuggen bzw. für Tests.
  - Im Spielbetrieb fängt die Maus ein (nicht bei Pause oder offenem Debug-Fenster F1).
  - Bis M6 wird die Platzhalterfigur `characters/figures/placeholder_mannequin.glb` (T-Pose) gezeichnet.
  - F2 zeigt den Zylinder, Bodennormale, Zustand und Tempo.

## Charakter (M8 Teil A, umgesetzt) – `Character.hpp`
- `gameplay::Character::fromInstance(npc, items)` liest ein `Npc`: `attributes` (`hp`, `hp_max`, `mana`, `mana_max`,
  `str`, `dex`; fehlt `hp`, startet es beim Maximum), `talents` (`melee_1h`, `melee_2h`, `bow`, `crossbow`, `sneak`,
  `picklock`, `pickpocket`, `acrobatics`, `magic_circle`; 0 = nicht gelernt), `protection` je Schadensart (`edge`,
  `blunt`, `point`, `fire`, `magic`, `fall`), `guild`, `level`, `xp`, `learn_points`, `inventory = { it_x = 3 }`,
  `equipment = { "it_y" }`. Unbekannte Namen sind ein Fehler mit Instanzname.
- `hp`/`mana` bleiben zwischen 0 und dem Maximum. `protection(art)` = eigener Wert + Ausrüstung.
- Erfahrung: `addExperience(xp, xpForLevel, lernpunkteJeStufe)`; Formel und Lernpunkte in `data/progression.lua`
  (`Progression.xp_for_level`, `learn_points_per_level`; Gothic 1: Stufe n bei 500·n(n+1)/2, 10 LP je Stufe).
- Gilden-Einstellungen: `Attitudes`-Tabelle und `attitude(a, b)` in `data/guilds.lua` (gleiche Gilde freundlich,
  fehlend neutral); die KI wertet sie mit M9 aus.
- Der Held ist das `Npc "pc_hero"` (`game/scripts/npcs/hero.lua`); `Engine::hero()` baut ihn nach dem Laden der
  Skripte und behält ihn beim Neuladen. Skriptfunktionen (Gruppe „Held“ in `docs/script-api.md`): `hero`, `stat`,
  `set_stat`, `talent`, `set_talent`, `add_xp` (Ereignis `level_up`), `give_item`, `remove_item`, `item_count`,
  `inventory`, `equip`, `unequip`, `equipped`.

## Items & Inventar (M8 Teil A, umgesetzt)
- Item-Instanz aus dem Skript (`gameplay::itemInfo`) + Laufzeit-Menge (`ItemStack`). `category` ist eine von
  `kItemCategories` (das Schema prüft sie): `melee_1h`, `melee_2h`, `bow`, `crossbow`, `ammo`, `armor`, `helmet`,
  `ring`, `amulet`, `belt`, `rune`, `scroll`, `potion`, `food`, `document`, `key`, `torch`, `misc`.
- Inventar ohne Gewichtslimit (wie Gothic), sortiert nach Kategorie (diese Reihenfolge), dann nach Name.
- Ausrüstungs-Plätze (`EquipSlot`, Skriptnamen): `melee`, `ranged`, `armor`, `helmet`, `ring1`/`ring2`, `amulet`,
  `belt`, `rune1`–`rune7`. Ringe nehmen den freien der zwei Plätze (sonst den ersten), Runen/Spruchrollen den ersten
  freien. `requires = { str = 20, bow = 1 }` (Attribute oder Talente) muss erfüllt sein. Entfernen eines
  ausgerüsteten Items legt es ab.
- **Benutzen** (Teil D, Werte und Regel Projektinhaber 2026-10-04): „use“ im Inventar-Fenster bzw. `use_item(item)`,
  **nur im Stand** (sonst „Nicht jetzt.“). Nahrung `use_eat` (`none/t_eat`), Tränke `use_drink` (`t_drink`),
  Schriftstücke `use_read` (`t_read_scroll`). Beim Event `use` wirken `effects = { hp = …, mana = … }` (bis zum
  Maximum), dann `on_use(item)` und das Ereignis `item_used`; Nahrung und Tränke sind verbraucht. Schriftstücke öffnen
  ihren `text` im Fenster „Document“ und bleiben im Inventar. Werte: Apfel +5, Brot +10, kleiner Heiltrank +40 LP.
- **Taschendiebstahl** (Teil D, wie Gothic 1): Aktionstaste beim Schleichen auf einen NPC im Fokus. Nur mit dem
  Talent `pickpocket` („Das kann ich nicht.“); mit Geschick ≥ `pickpocket_dex` des NPCs (Vorgabe
  `Pickpocketing.default_dex` = 30) gelingt er sicher: ein nicht getragenes Stück eines zufälligen Stapels, Ereignis
  `pickpocket(npc, item)`; sonst merkt er es, Ereignis `pickpocket_failed(npc)` (Reaktion mit M9). Je NPC ein Versuch.
  Eingefügte NPCs haben dafür ein Laufzeit-Inventar aus ihrer `Npc`-Instanz.
- **Besitz** (Teil D): Item-Vobs mit `owner` (world.md), Mobs mit `owner` in ihrer Lua-Definition. Nimmt der Held
  ein fremdes Item auf oder etwas aus einer fremden Truhe, kommt `theft(owner, item, count)`; `owned_by(vob)` fragt
  den Besitzer ab. Zeugen und Reaktionen (Wachen, Gildenmitglieder) folgen mit M9.
- **Modelle:** `Item.mesh = "items/<id>.glb"` (figuren, F6: Ursprung am Griffpunkt, Griffachse +Y; alle Schlüssel
  `items/it_key.glb`), sonst ein Platzhalter je Kategorie. Liegend ruht ein Gegenstand auf seiner breiten Seite – die
  dünnste Achse zeigt nach oben (Schwert, Schlüssel, Brot liegen flach), Tränke stehen; der tiefste Punkt liegt auf
  dem Boden.
- **In der Welt** (Teil B): Vob-Typ `item` (world.md), zur Laufzeit `Engine::spawnItem`, `insert('it_x')`,
  `drop_item('it_x', n)` (legt vor den Helden). **Aufheben** mit der Aktionstaste (klassisch Strg, modern E) auf ein Item
  im Fokus (`Engine::pickUpFocus`): Der Held bleibt stehen und spielt `none/t_pickup_ground` (Zustand `pickup` im
  Graphen, Parameter `pickup`); beim Event `pickup` wandert das Item ins Inventar, der Vob verschwindet, Skripte
  bekommen `item_taken(item, count)`. Ohne `pickup`-Zustand im Graphen geht es nach 0,35 s.
- **Fackel** (Entscheidung Projektinhaber 2026-10-08, wie Gothic 1; `runtime/EngineTorch.cpp`): Kategorie `torch`
  (`it_torch`, Modell von figuren mit `socket_flame` 0,6 m den Schaft hinauf), ohne Ausrüstungsplatz. Benutzen zündet
  sie an (`none/t_torch_light`: Event `torch_take` nimmt sie in die linke Hand, `torch_light` startet Flamme und Licht
  `data/fx/torch.toml`), danach die additive Haltung `none/a_torch_hold` über `clavicle_l` (Bezug `none/a_neutral`).
  Nochmal Benutzen steckt sie weg. Sie brennt ohne Grenze, solange sie gehalten wird; sichtbar mit Fäusten und
  Einhandwaffe. Zweihandwaffe, Bogen, Armbrust, Magie und Verwandlung stecken sie weg; Wasser löscht sie.
  Fallen gelassen (`hero_torch_drop`, `none/t_torch_drop`, Event `torch_drop`) liegt sie brennend am Boden, bis sie
  aufgehoben wird. Lua: `hero_torch()`, `hero_torch_drop()`, `hero_torch_lit()`, Ereignisse `torch_lit`/`torch_out`.
- **Inventar-Fenster** (Tab, bis zum Inventar-Bildschirm in M13): Werte, Schutz, Inhalt nach Kategorie,
  Ausrüsten/Ablegen/Wegwerfen; solange es offen ist, steht der Held und die Maus ist frei.

## Fokus (M8 Teil B, umgesetzt) – `Focus.hpp`
- `gameplay::selectFocus(kandidaten, auge, blickrichtung, settings, aktuell, sichtbar)`: Kandidaten (`FocusCandidate`:
  id, Art `Npc`/`Mob`/`Item`, Fokuspunkt) innerhalb von Reichweite und Winkel ihrer Art; **Vorrang NPC > Mob > Item**,
  innerhalb einer Art der kleinste Anteil `Winkel/Grenze + ½ · Abstand/Reichweite`. Der Winkel wird **waagrecht**
  gemessen (ein Gegenstand zu Füßen ist genauso „vorn“), dazu höchstens `height` Höhenunterschied.
- **Hysterese:** Der aktuelle Fokus bleibt innerhalb von `keep` × Reichweite/Winkel, solange keine höhere Art in
  Reichweite kommt. **Sicht:** Ein Strahl vom Auge zum Fokuspunkt gegen die Weltkollision; was das Ziel trägt, verdeckt
  es nicht (0,3 m Spielraum).
- Werte in `assets/source/data/focus.toml` (`[npc]`/`[mob]`/`[item]` mit `distance`, `angle`; `keep`, `height`):
  NPC 8 m/30°, Mob 3 m/35°, Item 2,5 m/35°.
- Engine (`EngineItems.cpp`): einmal je Frame nach der Simulation, vom Auge des Helden (Füße + 1,6 m) in seiner
  Blickrichtung; Kandidaten sind Item-Vobs (Mitte ihrer Box), Mobs (1 m über dem Ursprung) und Kreaturen/NPCs.
  `Engine::focus()` liefert Art, ID und Namen (Item: Skriptname, ab 2 Stück „Apfel (3)“; Mob: vorerst die
  Definition, Teil C den Namen; NPC: `name` der `Npc`-Instanz). Der Name steht über dem Ziel (bis zum HUD in M13 als
  Text im Debug-UI-Kontext); im Flugmodus kein Fokus.
- Im Kampf (M11): Gegner-Fokus mit Ziel-Lock.

## Mob-Interaktion (M8 Teil C, umgesetzt) – `Mobs.hpp`, `EngineMobs.cpp`
- **Mob-Typen** (`assets/source/data/mobs.toml` v1, Vertrag engine–figuren–welt, characters-pipeline.md §3.1):
  Clips `enter`/`loop`/`leave`/`extra` und Benutzer-Slots (`pos` = Fußpunkt im Mob-Raum, `facing`); Achsen Y oben,
  Ursprung am Boden, Vorderseite +Z. `gameplay::MobTypes::parse`, `placeSlot`, `chooseSlot` (nächster freier Slot).
- **Mob-Definition** in Lua (`Mob "mob_x" { name, type, lock, key, contents, owner, on_use }`): Der Vob verweist mit
  `components.mob.definition` darauf; `type` wählt den Mob-Typ. Eine Definition, die keine Lua-Instanz ist, gilt als
  bloßer Typ (offen, leer). `name` ist der Fokusname.
- **Ablauf:** Aktionstaste auf ein Mob im Fokus (`Engine::useMob`) → der Held geht zum nächsten Slot (1,6 m/s, ohne
  Kollision, dreht sich zum Mob) → `<typ>_enter` (Zustand im Menschen-Graphen, Clip aus `mobs.toml`) → bei `loop`
  `<typ>_loop` bis zum Verlassen (Rückwärts- oder Aktionstaste, Inventar schließen) → `<typ>_leave` → zurück nach
  `move`. Während der Benutzung steht der Held fest auf dem Slot. Ohne Zustand im Graphen dauert eine Phase 0,6 s.
  Events `open`/`close` (sonst spät im Clip) öffnen bzw. schließen; Skripte bekommen `mob_used(mob, type)` und den
  Hook `on_use(mob)` am Ende des Einstiegs.
- **Truhen:** offen zeigt das Inventar-Fenster ihren Inhalt neben dem des Helden (nehmen / hineinlegen).
- **Türen:** Der Tür-Mob ist das Türblatt (Ursprung an der Angel); derselbe Clip öffnet und schließt, die Tür dreht
  sich in 0,8 s um 90° um +Y, die Kollision dreht mit (der Körper wird je Schritt neu gesetzt). Die Slots gehören zur
  geschlossenen Tür.
- **Schlösser** (Entscheidung Projektinhaber 2026-10-04, wie Gothic 1): Mit dem `key`-Item schließt der Held auf dem
  Weg auf. Sonst mit einem Dietrich (`Lockpicking.item`): Phase `picklock` (`chest_picklock`), Fenster „Lockpick“, links
  /rechts drehen (Dreh- oder Seitwärts-Tasten) nach der Kombination (`lock = "LRRL"`); ein falscher Schritt setzt
  zurück und bricht den Dietrich mit `Lockpicking.break_chance[Talent]` (ohne Talent 50 %, Talent 1: 25 %, Talent 2:
  5 %; Knacken geht auch ohne Talent). Ohne Schlüssel und Dietrich: „Verschlossen.“, Ereignis `mob_locked`.
  Ereignisse `lock_picked`, `lockpick_broken`; `unlock(vob)` schließt per Skript auf, `mob_state(vob)` gibt den
  Zustand.
- **Amboss** (C2): In der Schleife bietet das Fenster „Choice“ die Rezepte (`Recipe "rcp_x" { name, mob = "anvil",
  takes, gives, strikes }`) des Mob-Typs an; fehlt Material, steht da „Dafür fehlt: …“. Nach der Wahl zählen die
  Events `hit_anvil` von `s_work` die Schläge (ohne Clip einer je 0,7 s); nach dem letzten wird `takes` verbraucht und
  `gives` gegeben, Ereignis `item_crafted(recipe)`. Schmieden ohne Talentpflicht (Entscheidung Projektinhaber; das
  Talent kommt später als Inhalt).
- **Bett** (C2, wie Gothic 1): `t_lie_down` trägt die Figur aufs Bett (Wurzel im Clip, der Held bleibt am Slot), in
  `s_lie` fragt „Choice“ bis wann: Morgen 8:00, Mittag 12:00, Abend 20:00, Mitternacht 0:00 (`GameTime::advanceTo`,
  heute falls noch vor uns, sonst morgen). Danach LP und Mana voll, Ereignis `slept(hour)`, der Held steht auf.
- Modelle: welts `assets/source/mobs/{chest,anvil,bed,door}.glb` (#148, Achsen und Maße nach `mobs.toml`). Der
  Truhendeckel (`MOB_LID`, Scharnier-Knoten, Drehung um X) bewegt sich noch nicht – statische Modelle werden als ein
  Mesh geladen; seine `COL_`-Kinder werden mit ihrer Knotenlage gelesen.
- Mob-Zustände (offen, verschlossen, Inhalt) gelten für die Sitzung; gespeichert werden sie mit M14.

## Dialog & Quests (M10)
### Dialog-Ablauf (Teil A, umgesetzt) – `runtime/EngineDialog.cpp`
- **Ansprechen:** NPC im Fokus und Aktionstaste (schleichend bleibt es Taschendiebstahl); Konsole `talk(npc)`. Der NPC
  unterbricht seinen Zustand, dreht sich zum Spieler und steht; danach beginnt sein Tagesablauf wieder.
- **Infos** (`Info "dia_x" { npc, nr, description, important, permanent, approach, condition(npc), run(npc) }`):
  verfügbar, wenn die Bedingung gilt und die Info `permanent` oder noch nicht gesagt ist (`Story.told`,
  `info_told(name)`); Menü nach `nr`, dazu immer „Ende“. Wählt der Spieler eine, sagt der Held zuerst ihre
  `description`.
- **`important`** (Entscheidung E1): beim Ansprechen zuerst, ohne Auswahl; der NPC spricht den Spieler von sich aus an,
  sobald er ihn in 3 m sieht. Mit `approach = true` geht er bis 10 m auf ihn zu.
- **run(npc)** reiht ein: `say(wer, text)` (`wer` = der NPC oder `"hero"`), `choice(text, fn)` (Antworten; gewählt sagt
  der Held den Text, dann läuft `fn(npc)`), `end_dialog()`. Ohne Antworten folgt wieder das Menü.
- **Zeilen** (E2, E4): deutscher Text inline in Lua, Schlüssel automatisch `<info>_<nn>` (für Übersetzung und spätere
  Sprachdateien), Dauer nach Zeichenzahl (0,06 s je Zeichen, mindestens 1,5 s), mit der Aktionstaste überspringbar.
- **Darstellung** (E3, E5): unten ein dunkles Feld, Sprechername und Zeile bzw. das Menü; Pfeiltasten
  (vor/zurück) bzw. Maus, Aktionstaste wählt. Vorläufig ImGui, die echte Oberfläche mit M14.
- Ereignisse `dialog_started(npc)`, `dialog_ended(npc)`; Abfrage `dialog_state()`, `dialog_choose(n)` (Konsole, Tests).

### Darstellung (Teil B, umgesetzt)
- **Kamera** Schuss/Gegenschuss: über die Schulter des Zuhörers auf den Sprecher, im Menü auf den NPC; weich
  übergeblendet (`DialogPresentation.camera` in `data/dialog.lua`: side, height, back, look, blend). Danach wieder die
  Spielerkamera.
- **Gesten:** je Zeile eine zufällige Geste des Sprechers aus `gestures` (figurens Set `dlg`), additiv über
  `spine_02` gegen die Referenz `dlg/a_neutral` (`Animator::playOverlay(…, additive, reference)`). Sitzt der NPC
  (`keep`: Haltungen, in denen er zum Reden bleibt), nur Kopfgesten (`head_gestures`) und das nur jede zweite Zeile;
  jede andere Tagesablauf-Animation endet beim Ansprechen.
- **Mund und Blick:** Der Sprecher bewegt den Mund (FaceAnimator „talking“), der NPC sieht den Helden an und
  umgekehrt (Look-at). Gesichts-Morphs werden nach Namen zugeordnet (`SkinnedMesh::mapMorphNames`, glTF
  `extras.targetNames`), so folgt z. B. ein Bart mit denselben Morphs dem Kiefer (figuren).
- Der Fokusname wird im Dialog ausgeblendet.

### Handel und Lernen (Teil C, umgesetzt) – `runtime/EngineTrade.cpp`, `data/trade.lua`, `data/teaching.lua`
- **Handel** (Entscheidung E6, wie Gothic 1): Eine Info mit `trade = true` (oder `trade_open()` im Dialog) öffnet nach
  ihren Zeilen das Fenster „Handel mit …“: links die Waren des Händlers (kaufen), rechts die des Helden (verkaufen),
  je ein Stück pro Klick; geschlossen geht der Dialog weiter. Währung `it_gulden` („Gulden“). Der Händler verkauft
  zum vollen Wert (`Trade.sell_factor` = 1, aufgerundet) und kauft zum halben (`buy_factor` = 0,5, abgerundet,
  mindestens 1); er zahlt aus seinen Gulden. Getragenes wird nicht verkauft. Ereignisse `item_bought`, `item_sold`;
  `trade_buy`, `trade_sell`, `trade_price`, `trade_close`, `npc_item_count`.
- **Lernen** (E7): Lehrer bieten im Dialog Antworten an (`teach_menu(npc, angebote, antworten)`), z. B.
  `{ attribute = "str", amount = 1, lp = 1, gulden = 5 }` oder `{ talent = "picklock", level = 1, lp = 10 }`; `teach`
  prüft Lernpunkte, Gulden, die Grenze (100) bzw. ob das Talent schon gelernt ist, und zieht ab
  (`set_learn_points`). Kosten wie Gothic 1: Attribut +1 = 1 LP, +5 = 5 LP, Talente 5–20 LP, dazu Gulden je Lehrer.
- Testlager: der alte Mann handelt (`dialogs/old_man.lua`), der Holzfäller lehrt Stärke (`dialogs/woodcutter.lua`).

### Tagebuch und Kapitel (Teil D, umgesetzt) – `game/scripts/lib/diary.lua`, `runtime/EngineDiary.cpp`
- **Aufträge** sind `Quest`-Instanzen (name, description, topic). `quest_start(name, text?)` (einmal; erster Eintrag
  ist die description), `quest_entry`, `quest_success`, `quest_fail`, `quest_status` („none“, „running“, „success“,
  „failed“). Einträge tragen Spieltag und Uhrzeit (`where().day` ab 1).
- **Notizen** nach Themen: `note(thema, text)`.
- **Kapitel:** `chapter()`, `set_chapter(n, titel?)` – Meldung „Kapitel n: …“ und Ereignis `chapter_changed(n)`; die
  Inhalte tauschen darauf Tagesabläufe (`set_routine`) und Infos (Bedingungen auf `chapter()`).
- Alles steht in `Story` (`quests`, `notes`, `chapter`) und wird mit dem Spielstand gespeichert (M15).
- **Fenster „Tagebuch“** (Aktion `log`: N klassisch, J modern; Konsole `diary_open()`): Kapitel, Reiter Laufend,
  Erledigt, Gescheitert, Notizen; je Auftrag die Einträge. Meldungen am Bildschirm über `notice(text)`.

### Vertical Slice (Teil E) – Inhalt im Testlager
- **Geschichte** „Bauern und Wache“ (Platzhalter, eigene Texte nach ADR 0008; der Projektinhaber überarbeitet sie):
  Die Torwache schickt den Helden zur Bäuerin („Arbeit im Lager“). Botengang: Essensbündel zum Holzfäller.
  Beschaffung: ein Grobes Schwert für die Wache (am Amboss schmieden, Rohlinge in der Truhe). Konflikt: der
  Familienring der Bäuerin liegt beim alten Mann – abkaufen (30 Gulden), von der Wache holen lassen oder stehlen
  (`pickpocket_item` am Npc: ein erfolgreicher Taschendiebstahl nimmt zuerst diesen Gegenstand). Sind alle drei
  erledigt: Kapitel 2.
- Dateien: `quests/farm_work.lua`, `dialogs/*.lua`, `items/story.lua`; Übergaben mit `npc_give_item` /
  `npc_take_item`.
- **Szenario-Test** `test_engine_m10_scenario.cpp`: die drei Aufträge über die Dialog-Schnittstelle durchgespielt,
  die drei Wege zum Ring, Kapitel 2, keine Skriptfehler.

### Weiter
- Dialog-Ablauf als Sequenz: `say` (Sprache + Untertitel + Gesten + Lippensync), `choices`, `trade`, `teach`, `end`.
- Dialog-Kamera: Schuss/Gegenschuss je Sprecher.
- Tagebuch: Topics mit Status, Einträge mit Zeitstempel.

## Kampf (M11)
Plan A–E freigegeben, Entscheidungen des Projektinhabers K1–K9 (2026-10-05, wie Gothic 1; Werte in
`data/combat.lua`).

**Teil A – Kampfkern (umgesetzt, `gameplay/Combat.hpp`, `EngineCombat.cpp`):**
- **Kämpfer** (`gameplay::Fighter`) für den Helden und jedes NPC: bereit, Angriff (Trefferfenster `hit_start` …
  `hit_end`, Kombofenster `combo_start` … `combo_end`), Parade, Ausweichen, Taumeln, bewusstlos, tot. Die Clip-Events
  treiben ihn; solange ein Clip fehlt (die menschlichen Kampfclips liefert figuren), steht eine feste Zeitleiste ein
  (Treffer 0,25–0,45 s, Kombo bis 0,7 s, Ende 0,9 s). Clips: `<modus>/t_attack_combo<n>`, `t_attack_l/r`, `t_parry`,
  `t_dodge_back`, `none/t_hit_light`, `none/t_ko`, `none/t_die_front` (Modus `fist`, `1h`, `2h`).
- **Kombos (K4):** Talent 0 Einzelschläge, 1 bis 3, 2 bis 4 und 1,25-mal schneller; der nächste Schlag nur im
  Kombofenster. Seitenhiebe ketten nicht. Talent: `melee_2h` für Zweihänder, sonst `melee_1h` (auch Fäuste).
- **Treffer:** im Trefferfenster jedes Ziel höchstens einmal je Schlag, in Reichweite (ab den Körpern: Faust 0,9 m,
  Einhand 1,3 m, Zweihand 1,7 m) und im Winkel (±50°) vor dem Angreifer. Eine Waffen-Kapsel entlang der Animation
  folgt mit den Clips.
- **Schaden (K2, K3):** je Schadensart max(Waffe − Schutz, 0), die Stärke zur Hauptart der Waffe (Fäuste: stumpf),
  mindestens 5; kritisch nach Talent 0/10/20 % mit doppeltem Waffenschaden. Tiere: `Npc.damage` (Art) plus Stärke.
- **Parade (K6):** blockt 0,4 s ab Beginn Schläge von vorn (±60°); der Schlag prallt ab (der Angreifer taumelt).
  Fäuste parieren keine Waffen, Tiere lassen sich nicht parieren.
- **Folgen (K7, K8):** Treffer lassen taumeln. Menschen, von Menschen auf 0 geschlagen, werden bewusstlos (1 LP,
  30 s, dann stehen sie auf); ein Schlag auf den Liegenden tötet; Tiere sterben (und töten). Der Held stirbt nie: er
  bleibt 5 s liegen und steht am Ort mit einem Zehntel seines Lebens auf (bis M15). Bewusstlose und Tote: keine
  Routine, kein Gehen.
- **Lua:** `npc_attack(npc, "front"|"left"|"right")`, `npc_parry`, `npc_dodge`, `hero_attack`, `hero_parry`,
  `fight_state(npc|"hero")`, `npc_stat`, `npc_set_stat`, `npc_teleport`; Ereignisse `npc_hit(angreifer, ziel,
  schaden, kritisch)`, `npc_parried`, `npc_knocked_out`, `npc_killed`.

**Teil B – der Held (umgesetzt bis auf das Kamera-Kampfprofil):**
- **Tasten (K1, `runtime/CombatInput.hpp`):** mit gezogener Waffe wie Gothic 1 – Aktionstaste (Strg) gehalten und
  vor = Schlag (erneut im Kombofenster: der nächste), links/rechts = Seitenhieb, zurück = Parade, Sprung =
  Ausweichschritt; solange Strg gehalten ist, geht der Held nicht. Zweitbelegung Maus: links Schlag (mit
  links/rechts gehalten: Seitenhieb), rechts Parade (neue Aktion `parry`, `engine.toml`). Mit gezogener Waffe
  nimmt die Aktionstaste nichts auf und spricht niemanden an.
- **Ziel-Lock (K5):** Beim Ziehen nimmt der Held das nächste lebende NPC vor sich bis 8 m (das fokussierte zuerst),
  hält es bis 12 m und dreht sich zu ihm (6 rad/s); die Drehtasten gehen dann seitwärts. Bewusstlos, tot, zu weit
  oder Waffe weg: neues Ziel bzw. keins. `Engine::heroCombatTarget()`.
- Der Held steht beim Schlagen, Parieren, Taumeln und Liegen; der Ausweichschritt geht bis zum Clip rückwärts.

**Teil C – Folgen (umgesetzt):**
- **Plündern (K7):** Bewusstlose und Tote bleiben im Fokus (tiefer, wo sie liegen); die Aktionstaste öffnet statt
  Dialog oder Taschendiebstahl das Inventar mit ihren Sachen daneben (nur nehmen). `loot(npc, item, count?)` (bis 3 m),
  Ereignis `npc_looted(npc, item, count)`.
- **Einstellung (K7, Inhalt in `ai/perceptions.lua`):** Wen der Held niederschlägt, ist ihm dauerhaft eine Stufe
  schlechter gesinnt (freundlich → neutral → verärgert). Wer es sieht (20 m, sieht den Helden) und dem Opfer
  nahesteht – gleiche oder befreundete Gilde, Wachen –, wird verärgert; ein Totschlag macht Zeugen feindlich, sie
  rufen Hilfe. Ereignis `npc_witnessed(zeuge, opfer, einstellung)`.

**Teil D – Kampf-KI (umgesetzt, Inhalt `ai/combat.lua`, Werte `CombatAi`):**
- `fight(npc, ziel)` startet `zs_attack` gegen den Helden oder ein NPC; je Schleife (0,5 s) ein Schritt
  (`fight_step`): in Reichweite gehen (`npc_reach`, Tiere in ihrer Gangart), zum Ziel drehen (`npc_face`), schlagen
  (Kombos nach Talent, ein Viertel Seitenhiebe), parieren, wenn das Ziel schlägt (10/30/50 % je Talent).
- Höchstens zwei greifen dasselbe Ziel an, die übrigen warten in 3,5 m (`fight_attackers`). Liegende lässt er in Ruhe
  (K7, K8), über 30 m gibt er auf. Tiere fliehen unter 20 % Leben, Feiglinge (Bauern, Ausgestoßene bis Stufe 3) unter
  50 %.
- Wer getroffen wird, schlägt zurück (`npc_hit`). Wer angreifen würde (Waffe, Eindringling, feindlich), greift jetzt
  an statt nur zu drohen; Gerufene helfen gegen den Feind des Rufers. Tiere: `zs_mm_attack` und die Jagd
  (`zs_mm_hunt`) kämpfen mit demselben Schritt – ein Wolf reißt den Laufvogel.
- Gleiche Gilde trifft sich nicht (Rudel, Kameraden); der Held trifft jeden.
- Engine-Hilfen: `npc_distance(npc, anderer)`, `npc_face(npc, anderer)`, `npc_reach(npc)` (`hero` für den Helden).

**Teil E – Fernkampf (umgesetzt, `EngineRanged.cpp`; Entscheidungen R1–R4):**
- **Ziehen (R1):** eigene Taste `draw_ranged` („2“) für den ausgerüsteten Bogen bzw. die Armbrust; ohne
  Nahkampfwaffe nimmt auch die Leertaste den Bogen. Der Bogen sitzt an `socket_hand_l`, die Armbrust an
  `socket_hand_r`. Waffenmodus 3, `player_weapon()` = `"ranged"`.
- **Schießen (R2):** Strg + vor bzw. linke Maustaste; der Ziel-Lock reicht mit Bogen 30 m. Nachladen von selbst
  (Bogen 1,0 s, Armbrust 1,6 s), solange Munition da ist (`it_arrow` bzw. `it_bolt`, `data/combat.lua`); ohne:
  Hinweis „Keine Pfeile.“
- **Treffer (R4):** auf das fokussierte bzw. gesperrte Ziel trifft der Schuss mit der Chance des Talents (`bow`,
  `crossbow`: 30/60/90 %) – er fliegt dann auf dem flachen Bogen der Ballistik genau dorthin; ein Fehlschuss geht
  5° zur Seite. Ohne Ziel fliegt er frei entlang des Blicks (40 m/s, Schwerkraft).
- **Schaden (R3):** Stich der Waffe minus Schutz gegen Stich, mindestens 5, ohne Stärke und Krit. Fernkampf tötet
  (K7); den Helden wirft er nur nieder (K8). Der Pfeil steckt danach im Ziel (Inventar, plünderbar); verfehlte
  bleiben am Boden liegen und lassen sich aufheben.
- **Geschosse** fliegen im festen Schritt (Strecke gegen Welt und Körper), gezeichnet mit dem Modell der Munition,
  +Y entlang der Flugbahn (figurens Pfeil: Ursprung in der Schaftmitte, +Y zur Spitze). Ereignis `npc_shot`.
- **Lua:** `draw_ranged()`, `hero_shoot()`. Inhalt: `it_crossbow`, `it_arrow`, `it_bolt` (Modelle folgen von figuren).

**DoD-Szenario** (`tests/runtime/test_engine_m11_scenario.cpp`, Gegner-Platzhalter `npcs/camp/bandits.lua`): ein
Bot spielt den Helden (Waffe gezogen, schlägt, pariert ab und zu). Ein Wegelagerer fällt mit Talent 2 deutlich
schneller als mit Talent 0; ein Wolfsrudel zu dritt (zwei zugleich) wird besiegt; der starke Gegner (Rotbart) wirft
einen ungeübten Helden nieder und unterliegt einem geübten, gerüsteten. Werte dazu: Taumeln nur ab 15 % des Lebens
(`stagger_share`, starke Gegner schütteln leichte Treffer ab), Tiere springen beim Biss vor (`animal_reach` 1,3 m).
Ob es sich responsiv anfühlt, entscheidet der Projektinhaber beim Probespielen.

**Kamera-Kampfprofil (K5, umgesetzt):** Mit gezogener Waffe und einem Gegner im Ziel-Lock blendet die Kamera
(`[camera.combat]`, 0,4 s) auf 2,4 m Abstand und 1,45 m Blickpunkt; drinnen gilt der nähere der beiden Abstände.
`Engine::playerCombatBlend()`.

**Kampfclips (umgesetzt, figuren #217):** Der Menschen-Graph lädt `2h`, `bow`, `cbow` und `mag`; für jeden einmaligen
Clip gibt es einen Zustand gleichen Namens (`1h/t_attack_combo1` → `1h_t_attack_combo1`), aus dem es am Ende in die
Fortbewegung der gezogenen Waffe zurückgeht (Graph-Parameter `weapon`: 1 Einhand, 2 Fäuste, 3 Bogen, 4 Armbrust,
5 Zweihand, 6 Magie). Die Engine betritt den Zustand des Kampfzugs (`Fighter::clip`); seine Events `hit_start`,
`hit_end`, `combo_start`, `combo_end` treiben den Kämpfer, sein Ende beendet den Zug. Treffer, Bewusstlosigkeit,
Aufstehen und Tod spielen `none/t_hit_light`, `t_ko` → `s_ko`, `t_ko_getup`, `t_die_front`. Fehlt einer Figur ein
Clip, bleibt die Zeitleiste. Menschen halten nach einem Kampf 5 s die Kampfhaltung ihrer Waffe.

**Weiter:** Fernkampf für NPCs (Jäger); Waffen-Kapsel entlang der Animation; Bogen-Clips beim Schießen.

## Magie (M12)
Plan A–E freigegeben, Entscheidungen des Projektinhabers Z1–Z9 (2026-10-06, wie Gothic 1).

**Teil B – Zauber als Inhalt (umgesetzt, `gameplay/Magic.hpp`):**
- **`Spell`-Instanzen** (`game/scripts/magic/`): `name`, `circle` (1–6), `mana`, `kind` (`projectile`, `area`,
  `self`, `target`, `summon`, `transform`), `invest` (`{ stages, mana }`: Aufladestufen, Z5), `damage` (Art),
  `radius`, `heal`, `effect` (`sleep`, `fear`), `duration`, `summon` (Npc), `species`, `fx` (`cast`, `trail`,
  `impact`, `on_target`: Effekte aus `data/fx`), `on_cast`.
- **Runen und Spruchrollen** sind Items der Kategorien `rune` bzw. `scroll` mit `spell` (`items/magic.lua`); die
  sieben Rune-Plätze gibt es seit M8.
- **Wer wirken darf** (`castBlocked`): eine Rune braucht den Kreis des Zaubers (Talent `magic_circle`, Z2), eine
  Spruchrolle nicht (Z3); beide kosten das Mana des Zaubers plus die Aufladestufen. Mana erholt sich nicht von
  selbst (Z1: Tränke, Schlaf, Stufe). Lua `cast_check(item, stages?)` → nil oder der Grund.
- **Startsatz (Z9):** Feuerpfeil (Kreis 1, 10 Mana, Feuer 25), Heilung (1, 10, +50 LP), Schlaf (2, 15, 20 s),
  Wolfsgestalt (2, 20), Wolf rufen (3, 25, 60 s); Runen `it_rune_*`, Rollen `it_scroll_*`, `it_potion_mana_small`.
  Dazu (Entscheidung C) Schrecken (Spruchrolle `it_scroll_fear`, 10 Mana, 10 s Furcht).
  Kreise kosten beim Lehrer 10/15/20/25/30/35 LP (`data/magic.lua`, Talentname „Kreis der Magie“).

**Teil C1 – Wirken des Helden (umgesetzt, `EngineMagic.cpp`):**
- **Ziehen (Z4):**
  - „1“ (`draw_magic`) zieht die Rune bzw. Spruchrolle des zuletzt gewählten Runenplatzes, sonst des ersten belegten; Waffenmodus 4, Graph-Wert `weapon` 6.
  - Die Tasten 4–9 (`rune_1` … `rune_6`) wählen einen Runenplatz. Ist nichts gezogen, ziehen sie ihn; mit gezogener Magie wechselt die Rune sofort in der Hand. Ist eine Waffe gezogen, steckt die Taste sie zuerst weg.
  - Die Rune liegt in der rechten Hand. Ziel-Lock wie beim Bogen (30 m); auch ein verzauberter Schläfer bleibt Ziel.
- **Wirken (Z5):**
  - Strg + vor bzw. linke Maustaste gehalten: Ein Zauber mit `invest` lädt auf, je `Magic.charge_seconds` (1 s) eine Stufe, soweit das Mana reicht. Loslassen wirkt; einfache Zauber wirken sofort.
  - Mana und Rolle werden beim Loslassen abgezogen. Die letzte Rolle weg: Die Hände sind leer.
  - Fehlt Kreis oder Mana: `mag/t_cast_fail` und der Hinweis aus `castBlocked`, nichts wird verbraucht.
  - Der Wurf-Clip je Art (`mag/t_cast_projectile`, `_target`, `_self`, `_area`, `_summon`) lässt den Zauber bei seinem Event `cast` wirken; ohne Clip nach 0,4 s. Beim Aufladen läuft `t_invest` → `s_invest`. Der Held steht beim Aufladen und Wirken.
  - Jede Stufe wirkt noch einmal so stark (Schaden, Heilung, Dauer; Fläche: Radius +50 %).
- **Wirkungen:**
  - **Projektil:** Ein Geschoss aus M11 E, gerade ohne Schwerkraft mit `Magic.projectile_speed` (30 m/s), aufs Ziel bzw. entlang der Sicht. Mit dem Effekt `trail`, `impact` am Einschlag. Schaden − Schutz der Art, mindestens 5, kein Volltreffer. Magie tötet wie Fernkampf, die Getroffenen spielen `none/t_hit_magic`.
  - **Selbst:** Heilung bis zum Höchstwert.
  - **Fläche:** Schaden an allen im `radius`.
  - **Ziel, Schlaf (Z6):** reicht bis `Magic.target_range` (25 m) und wirkt nur bis zur Stufe des Zaubernden, sonst „Der Zauber zeigt keine Wirkung.“. Der Schläfer liegt wie bewusstlos (Menschen `none/t_ko`, Tiere ihr Schlaf-Clip) mit dem Effekt `on_target` über dem Kopf. Er wacht nach der Dauer auf oder sobald er Schaden nimmt; ein Schlag auf ihn zählt dann wie auf einen Stehenden.
- **Lua:** `draw_magic()`, `hero_rune(platz)`, `hero_cast(halten)`, `hero_casting()`; Ereignisse `npc_cast`, `npc_asleep`, `npc_woke`.
- Verwandlung folgt in C2; bis dahin gibt es einen Hinweis, nichts wird verbraucht.

**Teil C2 – Beschwörung (umgesetzt, Z8):**
- Ein `summon`-Zauber ruft das Npc des Zaubers (Wolf rufen: `mon_wolf`) 2 m vor den Helden, mit dem Effekt `summon`, für `duration` Sekunden (jede Aufladestufe noch einmal so lange).
- Es gibt nur eines: Ein neues lässt das alte verschwinden. Nach seiner Zeit, bzw. 2 s nach seinem Tod, verschwindet es im Effekt.
- Ein verschwundenes Wesen bleibt für Skripte als tot bestehen (Namen wie `mon_wolf#2` bleiben gültig). Es wird nicht mehr gezeichnet, nicht aktualisiert, hat keine Kollision und ist weder fokussierbar noch plünderbar.
- **Verhalten** (`ai/summons.lua`, Zustand `zs_summoned`):
  - Es folgt dem Helden auf 2,5 m.
  - Es kämpft gegen dessen Ziel (`hero_target()`) bzw. gegen jeden, der ihn im Umkreis von 15 m angreift.
  - Es reagiert nicht auf den Helden wie ein wildes Tier, und schlägt nicht zurück, wenn der Held es versehentlich trifft.
- Der Held visiert sein eigenes Wesen nicht an. Die Gilden-Regel (ein Rudel beißt sich nicht) gilt nicht zwischen gerufenen und wilden Tieren.
- **Lua:** `hero_summon()`, `hero_target()`; Ereignisse `npc_summoned(npc, caster)`, `npc_vanished(npc)`.

**Teil C2 – Verwandlung (umgesetzt, Z7):**
- Ein `transform`-Zauber macht den Held zum Tier seiner `species`; Wolfsgestalt: `wolf`.
- Die Werte kommen vom ersten Npc dieser Art (`mon_wolf`): Stärke, Schaden und ein eigenes Leben. Der Held bekommt dazu Figur und Graph der Art (`characters/monsters/<art>/…`, `data/anim/<art>.animgraph.toml`), die Kapsel aus `creatures.toml` und die Gangarten aus den Mischpunkten des Zustands `move` (Wolf: gehen 1,19, rennen 6,0 m/s).
- Der Wechsel geschieht zu Beginn des nächsten Spieler-Schritts, nie mitten in einer Animation oder einem Treffer; dabei erscheint der Effekt `summon`.
- In Tiergestalt:
  - Die Kampftasten beißen (Waffenmodus 5, `player_weapon()` = `"animal"`, `assess_fighter` mit `"animal"`).
  - Keine Waffen, keine Magie, kein Inventar; die Aktionstaste nimmt, benutzt und spricht nichts. Klettern geht nicht.
  - Treffer gehen aufs Leben des Tiers; das Leben des Menschen bleibt unberührt.
- **Zurück:** mit „1“ (`draw_magic`, `hero_transform_back()`), wenn das Leben des Tiers aufgebraucht ist (statt bewusstlos), im Wasser und beim Weltwechsel.
- **Lua:** `hero_shape()`; Ereignis `hero_transformed(species)` (leer: wieder Mensch).
- Die Kamera sinkt auf die Höhe des Tiers (Teil D); beim Wolf schaut sie auf 0,8 m (`creatures.toml` `camera_height`).
- **Übergang** (figuren #255):
  - Hin: Der Mensch spielt `none/t_transform_out`. Bei dessen Event `swap` tauscht die Engine Figur, Kapsel und Werte; der Wolf beginnt in `wolf/t_transform_in`.
  - Zurück mit „1“: `wolf/t_transform_out` → Tausch → `none/t_transform_in`.
  - Während des Übergangs steht der Held.
  - Fehlt ein Clip bzw. das Event, wird sofort bzw. nach 2 s getauscht. Im Wasser und ohne Leben des Tiers geht es sofort zurück.

**Teil D – KI und Reaktionen (umgesetzt, ohne Brennen):**
- **Kamera in Tiergestalt:** Blickhöhe mal Kapselhöhe / 1,8 m, Abstand mal mindestens 0,6. `creatures.toml` `[<art>] camera_height` kann die Blickhöhe festlegen.
- **Reaktion auf den verwandelten Helden** (`assess_fighter` mit `"animal"`, `observe_player`):
  - Menschen: Wachen, wer Nahkampf gelernt hat, und ab Stufe 5 greifen an; die anderen fliehen.
  - Tiere: die eigene Art lässt ihn in Ruhe; wer ihn als Räuber kennt (`predators`), flieht; wer ihn als Beute kennt (`prey`), greift an.
  - Niemand spricht ihn an.
- **Furcht (Z6):** Zielzauber mit `effect = "fear"`: Der Getroffene flieht `duration` (Vorgabe 10) Sekunden vor dem Zaubernden (Zustand `zs_fear`, Ereignis `npc_feared`). Einen Furcht-Spruch im Startsatz gibt es noch nicht (Frage an den Projektinhaber).
- **NPCs zaubern:**
  - `Npc`-Feld `spells` (Liste von Sprüchen) mit dem Mana und Kreis (Talent `magic_circle`) des NPC.
  - `npc_cast_spell(npc, spell, ziel?)` spielt den Wurf-Clip und lässt den Spruch bei `cast` wirken: Geschoss vom NPC aus, Heilung auf sich, Schlaf bzw. Furcht auf NPCs (nicht auf den Helden), Fläche.
  - Ein Treffer bricht den Spruch ab; das Mana ist dann weg.
  - Die Kampf-KI heilt unter 30 % Leben (`CombatAi.heal_below`) und wirkt Angriffssprüche, solange das Ziel weiter als 3 m (`cast_distance`) entfernt ist und das Mana reicht; danach Nahkampf.
  - Lua: `npc_can_cast`, `npc_casting`.
  - Wer im Spiel zaubert, entscheidet der Projektinhaber; bisher nur ein Test-NPC.
**Teil D2 – Entscheidungen A–C des Projektinhabers (umgesetzt):**
- **A – Zaubernde NPCs:**
  - Der Kräuterhexer `npc_camp_hexer` steht im Lager (`rtn_camp_hexer` am Westpunkt, in `camp_people()`). Im Kampf heilt er sich bzw. wirkt Feuerpfeile; die Figur ist vorerst die des alten Mannes.
  - Der Wegelagerer trägt eine Spruchrolle Feuerpfeil und liest sie auf Abstand. NPCs ohne den nötigen Kreis wirken einen Spruch aus einer mitgeführten Rolle, die dabei verbraucht wird (`npcScrollFor`).
- **B – Brennen wie Gothic:**
  - Nur Sprüche mit `burn = true` setzen in Brand; der Feuerpfeil nicht.
  - Der Getroffene brennt `Magic.burn_seconds` (3 s) mit dem Effekt `fire` und nimmt am Ende jeder Sekunde `Magic.burn_damage` (5) Feuerschaden, ohne zu taumeln.
  - Menschen bleiben stehen und spielen `none/s_burn`.
  - Wasser löscht (schwimmen bzw. Wasser über den Knien).
  - Ereignis `npc_burning(npc, caster)`; auch der Held kann brennen.
- **C – Furcht-Rolle im Startsatz:** `spl_fear` „Schrecken“ (Ziel, 10 Mana, 10 s Flucht), Spruchrolle `it_scroll_fear`.

**Teil E – Meilenstein-Szenario** (`tests/runtime/test_engine_m12_scenario.cpp`): Ein Magier-Held mit dem Startsatz kämpft
gegen den Kräuterhexer und einen Wegelagerer:
- Schlaf legt den Wegelagerer hin.
- Ein Feuerpfeil trifft den Hexer, der mit eigenen Sprüchen antwortet.
- Ein gerufener Wolf greift ein.
- Heilung heilt genau 50 abzüglich der Treffer währenddessen.
- In Wolfsgestalt greift die Torwache an, der alte Mann flieht; „1“ macht den Helden wieder zum Menschen.

**Nicht umgesetzt** (aus der Roadmap-Zeile): Telekinese und Shader-Effekte. Offen, ob sie zu M12 gehören.

## Wirtschaft
Handel: Händler-Inventar, Preisfaktor Verkauf (z. B. 0,5), Währung als Item (`ItMi_Ore`).
