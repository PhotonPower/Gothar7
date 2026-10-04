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
