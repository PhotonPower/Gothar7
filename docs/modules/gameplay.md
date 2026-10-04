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
  - Modi (Kampf, Dialog, Schwimmen) folgen mit ihren Phasen als weitere Datensätze.
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
  `ring`, `amulet`, `belt`, `rune`, `scroll`, `potion`, `food`, `document`, `key`, `misc`.
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
- Waffenmodus-Wechsel (Ziehen/Wegstecken als Animation mit Event `item_to_hand`).
- Nahkampf: Angriff startet Clip; zwischen `hit_start`/`hit_end` Shapecast der Waffe; Kombo-Eingabe
  nur im `combo_window`; Anzahl Kombo-Schläge und Tempo abhängig von Talentstufe.
- Parade/Block, Seitwärtsschritt, Rückwärtsschritt.
- Schaden = max(Waffenschaden + Attributbonus − Schutz, Minimum); kritisch nach Talent; Faustkampf/menschliche Gegner → bewusstlos.
- Fernkampf: Zielmodus, Projektil (Ballistik), Treffer-Chance/-Streuung nach Talent.

## Magie (M12)
Rune (unendlich) vs. Spruchrolle (verbraucht), Mana-Kosten, Kreise; Zauber als Skript + Effekt-Daten
(`invest` zum Aufladen, `cast`, Projektil/Fläche/Verwandlung/Kontrolle/Beschwörung).

## Wirtschaft
Handel: Händler-Inventar, Preisfaktor Verkauf (z. B. 0,5), Währung als Item (`ItMi_Ore`).
