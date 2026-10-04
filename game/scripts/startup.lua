-- Start: Hilfen für die Konsole und Reaktion auf Weltwechsel (Beispielinhalt M7).
local util = require "lib.util" -- (Beispiel für require)

on("world_loaded", function(world)
    Story.visits = (Story.visits or 0) + 1
    print(string.format("Welt geladen: %s (Besuch %d)", world, Story.visits))
end)


--- Setzt die Leute des Testlagers an die Orte ihres Tagesablaufs (Beispielinhalt M9; Konsole oder --exec).
function camp_people()
    for _, npc in ipairs({ "npc_gate_guard", "npc_farmer_woman", "npc_woodcutter", "npc_old_man" }) do
        insert_npc(npc)
    end
end

--- Setzt die Bewohner Leonbergs an die Orte ihres Tagesablaufs (Leonberg lebt; Konsole oder automatisch beim Laden).
function leonberg_people()
    for _, npc in ipairs({ "npc_leo_smith", "npc_leo_innkeeper", "npc_leo_baker", "npc_leo_market",
                           "npc_leo_guard_lower", "npc_leo_guard_upper", "npc_leo_farmer", "npc_leo_citizen" }) do
        insert_npc(npc)
    end
end

-- Entscheidung des Projektinhabers (L2): Beim Laden von Leonberg sind seine Bewohner da, wie Gothics Startup-Skripte.
on("world_loaded", function(world)
    if world:find("leonberg/leonberg.g7world", 1, true) then
        leonberg_people()
    end
end)
