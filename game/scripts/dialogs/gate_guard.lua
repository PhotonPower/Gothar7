-- Dialog der Torwache (Beispielinhalt; Dialoge im Spiel mit M12). Bis dahin per Konsole ausprobierbar:
--   call_info("dia_gate_guard_hello")
Info "dia_gate_guard_hello" {
    npc = "npc_gate_guard",
    important = true,
    condition = function() return not Story.met_gate_guard end,
    run = function()
        print("Torwache: Halt! Wer bist du?")
        print("Du: Nur ein Wanderer auf der Suche nach Arbeit.")
        Story.met_gate_guard = true
    end,
}

Info "dia_gate_guard_work" {
    npc = "npc_gate_guard",
    nr = 2,
    description = "Gibt es hier Arbeit?",
    condition = function() return Story.met_gate_guard end,
    run = function()
        print("Torwache: Frag die Bäuerin am Feld. Die braucht immer Hilfe.")
        Story.quest_farm_work = "running"
    end,
}
