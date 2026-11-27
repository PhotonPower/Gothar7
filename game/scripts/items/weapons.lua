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

-- Lohn im Hort der Kellermutter (Dungeon Ebene 8, Entscheidung des Projektinhabers): die beste Einhandwaffe des Spiels.
-- Modell von figuren (#526).
Item "it_scythe_blade" {
    name = "Sensenklinge",
    description = "Aus der Fangklaue der Kellermutter geschliffen. Leicht gekrümmt, schärfer als jede Klinge aus der Schmiede.",
    mesh = "items/it_scythe_blade.glb",
    category = "melee_1h",
    value = 450,
    weight = 2.8,
    damage = { edge = 32 },
    requires = { str = 20 },
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
    mesh = "items/it_crossbow.glb",
    category = "crossbow",
    value = 120,
    weight = 3.5,
    damage = { point = 25 },
    requires = { str = 20 },
}

-- Munition (M11 Teil E): Pfeile für Bögen, Bolzen für Armbrüste (Modelle von figuren, F6).
Item "it_arrow" {
    name = "Pfeil",
    mesh = "items/it_arrow.glb",
    category = "ammo",
    value = 1,
    weight = 0.05,
}

Item "it_bolt" {
    name = "Bolzen",
    mesh = "items/it_bolt.glb",
    category = "ammo",
    value = 2,
    weight = 0.08,
}

Item "it_sword_2h" {
    name = "Zweihänder",
    mesh = "items/it_sword_2h.glb",
    category = "melee_2h",
    value = 300,
    weight = 6.0,
    damage = { edge = 40 },
    requires = { str = 40 },
}
