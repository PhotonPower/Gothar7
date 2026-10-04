# Animationsliste (Menschen und Monster)

Quelle der Wahrheit für **welche** Animationen gebraucht werden. Konvention siehe
`characters-pipeline.md` §3. Gepflegt von der Figuren-Spur (F2–F4); der Abgleich-Tool-Bericht
(F2) markiert Fehlendes.

**Legende** – Typ: `s` Schleife, `t` Übergang/Einmal, `a` additiv · RM: Root Motion ·
Quelle: Q = Quaternius CC0 (UAL1/UAL2, nur Knochen umbenennen), Q→ = aus einem Q-Clip abgeleitet
(rückwärts, Überblendung, Verkettung), MC = eigenes Mocap, K = Keyframe · **kein Mixamo** (öffentliches
Repo, Adobe-Bedingungen verbieten die Weitergabe; Entscheidung 2026-10-03) ·
Prio: **A** = Meilenstein A (M6), **B** = Vertical Slice (M10), **C** = später ·
Status: `offen` / `platzhalter` (aus einer Bibliothek, nachbearbeitet bzw. ersetzt in F4) /
`platzhalter-K` (**Keyframe-Platzhalter**, grob – in F4 durch Mocap ersetzen) / `fertig`.
Clips liegen in `assets/source/characters/anims/human/<modus>.glb`; Herkunft je Clip:
`tools/chargen/src/gothar_chargen/data/clips/<modus>.toml`. Fortschritt: `gothar-chargen report`.

## Prio A – Meilenstein A (vollständig ausgeschrieben)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/s_idle` | Stehen, entspannt | – | – | Q | platzhalter (UAL1 `Idle_Loop`) |
| `none/s_walk` | Gehen vorwärts | – | footstep_l/r | Q | platzhalter (UAL1 `Walk_Loop`) |
| `none/s_walk_back` | Rückwärts gehen | – | footstep_l/r | Q→ | platzhalter (`s_walk` rückwärts) |
| `none/s_strafe_l` / `s_strafe_r` | Seitwärts | – | footstep_l/r | K, später MC | platzhalter-K (Gehen mit seitlich gedrehter Hüfte) |
| `none/s_run` | Rennen | – | footstep_l/r | Q | platzhalter (UAL1 `Jog_Fwd_Loop`) |
| `none/s_sneak` | Schleichen | – | footstep_l/r (leise) | Q | platzhalter (UAL1 `Crouch_Fwd_Loop`) |
| `none/t_turn_l` / `t_turn_r` | Auf der Stelle drehen | – | footstep | K, später MC | platzhalter-K (Idle + gedämpfte Schritte + Körperdrehung) |
| `none/t_walk_2_run`, `t_run_2_walk`, `t_run_stop` | Übergänge | – | – | Q→/K | platzhalter (Überblendung 10/10/12 Frames) |
| `none/t_jump_start`, `s_jump_air`, `t_jump_land` | Sprung | – | land | Q | platzhalter (UAL1 `Jump_Start`/`_Loop`/`_Land`) |
| `none/t_jump_run` | Sprung aus dem Lauf | – | land | Q→ | platzhalter (`Jump_Loop` 0–20 + `Jump_Land`) |
| `none/s_fall`, `t_fall_land_hard` | Fallen, harte Landung | – | land | Q | platzhalter (UAL2 `NinjaJump_Idle_Loop`/`_Land`) |
| `none/t_climb_low` | Kante bis 1,0 m hochsteigen (Root steigt 1,0 m) | ✓ | – | Q, später MC/K | platzhalter (UAL2 `ClimbUp_1m_RM`, 1 m) |
| `none/t_climb_mid` | Kante bis 1,6 m hochziehen (Root steigt 1,6 m; engine skaliert herunter) | ✓ | – | K, später MC | platzhalter-K (`t_climb_low` gestreckt: 1,6 m, ×1,4 Zeit) |
| `none/t_climb_high` | Kante bis 2,2 m hochziehen (Root steigt 2,2 m; engine skaliert herunter) | ✓ | – | K, später MC | platzhalter-K (`t_climb_low` gestreckt: 2,2 m, ×1,75 Zeit) |
| `none/t_ladder_on`, `s_ladder_up`, `s_ladder_down`, `t_ladder_off` | Leiter | ✓ | – | K, später MC | platzhalter-K (Wechselposen, 0,6 m je Zyklus) |
| `swim/s_idle`, `swim/s_forward`, `swim/s_back` | Schwimmen | – | splash | Q, Q→ | platzhalter (UAL1 `Swim_Idle_Loop`, `Swim_Fwd_Loop`, rückwärts) |
| `swim/t_turn_l/r` | Schwimmend drehen | – | – | K, später MC | platzhalter-K (`swim/s_idle` + Körperdrehung) |
| `dive/s_idle`, `dive/s_forward`, `swim/t_2_dive`, `dive/t_2_swim` | Tauchen | – | – | K, später MC | platzhalter-K (Schwimmen geneigt/verlangsamt, Überblendungen) |
| `none/t_slide`, `s_slide` | Hang hinabrutschen (stehend, Gothic-typisch) | – | – | K | platzhalter-K (Balance-Pose mit Schwanken; Übergang aus `s_run`) |

Kletterhöhen passend zu den engine-Kantenklassen (Stand 2026-10-03: niedrig ≤ 1,0 m, mittel ≤ 1,6 m, hoch ≤ 2,2 m;
Spielgefühl-Werte aus engines `movement.toml`, Entscheidung beim Projektinhaber). Ändern sich die Klassen, werden
`t_climb_mid/high` nachgezogen (`data/clips/none.toml`).

## Prio B – Vertical Slice (Muster, wird in F2/F4 ausgeschrieben)

| Bereich | Umfang (Richtwert) | Muster / Beispiele | Quelle |
|---|---|---|---|
| Fortbewegung je Waffenmodus (`fist`, `1h`, `2h`, `bow`, `cbow`, `mag`) | ~60 | wie Prio A: s_idle, s_walk, s_run, s_walk_back, s_strafe_l/r, t_turn_l/r | Q/K |
| Waffe ziehen/wegstecken | ~12 | `1h/t_draw`, `1h/t_sheathe`, `2h/t_draw`, `bow/t_draw` … | MC/K |
| Nahkampf 1h/2h je Talent t0–t2 | ~60 | `1h/t_attack_combo1..4_t2`, `t_attack_l/r`, `t_parry`, `t_dodge_back`, `t_hit_front`, `t_stumble` | MC/K |
| Faustkampf | ~10 | `fist/t_attack_combo1..2`, `fist/t_parry` | Q/MC |
| Fernkampf | ~12 | `bow/s_aim`, `bow/t_shoot`, `bow/t_reload`, `cbow/…` | MC/K |
| Treffer, Tod, Bewusstlos | ~15 | `none/t_hit_light`, `t_die_front/back`, `t_ko`, `s_ko`, `t_ko_getup` | Q/MC |
| Mob-Interaktionen (je Mob: hin / Schleife / weg) | ~60 | `mob/chest/t_open`, `mob/anvil/s_work`, `mob/bed/t_lie_down`, `mob/grindstone/s_work`, `mob/cauldron/s_stir`, `mob/spit/s_turn`, `mob/bench/s_sit`, `mob/lever/t_pull`, `mob/door/t_open`, `mob/ore/s_hack` | MC/K |
| Item-Benutzung | ~15 | `none/t_eat`, `t_drink`, `t_read_scroll`, `t_pickup_ground`, `t_pickup_high`, `t_torch_light` | Q/MC |
| Lockpicking, Taschendiebstahl | ~4 | `mob/chest/s_picklock`, `none/t_pickpocket` | K |
| Ambient / Routinen | ~30 | `amb/s_guard_arms_crossed`, `amb/s_sit_ground`, `amb/s_sleep_ground`, `amb/s_sweep`, `amb/s_campfire_warm`, `amb/s_drink_mug`, `amb/s_train_sword`, `amb/s_lean_wall` | MC |
| Dialog-Gesten (additiv Oberkörper) | ~20 | `dlg/a_talk_neutral1..4`, `dlg/a_gesture_shrug`, `a_point`, `a_dismiss`, `a_threaten`, `a_greet` | MC |
| Magie | ~12 | `mag/t_invest`, `mag/s_invest_loop`, `mag/t_cast_projectile`, `t_cast_area`, `t_cast_self` | MC/K |

### Prio B – Item-Benutzung und Mobs für M8 (ausgeschrieben, Vertrag „Mobs“ §3.1)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/t_pickup_ground` | Gegenstand vom Boden aufheben | – | pickup | Q | platzhalter (UAL2 `Farm_Harvest`) |
| `none/t_pickup_high` | Gegenstand von Tisch/Regal nehmen | – | pickup | Q | platzhalter (UAL1 `PickUp_Table`) |
| `none/t_eat`, `t_drink` | Essen, Trinken | – | use, item_from_hand | K | platzhalter-K (rechte Hand zum Mund, Unterarm gedreht: Daumen oben, Bissen bzw. Flaschenhals am Mund – mit den F6-Gegenständen geprüft; Trinken mit gehobenem Kopf) |
| `none/t_read_scroll` | Schriftrolle lesen | – | use | K | platzhalter-K (beide Hände vor der Brust, Kopf gesenkt) |
| `none/t_pickpocket` | Taschendiebstahl | – | – | Q | platzhalter (UAL1 `Interact`) |
| `mob/chest/t_open`, `s_open`, `t_close` | Truhe öffnen, hineinsehen, schließen | – | open, close | Q | platzhalter (UAL2 `Chest_Open`; `s_open` hält die Pose mit offenem Deckel, `t_close` rückwärts) |
| `mob/chest/s_picklock` | Schloss knacken | – | (picklock_l/r) | Q | platzhalter (UAL1 `Fixing_Kneeling`) |
| `mob/anvil/t_start`, `s_work`, `t_stop` | Am Amboss schmieden | – | hit_anvil, sound:anvil_hit | Q | platzhalter (UAL2 `TreeChopping_Loop`, Überblendung 12 Frames) |
| `mob/bed/t_lie_down`, `s_lie`, `t_stand_up` | Ins Bett legen, liegen, aufstehen | ja (nur t_) | lie, stand | Q→ | platzhalter (UAL2 `LayToIdle` + Root Motion aufs Bett; `t_lie_down` rückwärts, `s_lie` hält das Liegen; liegt entlang der Längsseite, Kopfende −X des Betts, Drehung im Becken) |
| `mob/door/t_open` | Tür öffnen/schließen | – | open | Q | platzhalter (UAL1 `Interact`) |

### Prio B – Fortbewegung je Waffenmodus (ausgeschrieben, F2)

Je Modus `s_idle` = Haltung; die übrige Fortbewegung ist geschichtet: Beine, Becken und Wirbelsäule aus
den `none`-Clips, Arme und Kopf aus der Haltung (`layer` in `data/clips/<modus>.toml`). Events: footstep_l/r.

| Name | Haltung | Quelle | Status |
|---|---|---|---|
| `fist/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | Deckung (UAL1 `Punch_Enter`, Endpose) | Q→ | platzhalter |
| `1h/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | UAL1 `Sword_Idle` | Q→ | platzhalter |
| `2h/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | Keyframe-Pose: Hände zusammen an der rechten Hüfte | K | platzhalter-K |
| `bow/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | Keyframe-Pose: Bogen tief in der linken Hand | K | platzhalter-K |
| `cbow/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | UAL1 `Pistol_Idle_Loop` (beide Hände vorn) | Q→ | platzhalter |
| `mag/s_idle`, `s_walk`, `s_run`, `s_walk_back`, `s_strafe_l/r`, `t_turn_l/r` | UAL1 `Spell_Simple_Idle_Loop` | Q→ | platzhalter |

### Prio C – Routinen und Reaktionen für M9 (ausgeschrieben, Liste von engine)

Zustände mit Ein- und Ausstieg heißen `t_<x>_in` / `s_<x>` / `t_<x>_out`; die Zuordnung Freepoint → Clips steht
bei engine in Lua. Ursprung = Fußpunkt, Blick +Z, auf der Stelle; Gegenstände in der Hand erscheinen mit
`item_to_hand` und verschwinden mit `item_from_hand`. Neues Set `amb` (`anims/human/amb.glb`).

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `amb/t_sit_ground_in`, `s_sit_ground`, `t_sit_ground_out` | Am Boden sitzen (FP_SIT, auch am Lagerfeuer) | – | – | K | platzhalter-K (Knie angezogen, Arme um die Knie; sitzt, wo es stand) |
| `mob/bench/t_sit`, `s_sit`, `t_stand`, `s_sit_talk` | Bank: hinsetzen, sitzen, aufstehen, im Sitzen reden | – | – | Q | platzhalter (UAL1 `Sitting_Enter`, `Sitting_Idle_Loop`, `Sitting_Exit`, `Sitting_Talking_Loop`; Becken 0,33 m hinter den Füßen, Slot in `mobs.toml`) |
| `amb/t_guard_in`, `s_guard`, `t_guard_out` | Wache stehen, Arme verschränkt | – | – | Q | platzhalter (UAL2 `Idle_FoldArms_Loop`, Überblendung 15 Frames) |
| `amb/t_lean_wall_in`, `s_lean_wall`, `t_lean_wall_out` | Mit dem Rücken an der Wand (Wand hinter dem Fußpunkt) | – | – | K | platzhalter-K (auf `s_guard`, Becken 8 cm zurück, ein Bein angewinkelt) |
| `amb/s_talk_a`, `s_talk_b` | Reden, zwei Varianten | – | – | Q | platzhalter (UAL1 `Idle_Talking_Loop`; b mit anderer Arm- und Kopfhaltung) |
| `amb/s_listen` | Zuhören, nicken | – | – | K | platzhalter-K (auf `s_guard`) |
| `amb/t_sleep_ground_in`, `s_sleep_ground`, `t_sleep_ground_out` | Am Boden schlafen | – | lie, stand | Q→ | platzhalter (UAL2 `LayToIdle`, liegt, wo es stand) |
| `amb/s_campfire_warm` | Am Feuer hocken, Hände wärmen | – | – | Q→ | platzhalter (UAL1 `Crouch_Idle_Loop`, Hände nach vorn) |
| `amb/t_sweep_in`, `s_sweep`, `t_sweep_out` | Fegen mit dem Besen (`it_broom`) | – | item_to_hand, item_from_hand | K | platzhalter-K (Besen schräg nach unten, Oberkörper pendelt) |
| `amb/t_drink_mug_in`, `s_drink_mug`, `t_drink_mug_out` | Aus dem Krug trinken (`it_mug`) | – | item_to_hand, item_from_hand | K | platzhalter-K (Haltung wie `none/t_drink`) |
| `amb/s_train_sword` | Schwerttraining (Waffe in der Hand) | – | – | Q→ | platzhalter (UAL2 `Sword_Regular_Combo`, auf der Stelle, Schleife geschlossen) |
| `amb/s_chop_wood` | Holz hacken (FP_CHOP) | – | hit_wood, sound:wood_chop | Q | platzhalter (UAL2 `TreeChopping_Loop`) |
| `amb/s_harvest`, `s_water`, `s_repair_kneel` | Ernten, gießen, kniend reparieren (FP_HARVEST, FP_WATER, FP_REPAIR) | – | – | Q→ | platzhalter (UAL2 `Farm_Harvest`, `Farm_Watering`, UAL1 `Fixing_Kneeling`; auf der Stelle, Schleife geschlossen) |
| `none/s_idle_look`, `none/t_idle_scratch` | Stand-Varianten: sich umsehen, am Kopf kratzen | – | – | K | platzhalter-K |
| `none/t_warn`, `t_point`, `t_surprised`, `t_search` | Reaktionen: warnen (erhobene Faust), zeigen, erschrecken, suchen | – | – | K | platzhalter-K |

## Monster (F5, Vertrag `characters-pipeline.md` §7)

Mindestumfang je Art. Clips liegen in `assets/source/characters/monsters/<art>/anims/<art>.glb`, Herkunft je
Clip: `data/clips/<art>.toml`. Root Motion: `s_walk`/`s_run` bewegen `root` vorwärts, `t_turn_l/r` drehen
`root` um die Hochachse (Wunsch engine); alle anderen Clips bleiben am Ort.

### Wolf (`wolf`, Platzhalter aus Quaternius Animal Pack Vol.2, CC0)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `wolf/s_idle` | Stehen | – | – | Q | platzhalter (`Idle`) |
| `wolf/s_walk` | Gehen | ✓ 0,34 m/s | footstep_front/back_l/r | Q→ | platzhalter (`Walking` + root vorwärts, Füße stehen) |
| `wolf/s_run` | Rennen | ✓ 1,1 m/s | footstep_front/back_l/r | Q→ | platzhalter-K (`Walking` ×2,5 schneller, Schritte ×1,3) |
| `wolf/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K (Schritte + root-Drehung) |
| `wolf/t_attack_1`, `t_attack_2` | Biss nach vorn (Ducken, Satz) / Schnappen zur Seite | – | hit_start, hit_end | K | platzhalter-K |
| `wolf/t_hit` | Treffer | – | – | K | platzhalter-K |
| `wolf/t_die` | Tod: taumeln, auf die Seite kippen | – | – | K | platzhalter-K |
| `wolf/s_eat`, `s_sleep` | Fressen (Kopf tief, kauen), Schlafen (liegend, atmet) | – | – | K | platzhalter-K |
| `wolf/t_threaten` | Drohen: Kopf tief, Knurr-Nicken | – | – | K | platzhalter-K |

### Keiler (`keiler`, Platzhalter aus Quaternius Farm Animal Pack „Pig“, CC0)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `keiler/s_idle` | Stehen | – | – | Q | platzhalter (`Idle`) |
| `keiler/s_walk` | Gehen | ✓ 0,8 m/s | footstep_front/back_l/r | Q→ | platzhalter (`Walk` 0–32 + root vorwärts) |
| `keiler/s_run` | Traben | ✓ 1,65 m/s | footstep_front/back_l/r | Q→ | platzhalter-K (`Walk` schneller, weitere Schritte; Quell-`Run` streckt die Beine, unbrauchbar) |
| `keiler/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K |
| `keiler/t_attack_1`, `t_attack_2` | Anrennen und Hauer hochreißen / Hauer-Hieb zur Seite | – | hit_start, hit_end | K | platzhalter-K |
| `keiler/t_hit` | Treffer | – | – | K | platzhalter-K |
| `keiler/t_die` | Tod: bäumt sich auf, fällt auf die Seite | – | – | Q | platzhalter (`Death`) |
| `keiler/s_eat`, `s_sleep` | Wühlen (Schnauze am Boden), Schlafen (Seitenlage, atmet) | – | – | K | platzhalter-K |
| `keiler/t_threaten` | Drohen: Kopf tief, Scharren mit dem Vorderhuf | – | – | K | platzhalter-K |

### Laufvogel (`laufvogel`, Platzhalter aus Quaternius „5 Low poly animals“ – Küken, vergrößert, CC0)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `laufvogel/s_idle` | Stehen, umschauen | – | – | K | platzhalter-K |
| `laufvogel/s_walk` | Gehen | ✓ 0,38 m/s | footstep_l/r | Q→ | platzhalter-K (Quell-Schritt am Ort ×2 schneller + root vorwärts; Tempo aus der Schrittlänge) |
| `laufvogel/s_run` | Rennen | ✓ 0,85 m/s | footstep_l/r | Q→ | platzhalter-K (dito, ×3,3 schneller, Schritte ×1,3) |
| `laufvogel/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_l/r | K | platzhalter-K |
| `laufvogel/t_attack_1`, `t_attack_2` | Schnabelhieb nach vorn / Tritt mit dem rechten Fuß | – | hit_start, hit_end | K | platzhalter-K |
| `laufvogel/t_hit` | Treffer | – | – | K | platzhalter-K |
| `laufvogel/t_die` | Tod: taumeln, auf die Seite fallen | – | – | K | platzhalter-K |
| `laufvogel/s_eat`, `s_sleep` | Picken am Boden, Schlafen (auf gefalteten Beinen sitzend) | – | – | K | platzhalter-K |
| `laufvogel/t_threaten` | Drohen: aufrichten, Hals vor, Fauch-Nicken | – | – | K | platzhalter-K |

## Prio C – später

Akrobatik-Varianten, Gangarten (Militär, Frauen, Entspannt) als Varianten-Sets, zusätzliche
Ambient-Routinen, Tanz/Musizieren, Verwandlungs-Übergänge, Spezialaktionen für Story-Szenen.

**Gesamtumfang Menschen (Schätzung): ~400 Clips.** Monster je Art ~15–25 Clips (siehe characters-pipeline.md §7).
