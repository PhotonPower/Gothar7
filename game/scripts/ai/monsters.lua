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
        or s == "zs_mm_wake" or s == "zs_mm_steal"
end

--- Ruht (Dungeon D6): schläft wie eine Statue bzw. eingerollt und nimmt nur halb so weit wahr.
local function resting(npc)
    local s = state(npc)
    return s == "zs_mm_sleep" or s == "zs_dormant"
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
    if not c or reacting(npc) or resting(npc) or (Summoned and Summoned[npc]) then
        return
    end
    -- Der Held in Tiergestalt (M12, Z7): die eigene Art lässt ihn in Ruhe; Beute flieht, Räuber jagen ihn.
    local shape = hero_shape()
    if shape then
        if shape == instance("Npc", npc).species then
            return
        elseif c.predators and c.predators[shape] then
            if distance <= (c.flee_distance or 12) then
                npc_start_state(npc, "zs_mm_flee")
            end
            return
        elseif c.prey and c.prey[shape] then
            npc_start_state(npc, "zs_mm_attack")
            return
        end
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
        elseif not resting(npc) and not reacting(npc) then
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

-- Dungeon D6 (docs/modules/dungeon.md "Monster im Dungeon"): Spawn-Marker setzen Monster ein, die ruhen
-- (zs_dormant), bis sie den Helden mit halber Reichweite sehen oder hören, getroffen werden oder das Ereignis
-- `wake` ihren Marker erreicht. Erwacht bleiben sie wach (gespeichert über npc_dormant); sie streifen um ihren
-- Marker und folgen dem Helden nicht in andere Ebenen.

local homes = {}  -- Monster -> sein Spawn-Marker (das Revier)
local stolen = {} -- Mauerschlupfer -> { item, count }

State "zs_dormant" {
    begin = function(npc)
        npc_clear(npc)
        npc_play(npc, "dormant") -- s_dormant (ohne den Clip: s_sleep)
    end,
}

--- Weckt ein ruhendes Monster: es erwacht (t_wake) und handelt danach (angreifen bzw. stehlen).
function wake(npc)
    if not npc_dormant(npc) or fight_state(npc) == "dead" then
        return
    end
    npc_set_dormant(npc, false)
    npc_start_state(npc, "zs_mm_wake")
end

State "zs_mm_wake" {
    begin = function(npc)
        npc_clear(npc)
        npc_turn_to_player(npc)
        npc_play(npc, "wake")
        npc_wait(npc, (animal(npc) or {}).wake_seconds or 1.0)
    end,
    loop = function(npc)
        local c = animal(npc) or {}
        if c.steals and player_has_shiny() then
            npc_start_state(npc, "zs_mm_steal")
        elseif c.flash and c.threaten_distance and npc_distance_to_player(npc) <= c.threaten_distance then
            npc_start_state(npc, "zs_mm_threaten") -- Beinhauswart: erst die Laterne (ai/beinhauswart.lua)
        else
            emit("npc_would_attack", npc, "animal")
            npc_start_state(npc, "zs_mm_attack")
        end
    end,
}

--- Das Glänzende im Inventar des Helden (Gegenstände mit tags = { "shiny" }): das wertvollste, sonst nil.
function shiny_item()
    local best
    for _, stack in ipairs(inventory()) do
        local def = instance("Item", stack.item)
        local tags = def and def.tags or {}
        for _, tag in ipairs(tags) do
            if tag == "shiny" and (not best or (def.value or 0) > (instance("Item", best.item).value or 0)) then
                best = stack
            end
        end
    end
    return best
end

function player_has_shiny()
    return shiny_item() ~= nil
end

--- Schnappt sich etwas Glänzendes (Event `steal` von t_steal): ins eigene Inventar und ins Maul, dann flieht er.
local function steal(npc)
    if stolen[npc] then
        return
    end
    local stack = shiny_item()
    if not stack or npc_distance_to_player(npc) > 2.5 then
        return
    end
    local count = math.min(stack.count, 20) -- Gulden: eine Handvoll
    if remove_item(stack.item, count) then
        npc_give_item(npc, stack.item, count)
        npc_hold(npc, stack.item, "socket_mouth")
        stolen[npc] = { item = stack.item, count = count }
        emit("npc_stole", npc, stack.item, count)
    end
end

State "zs_mm_steal" {
    begin = function(npc)
        npc_clear(npc)
        npc_follow_player(npc, 10, 0.8, true)
    end,
    loop = function(npc, seconds)
        if stolen[npc] then
            npc_start_state(npc, "zs_mm_flee")
            return
        end
        if seconds > 12 or not player_has_shiny() then
            npc_start_state(npc, "zs_mm_attack") -- nichts mehr zu holen
            return
        end
        if npc_distance_to_player(npc) <= 1.8 then
            npc_clear(npc)
            npc_turn_to_player(npc)
            npc_play(npc, "steal")
            npc_wait(npc, 1.0)
            -- ohne Clip bzw. Event: nach dem Warten
            if npc_state(npc).commands == 0 then
                steal(npc)
            end
        else
            npc_follow_player(npc, 10, 0.8, true)
        end
    end,
}

on("npc_clip_event", function(npc, event)
    if event == "steal" and state(npc) == "zs_mm_steal" then
        steal(npc)
    end
end)

-- Stirbt ein Dieb, fällt das Gestohlene wieder heraus.
on("npc_killed", function(target)
    local s = stolen[target]
    if s then
        stolen[target] = nil
        npc_drop_item(target, s.item, math.min(s.count, npc_item_count(target, s.item)))
    end
end)

--- Ein Monster eines Spawn-Markers kommt in die Welt (auch nach dem Laden): ruhend oder wach im Revier.
on("npc_spawned", function(npc, spawn, dormant)
    homes[npc] = spawn
    animals[npc] = true
    if dormant then
        npc_start_state(npc, "zs_dormant", spawn)
    else
        npc_start_state(npc, "zs_mm_roam", spawn)
    end
end)

-- `wake` an einen Spawn-Marker weckt alle seine Monster (ein Gatter öffnet sich, eine Nische wird durchsucht).
on("dungeon_event", function(target, action)
    if action == "wake" then
        for _, npc in ipairs(spawn_npcs(target)) do
            wake(npc)
        end
    end
end)

local function sight(npc)
    local n = instance("Npc", npc)
    return (n and n.senses and n.senses.sight) or Perception.sight
end

on("observe_player", function(npc, distance)
    if state(npc) == "zs_dormant" and distance <= sight(npc) * 0.5 then
        wake(npc)
    end
end)

on("assess_noise", function(npc, kind, x, y, z)
    if state(npc) ~= "zs_dormant" then
        return
    end
    local s = npc_state(npc)
    local d = math.sqrt((s.x - x) ^ 2 + (s.y - y) ^ 2 + (s.z - z) ^ 2)
    if d <= ((Perception.noise or {})[kind] or 0) * 0.5 then
        wake(npc)
    end
end)

on("npc_hit", function(attacker, target)
    if target ~= "hero" and homes[target] and npc_dormant(target) then
        npc_set_dormant(target, false) -- getroffen: wach (den Kampf beginnt ai/combat.lua)
    end
end)

-- Wach und ohne Aufgabe (nach einem Kampf, einer Flucht): zurück ins Revier um den Marker.
every(1.0, function()
    for npc, home in pairs(homes) do
        local ok, s = pcall(npc_state, npc)
        if not ok then
            homes[npc] = nil -- nicht (mehr) in dieser Welt
        elseif s.state == "" and not npc_dormant(npc) and fight_state(npc) ~= "dead" then
            npc_start_state(npc, "zs_mm_roam", home)
        end
    end
end)
