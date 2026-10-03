-- Tagesablauf der Torwache (Beispielinhalt; ausgewertet mit M9).
Routine "rtn_gate_guard_start" {
    { from = "08:00", to = "22:00", state = "zs_stand_guarding", at = "wp_camp_gate" },
    { from = "22:00", to = "08:00", state = "zs_sleep", at = "wp_camp_guard_bed" },
}
