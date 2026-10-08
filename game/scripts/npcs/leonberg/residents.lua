-- Die Bewohner Leonbergs („Leonberg lebt“, Platzhalter; eigene Namen nach Entscheidung des Projektinhabers L1).
-- Orte nach welts docs/design/leonberg-routinen-orte.md; Figuren von figuren (fehlt eine noch, steht die
-- Standardfigur ein). Beim Laden von Leonberg setzt leonberg_people() sie ein (L2, startup.lua).

Npc "npc_leo_smith" {
    voice = "craftsman",
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
    attributes = { str = 20, hp = 80 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/innkeeper.figure.toml",
    routine = "rtn_leo_innkeeper",
}

Npc "npc_leo_baker" {
    gender = "f",
    voice = "craftsman",
    name = "Gertrud die Bäckerin",
    guild = "citizen",
    level = 3,
    attributes = { str = 12, hp = 60 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/baker_wife.figure.toml",
    inventory = { it_gulden = 80, it_bread = 12, it_pretzel = 10, it_apple = 6 },
    routine = "rtn_leo_baker",
}

Npc "npc_leo_market" {
    gender = "f",
    name = "Agnes vom Krämerladen",
    guild = "citizen",
    level = 3,
    attributes = { str = 10, hp = 60 }, -- Kampf (M11): Platzhalterwerte
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
    attributes = { str = 25, hp = 90 }, -- Kampf (M11): Platzhalterwerte
    figure = "characters/figures/farmer.figure.toml",
    routine = "rtn_leo_farmer",
}

Npc "npc_leo_citizen" {
    name = "Matthis der Ratsdiener",
    guild = "citizen",
    level = 2,
    attributes = { str = 12, hp = 70 }, -- Kampf (M11): Platzhalterwerte
    figure_set = "citizen",
    routine = "rtn_leo_citizen",
}

-- Neue Bewohner (welt Phase 1: Handwerkerhäuser; figurens Rollen-Figuren). Was sie verkaufen, steht in ihrem Inventar.
Npc "npc_leo_baker_husband" {
    voice = "craftsman",
    name = "Konrad der Bäcker",
    guild = "citizen",
    level = 4,
    attributes = { str = 22, hp = 80 },
    figure = "characters/figures/baker.figure.toml",
    inventory = { it_gulden = 60, it_bread = 6 },
    routine = "rtn_leo_baker_husband",
}

Npc "npc_leo_butcher" {
    voice = "craftsman",
    name = "Hans der Metzger",
    guild = "citizen",
    level = 6,
    attributes = { str = 35, hp = 120 },
    figure = "characters/figures/butcher.figure.toml",
    inventory = { it_gulden = 120, it_ham = 6, it_sausage = 12 },
    routine = "rtn_leo_butcher",
}

Npc "npc_leo_joiner" {
    voice = "craftsman",
    name = "Lorenz der Schreiner",
    guild = "citizen",
    level = 4,
    attributes = { str = 24, hp = 90 },
    figure = "characters/figures/joiner.figure.toml",
    inventory = { it_gulden = 70, it_saw = 1, it_hammer = 1 },
    routine = "rtn_leo_joiner",
}

Npc "npc_leo_potter" {
    voice = "craftsman",
    name = "Michel der Töpfer",
    guild = "citizen",
    level = 3,
    attributes = { str = 18, hp = 70 },
    figure = "characters/figures/potter.figure.toml",
    inventory = { it_gulden = 60, it_jug = 5, it_bowl = 8, it_mug = 6 },
    routine = "rtn_leo_potter",
}

Npc "npc_leo_goldsmith" {
    voice = "craftsman",
    name = "Ruprecht der Goldschmied",
    guild = "citizen",
    level = 5,
    attributes = { str = 14, hp = 70 },
    figure = "characters/figures/goldsmith.figure.toml",
    inventory = { it_gulden = 400, it_ring_silver = 3, it_ring_gold = 2, it_amulet = 1, it_chain_gold = 1 },
    routine = "rtn_leo_goldsmith",
}

Npc "npc_leo_cloth_merchant" {
    gender = "f",
    voice = "citizen",
    name = "Elsbeth die Tuchhändlerin",
    guild = "citizen",
    level = 3,
    attributes = { str = 10, hp = 60 },
    figure = "characters/figures/cloth_merchant.figure.toml",
    inventory = { it_gulden = 200, it_cloth_bolt = 8 },
    routine = "rtn_leo_cloth_merchant",
}

Npc "npc_leo_tailor" {
    voice = "craftsman",
    name = "Jörg der Gewandschneider",
    guild = "citizen",
    level = 3,
    attributes = { str = 14, hp = 65 },
    figure = "characters/figures/tailor.figure.toml",
    inventory = { it_gulden = 90, it_cloth_bolt = 2, it_shears = 1 },
    routine = "rtn_leo_tailor",
}

Npc "npc_leo_herbalist" {
    gender = "f",
    voice = "citizen",
    name = "Mechthild die Kräuterfrau",
    guild = "citizen",
    level = 4,
    attributes = { str = 8, hp = 55, mana = 20 },
    figure = "characters/figures/herbalist.figure.toml",
    inventory = { it_gulden = 150, it_herb_sage = 8, it_herb_chamomile = 8, it_herb_nettle = 10,
                  it_herbs_dried = 4, it_potion_heal_small = 4, it_potion_heal_medium = 2,
                  it_potion_mana_small = 2, it_potion_speed = 1, it_potion_strength = 1 },
    routine = "rtn_leo_herbalist",
}

Npc "npc_leo_bather" {
    voice = "citizen",
    name = "Meister Wendel, der Bader",
    guild = "citizen",
    level = 5,
    attributes = { str = 16, hp = 80 },
    figure = "characters/figures/bather.figure.toml",
    inventory = { it_gulden = 120, it_potion_heal_small = 3 },
    routine = "rtn_leo_bather",
}

Npc "npc_leo_merchant_m" {
    name = "Hieronymus der Kaufmann",
    guild = "citizen",
    level = 4,
    attributes = { str = 12, hp = 70 },
    figure_set = "merchant_m",
    inventory = { it_gulden = 300 },
    routine = "rtn_leo_merchant_m",
}

Npc "npc_leo_merchant_f" {
    gender = "f",
    name = "Margarete, die Kaufmannsfrau",
    guild = "citizen",
    level = 3,
    attributes = { str = 9, hp = 60 },
    figure_set = "merchant_f",
    inventory = { it_gulden = 150, it_ring_gold = 1 },
    routine = "rtn_leo_merchant_f",
}
