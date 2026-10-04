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

-- Geschichte (M10 Teil E): der Ring der Bäuerin. Drei Wege: abkaufen, stehlen (pickpocket_item), die Wache holen.
Info "dia_old_man_ring" {
    npc = "npc_old_man",
    nr = 2,
    description = "Du hast einen Ring, der dir nicht gehört.",
    condition = function()
        return quest_status("quest_ring") == "running" and npc_item_count("npc_old_man", "it_ring_family") > 0
    end,
    run = function(npc)
        say(npc, "Gefunden hab ich ihn. Im Gras. Was man findet, gehört einem, oder?")
        choice("Ich kaufe ihn dir ab. (30 Gulden)", function()
            if item_count("it_gulden") >= 30 then
                remove_item("it_gulden", 30)
                npc_give_item(npc, "it_gulden", 30)
                npc_take_item(npc, "it_ring_family")
                give_item("it_ring_family")
                say(npc, "Ein gutes Geschäft. Für mich.")
                quest_entry("quest_ring", "Ich habe dem alten Mann den Ring abgekauft.")
            else
                say(npc, "Ohne Gulden kein Ring, mein Freund.")
            end
        end)
        choice("Gib ihn her, oder ich hole die Wache.", function()
            say(npc, "Hol sie doch. Mal sehen, wem sie glaubt.")
            quest_entry("quest_ring", "Der alte Mann will den Ring nicht hergeben. Vielleicht hilft die Torwache.")
        end)
        choice("Schon gut.", function() end)
    end,
}
