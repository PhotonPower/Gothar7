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

--- Am Wegpunkt am Boden schlafen (Betten benutzen NPCs mit M11).
State "zs_sleep" {
    begin = function(npc, at)
        npc_goto(npc, at)
        npc_play(npc, "sleep_ground")
    end,
    loop = retry,
}

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
