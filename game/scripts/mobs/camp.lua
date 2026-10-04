-- Mobs des Testlagers (M8 Teil C): Ein Vob verweist mit components.mob.definition auf eine dieser Instanzen;
-- `type` wählt Slots und Clips aus data/mobs.toml.
Mob "mob_camp_chest" {
    name = "Truhe",
    type = "chest",
    contents = { it_apple = 2, it_lockpick = 1, it_blank_hot = 2 },
}

Mob "mob_camp_chest_locked" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRRLR",
    key = "it_key_chest_hut",
    contents = { it_ring_protection = 1, it_amulet_old = 1 },
}

Mob "mob_camp_door" {
    name = "Tür",
    type = "door",
}

Mob "mob_camp_door_locked" {
    name = "Verschlossene Tür",
    type = "door",
    lock = "RLR",
    key = "it_key_hut_door",
}

Mob "mob_camp_anvil" {
    name = "Amboss",
    type = "anvil",
}

Mob "mob_camp_bed" {
    name = "Bett",
    type = "bed",
}
