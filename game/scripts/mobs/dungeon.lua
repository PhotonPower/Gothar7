-- Truhen und Nischen des Dungeons unter dem Schloss (welt W8, docs/design/schloss-dungeon.md). welt trägt den Namen
-- als components.mob.definition an der Truhe bzw. Nische ein; `type` wählt Slots und Clips aus data/mobs.toml.
-- Schlösser ohne Schlüssel: nur mit dem Dietrich (wie im Lager); der erste liegt in der Truhe von Ebene 1.

-- Ebene 1 „Alte Gewölbe“
Mob "mob_dng_e1_chest" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 15, it_potion_heal_small = 1, it_lockpick = 2, it_torch = 1 },
}

Mob "mob_dng_e1_niche" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_heal = 1, it_gulden = 10 },
}

-- Ebene 2 „Verlies“
-- Osthalle (Truhe beim Teleporter): offen.
Mob "mob_dng_e2_chest_osthalle" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 30, it_potion_heal_small = 2, it_lockpick = 1 },
}

-- Zelle 2: was ein Gefangener zurückließ.
Mob "mob_dng_e2_chest_zelle2" {
    name = "Truhe",
    type = "chest",
    contents = { it_bread = 1, it_club = 1, it_gulden = 5 },
}

Mob "mob_dng_e2_niche_zelle2" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_sleep = 1, it_herbs_dried = 2 },
}

-- Zelle 4: die Ausrüstung der Wache, verschlossen.
Mob "mob_dng_e2_chest_zelle4" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRL",
    contents = { it_sword_crude = 1, it_arrow = 15, it_potion_mana_small = 1 },
}

-- Kerkerhalle: verschlossen, und eine Nische zwischen den Pfeilern.
Mob "mob_dng_e2_chest_kerker" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RLLR",
    contents = { it_gulden = 45, it_scroll_firebolt = 2, it_potion_heal_medium = 1 },
}

Mob "mob_dng_e2_niche_kerker" {
    name = "Nische",
    type = "niche",
    contents = { it_amulet_old = 1, it_gulden = 20 },
}

-- Depot hinter der Geheimwand am Flurende (neben den losen Funden auf dem Boden).
Mob "mob_dng_e2_niche_depot" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_fear = 1, it_potion_mana_medium = 1 },
}

-- Südgang vor der Treppe hinab.
Mob "mob_dng_e2_chest_sued" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 25, it_potion_heal_small = 1, it_bolt = 10 },
}

-- Schatzkammer (hinter der Platte, beim Gatterbock): das schwerste Schloss der Ebene.
Mob "mob_dng_e2_chest_schatz" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRRLRL",
    contents = {
        it_gulden = 120,
        it_chain_gold = 1,
        it_potion_heal_medium = 2,
        it_potion_speed = 1,
        it_scroll_telekinesis = 1,
    },
}
