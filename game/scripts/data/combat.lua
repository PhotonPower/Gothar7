-- Kampfwerte (M11, Entscheidungen des Projektinhabers 2026-10-05, wie Gothic 1). Je Talentstufe 0, 1, 2.
Combat = {
    min_damage = 5,                -- K2: Schaden = Waffe + Stärke - Schutz, mindestens das
    crit_chance = { 0, 0.1, 0.2 }, -- K3: kritischer Treffer je Talentstufe
    crit_factor = 2,               -- K3: doppelter Waffenschaden
    combo_hits = { 1, 3, 4 },      -- K4: Talent 0 Einzelschläge, 1 bis 3, 2 bis 4
    attack_speed = { 1, 1, 1.25 }, -- K4: und schneller (Abspielrate)
    parry_seconds = 0.4,           -- K6: die Parade blockt so lange ab ihrem Beginn
    parry_angle = 60,              -- K6: Treffer von vorn bis zu diesem Winkel
    knockout_seconds = 30,         -- K7: bewusstlos, dann steht er auf
    stagger_seconds = 0.5,         -- Taumeln nach einem Treffer (ohne Clip)
    fist_reach = 0.9,              -- Reichweite in m ab der Körpervorderseite
    reach_1h = 1.3,
    reach_2h = 1.7,
    hit_angle = 50,                -- halber Winkel des Schlags vor dem Angreifer
}

-- Fernkampf (M11 Teil E, Entscheidungen R1-R4 wie Gothic 1).
Combat.projectile_speed = 40                -- m/s
Combat.miss_spread = 5                      -- R4: ein Fehlschuss geht so viel Grad daneben
Combat.bow_hit_chance = { 0.3, 0.6, 0.9 }   -- R4: auf das fokussierte Ziel je Talentstufe
Combat.crossbow_hit_chance = { 0.3, 0.6, 0.9 }
Combat.bow_reload = 1.0                     -- s zwischen zwei Schüssen (Nachladen automatisch, R2)
Combat.crossbow_reload = 1.6
Combat.bow_ammo = "it_arrow"
Combat.crossbow_ammo = "it_bolt"
