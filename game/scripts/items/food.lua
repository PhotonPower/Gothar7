-- Essen und Tränke (Beispielinhalt; Werte Projektinhaber 2026-10-04). Benutzen im Inventar wirkt beim Event
-- `use` von t_eat bzw. t_drink und verbraucht das Item.
Item "it_apple" {
    name = "Apfel",
    mesh = "items/it_apple.glb",
    category = "food",
    value = 2,
    weight = 0.2,
    effects = { hp = 5 },
}

Item "it_bread" {
    name = "Brot",
    mesh = "items/it_bread.glb",
    category = "food",
    value = 5,
    weight = 0.4,
    effects = { hp = 10 },
}

Item "it_potion_heal_small" {
    name = "Kleiner Heiltrank",
    mesh = "items/it_potion_heal_small.glb",
    category = "potion",
    value = 30,
    weight = 0.3,
    effects = { hp = 40 },
}

-- Waren der Leonberger Händler (figuren F6; Wirkungen und Preise engine, vom Projektinhaber bestätigt 2026-10-08).
Item "it_ham" {
    name = "Schinken",
    mesh = "items/it_ham.glb",
    category = "food",
    value = 12,
    weight = 0.8,
    effects = { hp = 20 },
}

Item "it_sausage" {
    name = "Wurst",
    mesh = "items/it_sausage.glb",
    category = "food",
    value = 8,
    weight = 0.3,
    effects = { hp = 12 },
}

Item "it_cheese" {
    name = "Käse",
    mesh = "items/it_cheese.glb",
    category = "food",
    value = 7,
    weight = 0.5,
    effects = { hp = 10 },
}

Item "it_pretzel" {
    name = "Brezel",
    mesh = "items/it_pretzel.glb",
    category = "food",
    value = 4,
    weight = 0.2,
    effects = { hp = 8 },
}

Item "it_beer" {
    name = "Bier",
    mesh = "items/it_beer.glb",
    category = "food",
    value = 4,
    weight = 0.6,
    effects = { hp = 5 },
    tags = { "drink" }, -- getrunken, nicht gegessen (vorerst ohne Rausch)
}

Item "it_wine" {
    name = "Wein",
    mesh = "items/it_wine.glb",
    category = "food",
    value = 8,
    weight = 0.8,
    effects = { hp = 6 },
    tags = { "drink" },
}

Item "it_herb_sage" {
    name = "Salbei",
    mesh = "items/it_herb_sage.glb",
    category = "food",
    value = 6,
    weight = 0.05,
    effects = { hp = 6 },
}

Item "it_herb_chamomile" {
    name = "Kamille",
    mesh = "items/it_herb_chamomile.glb",
    category = "food",
    value = 4,
    weight = 0.05,
    effects = { hp = 4 },
}

Item "it_herb_nettle" {
    name = "Brennnessel",
    mesh = "items/it_herb_nettle.glb",
    category = "food",
    value = 3,
    weight = 0.05,
    effects = { hp = 3 },
}

Item "it_herbs_dried" {
    name = "Getrocknete Kräuter",
    description = "Ein Bund getrockneter Kräuter, wie ihn Kräuterkundige für Tränke brauchen.",
    mesh = "items/it_herbs_dried.glb",
    category = "misc",
    value = 10,
    weight = 0.1,
}

Item "it_potion_heal_medium" {
    name = "Heiltrank",
    mesh = "items/it_potion_heal_medium.glb",
    category = "potion",
    value = 60,
    weight = 0.3,
    effects = { hp = 70 },
}

Item "it_potion_speed" {
    name = "Trank der Schnelligkeit",
    description = "Für zwei Minuten läuft man schneller.",
    mesh = "items/it_potion_speed.glb",
    category = "potion",
    value = 80,
    weight = 0.3,
    boost = { speed = 1.3, seconds = 120 },
}

Item "it_potion_strength" {
    name = "Elixier der Stärke",
    description = "Selten und teuer: Es macht für immer stärker.",
    mesh = "items/it_potion_strength.glb",
    category = "potion",
    value = 800,
    weight = 0.3,
    effects = { str = 3 },
}
