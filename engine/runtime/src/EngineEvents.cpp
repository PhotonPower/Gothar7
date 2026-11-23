// The event net (docs/modules/dungeon.md D1): triggers and relays send actions to named vobs. Relays are
// world::EventNet's; the engine wires the world into it and handles every other target here - doors act,
// a Mob definition's on_event hears it, and Lua gets dungeon_event(target, action, source).

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>
#include <set>
#include <string>
#include <vector>

namespace g7
{
namespace
{
std::string eventActionList()
{
    std::string list;
    for (const std::string_view a : world::kEventActions)
    {
        list += (list.empty() ? "" : ", ") + std::string(a);
    }
    return list;
}
} // namespace

void Engine::rebuildEventNet()
{
    // The scene is set up anew (a world loaded, the editor rebuilt it): the state goes over to the new net.
    const world::EventNetSnapshot kept = m_events.snapshot();
    m_events.clear();
    m_events.setDeliver([this](const world::NetEvent& event) { deliverEvent(event); });
    std::set<std::string> names;
    m_scene.each<world::Vob>([&](entt::entity, const world::Vob& vob) { names.insert(vob.nameText); });
    m_scene.each<world::Vob, world::Relay>([&](entt::entity, const world::Vob& vob, const world::Relay& relay)
                                           { m_events.addRelay(vob.nameText, relay); });
    const auto checkTargets = [&](const world::Vob& vob, const std::vector<std::string>& targets)
    {
        m_events.addSender(vob.nameText, targets);
        for (const std::string& target : targets)
        {
            if (!names.contains(target))
            {
                // Allowed (Lua handles names that are no vob), but mostly a typo in the world.
                G7_LOG_INFO("engine", "{}: target {} of {} is no vob (only Lua hears it)", m_worldPath,
                            target, vob.nameText);
            }
        }
    };
    m_scene.each<world::Vob, world::TriggerVolume>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume)
        { checkTargets(vob, volume.targets); });
    m_scene.each<world::Vob, world::Relay>([&](entt::entity, const world::Vob& vob, const world::Relay& relay)
                                           { checkTargets(vob, relay.targets); });
    for (const std::string& relay : m_events.cycles())
    {
        G7_LOG_WARN("engine", "{}: relay {} lies on a cycle (runs once per step)", m_worldPath, relay);
    }
    if (!kept.relays.empty() || !kept.pending.empty())
    {
        (void)m_events.restore(kept);
    }
}

void Engine::triggerTargets(const world::Vob& vob, const world::TriggerVolume& volume)
{
    for (const std::string& target : volume.targets)
    {
        m_events.send(target, volume.action, vob.nameText);
    }
    if (volume.targetId.valid())
    {
        // The old reserved target by id (M4): sent to that vob's name.
        if (const entt::entity e = m_scene.findById(volume.targetId); e != entt::null)
        {
            m_events.send(m_scene.get<world::Vob>(e)->nameText, volume.action, vob.nameText);
        }
    }
}

Result<void> Engine::sendEvent(std::string_view target, std::string_view action, std::string_view source)
{
    if (!world::isEventAction(action))
    {
        return Error{std::format("unknown action \"{}\" (one of {})", action, eventActionList())};
    }
    if (target.empty())
    {
        return Error{"no target"};
    }
    m_events.send(target, action, source);
    return {};
}

void Engine::deliverEvent(const world::NetEvent& event)
{
    G7_LOG_DEBUG("engine", "event {} -> {} ({})", event.action, event.target, event.source);
    (void)moverEvent(event); // dungeon D2: gates, secret walls, trapdoors
    if (const auto vob = findMob(event.target); vob)
    {
        if (const auto it = m_mobs.find(vob->value); it != m_mobs.end())
        {
            MobRuntime& m = it->second;
            const std::string_view action = event.action;
            if (m.type == "door")
            {
                // The mechanism moves the door whether it is locked or not (a lever opens a locked gate); the
                // lock stays as it is.
                if (action == "open" || action == "close")
                {
                    if (m.open != (action == "open"))
                    {
                        swingDoor(m, action == "open");
                    }
                }
                else if (action == "toggle" || action == "trigger")
                {
                    swingDoor(m, !m.open);
                }
            }
            if (action == "lock" || action == "unlock")
            {
                if (m.lock.empty() && m.key.empty() && action == "lock")
                {
                    G7_LOG_WARN("engine", "event lock -> {}: the mob has no lock and no key - stays unlocked",
                                event.target);
                }
                else
                {
                    m.locked = action == "lock";
                }
            }
            if (m_scripts)
            {
                if (const script::Instance* def = m_scripts->findInstance("Mob", m.definition);
                    def != nullptr)
                {
                    if (const script::FunctionRef f = def->fields["on_event"].asFunction(); f.valid())
                    {
                        const script::Value args[] = {m.definition, event.action, event.source};
                        (void)m_scripts->call(f, args);
                    }
                }
            }
        }
    }
    if (m_scripts)
    {
        const script::Value args[] = {event.target, event.action, event.source};
        m_scripts->emit("dungeon_event", args);
    }
}

void Engine::bindEventFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    bindMoverFunctions(); // dungeon D2
    vm.bind({"send", "send(target: string, action?: string, source?: string)",
             "Schickt ein Ereignis an das Vob `target` (Dungeon D1), wie es ein Auslöser tut: `action` ist "
             "toggle, open, close, on, off, wake, trigger (Vorgabe), lock oder unlock; `source` der Absender "
             "(Vorgabe \"lua\"). Es wirkt im nächsten festen Schritt: Relais zählen es, Türen gehen auf oder "
             "zu bzw. werden ver- und entriegelt, bewegte Teile (Gatter, Geheimwand, Falltür; D2) fahren auf "
             "oder zu, alle anderen Ziele hören es als Ereignis `dungeon_event`.",
             "Welt", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString() || (a.size() > 1 && !a[1].isString() && !a[1].isNil()) ||
                     (a.size() > 2 && !a[2].isString() && !a[2].isNil()))
                 {
                     return Error{"expects (target, action?, source?)"};
                 }
                 const std::string_view action =
                     a.size() > 1 && a[1].isString() ? a[1].asString() : world::kEventTrigger;
                 const std::string_view source =
                     a.size() > 2 && a[2].isString() ? a[2].asString() : std::string_view("lua");
                 if (auto sent = sendEvent(a[0].asString(), action, source); !sent)
                 {
                     return sent.error();
                 }
                 return Value();
             }});
    vm.bind({"relay_state", "relay_state(name: string) -> {mode, count, counted, inputs, spent} | nil",
             "Der Zustand des Relais `name` der Welt (Dungeon D1): Art (any, all, count), nötige Anzahl, "
             "bisher gezählt, die Eingänge, die an sind (Absender-Namen), und ob ein einmaliges Relais "
             "verbraucht ist; nil, wenn es kein Relais dieses Namens gibt.",
             "Welt", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"expects (name)"};
                 }
                 const world::Relay* relay = m_events.relay(a[0].asString());
                 if (relay == nullptr)
                 {
                     return Value();
                 }
                 const world::RelaySnapshot s = m_events.relayState(a[0].asString());
                 std::vector<Value> inputs(s.inputs.begin(), s.inputs.end());
                 const char* mode = relay->mode == world::Relay::Mode::All     ? "all"
                                    : relay->mode == world::Relay::Mode::Count ? "count"
                                                                               : "any";
                 return script::makeTable({}, {{"mode", std::string(mode)},
                                               {"count", static_cast<i64>(relay->count)},
                                               {"counted", static_cast<i64>(s.counted)},
                                               {"inputs", script::makeTable(std::move(inputs))},
                                               {"spent", s.spent}});
             }});
    vm.bind(
        {"dungeon_event",
         "on(\"dungeon_event\", fn(target: string, action: string, source: string))",
         "Ein Ereignis des Netzes (Dungeon D1) hat ein Ziel erreicht, das kein Relais ist: von einem "
         "Auslöser "
         "(Trigger, Relais) oder `send`. `source` ist der Name des Absenders. Türen haben schon reagiert; "
         "eine Mob-Definition mit `on_event(mob, action, source)` hört es vorher.",
         "Ereignisse",
         {}});
}
} // namespace g7
