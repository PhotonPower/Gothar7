-- Der Vogt auf dem Schloss (F1: mit Dialogen; eigene Texte, keine Aufträge) und kurze Worte des Gesindes.

Info "dia_leo_vogt_hello" {
    npc = "npc_leo_vogt",
    nr = 1,
    description = "Wer seid Ihr?",
    run = function(npc)
        say(npc, "Reinhard, Vogt des Grafen. Ich spreche hier Recht, treibe die Abgaben ein und halte die Mauern.")
        say(npc, "Und du stehst in meinem Schloss. Also mach es kurz.")
    end,
}

Info "dia_leo_vogt_town" {
    npc = "npc_leo_vogt",
    nr = 2,
    permanent = true,
    description = "Wie steht es um die Stadt?",
    run = function(npc)
        say(npc, "Die Händler klagen über die Zölle, die Bauern über die Zehnten. So war es immer.")
        say(npc, "Solange die Tore halten und der Markt läuft, ist Leonberg gut regiert.")
    end,
}

Info "dia_leo_vogt_cellar" {
    npc = "npc_leo_vogt",
    nr = 3,
    description = "Was liegt unter dem Schloss?",
    run = function(npc)
        say(npc, "Ein Weinkeller und das Verlies. Mehr nicht.")
        say(npc, "Und wenn die Mägde von Geräuschen hinter dem großen Fass reden, dann ist das der Wein, der arbeitet.")
    end,
}

Info "dia_leo_castle_cook_hello" {
    npc = "npc_leo_castle_cook",
    nr = 1,
    permanent = true,
    description = "Was kocht Ihr?",
    run = function(npc)
        say(npc, "Für den Vogt Braten, für die Wachen Grütze. Und für dich nichts, solange du mir im Weg stehst.")
    end,
}

Info "dia_leo_castle_scribe_hello" {
    npc = "npc_leo_castle_scribe",
    nr = 1,
    permanent = true,
    description = "Was schreibst du da?",
    run = function(npc)
        say(npc, "Abgaben, Urteile, Briefe an den Grafen. Was der Vogt sagt, steht am Abend hier.")
    end,
}
