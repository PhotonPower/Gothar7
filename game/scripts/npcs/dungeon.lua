-- Monster des Dungeons (D6, docs/design/monsters.md; Werte Platzhalter bis zur Abstimmung). Sie kommen über
-- Spawn-Marker (Vob-Typ `spawn`) in die Ebenen und haben keinen Tagesablauf: Sie ruhen (zs_dormant) oder streifen um
-- ihren Marker (ai/monsters.lua; im Dungeon gibt es keinen Tag-Nacht-Rhythmus, Frage 14).

Npc "mon_gatterbock" {
    name = "Gatterbock",
    guild = "gatterbock",
    level = 14,
    species = "gatterbock",
    attributes = { str = 60, hp = 220 },
    damage = { blunt = 10 }, -- Steinfäuste: umwerfen
    -- Klingen prallen ab, stumpfe Waffen und Magie wirken (monsters.md)
    protection = { edge = 60, point = 80, blunt = 10, fire = 40, magic = 0, fall = 1000 },
    senses = { angle = 140 },
}

Npc "mon_mauerschlupfer" {
    name = "Mauerschlupfer",
    guild = "mauerschlupfer",
    level = 4,
    species = "mauerschlupfer",
    attributes = { str = 12, dex = 30, hp = 45 },
    damage = { edge = 4 }, -- Biss und Kralle
    senses = { sight = 0, hearing = 2 }, -- blind: hört statt zu sehen (große Ohren)
}
