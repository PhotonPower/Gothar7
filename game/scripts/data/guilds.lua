-- Gilden (data/ lädt nach lib/, vor dem Inhalt). Einstellungen untereinander folgen mit M9.
Guilds = {
    farmer = { name = "Bauern" },
    guard = { name = "Wachen" },
    hunter = { name = "Jäger" },
    outcast = { name = "Ausgestoßene" },
}

-- Einstellungen der Gilden zueinander (Gothic: freundlich, neutral, feindlich); was fehlt, ist neutral.
-- Die KI wertet sie mit M9 aus.
Attitudes = {
    guard = { outcast = "hostile", farmer = "friendly" },
    farmer = { guard = "friendly" },
    outcast = { guard = "hostile" },
}

--- Einstellung von Gilde `a` zu Gilde `b`: "friendly", "neutral" oder "hostile".
function attitude(a, b)
    if a == b then
        return "friendly"
    end
    local row = Attitudes[a]
    return row and row[b] or "neutral"
end

-- Tiere (M9 Teil D): eigene Gilden; ihr Verhalten steht in data/creatures.lua.
Guilds.wolf = { name = "Wölfe" }
Guilds.keiler = { name = "Keiler" }
Guilds.laufvogel = { name = "Laufvögel" }
