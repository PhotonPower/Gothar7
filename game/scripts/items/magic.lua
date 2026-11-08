-- Runen (dauerhaft, brauchen den Kreis des Zaubers) und Spruchrollen (ohne Kreis, verbraucht) - M12, Z2/Z3.
-- Modelle von figuren (#222): eine Datei je Rune (gleiche Form, eigenes Material), eine für alle Rollen.
local function rune(id, spell, name, value)
    Item(id) {
        name = "Rune: " .. name,
        mesh = "items/" .. id .. ".glb",
        category = "rune",
        value = value,
        weight = 0.2,
        spell = spell,
    }
end

local function scroll(id, spell, name, value)
    Item(id) {
        name = "Spruchrolle: " .. name,
        mesh = "items/it_scroll.glb",
        category = "scroll",
        value = value,
        weight = 0.1,
        spell = spell,
    }
end

rune("it_rune_firebolt", "spl_firebolt", "Feuerpfeil", 150)
rune("it_rune_heal", "spl_heal", "Heilung", 150)
rune("it_rune_sleep", "spl_sleep", "Schlaf", 300)
rune("it_rune_transform_wolf", "spl_transform_wolf", "Wolfsgestalt", 300)
rune("it_rune_summon_wolf", "spl_summon_wolf", "Wolf rufen", 450)
rune("it_rune_telekinesis", "spl_telekinesis", "Telekinese", 300) -- M17 T; Modell folgt (figuren), bis dahin Platzhalter

scroll("it_scroll_firebolt", "spl_firebolt", "Feuerpfeil", 25)
scroll("it_scroll_heal", "spl_heal", "Heilung", 25)
scroll("it_scroll_sleep", "spl_sleep", "Schlaf", 50)
scroll("it_scroll_fear", "spl_fear", "Schrecken", 40)
scroll("it_scroll_telekinesis", "spl_telekinesis", "Telekinese", 50) -- M17 T, Entscheidung 7

Item "it_potion_mana_small" {
    name = "Kleiner Manatrank",
    mesh = "items/it_potion_mana_small.glb",
    category = "potion",
    value = 30,
    weight = 0.3,
    effects = { mana = 30 },
}

Item "it_potion_mana_medium" {
    name = "Manatrank",
    mesh = "items/it_potion_mana_medium.glb",
    category = "potion",
    value = 60,
    weight = 0.3,
    effects = { mana = 50 },
}
