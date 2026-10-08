-- Plauder-Dialoge der Leonberger („Leonberg lebt“, Entscheidungen L1/L3: eigene Texte, keine Aufträge; Bäckerin
-- und Marktfrau handeln, der Schmied lehrt Schmieden).

Info "dia_leo_smith_hello" {
    npc = "npc_leo_smith",
    nr = 1,
    description = "Viel zu tun?",
    run = function(npc)
        say(npc, "Hufeisen, Nägel, Beschläge für die Torflügel. Ein Schmied hat nie Feierabend.")
        say(npc, "Und wenn doch, sitze ich in der Krummen Gans.")
    end,
}

Info "dia_leo_smith_teach" {
    npc = "npc_leo_smith",
    nr = 5,
    permanent = true,
    description = "Kannst du mir das Schmieden beibringen?",
    run = function(npc)
        say(npc, "Kann ich. Umsonst ist es aber nicht.")
        teach_menu(npc, { { talent = "smithing", level = 1, lp = 10, gulden = 50 } }, {
            ok = "Gut. Halt das Eisen heiß und schlag nicht daneben.",
            lp = "Komm wieder, wenn du mehr gelernt hast.",
            gulden = "Fünfzig Gulden, keinen weniger.",
            known = "Das kannst du doch längst.",
        })
    end,
}

Info "dia_leo_innkeeper_hello" {
    npc = "npc_leo_innkeeper",
    nr = 1,
    permanent = true,
    description = "Was gibt es Neues?",
    run = function(npc)
        say(npc, "Neues? Die Wachen beschweren sich über die Nachtschicht, die Bauern über den Regen.")
        say(npc, "Und alle über mein Bier. Trinken tun sie's trotzdem.")
    end,
}

Info "dia_leo_baker_trade" {
    npc = "npc_leo_baker",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was backst du heute?",
    run = function(npc)
        say(npc, "Brot, frisch aus dem Ofen. Für ein paar Gulden gehört es dir.")
    end,
}

Info "dia_leo_market_trade" {
    npc = "npc_leo_market",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was hast du im Laden?",
    run = function(npc)
        say(npc, "Ein bisschen von allem. Schau, was du brauchst.")
    end,
}

Info "dia_leo_guard_lower_hello" {
    npc = "npc_leo_guard_lower",
    nr = 1,
    permanent = true,
    description = "Alles ruhig am Tor?",
    run = function(npc)
        say(npc, "Solange du keinen Ärger machst, ja.")
    end,
}

Info "dia_leo_guard_upper_hello" {
    npc = "npc_leo_guard_upper",
    nr = 1,
    permanent = true,
    description = "Wer führt hier das Kommando?",
    run = function(npc)
        say(npc, "Ich. Und nachts lasse ich niemanden durch, der hier nichts verloren hat.")
    end,
}

Info "dia_leo_farmer_hello" {
    npc = "npc_leo_farmer",
    nr = 1,
    permanent = true,
    description = "Wie steht das Korn?",
    run = function(npc)
        say(npc, "Besser als letztes Jahr. Wenn die Stadt nur nicht so viel davon wollte.")
    end,
}

Info "dia_leo_citizen_hello" {
    npc = "npc_leo_citizen",
    nr = 1,
    permanent = true,
    description = "Was macht ein Ratsdiener?",
    run = function(npc)
        say(npc, "Laufen. Vom Rathaus zum Markt, vom Markt zum Rathaus. Und zuhören, was die Leute reden.")
    end,
}

-- --- welt Phase 1: Handwerker und Händler (eigene Texte; wer etwas verkauft, handelt mit dem, was er im Inventar hat)
Info "dia_leo_baker_husband_hello" {
    npc = "npc_leo_baker_husband",
    nr = 1,
    permanent = true,
    description = "Du bist der Bäcker?",
    run = function(npc)
        say(npc, "Seit ich denken kann. Um vier am Ofen, um eins im Gasthaus. Dazwischen Mehl.")
        say(npc, "Brot kaufst du bei meiner Frau. Ich backe nur.")
    end,
}

Info "dia_leo_butcher_trade" {
    npc = "npc_leo_butcher",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was hast du heute?",
    run = function(npc)
        say(npc, "Schinken und Würste, alles vom eigenen Schwein. Wer bei mir kauft, kommt satt durch den Winter.")
    end,
}

Info "dia_leo_joiner_hello" {
    npc = "npc_leo_joiner",
    nr = 1,
    permanent = true,
    description = "Was baust du da?",
    run = function(npc)
        say(npc, "Truhen, Bänke, Fensterläden. Und Särge, wenn einer bestellt. Holz fragt nicht, wofür.")
    end,
}

Info "dia_leo_joiner_trade" {
    npc = "npc_leo_joiner",
    nr = 2,
    permanent = true,
    trade = true,
    description = "Verkaufst du Werkzeug?",
    run = function(npc)
        say(npc, "Ein altes Stück habe ich übrig. Gut gepflegt, versteht sich.")
    end,
}

Info "dia_leo_potter_trade" {
    npc = "npc_leo_potter",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Zeig mir deine Töpferwaren.",
    run = function(npc)
        say(npc, "Kannen, Schüsseln, Krüge. Was zerbricht, verkaufe ich dir gern noch einmal.")
    end,
}

Info "dia_leo_goldsmith_trade" {
    npc = "npc_leo_goldsmith",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was hast du an Schmuck?",
    run = function(npc)
        say(npc, "Ringe, ein Amulett, eine Kette. Gute Arbeit hat ihren Preis, und ich arbeite gut.")
    end,
}

Info "dia_leo_cloth_merchant_trade" {
    npc = "npc_leo_cloth_merchant",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was kostet dein Tuch?",
    run = function(npc)
        say(npc, "Wolle aus dem Gäu, gefärbt am Glemsufer. Fass es ruhig an, es hält, was es verspricht.")
    end,
}

Info "dia_leo_tailor_hello" {
    npc = "npc_leo_tailor",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Kannst du mir etwas nähen?",
    run = function(npc)
        say(npc, "Bring mir Tuch und Geduld. Bis dahin verkaufe ich dir, was ich übrig habe.")
    end,
}

Info "dia_leo_herbalist_trade" {
    npc = "npc_leo_herbalist",
    nr = 1,
    permanent = true,
    trade = true,
    description = "Was hast du an Kräutern und Tränken?",
    run = function(npc)
        say(npc, "Salbei, Kamille, Nesseln. Und was man daraus kocht, wenn man es kann.")
        say(npc, "Für den Trank der Stärke musst du tief in den Beutel greifen. Er wirkt dafür ein Leben lang.")
    end,
}

Info "dia_leo_bather_hello" {
    npc = "npc_leo_bather",
    nr = 1,
    permanent = true,
    description = "Was macht ein Bader?",
    run = function(npc)
        say(npc, "Baden, scheren, Zähne ziehen, Wunden flicken. Wer in Leonberg blutet, kommt zu mir.")
    end,
}

Info "dia_leo_bather_heal" {
    npc = "npc_leo_bather",
    nr = 2,
    permanent = true,
    description = "Kannst du mich heilen?",
    run = function(npc)
        say(npc, "Zeig her. Für zwanzig Gulden flicke ich dich zusammen.")
        choice("Hier sind zwanzig Gulden.", function()
            if item_count("it_gulden") >= 20 then
                remove_item("it_gulden", 20)
                set_stat("hp", stat("hp_max"))
                say(npc, "So. Das hält wieder eine Weile.")
            else
                say(npc, "Ohne Geld keine Salbe.")
            end
        end)
        choice("Später vielleicht.", function()
            say(npc, "Wie du willst. Verblute nur nicht vor meiner Tür.")
        end)
    end,
}

Info "dia_leo_bather_trade" {
    npc = "npc_leo_bather",
    nr = 3,
    permanent = true,
    trade = true,
    description = "Verkaufst du Heiltränke?",
    run = function(npc)
        say(npc, "Ein paar habe ich da. Für die Reise.")
    end,
}

Info "dia_leo_merchant_m_hello" {
    npc = "npc_leo_merchant_m",
    nr = 1,
    permanent = true,
    description = "Was führt dich nach Leonberg?",
    run = function(npc)
        say(npc, "Der Handel, was sonst. Tuch nach Stuttgart, Wein zurück. Und der Rat will auch noch seinen Zoll.")
    end,
}

Info "dia_leo_merchant_f_hello" {
    npc = "npc_leo_merchant_f",
    nr = 1,
    permanent = true,
    description = "Schöner Tag heute.",
    run = function(npc)
        say(npc, "Für dich vielleicht. Mein Mann hat schon wieder das halbe Lager verkauft, bevor ich den Preis kannte.")
    end,
}
