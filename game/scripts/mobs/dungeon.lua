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
    contents = { it_scroll_heal = 1, it_scroll_light = 1, it_gulden = 10 },
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

-- Ebene 3 „Zisterne“
-- Nordostkammer: Truhe und Nische nebeneinander.
Mob "mob_dng_e3_chest_nord" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 35, it_potion_heal_medium = 1, it_bolt = 15 },
}

Mob "mob_dng_e3_niche_nord" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_heal = 2, it_herb_sage = 2 },
}

-- Depot hinter der Geheimwand (die drei losen Steine), neben den Funden auf dem Boden.
Mob "mob_dng_e3_niche_depot" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_firebolt = 2, it_potion_mana_medium = 1, it_ring_silver = 1 },
}

-- Ostkammer: verschlossen.
Mob "mob_dng_e3_chest_ost" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RRLRL",
    contents = { it_gulden = 70, it_potion_strength = 1, it_scroll_sleep = 1, it_lockpick = 2 },
}

-- Schleusenkammer vor dem Abstieg nach Ebene 4: Vorrat für unten.
Mob "mob_dng_e3_chest_schleuse" {
    name = "Truhe",
    type = "chest",
    contents = { it_potion_heal_medium = 2, it_potion_heal_small = 2, it_torch = 1, it_bread = 2 },
}

-- Ebene 4 „Beinhaus“ (welt #495)
-- Gräberkammer im Osten, wo der Schacht ankommt.
Mob "mob_dng_e4_chest_graeber" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 50, it_potion_heal_medium = 1, it_potion_mana_small = 1, it_arrow = 20 },
}

-- Gruft 1: verschlossen, und eine Nische.
Mob "mob_dng_e4_chest_gruft1" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LLRLR",
    contents = { it_gulden = 80, it_amulet = 1, it_scroll_fear = 1, it_potion_heal_medium = 1 },
}

Mob "mob_dng_e4_niche_gruft1" {
    name = "Nische",
    type = "niche",
    contents = { it_ring_silver = 1, it_gulden = 30 },
}

-- Gruft 3 mit dem Depot.
Mob "mob_dng_e4_chest_gruft3" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 60, it_scroll_firebolt = 2, it_potion_mana_medium = 1 },
}

Mob "mob_dng_e4_niche_gruft3" {
    name = "Nische",
    type = "niche",
    contents = { it_ring_gold = 1, it_potion_strength = 1 },
}

-- Beinhaus, die Kammer des Beinhauswarts.
Mob "mob_dng_e4_niche_beinhaus" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_sleep = 2, it_herbs_dried = 3, it_gulden = 25 },
}

-- Totengräberkammer: Werkzeug und Vorrat des Totengräbers.
Mob "mob_dng_e4_chest_totengraeber" {
    name = "Truhe",
    type = "chest",
    contents = { it_hammer = 1, it_axe = 1, it_torch = 2, it_bread = 2, it_beer = 1, it_gulden = 15 },
}

-- Treppenkammer hinter dem Zeitgatter: der Lohn der Ebene.
Mob "mob_dng_e4_chest_treppe" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RLRRLRL",
    contents = {
        it_gulden = 200,
        it_rune_firebolt = 1,
        it_sword_2h = 1,
        it_potion_heal_medium = 3,
        it_potion_speed = 1,
        it_lockpick = 2,
    },
}

-- Ebene 5 „Werkstatt“ (welt #501). Bewusst maßvoll: das Depot hält schon Ring, Trank, 200 Gulden und Dietriche.
Mob "mob_dng_e5_chest_lager" {
    name = "Truhe",
    type = "chest",
    contents = { it_gulden = 40, it_potion_heal_medium = 1, it_bolt = 20, it_torch = 1 },
}

-- Schmiede: Werkzeug und ein Stück Arbeit, das liegen blieb.
Mob "mob_dng_e5_chest_schmiede" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRLLR",
    contents = { it_hammer = 1, it_blank_hot = 2, it_sword_old = 1, it_gulden = 30 },
}

Mob "mob_dng_e5_niche_schmiede" {
    name = "Nische",
    type = "niche",
    contents = { it_potion_heal_small = 2, it_gulden = 20 },
}

-- Uhrwerkskammer.
Mob "mob_dng_e5_chest_uhrwerk" {
    name = "Truhe",
    type = "chest",
    contents = { it_scroll_telekinesis = 1, it_potion_mana_medium = 1, it_gulden = 35 },
}

-- Gießerei.
Mob "mob_dng_e5_chest_giesserei" {
    name = "Truhe",
    type = "chest",
    contents = { it_ladle = 1, it_potion_heal_medium = 1, it_gulden = 25 },
}

Mob "mob_dng_e5_niche_giesserei" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_firebolt = 1, it_herbs_dried = 2 },
}

-- Depot: nur eine Kleinigkeit neben den Funden auf dem Boden.
Mob "mob_dng_e5_niche_depot" {
    name = "Nische",
    type = "niche",
    contents = { it_potion_mana_small = 1, it_scroll_heal = 1 },
}

-- Treppenkammer: verschlossen, der Lohn der Ebene (maßvoller als in Ebene 4).
Mob "mob_dng_e5_chest_treppe" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RLRLLR",
    contents = { it_gulden = 90, it_potion_heal_medium = 2, it_potion_speed = 1, it_scroll_sleep = 1 },
}

-- Ebene 6 „Labyrinth“ (welt #510). Maßvoll wie Ebene 5: das versteckte Depot hält schon Goldring, Heiltränke, Gulden
-- und Pfeile; keine Rune, keine Zweihandwaffe.
-- Sackgasse hinter der Geheimwand (Norden): verschlossen, der Lohn fürs Suchen.
Mob "mob_dng_e6_chest_geheim" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRRLR",
    contents = { it_gulden = 60, it_ring_silver = 1, it_potion_heal_medium = 1, it_scroll_fear = 1 },
}

-- Sackgasse im Westen: was ein Verirrter zurückließ.
Mob "mob_dng_e6_chest_west" {
    name = "Truhe",
    type = "chest",
    contents = { it_torch = 2, it_bread = 1, it_potion_heal_small = 1, it_gulden = 15 },
}

-- Sackgasse im Süden.
Mob "mob_dng_e6_chest_sued" {
    name = "Truhe",
    type = "chest",
    contents = { it_arrow = 20, it_potion_mana_small = 1, it_gulden = 25 },
}

-- Sackgasse im Südosten bei der Teleporter-Tür.
Mob "mob_dng_e6_chest_teleporter" {
    name = "Truhe",
    type = "chest",
    contents = { it_scroll_light = 1, it_potion_speed = 1, it_lockpick = 2, it_gulden = 20 },
}

-- Hebelkammer: verschlossen.
Mob "mob_dng_e6_chest_hebel" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RLLRLR",
    contents = { it_gulden = 80, it_potion_heal_medium = 2, it_potion_mana_medium = 1, it_scroll_sleep = 1 },
}

-- Depot: nur eine Kleinigkeit neben den Funden auf dem Boden.
Mob "mob_dng_e6_niche_depot" {
    name = "Nische",
    type = "niche",
    contents = { it_potion_strength = 1, it_gulden = 15 },
}

-- Nordostecke vor dem Depot.
Mob "mob_dng_e6_niche_nordost" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_heal = 1, it_herbs_dried = 2 },
}

-- Gang im Süden.
Mob "mob_dng_e6_niche_gang" {
    name = "Nische",
    type = "niche",
    contents = { it_scroll_firebolt = 1, it_gulden = 10 },
}

-- Ebene 7 „Kristallgrotte“ (welt #520). Maßvoll wie Ebene 5/6, keine Rune, keine Zweihandwaffe; dazu Leuchtkristalle.
-- Untere Höhle hinter der Falle: ein Lohn für den Sturz.
Mob "mob_dng_e7_chest_absturz" {
    name = "Truhe",
    type = "chest",
    contents = { it_potion_heal_small = 2, it_torch = 1, it_gulden = 20 },
}

-- Seegrotte, Nordufer.
Mob "mob_dng_e7_chest_seeufer" {
    name = "Truhe",
    type = "chest",
    contents = { it_kristall = 2, it_potion_mana_small = 1, it_bolt = 15, it_gulden = 25 },
}

-- Insel im See (über den Steg, Brunnenmolche wachen): die beste Truhe der Ebene.
Mob "mob_dng_e7_chest_insel" {
    name = "Truhe",
    type = "chest",
    contents = {
        it_ring_protection = 1,
        it_kristall = 3,
        it_potion_heal_medium = 1,
        it_scroll_fear = 1,
        it_gulden = 70,
    },
}

-- Treppenkammer vor Ebene 8: verschlossen, Heiltränke als Vorbereitung auf die Kellermutter.
Mob "mob_dng_e7_chest_treppe" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "RRLRLL",
    contents = { it_potion_heal_medium = 3, it_potion_heal_small = 2, it_potion_strength = 1, it_gulden = 40 },
}

-- Kristallkammer: das Depot hinter der Kristallwand.
Mob "mob_dng_e7_niche_kristall" {
    name = "Nische",
    type = "niche",
    contents = { it_kristall = 2, it_potion_mana_medium = 1, it_gulden = 15 },
}

-- Ebene 8 „Hort der Kellermutter“ (welt #523). Die Vorkammer bereitet auf den Kampf vor; der Hort ist der große Lohn
-- des Dungeons (neben 500 losen Gulden): deutlich reicher als Ebene 4–7.
-- Vorkammer vor dem Boss: Tränke als Vorbereitung.
Mob "mob_dng_e8_chest_vorkammer" {
    name = "Truhe",
    type = "chest",
    contents = { it_potion_heal_medium = 3, it_potion_mana_medium = 1, it_potion_strength = 1, it_gulden = 30 },
}

-- Hort: Rüstung und Gold.
Mob "mob_dng_e8_chest_hort_1" {
    name = "Truhe",
    type = "chest",
    contents = { it_armor_leather = 1, it_ring_gold = 1, it_potion_heal_medium = 2, it_gulden = 250 },
}

-- Hort: verschlossen, die Rune des Wolfsrufs und Schmuck.
Mob "mob_dng_e8_chest_hort_2" {
    name = "Verschlossene Truhe",
    type = "chest",
    lock = "LRLRRLL",
    contents = {
        it_rune_summon_wolf = 1,
        it_amulet = 1,
        it_chain_gold = 1,
        it_potion_mana_medium = 2,
        it_gulden = 300,
    },
}

-- Hort: Schlafrune, Armbrust mit Bolzen, Ring.
Mob "mob_dng_e8_chest_hort_3" {
    name = "Truhe",
    type = "chest",
    contents = { it_rune_sleep = 1, it_crossbow = 1, it_bolt = 30, it_ring_silver = 1, it_gulden = 200 },
}

-- Nische im Hort: was von der Brut der Kellermutter blieb, und ein altes Amulett.
Mob "mob_dng_e8_niche_hort" {
    name = "Nische",
    type = "niche",
    contents = { it_brood_gland = 1, it_amulet_old = 1 },
}
