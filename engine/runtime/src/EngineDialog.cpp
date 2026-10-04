// Dialogues (M10 part A, docs/modules/gameplay.md "Dialog & Quests"): talking to an NPC shows the Infos whose
// condition holds and which are not told yet (or permanent), ordered by `nr`; important ones start by
// themselves. An Info's run(npc) queues lines (say), answers (choice) and the end (end_dialog). Lines stay
// for a time by their length and can be skipped. Decisions of the owner (2026-10-04): text only for now,
// inline German text in Lua with automatic keys (info name + number) for later translation and voice files.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kLinePerCharacter = 0.06f; ///< seconds a line stays, per character
constexpr f32 kLineMinimum = 1.5f;
constexpr f32 kTalkDistance = 3.0f;      ///< an important Info starts when the NPC sees the player this near
constexpr f32 kApproachDistance = 10.0f; ///< ... or walks up to him from this near (`approach = true`)
constexpr std::string_view kHero = "hero";

std::string_view baseName(std::string_view npc)
{
    return npc.substr(0, npc.find('#'));
}
} // namespace

std::vector<const script::Instance*> Engine::availableInfos(std::string_view npc, bool important)
{
    std::vector<const script::Instance*> out;
    if (!m_scripts)
    {
        return out;
    }
    const script::Value told = m_scripts->story()["told"];
    for (const script::Instance* info : m_scripts->instancesOf("Info"))
    {
        if (info->fields["npc"].asString() != baseName(npc) ||
            info->fields["important"].asBool() != important)
        {
            continue;
        }
        if (!info->fields["permanent"].asBool() && told[info->name].asBool())
        {
            continue; // said already
        }
        if (const script::FunctionRef condition = info->fields["condition"].asFunction(); condition.valid())
        {
            const script::Value args[] = {std::string(npc)};
            auto ok = m_scripts->call(condition, args);
            if (!ok)
            {
                G7_LOG_WARN("engine", "{}.condition: {}", info->name, ok.error().message);
                continue;
            }
            if (!ok.value().asBool())
            {
                continue;
            }
        }
        out.push_back(info);
    }
    std::ranges::stable_sort(out, {}, [](const script::Instance* i) { return i->fields["nr"].asInteger(0); });
    return out;
}

Result<void> Engine::startDialog(u32 npcId)
{
    Creature* c = creature(npcId);
    if (c == nullptr || !c->character || !m_scripts)
    {
        return Error{"nobody to talk to"};
    }
    if (m_dialog)
    {
        return Error{"already talking"};
    }
    // The NPC leaves what it does (its routine takes over afterwards) and faces the player.
    finishState(*c);
    c->commands.clear();
    c->commandRunning = false;
    c->route.reset();
    c->talking = true;
    c->routineEntry = -1;
    m_dialog.emplace();
    m_dialog->npc = npcId;
    m_dialog->npcName = c->species;
    const script::Value args[] = {c->species};
    m_scripts->emit("dialog_started", args);
    // Important Infos run first, without a choice (Gothic: the NPC speaks first).
    if (const auto important = availableInfos(c->species, true); !important.empty())
    {
        runInfo(*important.front());
    }
    return {};
}

void Engine::runInfo(const script::Instance& info)
{
    m_dialog->info = info.name;
    m_dialog->lineNumber = 0;
    m_dialog->menu.clear();
    if (!info.fields["permanent"].asBool())
    {
        (void)m_scripts->runString(
            std::format("Story.told = Story.told or {{}} Story.told['{}'] = true", info.name));
    }
    if (const script::FunctionRef run = info.fields["run"].asFunction(); run.valid())
    {
        const script::Value args[] = {m_dialog->npcName};
        if (auto ok = m_scripts->call(run, args); !ok)
        {
            G7_LOG_WARN("engine", "{}.run: {}", info.name, ok.error().message);
        }
    }
}

void Engine::buildDialogMenu()
{
    m_dialog->menu.clear();
    m_dialog->selected = 0;
    if (!m_dialog->choices.empty())
    {
        m_dialog->menu = m_dialog->choices; // the answers of the running Info
        return;
    }
    for (const script::Instance* info : availableInfos(m_dialog->npcName, false))
    {
        DialogOption option;
        option.text = std::string(info->fields["description"].asString());
        option.info = info->name;
        m_dialog->menu.push_back(std::move(option));
    }
    DialogOption end;
    end.text = "Ende";
    end.end = true;
    m_dialog->menu.push_back(std::move(end));
}

void Engine::dialogChoose(usize index)
{
    if (!m_dialog || !m_dialog->lines.empty() || index >= m_dialog->menu.size())
    {
        return;
    }
    const DialogOption option = m_dialog->menu[index];
    m_dialog->menu.clear();
    if (option.end)
    {
        endDialog();
        return;
    }
    if (!option.info.empty())
    {
        m_dialog->choices.clear();
        if (const script::Instance* info = m_scripts->findInstance("Info", option.info))
        {
            // The player says the menu text first (Gothic: the hero asks).
            queueLine(std::string(kHero), option.text);
            runInfo(*info);
        }
        return;
    }
    // An answer of the running Info: the player says it, then its function runs (it may add new answers).
    m_dialog->choices.clear();
    queueLine(std::string(kHero), option.text);
    if (option.choice.valid())
    {
        const script::Value args[] = {m_dialog->npcName};
        if (auto ok = m_scripts->call(option.choice, args); !ok)
        {
            G7_LOG_WARN("engine", "{} choice: {}", m_dialog->info, ok.error().message);
        }
    }
}

void Engine::dialogSkip()
{
    if (m_dialog && !m_dialog->lines.empty())
    {
        m_dialog->lines.pop_front();
        m_dialog->lineTime = 0.0f;
    }
}

void Engine::queueLine(std::string speaker, std::string text)
{
    DialogLine line;
    line.speaker = std::move(speaker);
    line.key = std::format("{}_{:02}", m_dialog->info.empty() ? m_dialog->npcName : m_dialog->info,
                           m_dialog->lineNumber++);
    const script::Instance* npc = m_scripts->findInstance(
        "Npc", line.speaker == kHero ? std::string_view("pc_hero") : std::string_view(line.speaker));
    line.name = npc != nullptr ? std::string(npc->fields["name"].asString()) : line.speaker;
    line.seconds = std::max(kLineMinimum, kLinePerCharacter * static_cast<f32>(text.size()));
    line.text = std::move(text);
    G7_LOG_INFO("engine", "{} [{}]: {}", line.name, line.key, line.text);
    m_dialog->lines.push_back(std::move(line));
}

void Engine::endDialog()
{
    if (!m_dialog)
    {
        return;
    }
    const std::string npc = m_dialog->npcName;
    if (Creature* c = creature(m_dialog->npc))
    {
        c->talking = false; // its routine starts again
    }
    m_dialog.reset();
    if (m_scripts)
    {
        const script::Value args[] = {npc};
        m_scripts->emit("dialog_ended", args);
    }
}

void Engine::fixedUpdateDialog(f32 seconds)
{
    if (!m_dialog)
    {
        return;
    }
    Creature* c = creature(m_dialog->npc);
    if (c == nullptr)
    {
        m_dialog.reset(); // gone (world change)
        return;
    }
    if (!m_dialog->lines.empty())
    {
        m_dialog->lineTime += seconds;
        if (m_dialog->lineTime >= m_dialog->lines.front().seconds)
        {
            m_dialog->lines.pop_front();
            m_dialog->lineTime = 0.0f;
        }
        return;
    }
    if (m_dialog->endRequested)
    {
        endDialog();
        return;
    }
    if (m_dialog->menu.empty())
    {
        buildDialogMenu();
    }
}

void Engine::dialogInput()
{
    using platform::Action;
    if (!m_dialog)
    {
        return;
    }
    const bool action = m_actions.pressed(m_input, Action::Action) || m_actions.pressed(m_input, Action::Use);
    if (!m_dialog->lines.empty())
    {
        if (action)
        {
            dialogSkip(); // E4: lines can be skipped
        }
        return;
    }
    const auto count = static_cast<i32>(m_dialog->menu.size());
    if (count == 0)
    {
        return;
    }
    if (m_actions.pressed(m_input, Action::MoveForward))
    {
        m_dialog->selected = (m_dialog->selected + count - 1) % count;
    }
    if (m_actions.pressed(m_input, Action::MoveBack))
    {
        m_dialog->selected = (m_dialog->selected + 1) % count;
    }
    if (action)
    {
        dialogChoose(static_cast<usize>(m_dialog->selected));
    }
}

void Engine::dialogUi()
{
    if (!m_dialog)
    {
        return;
    }
    ui::DialogPanel panel;
    if (!m_dialog->lines.empty())
    {
        panel.speaker = m_dialog->lines.front().name;
        panel.line = m_dialog->lines.front().text;
    }
    else
    {
        for (const DialogOption& o : m_dialog->menu)
        {
            panel.options.push_back(o.text);
        }
        panel.selected = m_dialog->selected;
    }
    m_debugUi.dialogPanel(panel);
    if (panel.clicked >= 0)
    {
        dialogChoose(static_cast<usize>(panel.clicked));
    }
    else if (panel.selected != m_dialog->selected && panel.selected >= 0)
    {
        m_dialog->selected = panel.selected;
    }
}

void Engine::dialogPerception(Creature& c, f32 distance, bool sees)
{
    // Important Infos: the NPC speaks to the player when he sees him close; with approach = true it walks up
    // to him first (owner decision E1).
    if (m_dialog || !sees || c.talking || distance > kApproachDistance || !m_player.valid() ||
        !heroStanding())
    {
        return;
    }
    const auto important = availableInfos(c.species, true);
    if (important.empty())
    {
        return;
    }
    if (distance <= kTalkDistance)
    {
        (void)startDialog(c.id);
    }
    else if (important.front()->fields["approach"].asBool() && !c.approaching)
    {
        c.approaching = true;
        c.commands.clear();
        c.commandRunning = false;
        Creature::Command cmd{Creature::Command::Kind::GoTo, "@player"};
        cmd.distance = 1.5f;
        c.commands.push_back(std::move(cmd));
    }
}

void Engine::bindDialogFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    const auto inDialog = [this]() -> Result<void>
    {
        if (!m_dialog)
        {
            return Error{"only in a dialogue (an Info's run or a choice)"};
        }
        return {};
    };
    vm.bind({"say", "say(who: string, text: string)",
             "Im Dialog: eine Zeile. `who` ist der NPC (das Argument von run) oder `\"hero\"`. Sie steht als "
             "Untertitel mit Sprechername, so lange wie ihre Länge verlangt (überspringbar); Schlüssel "
             "`<info>_<nn>` für Übersetzung und Sprachaufnahmen.",
             "Dialoge", [this, inDialog](std::span<const Value> a) -> Result<Value>
             {
                 if (auto ok = inDialog(); !ok)
                 {
                     return ok.error();
                 }
                 if (a.size() < 2 || !a[0].isString() || !a[1].isString())
                 {
                     return Error{"expects (who: string, text: string)"};
                 }
                 queueLine(std::string(a[0].asString()), std::string(a[1].asString()));
                 return Value();
             }});
    vm.bind({"choice", "choice(text: string, fn: function)",
             "Im Dialog: eine Antwort zur Auswahl (Gothic Info_AddChoice). Gewählt sagt der Held `text`, "
             "dann läuft "
             "`fn(npc)`; sie kann neue Antworten hinzufügen. Ohne Antworten folgt wieder die Themenliste.",
             "Dialoge", [this, inDialog](std::span<const Value> a) -> Result<Value>
             {
                 if (auto ok = inDialog(); !ok)
                 {
                     return ok.error();
                 }
                 if (a.size() < 2 || !a[0].isString() || !a[1].isFunction())
                 {
                     return Error{"expects (text: string, fn: function)"};
                 }
                 DialogOption option;
                 option.text = std::string(a[0].asString());
                 option.choice = a[1].asFunction();
                 m_dialog->choices.push_back(std::move(option));
                 return Value();
             }});
    vm.bind({"end_dialog", "end_dialog()", "Im Dialog: beendet ihn, sobald die Zeilen gesagt sind.",
             "Dialoge", [this, inDialog](std::span<const Value>) -> Result<Value>
             {
                 if (auto ok = inDialog(); !ok)
                 {
                     return ok.error();
                 }
                 m_dialog->endRequested = true;
                 return Value();
             }});
    vm.bind({"talk", "talk(npc: string) -> boolean",
             "Beginnt einen Dialog mit dem NPC (Konsole, Tests; im Spiel: Aktionstaste auf ihn).", "Dialoge",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.empty() ? std::nullopt : npcByInstance(a[0].asString());
                 if (!id)
                 {
                     return Error{"no such NPC in this world"};
                 }
                 if (auto ok = startDialog(*id); !ok)
                 {
                     return ok.error();
                 }
                 return Value(true);
             }});
    vm.bind(
        {"dialog_state", "dialog_state() -> {npc, speaker, line, key, options} | nil",
         "Der laufende Dialog: wer spricht, die Zeile und ihr Schlüssel, bzw. die Auswahl (Liste der Texte).",
         "Dialoge", [this](std::span<const Value>) -> Result<Value>
         {
             if (!m_dialog)
             {
                 return Value();
             }
             std::vector<Value> options;
             for (const DialogOption& o : m_dialog->menu)
             {
                 options.emplace_back(o.text);
             }
             const bool speaking = !m_dialog->lines.empty();
             return script::makeTable(
                 {}, {{"npc", m_dialog->npcName},
                      {"speaker", speaking ? m_dialog->lines.front().speaker : std::string()},
                      {"line", speaking ? m_dialog->lines.front().text : std::string()},
                      {"key", speaking ? m_dialog->lines.front().key : std::string()},
                      {"options", script::makeTable(std::move(options), {})}});
         }});
    vm.bind({"dialog_choose", "dialog_choose(n: integer)",
             "Wählt den n-ten Eintrag (ab 1) der Auswahl (Konsole, Tests).", "Dialoge",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (!m_dialog || a.empty() || !a[0].isNumber() || a[0].asInteger() < 1)
                 {
                     return Error{"expects (n >= 1) in a dialogue"};
                 }
                 dialogChoose(static_cast<usize>(a[0].asInteger() - 1));
                 return Value();
             }});
    vm.bind({"info_told", "info_told(info: string) -> boolean", "Ob die Info schon gesagt wurde.", "Dialoge",
             [this](std::span<const Value> a) -> Result<Value>
             { return Value(!a.empty() && m_scripts->story()["told"][a[0].asString()].asBool()); }});
    vm.bind({"dialog_started",
             "on(\"dialog_started\", fn(npc: string))",
             "Ein Dialog beginnt.",
             "Ereignisse",
             {}});
    vm.bind({"dialog_ended",
             "on(\"dialog_ended\", fn(npc: string))",
             "Ein Dialog ist vorbei.",
             "Ereignisse",
             {}});
}
} // namespace g7
