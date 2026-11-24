// Switches and pressure plates (docs/modules/dungeon.md D3). Levers and wall buttons are mobs: using one
// sends its action to its targets (EngineMobs.cpp calls useSwitch at the clip's moment). Plates are triggers
// with components.trigger.plate: pressed while the hero, a creature or (weight any) an item lies in them.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/runtime/Engine.hpp>

#include <string>
#include <utility>

namespace g7
{
namespace
{
constexpr f32 kPlateProbeLift = 0.05f; ///< feet (and an item's lowest point) are tested this far up
} // namespace

void Engine::useSwitch(world::VobId vob, MobRuntime& m)
{
    const entt::entity e = m_scene.findById(vob);
    const std::string source = e != entt::null ? m_scene.get<world::Vob>(e)->nameText : m.definition;
    m.found = true;
    std::string action = m.action.empty() ? std::string(world::kEventTrigger) : m.action;
    if (m.type == "lever")
    {
        // Two positions: thrown sends the action, thrown back its opposite (a toggle toggles again).
        m.open = !m.open;
        if (!m.open)
        {
            action = std::string(world::inverseEventAction(action));
        }
    }
    else
    {
        if (m.once && m.spent)
        {
            notice(uiText("UI_SWITCH_STUCK"));
            return;
        }
        m.spent = true;
    }
    if (m.targets.empty())
    {
        G7_LOG_INFO("engine", "switch {} has no targets", source);
    }
    for (const std::string& target : m.targets)
    {
        m_events.send(target, action, source);
    }
}

void Engine::rebuildPlates()
{
    // Pressed or not comes from what lies on them at the next step (also after loading a save): quietly.
    std::map<u64, PlateRuntime> before = std::exchange(m_plates, {});
    m_scene.each<world::Vob, world::TriggerVolume>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume)
        {
            if (!volume.plate)
            {
                return;
            }
            PlateRuntime p;
            if (const auto old = before.find(vob.id.value); old != before.end())
            {
                p.pressed = old->second.pressed;
            }
            m_plates.emplace(vob.id.value, p);
        });
    m_platesPrimed = false;
}

void Engine::fixedUpdatePlates()
{
    if (m_plates.empty())
    {
        return;
    }
    G7_PROFILE_SCOPE("plates");
    const Vec3 lift(0.0f, kPlateProbeLift, 0.0f);
    for (auto& [id, p] : m_plates)
    {
        const entt::entity e = m_scene.findById(world::VobId{id});
        const world::TriggerVolume* volume = e != entt::null ? m_scene.get<world::TriggerVolume>(e) : nullptr;
        if (volume == nullptr || !volume->plate)
        {
            continue;
        }
        const Mat4 world = m_scene.worldMatrix(e);
        const auto inside = [&](const Vec3& point)
        { return world::TriggerSystem::contains(*volume, world, point); };
        const world::Plate::Weight weight = volume->plate->weight;
        bool pressed = m_player.valid() && inside(m_player.feet() + lift);
        if (!pressed && weight != world::Plate::Weight::Player)
        {
            for (const auto& c : m_creatures)
            {
                if (!c->vanished && inside(c->position + lift))
                {
                    pressed = true;
                    break;
                }
            }
        }
        if (!pressed && weight == world::Plate::Weight::Any)
        {
            for (const WorldItem& item : m_worldItems)
            {
                const Vec3 centre = item.bounds.center();
                if (inside(Vec3(centre.x, item.bounds.min.y, centre.z) + lift))
                {
                    pressed = true;
                    break;
                }
            }
        }
        if (pressed == p.pressed)
        {
            continue;
        }
        p.pressed = pressed;
        if (!m_platesPrimed)
        {
            continue; // as it was when the world was saved or set up: nothing is sent
        }
        const std::string& name = m_scene.get<world::Vob>(e)->nameText;
        const std::string action = pressed ? volume->action
                                   : !volume->plate->releaseAction.empty()
                                       ? volume->plate->releaseAction
                                       : std::string(world::inverseEventAction(volume->action));
        G7_LOG_DEBUG("engine", "plate {} {}", name, pressed ? "pressed" : "released");
        for (const std::string& target : volume->targets)
        {
            m_events.send(target, action, name);
        }
    }
    m_platesPrimed = true;
}

bool Engine::platePressed(std::string_view name) const
{
    for (const auto& [id, p] : m_plates)
    {
        if (const entt::entity e = m_scene.findById(world::VobId{id});
            e != entt::null && m_scene.get<world::Vob>(e)->nameText == name)
        {
            return p.pressed;
        }
    }
    return false;
}

void Engine::bindSwitchFunctions()
{
    using script::Value;
    m_scripts->bind(
        {"switch_state", "switch_state(name: string) -> {type, on, found, spent} | nil",
         "Der Zustand des Schalters `name` (Dungeon D3, Mob-Typen lever und wall_button): Typ, ob ein Hebel "
         "umgelegt ist, ob ein versteckter Schalter gefunden (einmal benutzt) ist und ob ein einmaliger "
         "Knopf "
         "verbraucht ist; für eine Trittplatte (Trigger mit `plate`) `{type = \"plate\", on = gedrückt}`; "
         "nil, "
         "wenn es nichts davon mit diesem Namen gibt.",
         "Welt", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (name)"};
             }
             const std::string_view name = a[0].asString();
             if (const auto vob = findMob(name); vob)
             {
                 if (const auto it = m_mobs.find(vob->value);
                     it != m_mobs.end() && (it->second.type == "lever" || it->second.type == "wall_button"))
                 {
                     const MobRuntime& m = it->second;
                     return script::makeTable(
                         {}, {{"type", m.type}, {"on", m.open}, {"found", m.found}, {"spent", m.spent}});
                 }
             }
             for (const auto& [id, p] : m_plates)
             {
                 if (const entt::entity e = m_scene.findById(world::VobId{id});
                     e != entt::null && m_scene.get<world::Vob>(e)->nameText == name)
                 {
                     return script::makeTable({}, {{"type", std::string("plate")}, {"on", p.pressed}});
                 }
             }
             return Value();
         }});
}
} // namespace g7
