-- Schmieden (Beispielinhalt M8 Teil C2, Entscheidung Projektinhaber: Beispielrezept ohne Talentpflicht; das
-- Schmiede-Talent kommt später als Inhalt).
Item "it_blank_hot" {
    name = "Glühender Rohling",
    category = "misc",
    value = 5,
}

Item "it_sword_crude" {
    name = "Grobes Schwert",
    category = "melee_1h",
    value = 40,
    damage = { edge = 12 },
    requires = { str = 10 },
}

Recipe "rcp_sword_crude" {
    name = "Grobes Schwert",
    mob = "anvil",
    takes = { it_blank_hot = 1 },
    gives = { it_sword_crude = 1 },
    strikes = 3,
}
