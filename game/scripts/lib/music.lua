-- Stinger der Musik (M13 Teil C, Entscheidung Projektinhaber): kurze Klänge auf dem nächsten Schlag über der
-- Musik, wenn ein Auftrag gelingt, der Held eine Stufe aufsteigt, niedergeht oder ein neues Kapitel beginnt.
-- Die Stinger selbst stehen in data/music.toml.

on("quest_success", function(quest)
    music_stinger("quest")
end)

on("level_up", function(level)
    music_stinger("level_up")
end)

on("chapter_changed", function(n)
    music_stinger("chapter")
end)

-- Der Held stirbt nicht (K8), er geht nieder und steht wieder auf; das ist sein Stinger "death".
local function hero_down(npc)
    if npc == "hero" then
        music_stinger("death")
    end
end
on("npc_knocked_out", hero_down)
on("npc_killed", hero_down)
