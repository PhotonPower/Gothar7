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

Npc "mon_kristallkrebs" { -- Grotte (Ebene 7): Kristalle auf dem Rücken, Scheren halten fest (monsters.md)
    name = "Kristallkrebs",
    guild = "kristallkrebs",
    level = 10,
    species = "kristallkrebs",
    attributes = { str = 40, dex = 10, hp = 140 },
    damage = { edge = 20 }, -- Scheren
    -- Schieferpanzer gegen Klingen und Pfeile; stumpfe Waffen splittern ihn, Magie wirft er zum Teil zurück
    protection = { edge = 35, point = 50, blunt = 10, fire = 20, magic = 10, fall = 0 },
    inventory = { it_kristall = 1 }, -- Beute; stumpfe Treffer schlagen bis zu drei weitere ab (Creatures.splinter)
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

Npc "mon_beinhauswart" { -- Beinhaus (Ebene 4, monsters.md): lebender Totengräber, kein Untoter; Werte Platzhalter
    name = "Beinhauswart",
    guild = "beinhauswart",
    level = 9,
    species = "beinhauswart",
    attributes = { str = 35, dex = 15, hp = 110 },
    damage = { blunt = 18 }, -- Spaten
    protection = { edge = 10, point = 10, blunt = 5, fire = 0, magic = 5, fall = 0 },
    senses = { angle = 150 },
    inventory = { it_lantern_beinhaus = 1, it_shroud_rag = 1 }, -- Beute (die Laterne, solange er sie nicht fallen ließ)
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
