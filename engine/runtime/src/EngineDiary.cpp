// The diary (M10 part D; screen since M14 part E, action log: N classic, J modern) with the chapter, the
// quests by status with their entries and the notes by topic. The data lives in Story (game/scripts/lib/
// diary.lua, German texts), so it is saved with the game; the engine shows it in the UI language.

#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
Engine::DiaryData Engine::diaryData() const
{
    DiaryData panel;
    if (!m_scripts)
    {
        return panel;
    }
    const auto entryText = [this](const script::Value& e)
    {
        return uiText("UI_DIARY_ENTRY", {{"day", std::to_string(e["day"].asInteger(0))},
                                         {"time", std::string(e["time"].asString())},
                                         {"text", contentText(e["text"].asString())}});
    };
    const script::Value story = m_scripts->story();
    panel.chapter = uiText("UI_DIARY_CHAPTER", {{"n", std::to_string(story["chapter"].asInteger(1))}});
    if (const script::Table* quests = story["quests"].asTable())
    {
        for (const auto& [name, q] : quests->fields)
        {
            const std::string_view status = q["status"].asString();
            if (status == "none")
            {
                continue;
            }
            DiaryData::Topic quest;
            const script::Instance* def = m_scripts->findInstance("Quest", name);
            quest.name = def != nullptr ? contentText(def->fields["name"].asString()) : name;
            if (const script::Table* entries = q["entries"].asTable())
            {
                for (const script::Value& e : entries->array)
                {
                    quest.entries.push_back(entryText(e));
                }
            }
            (status == "running"   ? panel.running
             : status == "success" ? panel.done
                                   : panel.failed)
                .push_back(std::move(quest));
        }
    }
    if (const script::Table* notes = story["notes"].asTable())
    {
        for (const script::Value& n : notes->array)
        {
            const std::string topic = contentText(n["topic"].asString());
            auto it = std::ranges::find(panel.notes, topic, &DiaryData::Topic::name);
            if (it == panel.notes.end())
            {
                panel.notes.push_back({topic, {}});
                it = panel.notes.end() - 1;
            }
            it->entries.push_back(entryText(n));
        }
    }
    return panel;
}

void Engine::bindDiaryFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"notice", "notice(text: string)",
             "Eine kurze Meldung am Bildschirm (Tagebuch-Einträge, Kapitel, Hinweise).", "Welt",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"expects (text: string)"};
                 }
                 notice(contentText(a[0].asString())); // a literal text of the scripts (translated)
                 return Value();
             }});
    vm.bind({"diary_open", "diary_open(open?: boolean)",
             "Öffnet (bzw. mit `false` schließt) das Tagebuch, wie die Taste log (N bzw. J).", "Welt",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 m_diaryOpen = a.empty() || a[0].asBool();
                 return Value();
             }});
    vm.bind({"chapter_changed",
             "on(\"chapter_changed\", fn(chapter: integer))",
             "Das Kapitel hat gewechselt (set_chapter in lib/diary.lua).",
             "Ereignisse",
             {}});
    vm.bind({"quest_success",
             "on(\"quest_success\", fn(quest: string))",
             "Ein Auftrag ist gelungen (quest_success in lib/diary.lua).",
             "Ereignisse",
             {}});
}
} // namespace g7
