# Animationsliste (Menschen)

Quelle der Wahrheit für **welche** Animationen gebraucht werden. Konvention siehe
`characters-pipeline.md` §3. Gepflegt von der Figuren-Spur (F2–F4); der Abgleich-Tool-Bericht
(F2) markiert Fehlendes.

**Legende** – Typ: `s` Schleife, `t` Übergang/Einmal, `a` additiv · RM: Root Motion ·
Quelle (Vorschlag): Q = Quaternius CC0, M = Mixamo, MC = eigenes Mocap, K = Keyframe ·
Prio: **A** = Meilenstein A (M6), **B** = Vertical Slice (M10), **C** = später ·
Status: offen / platzhalter / fertig

## Prio A – Meilenstein A (vollständig ausgeschrieben)

| Name | Zweck | RM | Events | Quelle | Status |
|---|---|---|---|---|---|
| `none/s_idle` | Stehen, entspannt | – | – | Q | offen |
| `none/s_walk` | Gehen vorwärts | – | footstep_l/r | Q | offen |
| `none/s_walk_back` | Rückwärts gehen | – | footstep_l/r | Q/M | offen |
| `none/s_strafe_l` / `s_strafe_r` | Seitwärts | – | footstep_l/r | Q/M | offen |
| `none/s_run` | Rennen | – | footstep_l/r | Q | offen |
| `none/s_sneak` | Schleichen | – | footstep_l/r (leise) | Q/M | offen |
| `none/t_turn_l` / `t_turn_r` | Auf der Stelle drehen | – | footstep | M | offen |
| `none/t_walk_2_run`, `t_run_2_walk`, `t_run_stop` | Übergänge | – | – | K | offen |
| `none/t_jump_start`, `s_jump_air`, `t_jump_land` | Sprung | – | land | Q | offen |
| `none/t_jump_run` | Sprung aus dem Lauf | – | land | Q/M | offen |
| `none/s_fall`, `t_fall_land_hard` | Fallen, harte Landung | – | land | Q/M | offen |
| `none/t_climb_low` | Kante ~0,8 m hochsteigen | ✓ | – | MC/K | offen |
| `none/t_climb_mid` | Kante ~1,4 m hochziehen | ✓ | – | MC/K | offen |
| `none/t_climb_high` | Kante ~2,0 m hochziehen | ✓ | – | MC/K | offen |
| `none/t_ladder_on`, `s_ladder_up`, `s_ladder_down`, `t_ladder_off` | Leiter | ✓ | – | M/K | offen |
| `swim/s_idle`, `swim/s_forward`, `swim/s_back`, `swim/t_turn_l/r` | Schwimmen | – | splash | Q/M | offen |
| `dive/s_idle`, `dive/s_forward`, `swim/t_2_dive`, `dive/t_2_swim` | Tauchen | – | – | M/K | offen |
| `none/t_slide`, `s_slide` | Hang hinabrutschen | – | – | K | offen |

## Prio B – Vertical Slice (Muster, wird in F2/F4 ausgeschrieben)

| Bereich | Umfang (Richtwert) | Muster / Beispiele | Quelle |
|---|---|---|---|
| Fortbewegung je Waffenmodus (`fist`, `1h`, `2h`, `bow`, `cbow`, `mag`) | ~60 | wie Prio A: s_idle, s_walk, s_run, s_walk_back, s_strafe_l/r, t_turn_l/r | Q/M |
| Waffe ziehen/wegstecken | ~12 | `1h/t_draw`, `1h/t_sheathe`, `2h/t_draw`, `bow/t_draw` … | MC/K |
| Nahkampf 1h/2h je Talent t0–t2 | ~60 | `1h/t_attack_combo1..4_t2`, `t_attack_l/r`, `t_parry`, `t_dodge_back`, `t_hit_front`, `t_stumble` | MC/K |
| Faustkampf | ~10 | `fist/t_attack_combo1..2`, `fist/t_parry` | Q/MC |
| Fernkampf | ~12 | `bow/s_aim`, `bow/t_shoot`, `bow/t_reload`, `cbow/…` | M/K |
| Treffer, Tod, Bewusstlos | ~15 | `none/t_hit_light`, `t_die_front/back`, `t_ko`, `s_ko`, `t_ko_getup` | Q/M |
| Mob-Interaktionen (je Mob: hin / Schleife / weg) | ~60 | `mob/chest/t_open`, `mob/anvil/s_work`, `mob/bed/t_lie_down`, `mob/grindstone/s_work`, `mob/cauldron/s_stir`, `mob/spit/s_turn`, `mob/bench/s_sit`, `mob/lever/t_pull`, `mob/door/t_open`, `mob/ore/s_hack` | MC/K |
| Item-Benutzung | ~15 | `none/t_eat`, `t_drink`, `t_read_scroll`, `t_pickup_ground`, `t_pickup_high`, `t_torch_light` | M/MC |
| Lockpicking, Taschendiebstahl | ~4 | `mob/chest/s_picklock`, `none/t_pickpocket` | K |
| Ambient / Routinen | ~30 | `amb/s_guard_arms_crossed`, `amb/s_sit_ground`, `amb/s_sleep_ground`, `amb/s_sweep`, `amb/s_campfire_warm`, `amb/s_drink_mug`, `amb/s_train_sword`, `amb/s_lean_wall` | MC |
| Dialog-Gesten (additiv Oberkörper) | ~20 | `dlg/a_talk_neutral1..4`, `dlg/a_gesture_shrug`, `a_point`, `a_dismiss`, `a_threaten`, `a_greet` | MC |
| Magie | ~12 | `mag/t_invest`, `mag/s_invest_loop`, `mag/t_cast_projectile`, `t_cast_area`, `t_cast_self` | MC/K |

## Prio C – später

Akrobatik-Varianten, Gangarten (Militär, Frauen, Entspannt) als Varianten-Sets, zusätzliche
Ambient-Routinen, Tanz/Musizieren, Verwandlungs-Übergänge, Spezialaktionen für Story-Szenen.

**Gesamtumfang Menschen (Schätzung): ~400 Clips.** Monster je Art ~15–25 Clips (siehe characters-pipeline.md §7).
