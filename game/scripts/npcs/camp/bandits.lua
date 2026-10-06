-- Wegelagerer (M11, Platzhalter für Kampftests und das DoD-Szenario; eigene Namen): nicht im Lager eingesetzt,
-- Skripte bzw. Tests setzen sie mit insert_npc ein. Der Anführer ist der „starke Gegner“ des M11-Meilensteins.
Npc "npc_bandit" {
    name = "Wegelagerer",
    guild = "outcast",
    level = 4,
    attributes = { str = 20, dex = 15, hp = 80 },
    talents = { melee_1h = 1 },
    equipment = { "it_club" },
}

Npc "npc_bandit_leader" {
    name = "Rotbart, Anführer der Wegelagerer",
    guild = "outcast",
    level = 12,
    attributes = { str = 45, dex = 25, hp = 260 },
    talents = { melee_2h = 2 },
    protection = { edge = 15, blunt = 10 },
    equipment = { "it_sword_2h" }, -- Zweihänder (figuren baut das Modell)
}
