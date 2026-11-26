-- Kellermutter, der Boss des Dungeons (docs/design/monsters.md, Werte in Creatures.kellermutter). Drei Phasen nach
-- ihrem Leben: 1 Sensen und Zangen (ai/combat.lua); 2 (unter phases.rage) tobt sie einmal (t_rage) und ruft danach
-- alle brood.call_seconds ihre Brut aus den Wänden (t_call, Clip-Event spawn_brood); 3 (unter phases.guard)
-- schleppt sie sich zum Hort, schützt ihn (s_guard) und ihre Schwachstelle am Brutsack ist offen (npc_set_weak).
-- Die Mechanik (Schwachstelle, Taumeln, Aktionen) ist C++; hier steht nur der Ablauf.

local phase = {}    -- Kellermutter -> 1, 2, 3
local lastCall = {} -- Kellermutter -> Spielsekunden des letzten Rufs
local brood = {}    -- Kellermutter -> { Namen ihrer Brut }
local hoard = {}    -- Kellermutter -> ihr Spawn-Marker (der Hort)
local clock = 0     -- Sekunden seit dem Laden (every)

local function boss(npc)
    local c = animal(npc)
    return c and c.phases and c or nil
end

--- Wie viele ihrer Brut noch leben.
function brood_alive(npc)
    local n = 0
    for _, name in ipairs(brood[npc] or {}) do
        local ok, _ = pcall(npc_state, name)
        if ok and fight_state(name) ~= "dead" then
            n = n + 1
        end
    end
    return n
end

--- Die Phase der Kellermutter (1 bis 3).
function boss_phase(npc)
    return phase[npc] or 1
end

State "zs_km_rage" {
    begin = function(npc)
        npc_clear(npc)
        npc_play(npc, "rage")
        npc_wait(npc, 1.6)
    end,
    loop = function(npc)
        npc_start_state(npc, "zs_km_call")
    end,
}

State "zs_km_call" {
    begin = function(npc)
        npc_clear(npc)
        npc_turn_to_player(npc)
        npc_play(npc, "call")
        npc_wait(npc, 1.4)
        lastCall[npc] = clock
    end,
    loop = function(npc)
        fight(npc, "hero")
    end,
}

State "zs_km_guard" {
    begin = function(npc)
        npc_clear(npc)
        if hoard[npc] then
            npc_goto(npc, hoard[npc])
        end
        npc_play(npc, "guard")
    end,
    loop = function(npc)
        -- Kommt der Held an den Hort, schlägt sie zu und kehrt danach zurück (npc_hit unten).
        local s, h = npc_state(npc), where()
        if math.sqrt((s.x - h.x) ^ 2 + (s.z - h.z) ^ 2) < 6 then
            fight(npc, "hero")
        end
    end,
}

--- Wechselt die Phase nach dem Leben (nach jedem Treffer).
local function check_phase(npc)
    local c = boss(npc)
    if not c or fight_state(npc) == "dead" then
        return
    end
    local share = npc_stat(npc, "hp") / math.max(1, npc_stat(npc, "hp_max"))
    local now = phase[npc] or 1
    if share <= c.phases.guard and now < 3 then
        phase[npc] = 3
        npc_set_weak(npc, true)
        npc_start_state(npc, "zs_km_guard")
    elseif share <= c.phases.rage and now < 2 then
        phase[npc] = 2
        npc_start_state(npc, "zs_km_rage")
    end
end

on("npc_hit", function(attacker, target)
    if target ~= "hero" and boss(target) then
        check_phase(target)
    end
end)

on("npc_spawned", function(npc, spawn)
    if boss(npc) then
        hoard[npc] = spawn
    end
end)

--- Event spawn_brood von t_call: ihre Brut kommt rings um sie aus den Wänden und greift an.
on("npc_clip_event", function(npc, event)
    local c = boss(npc)
    if event ~= "spawn_brood" or not c then
        return
    end
    local b = c.brood
    local free = math.min(b.count, b.max - brood_alive(npc))
    local s = npc_state(npc)
    brood[npc] = brood[npc] or {}
    for i = 1, free do
        local name = insert_npc(b.npc, hoard[npc] or s.at)
        local angle = (i / free) * 2 * math.pi
        npc_teleport(name, s.x + 3.5 * math.cos(angle), s.y, s.z + 3.5 * math.sin(angle))
        table.insert(brood[npc], name)
        fight(name, "hero")
    end
end)

-- Phase 2: ruft wieder, sobald call_seconds vorbei sind und sie gerade kämpft.
every(1.0, function()
    clock = clock + 1
    for npc, p in pairs(phase) do
        local c = boss(npc)
        local ok, s = pcall(npc_state, npc)
        if not ok then
            phase[npc] = nil
        elseif p == 2 and c and (s.state == "zs_attack" or s.state == "zs_mm_attack") and clock - (lastCall[npc] or 0) >= c.brood.call_seconds then
            npc_start_state(npc, "zs_km_call")
        elseif p == 3 and s.state == "" and fight_state(npc) ~= "dead" then
            npc_start_state(npc, "zs_km_guard") -- nach einem Kampf zurück zum Hort
        end
    end
end)
