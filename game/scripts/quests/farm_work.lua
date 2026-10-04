-- Aufträge im Testlager (Platzhalter-Geschichte „Bauern und Wache“, M10 Teil E; eigene Texte nach ADR 0008).
-- Im Tagebuch über lib/diary.lua: quest_start, quest_entry, quest_success, quest_fail.

Quest "quest_farm_work" {
    name = "Arbeit im Lager",
    description = "Die Torwache meint, die Bäuerin am Feld brauche Hilfe.",
    topic = "topic_camp",
}

Quest "quest_lunch" { -- Botengang
    name = "Essen für den Holzfäller",
    description = "Die Bäuerin hat mir ein Essensbündel für den Holzfäller mitgegeben. Er arbeitet im Norden des Lagers.",
    topic = "topic_camp",
}

Quest "quest_sword" { -- Beschaffung
    name = "Eine Klinge für die Wache",
    description = "Die Torwache braucht ein Grobes Schwert. Am Amboss hinter dem Tor kann man eines schmieden.",
    topic = "topic_camp",
}

Quest "quest_ring" { -- Konflikt
    name = "Der Ring der Bäuerin",
    description = "Der Bäuerin fehlt ihr Familienring. Sie verdächtigt den alten Mann.",
    topic = "topic_camp",
}

--- Sind alle drei Aufträge erledigt, ist die Arbeit im Lager getan: Kapitel 2.
function camp_story_check()
    if quest_status("quest_farm_work") == "running" and quest_status("quest_lunch") == "success"
        and quest_status("quest_sword") == "success" and quest_status("quest_ring") == "success" then
        quest_success("quest_farm_work", "Im Lager kennt man mich jetzt. Zeit, weiterzuziehen.")
        set_chapter(2, "Weiter nach Leonberg")
    end
end
