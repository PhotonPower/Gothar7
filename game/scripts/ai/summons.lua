-- Beschworene Wesen (M12 Teil C2, Entscheidung Z8): Ein vom Helden gerufenes Tier folgt ihm und kämpft gegen sein
-- Ziel bzw. gegen jeden, der ihn angreift; nach seiner Zeit verschwindet es (Engine). Werte sind Spielgefühl.
SummonAi = {
    follow_distance = 2.5, -- m: so nah folgt es dem Helden
    guard_radius = 15,     -- m: Angreifer des Helden bis so weit weg bemerkt es
}

Summoned = {} -- npc -> wer es rief ("hero")

--- Wen es angreifen soll: das Ziel des Helden, sonst wer den Helden angreift.
local function enemy(npc)
    local target = hero_target()
    if target and target ~= npc and not Summoned[target] then
        return target
    end
    for attacker, victim in pairs(Fights) do
        if victim == "hero" and not Summoned[attacker] and npc_distance(npc, attacker) <= SummonAi.guard_radius then
            local s = fight_state(attacker)
            if s ~= "dead" and s ~= "down" then
                return attacker
            end
        end
    end
    return nil
end

State "zs_summoned" {
    begin = function(npc)
        npc_clear(npc)
    end,
    loop = function(npc)
        if npc_distance(npc, "hero") > SummonAi.follow_distance + 1.5 then
            npc_follow_player(npc, 0.5, SummonAi.follow_distance, true)
        else
            npc_face(npc, "hero")
        end
    end,
}

on("npc_summoned", function(npc, caster)
    Summoned[npc] = caster
    set_routine(npc, "")
    npc_clear(npc)
    npc_start_state(npc, "zs_summoned")
end)

on("npc_vanished", function(npc)
    Summoned[npc] = nil
end)

-- Zweimal je Sekunde: kämpfen, wenn es einen Feind gibt, sonst folgen.
every(0.5, function()
    for npc in pairs(Summoned) do
        local s = fight_state(npc)
        if s == "dead" then
            Summoned[npc] = nil
        elseif s ~= "down" then
            local state = npc_state(npc).state
            if state ~= "zs_attack" then
                local e = enemy(npc)
                if e then
                    fight(npc, e)
                elseif state ~= "zs_summoned" then
                    npc_start_state(npc, "zs_summoned")
                end
            end
        end
    end
end)

-- Furcht (M12, Z6): Der Getroffene läuft `seconds` Sekunden vor dem Zaubernden weg; ein Kampf endet damit.
local feared = {} -- npc -> { seconds, caster }

State "zs_fear" {
    begin = function(npc)
        local f = feared[npc] or { seconds = 10, caster = "hero" }
        npc_clear(npc)
        npc_flee(npc, f.seconds, f.caster ~= "hero" and f.caster or nil)
    end,
    loop = function(npc)
        feared[npc] = nil -- die Flucht (npc_flee) lief die ganze Zeit; danach ist es vorbei
        return "done"
    end,
}

on("npc_feared", function(npc, caster, seconds)
    feared[npc] = { seconds = seconds, caster = caster }
    npc_start_state(npc, "zs_fear")
end)
