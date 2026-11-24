-- Tagesabläufe der Schlossbewohner (F1). Betten: Gemach (Vogt), Wachstube (2, die Wachen schlafen versetzt),
-- Gesindestube (2: Köchin, Schreiber). Lesen und Kochen haben noch keine eigenen Zustände (Clips fehlen): der
-- Schreiber sitzt am Tisch der Schreibstube, die Köchin steht am Herd (FP_COOK).

Routine "rtn_leo_vogt" {
    { from = "07:00", to = "08:00", state = "zs_pray", at = "fp_pray_leo_schloss_kapelle_01" },
    { from = "08:00", to = "12:00", state = "zs_sit_table", at = "wp_leo_schloss_schreibstube" },
    { from = "12:00", to = "13:00", state = "zs_sit_table", at = "wp_leo_schloss_festsaal" },
    { from = "13:00", to = "17:00", state = "zs_stand", at = "fp_stand_leo_schloss_halle_02" },
    { from = "17:00", to = "20:00", state = "zs_sit_table", at = "wp_leo_schloss_bibliothek" },
    { from = "20:00", to = "22:00", state = "zs_smalltalk", at = "fp_smalltalk_leo_schloss_festsaal_01" },
    { from = "22:00", to = "07:00", state = "zs_sleep", at = "wp_leo_schloss_gemach" },
}

Routine "rtn_leo_castle_guard_day" { -- am Portal in der Halle, abends in der Wachstube
    { from = "06:00", to = "12:00", state = "zs_stand_guarding", at = "fp_stand_leo_schloss_halle_01" },
    { from = "12:00", to = "13:00", state = "zs_sit_table", at = "wp_leo_schloss_wachstube" },
    { from = "13:00", to = "19:00", state = "zs_stand_guarding", at = "fp_stand_leo_schloss_halle_01" },
    { from = "19:00", to = "22:00", state = "zs_sit", at = "wp_leo_schloss_wachstube" },
    { from = "22:00", to = "06:00", state = "zs_sleep", at = "wp_leo_schloss_wachstube" },
}

Routine "rtn_leo_castle_guard_night" { -- Nachtwache in der Halle; schläft am Tag
    { from = "19:00", to = "06:00", state = "zs_stand_guarding", at = "fp_stand_leo_schloss_halle_01" },
    { from = "06:00", to = "08:00", state = "zs_sit_table", at = "wp_leo_schloss_kueche" },
    { from = "08:00", to = "16:00", state = "zs_sleep", at = "wp_leo_schloss_wachstube" },
    { from = "16:00", to = "19:00", state = "zs_sit", at = "wp_leo_schloss_wachstube" },
}

Routine "rtn_leo_castle_cook" {
    { from = "05:00", to = "12:00", state = "zs_stand", at = "fp_cook_leo_schloss_kueche_01" },
    { from = "12:00", to = "13:00", state = "zs_sit_table", at = "wp_leo_schloss_kueche" },
    { from = "13:00", to = "15:00", state = "zs_sweep", at = "wp_leo_schloss_speisekammer" },
    { from = "15:00", to = "19:00", state = "zs_stand", at = "fp_cook_leo_schloss_kueche_01" },
    { from = "19:00", to = "21:00", state = "zs_smalltalk", at = "wp_leo_schloss_gesinde" },
    { from = "21:00", to = "05:00", state = "zs_sleep", at = "wp_leo_schloss_gesinde" },
}

Routine "rtn_leo_castle_scribe" {
    { from = "07:00", to = "12:00", state = "zs_sit_table", at = "wp_leo_schloss_schreibstube" },
    { from = "12:00", to = "13:00", state = "zs_sit_table", at = "wp_leo_schloss_kueche" },
    { from = "13:00", to = "18:00", state = "zs_sit_table", at = "wp_leo_schloss_bibliothek" },
    { from = "18:00", to = "21:00", state = "zs_smalltalk", at = "fp_smalltalk_leo_schloss_halle_01" },
    { from = "21:00", to = "07:00", state = "zs_sleep", at = "wp_leo_schloss_gesinde" },
}
