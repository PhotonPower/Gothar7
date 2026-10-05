-- Die Bewohner Leonbergs („Leonberg lebt“, Platzhalter; eigene Namen nach Entscheidung des Projektinhabers L1).
-- Orte nach welts docs/design/leonberg-routinen-orte.md; Figuren von figuren (fehlt eine noch, steht die
-- Standardfigur ein). Beim Laden von Leonberg setzt leonberg_people() sie ein (L2, startup.lua).

Npc "npc_leo_smith" {
    name = "Ulrich der Schmied",
    guild = "citizen",
    level = 8,
    figure = "characters/figures/smith.figure.toml",
    attributes = { str = 40, hp = 140 },
    routine = "rtn_leo_smith",
}

Npc "npc_leo_innkeeper" {
    name = "Bertold, Wirt der Krummen Gans",
    guild = "citizen",
    level = 4,
    figure = "characters/figures/innkeeper.figure.toml",
    routine = "rtn_leo_innkeeper",
}

Npc "npc_leo_baker" {
    name = "Gertrud die Bäckerin",
    guild = "citizen",
    level = 3,
    figure_set = "citizen_f",
    inventory = { it_gulden = 80, it_bread = 12, it_apple = 6 },
    routine = "rtn_leo_baker",
}

Npc "npc_leo_market" {
    name = "Agnes vom Krämerladen",
    guild = "citizen",
    level = 3,
    figure = "characters/figures/market_woman.figure.toml",
    inventory = { it_gulden = 100, it_lockpick = 2, it_mug = 3, it_broom = 2, it_potion_heal_small = 3, it_apple = 5 },
    routine = "rtn_leo_market",
}

Npc "npc_leo_guard_lower" {
    name = "Kaspar, Wache am Unteren Tor",
    guild = "guard",
    level = 10,
    figure = "characters/figures/gate_guard.figure.toml",
    attributes = { str = 35, dex = 25, hp = 160 },
    equipment = { "it_sword_old" },
    routine = "rtn_leo_guard_lower",
}

Npc "npc_leo_guard_upper" {
    name = "Lienhard, Hauptmann am Oberen Tor",
    guild = "guard",
    level = 14,
    figure = "characters/figures/guard_captain.figure.toml",
    attributes = { str = 45, dex = 30, hp = 200 },
    equipment = { "it_sword_old" },
    routine = "rtn_leo_guard_upper",
}

Npc "npc_leo_farmer" {
    name = "Veit vom Scheunenhof",
    guild = "farmer",
    level = 3,
    figure = "characters/figures/farmer.figure.toml",
    routine = "rtn_leo_farmer",
}

Npc "npc_leo_citizen" {
    name = "Matthis der Ratsdiener",
    guild = "citizen",
    level = 2,
    figure_set = "citizen",
    routine = "rtn_leo_citizen",
}
