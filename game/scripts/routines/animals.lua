-- Tagesabläufe der Tiere im Testlager (M9 Teil D): Wölfe nachts, Keiler und Laufvogel tags.
Routine "rtn_mon_wolf" {
    { from = "20:00", to = "07:00", state = "zs_mm_roam", at = "wp_wolf_den" },
    { from = "07:00", to = "20:00", state = "zs_mm_sleep", at = "wp_wolf_den" },
}

Routine "rtn_mon_keiler" {
    { from = "06:00", to = "21:00", state = "zs_mm_roam", at = "wp_meadow_north" },
    { from = "21:00", to = "06:00", state = "zs_mm_sleep", at = "wp_meadow_north" },
}

Routine "rtn_mon_laufvogel" {
    { from = "06:00", to = "21:00", state = "zs_mm_roam", at = "wp_meadow_south" },
    { from = "21:00", to = "06:00", state = "zs_mm_sleep", at = "wp_meadow_south" },
}
