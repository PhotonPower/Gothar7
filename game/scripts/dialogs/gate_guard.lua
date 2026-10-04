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

-- Geschichte (M10 Teil E): eine Klinge besorgen; den Ring-Dieb melden.
Info "dia_gate_guard_blades" {
    npc = "npc_gate_guard",
    nr = 3,
    description = "Ich höre, eure Klingen sind stumpf?",
    condition = function()
        return quest_status("quest_lunch") == "success" and quest_status("quest_sword") == "none"
    end,
    run = function(npc)
        say(npc, "Stumpf ist gar kein Wort. Mit meinem Schwert schneide ich nicht mal Brot.")
        say(npc, "Bring mir ein Grobes Schwert. Hinter dem Tor steht ein Amboss, Rohlinge liegen in der Truhe daneben.")
        quest_start("quest_sword")
    end,
}

Info "dia_gate_guard_sword" {
    npc = "npc_gate_guard",
    nr = 4,
    description = "Hier ist ein Grobes Schwert.",
    condition = function()
        return quest_status("quest_sword") == "running" and item_count("it_sword_crude") > 0
    end,
    run = function(npc)
        remove_item("it_sword_crude")
        npc_give_item(npc, "it_sword_crude")
        say(npc, "Nicht schön, aber scharf. Genau was ich brauche.")
        give_item("it_gulden", 30)
        add_xp(100)
        quest_success("quest_sword", "Die Wache hat ihr Schwert.")
        camp_story_check()
    end,
}

Info "dia_gate_guard_thief" {
    npc = "npc_gate_guard",
    nr = 6,
    description = "Der alte Mann hat den Ring der Bäuerin.",
    condition = function()
        return quest_status("quest_ring") == "running" and npc_item_count("npc_old_man", "it_ring_family") > 0
    end,
    run = function(npc)
        say(npc, "So, so. Den alten Kauz hatte ich schon länger im Auge.")
        say(npc, "Ich hol mir das Ding. Warte hier.")
        npc_take_item("npc_old_man", "it_ring_family")
        say(npc, "Da. Bring ihn der Bäuerin, sie wird sich freuen.")
        give_item("it_ring_family")
        quest_entry("quest_ring", "Die Wache hat dem alten Mann den Ring abgenommen. Ich soll ihn der Bäuerin bringen.")
    end,
}
