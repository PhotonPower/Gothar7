-- Reaktionen auf Wahrnehmungen (M9 Teil C, Gothics B_Assess…): Die Engine meldet, was ein NPC sieht oder hört;
-- hier steht, was er daraus macht. Texte in data/shouts.lua. Bis zum Kampf (M11) droht ein NPC, der angreifen
-- würde, und folgt dem Spieler; die Engine bekommt das Ereignis npc_would_attack(npc, grund).

--- Ob der NPC gerade auf etwas anderes reagiert oder nicht wahrnehmen kann (schläft).
local function busy(npc)
    local s = npc_state(npc).state
    return s == "zs_sleep" or s == "zs_warn_weapon" or s == "zs_threaten" or s == "zs_intruder" or s == "zs_flee"
end

--- Tiere reagieren nach ai/monsters.lua.
local function human(npc)
    return animal(npc) == nil
end

local function guild_of(npc)
    return instance("Npc", npc).guild
end

local function is_guard(npc)
    return guild_of(npc) == "guard"
end

--- Wer bei Gefahr wegläuft statt mitzumachen: Bauern und Ausgestoßene niedriger Stufe (C2).
local function coward(npc)
    local n = instance("Npc", npc)
    return (n.guild == "farmer" or n.guild == "outcast") and (n.level or 0) <= 3
end

--- Ruft Kameraden in Hörweite: wessen Gilde der eigenen freundlich gesinnt ist, hilft (assess_call);
--- Feiglinge in der Nähe laufen weg.
local function alarm(npc)
    for _, near in ipairs(npcs_near(npc, 15)) do
        if not busy(near.npc) then
            if coward(near.npc) and near.distance <= 10 then
                npc_start_state(near.npc, "zs_flee")
            elseif attitude(near.guild, guild_of(npc)) == "friendly" then
                emit("assess_call", near.npc, npc)
            end
        end
    end
end

--- Würde angreifen: verärgert, ruft Hilfe, meldet es und droht (Kampf ab M11).
local function would_attack(npc, reason)
    set_temp_attitude(npc, "angry")
    emit("npc_would_attack", npc, reason)
    alarm(npc)
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

State "zs_flee" {
    begin = function(npc)
        npc_clear(npc)
        npc_shout(npc, Shouts.flee)
        npc_flee(npc, 8)
    end,
    loop = function(npc, seconds)
        return "done"
    end,
}

on("assess_call", function(helper, caller)
    if not human(helper) or busy(helper) then
        return
    end
    set_temp_attitude(helper, "angry")
    npc_say(helper, Shouts.help)
    npc_start_state(helper, "zs_threaten")
end)

on("assess_player", function(npc, distance)
    -- Feindlich Gesinnte greifen auf kurze Entfernung an (bis M11: drohen).
    if human(npc) and npc_attitude(npc) == "hostile" and distance <= 10 and not busy(npc) then
        npc_say(npc, Shouts.hostile)
        would_attack(npc, "hostile")
    end
end)

on("assess_fighter", function(npc, distance, what)
    if not human(npc) or busy(npc) then
        return
    end
    if coward(npc) then
        if distance < 4 then
            npc_start_state(npc, "zs_flee")
        end
        return
    end
    -- Wachen warnen auf Sichtweite, alle anderen nur, wenn man ihnen damit nahe kommt.
    if is_guard(npc) or distance < 5 then
        npc_start_state(npc, "zs_warn_weapon")
    end
end)

on("assess_enter_room", function(npc, owner, area)
    if not human(npc) then
        return
    end
    set_temp_attitude(npc, "angry")
    if npc_state(npc).state ~= "zs_intruder" then
        npc_start_state(npc, "zs_intruder", area)
    end
end)

on("assess_theft", function(npc, owner, item)
    if not human(npc) then
        return
    end
    npc_say(npc, Shouts.thief)
    would_attack(npc, "theft")
end)

on("assess_use_mob", function(npc, owner, mob)
    if human(npc) and (npc == owner or is_guard(npc)) then
        npc_say(npc, Shouts.foreign_mob)
        would_attack(npc, "mob")
    end
end)

on("assess_noise", function(npc, kind)
    if human(npc) and not busy(npc) and npc_state(npc).state ~= "zs_look_around" then
        npc_start_state(npc, "zs_look_around")
    end
end)
