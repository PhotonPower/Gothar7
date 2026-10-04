-- Tagesabläufe der Leonberger („Leonberg lebt“). Wegpunkte und Freepoints: welts Routinen-Orte. Nachts stehen sie
-- an ihrem Haus (zs_lean), bis NPCs mit M11 Betten benutzen.

Routine "rtn_leo_smith" {
    { from = "07:00", to = "12:00", state = "zs_repair", at = "wp_leo_schmiede_znp" },
    { from = "12:00", to = "13:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "13:00", to = "19:00", state = "zs_chop_wood", at = "wp_leo_schmiede_znp" },
    { from = "19:00", to = "22:00", state = "zs_sit", at = "wp_leo_gasthaus_zhe" },
    { from = "22:00", to = "07:00", state = "zs_lean", at = "wp_leo_wohnhaus_zky" },
}

Routine "rtn_leo_innkeeper" {
    { from = "08:00", to = "12:00", state = "zs_stand", at = "wp_leo_gasthaus_zhe" },
    { from = "12:00", to = "01:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "01:00", to = "08:00", state = "zs_stand", at = "wp_leo_gasthaus_zhe" },
}

Routine "rtn_leo_baker" {
    { from = "05:00", to = "09:00", state = "zs_sweep", at = "wp_leo_baecker_zjw" },
    { from = "09:00", to = "18:00", state = "zs_stand_shop", at = "wp_leo_baecker_zjw" },
    { from = "18:00", to = "05:00", state = "zs_lean", at = "wp_leo_wohnhaus_zjv" },
}

Routine "rtn_leo_market" {
    { from = "08:00", to = "17:00", state = "zs_stand_shop", at = "wp_leo_kraemer_zkx" },
    { from = "17:00", to = "21:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "08:00", state = "zs_lean", at = "wp_leo_wohnhaus_zhk" },
}

Routine "rtn_leo_guard_lower" { -- Tagschicht am Unteren Tor
    { from = "06:00", to = "22:00", state = "zs_stand_guarding", at = "fp_stand_leo_wache_zh5_01" },
    { from = "22:00", to = "06:00", state = "zs_lean", at = "wp_leo_wache_zh5" },
}

Routine "rtn_leo_guard_upper" { -- Nachtschicht am Oberen Tor
    { from = "21:00", to = "07:00", state = "zs_stand_guarding", at = "fp_stand_leo_wache_zj7_t1_01" },
    { from = "07:00", to = "21:00", state = "zs_lean", at = "wp_leo_wache_zj7_t1" },
}

Routine "rtn_leo_farmer" {
    { from = "06:00", to = "11:00", state = "zs_harvest", at = "wp_leo_bauer_zkl" },
    { from = "11:00", to = "17:00", state = "zs_chop_wood", at = "wp_leo_bauer_zkl" },
    { from = "17:00", to = "21:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "06:00", state = "zs_lean", at = "wp_leo_wohnhaus_zl2_t2" },
}

Routine "rtn_leo_citizen" {
    { from = "08:00", to = "12:00", state = "zs_smalltalk", at = "wp_leo_amtshaus_zkp" },
    { from = "12:00", to = "16:00", state = "zs_stand", at = "wp_leo_kraemer_zkx" },
    { from = "16:00", to = "20:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "20:00", to = "08:00", state = "zs_lean", at = "wp_leo_wohnhaus_zju" },
}
