-- Tiere (Beispielinhalt M9 Teil D). Wie Gothics Monster sind es Npcs; `species` wählt Figur und Graph.
Npc "mon_wolf" {
    name = "Wolf",
    guild = "wolf",
    level = 6,
    species = "wolf",
    attributes = { str = 20, dex = 20, hp = 60 },
    routine = "rtn_mon_wolf",
    senses = { angle = 220 }, -- Tiere bemerken fast ringsum
}

Npc "mon_keiler" {
    name = "Keiler",
    guild = "keiler",
    level = 5,
    species = "keiler",
    attributes = { str = 25, hp = 80 },
    routine = "rtn_mon_keiler",
    senses = { angle = 220 },
}

Npc "mon_laufvogel" {
    name = "Laufvogel",
    guild = "laufvogel",
    level = 3,
    species = "laufvogel",
    attributes = { str = 10, dex = 15, hp = 40 },
    routine = "rtn_mon_laufvogel",
    senses = { angle = 220 },
}
