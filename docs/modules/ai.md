# ai

**Zweck:** Das „Eigenleben“ der Welt – das Herz von Gothic. Mechanik in C++, Verhalten in Skripten.

## 1. Wegnetz & Navigation (M9 Teil A, umgesetzt) – `ai/Waynet.hpp`, `runtime/EngineNpcs.cpp`
- **Wegpunkte (WP)** und **Freepoints (FP)** aus dem `waynet`-Block der Welt (world.md „Wegnetz“, Vertrag mit welt),
  `ai::Waynet::build`. Namen ohne Rücksicht auf Groß- und Kleinschreibung (`find`, `findFreepoint`); Freepoint-Typ
  = zweites Namensglied (`FP_SIT_…` → `SIT`). Belegung der Freepoints kommt mit Teil B.
- **A\*** über die ungerichteten Kanten (`path`), Heuristik euklidisch.
- **Route** (`route(von, ziel, gehbar)`): geradeaus, wenn die Linie gehbar ist; sonst zum nächsten von der Position
  aus gehbaren WP, über das Netz zum WP, von dem aus das Ziel gehbar ist, dann zum Ziel. **Glättung:** Von jedem Punkt
  aus geht es zum weitesten Folgepunkt, der noch in gerader Linie gehbar ist.
- **Gehbar** (Engine, `walkableLine`): Kugeln mit dem Radius der NPC-Kapsel (0,3 m) in 0,5 / 1,0 / 1,5 m Höhe
  treffen auf der Linie nichts – Zäune mit Lücken zwischen den Latten zählen als Hindernis, niedrige Stufen nicht.
  Dazu Bodenproben alle 0,5 m: kein Schritt steiler als 35° (wie welts Wegnetzprüfung), kein fehlender Boden
  (Abgrund). Linien über 50 m gelten nicht als gehbar (Aufwand); sie gehen über das Wegnetz (welt #171).
- **NPCs** (eingefügte `Npc`-Instanzen) haben eine eigene Kapsel wie der Held (`physics::CharacterController`):
  Schwerkraft, Stufen, Kollision mit der Welt. `npc_goto(npc, ziel, rennen)` bzw. `Engine::npcGoTo`: drehen sich zum
  nächsten Routenpunkt (6 rad/s), gehen (Geschwindigkeit aus den Blendpunkten des Menschen-Graphen) oder rennen, ein
  Punkt gilt in 0,35 m als erreicht. **Blockiert**: Wird der Weg zum nächsten Routenpunkt 1,5 s lang nicht um 0,3 m
  kürzer (Hindernis, oder er rutscht am Hang ab – Bewegung allein zählt nicht), wird neu geplant, höchstens dreimal,
  dann `npc_blocked`. Ist er dabei schon näher als 1,2 m am Punkt (Kiste auf dem Ziel, Wegpunkt zu nah an einer
  Hausecke), gilt der Punkt als erreicht. Ankunft: Ereignis `npc_arrived(npc, ziel)`.
- **Debug** (F2): Kanten, Wegpunkte mit Namen (bis 40 m), Freepoints mit Blickrichtung, die Routen gehender NPCs.
- Testlager: Wegnetz mit 11 Punkten und 4 Freepoints (um das Südende des Zauns herum; das Tor ist zu).
- Später optional: Navmesh für freie Bewegung im Kampf (ADR, wenn nötig).

## 2. NPC-Zustandsautomat (M9 Teil B, umgesetzt) – `runtime/EngineAi.cpp`, Inhalt `game/scripts/ai/states.lua`
```lua
State "zs_sit_campfire" {
    begin  = function(npc, at) ... end,            -- füllt die Befehlsliste
    loop   = function(npc, seconds) ... end,       -- alle 0,5 s, solange die Liste leer ist; "done" beendet
    finish = function(npc) ... end,                -- räumt auf (optional)
}
```
- Ein NPC hat genau einen aktiven Zustand und eine **Befehlsliste**, die der Zustand füllt:
  `npc_goto` (Wegpunkt/Freepoint), `npc_goto_freepoint` (Typ, Umkreis), `npc_turn` (Richtung eines Punkts),
  `npc_play` (Tagesablauf-Animation, optional mit Gegenstand in der Hand), `npc_stop`, `npc_wait`, `npc_say`;
  `npc_clear` leert sie. Gehen oder eine andere Animation beendet eine laufende erst mit ihrem `_out`-Clip.
- **Animationen** (`npc_play(npc, "sit_ground")`): Zustände `amb_<x>_in` → `amb_<x>` → `amb_<x>_out` des
  Menschen-Graphen (figurens Set `amb`); ohne `_in`/`_out` direkt die Schleife; `idle_look`, `idle_scratch`,
  `react_warn` usw. heißen direkt so. Ein Gegenstand (`npc_play(npc, "sweep", "it_broom")`) erscheint beim
  Clip-Ereignis `item_to_hand` an `socket_hand_r` und verschwindet bei `item_from_hand` bzw. am Ende.
- Zustandswechsel: durch die Routine (Zeitfenster), Skript (`npc_start_state(npc, zustand, ort?)`), später die
  Wahrnehmung (Teil C). Eine Unterbrechung speichert den alten Zustand nicht – endet sie („done“), greift wieder
  die Routine (wie in Gothic).

## 3. Tagesabläufe (Routinen, M9 Teil B, umgesetzt)
```lua
Routine "rtn_farmer_woman" {
    { from = "06:00", to = "12:00", state = "zs_sweep", at = "wp_camp_center" },
    { from = "23:00", to = "06:00", state = "zs_sleep", at = "wp_camp_west" },   -- über Mitternacht
}
```
- Zeitfenster [from, to); einmal je Spielminute prüft die Engine den Eintrag, bei Wechsel endet der alte Zustand
  (finish) und der neue beginnt mit `at`.
- `routine` am `Npc`; `set_routine(npc, rtn)` wechselt (Kapitel), `""` schaltet ab. `insert_npc(npc, ort?)` setzt
  ein Npc ohne Ort an den Ort des aktuellen Eintrags.
- **Freepoints** werden belegt: `npc_goto_freepoint(npc, "SIT", 12)` nimmt den nächsten freien dieses Typs im
  Umkreis, reserviert ihn bis zum Ende des Zustands und dreht den NPC in seine Richtung; ist keiner frei, bleibt
  er stehen.
- **KI-LOD**: NPCs weiter als `[ai] simulation_distance` (80 m) vom Spieler (zurück 5 m näher) werden weder bewegt noch animiert und
  beginnen keinen Zustand; wechselt ihr Zeitfenster, stehen sie sofort am neuen Ort. Kommt der Spieler näher,
  beginnt der aktuelle Zustand.
- Testlager: `camp_people()` (Konsole oder `--exec=camp_people()`) setzt Torwache, Bäuerin, Holzfäller und den
  alten Mann an die Orte ihres Tagesablaufs (`routines/camp.lua`).

## 4. Wahrnehmung (M9 Teil C1, umgesetzt) – `runtime/EnginePerception.cpp`, Inhalt `ai/perceptions.lua`
| Sinn | Umsetzung |
|---|---|
| Sehen | Kegel (Vorgabe 100°) und Reichweite (25 m) je NPC, Strahl von Augenhöhe (1,6 m) zur Brust des Spielers gegen die Welt; schleicht er, halbe Reichweite, nachts (21–6 Uhr) 0,6 |
| Hören | Geräusche mit Ort und Radius (mal Gehör des NPCs), 2 s hörbar: die Engine meldet Rennen (`run`) und jeden Dietrich-Dreh (`lockpick`), Skripte rufen `noise(x, y, z, radius, art)` |
| Nähe | Besitzer im privaten Bereich: in `room_distance` (8 m) auch ohne Sicht |

- Werte in `data/perception.lua` (`Perception`: sight, angle, sneak_factor, night_factor, near_distance,
  forget_seconds, room_distance, noise.<art>); je Npc `senses = { sight, angle, hearing }`.
- **Takt:** nur simulierte NPCs (< 80 m, KI-LOD); unter 20 m fünfmal je Sekunde, sonst einmal, gestaffelt.
- **Ereignisse** an Lua, der NPC zuerst (Gothics B_Assess…):
  `assess_player(npc, abstand)` (neu gesehen bzw. nach 10 s wieder), `assess_fighter(npc, abstand, "weapon"|"fists")`
  (einmal je Ziehen), `assess_noise(npc, art, x, y, z)`, `assess_theft(npc, besitzer, item, anzahl)` (wer zusieht;
  beim misslungenen Taschendiebstahl immer das Opfer), `assess_use_mob(npc, besitzer, mob)` (fremder Mob benutzt
  bzw. Schloss geknackt), `assess_enter_room(npc, besitzer, bereich)` (Trigger mit `owner`, world.md).
- **Abfragen:** `npc_sees_player`, `npc_distance_to_player`, `player_weapon`, `player_inside(bereich)`.
- **Waffe ziehen** (Taste `draw_weapon`, Konsole `draw_weapon()`): die ausgerüstete Nahkampfwaffe in
  `socket_hand_r`, Bewegung im 1h-Set; ohne Waffe Fäuste. Schwimmen und Klettern stecken sie weg. Figurens
  `1h|fist/t_draw` und `t_sheath` spielen dabei; das Schwert erscheint bzw. verschwindet bei ihren Events `draw`
  und `sheath` (`Engine::weaponEvent`).
- **Reaktionen** (Inhalt, `ai/perceptions.lua`, Texte zentral in `data/shouts.lua`, eigene Formulierungen):
  Wachen warnen bei gezogener Waffe auf Sicht, andere ab 5 m; zweimal gewarnt, dann `npc_would_attack(npc, grund)`
  und Drohen (`zs_threaten`: dem Spieler 15 s folgen) bis zum Kampf in M11. Privater Bereich: hinauswerfen, nach 6 s
  drohen. Diebstahl bzw. fremder Mob gesehen: schimpfen und drohen. Geräusch: kurz umsehen. Schlafende nehmen nichts
  wahr.
- **Befehle dazu:** `npc_turn_to_player`, `npc_goto_player(npc, abstand, rennen)`, `npc_follow_player(npc, sekunden,
  abstand)`.
- Testlager: `TRG_PRIVAT_WACHE` (Schlafplatz der Wache mit Truhen hinter dem Tor) gehört `npc_gate_guard`.

## 5. Einstellungen & Gruppen (M9 Teil C2, umgesetzt) – Inhalt `ai/attitudes.lua`, `ai/perceptions.lua`
- **Einstellung NPC → Spieler** (`npc_attitude(npc)`: `friendly`, `neutral`, `angry`, `hostile`): vorübergehend
  (`set_temp_attitude(npc, a, sekunden)`, vergessen nach `AttitudeSettings.forget_seconds`, 300 s) vor dauerhaft
  (`set_attitude(npc, a)`, in `Story.attitudes`, wird gespeichert) vor der Gilden-Tabelle `Attitudes`
  (`data/guilds.lua`) gegen die Gilde des Helden. Ganz in Lua.
- **Verärgert** wird ein NPC, der angreifen würde, der den Spieler in seinem Bereich erwischt, und wer zu Hilfe kommt.
  **Feindlich** Gesinnte greifen auf Sicht in 10 m an (bis M11: drohen).
- **Kameraden:** Würde ein NPC angreifen, ruft er: NPCs in 15 m, deren Gilde der seinen freundlich gesinnt ist,
  bekommen `assess_call(helfer, rufer)` und drohen mit. Engine-Abfrage dazu `npcs_near(npc, radius)` (Liste
  `{npc, guild, distance}`, nach Abstand).
- **Fliehen:** Bauern und Ausgestoßene bis Stufe 3 laufen weg, wenn in 10 m jemand angreifen würde oder der Spieler
  ihnen mit gezogener Waffe näher als 4 m kommt (`zs_flee`: `npc_flee(npc, 8)` – zum Wegpunkt im Umkreis von 30 m, der
  am weitesten vom Spieler weg ist, alle 2 s neu gewählt). Fliehen bei wenig Leben kommt mit dem Kampf (M11).
- **Warnungen** vor dem Angriff: Abschnitt 4 (zweimal, dann `npc_would_attack`).
- `npc_shout(npc, text)` ruft sofort, ohne auf die Befehlsliste zu warten (Weglaufen).

## 6. Monster (M9 Teil D, umgesetzt) – Inhalt `ai/monsters.lua`, Werte `data/creatures.lua`
- **Tiere sind Npcs** (wie Gothics Monster): `species = "wolf" | "keiler" | "laufvogel"` wählt Figur und Graph
  (`characters/monsters/<art>`), die Kapsel steht in `assets/source/data/creatures.toml` (Wolf 0,35 / 0,9 m, Keiler
  0,45 / 1,0 m, Laufvogel 0,4 / 1,8 m). Tagesablauf, Zustände, Wahrnehmung (`senses.angle = 220`) wie bei Menschen.
- **Namen:** Mehrere NPCs derselben Instanz heißen `mon_wolf`, `mon_wolf#2` …; `insert_npc` gibt den Namen zurück,
  Instanz-Abfragen ignorieren das `#n`.
- **Revier:** Mitte = Wegpunkt des Tagesablaufs, Radius `territory`; `npc_roam(npc, mitte, radius)` geht zu einem
  zufälligen, von der Mitte gerade erreichbaren Punkt. Zwischendurch fressen (`eat_chance`).
- **Tagesablauf** (Entscheidung Projektinhaber): Wölfe streifen nachts und schlafen tags, Keiler und Laufvogel
  umgekehrt (`routines/animals.lua`).
- **Rudel:** `insert_pack(instanz, anzahl, ort)` – das erste Tier führt, die anderen folgen ihm
  (`npc_follow_npc`); droht eines, droht das ganze Rudel.
- **Spieler** (Entscheidung Projektinhaber, wie Gothic): Wolf und Keiler drohen ab 12 m (`threaten_distance`) 3 s lang
  (Knurren bzw. Scharren, Clip `threaten`), dann `npc_would_attack(npc, "animal")` und Angriff – bis M11 verfolgen
  sie den Spieler 20 s. Der Laufvogel ist neutral und greift erst unter 4 m an (`attack_distance`). Grundlage ist das
  Ereignis `observe_player(npc, abstand)` bei jedem Blick, solange der NPC den Spieler sieht. Schlafende nehmen nichts
  wahr.
- **Beute und Räuber:** einmal je Sekunde: ein Wolf jagt einen Laufvogel in 15 m (`npc_follow_npc`, rennend), ein
  Laufvogel flieht vor einem Wolf in 12 m (`npc_flee(npc, 8, wolf)`: 10 m gerade weg, sonst 45° seitlich, sonst zum
  fernsten Wegpunkt). Der Laufvogel (6,5 m/s) ist schneller als der Wolf (6,0 m/s) und entkommt im Freien.
- **Gangarten** (Projektinhaber: Tiere schneller als der Held mit 4 m/s; Clips figuren #200): Wolf gehen 1,19 / Trab
  2,99 / rennen 6,0 m/s, Keiler 0,99 / 4,98, Laufvogel 1,30 / 6,5. Die Engine liest sie aus den Blend-Punkten von
  `move` (erster Punkt > 0 gehen, letzter rennen, ein mittlerer Trab). Das `run`-Argument der NPC-Befehle nimmt
  `true` (rennen) oder `"trot"` (Trab; ohne Trab-Clip gehen). In `data/creatures.lua`: `pack_gait` (Rudel folgt
  dem Anführer) und `chase_gait` (Verfolgen; über 8 m rennt jedes Tier), beim Wolf `"trot"`.
- Fliehen bei wenig Leben kommt mit dem Kampf (M11).
- **Testlager:** Wegpunkte `WP_WOLF_DEN` (Wolfsbau im Westen), `WP_MEADOW_NORTH` (Keiler), `WP_MEADOW_SOUTH`
  (Laufvogel), mit Wegen zum Lager. Einsetzen z. B. `insert_pack('mon_wolf', 3, 'wp_wolf_den')`,
  `insert_animal('mon_laufvogel')`.
- **Einsetzen auf dem Boden:** `insert_npc` stellt auf den Boden unter bzw. über dem Wegpunkt, `insert` vor dem
  Spieler sucht den Boden notfalls von oben (Hang; welt).

## Geplante API
```cpp
namespace g7::ai {
class Waynet { public: std::optional<Path> findPath(Vec3 from, StringId toWp) const; ... };
struct Brain { StateRef current; RoutineRef routine; AiQueue queue; PerceptionSet active; ... }; // component
class AiSystem { public: void fixedUpdate(world::World&, script::ScriptVm&, f64 dt);
                 void emitNoise(Vec3 pos, f32 radius, NoiseType, entt::entity source); };
}
```

## Leonberg lebt (Inhalt nach M10)
- Acht Bewohner mit Tagesabläufen auf welts Routinen-Orten (`docs/design/leonberg-routinen-orte.md`):
  Schmied, Wirt, Bäckerin und Marktfrau (handeln), zwei Torwachen (Tag- bzw. Nachtschicht), Bauer, Ratsdiener;
  `npcs/leonberg/residents.lua`, `routines/leonberg.lua`, `dialogs/leonberg.lua` (eigene Namen und Plauder-Dialoge,
  Entscheidungen L1–L3). Beim Laden von Leonberg setzt `leonberg_people()` sie ein (`startup.lua`, L2).
- Tätigkeiten an Freepoints: `zs_repair`, `zs_harvest`, `zs_smalltalk`, `zs_drink` (mit Krug), `zs_lean`, `zs_sit`,
  `zs_stand_shop`; nachts lehnen sie an ihrem Haus, bis NPCs mit M11 Betten benutzen.
- **Stimme:** `gender` (`"m"`/`"f"`, Vorgabe `"m"`) und `voice` (Stimmgruppe, Vorgabe die Gilde) wählen die Stimme der Zurufe (`data/voices.lua`, audio.md „Sprache“).
- **Figuren:** `figure` (eigenes Manifest) oder `figure_set` für Namenlose: eine Figur aus
  `data/figure_sets.toml` (`[sets] citizen = [...]`, figuren), stabil nach dem Namen gewählt. Fehlt die Figur, steht
  die Standardfigur ein (Warnung).
- `route_length(von, nach)`: Weglänge zwischen zwei Punkten bzw. `nil` (Prüfung der Routinen-Orte).
- Tests: Die Orte aller `rtn_leo_*` stehen in Leonbergs Wegnetz (auch in der CI); mit den erzeugten Daten (lokal)
  ein Spieltag in Leonberg, jeder zur rechten Zeit am rechten Ort.

## Türen (für welts begehbare Häuser)
- Unverschlossene Tür-Mobs sind für die Wegeplanung kein Hindernis (`walkableLine` geht am Türblatt vorbei weiter,
  höchstens zwei Türen je Linie; ein Türblatt ist kein Boden).
- Ein gehender NPC öffnet eine geschlossene Tür, deren Blatt näher als 1,6 m vor ihm ist (ohne Animation, bis NPCs
  mit M11 Mobs benutzen), wartet, während sie aufschwingt, und schließt sie wieder, sobald er 1,8 m von der Angel
  weg ist. Verschlossene Türen öffnet nur der Besitzer (`owner` des Mobs: NPC oder Gilde); für die Planung sind sie
  Wände.
- Türen, die laut Weltdatei offen stehen (`components.mob.open`, world.md), stehen beim Laden offen.

## Debug (M9 Teil E, umgesetzt) – `runtime/EngineAiDebug.cpp`
- **Fenster „AI“** (Debug-UI, F1): je NPC Name, Zustand, Abstand zum Spieler, „sees you“, „(far)“ außerhalb der
  KI-LOD; aufgeklappt Tagesablauf und Ort, Animation (Graph-Zustand und Tagesablauf-Animation), Einstellung zum
  Spieler, die Befehlsliste (die erste läuft). Filter nach Namen; oben die Zahl der Skriptfehler seit dem Start
  (`ScriptVm::callErrors`).
- **Overlay** (F2): Ein Klick auf einen Namen zeigt dessen Sichtkegel (gelb, grün mit Linie zum Spieler, solange
  er ihn sieht) und die Hörweite fürs Rennen (blau); dazu Wegnetz und Routen (Abschnitt 1).
- Zustände, die ihre Tätigkeit nicht erreichen (Weg versperrt), beginnen nach 5 s neu (`retry` in
  `ai/states.lua`).

## Tests
- Je Teil: `tests/ai` (Wegnetz), `tests/runtime` „Engine NPCs“, „NPC routines“, „perception“, „attitudes“,
  „animals“, „weapon“.
- **M9-Szenario** (`test_engine_m9_scenario.cpp`, die DoD): vier Leute und sechs Tiere im Testlager einen Spieltag
  lang (Spielminute = 0,25 s) – der Spieler fern, sie springen an die Orte ihres Tagesablaufs: jede Stunde jeder im
  Zustand seines Eintrags am richtigen Wegpunkt; zwei Stunden mitten unter ihnen; keine Skriptfehler; die Wache
  warnt binnen 30 Ticks vor gezogener Waffe und bemerkt den Spieler in ihrem Bereich binnen 30 Ticks.
