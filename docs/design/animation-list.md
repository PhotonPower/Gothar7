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
| Waffe ziehen/wegstecken | ~12 | `1h/t_draw`, `1h/t_sheath`, `2h/t_draw`, `bow/t_draw` … | MC/K |
| Nahkampf 1h/2h (Talent nur über die Abspielrate) | ~20 | `1h/t_attack_combo1..4`, `t_attack_l/r`, `t_parry`, `t_dodge_back` | MC/K |
| Faustkampf | ~10 | `fist/t_attack_combo1..2`, `fist/t_parry` | Q/MC |
| Fernkampf | ~12 | `bow/s_aim`, `bow/t_shoot`, `bow/t_reload`, `cbow/…` | MC/K |
| Treffer, Tod, Bewusstlos | ~15 | `none/t_hit_light`, `t_die_front/back`, `t_ko`, `s_ko`, `t_ko_getup` | Q/MC |
| Mob-Interaktionen (je Mob: hin / Schleife / weg) | ~60 | `mob/chest/t_open`, `mob/anvil/s_work`, `mob/bed/t_lie_down`, `mob/grindstone/s_work`, `mob/cauldron/s_stir`, `mob/spit/s_turn`, `mob/bench/s_sit`, `mob/lever/t_pull`, `mob/door/t_open`, `mob/ore/s_hack` | MC/K |
| Item-Benutzung | ~15 | `none/t_eat`, `t_drink`, `t_read_scroll`, `t_pickup_ground`, `t_pickup_high`, `t_torch_light` | Q/MC |
| Lockpicking, Taschendiebstahl | ~4 | `mob/chest/s_picklock`, `none/t_pickpocket` | K |
| Ambient / Routinen | ~30 | `amb/s_guard_arms_crossed`, `amb/s_sit_ground`, `amb/s_sleep_ground`, `amb/s_sweep`, `amb/s_campfire_warm`, `amb/s_drink_mug`, `amb/s_train_sword`, `amb/s_lean_wall` | MC |
| Dialog-Gesten (additiv Oberkörper) | ~20 | `dlg/a_talk_neutral1..4`, `dlg/a_gesture_shrug`, `a_point`, `a_dismiss`, `a_threaten`, `a_greet` | MC |
| Magie | ~14 | `mag/t_draw`, `t_invest`, `s_invest`, `t_cast_projectile/target/self/area/summon`, `s_cast_loop` (ausgeschrieben unten) | MC/K |

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
| `amb/t_chop_wood_in`, `s_chop_wood`, `t_chop_wood_out` | Holz hacken mit der Axt `it_axe` (FP_CHOP) | – | item_to_hand, hit_wood, sound:wood_chop, item_from_hand | Q→ | platzhalter (UAL2 `TreeChopping_Loop`, Unterarm halb gedreht: Schneide voran; Ein-/Ausstieg Überblendung 12 Frames) |
| `amb/s_harvest`, `s_water`, `s_repair_kneel` | Ernten, gießen, kniend reparieren (FP_HARVEST, FP_WATER, FP_REPAIR) | – | – | Q→ | platzhalter (UAL2 `Farm_Harvest`, `Farm_Watering`, UAL1 `Fixing_Kneeling`; auf der Stelle, Schleife geschlossen) |
| `none/s_idle_look`, `none/t_idle_scratch` | Stand-Varianten: sich umsehen, am Kopf kratzen | – | – | K | platzhalter-K |
| `none/t_warn`, `t_point`, `t_surprised`, `t_search` | Reaktionen: warnen (erhobene Faust), zeigen, erschrecken, suchen | – | – | K | platzhalter-K |
| `1h/t_draw`, `1h/t_sheath` | Einhandwaffe ziehen bzw. wegstecken (Overlay ab spine_02) | – | draw, sheath | K | platzhalter-K (rechte Hand greift quer über den Bauch zum Griff an `socket_hip_1h`, dann in die `1h`-Haltung; wegstecken rückwärts) |
| `fist/t_draw`, `fist/t_sheath` | Fäuste heben bzw. senken | – | – | K | platzhalter-K (Überblendung 15 Frames) |

### Prio B – Dialog-Gesten für M10 (ausgeschrieben, additiv)

Additive Oberkörper-Clips (Vertrag `characters-pipeline.md` §3.2), Set `dlg` (`anims/human/dlg.glb`), Referenz
`dlg/a_neutral`. Event `beat` auf der Betonung.

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `dlg/a_neutral` | Referenzpose (1 Frame, erster Frame von `none/s_idle` ohne Atmung) | – | – | K | platzhalter-K |
| `dlg/a_talk_1`, `a_talk_2`, `a_talk_3`, `a_talk_4` | Redegesten: rechte Hand, beide Hände, linke Hand, offen | 2,5–3,5 s | – | K | platzhalter-K |
| `dlg/a_nod`, `a_shake_head` | Nicken, Kopf schütteln | 0,9–1,1 s | beat | K | platzhalter-K |
| `dlg/a_shrug`, `a_dismiss` | Achselzucken (Handflächen offen), abwinken | 1,2–1,5 s | beat | K | platzhalter-K |
| `dlg/a_point_self`, `a_point` | auf sich zeigen, nach vorn zeigen (Zeigefinger gestreckt) | 1,7–1,8 s | beat | K | platzhalter-K |
| `dlg/a_explain`, `a_threaten` | mit beiden Händen erklären, mit dem Zeigefinger drohen | 2–2,7 s | beat | K | platzhalter-K |
| `dlg/a_greet`, `a_fist`, `a_bow` | grüßen (Hand heben), Faust (Ärger), Oberkörper zum Gruß neigen | 1,5–1,6 s | beat | K | platzhalter-K |
| `dlg/a_arms_crossed_in`, `a_arms_crossed`, `a_arms_crossed_out` | Arme verschränken, halten, lösen | 0,5 s / Schleife | – | Q→ | platzhalter (UAL2 `Idle_FoldArms_Loop` ab spine_02) |
| `dlg/a_hands_hips_in`, `a_hands_hips`, `a_hands_hips_out` | Hände in die Hüften, halten, lösen | 0,5 s / Schleife | – | K | platzhalter-K |

### Prio B – Kampf für M11 (ausgeschrieben, Liste von engine)

Vertrag mit engine (2026-10-05): Jeder Angriff beginnt und endet in der Kampfhaltung des Modus (`<modus>/s_idle`),
engine blendet 0,1 s zum nächsten Schlag; Trefferfenster `hit_start`/`hit_end` (Fäuste: Aufprall der Faust),
Kombo-Fenster `combo_start` (= `hit_end`) bis `combo_end` (ca. 80 %), danach Erholung. Talentstufen nur über die
Abspielrate (keine Clips je Stufe). Zweihänder: linke Hand per Rezept `two_hands` am Griff unter der rechten.
Fernkampf: Event `release` beim Lösen des Schusses. Schwung-Laut (M13, Wunsch engine 2026-10-08): `sound:swing` in jedem
Schlag-Clip von `1h`, `2h` und `fist`, 4 Bilder vor `hit_start` (Ziehen, Wegstecken und Treffer setzt engine selbst).

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/t_hit_light` | leichter Treffer (Zucken) | 0,33 s | – | Q | platzhalter (UAL1 `Hit_Chest`) |
| `none/t_die_front` | Tod nach vorn, bleibt liegen (keine Schleife) | 1,7 s | – | K | platzhalter-K (taumeln, Knie, aufs Gesicht) |
| `none/t_die_back` | Tod nach hinten, bleibt liegen | 2,4 s | – | Q | platzhalter (UAL1 `Death01`) |
| `none/t_ko` | K.o. (Faustkampf verloren): rücklings hingeworfen, endet in der `s_ko`-Pose | 0,83 s | – | Q | platzhalter (UAL2 `Hit_Knockback`) |
| `none/s_ko` | bewusstlos liegen (Schleife, atmet) | 2,0 s | – | K | platzhalter-K (letzte Pose von `t_ko`) |
| `none/t_ko_getup` | aufstehen, beginnt in der `s_ko`-Pose | 1,5 s | – | Q | platzhalter (UAL2 `LayToIdle`, eingeblendet aus `s_ko`) |
| `fist/t_attack_combo1` | Faust: Gerade links (Kombo 1) | 0,87 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL1 `Punch_Jab`, aus/in `fist/s_idle`) |
| `fist/t_attack_combo2` | Faust: Gerade rechts (Kombo 2) | 1,0 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL1 `Punch_Cross`, aus/in `fist/s_idle`) |
| `fist/t_parry` | Deckung: Unterarme vor dem Gesicht (Blockfenster setzt engine) | 0,6 s | – | K | platzhalter-K (Pose per Gittersuche) |
| `1h/t_attack_combo1` | Kombo 1: Hieb von oben | 1,4 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL2 `Sword_Regular_A` + `_Rec`, aus/in `1h/s_idle`) |
| `1h/t_attack_combo2` | Kombo 2: waagrechter Schnitt mit Ausfall | 1,6 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL2 `Sword_Regular_B` + `_Rec`) |
| `1h/t_attack_combo3` | Kombo 3: Stich | 1,0 s | hit_start, hit_end, combo_start, combo_end | K | platzhalter-K (UAL2 `Sword_Regular_C` ist ein Drehsprung, unbrauchbar) |
| `1h/t_attack_combo4` | Kombo 4: wuchtiger Abschluss | 1,5 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL1 `Sword_Attack`) |
| `1h/t_attack_l` | Richtungshieb nach links (Gothic 1) | 1,0 s | hit_start, hit_end | K | platzhalter-K |
| `1h/t_attack_r` | Richtungshieb nach rechts (Rückhand) | 1,0 s | hit_start, hit_end | K | platzhalter-K |
| `1h/t_parry` | Parade von vorn (Blockfenster setzt engine) | 0,5 s | – | Q | platzhalter (UAL2 `Sword_Block`, gekürzt) |
| `1h/t_dodge_back` | Sprung zurück, Root Motion 0,8 m | 0,8 s | – | K | platzhalter-K |
| `2h/t_attack_combo1` | Kombo 1: Hieb von oben, beide Hände am Griff | 1,4 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL2 `Sword_Regular_A` + `_Rec`, linke Hand per `two_hands`) |
| `2h/t_attack_combo2` | Kombo 2: weiter waagrechter Schnitt | 1,6 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL2 `Sword_Regular_B` + `_Rec`; am weitesten Punkt erreicht die linke Hand den Griff nicht ganz) |
| `2h/t_attack_combo3` | Kombo 3: schräger Hieb von oben | 1,3 s | hit_start, hit_end, combo_start, combo_end | K | platzhalter-K |
| `2h/t_attack_combo4` | Kombo 4: wuchtiger Abschluss | 1,5 s | hit_start, hit_end, combo_start, combo_end | Q | platzhalter (UAL1 `Sword_Attack`) |
| `2h/t_attack_l` | Richtungshieb nach links, weit ausholend | 1,3 s | hit_start, hit_end | K | platzhalter-K |
| `2h/t_attack_r` | Richtungshieb nach rechts | 1,3 s | hit_start, hit_end | K | platzhalter-K |
| `2h/t_parry` | Parade von vorn, beide Hände am Griff | 0,5 s | – | Q | platzhalter (UAL2 `Sword_Block`, gekürzt) |
| `2h/t_dodge_back` | Sprung zurück, Root Motion 0,8 m | 0,8 s | – | K | platzhalter-K |
| `bow/s_aim` | zielen (Schleife): seitlich, Bogenarm gestreckt, Sehnenhand an der Wange | 2,5 s | – | K | platzhalter-K (Pose per Gittersuche) |
| `bow/t_shoot` | Schuss, Sehnenhand schnellt zurück, wieder zielen | 0,67 s | release | K | platzhalter-K |
| `bow/t_reload` | Pfeil aus dem Köcher über der rechten Schulter, auflegen, zielen | 1,2 s | – | K | platzhalter-K |
| `cbow/s_aim` | zielen (Schleife), beide Hände vorn | 2,0 s | – | Q | platzhalter (UAL1 `Pistol_Aim_Neutral`, gehalten) |
| `cbow/t_shoot` | Schuss mit Rückstoß | 0,63 s | release | Q | platzhalter (UAL1 `Pistol_Shoot`) |
| `cbow/t_reload` | nachladen | 1,7 s | – | Q | platzhalter (UAL1 `Pistol_Reload`) |

### Prio B – Magie für M12 (ausgeschrieben, Liste von engine)

Vertrag mit engine (2026-10-06): Rune bzw. Spruchrolle in der rechten Hand (`socket_hand_r`); jeder Einmal-Clip
beginnt und endet in `mag/s_idle`; Event `cast` = der Effekt startet. Fünf Wirk-Varianten; Verwandlung nutzt
`t_cast_self`, Schlaf die K.o.-Clips von `none`. Kreise bzw. Talent nur über Abspielrate und Effekt.

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `mag/t_draw` | Rune bzw. Spruchrolle aus dem Beutel am Gürtel in die Hand | 0,8 s | draw | K | platzhalter-K |
| `mag/t_sheath` | zurück in den Beutel | 0,8 s | sheath | K | platzhalter-K (rückwärts) |
| `mag/t_invest` | Aufladen beginnen | 0,6 s | – | Q | platzhalter (UAL1 `Spell_Simple_Enter`) |
| `mag/s_invest` | Aufladen, solange die Taste gehalten wird (Schleife; Stufen zeigt der Effekt) | 2,0 s | – | K | platzhalter-K (Endpose von `t_invest`) |
| `mag/t_cast_projectile` | Stoß nach vorn (Feuerpfeil) | 0,9 s | cast | Q | platzhalter (UAL1 `Spell_Simple_Shoot`) |
| `mag/t_cast_target` | auf ein Ziel zeigen (Schlaf, Furcht, Kontrolle) | 1,1 s | cast | K | platzhalter-K (Pose per Gittersuche) |
| `mag/t_cast_self` | Hände zur Brust (Heilung, eigene Verwandlung) | 1,1 s | cast | K | platzhalter-K (Pose per Gittersuche) |
| `mag/t_cast_area` | Arme hoch, dann ausgebreitet (Flächenzauber) | 1,2 s | cast | K | platzhalter-K |
| `mag/t_cast_summon` | Arm hoch, dann zum Boden vor sich (Beschwörung) | 1,3 s | cast | K | platzhalter-K |
| `mag/s_cast_loop` | Dauerzauber: Arm ausgestreckt (Schleife) | 2,5 s | – | K | platzhalter-K |
| `mag/t_cast_loop_end` | vom Dauerzauber zurück in die Haltung | 0,4 s | – | K | platzhalter-K |
| `mag/t_cast_fail` | misslingt (kein Mana): die Hand zuckt | 0,7 s | – | K | platzhalter-K |
| `none/t_hit_magic` | Treffer durch Magie: zurückgeworfen, Arme vor dem Gesicht | 0,9 s | – | K | platzhalter-K |
| `none/s_burn` | brennt (Schleife): rennt mit schlagenden Armen, engine bewegt ihn | 0,93 s | footstep_l/r | K | platzhalter-K (über `none/s_run`) |
| `none/t_transform_out` | Verwandlung Mensch → Wolf: krümmt sich, sinkt auf alle viere; bei `swap` tauscht engine das Modell | 0,8 s | swap (Bild 12) | K | platzhalter-K (mit engine 2026-10-08) |
| `none/t_transform_in` | Rückverwandlung Wolf → Mensch: richtet sich aus der Hocke auf, endet in `none/s_idle` | 0,8 s | – | K | platzhalter-K (mit engine 2026-10-08) |

### Prio B – Fackel (F6, ausgeschrieben, mit engine 2026-10-08)

Eigene Datei `anims/human/torch.glb` (Set `torch`, `none.glb` bleibt unverändert; engine sucht Clips über den vollen
Namen in allen Sätzen, Namen über alle Dateien eindeutig, Validator `anim.duplicate`). `it_torch` in der linken Hand
(`socket_hand_l`), Flamme am Knoten `socket_flame` des Items. Entscheidung Projektinhaber (wie Gothic 1): sichtbar
beim Gehen, Rennen und Schleichen, auch mit Fäusten oder Einhandwaffe rechts; bei Zweihänder, Bogen, Armbrust und
Magie weggesteckt; brennt unbegrenzt, Wasser löscht sie. `a_torch_hold` bewegt nur Schulter und Arm links und passt
deshalb additiv über `none`-, `fist`- und `1h`-Clips (Referenz `none/a_neutral`).

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/a_neutral` | Referenzpose der `none`-Overlays (1 Frame, erster Frame von `none/s_idle` ohne Atmung) | – | – | K | platzhalter-K |
| `none/a_torch_hold` | Fackel tragen: linker Arm angewinkelt, Fackel aufrecht neben dem Körper, leicht vor (Schleife) | 1 s | – | K | platzhalter-K (Haltung per Gittersuche) |
| `none/a_torch_hold_in`, `a_torch_hold_out` | in die Tragehaltung bzw. zurück zum freien Arm | 0,33 s | – | K | platzhalter-K |
| `none/t_torch_light` | Fackel vom Gürtel nehmen, hochheben, mit rechts anzünden; endet in der Tragehaltung | 1,2 s | torch_take, torch_light | K | platzhalter-K |
| `none/t_torch_drop` | Fackel nach vorn links wegwerfen (liegt brennend am Boden) | 0,67 s | torch_drop | K | platzhalter-K |

### Prio C – Gangarten-Varianten für NPC-Vielfalt (ausgeschrieben, mit engine 2026-10-08)

Eigene Datei `anims/human/gait.glb` (Set `gait`, `none.glb` bleibt unverändert). Eine Figur mit `[anim] variant = "<v>"`
im Manifest (`characters-pipeline.md` §6.2) spielt `none/X_<v>` statt `none/X`, wenn es den Clip gibt, sonst den
Grundclip. Die Varianten sind feste Haltungen oberhalb des Beckens über den Grundclips; Füße und Tempo bleiben die
des Grundclips (`events.toml`), alt trabt langsam (`s_run` mit halber Geschwindigkeit). Armhaltungen per Gittersuche.

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/s_idle_woman`, `s_walk_woman`, `s_run_woman` | Frau: aufrecht, Arme nah am Körper | – / 0,98 / 5,90 m/s | footstep_l/r | K | platzhalter-K (über `none/s_idle`, `s_walk`, `s_run`) |
| `none/s_idle_military`, `s_walk_military` | Wache: aufrecht, Brust raus, Kinn hoch, Fäuste | – / 0,98 m/s | footstep_l/r | K | platzhalter-K |
| `none/s_idle_old`, `s_walk_old`, `s_run_old` | Alt: gebeugt, Kopf vor, linke Hand im Kreuz; langsamer Trab | – / 0,98 / 2,94 m/s | footstep_l/r | K | platzhalter-K |
| `none/s_idle_relaxed`, `s_walk_relaxed` | Entspannt: Hände hinter dem Rücken, Kopf leicht gesenkt | – / 0,98 m/s | footstep_l/r | K | platzhalter-K |

### Prio B – Kirche und Arme (ausgeschrieben, mit engine und welt 2026-10-08)

Eigene Datei `anims/human/church.glb` (Satz `church`, `amb.glb` bleibt unverändert; engine sucht die Clips über den vollen
Namen in allen Sätzen). Freepoints von welt: `FP_PRAY_…` (in der Kirche, Blick zum Altar, 0,6 m frei nach vorn),
`FP_PREACH_…` (Altar, Kanzel, Markt), `FP_BEG_…` (Kirchentür, Brunnen, Tor; Wand dahinter möglich). Ursprung =
Fußpunkt am Freepoint, Blick +Z, ohne Root Motion. Figuren: `priest` (benannt), Sets `monk` (nur Männer) und
`beggar` (`_m`/`_f`).

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `amb/t_pray_in`, `amb/t_pray_out` | auf beide Knie sinken (über ein Knie), Hände vor der Brust falten; aufstehen rückwärts | 1,5 s | – | K | platzhalter-K |
| `amb/s_pray` | kniend beten (Schleife): Kopf gesenkt, ab und zu gehoben | 4 s | – | K | platzhalter-K (Handhaltung per Gittersuche) |
| `amb/s_pray_stand` | stehend beten (Mönche), Hände gefaltet | 4 s | – | K | platzhalter-K |
| `amb/s_preach` | predigen (Schleife): rechte Hand erhoben, mit der linken erklären, beide Hände offen | 6 s | beat | K | platzhalter-K |
| `amb/t_beg_in`, `amb/t_beg_out` | sich auf den Boden setzen (linkes Knie hoch, rechtes Bein ausgestreckt); aufstehen rückwärts | 1,5 s | – | K | platzhalter-K |
| `amb/s_beg` | sitzend betteln (Schleife): rechte Hand offen nach vorn, ab und zu gehoben | 4 s | – | K | platzhalter-K |

### Prio B – Musiker im Gasthof (ausgeschrieben, mit engine 2026-10-08)

Eigene Datei `anims/human/music.glb` (Satz `music`, Clips heißen `amb/…`). `it_lute` in der rechten Hand
(`socket_hand_r`) über `item_to_hand` bzw. `item_from_hand` in den Ein- und Ausstiegen; `sound:lute_play` am Anfang
jeder Schleife. Stehend am Freepoint oder sitzend auf Bank bzw. Hocker dahinter (Becken 0,3 m zurück wie `mob/bench`).
Die Armhaltungen sind gesucht: rechte Hand vor dem Bauch über den Saiten, die Laute per Hand so gedreht, dass der Hals
nach links oben und die Decke nach vorn zeigt, linke Hand am Hals (je 1–3 cm genau).

| Name | Zweck | Länge | Events | Quelle | Status |
|---|---|---|---|---|---|
| `amb/t_play_lute_in`, `amb/t_play_lute_out` | Laute nehmen und in Spielhaltung bringen; zurück | 0,8 s | item_to_hand, item_from_hand | K | platzhalter-K |
| `amb/s_play_lute` | stehend spielen (Schleife): rechter Unterarm zupft, Kopf wiegt | 4 s | sound:lute_play | K | platzhalter-K |
| `amb/t_play_lute_sit_in`, `amb/t_play_lute_sit_out` | hinsetzen und Laute nehmen; zurück | 1,3 s | item_to_hand, item_from_hand | K | platzhalter-K |
| `amb/s_play_lute_sit` | sitzend spielen (Schleife) | 4 s | sound:lute_play | K | platzhalter-K |

## Monster (F5, Vertrag `characters-pipeline.md` §7)

Mindestumfang je Art. Clips liegen in `assets/source/characters/monsters/<art>/anims/<art>.glb`, Herkunft je
Clip: `data/clips/<art>.toml`. Root Motion: `s_walk`/`s_trot`/`s_run` bewegen `root` vorwärts, `t_turn_l/r` drehen
`root` um die Hochachse (Wunsch engine); alle anderen Clips bleiben am Ort.

### Wolf (`wolf`, eigene Art, `gothar-chargen creature`; ersetzt den Quaternius-Platzhalter 2026-10-08)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `wolf/s_idle` | Stehen: atmen, Ohren zucken, umschauen, Schwanz pendelt | – | – | K | platzhalter-K |
| `wolf/s_walk` | Gehen (Kreuzgang) | ✓ 1,2 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt)) |
| `wolf/s_trot` | Traben (diagonale Paare; Rudel folgen, drohend annähern) | ✓ 3,0 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt); mit engine vereinbart 2026-10-05) |
| `wolf/s_run` | Rennen (Rotationsgalopp, Rücken beugt sich) | ✓ 6,0 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt)) |
| `wolf/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K (Schritte + root-Drehung) |
| `wolf/t_attack_1`, `t_attack_2` | Biss nach vorn (Ducken, Satz) / Schnappen zur Seite; Kiefer auf und zu, Ohren angelegt | – | hit_start, hit_end | K | platzhalter-K |
| `wolf/t_hit` | Treffer | – | – | K | platzhalter-K |
| `wolf/t_die` | Tod: taumeln, auf die Seite kippen | – | – | K | platzhalter-K |
| `wolf/s_eat`, `s_sleep` | Fressen (Kopf tief, Kiefer kaut), Schlafen (liegend, atmet) | – | – | K | platzhalter-K |
| `wolf/t_threaten` | Drohen: Kopf tief, Zähne gefletscht, Ohren flach, Knurr-Nicken | – | – | K | platzhalter-K |
| `wolf/t_transform_in` | Verwandlung Mensch → Wolf: aus der Hocke aufrichten, Fell schütteln, endet in `wolf/s_idle` (0,8 s) | – | – | K | platzhalter-K (mit engine 2026-10-08) |
| `wolf/t_transform_out` | Rückverwandlung: steigt auf die Hinterbeine, Kopf nach oben; bei `swap` tauscht engine das Modell (0,8 s) | – | swap (Bild 12) | K | platzhalter-K (mit engine 2026-10-08) |

### Keiler (`keiler`, eigene Art, `gothar-chargen creature`; ersetzt den Quaternius-Platzhalter 2026-10-08)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `keiler/s_idle` | Stehen: atmen, schnüffeln, Ohren und Schwanz zucken | – | – | K | platzhalter-K |
| `keiler/s_walk` | Gehen (Kreuzgang) | ✓ 1,0 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt)) |
| `keiler/s_run` | Rennen (Galopp) | ✓ 5,0 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt); Quell-`Run` streckt die Beine, unbrauchbar) |
| `keiler/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K |
| `keiler/t_attack_1`, `t_attack_2` | Anrennen und Hauer hochreißen / Hauer-Hieb zur Seite; Maul auf, Ohren angelegt | – | hit_start, hit_end | K | platzhalter-K |
| `keiler/t_hit` | Treffer | – | – | K | platzhalter-K |
| `keiler/t_die` | Tod: taumelt, knickt vorn ein, rollt auf die Seite | – | – | K | platzhalter-K |
| `keiler/s_eat`, `s_sleep` | Wühlen (Schnauze am Boden, Kiefer arbeitet), Schlafen (Seitenlage, atmet) | – | – | K | platzhalter-K |
| `keiler/t_threaten` | Drohen: Kopf tief, Ohren flach, schnaubt, scharrt mit dem Vorderhuf | – | – | K | platzhalter-K |

### Laufvogel (`laufvogel`, eigene Art, `gothar-chargen creature`; ersetzt den Quaternius-Platzhalter 2026-10-08)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `laufvogel/s_idle` | Stehen, ruckartig umschauen, Flügel schütteln | – | – | K | platzhalter-K |
| `laufvogel/s_walk` | Gehen (Kopf nickt mit) | ✓ 1,3 m/s | footstep_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt)) |
| `laufvogel/s_run` | Rennen (mit Flugphase, Hals vorgestreckt, Flügel abgespreizt) | ✓ 6,5 m/s | footstep_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`: Füße stehen, Beine per IK, Körper federt)) |
| `laufvogel/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_l/r | K | platzhalter-K |
| `laufvogel/t_attack_1`, `t_attack_2` | Hakenschnabel-Hieb nach vorn / Tritt mit dem rechten Fuß, Flügel zum Ausgleich | – | hit_start, hit_end | K | platzhalter-K |
| `laufvogel/t_hit` | Treffer | – | – | K | platzhalter-K |
| `laufvogel/t_die` | Tod: taumeln, auf die Seite fallen | – | – | K | platzhalter-K |
| `laufvogel/s_eat`, `s_sleep` | Picken am Boden, Schlafen (auf gefalteten Beinen sitzend, Kopf nach hinten gelegt) | – | – | K | platzhalter-K |
| `laufvogel/t_threaten` | Drohen: aufrichten, Flügel spreizen, Schnabel auf, Fauch-Nicken | – | – | K | platzhalter-K |

### Schinder (`schinder`, eigene Art, `gothar-chargen creature`; Design `monsters.md`)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `schinder/s_idle` | Stehen, Kopf tief, schnüffeln und umschauen | – | – | K | platzhalter-K |
| `schinder/s_walk` | Gehen (Kreuzgang) | ✓ 1,3 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `schinder/s_run` | Rennen (Galopp, Rücken beugt sich) | ✓ 6,5 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `schinder/s_sneak` | Schleichen: Beute umkreisen, geduckt, Kopf vor, Ohren zurück | ✓ 0,7 m/s | footstep_front/back_l/r | K | platzhalter-K (`gait` mit fester Haltung; mit engine 2026-10-07) |
| `schinder/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K (Treten am Ort + root-Drehung) |
| `schinder/t_attack_1`, `t_attack_2` | Schneller Biss nach vorn / Zubeißen und mit Kopfschütteln reißen | – | hit_start, hit_end | K | platzhalter-K |
| `schinder/t_hit` | Treffer | – | – | K | platzhalter-K |
| `schinder/t_die` | Tod: taumeln, auf die Seite fallen | – | – | K | platzhalter-K |
| `schinder/s_eat`, `s_sleep` | An der Leiche zerren und kauen / Schlafen (zusammengerollt, atmet) | – | – | K | platzhalter-K |
| `schinder/t_threaten` | Drohen: geduckt, Kopf tief, fauchen und kichern (Kiefer klappert) | – | – | K | platzhalter-K |
| `schinder/t_call` | Rudelruf: Kopf hochgeworfen, heulen | – | call, sound:schinder_call | K | platzhalter-K (mit engine 2026-10-07) |
| `schinder/s_cower` | Unterlegen: tief geduckt, Ohren flach, Schwanz eingezogen, zittert | – | – | K | platzhalter-K (mit engine 2026-10-07) |

### Quaderbuckel (`quaderbuckel`, eigene Art, `gothar-chargen creature`; Design `monsters.md`)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `quaderbuckel/s_idle` | Schwer stehen, atmen, Kopf pendelt | – | – | K | platzhalter-K |
| `quaderbuckel/s_walk` | Gehen (stapfend, Kreuzgang) | ✓ 0,8 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `quaderbuckel/s_run` | Rennen (schwerfälliger Trab, langsamer als der Held) | ✓ 3,5 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `quaderbuckel/s_charge` | Anrennen mit gesenktem Schild, **am Ort** (Eigengeschwindigkeit) | 4,5 m/s | footstep_front/back_l/r | K | platzhalter-K (`gait` am Ort; mit engine 2026-10-07) |
| `quaderbuckel/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K |
| `quaderbuckel/t_attack_1` | Rammstoß: zurücksetzen, Stoß mit dem Schild (engine wirft um) | – | hit_start, hit_end | K | platzhalter-K |
| `quaderbuckel/t_attack_2` | Kopf- und Hauerhieb zur Seite, dann Zuschnappen | – | hit_start, hit_end | K | platzhalter-K |
| `quaderbuckel/t_hit` | Treffer | – | – | K | platzhalter-K |
| `quaderbuckel/t_die` | Tod: Beine knicken weg, sinkt auf den Bauch | – | – | K | platzhalter-K |
| `quaderbuckel/s_eat`, `s_sleep` | Flechten vom Stein schaben / eingegraben schlafen, Kopf unter dem Schild | – | – | K | platzhalter-K |
| `quaderbuckel/t_threaten` | Drohen: Vorderleib hoch, Maul offen, Schwanz peitscht | – | – | K | platzhalter-K |
| `quaderbuckel/t_warn` | Warnen: mit den Vorderfüßen stampfen, Platten knirschen | – | footstep_front_l/r, sound:quaderbuckel_grind | K | platzhalter-K (mit engine 2026-10-07) |
| `quaderbuckel/t_block_in`, `s_block`, `t_block_out` | Kopf unter den Stirnschild ziehen, halten, wieder heraus (Klingen prallen von vorn ab) | – | – | K | platzhalter-K (mit engine 2026-10-07) |

### Glemsmahr (`glemsmahr`, eigene Art, `gothar-chargen creature`; Design `monsters.md`)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `glemsmahr/s_idle` | Geduckt lauschen, Kopf schief, Ohren drehen | – | – | K | platzhalter-K |
| `glemsmahr/s_walk` | Knöchelgang (Kreuzgang) | ✓ 1,4 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `glemsmahr/s_run` | Galopp auf allen vieren | ✓ 6,5 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `glemsmahr/s_sneak` | Anschleichen, sehr tief, Kopf vor, Ohren zurück | ✓ 0,9 m/s | footstep_front/back_l/r | K | platzhalter-K (`gait` mit fester Haltung; mit engine 2026-10-07) |
| `glemsmahr/t_turn_l/r` | Auf der Stelle drehen (90°) | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K |
| `glemsmahr/t_rise`, `s_upright`, `t_lower` | Aufrichten auf die Hinterbeine, aufrecht wittern und umschauen, wieder auf alle viere | – | – | K | platzhalter-K (mit engine 2026-10-07: aufrecht nur Drohen bzw. Wittern) |
| `glemsmahr/t_attack_1` | Krallenhieb: Vorderleib hebt sich, rechter Arm schlägt quer | – | hit_start, hit_end | K | platzhalter-K |
| `glemsmahr/t_attack_2` | Biss nach vorn mit den Nadelzähnen | – | hit_start, hit_end | K | platzhalter-K |
| `glemsmahr/t_leap` | Sprung auf den Helden, **am Ort** (engine fährt ~3 m) | – | leap_start, leap_land, hit_start, hit_end | K | platzhalter-K (mit engine 2026-10-07) |
| `glemsmahr/t_jump_back` | Satz zurück ins Dunkel, **am Ort** (engine fährt ~2,5 m) | – | leap_start, leap_land | K | platzhalter-K (mit engine 2026-10-07) |
| `glemsmahr/t_recoil` | Zurückweichen vor Licht bzw. Feuer: Arm vor den Augen, fauchen | – | recoil, sound:glemsmahr_hiss | K | platzhalter-K (mit engine 2026-10-07) |
| `glemsmahr/t_hit` | Treffer | – | – | K | platzhalter-K |
| `glemsmahr/t_die` | Tod: bricht auf die Seite zusammen | – | – | K | platzhalter-K |
| `glemsmahr/s_eat`, `s_sleep` | Geduckt reißen und Brocken zum Maul führen / zusammengerollt schlafen | – | – | K | platzhalter-K |
| `glemsmahr/t_threaten` | Drohen: halb aufgerichtet, Arme gespreizt, fauchen | – | sound:glemsmahr_hiss | K | platzhalter-K |

### Bergleu (`bergleu`, Boss, eigene Art, `gothar-chargen creature`; Design `monsters.md`)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `bergleu/s_idle` | Schwer atmend, Mähne und Schwanz pendeln, umschauen | – | – | K | platzhalter-K |
| `bergleu/s_walk` | Pirschgang (Kreuzgang) | ✓ 1,5 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `bergleu/s_run` | Sprunggalopp, Rücken beugt sich | ✓ 8,0 m/s | footstep_front/back_l/r | K | platzhalter-K (eigener Gang-Zyklus (`gait`)) |
| `bergleu/s_stalk` | Anpirschen: tief geduckt, Kopf vor | ✓ 1,0 m/s | footstep_front/back_l/r | K | platzhalter-K (`gait` mit fester Haltung; mit engine 2026-10-08) |
| `bergleu/t_turn_l/r`, `t_turn_back` | Drehen 90° bzw. schnelle 180°-Wende | ✓ Drehung | footstep_front/back_l/r | K | platzhalter-K |
| `bergleu/t_attack_1` | Prankenhieb von rechts (Treffer an `socket_paw_r`) | – | hit_start, hit_end | K | platzhalter-K |
| `bergleu/t_attack_2` | Biss mit Schütteln | – | hit_start, hit_end | K | platzhalter-K |
| `bergleu/t_tail_lash` | Schwanzschlag: Hüfte schwingt, Schwanz fegt nach hinten bzw. zur Seite (Treffer an `socket_tail`) | – | hit_start, hit_end | K | platzhalter-K (mit engine 2026-10-08) |
| `bergleu/t_pounce` | Sprung auf den Helden, **am Ort** (engine fährt ~5 m) | – | leap_start, leap_land, hit_start, hit_end | K | platzhalter-K (mit engine 2026-10-08) |
| `bergleu/t_roar` | Brüllen: Vorderleib hoch, Maul weit auf, Mähne gesträubt (Held taumelt, Begleiter fliehen) | – | roar, sound:bergleu_roar | K | platzhalter-K (mit engine 2026-10-08) |
| `bergleu/t_rage` | Übergang in die Raserei: Kopf schütteln, scharren, brüllen; danach normale Clips mit Rate ~1,3 | – | rage, sound:bergleu_roar | K | platzhalter-K (mit engine 2026-10-08) |
| `bergleu/t_hit`, `t_stagger` | Treffer / schwerer Treffer bzw. parierter Prankenhieb | – | – | K | platzhalter-K |
| `bergleu/t_die` | Langer Tod: taumelt, sinkt auf den Bauch, Kopf zu Boden | – | – | K | platzhalter-K |
| `bergleu/s_eat`, `s_sleep` | Liegend am Riss fressen / auf der Seite schlafen | – | – | K | platzhalter-K |
| `bergleu/t_threaten` | Drohen: geduckt, fauchen, Ohren flach, Schwanz peitscht | – | – | K | platzhalter-K |

## Prio C – später

Akrobatik-Varianten, weitere Gangarten-Varianten (erste vier siehe oben), zusätzliche
Ambient-Routinen, Tanz/Musizieren, weitere Verwandlungs-Übergänge (Wolf: siehe Magie und Wolf), Spezialaktionen für Story-Szenen.

**Gesamtumfang Menschen (Schätzung): ~400 Clips.** Monster je Art ~15–25 Clips (siehe characters-pipeline.md §7).
