// The diary (M10 part D): window "Tagebuch" (action log: N classic, J modern) with the chapter, the quests by
// status with their entries and the notes by topic. The data lives in Story (game/scripts/lib/diary.lua), so
// it is saved with the game; the engine only shows it.

#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
namespace
{
std::string entryText(const script::Value& e)
{
    return std::format("Tag {}, {}: {}", e["day"].asInteger(0), e["time"].asString(), e["text"].asString());
}
} // namespace

ui::DiaryPanel Engine::diaryPanelData() const
{
    ui::DiaryPanel panel;
    if (!m_scripts)
    {
        return panel;
    }
    const script::Value story = m_scripts->story();
    panel.chapter = std::format("Kapitel {}", story["chapter"].asInteger(1));
    if (const script::Table* quests = story["quests"].asTable())
    {
        for (const auto& [name, q] : quests->fields)
        {
            const std::string_view status = q["status"].asString();
            if (status == "none")
            {
                continue;
            }
            ui::DiaryPanel::Quest quest;
            const script::Instance* def = m_scripts->findInstance("Quest", name);
            quest.name = def != nullptr ? std::string(def->fields["name"].asString()) : name;
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
            const std::string topic(n["topic"].asString());
            auto it = std::ranges::find(panel.notes, topic, &ui::DiaryPanel::Quest::name);
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

void Engine::diaryUi()
{
    if (!m_diaryOpen)
    {
        return;
    }
    ui::DiaryPanel panel = diaryPanelData();
    panel.open = true;
    m_debugUi.diaryPanel(panel);
    m_diaryOpen = panel.open;
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
                 notice(std::string(a[0].asString()));
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
}
} // namespace g7
