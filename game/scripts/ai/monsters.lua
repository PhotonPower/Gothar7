-- Verhalten der Tiere (M9 Teil D, Gothics ZS_MM_…): streifen im Revier und fressen, schlafen, drohen, greifen an
-- (bis M11: verfolgen), jagen Beute, fliehen vor Räubern. Werte in data/creatures.lua.

local leaders = {} -- Rudelmitglied -> Anführer
local animals = {} -- alle eingesetzten Tiere (für Jagd und Flucht)

--- Setzt ein Rudel ein: das erste Tier ist der Anführer, die anderen folgen ihm. Gibt die Namen zurück
--- ("mon_wolf", "mon_wolf#2" ...).
function insert_pack(npc, count, at)
    local names = {}
    for i = 1, count do
        local name = insert_npc(npc, at)
        names[#names + 1] = name
        animals[name] = true
        if i > 1 then
            leaders[name] = names[1]
        end
    end
    return names
end

--- Setzt ein einzelnes Tier ein (wie insert_npc; es nimmt an Jagd und Flucht teil).
function insert_animal(npc, at)
    local name = insert_npc(npc, at)
    animals[name] = true
    return name
end

local function state(npc)
    return npc_state(npc).state
end

local function reacting(npc)
    local s = state(npc)
    return s == "zs_mm_threaten" or s == "zs_mm_attack" or s == "zs_mm_hunt" or s == "zs_mm_flee"
end

--- Ein Streifzug im Revier: Rudelmitglieder folgen dem Anführer; manchmal erst fressen.
local function roam(npc, at)
    local c = animal(npc)
    if math.random() < (c.eat_chance or 0) then
        npc_play(npc, "eat")
        npc_wait(npc, 5 + math.random() * 5)
        npc_stop(npc)
    end
    if leaders[npc] then
        npc_follow_npc(npc, leaders[npc], 3, 8, c.pack_gait)
    else
        npc_roam(npc, at, c.territory)
        npc_wait(npc, 1 + math.random() * 3)
    end
end

State "zs_mm_roam" {
    begin = function(npc, at)
        roam(npc, at)
    end,
    loop = function(npc)
        roam(npc, npc_state(npc).at)
    end,
}

State "zs_mm_sleep" {
    begin = function(npc, at)
        npc_roam(npc, at, leaders[npc] and 4 or 1)
        npc_play(npc, "sleep")
    end,
}

State "zs_mm_threaten" {
    begin = function(npc)
        npc_clear(npc)
        npc_turn_to_player(npc)
        npc_play(npc, "threaten")
    end,
    loop = function(npc, seconds)
        local c = animal(npc)
        if npc_distance_to_player(npc) > c.threaten_distance + 4 then
            return "done" -- er ist zurückgewichen
        end
        if seconds >= c.threaten_seconds then
            emit("npc_would_attack", npc, "animal")
            npc_start_state(npc, "zs_mm_attack")
            return
        end
        npc_turn_to_player(npc)
        npc_play(npc, "threaten")
    end,
}

State "zs_mm_attack" {
    begin = function(npc)
        npc_clear(npc)
        fight_begin(npc, "hero") -- M11: der Kampf (ai/combat.lua)
    end,
    loop = function(npc)
        return fight_step(npc)
    end,
    finish = function(npc)
        fight_end(npc)
    end,
}

State "zs_mm_hunt" {
    begin = function(npc, prey)
        npc_clear(npc)
        fight_begin(npc, prey) -- M11: jagt und reißt die Beute
    end,
    loop = function(npc)
        return fight_step(npc)
    end,
    finish = function(npc)
        fight_end(npc)
    end,
}

State "zs_mm_flee" {
    begin = function(npc, from)
        npc_clear(npc)
        npc_flee(npc, 8, from)
    end,
    loop = function()
        return "done"
    end,
}

--- Das ganze Rudel macht mit (drohen, angreifen).
local function with_pack(npc, s)
    local leader = leaders[npc] or npc
    for member, l in pairs(leaders) do
        if l == leader and member ~= npc and not reacting(member) then
            npc_start_state(member, s)
        end
    end
    if leader ~= npc and not reacting(leader) then
        npc_start_state(leader, s)
    end
end

on("observe_player", function(npc, distance)
    local c = animal(npc)
    if not c or reacting(npc) or state(npc) == "zs_mm_sleep" then
        return
    end
    if c.threaten_distance and distance <= c.threaten_distance then
        npc_start_state(npc, "zs_mm_threaten")
        with_pack(npc, "zs_mm_threaten")
    elseif c.attack_distance and distance <= c.attack_distance then
        emit("npc_would_attack", npc, "animal")
        npc_start_state(npc, "zs_mm_attack")
    end
end)

-- Jagd und Flucht unter Tieren: einmal je Sekunde.
every(1.0, function()
    for npc in pairs(animals) do
        local ok, s = pcall(npc_state, npc)
        if not ok then
            animals[npc] = nil -- nicht (mehr) in dieser Welt
        elseif s.state ~= "zs_mm_sleep" and not reacting(npc) then
            local c = animal(npc)
            for _, near in ipairs(npcs_near(npc, 15)) do
                local other = instance("Npc", near.npc).species
                if c.predators and c.predators[other] and near.distance <= (c.flee_distance or 12) then
                    npc_start_state(npc, "zs_mm_flee", near.npc)
                    break
                elseif c.prey and c.prey[other] and not leaders[npc] then
                    npc_start_state(npc, "zs_mm_hunt", near.npc)
                    break
                end
            end
        end
    end
end)
