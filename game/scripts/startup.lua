-- Start: Hilfen für die Konsole und Reaktion auf Weltwechsel (Beispielinhalt M7).
local util = require "lib.util" -- (Beispiel für require)

--- Ruft einen Dialog direkt auf, wenn seine Bedingung gilt (bis die Dialoge mit M12 kommen).
function call_info(name)
    local info = instance("Info", name)
    if not info then
        error("no Info " .. name)
    end
    if info.condition and not info.condition() then
        return false
    end
    info.run()
    return true
end

on("world_loaded", function(world)
    Story.visits = (Story.visits or 0) + 1
    print(string.format("Welt geladen: %s (Besuch %d)", world, Story.visits))
end)

