-- Reaktionen auf Wahrnehmungen (M9 Teil C, Gothics B_Assess…): Die Engine meldet, was ein NPC sieht oder hört;
-- hier steht, was er daraus macht. Texte in data/shouts.lua. Bis zum Kampf (M11) droht ein NPC, der angreifen
-- würde, und folgt dem Spieler; die Engine bekommt das Ereignis npc_would_attack(npc, grund).

--- Ob der NPC gerade auf etwas anderes reagiert oder nicht wahrnehmen kann (schläft).
local function busy(npc)
    local s = npc_state(npc).state
    return s == "zs_sleep" or s == "zs_warn_weapon" or s == "zs_threaten" or s == "zs_intruder"
end

local function is_guard(npc)
    return instance("Npc", npc).guild == "guard"
end

--- Würde angreifen: melden und drohen (Kampf ab M11).
local function would_attack(npc, reason)
    emit("npc_would_attack", npc, reason)
    npc_start_state(npc, "zs_threaten")
end

-- Wie oft ein NPC schon wegen der Waffe gewarnt hat (vergisst es, wenn sie weg ist).
local warnings = {}

State "zs_warn_weapon" {
    begin = function(npc)
        npc_clear(npc)
        npc_turn_to_player(npc)
        warnings[npc] = (warnings[npc] or 0) + 1
        npc_say(npc, Shouts.weapon_warn[math.min(warnings[npc], #Shouts.weapon_warn)])
    end,
    loop = function(npc, seconds)
        if player_weapon() == "none" then
            warnings[npc] = nil
            npc_say(npc, Shouts.weapon_calm)
            return "done"
        end
        if seconds > 4 then
            if warnings[npc] >= #Shouts.weapon_warn then
                warnings[npc] = nil
                would_attack(npc, "weapon")
            else
                npc_start_state(npc, "zs_warn_weapon") -- die nächste Warnung
            end
        end
    end,
}

State "zs_threaten" {
    begin = function(npc)
        npc_clear(npc)
        npc_say(npc, Shouts.threaten)
        npc_follow_player(npc, 15, 2)
    end,
    loop = function(npc, seconds)
        return "done" -- folgt 15 s, danach wieder der Tagesablauf
    end,
}

State "zs_intruder" {
    begin = function(npc, at)
        npc_clear(npc)
        npc_turn_to_player(npc)
        npc_say(npc, Shouts.intruder)
    end,
    loop = function(npc, seconds)
        local area = npc_state(npc).at
        if not player_inside(area) then
            return "done"
        end
        if seconds > 6 then
            npc_say(npc, Shouts.intruder_again)
            would_attack(npc, "intruder")
        end
    end,
}

on("assess_fighter", function(npc, distance, what)
    if busy(npc) then
        return
    end
    -- Wachen warnen auf Sichtweite, alle anderen nur, wenn man ihnen damit nahe kommt.
    if is_guard(npc) or distance < 5 then
        npc_start_state(npc, "zs_warn_weapon")
    end
end)

on("assess_enter_room", function(npc, owner, area)
    if npc_state(npc).state ~= "zs_intruder" then
        npc_start_state(npc, "zs_intruder", area)
    end
end)

on("assess_theft", function(npc, owner, item)
    npc_say(npc, Shouts.thief)
    would_attack(npc, "theft")
end)

on("assess_use_mob", function(npc, owner, mob)
    if npc == owner or is_guard(npc) then
        npc_say(npc, Shouts.foreign_mob)
        would_attack(npc, "mob")
    end
end)

on("assess_noise", function(npc, kind)
    if not busy(npc) and npc_state(npc).state ~= "zs_look_around" then
        npc_start_state(npc, "zs_look_around")
    end
end)
