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
    inventory = { it_ember_eye = 2 }, -- Beute
}

Npc "mon_mauerkriecher" { -- Brut der Kellermutter, kommt zu 3-6 aus den Ritzen (spawn count, monsters.md)
    name = "Mauerkriecher",
    guild = "mauerkriecher",
    level = 3,
    species = "mauerkriecher",
    attributes = { str = 10, dex = 20, hp = 30 },
    damage = { edge = 5 }, -- Zangenbiss
    protection = { edge = 15, point = 25, blunt = 0, fire = 0, magic = 0, fall = 0 }, -- Panzer gegen Klingen
    inventory = { it_crawler_plate = 1 }, -- Beute
}

Npc "mon_brunnenmolch" { -- Zisterne (Ebenen 3 und 7, monsters.md); Werte Platzhalter
    name = "Brunnenmolch",
    guild = "brunnenmolch",
    level = 7,
    species = "brunnenmolch",
    attributes = { str = 30, dex = 10, hp = 90 },
    damage = { edge = 15 }, -- Biss
    protection = { edge = 5, point = 5, blunt = 10, fire = 30, magic = 0, fall = 0 }, -- an Land verletzlich
    senses = { sight = 0, hearing = 2 }, -- blind: hört statt zu sehen (Schleichen hilft)
    inventory = { it_newt_skin = 1, it_newt_gills = 2 }, -- Beute (Alchemie)
}

Npc "mon_mauerschlupfer" {
    name = "Mauerschlupfer",
    guild = "mauerschlupfer",
    level = 4,
    species = "mauerschlupfer",
    attributes = { str = 12, dex = 30, hp = 45 },
    damage = { edge = 4 }, -- Biss und Kralle
    senses = { sight = 0, hearing = 2 }, -- blind: hört statt zu sehen (große Ohren)
    inventory = { it_bat_ear = 2 }, -- Beute
}
