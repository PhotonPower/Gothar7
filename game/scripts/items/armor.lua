-- Rüstung und Schmuck (Beispielinhalt M8). `protection` je Schadensart, `requires` Attribute oder Talente.
Item "it_armor_leather" {
    name = "Lederrüstung",
    category = "armor",
    value = 250,
    protection = { edge = 15, blunt = 10, point = 10 },
    requires = { str = 15 },
}

Item "it_ring_protection" {
    name = "Ring des Schutzes",
    category = "ring",
    value = 300,
    protection = { edge = 5, blunt = 5 },
}

Item "it_amulet_old" {
    name = "Altes Amulett",
    category = "amulet",
    value = 120,
    protection = { magic = 5 },
}

Item "it_key_chest_hut" {
    name = "Truhenschlüssel",
    category = "key",
    value = 0,
}
