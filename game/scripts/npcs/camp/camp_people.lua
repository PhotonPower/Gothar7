-- Leute im Lager (Beispielinhalt M7). `figure` ist ein Figuren-Manifest (characters/figures/*.figure.toml);
-- `routine` ist der Tagesablauf (routines/, M9), Dialoge folgen mit M12.
Npc "npc_gate_guard" {
    name = "Torwache",
    guild = "guard",
    level = 8,
    figure = "characters/figures/guard.figure.toml",
    attributes = { str = 30, dex = 20, hp = 120 },
    equipment = { "it_sword_old" },
    routine = "rtn_gate_guard_start",
}

Npc "npc_farmer_woman" {
    name = "Bäuerin",
    guild = "farmer",
    level = 2,
    figure = "characters/figures/peasant_woman.figure.toml",
    inventory = { it_apple = 5, it_bread = 2, it_letter_farm = 1 },
    pickpocket_dex = 15, -- leicht zu bestehlen
    routine = "rtn_farmer_woman",
}

Npc "npc_woodcutter" {
    name = "Holzfäller",
    guild = "farmer",
    level = 4,
    figure = "characters/figures/laborer.figure.toml",
    equipment = { "it_club" },
    routine = "rtn_woodcutter",
}

Npc "npc_old_man" {
    name = "Alter Mann",
    guild = "outcast",
    level = 1,
    figure = "characters/figures/old_man.figure.toml",
    routine = "rtn_old_man",
    -- Er handelt mit allerlei Kram (M10): seine Waren und Gulden zum Bezahlen.
    inventory = { it_gulden = 120, it_apple = 6, it_bread = 4, it_lockpick = 3, it_potion_heal_small = 2 },
}
