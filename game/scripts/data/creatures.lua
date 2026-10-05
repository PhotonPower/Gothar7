-- Tiere (M9 Teil D): wie sie leben und auf den Spieler und andere Tiere reagieren. Entscheidungen des
-- Projektinhabers (2026-10-04): Wolf und Keiler drohen ab etwa 12 m einige Sekunden, dann greifen sie an (bis zum
-- Kampf in M11: verfolgen); der Laufvogel ist neutral und greift erst unter 4 m an. Wolfsrudel 2-4 mit Anführer.
-- Wölfe sind nachts unterwegs und schlafen tags, Keiler und Laufvogel umgekehrt; zwischendurch fressen sie.
Creatures = {
    wolf = {
        territory = 25,         -- m um den Wegpunkt des Tagesablaufs
        threaten_distance = 12, -- näher: drohen (Knurren)
        threaten_seconds = 3,   -- so lange, dann Angriff
        eat_chance = 0.25,      -- je Streifzug: erst fressen
        prey = { laufvogel = true },
        pack_gait = "trot",  -- Rudel folgt dem Anführer im Trab (figuren #200: Trab 3 m/s)
        chase_gait = "trot", -- verfolgen: nah im Trab, ab 8 m rennend (Gangarten: Gehen 1,2 / Trab 3 / Rennen 6)
    },
    keiler = {
        territory = 20,
        threaten_distance = 12, -- Scharren
        threaten_seconds = 3,
        eat_chance = 0.4,
    },
    laufvogel = {
        territory = 20,
        attack_distance = 4, -- neutral, erst so nah greift er an
        eat_chance = 0.4,
        predators = { wolf = true },
        flee_distance = 12, -- vor Räubern
    },
}

--- Die Werte des Tieres aus Creatures; nil für Menschen (Npc ohne `species`).
function animal(npc)
    local n = instance("Npc", npc)
    return n and n.species and Creatures[n.species] or nil
end
