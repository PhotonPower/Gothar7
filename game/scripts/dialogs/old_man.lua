-- Der alte Mann handelt mit allerlei Kram (Beispielinhalt M10 Teil C). `trade = true`: nach seinen Zeilen öffnet
-- sich der Handel; geschlossen geht der Dialog weiter.

Info "dia_old_man_hello" {
    npc = "npc_old_man",
    nr = 1,
    description = "Wer bist du?",
    run = function(npc)
        say(npc, "Einer, der schon zu lange hier draußen lebt.")
        say(npc, "Was andere wegwerfen, hebe ich auf. Und was ich aufhebe, verkaufe ich.")
    end,
}

Info "dia_old_man_trade" {
    npc = "npc_old_man",
    nr = 5,
    permanent = true,
    trade = true,
    description = "Zeig mir, was du hast.",
    run = function(npc)
        say(npc, "Schau dich ruhig um. Aber gezahlt wird in Gulden.")
    end,
}
