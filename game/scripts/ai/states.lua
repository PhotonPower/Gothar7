-- Zustände der NPCs (M9 Teil B, Gothics ZS_): begin(npc, at) füllt die Befehlsliste, loop(npc, sekunden) läuft
-- alle halbe Sekunde, solange die Liste leer ist ("done" beendet den Zustand, dann greift wieder der Tagesablauf),
-- finish(npc) räumt auf. Werte und Abläufe sind Inhalt; die Engine kennt nur die Befehle (docs/script-api.md).

local function idle_now_and_then(npc, seconds)
    -- Gelegentlich umsehen oder kratzen, wie Gothics Stehende.
    if math.random() < 0.04 then
        npc_play(npc, math.random() < 0.5 and "idle_look" or "idle_scratch")
        npc_wait(npc, 3)
        npc_stop(npc)
    end
end

--- Kam der NPC nicht zu seiner Tätigkeit (Weg versperrt, npc_blocked), beginnt der Tagesablauf den Zustand nach
--- einer Weile neu (Gothic: die Routine versucht es wieder).
local function retry(npc, seconds)
    if npc_state(npc).ambient == "" and seconds > 5 then
        return "done"
    end
end

--- Wache stehen am Wegpunkt, in dessen Richtung.
State "zs_stand_guarding" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_turn(npc, at)
        npc_play(npc, "guard")
    end,
    loop = retry,
}

-- Ein Mob benutzen (npc_use_mob, wie Gothics AI_UseMob); ohne freien Platz die Tätigkeit wie bisher am
-- Freepoint bzw. Wegpunkt. Wer sitzt oder liegt, bleibt, bis der Tagesablauf weiterzieht.
local without_mob = {} -- npc -> true: kein Platz frei, die Ersatz-Tätigkeit läuft

local function mob_or(npc, seconds, fallback)
    local s = npc_state(npc)
    if s.mob ~= "" or s.commands > 0 then
        return nil -- sitzt, liegt oder ist auf dem Weg
    end
    if not without_mob[npc] then
        without_mob[npc] = true
        fallback(npc)
        return nil
    end
    return retry(npc, seconds)
end

local function at_mob(mob, loop, freepoint, ambient, item)
    return {
        begin = function(npc, at)
            without_mob[npc] = nil
            npc_goto(npc, at)
            npc_use_mob(npc, mob, 15, loop)
        end,
        loop = function(npc, seconds)
            return mob_or(npc, seconds, function(n)
                if freepoint then
                    npc_goto_freepoint(n, freepoint, 15)
                end
                npc_play(n, ambient, item)
            end)
        end,
        finish = function(npc)
            without_mob[npc] = nil
        end,
    }
end

--- Schlafen (Entscheidung Projektinhaber): zuerst im eigenen Bett (Besitzer bzw. im Haus des Schlafplatzes), sonst
--- in einem freien Bett in der Nähe, sonst am Wegpunkt am Boden.
State "zs_sleep" {
    begin = function(npc, at)
        without_mob[npc] = nil
        npc_goto(npc, at)
        npc_use_mob(npc, "bed", 12)
    end,
    loop = function(npc, seconds)
        return mob_or(npc, seconds, function(n)
            npc_play(n, "sleep_ground")
        end)
    end,
    finish = function(npc)
        without_mob[npc] = nil
    end,
}

State "zs_sit_table" (at_mob("table", nil, "SIT", "sit_ground"))                -- am Tisch sitzen
State "zs_drink_table" (at_mob("table", "drink", "DRINK", "drink_mug", "it_mug")) -- am Tisch trinken (Krug)
State "zs_talk_table" (at_mob("table", "talk", "SMALLTALK", "talk_a"))          -- am Tisch reden
State "zs_sit_bench" (at_mob("bench", nil, "SIT", "sit_ground"))                -- auf der Bank sitzen
State "zs_smith_anvil" (at_mob("anvil", nil, "REPAIR", "repair_kneel"))         -- am Amboss schmieden

--- Sich an einen freien Sitzplatz in der Nähe setzen; ohne einen stehen bleiben.
State "zs_sit_campfire" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_goto_freepoint(npc, "SIT", 12)
        npc_play(npc, "sit_ground")
    end,
    loop = retry,
}

--- Stehend am Feuer wärmen.
State "zs_campfire" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_goto_freepoint(npc, "CAMPFIRE", 12)
        npc_play(npc, "campfire_warm")
    end,
    loop = retry,
}

--- Holz hacken an einem Hackplatz.
State "zs_chop_wood" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_goto_freepoint(npc, "CHOP", 15)
        npc_play(npc, "chop_wood", "it_axe")
    end,
    loop = retry,
}

--- Fegen: ein Stück fegen, dann weiter zum nächsten Fleck.
State "zs_sweep" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_goto_freepoint(npc, "SWEEP", 15)
        npc_play(npc, "sweep", "it_broom")
    end,
    loop = retry,
}

--- Herumstehen am Wegpunkt (Vorgabe, wenn nichts anderes passt).
State "zs_stand" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_turn(npc, at)
    end,
    loop = idle_now_and_then,
}

--- Kurz umsehen, dann weiter im Tagesablauf (z. B. als Reaktion auf ein Geräusch, npc_start_state).
State "zs_look_around" {
    begin = function(npc)
        npc_play(npc, "idle_look")
    end,
    loop = function(npc, seconds)
        if seconds > 3 then
            return "done"
        end
    end,
}

-- Tätigkeiten an welts Freepoints in Leonberg (Leonberg lebt). Jeweils: zum Ort, zum nächsten freien Freepoint
-- des Typs, die Tagesablauf-Animation; ohne freien Platz stehen sie am Wegpunkt.
local function at_freepoint(kind, ambient, item)
    return {
        begin = function(npc, at)
            npc_goto(npc, at)
            npc_goto_freepoint(npc, kind, 15)
            npc_play(npc, ambient, item)
        end,
        loop = retry,
    }
end

State "zs_repair" (at_freepoint("REPAIR", "repair_kneel"))   -- ausbessern, kniend (Schmied, Handwerker)
State "zs_harvest" (at_freepoint("HARVEST", "harvest"))      -- ernten (Bauer)
State "zs_smalltalk" (at_freepoint("SMALLTALK", "talk_a"))   -- plaudern
State "zs_drink" (at_freepoint("DRINK", "drink_mug", "it_mug")) -- aus dem Krug trinken
State "zs_lean" (at_freepoint("LEAN", "lean_wall"))          -- an der Wand lehnen (abends, nachts vor dem Haus)
State "zs_sit" (at_freepoint("SIT", "sit_ground"))           -- sitzen (bis M11 am Boden neben der Bank)

--- Am Laden stehen und sich ab und zu umsehen (Händler).
State "zs_stand_shop" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_goto_freepoint(npc, "STAND", 15)
    end,
    loop = function(npc, seconds)
        if math.random() < 0.05 then
            npc_play(npc, "idle_look")
            npc_wait(npc, 3)
            npc_stop(npc)
        end
    end,
}
