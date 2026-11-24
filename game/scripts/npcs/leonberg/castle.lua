-- Die Bewohner des Schlosses (Entscheidung des Projektinhabers F1, docs/design/schloss-dungeon.md): der Vogt (mit
-- Dialogen), zwei Wachen, die Köchin und der Schreiber. Orte: welts Schloss-Innenraum (#447). Platzhalterwerte;
-- eingesetzt mit den übrigen Leonbergern (leonberg_people, startup.lua).

Npc "npc_leo_vogt" {
    name = "Reinhard, Vogt auf dem Schloss",
    guild = "citizen",
    level = 12,
    figure = "characters/figures/merchant_m_1.figure.toml",
    attributes = { str = 25, hp = 120 },
    inventory = { it_gulden = 200 },
    routine = "rtn_leo_vogt",
}

Npc "npc_leo_castle_guard_day" {
    name = "Jörg, Schlosswache",
    guild = "guard",
    level = 10,
    figure = "characters/figures/guard_m_1.figure.toml",
    attributes = { str = 35, dex = 25, hp = 160 },
    equipment = { "it_sword_old" },
    routine = "rtn_leo_castle_guard_day",
}

Npc "npc_leo_castle_guard_night" {
    name = "Veit, Schlosswache",
    guild = "guard",
    level = 10,
    figure = "characters/figures/guard_m_2.figure.toml",
    attributes = { str = 35, dex = 25, hp = 160 },
    equipment = { "it_sword_old" },
    routine = "rtn_leo_castle_guard_night",
}

Npc "npc_leo_castle_cook" {
    gender = "f",
    voice = "craftsman",
    name = "Margarete, Köchin auf dem Schloss",
    guild = "citizen",
    level = 3,
    figure = "characters/figures/craftsman_f_1.figure.toml",
    attributes = { str = 14, hp = 60 },
    inventory = { it_bread = 4, it_apple = 4 },
    routine = "rtn_leo_castle_cook",
}

Npc "npc_leo_castle_scribe" {
    name = "Lukas der Schreiber",
    guild = "citizen",
    level = 2,
    figure = "characters/figures/citizen_m_3.figure.toml",
    attributes = { str = 8, hp = 50 },
    routine = "rtn_leo_castle_scribe",
}
