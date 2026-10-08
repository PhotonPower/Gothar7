-- Zauber (M12, Startsatz nach Entscheidung Z9 mit eigenen Namen). Werte sind Spielgefühl (Projektinhaber).
Spell "spl_firebolt" {
    name = "Feuerpfeil",
    circle = 1,
    mana = 10,
    kind = "projectile",
    damage = { fire = 25 },
    fx = { trail = "firebolt", impact = "impact_fire" },
    sounds = { cast = "spell_cast", impact = "fire_impact" }, -- M13 (Platzhalter)
}

Spell "spl_heal" {
    name = "Heilung",
    circle = 1,
    mana = 10,
    kind = "self",
    heal = 50,
    fx = { cast = "heal" },
    sounds = { cast = "heal" },
}

Spell "spl_sleep" {
    name = "Schlaf",
    circle = 2,
    mana = 15,
    kind = "target",
    effect = "sleep",
    duration = 20,           -- Z6: Schlaf 20 s; Schaden weckt
    fx = { on_target = "sleep" },
    sounds = { cast = "spell_cast" },
}

Spell "spl_transform_wolf" {
    name = "Wolfsgestalt",
    circle = 2,
    mana = 20,
    kind = "transform",
    species = "wolf",        -- Z7: ohne Waffen; zurück mit „1“ bzw. bei 0 LP
    sounds = { cast = "summon" },
}

Spell "spl_summon_wolf" {
    name = "Wolf rufen",
    circle = 3,
    mana = 25,
    kind = "summon",
    summon = "mon_wolf",
    duration = 60,           -- Z8: ein Begleiter für 60 s
    fx = { on_target = "summon" },
    sounds = { cast = "summon" },
}

-- Entscheidung C: Furcht als Spruchrolle im Startsatz (Z6: 10 s Flucht vor dem Zaubernden).
Spell "spl_fear" {
    name = "Schrecken",
    circle = 1,
    mana = 10,
    kind = "target",
    effect = "fear",
    duration = 10,
    fx = { on_target = "smoke" },
    sounds = { cast = "spell_cast" },
}
