-- Brunnenmolch (docs/design/monsters.md, Werte in Creatures.brunnenmolch): schwimmt nicht (Projektinhaber
-- 2026-11-24). Er lauert halb untergetaucht im seichten Wasser (zs_dormant am Spawn-Marker), taucht auf, wenn er den
-- Helden hört (ai/monsters.lua, blind: nur Hören), schnappt bzw. beißt (ai/combat.lua; nach oben schnappen ist C++)
-- und geht nach dem Kampf zurück an sein Wasser, wo er wieder untersinkt (t_submerge).

local waters = {} -- Brunnenmolch -> sein Spawn-Marker (das Wasser)

local function newt(npc)
    local c = animal(npc)
    return c and c.returns and c or nil
end

--- Setzt das Wasser eines Brunnenmolchs (beim Spawn von selbst: sein Marker).
function newt_water(npc, at)
    if newt(npc) then
        waters[npc] = at
    end
end

on("npc_spawned", function(npc, spawn)
    newt_water(npc, spawn)
end)

State "zs_bm_return" {
    begin = function(npc, at)
        npc_clear(npc)
        npc_goto(npc, at)
        npc_play(npc, "submerge")
        npc_wait(npc, 1.2)
    end,
    loop = function(npc)
        npc_set_dormant(npc, true)
        npc_start_state(npc, "zs_dormant", waters[npc])
    end,
}

-- Wach und ohne Kampf: zurück ins Wasser.
every(1.0, function()
    for npc, at in pairs(waters) do
        local ok, s = pcall(npc_state, npc)
        if not ok then
            waters[npc] = nil
        elseif not npc_dormant(npc) and fight_state(npc) ~= "dead" and (s.state == "" or s.state == "zs_mm_roam") then
            npc_start_state(npc, "zs_bm_return", at)
        end
    end
end)
