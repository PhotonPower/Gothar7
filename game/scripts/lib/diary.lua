-- Tagebuch und Kapitel (M10 Teil D): Aufträge (laufend, erfolgreich, gescheitert) mit Einträgen, Notizen nach
-- Themen, das Kapitel. Alles steht in Story und wird mit dem Spielstand gespeichert; das Fenster „Tagebuch“
-- (Aktion log: N bzw. J) zeigt es. Aufträge sind `Quest`-Instanzen (name, description, topic).

local function stamp()
    local w = where()
    return { day = w.day, time = w.time }
end

local function quests()
    Story.quests = Story.quests or {}
    return Story.quests
end

local function quest(name)
    if not instance("Quest", name) then
        error("no Quest " .. tostring(name))
    end
    local q = quests()[name]
    if not q then
        q = { status = "none", entries = {} }
        quests()[name] = q
    end
    return q
end

local function add(q, text)
    if text then
        local s = stamp()
        q.entries[#q.entries + 1] = { day = s.day, time = s.time, text = text }
    end
end

--- Beginnt einen Auftrag (einmal; später ohne Wirkung) mit einem ersten Eintrag.
function quest_start(name, text)
    local q = quest(name)
    if q.status ~= "none" then
        return false
    end
    q.status = "running"
    add(q, text or instance("Quest", name).description)
    notice("Neuer Auftrag: " .. instance("Quest", name).name)
    return true
end

--- Ein weiterer Eintrag zu einem Auftrag.
function quest_entry(name, text)
    add(quest(name), text)
    notice("Tagebuch: " .. instance("Quest", name).name)
end

--- Schließt einen laufenden Auftrag ab.
function quest_success(name, text)
    local q = quest(name)
    q.status = "success"
    add(q, text)
    notice("Auftrag erledigt: " .. instance("Quest", name).name)
end

--- Ein Auftrag ist gescheitert.
function quest_fail(name, text)
    local q = quest(name)
    q.status = "failed"
    add(q, text)
    notice("Auftrag gescheitert: " .. instance("Quest", name).name)
end

--- "none", "running", "success" oder "failed".
function quest_status(name)
    local q = quests()[name]
    return q and q.status or "none"
end

--- Eine Notiz unter einem Thema (z. B. "Das Lager", "Händler").
function note(topic, text)
    Story.notes = Story.notes or {}
    local s = stamp()
    Story.notes[#Story.notes + 1] = { topic = topic, day = s.day, time = s.time, text = text }
    notice("Tagebuch: " .. topic)
end

--- Das Kapitel (1 zu Beginn).
function chapter()
    return Story.chapter or 1
end

--- Wechselt das Kapitel: Ereignis chapter_changed(n), die Inhalte tauschen darauf Tagesabläufe und Infos.
function set_chapter(n, title)
    Story.chapter = n
    notice(title and string.format("Kapitel %d: %s", n, title) or string.format("Kapitel %d", n))
    emit("chapter_changed", n)
end
