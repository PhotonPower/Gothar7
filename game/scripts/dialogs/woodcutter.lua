-- Der Holzfäller zeigt dem Helden, wie man zupackt (Beispielinhalt M10 Teil C: Lehrer). Seine Angebote kosten
-- Lernpunkte und ein paar Gulden (data/teaching.lua).

local offers = {
    { attribute = "str", amount = 1, lp = 1, gulden = 5 },
    { attribute = "str", amount = 5, lp = 5, gulden = 25 },
}

local replies = {
    ok = "So. Merkst du's? Mehr Kraft im Arm.",
    lp = "Dafür fehlt dir noch die Erfahrung.",
    gulden = "Umsonst ist das nicht, Freund.",
    limit = "Mehr kann ich dir da nicht beibringen.",
}

Info "dia_woodcutter_hello" {
    npc = "npc_woodcutter",
    nr = 1,
    description = "Harte Arbeit, das Holzhacken?",
    run = function(npc)
        say(npc, "Hart? Man muss nur wissen, wo man ansetzt.")
        say(npc, "Wenn du willst, zeig ich dir, wie man richtig zupackt.")
        Story.met_woodcutter = true
    end,
}

Info "dia_woodcutter_teach" {
    npc = "npc_woodcutter",
    nr = 5,
    permanent = true,
    description = "Bring mir bei, kräftiger zuzupacken.",
    condition = function()
        return Story.met_woodcutter
    end,
    run = function(npc)
        say(npc, "Dann zeig mal, was in dir steckt.")
        teach_menu(npc, offers, replies)
    end,
}

-- Geschichte (M10 Teil E): der Botengang der Bäuerin.
Info "dia_woodcutter_lunch" {
    npc = "npc_woodcutter",
    nr = 2,
    description = "Die Bäuerin schickt dir dein Essen.",
    condition = function()
        return quest_status("quest_lunch") == "running" and item_count("it_lunch") > 0
    end,
    run = function(npc)
        remove_item("it_lunch")
        npc_give_item(npc, "it_lunch")
        say(npc, "Ah, endlich! Mir hängt der Magen schon in den Kniekehlen.")
        say(npc, "Hier, für den Weg. Und sag der Wache, dass sie sich um ihre stumpfen Klingen kümmern soll.")
        give_item("it_gulden", 10)
        add_xp(50)
        quest_success("quest_lunch", "Der Holzfäller hat sein Essen. Er meint, die Wache habe Ärger mit stumpfen Klingen.")
        camp_story_check()
    end,
}
