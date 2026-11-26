-- Beinhauswart (docs/design/monsters.md, Werte in Creatures.beinhauswart): trägt seine grüne Laterne (npc_hold an
-- socket_lantern), stößt sie dem Helden nach dem Erwachen und danach alle flash.every Sekunden entgegen
-- (zs_mm_threaten, t_threaten, Clip-Event flash: das Blenden ist C++) und lässt sie bei lantern_drop seines Lebens
-- fallen (Rasend-Phase: nur noch der Spaten).

local wardens = {} -- Beinhauswart -> Spielsekunden des letzten Blendens
local lanterns = {} -- Beinhauswart -> true, solange er seine Laterne trägt
local clock = 0

local function warden(npc)
    local c = animal(npc)
    return c and c.flash and c.lantern and c or nil
end

--- Gibt dem Beinhauswart seine Laterne in die Hand (beim Einsetzen durch einen Spawn-Marker von selbst).
function arm_warden(npc)
    local c = warden(npc)
    if c and npc_item_count(npc, c.lantern) > 0 then
        npc_hold(npc, c.lantern, "socket_lantern")
        lanterns[npc] = true
        wardens[npc] = wardens[npc] or -c.flash.every
    end
end

--- Ob er seine Laterne noch trägt.
function warden_has_lantern(npc)
    return lanterns[npc] == true
end

on("npc_spawned", function(npc)
    arm_warden(npc)
end)

on("npc_clip_event", function(npc, event)
    if event == "flash" and wardens[npc] then
        wardens[npc] = clock
    end
end)

-- Rasend-Phase: unter lantern_drop des Lebens fällt die Laterne zu Boden (Beute zum Aufheben).
on("npc_hit", function(attacker, target)
    local c = warden(target)
    if not c or not warden_has_lantern(target) or fight_state(target) == "dead" then
        return
    end
    if npc_stat(target, "hp") <= c.lantern_drop * npc_stat(target, "hp_max") then
        npc_hold(target)
        npc_drop_item(target, c.lantern, 1)
        lanterns[target] = nil
        wardens[target] = nil
    end
end)

-- Im Kampf: wieder blenden, sobald flash.every vorbei ist und der Held nah genug steht.
every(1.0, function()
    clock = clock + 1
    for npc, last in pairs(wardens) do
        local c = warden(npc)
        local ok, s = pcall(npc_state, npc)
        if not ok or not c then
            wardens[npc] = nil
        elseif s.state == "zs_mm_attack" and fight_state(npc) == "ready" and clock - last >= c.flash.every
            and npc_distance_to_player(npc) <= c.threaten_distance then
            npc_start_state(npc, "zs_mm_threaten")
        end
    end
end)
