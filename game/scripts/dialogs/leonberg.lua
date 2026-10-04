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
