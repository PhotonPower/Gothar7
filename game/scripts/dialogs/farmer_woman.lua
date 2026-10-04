-- Die Bäuerin (Platzhalter-Geschichte M10 Teil E): gibt den Botengang und den Ring-Auftrag.

Info "dia_farmer_woman_sent" {
    npc = "npc_farmer_woman",
    nr = 1,
    description = "Die Wache schickt mich. Du brauchst Hilfe?",
    condition = function()
        return quest_status("quest_farm_work") == "running"
    end,
    run = function(npc)
        say(npc, "Hilfe kann ich immer brauchen. Fang mit etwas Leichtem an.")
        say(npc, "Der Holzfäller vergisst über der Arbeit das Essen. Bring ihm dieses Bündel.")
        give_item("it_lunch")
        quest_start("quest_lunch")
    end,
}

Info "dia_farmer_woman_sad" {
    npc = "npc_farmer_woman",
    nr = 2,
    description = "Du siehst bedrückt aus.",
    condition = function()
        return quest_status("quest_lunch") == "success"
    end,
    run = function(npc)
        say(npc, "Mein Ring ist fort. Der Ring meiner Mutter, das Einzige, was mir von ihr geblieben ist.")
        say("hero", "Hast du einen Verdacht?")
        say(npc, "Der alte Mann schleicht ständig um meine Hütte. Aber beweisen kann ich nichts.")
        quest_start("quest_ring")
    end,
}

Info "dia_farmer_woman_ring" {
    npc = "npc_farmer_woman",
    nr = 3,
    description = "Hier ist dein Ring.",
    condition = function()
        return quest_status("quest_ring") == "running" and item_count("it_ring_family") > 0
    end,
    run = function(npc)
        remove_item("it_ring_family")
        npc_give_item(npc, "it_ring_family")
        say(npc, "Tatsächlich, das ist er! Wie soll ich dir das je danken?")
        say(npc, "Viel habe ich nicht, aber nimm das hier.")
        give_item("it_gulden", 20)
        add_xp(100)
        quest_success("quest_ring", "Die Bäuerin hat ihren Ring zurück.")
        camp_story_check()
    end,
}

Info "dia_farmer_woman_work" {
    npc = "npc_farmer_woman",
    nr = 9,
    permanent = true,
    description = "Wie läuft's auf dem Feld?",
    run = function(npc)
        say(npc, "Der Boden ist steinig, aber das Korn kommt. Wenn nur die Wölfe nicht wären.")
    end,
}
