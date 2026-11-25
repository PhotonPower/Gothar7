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

Npc "mon_mauerkriecher" { -- Brut der Kellermutter, kommt zu 3-6 aus den Ritzen (spawn count, monsters.md)
    name = "Mauerkriecher",
    guild = "mauerkriecher",
    level = 3,
    species = "mauerkriecher",
    attributes = { str = 10, dex = 20, hp = 30 },
    damage = { edge = 5 }, -- Zangenbiss
    protection = { edge = 15, point = 25, blunt = 0, fire = 0, magic = 0, fall = 0 }, -- Panzer gegen Klingen
}

Npc "mon_kellermutter" { -- Boss im Hort (Ebene 8, monsters.md); Werte Platzhalter
    name = "Kellermutter",
    guild = "kellermutter",
    level = 25,
    species = "kellermutter",
    attributes = { str = 80, dex = 10, hp = 600 },
    damage = { edge = 40 }, -- Sensenarme
    protection = { edge = 40, point = 50, blunt = 30, fire = 20, magic = 20, fall = 1000 },
    senses = { angle = 160 },
    inventory = { it_scythe_claw = 2, it_brood_gland = 1 }, -- Beute
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
