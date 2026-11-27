// Teleporters (docs/modules/dungeon.md D4): triggers with components.trigger.teleport put whoever enters them
// at a vob of the same world or, the hero only, on a start point of another world. A short white flash, the
// effect "teleport" at both ends; arriving inside another teleporter does not send him back (the triggers
// are primed). NPCs, animals and items go only within the world and only when they enter.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <utility>

namespace g7
{
namespace
{
constexpr f32 kFlashSeconds = 0.2f; ///< dungeon.md: white 0.2 s
constexpr f32 kProbeLift = 0.05f;   ///< feet are tested this far up
constexpr f32 kColumnHeight = 0.1f; ///< the column starts a little above the trigger's bottom

bool takesHero(world::Teleport::Filter f)
{
    return f != world::Teleport::Filter::Items;
}

bool takesCreatures(world::Teleport::Filter f)
{
    return f == world::Teleport::Filter::Creature || f == world::Teleport::Filter::Any;
}

bool takesItems(world::Teleport::Filter f)
{
    return f == world::Teleport::Filter::Items || f == world::Teleport::Filter::Any;
}

f32 yawOf(const Mat4& m)
{
    const Vec3 ahead = Vec3(m * Vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return std::atan2(-ahead.x, -ahead.z);
}
} // namespace

void Engine::rebuildTeleports()
{
    std::map<u64, TeleportRuntime> before = std::exchange(m_teleports, {});
    for (auto& [id, t] : before)
    {
        if (t.effect)
        {
            m_particles.stop(*t.effect);
        }
    }
    m_scene.each<world::Vob, world::TriggerVolume>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume)
        {
            if (!volume.teleport)
            {
                return;
            }
            TeleportRuntime t;
            t.active = volume.teleport->active;
            if (const auto old = before.find(vob.id.value); old != before.end())
            {
                t.active = old->second.active;
            }
            if (!volume.teleport->to.empty() && teleportTarget(*volume.teleport) == std::nullopt)
            {
                G7_LOG_WARN("engine", "{}: teleporter {} leads to {}, which is no vob", m_worldPath,
                            vob.nameText, volume.teleport->to);
            }
            m_teleports.emplace(vob.id.value, std::move(t));
        });
    for (auto& [id, t] : m_teleports)
    {
        updateTeleportEffect(id, t);
    }
}

std::optional<std::pair<Vec3, f32>> Engine::teleportTarget(const world::Teleport& teleport) const
{
    const entt::entity e = m_scene.findByName(StringId(teleport.to));
    if (teleport.to.empty() || e == entt::null)
    {
        return std::nullopt;
    }
    const Mat4 m = m_scene.worldMatrix(e);
    const f32 yaw = teleport.yaw ? glm::radians(*teleport.yaw) : yawOf(m);
    return std::pair<Vec3, f32>{Vec3(m[3]), yaw};
}

void Engine::updateTeleportEffect(u64 id, TeleportRuntime& t)
{
    const entt::entity e = m_scene.findById(world::VobId{id});
    const world::TriggerVolume* volume = e != entt::null ? m_scene.get<world::TriggerVolume>(e) : nullptr;
    const bool shows = volume != nullptr && volume->teleport && volume->teleport->visible && t.active;
    if (shows && !t.effect)
    {
        const Mat4 m = m_scene.worldMatrix(e);
        const f32 bottom =
            volume->shape == world::TriggerVolume::Shape::Box ? volume->halfExtents.y : volume->radius;
        const Vec3 at = Vec3(m[3]) - Vec3(0.0f, bottom - kColumnHeight, 0.0f);
        t.effect = startEffect("teleport_column", at, Vec3(0.0f, 1.0f, 0.0f));
    }
    else if (!shows && t.effect)
    {
        m_particles.stop(*t.effect);
        t.effect.reset();
    }
}

void Engine::heroEnteredTeleport(const world::Vob& vob, const world::Teleport& teleport)
{
    const auto it = m_teleports.find(vob.id.value);
    if (it == m_teleports.end() || !it->second.active || !takesHero(teleport.filter) || m_pendingTeleport)
    {
        return;
    }
    if (!teleport.world.empty())
    {
        // Another level: a level change; the white flash covers it, the arrival fades from black.
        G7_LOG_INFO("engine", "teleporter {} -> {} ({})", vob.nameText, teleport.world, teleport.start);
        requestWorldChange(teleport.world, teleport.start);
        if (teleport.keepVelocity && m_pendingWorldChange && m_player.valid())
        {
            m_pendingWorldChange->fall = m_player.velocity(); // like changeWorld.fall
        }
        return;
    }
    const auto target = teleportTarget(teleport);
    if (!target)
    {
        G7_LOG_WARN("engine", "teleporter {}: no vob {} to go to", vob.nameText, teleport.to);
        return;
    }
    m_pendingTeleport = PendingTeleport{
        vob.nameText, target->first, target->second, teleport.silent,
        teleport.keepVelocity && m_player.valid() ? std::optional<Vec3>(m_player.velocity()) : std::nullopt};
}

void Engine::performHeroTeleport()
{
    if (!m_pendingTeleport || !m_player.valid())
    {
        m_pendingTeleport.reset();
        return;
    }
    const PendingTeleport t = std::move(*m_pendingTeleport);
    m_pendingTeleport.reset();
    const Vec3 from = m_player.feet();
    if (!t.silent)
    {
        (void)startEffect("teleport", from + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        (void)playSound("teleport", from);
    }
    teleportPlayer(t.feet + Vec3(0.0f, 0.05f, 0.0f), t.yaw);
    if (!t.silent)
    {
        (void)startEffect("teleport", t.feet + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        (void)playSound("teleport", t.feet);
        m_screenFade = ScreenFade{Vec3(1.0f), kFlashSeconds, kFlashSeconds};
    }
    if (t.keepVelocity)
    {
        // keep_velocity (under a trapdoor): the fall goes on at the target and, like changeWorld.fall, costs
        // nothing when it lands.
        m_player.setVelocity(*t.keepVelocity);
        m_fallUnhurt = true;
    }
    // Arriving inside a teleporter (or any trigger) counts as already in it: no bouncing back.
    const world::TriggerProbe probe{kCameraProbe, triggerProbePosition(), true};
    m_triggers.prime(m_scene, std::span(&probe, 1));
    G7_LOG_INFO("engine", "teleporter {}: hero at ({:.1f}, {:.1f}, {:.1f})", t.source, t.feet.x, t.feet.y,
                t.feet.z);
    if (m_scripts)
    {
        const script::Value args[] = {t.source};
        m_scripts->emit("hero_teleported", args);
    }
}

void Engine::moveWorldItem(world::VobId vob, const Vec3& at)
{
    const entt::entity e = m_scene.findById(vob);
    if (e == entt::null)
    {
        return;
    }
    Transform local = *std::as_const(m_scene).get<Transform>(e);
    local.position = at; // item vobs have no parent
    m_scene.setTransform(e, local);
    m_scene.updateTransforms();
    for (WorldItem& item : m_worldItems)
    {
        if (item.vob == vob)
        {
            const Vec3 shift = at - Vec3(item.transform[3]);
            item.transform[3] = Vec4(at, 1.0f);
            item.bounds.min += shift;
            item.bounds.max += shift;
            m_cullGridDirty = true;
        }
    }
}

void Engine::fixedUpdateTeleports(f32 seconds)
{
    m_screenFade.seconds = std::max(m_screenFade.seconds - seconds, 0.0f);
    if (m_teleports.empty())
    {
        return;
    }
    G7_PROFILE_SCOPE("teleports");
    const Vec3 lift(0.0f, kProbeLift, 0.0f);
    // Who is in which teleporter now (for arrivals in another one: they count as inside, no bouncing).
    const auto insideOf = [&](const Vec3& point)
    {
        std::vector<u64> in;
        for (const auto& [id, t] : m_teleports)
        {
            const entt::entity e = m_scene.findById(world::VobId{id});
            const world::TriggerVolume* v = e != entt::null ? m_scene.get<world::TriggerVolume>(e) : nullptr;
            if (v != nullptr && world::TriggerSystem::contains(*v, m_scene.worldMatrix(e), point))
            {
                in.push_back(id);
            }
        }
        return in;
    };
    for (auto& [id, t] : m_teleports)
    {
        const entt::entity e = m_scene.findById(world::VobId{id});
        const world::TriggerVolume* volume = e != entt::null ? m_scene.get<world::TriggerVolume>(e) : nullptr;
        if (volume == nullptr || !volume->teleport)
        {
            continue;
        }
        const world::Teleport& teleport = *volume->teleport;
        const Mat4 world = m_scene.worldMatrix(e);
        const auto inside = [&](const Vec3& p) { return world::TriggerSystem::contains(*volume, world, p); };
        // Creatures: the world's own target only (question 9: they never change the level).
        std::set<u32> creaturesNow;
        for (const auto& c : m_creatures)
        {
            if (!c->vanished && inside(c->position + lift))
            {
                creaturesNow.insert(c->id);
            }
        }
        std::set<u64> itemsNow;
        for (const WorldItem& item : m_worldItems)
        {
            const Vec3 centre = item.bounds.center();
            if (inside(Vec3(centre.x, item.bounds.min.y, centre.z) + lift))
            {
                itemsNow.insert(item.vob.value);
            }
        }
        if (!t.primed)
        {
            // A world was loaded or a save applied: who stands in it now came there, they stay.
            t.inside = std::move(creaturesNow);
            t.itemsInside = std::move(itemsNow);
            t.primed = true;
            continue;
        }
        const auto target = teleportTarget(teleport);
        if (t.active && target && takesCreatures(teleport.filter))
        {
            for (const u32 cid : creaturesNow)
            {
                Creature* c = creature(cid);
                if (t.inside.contains(cid) || c == nullptr || c->dead || !c->body)
                {
                    continue;
                }
                c->body->teleport(target->first + Vec3(0.0f, 0.05f, 0.0f));
                c->position = c->positionBefore = target->first;
                c->yaw = c->yawBefore = target->second;
                c->route.reset();
                (void)startEffect("teleport", target->first + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
                for (const u64 other : insideOf(target->first + lift))
                {
                    m_teleports[other].inside.insert(cid);
                }
                G7_LOG_INFO("engine", "teleporter {}: {} goes to {}", m_scene.get<world::Vob>(e)->nameText,
                            c->species, teleport.to);
            }
        }
        if (t.active && target && takesItems(teleport.filter))
        {
            for (const u64 item : itemsNow)
            {
                if (t.itemsInside.contains(item))
                {
                    continue;
                }
                moveWorldItem(world::VobId{item}, target->first);
                for (const u64 other : insideOf(target->first + lift))
                {
                    m_teleports[other].itemsInside.insert(item);
                }
            }
        }
        // Who is in it now (those just sent away are not); arrivals were added above for their target.
        std::erase_if(t.inside, [&](u32 cid) { return !creaturesNow.contains(cid); });
        std::erase_if(t.itemsInside, [&](u64 item) { return !itemsNow.contains(item); });
        if (!t.active || !target)
        {
            t.inside.insert(creaturesNow.begin(), creaturesNow.end()); // an inactive one: they are in it
            t.itemsInside.insert(itemsNow.begin(), itemsNow.end());
        }
    }
}

bool Engine::teleportEvent(const world::NetEvent& event)
{
    for (auto& [id, t] : m_teleports)
    {
        const entt::entity e = m_scene.findById(world::VobId{id});
        if (e == entt::null || m_scene.get<world::Vob>(e)->nameText != event.target)
        {
            continue;
        }
        const std::string_view action = event.action;
        if (action == "on" || action == "open")
        {
            t.active = true;
        }
        else if (action == "off" || action == "close")
        {
            t.active = false;
        }
        else if (action == "toggle" || action == "trigger")
        {
            t.active = !t.active;
        }
        updateTeleportEffect(id, t);
        return true;
    }
    return false;
}

std::optional<bool> Engine::teleportActive(std::string_view name) const
{
    for (const auto& [id, t] : m_teleports)
    {
        if (const entt::entity e = m_scene.findById(world::VobId{id});
            e != entt::null && m_scene.get<world::Vob>(e)->nameText == name)
        {
            return t.active;
        }
    }
    return std::nullopt;
}

void Engine::bindTeleportFunctions()
{
    using script::Value;
    m_scripts->bind(
        {"teleport_state", "teleport_state(name: string) -> boolean | nil",
         "Ob der Teleporter `name` (Dungeon D4, Trigger mit `teleport`) an ist; nil, wenn es keinen "
         "gibt. An- und ausgeschaltet wird er mit `send(name, 'on' | 'off' | 'toggle')`.",
         "Welt", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (name)"};
             }
             const auto active = teleportActive(a[0].asString());
             return active ? Value(*active) : Value();
         }});
    m_scripts->bind(
        {"hero_teleported",
         "on(\"hero_teleported\", fn(teleporter: string))",
         "Ein Teleporter derselben Welt (Dungeon D4) hat den Helden versetzt; `teleporter` ist sein "
         "Name. In eine andere Ebene folgt stattdessen `world_loaded`.",
         "Ereignisse",
         {}});
}
} // namespace g7
