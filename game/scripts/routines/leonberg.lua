-- Tagesabläufe der Leonberger („Leonberg lebt“). Wegpunkte und Freepoints: welts Routinen-Orte und Phase 1
-- (begehbare Erdgeschosse). Nachts schlafen sie in ihrem Haus (zs_sleep: eigenes Bett, ein freies in der Nähe oder
-- der Boden); wer dort keinen Schlafplatz hat, lehnt am Haus (zs_lean).

Routine "rtn_leo_smith" {
    { from = "07:00", to = "12:00", state = "zs_repair", at = "wp_leo_schmiede_znp" },
    { from = "12:00", to = "13:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "13:00", to = "19:00", state = "zs_chop_wood", at = "wp_leo_schmiede_znp" },
    { from = "19:00", to = "22:00", state = "zs_sit", at = "wp_leo_gasthaus_zhe" },
    { from = "22:00", to = "07:00", state = "zs_sleep", at = "wp_leo_schmiede_znp_oben_kammer" },
}

Routine "rtn_leo_innkeeper" {
    { from = "08:00", to = "12:00", state = "zs_stand", at = "wp_leo_gasthaus_zhe" },
    { from = "12:00", to = "01:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "01:00", to = "08:00", state = "zs_sleep", at = "wp_leo_gasthaus_zhe_oben_kammer_2" },
}

Routine "rtn_leo_baker" { -- Phase 1: sie arbeitet drinnen
    { from = "05:00", to = "09:00", state = "zs_sweep", at = "wp_leo_baecker_zjw_innen" },
    { from = "09:00", to = "18:00", state = "zs_stand_shop", at = "wp_leo_baecker_zjw_innen" },
    { from = "18:00", to = "21:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "05:00", state = "zs_sleep", at = "wp_leo_wohnhaus_zjv_oben_kammer" },
}

Routine "rtn_leo_market" { -- Phase 1: sie steht im Laden
    { from = "08:00", to = "17:00", state = "zs_stand_shop", at = "wp_leo_kraemer_zkx_innen" },
    { from = "17:00", to = "21:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "08:00", state = "zs_sleep", at = "wp_leo_kraemer_zkx_oben_kammer" },
}

Routine "rtn_leo_guard_lower" { -- Tagschicht in der Wachstube am Unteren Tor (Phase 1: drinnen)
    { from = "06:00", to = "12:00", state = "zs_stand_guarding", at = "fp_stand_leo_wache_zh5_kammer_01" },
    { from = "12:00", to = "13:00", state = "zs_sit", at = "wp_leo_wache_zh5_kammer" },
    { from = "13:00", to = "19:00", state = "zs_stand_guarding", at = "fp_stand_leo_wache_zh5_kammer_01" },
    { from = "19:00", to = "22:00", state = "zs_campfire", at = "wp_leo_wache_zh5_innen" },
    { from = "22:00", to = "06:00", state = "zs_sleep", at = "wp_leo_wache_zh5_kammer" },
}

Routine "rtn_leo_guard_upper" { -- Nachtschicht am Oberen Tor
    { from = "21:00", to = "07:00", state = "zs_stand_guarding", at = "fp_stand_leo_wache_zj7_t1_01" },
    { from = "07:00", to = "21:00", state = "zs_lean", at = "wp_leo_wache_zj7_t1" },
}

Routine "rtn_leo_farmer" {
    { from = "06:00", to = "11:00", state = "zs_harvest", at = "wp_leo_bauer_zkl" },
    { from = "11:00", to = "17:00", state = "zs_chop_wood", at = "wp_leo_bauer_zkl" },
    { from = "17:00", to = "21:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "06:00", state = "zs_sleep", at = "wp_leo_wohnhaus_zl2_t2_oben" },
}

Routine "rtn_leo_citizen" {
    { from = "08:00", to = "12:00", state = "zs_smalltalk", at = "wp_leo_amtshaus_zkp" },
    { from = "12:00", to = "16:00", state = "zs_stand", at = "wp_leo_kraemer_zkx" },
    { from = "16:00", to = "20:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "20:00", to = "08:00", state = "zs_lean", at = "wp_leo_wohnhaus_zju" },
}

-- --- welt Phase 1: Handwerker und Händler in ihren Häusern --------------------------------------------------
Routine "rtn_leo_baker_husband" { -- am Ofen ab vier Uhr
    { from = "04:00", to = "13:00", state = "zs_campfire", at = "wp_leo_baecker_zjw_innen" },
    { from = "13:00", to = "17:00", state = "zs_sit", at = "wp_leo_gasthaus_zhe" },
    { from = "17:00", to = "20:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "20:00", to = "04:00", state = "zs_sleep", at = "wp_leo_wohnhaus_zjv_oben_kammer_2" },
}

Routine "rtn_leo_butcher" {
    { from = "05:00", to = "12:00", state = "zs_chop_wood", at = "wp_leo_metzger_znq_innen" },
    { from = "12:00", to = "18:00", state = "zs_stand_shop", at = "wp_leo_metzger_znq_innen" },
    { from = "18:00", to = "22:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "22:00", to = "05:00", state = "zs_sleep", at = "wp_leo_metzger_znq_kammer" },
}

Routine "rtn_leo_joiner" {
    { from = "07:00", to = "12:00", state = "zs_repair", at = "wp_leo_schreiner_zjr_innen" },
    { from = "12:00", to = "13:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "13:00", to = "18:00", state = "zs_repair", at = "wp_leo_schreiner_zjr_innen" },
    { from = "18:00", to = "19:00", state = "zs_sweep", at = "wp_leo_schreiner_zjr_innen" },
    { from = "19:00", to = "07:00", state = "zs_sleep", at = "wp_leo_schreiner_zjr_innen" },
}

Routine "rtn_leo_potter" {
    { from = "07:00", to = "14:00", state = "zs_repair", at = "wp_leo_toepfer_zkt_innen" },
    { from = "14:00", to = "18:00", state = "zs_stand_shop", at = "wp_leo_toepfer_zkt_innen" },
    { from = "18:00", to = "21:00", state = "zs_campfire", at = "wp_leo_toepfer_zkt_innen" }, -- am Brennofen
    { from = "21:00", to = "07:00", state = "zs_sleep", at = "wp_leo_toepfer_zkt_kammer" },
}

Routine "rtn_leo_goldsmith" {
    { from = "09:00", to = "17:00", state = "zs_stand_shop", at = "wp_leo_goldschmied_zmr_innen" },
    { from = "17:00", to = "20:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "20:00", to = "09:00", state = "zs_sleep", at = "wp_leo_goldschmied_zmr_kammer" },
}

Routine "rtn_leo_cloth_merchant" {
    { from = "08:00", to = "17:00", state = "zs_stand_shop", at = "wp_leo_tuchhaendler_zma_innen" },
    { from = "17:00", to = "20:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "20:00", to = "08:00", state = "zs_sleep", at = "wp_leo_tuchhaendler_zma_oben_kammer" },
}

Routine "rtn_leo_tailor" {
    { from = "08:00", to = "16:00", state = "zs_repair", at = "wp_leo_gewandschneider_zkv_innen" }, -- nähen
    { from = "16:00", to = "19:00", state = "zs_stand_shop", at = "wp_leo_gewandschneider_zkv_innen" },
    { from = "19:00", to = "08:00", state = "zs_sleep", at = "wp_leo_gewandschneider_zkv_oben_kammer" },
}

Routine "rtn_leo_herbalist" {
    { from = "06:00", to = "11:00", state = "zs_repair", at = "wp_leo_kraeuter_zkq_t1_innen" }, -- Kräuter binden
    { from = "11:00", to = "18:00", state = "zs_stand_shop", at = "wp_leo_kraeuter_zkq_t1_innen" },
    { from = "18:00", to = "21:00", state = "zs_campfire", at = "wp_leo_kraeuter_zkq_t1_innen" }, -- am Sud
    { from = "21:00", to = "06:00", state = "zs_sleep", at = "wp_leo_kraeuter_zkq_t1_kammer" },
}

Routine "rtn_leo_bather" { -- der Bader heilt in seiner Badstube
    { from = "07:00", to = "12:00", state = "zs_stand", at = "fp_stand_leo_bader_zhk_innen_01" },
    { from = "12:00", to = "14:00", state = "zs_sit", at = "wp_leo_bader_zhk_innen" },
    { from = "14:00", to = "19:00", state = "zs_stand", at = "fp_stand_leo_bader_zhk_innen_01" },
    { from = "19:00", to = "22:00", state = "zs_campfire", at = "wp_leo_bader_zhk_innen" },
    { from = "22:00", to = "07:00", state = "zs_sleep", at = "wp_leo_bader_zhk_oben_kammer" },
}

Routine "rtn_leo_merchant_m" { -- ein Kaufmann zwischen Markt, Amtshaus, Goldschmied und Gasthaus
    { from = "08:00", to = "11:00", state = "zs_stand", at = "wp_leo_kraemer_zkx" },
    { from = "11:00", to = "14:00", state = "zs_smalltalk", at = "wp_leo_amtshaus_zkp" },
    { from = "14:00", to = "17:00", state = "zs_stand", at = "wp_leo_goldschmied_zmr_vor" },
    { from = "17:00", to = "21:00", state = "zs_drink", at = "wp_leo_gasthaus_zhe" },
    { from = "21:00", to = "08:00", state = "zs_sleep", at = "wp_leo_wohnhaus_zjv_oben_kammer_3" },
}

Routine "rtn_leo_merchant_f" {
    { from = "09:00", to = "12:00", state = "zs_stand", at = "wp_leo_tuchhaendler_zma_vor" },
    { from = "12:00", to = "15:00", state = "zs_stand", at = "wp_leo_kraemer_zkx" },
    { from = "15:00", to = "19:00", state = "zs_smalltalk", at = "wp_leo_gasthaus_zhe" },
    { from = "19:00", to = "09:00", state = "zs_sleep", at = "wp_leo_wohnhaus_zjv_kammer" },
}
