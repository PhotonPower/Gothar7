# Leonberg – Routinen-Orte (W7)

Erzeugt von `gothar-worldgen assemble leonberg` aus `tools/worldgen/data/leonberg/uses.json`
(`generated/uses_table.md`, hier als Übergabe an engine und figuren eingecheckt; bei Änderungen der Nutzungen
neu erzeugen und kopieren). Je Haus: Routinen-Wegpunkt vor der Tür (Ziel für `at` in Routinen), Freepoints vor
dem Haus (`npc_goto_freepoint(npc, "<TYP>", r)` findet sie über den Typ) und Mobs neben der Tür. Freepoint- und
Mob-Namen enthalten Nutzung bzw. Gewerk und Kürzel. Das Kürzel ist die LoD2-ID ohne gemeinsamen Präfix,
mit Groß-/Kleinschreibung (in Namen groß geschrieben). „Innen“: in den begehbaren Häusern der Wegpunkt im Raum (`…_INNEN`, über `…_TUER` in der Türöffnung und `…_VOR` davor mit dem Routinen-Wegpunkt verbunden) und die Freepoints im Raum.

| Kürzel | Nutzung | Name | Bewohner | Routinen-Wegpunkt | Freepoints | Mobs | Innen |
|---|---|---|---|---|---|---|---|
| ZhE | Gasthaus | Zur krummen Gans | 5 | `WP_LEO_GASTHAUS_ZHE` | `FP_SIT_LEO_GASTHAUS_ZHE_01`, `FP_SIT_LEO_GASTHAUS_ZHE_02`, `FP_SIT_LEO_GASTHAUS_ZHE_03`, `FP_DRINK_LEO_GASTHAUS_ZHE_01`, `FP_DRINK_LEO_GASTHAUS_ZHE_02`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_01`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_02`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_03`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_04` | `MOB_LEO_GASTHAUS_ZHE_BENCH_1` | `FP_CAMPFIRE_LEO_GASTHAUS_ZHE_INNEN_01`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_INNEN_01`, `FP_SMALLTALK_LEO_GASTHAUS_ZHE_INNEN_02`, `FP_STAND_LEO_GASTHAUS_ZHE_INNEN_01`, `WP_LEO_GASTHAUS_ZHE_INNEN` |
| Zja | Gasthaus | Zum hölzernen Krug | 3 | `WP_LEO_GASTHAUS_ZJA` | `FP_SIT_LEO_GASTHAUS_ZJA_01`, `FP_SIT_LEO_GASTHAUS_ZJA_02`, `FP_SIT_LEO_GASTHAUS_ZJA_03`, `FP_DRINK_LEO_GASTHAUS_ZJA_01`, `FP_DRINK_LEO_GASTHAUS_ZJA_02`, `FP_SMALLTALK_LEO_GASTHAUS_ZJA_01`, `FP_SMALLTALK_LEO_GASTHAUS_ZJA_02`, `FP_SMALLTALK_LEO_GASTHAUS_ZJA_03`, `FP_SMALLTALK_LEO_GASTHAUS_ZJA_04` | `MOB_LEO_GASTHAUS_ZJA_BENCH_1` |  |
| ZjW | Bäcker | Backstube am Markt | 4 | `WP_LEO_BAECKER_ZJW` | `FP_SWEEP_LEO_BAECKER_ZJW_01` | – |  |
| ZnQ | Metzger | Fleischbank an der Gasse | 4 | `WP_LEO_METZGER_ZNQ` | `FP_CHOP_LEO_METZGER_ZNQ_01`, `FP_STAND_LEO_METZGER_ZNQ_01` | – |  |
| Zmr | Händler (goldschmied) | Goldschmied | 3 | `WP_LEO_GOLDSCHMIED_ZMR` | `FP_STAND_LEO_GOLDSCHMIED_ZMR_01`, `FP_STAND_LEO_GOLDSCHMIED_ZMR_02` | `MOB_LEO_GOLDSCHMIED_ZMR_CHEST_1` |  |
| ZmA | Händler (tuchhaendler) | Tuchhandel | 3 | `WP_LEO_TUCHHAENDLER_ZMA` | `FP_STAND_LEO_TUCHHAENDLER_ZMA_01`, `FP_STAND_LEO_TUCHHAENDLER_ZMA_02` | – |  |
| Zkx | Händler (kraemer) | Krämerladen | 3 | `WP_LEO_KRAEMER_ZKX` | `FP_STAND_LEO_KRAEMER_ZKX_01`, `FP_STAND_LEO_KRAEMER_ZKX_02` | `MOB_LEO_KRAEMER_ZKX_CHEST_1` | `FP_STAND_LEO_KRAEMER_ZKX_INNEN_01`, `WP_LEO_KRAEMER_ZKX_INNEN` |
| Zkv | Händler (gewandschneider) | Gewandschneiderei | 2 | `WP_LEO_GEWANDSCHNEIDER_ZKV` | `FP_STAND_LEO_GEWANDSCHNEIDER_ZKV_01`, `FP_STAND_LEO_GEWANDSCHNEIDER_ZKV_02` | `MOB_LEO_GEWANDSCHNEIDER_ZKV_CHEST_1` |  |
| ZkY | Wohnhaus | – | 5 | `WP_LEO_WOHNHAUS_ZKY` | `FP_LEAN_LEO_WOHNHAUS_ZKY_01` | – |  |
| ZnP | Schmiede | Schmiede am Oberen Tor | 3 | `WP_LEO_SCHMIEDE_ZNP` | `FP_REPAIR_LEO_SCHMIEDE_ZNP_01`, `FP_CHOP_LEO_SCHMIEDE_ZNP_01` | `MOB_LEO_SCHMIEDE_ZNP_ANVIL_1` | `FP_CAMPFIRE_LEO_SCHMIEDE_ZNP_INNEN_01`, `FP_LEAN_LEO_SCHMIEDE_ZNP_INNEN_01`, `WP_LEO_SCHMIEDE_ZNP_INNEN` |
| ZkB-H4 | Werkstatt (schuster) | Schusterei | 3 | `WP_LEO_SCHUSTER_ZKB_H4` | `FP_REPAIR_LEO_SCHUSTER_ZKB_H4_01`, `FP_SWEEP_LEO_SCHUSTER_ZKB_H4_01` | – |  |
| ZjR | Werkstatt (schreiner) | Schreinerei | 2 | `WP_LEO_SCHREINER_ZJR` | `FP_REPAIR_LEO_SCHREINER_ZJR_01`, `FP_SWEEP_LEO_SCHREINER_ZJR_01` | `MOB_LEO_SCHREINER_ZJR_BENCH_1` |  |
| ZkT | Werkstatt (toepfer) | Töpferei | 8 | `WP_LEO_TOEPFER_ZKT` | `FP_REPAIR_LEO_TOEPFER_ZKT_01`, `FP_SWEEP_LEO_TOEPFER_ZKT_01` | `MOB_LEO_TOEPFER_ZKT_BENCH_1` |  |
| Zhk | Bader | Badstube | 8 | `WP_LEO_BADER_ZHK` | `FP_STAND_LEO_BADER_ZHK_01`, `FP_SIT_LEO_BADER_ZHK_01` | – |  |
| ZkQ-T1 | Kräuterhändler | Kräuterkammer | 2 | `WP_LEO_KRAEUTER_ZKQ_T1` | `FP_STAND_LEO_KRAEUTER_ZKQ_T1_01` | – |  |
| ZkP | Amtshaus | Rathaus | 8 | `WP_LEO_AMTSHAUS_ZKP` | `FP_STAND_LEO_AMTSHAUS_ZKP_01`, `FP_STAND_LEO_AMTSHAUS_ZKP_02`, `FP_SMALLTALK_LEO_AMTSHAUS_ZKP_01`, `FP_SMALLTALK_LEO_AMTSHAUS_ZKP_02`, `FP_SMALLTALK_LEO_AMTSHAUS_ZKP_03`, `FP_SMALLTALK_LEO_AMTSHAUS_ZKP_04` | – |  |
| Zh5 | Wache | Wachstube am Unteren Tor | 8 | `WP_LEO_WACHE_ZH5` | `FP_STAND_LEO_WACHE_ZH5_01`, `FP_STAND_LEO_WACHE_ZH5_02`, `FP_LEAN_LEO_WACHE_ZH5_01` | – |  |
| Zj7-T1 | Wache | Wachstube am Oberen Tor | 3 | `WP_LEO_WACHE_ZJ7_T1` | `FP_STAND_LEO_WACHE_ZJ7_T1_01`, `FP_STAND_LEO_WACHE_ZJ7_T1_02`, `FP_LEAN_LEO_WACHE_ZJ7_T1_01` | – |  |
| ZiU | Pfarrhaus | Pfarrhaus | 3 | `WP_LEO_PFARRHAUS_ZIU` | `FP_SWEEP_LEO_PFARRHAUS_ZIU_01` | – |  |
| ZkL | Bauernhof | Scheunenhof | 1 | `WP_LEO_BAUER_ZKL` | `FP_CHOP_LEO_BAUER_ZKL_01`, `FP_HARVEST_LEO_BAUER_ZKL_01` | – |  |
| Ziz | Bauernhof | Hof an der Mauer | 2 | `WP_LEO_BAUER_ZIZ` | `FP_CHOP_LEO_BAUER_ZIZ_01`, `FP_HARVEST_LEO_BAUER_ZIZ_01` | – |  |
| Zl2-T2 | Wohnhaus | – | 4 | `WP_LEO_WOHNHAUS_ZL2_T2` | `FP_LEAN_LEO_WOHNHAUS_ZL2_T2_01` | – | `FP_CAMPFIRE_LEO_WOHNHAUS_ZL2_T2_INNEN_01`, `FP_LEAN_LEO_WOHNHAUS_ZL2_T2_INNEN_01`, `WP_LEO_WOHNHAUS_ZL2_T2_INNEN` |
| Zl2-T1 | Wohnhaus | – | 3 | `WP_LEO_WOHNHAUS_ZL2_T1` | `FP_LEAN_LEO_WOHNHAUS_ZL2_T1_01` | – |  |
| ZjC | Wohnhaus | – | 1 | `WP_LEO_WOHNHAUS_ZJC` | `FP_LEAN_LEO_WOHNHAUS_ZJC_01` | – |  |
| ZjV | Wohnhaus | – | 8 | `WP_LEO_WOHNHAUS_ZJV` | `FP_LEAN_LEO_WOHNHAUS_ZJV_01` | – | `FP_CAMPFIRE_LEO_WOHNHAUS_ZJV_INNEN_01`, `FP_LEAN_LEO_WOHNHAUS_ZJV_INNEN_01`, `WP_LEO_WOHNHAUS_ZJV_INNEN` |
| ZhJ | Wohnhaus | – | 4 | `WP_LEO_WOHNHAUS_ZHJ` | `FP_LEAN_LEO_WOHNHAUS_ZHJ_01` | – |  |
| ZjU | Wohnhaus | – | 6 | `WP_LEO_WOHNHAUS_ZJU` | `FP_LEAN_LEO_WOHNHAUS_ZJU_01` | – |  |
| Zj0 | Wohnhaus | – | 2 | `WP_LEO_WOHNHAUS_ZJ0` | `FP_LEAN_LEO_WOHNHAUS_ZJ0_01` | – |  |
| Zjr | Wohnhaus | – | 2 | `WP_LEO_WOHNHAUS_ZJR` | `FP_LEAN_LEO_WOHNHAUS_ZJR_01` | – |  |
| ZhK | Wohnhaus | – | 5 | `WP_LEO_WOHNHAUS_ZHK` | `FP_LEAN_LEO_WOHNHAUS_ZHK_01` | – |  |

Nicht platziert (zu wenig Platz vor dem Haus bzw. ein Weg zu nah): je eine zweite Bank an beiden Gasthäusern,
die Truhe des Tuchhändlers, die Bank der Schusterei, der zweite Freepoint (STAND) des Bäckers (seit das Gelände
unter dem Raum des Nachbarhauses ZjV abgesenkt ist, W7 C1). Der Bericht steht in `generated/uses_places.json` (`failed`).
