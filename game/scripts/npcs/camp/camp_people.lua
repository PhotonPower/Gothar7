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
    gender = "f",
    name = "Bäuerin",
    guild = "farmer",
    level = 2,
    attributes = { str = 10, hp = 60 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/peasant_woman.figure.toml",
    inventory = { it_apple = 5, it_bread = 2, it_letter_farm = 1 },
    pickpocket_dex = 15, -- leicht zu bestehlen
    routine = "rtn_farmer_woman",
}

Npc "npc_woodcutter" {
    name = "Holzfäller",
    guild = "farmer",
    level = 4,
    attributes = { str = 25, hp = 90 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/laborer.figure.toml",
    equipment = { "it_club" },
    routine = "rtn_woodcutter",
}

Npc "npc_old_man" {
    name = "Alter Mann",
    guild = "outcast",
    level = 1,
    attributes = { str = 8, hp = 50 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/old_man.figure.toml",
    routine = "rtn_old_man",
    -- Er handelt mit allerlei Kram (M10): seine Waren und Gulden zum Bezahlen.
    inventory = { it_gulden = 120, it_apple = 6, it_bread = 4, it_lockpick = 3, it_potion_heal_small = 2,
                  it_ring_family = 1 }, -- der Ring der Bäuerin (Geschichte M10)
    pickpocket_dex = 20,
    pickpocket_item = "it_ring_family", -- wer ihn bestiehlt, erwischt den Ring
}

-- Der Kräuterhexer (M12, Entscheidung A des Projektinhabers): zaubert im Kampf Feuerpfeile auf Abstand und heilt
-- sich (ai/combat.lua, Feld spells). Figur vorerst die des alten Mannes (Platzhalter).
Npc "npc_camp_hexer" {
    name = "Kräuterhexer",
    guild = "outcast",
    level = 8,
    attributes = { str = 10, hp = 80, mana = 60 },
    talents = { magic_circle = 1 },
    spells = { "spl_heal", "spl_firebolt" },
    figure = "characters/figures/old_man.figure.toml",
    inventory = { it_potion_mana_small = 2 },
    routine = "rtn_camp_hexer",
}
