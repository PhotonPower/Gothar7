-- Der Held: ein Npc wie alle anderen (gameplay.md „Charakter“); die Engine baut die Spielfigur daraus.
Npc "pc_hero" {
    name = "Held",
    level = 0,
    attributes = { hp = 40, hp_max = 40, mana = 5, mana_max = 5, str = 10, dex = 10 },
    inventory = { it_apple = 2 },
}
