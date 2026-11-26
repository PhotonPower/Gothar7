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

-- Telekinese (M17 Teil T, Entscheidungen 1-8 des Projektinhabers 2026-11-08): ein Gegenstand bis 1,2 m schwebt
-- zum Zaubernden, solange der Zauber gehalten wird; loslassen oder Mana leer: er fällt. Personen nicht.
Spell "spl_telekinesis" {
    name = "Telekinese",
    circle = 2,
    mana = 10,               -- beim Anheben (Entscheidung 1)
    mana_per_second = 2,     -- solange gehalten
    kind = "telekinesis",
    range = 15,              -- Entscheidung 2: anvisieren bis 15 m
    lift = 0.5,              -- erst 0,5 m anheben
    speed = 3,               -- dann 3 m/s
    carry = 1.2,             -- Entscheidung 4: Gegenstände bis 1,2 m, auch Waffen
    blocked_drop = 1.0,      -- hängt er etwa 1 s an einem Hindernis fest, fällt er (Projektinhaber 2026-11-08)
    fx = { on_target = "telekinesis" },
    sounds = { cast = "spell_cast" },
}

-- Licht (Wunsch des Projektinhabers, 2026-11-26: „eine Art Plasmasphäre, die Licht macht und auf der Hand schwebt"):
-- eine Kugel aus blassblauem Plasma über der offenen linken Hand; ist die Hand belegt (Zweihänder, Bogen, Rune,
-- Fackel, Klettern, Schwimmen, Mob), schwebt sie über der linken Schulter. 300 s Spielzeit (Simulation, steht in der
-- Pause), erneutes Wirken erneuert sie; in den letzten Sekunden flackert sie stärker, dann verlischt sie.
Spell "spl_light" {
    name = "Licht",
    circle = 1,
    mana = 10,
    kind = "light",
    duration = 300,
    light = {
        color = { 0.86, 0.92, 1.0 }, -- linear: warm-neutrales Weiß mit leichtem Kaltstich
        range = 8,                   -- m
        intensity = 3.0,
        flicker = 0.08,              -- sanft
        hover = 0.15,                -- m über der offenen Hand
        shoulder = 0.25,             -- m über der linken Schulter, solange die Hand belegt ist
        warn = 10,                   -- die letzten Sekunden ...
        warn_flicker = 0.5,          -- ... flackert sie so stark
        fade = 1.0,                  -- s Verlöschen
    },
    fx = { orb = "light_orb", sparks = "light_sparks" },
    sounds = { cast = "light_pop", loop = "light_hum", fade = "light_pop" },
}
