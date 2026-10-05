-- Waffen (Beispielinhalt M7). Ohne `mesh` zeigt die Engine einen Platzhalter nach `category`.
Item "it_sword_old" {
    name = "Altes Schwert",
    mesh = "items/it_sword_old.glb",
    category = "melee_1h",
    value = 40,
    weight = 2.5,
    damage = { edge = 18 },
    requires = { str = 10 },
}

Item "it_club" {
    name = "Knüppel",
    mesh = "items/it_club.glb",
    category = "melee_1h",
    value = 8,
    weight = 2.0,
    damage = { blunt = 12 },
}

Item "it_bow_short" {
    name = "Kurzbogen",
    mesh = "items/it_bow_short.glb",
    category = "bow",
    value = 60,
    weight = 1.2,
    damage = { point = 15 },
    requires = { dex = 15 },
}

Item "it_crossbow" {
    name = "Leichte Armbrust",
    category = "crossbow",
    value = 120,
    weight = 3.5,
    damage = { point = 25 },
    requires = { str = 20 },
}

-- Munition (M11 Teil E): Pfeile für Bögen, Bolzen für Armbrüste. Modelle kommen von figuren (F6).
Item "it_arrow" {
    name = "Pfeil",
    category = "ammo",
    value = 1,
    weight = 0.05,
}

Item "it_bolt" {
    name = "Bolzen",
    category = "ammo",
    value = 2,
    weight = 0.08,
}
