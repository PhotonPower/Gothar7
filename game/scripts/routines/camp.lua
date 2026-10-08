-- Tagesabläufe der Leute im Lager (Beispielinhalt M9). Zeitfenster [from, to), über Mitternacht erlaubt.
Routine "rtn_farmer_woman" {
    { from = "06:00", to = "12:00", state = "zs_sweep", at = "wp_camp_center" },
    { from = "12:00", to = "19:00", state = "zs_campfire", at = "wp_camp_fire" },
    { from = "19:00", to = "23:00", state = "zs_sit_campfire", at = "wp_camp_fire" },
    { from = "23:00", to = "06:00", state = "zs_sleep", at = "wp_camp_west" },
}

Routine "rtn_woodcutter" {
    { from = "07:00", to = "18:00", state = "zs_chop_wood", at = "wp_camp_north" },
    { from = "18:00", to = "22:00", state = "zs_sit_campfire", at = "wp_camp_fire" },
    { from = "22:00", to = "07:00", state = "zs_sleep", at = "wp_camp_north" },
}

Routine "rtn_old_man" {
    { from = "08:00", to = "20:00", state = "zs_stand", at = "wp_camp_south" },
    { from = "20:00", to = "08:00", state = "zs_sleep", at = "wp_camp_south" },
}

Routine "rtn_camp_hexer" {
    { from = "07:00", to = "21:00", state = "zs_stand", at = "wp_camp_west" },
    { from = "21:00", to = "07:00", state = "zs_sleep", at = "wp_camp_west" },
}
