-- Dialog der Torwache (Beispielinhalt M10). Ansprechen: Fokus auf die Wache, Aktionstaste; Konsole: talk("npc_gate_guard").
-- run(npc) reiht Zeilen ein: say(npc, …) die Wache, say("hero", …) der Held; choice(text, fn) bietet Antworten an.

Info "dia_gate_guard_hello" {
    npc = "npc_gate_guard",
    important = true, -- spricht den Helden von sich aus an
    approach = true,  -- und kommt dafür auf ihn zu
    condition = function()
        return not Story.met_gate_guard
    end,
    run = function(npc)
        say(npc, "Moment mal. Dich hab ich hier noch nie gesehen.")
        say("hero", "Ich bin nur auf der Durchreise und suche Arbeit.")
        say(npc, "Arbeit gibt's genug. Aber hier im Lager benimmst du dich, verstanden?")
        Story.met_gate_guard = true
    end,
}

Info "dia_gate_guard_work" {
    npc = "npc_gate_guard",
    nr = 2,
    description = "Gibt es hier Arbeit?",
    condition = function()
        return Story.met_gate_guard
    end,
    run = function(npc)
        say(npc, "Die Bäuerin draußen am Feld sucht immer helfende Hände.")
        choice("Dann gehe ich gleich zu ihr.", function()
            say(npc, "Gut. Sag ihr, die Wache schickt dich.")
            quest_start("quest_farm_work")
        end)
        choice("Feldarbeit ist nichts für mich.", function()
            say(npc, "Dann wirst du hier nicht alt, Fremder.")
        end)
    end,
}

Info "dia_gate_guard_camp" {
    npc = "npc_gate_guard",
    nr = 5,
    permanent = true, -- immer wieder fragbar
    description = "Was ist das hier für ein Lager?",
    run = function(npc)
        say(npc, "Bauern, ein paar Holzfäller und wir, die auf alles aufpassen.")
        say(npc, "Nachts kommen die Wölfe bis an den Zaun. Bleib am Feuer, wenn's dunkel wird.")
    end,
}
