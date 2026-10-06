-- Zauber (M12, Startsatz nach Entscheidung Z9 mit eigenen Namen). Werte sind Spielgefühl (Projektinhaber).
Spell "spl_firebolt" {
    name = "Feuerpfeil",
    circle = 1,
    mana = 10,
    kind = "projectile",
    damage = { fire = 25 },
    fx = { cast = "heal", trail = "firebolt", impact = "impact_fire" },
}

Spell "spl_heal" {
    name = "Heilung",
    circle = 1,
    mana = 10,
    kind = "self",
    heal = 50,
    fx = { cast = "heal" },
}

Spell "spl_sleep" {
    name = "Schlaf",
    circle = 2,
    mana = 15,
    kind = "target",
    effect = "sleep",
    duration = 20,           -- Z6: Schlaf 20 s; Schaden weckt
    fx = { on_target = "sleep" },
}

Spell "spl_transform_wolf" {
    name = "Wolfsgestalt",
    circle = 2,
    mana = 20,
    kind = "transform",
    species = "wolf",        -- Z7: ohne Waffen; zurück mit „1“ bzw. bei 0 LP
}

Spell "spl_summon_wolf" {
    name = "Wolf rufen",
    circle = 3,
    mana = 25,
    kind = "summon",
    summon = "mon_wolf",
    duration = 60,           -- Z8: ein Begleiter für 60 s
}
