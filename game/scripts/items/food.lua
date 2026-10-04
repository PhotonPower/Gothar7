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
