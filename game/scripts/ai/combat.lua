-- Kampf-KI (M11 Teil D, Gothics ZS_Attack): Ein NPC kämpft gegen ein Ziel (den Helden oder ein anderes NPC) - er
-- geht in Reichweite, schlägt (Kombos nach seinem Talent), pariert manchmal, lässt Liegende in Ruhe (K7, K8), gibt
-- auf, wenn das Ziel zu weit weg ist, und flieht bei wenig Leben. Höchstens zwei greifen dasselbe Ziel zugleich an,
-- die anderen warten im Kreis. Werte sind Spielgefühl (Projektinhaber).
CombatAi = {
    give_up_distance = 30,              -- m: weiter weg gibt er auf
    max_attackers = 2,                  -- greifen ein Ziel zugleich an
    wait_distance = 3.5,                -- m: die übrigen warten so weit weg
    parry_chance = { 0.1, 0.3, 0.5 },   -- je Talentstufe: pariert einen Schlag des Ziels
    side_chance = 0.25,                 -- Anteil der Seitenhiebe
    flee_below = { animal = 0.2, coward = 0.5 }, -- Anteil des Lebens: Tiere bzw. Feiglinge fliehen darunter
    cast_distance = 3,                  -- m: Zauberkundige wirken Angriffssprüche erst weiter weg (M12)
    heal_below = 0.3,                   -- Anteil des Lebens: darunter heilen sie sich
}

Fights = {}          -- npc -> Ziel
local attackers = {} -- Ziel -> { npc = true }
local round = {}     -- npc -> Nummer des Kampfes: ein neuer Kampf überlebt das Ende des alten Zustands
local begun = {}     -- npc -> die Nummer, mit der der laufende Kampfzustand begann

--- Wie viele von ihnen noch kämpfen können (Liegende und Tote geben ihren Platz frei).
local function count(t)
    local n = 0
    for npc in pairs(t or {}) do
        local s = fight_state(npc)
        if s ~= "dead" and s ~= "down" then
            n = n + 1
        end
    end
    return n
end

local function leave(npc)
    local target = Fights[npc]
    if target and attackers[target] then
        attackers[target][npc] = nil
    end
    Fights[npc] = nil
end

--- Gegen wen `npc` kämpft ("hero", ein NPC oder nil). Die Engine fragt es für die Kampfmusik (M13).
function fight_target(npc)
    return Fights[npc]
end

--- Wie viele gerade dasselbe Ziel angreifen (höchstens CombatAi.max_attackers).
function fight_attackers(target)
    return count(attackers[target])
end

--- Ein Kampfzustand beginnt (zs_attack, zs_mm_attack, zs_mm_hunt) gegen `target`.
function fight_begin(npc, target)
    if target ~= nil and Fights[npc] ~= target then
        leave(npc)
        Fights[npc] = target
        round[npc] = (round[npc] or 0) + 1
    end
    begun[npc] = round[npc]
end

--- Der Kampfzustand endet: der Kampf ist vorbei, außer ein neuer hat schon begonnen.
function fight_end(npc)
    if begun[npc] == round[npc] then
        leave(npc)
    end
end

--- `npc` kämpft gegen `target` ("hero" oder ein NPC).
function fight(npc, target)
    if fight_state(npc) == "down" or fight_state(npc) == "dead" then
        return
    end
    leave(npc)
    Fights[npc] = target
    round[npc] = (round[npc] or 0) + 1
    npc_start_state(npc, "zs_attack")
end

local function talent(npc)
    local n = instance("Npc", npc)
    local t = n and n.talents or {}
    return math.min(math.max(t.melee_1h or 0, t.melee_2h or 0), 2)
end

--- Welchen Spruch ein Zauberkundiger jetzt wirkt: Heilung unter CombatAi.heal_below seines Lebens, sonst einen
--- Angriffsspruch, solange das Ziel weiter als CombatAi.cast_distance weg ist; nil: keinen.
function caster_spell(npc, distance)
    local n = instance("Npc", npc)
    if not n or not n.spells then
        return nil
    end
    local hp, max = npc_stat(npc, "hp"), npc_stat(npc, "hp_max")
    for _, name in ipairs(n.spells) do
        local s = instance("Spell", name)
        if s and npc_can_cast(npc, name) then
            if s.kind == "self" and s.heal and max > 0 and hp < max * CombatAi.heal_below then
                return { name = name, self = true }
            elseif (s.kind == "projectile" or s.kind == "target") and distance > CombatAi.cast_distance then
                return { name = name }
            end
        end
    end
    return nil
end

local function fleeing(npc)
    local hp, max = npc_stat(npc, "hp"), npc_stat(npc, "hp_max")
    if max <= 0 then
        return false
    end
    if animal(npc) then
        return hp < max * CombatAi.flee_below.animal
    end
    local n = instance("Npc", npc)
    local coward = (n.guild == "farmer" or n.guild == "outcast") and (n.level or 0) <= 3
    return coward and hp < max * CombatAi.flee_below.coward
end

--- Ein Kampfschritt (je Schleife, 0,5 s): "done", wenn der Kampf vorbei ist.
function fight_step(npc)
    local target = Fights[npc]
    if target == nil then
        return "done"
    end
    local them = fight_state(target)
    if them == "dead" or them == "down" then -- K7/K8: Liegende lässt er in Ruhe
        leave(npc)
        return "done"
    end
    local distance = npc_distance(npc, target)
    if distance > CombatAi.give_up_distance then
        leave(npc)
        return "done"
    end
    if fleeing(npc) then
        leave(npc)
        npc_clear(npc)
        if target == "hero" then
            npc_flee(npc, 8)
        else
            npc_flee(npc, 8, target)
        end
        return "done"
    end
    if fight_state(npc) ~= "ready" or npc_casting(npc) then
        return -- mitten in einem Schlag, einer Parade, taumelnd, einem Zauber
    end
    -- Zauberkundige (M12 Teil D, Npc-Feld spells): heilen sich bei wenig Leben, zaubern auf Abstand.
    local spell = caster_spell(npc, distance)
    if spell then
        npc_face(npc, target)
        if spell.self then
            npc_cast_spell(npc, spell.name)
        else
            npc_cast_spell(npc, spell.name, target)
        end
        return
    end
    attackers[target] = attackers[target] or {}
    if not attackers[target][npc] and count(attackers[target]) >= CombatAi.max_attackers then
        -- Warten, bis ein Platz frei wird.
        if distance > CombatAi.wait_distance + 1 then
            if target == "hero" then
                npc_follow_player(npc, 0.5, CombatAi.wait_distance)
            else
                npc_follow_npc(npc, target, CombatAi.wait_distance, 0.5)
            end
        else
            npc_face(npc, target)
        end
        return
    end
    attackers[target][npc] = true
    local reach = npc_reach(npc)
    if distance > reach then -- the bodies touch at about 1.4 m: the full reach counts
        local gait = animal(npc) and animal(npc).chase_gait or "run" -- Tiere: nah im Trab, ab 8 m rennend
        -- Folgen setzt erst 1 m über dem Wunschabstand neu an: so nah, dass es danach sicher in Reichweite ist.
        local near = math.max(0.5, reach - 1.2)
        if target == "hero" then
            npc_follow_player(npc, 0.5, near, gait)
        else
            npc_follow_npc(npc, target, near, 0.5, gait)
        end
        return
    end
    npc_face(npc, target)
    if not animal(npc) and them == "attack" and math.random() < CombatAi.parry_chance[talent(npc) + 1] then
        npc_parry(npc)
    elseif not animal(npc) and math.random() < CombatAi.side_chance then
        npc_attack(npc, math.random() < 0.5 and "left" or "right")
    else
        npc_attack(npc)
    end
end

State "zs_attack" {
    begin = function(npc)
        npc_clear(npc)
        fight_begin(npc)
    end,
    loop = function(npc)
        return fight_step(npc)
    end,
    finish = function(npc)
        fight_end(npc)
    end,
}

-- Getroffen: zurückschlagen (Feiglinge fliehen); Tiere ebenso. Wer schon kämpft, bleibt bei seinem Ziel.
on("npc_hit", function(attacker, target)
    if target == "hero" or Fights[target] or fight_state(target) == "down" or fight_state(target) == "dead" then
        return
    end
    if Summoned and Summoned[target] == attacker then -- ein Versehen des Rufenden (M12): kein Zurückschlagen
        return
    end
    fight(target, attacker)
end)
