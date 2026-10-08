// Mobs (M8 part C1): mob vobs with their Lua definition (Mob) and type (data/mobs.toml: slots, clips), the
// use sequence (walk to the nearest slot -> enter clip -> loop -> leave clip), locks (key, or lockpicking as
// in Gothic 1), chests with their contents next to the hero's inventory, doors swinging with their collision.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <utility>

namespace g7
{
namespace
{
constexpr f32 kApproachSpeed = 1.6f; ///< m/s while walking to the slot
constexpr f32 kDoorSwingSeconds = 0.8f;
constexpr f32 kDoorOpenAngle = 1.5707963f;   ///< doors open by 90 degrees about +Y (away from the front)
constexpr f32 kFallbackClipSeconds = 0.6f;   ///< a phase without its clip in the graph lasts this long
constexpr f64 kNoticeSeconds = 2.5;          ///< "Verschlossen." and the like
constexpr f32 kStrikeFallbackSeconds = 0.7f; ///< a hammer blow without the hit_anvil event
/// Sleeping (Entscheidung Projektinhaber 2026-10-04, wie Gothic 1): until morning, noon, evening or midnight.
constexpr std::pair<u32, const char*> kSleepUntil[] = {{8, "Bis zum Morgen (8:00)"},
                                                       {12, "Bis Mittag (12:00)"},
                                                       {20, "Bis zum Abend (20:00)"},
                                                       {0, "Bis Mitternacht (0:00)"}};

f32 wrapAngle(f32 a)
{
    constexpr f32 kTwoPi = 6.28318530718f;
    a = std::fmod(a + 0.5f * kTwoPi, kTwoPi);
    return (a < 0.0f ? a + kTwoPi : a) - 0.5f * kTwoPi;
}

AABB boundsOf(const AABB& box, const Mat4& m)
{
    AABB out{Vec3(1e30f), Vec3(-1e30f)};
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z);
        const Vec3 p = Vec3(m * Vec4(corner, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

/// Position, rotation and scale of an affine matrix without shear.
void decomposeMatrix(const Mat4& m, Vec3& position, Quat& rotation, Vec3& scale)
{
    position = Vec3(m[3]);
    scale = Vec3(glm::length(Vec3(m[0])), glm::length(Vec3(m[1])), glm::length(Vec3(m[2])));
    const glm::mat3 r(Vec3(m[0]) / scale.x, Vec3(m[1]) / scale.y, Vec3(m[2]) / scale.z);
    rotation = glm::normalize(glm::quat_cast(r));
}

std::string_view phaseName(int phase)
{
    constexpr std::string_view kNames[] = {"approach", "picklock", "enter", "loop", "leave"};
    return kNames[phase];
}
} // namespace

void Engine::loadMobTypes()
{
    const std::string path = m_config.settings.get<std::string>("game.mobs", "data/mobs.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "{}: {} (no mob can be used)", path, bytes.error().message);
        return;
    }
    auto types = gameplay::MobTypes::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!types)
    {
        G7_LOG_WARN("engine", "{} (no mob can be used)", types.error().message);
        return;
    }
    m_mobTypes = std::move(types).value();
}

void Engine::rebuildMobs()
{
    // Keeps the state of mobs still there (refreshScene), adds new ones from their Lua definition.
    std::unordered_map<u64, MobRuntime> mobs;
    m_scene.each<world::Vob, world::MobRef>(
        [&](entt::entity, const world::Vob& vob, const world::MobRef& ref)
        {
            if (const auto it = m_mobs.find(vob.id.value);
                it != m_mobs.end() && it->second.definition == ref.definition)
            {
                mobs.emplace(vob.id.value, std::move(it->second));
                return;
            }
            MobRuntime m;
            m.definition = ref.definition;
            const script::Instance* def =
                m_scripts ? m_scripts->findInstance("Mob", ref.definition) : nullptr;
            if (def != nullptr)
            {
                m.name = std::string(def->fields["name"].asString());
                m.type = std::string(def->fields["type"].asString());
                m.lock = std::string(def->fields["lock"].asString());
                m.key = std::string(def->fields["key"].asString());
                m.owner = std::string(def->fields["owner"].asString());
                if (const script::Table* contents = def->fields["contents"].asTable())
                {
                    for (const auto& [item, count] : contents->fields)
                    {
                        m.contents[item] = static_cast<u32>(std::max<i64>(1, count.asInteger(1)));
                    }
                }
            }
            else
            {
                // A bare type ("chest") works too: unlocked, empty, named after the type.
                m.type = ref.definition;
                m.name = ref.definition;
            }
            if (!m.lock.empty() && !gameplay::Lockpick::validCombination(m.lock))
            {
                G7_LOG_WARN("engine", "mob {}: lock \"{}\" must be L and R only - ignored", m.definition,
                            m.lock);
                m.lock.clear();
            }
            m.locked = !m.lock.empty();
            if (m_mobTypes.find(m.type) == nullptr)
            {
                G7_LOG_WARN("engine", "mob vob {} ({}): type \"{}\" is not in data/mobs.toml", vob.id.value,
                            vob.nameText, m.type);
            }
            const entt::entity e = m_scene.findById(vob.id);
            m.closedRotation = std::as_const(m_scene).get<Transform>(e)->rotation;
            if (ref.open && m.type == "door")
            {
                // Open from the start (world file): turned to the open angle with the next swing step.
                m.open = true;
                m.doorFrom = kDoorOpenAngle;
                m.doorTo = kDoorOpenAngle;
                m.doorTime = 0.0f;
            }
            mobs.emplace(vob.id.value, std::move(m));
        });
    m_mobs = std::move(mobs);
}

void Engine::storeDoors(world::WorldFile& file) const
{
    // Doors are written closed with their state (components.mob.open), not at the angle they stand at.
    for (world::WorldFileVob& vob : file.vobs)
    {
        if (const auto it = m_mobs.find(vob.id.value); it != m_mobs.end() && it->second.type == "door")
        {
            vob.transform.rotation = it->second.closedRotation;
            vob.mob.open = it->second.open;
        }
    }
}

std::optional<MobInfo> Engine::mobInfo(world::VobId vob) const
{
    const auto it = m_mobs.find(vob.value);
    if (it == m_mobs.end())
    {
        return std::nullopt;
    }
    const MobRuntime& m = it->second;
    MobInfo info{m.definition, m.type, m.name, m.locked, m.open, {}};
    for (const auto& [item, count] : m.contents)
    {
        info.contents.push_back({item, count});
    }
    return info;
}

std::optional<world::VobId> Engine::findMob(std::string_view vobName) const
{
    const entt::entity e = m_scene.findByName(StringId(vobName));
    if (e == entt::null || !m_scene.has<world::MobRef>(e))
    {
        return std::nullopt;
    }
    return m_scene.idOf(e);
}

void Engine::notice(std::string text)
{
    G7_LOG_INFO("engine", "{}", text);
    showNotice(std::move(text), kNoticeSeconds);
}

Result<void> Engine::useFocusedMob()
{
    if (!m_focus || m_focus->kind != gameplay::FocusKind::Mob)
    {
        return Error{"no mob in focus"};
    }
    return useMob(world::VobId{m_focus->id});
}

Result<void> Engine::useMob(world::VobId vob)
{
    if (m_mobUse || m_pickup)
    {
        return Error{"busy"};
    }
    if (!m_player.valid() || !m_hero)
    {
        return Error{"no hero"};
    }
    const auto it = m_mobs.find(vob.value);
    const entt::entity e = m_scene.findById(vob);
    if (it == m_mobs.end() || e == entt::null)
    {
        return Error{std::format("no mob {}", vob.value)};
    }
    MobRuntime& m = it->second;
    const gameplay::MobType* type = m_mobTypes.find(m.type);
    if (type == nullptr || type->slots.empty())
    {
        return Error{std::format("mob type \"{}\" has no slots (data/mobs.toml)", m.type)};
    }
    const Mat4 world = mobRestMatrix(vob);
    // Slots NPCs sit on are taken (the hero sits down beside them where one is free).
    const auto place = gameplay::chooseSlot(*type, world, m_player.feet(), busySlots(m, "hero"));
    if (!place)
    {
        notice("Hier ist kein Platz.");
        if (m_scripts)
        {
            const script::Value args[] = {m.definition};
            m_scripts->emit("mob_full", args);
        }
        return Error{"no free slot"};
    }
    bool picklock = false;
    if (m.locked)
    {
        if (!m.key.empty() && m_hero->itemCount(m.key) > 0)
        {
            m.locked = false; // the key opens it on the way (Gothic: no extra step)
            notice(std::format("{} aufgeschlossen.", m.name));
        }
        else if (const std::string pick = lockpickItem(); m_hero->itemCount(pick) > 0)
        {
            picklock = true;
        }
        else
        {
            notice("Verschlossen.");
            if (m_scripts)
            {
                const script::Value args[] = {m.definition};
                m_scripts->emit("mob_locked", args);
            }
            return Error{"locked"};
        }
    }
    MobUse use;
    use.vob = vob;
    use.type = m.type;
    use.place = *place;
    use.fromFeet = m_player.feet();
    use.fromYaw = m_movement.yaw();
    use.approachSeconds = std::clamp(glm::length(place->feet - use.fromFeet) / kApproachSpeed, 0.1f, 1.5f);
    use.picklock = picklock;
    if (picklock)
    {
        use.lockpick.emplace(m.lock);
    }
    m.occupants[static_cast<u32>(place->index)] = "hero";
    m_mobUse = std::move(use);
    m_mobEvents.clear();
    return {};
}

Mat4 Engine::mobRestMatrix(world::VobId vob)
{
    // The slots belong to the mob at rest: an open door's slots stay where the closed door has them.
    const entt::entity e = m_scene.findById(vob);
    const auto it = m_mobs.find(vob.value);
    if (e == entt::null || it == m_mobs.end())
    {
        return Mat4(1.0f);
    }
    m_scene.updateTransforms();
    Transform closed = *std::as_const(m_scene).get<Transform>(e);
    closed.rotation = it->second.closedRotation;
    const entt::entity parent = m_scene.parent(e);
    return (parent != entt::null ? m_scene.worldMatrix(parent) : Mat4(1.0f)) * closed.toMatrix();
}

u32 Engine::busySlots(const MobRuntime& m, std::string_view except) const
{
    u32 busy = 0;
    for (const auto& [slot, who] : m.occupants)
    {
        if (who != except && slot < 32)
        {
            busy |= 1u << slot;
        }
    }
    return busy;
}

std::string Engine::lockpickItem() const
{
    const script::Value lp = m_scripts ? m_scripts->global("Lockpicking") : script::Value();
    return lp["item"].isString() ? std::string(lp["item"].asString()) : std::string("it_lockpick");
}

f32 Engine::lockpickBreakChance() const
{
    // Lockpicking.break_chance[talent] from the scripts; without it the owner's values (50 / 25 / 5 %).
    const i32 talent = m_hero ? std::clamp(m_hero->talent("picklock"), 0, 2) : 0;
    const script::Value lp = m_scripts ? m_scripts->global("Lockpicking") : script::Value();
    if (const script::Table* chances = lp["break_chance"].asTable();
        chances != nullptr && static_cast<usize>(talent) < chances->array.size() &&
        chances->array[static_cast<usize>(talent)].isNumber())
    {
        return std::clamp(static_cast<f32>(chances->array[static_cast<usize>(talent)].asNumber()), 0.0f,
                          1.0f);
    }
    constexpr f32 kDefaults[] = {0.5f, 0.25f, 0.05f};
    return kDefaults[talent];
}

std::optional<std::string_view> Engine::mobPhase() const noexcept
{
    if (!m_mobUse)
    {
        return std::nullopt;
    }
    return phaseName(static_cast<int>(m_mobUse->phase));
}

void Engine::lockpickNoticed(const MobRuntime& m)
{
    if (!m.owner.empty())
    {
        const script::Value seen[] = {m.owner, m.definition};
        witnessed("assess_use_mob", seen); // picking somebody else's lock
    }
}

void Engine::mobCommand(MobCommand command)
{
    if (!m_mobUse)
    {
        return;
    }
    MobUse& use = *m_mobUse;
    if (command == MobCommand::Leave)
    {
        use.leaveRequested = true;
        return;
    }
    if (use.phase != MobUse::Phase::Picklock || !use.lockpick || !m_hero)
    {
        return;
    }
    MobRuntime& m = m_mobs.at(use.vob.value);
    if (m_player.valid())
    {
        emitNoise(m_player.feet(), noiseRadius("lockpick"), "lockpick"); // every turn clicks (M9 part C)
    }
    const f32 roll = m_random ? m_random() : std::uniform_real_distribution<f32>(0.0f, 1.0f)(m_rng);
    switch (use.lockpick->turn(command == MobCommand::TurnLeft ? 'L' : 'R', roll, lockpickBreakChance()))
    {
    case gameplay::Lockpick::Result::Progress:
        use.lockpickResult = "Klick.";
        break;
    case gameplay::Lockpick::Result::Reset:
        use.lockpickResult = "Der Dietrich rutscht ab - von vorn.";
        break;
    case gameplay::Lockpick::Result::Broken:
    {
        (void)m_hero->removeItem(lockpickItem());
        use.lockpickResult = "Der Dietrich ist abgebrochen.";
        notice(use.lockpickResult);
        if (m_scripts)
        {
            const script::Value args[] = {m.definition};
            m_scripts->emit("lockpick_broken", args);
        }
        lockpickNoticed(m); // M9 part C
        if (m_hero->itemCount(lockpickItem()) == 0)
        {
            use.leaveRequested = true;
        }
        break;
    }
    case gameplay::Lockpick::Result::Opened:
        m.locked = false;
        use.lockpickResult = "Das Schloss springt auf.";
        notice(use.lockpickResult);
        if (m_scripts)
        {
            const script::Value args[] = {m.definition};
            m_scripts->emit("lock_picked", args);
        }
        lockpickNoticed(m); // M9 part C
        break;
    }
}

bool Engine::playMobState(std::string_view state)
{
    if (m_figure && m_figure->animator.hasState(state))
    {
        m_figure->animator.enter(state, 0.15f);
        return true;
    }
    return false;
}

bool Engine::mobClipDone(const MobUse& use) const
{
    if (use.animated)
    {
        return m_figure && (m_figure->animator.state() != use.state || m_figure->animator.stateEnded());
    }
    return use.time >= kFallbackClipSeconds;
}

void Engine::startMobPhase(MobUse& use, MobUse::Phase phase)
{
    use.phase = phase;
    use.time = 0.0f;
    use.eventFired = false;
    m_mobEvents.clear();
    const char* suffix = phase == MobUse::Phase::Picklock ? "_picklock"
                         : phase == MobUse::Phase::Enter  ? "_enter"
                         : phase == MobUse::Phase::Loop   ? "_loop"
                                                          : "_leave";
    use.state = use.type + suffix;
    use.animated = playMobState(use.state);
}

void Engine::finishMobUse()
{
    if (!m_mobUse)
    {
        return;
    }
    if (m_figure && m_figure->animator.hasState("move"))
    {
        m_figure->animator.enter("move", 0.25f);
    }
    if (m_mobUse->containerOpen)
    {
        m_inventoryOpen = false;
    }
    if (m_player.valid())
    {
        m_player.teleport(m_player.feet()); // back into the physics, settled on the ground
    }
    if (const auto it = m_mobs.find(m_mobUse->vob.value); it != m_mobs.end())
    {
        std::erase_if(it->second.occupants, [](const auto& o) { return o.second == "hero"; });
    }
    m_mobUse.reset();
}

void Engine::approachMob(f32 seconds)
{
    MobUse& use = *m_mobUse;
    use.time += seconds;
    const f32 t = std::clamp(use.time / use.approachSeconds, 0.0f, 1.0f);
    const f32 smooth = t * t * (3.0f - 2.0f * t);
    m_player.moveTo(glm::mix(use.fromFeet, use.place.feet, smooth));
    m_movement.setYaw(use.fromYaw + wrapAngle(use.place.yaw - use.fromYaw) * smooth);
    if (t >= 1.0f)
    {
        m_movement.setYaw(use.place.yaw);
        startMobPhase(use, use.picklock ? MobUse::Phase::Picklock : MobUse::Phase::Enter);
    }
}

void Engine::fixedUpdateMobs(f32 seconds)
{
    swingDoors(seconds);
    if (!m_mobUse || m_mobUse->phase == MobUse::Phase::Approach)
    {
        return; // the approach moves the hero in movePlayer
    }
    MobUse& use = *m_mobUse;
    use.time += seconds;
    MobRuntime& m = m_mobs.at(use.vob.value);
    const gameplay::MobType* type = m_mobTypes.find(m.type);
    const bool hasEvent = [&]
    {
        for (const std::string& e : m_mobEvents)
        {
            if (e == "open" || e == "close")
            {
                return true;
            }
        }
        return false;
    }();
    // Whether the clip's moment has come: its event ("open"/"close"); late in the clip if it has none; half
    // way without a clip.
    const bool moment = hasEvent || (!use.animated && use.time >= 0.5f * kFallbackClipSeconds) ||
                        (use.animated && mobClipDone(use)) ||
                        (use.animated && m_figure && m_figure->animator.stateProgress() >= 0.75f);
    switch (use.phase)
    {
    case MobUse::Phase::Approach:
        break;
    case MobUse::Phase::Picklock:
        if (!m.locked)
        {
            startMobPhase(use, MobUse::Phase::Enter);
        }
        else if (use.leaveRequested)
        {
            finishMobUse();
        }
        break;
    case MobUse::Phase::Enter:
        if (!use.eventFired && moment)
        {
            use.eventFired = true;
            if (m.type == "door")
            {
                m.open = !m.open;
                m.doorFrom = m.doorAngle;
                m.doorTo = m.open ? kDoorOpenAngle : 0.0f;
                m.doorTime = 0.0f;
            }
            else
            {
                m.open = true;
            }
        }
        if (use.eventFired && mobClipDone(use))
        {
            if (m_scripts)
            {
                const script::Value args[] = {m.definition, m.type};
                m_scripts->emit("mob_used", args);
                if (!m.owner.empty())
                {
                    const script::Value seen[] = {m.owner, m.definition};
                    witnessed("assess_use_mob", seen); // somebody else's chest or door (M9 part C)
                }
                const script::Instance* def = m_scripts->findInstance("Mob", m.definition);
                if (def != nullptr)
                {
                    if (const script::FunctionRef f = def->fields["on_use"].asFunction(); f.valid())
                    {
                        const script::Value fargs[] = {m.definition};
                        (void)m_scripts->call(f, fargs);
                    }
                }
            }
            if (type != nullptr && !type->loop.empty())
            {
                startMobPhase(use, MobUse::Phase::Loop);
                if (m.type == "chest")
                {
                    use.containerOpen = true;
                    m_inventoryOpen = true;
                    m_inventoryMessage.clear();
                }
            }
            else
            {
                finishMobUse();
            }
        }
        break;
    case MobUse::Phase::Loop:
        if (use.strikesLeft > 0)
        {
            // Forging: each hit_anvil of s_work is one blow (without the clip: one per 0.7 s).
            u32 blows = 0;
            for (const std::string& e : m_mobEvents)
            {
                blows += e == "hit_anvil" ? 1 : 0;
            }
            use.strikeTimer += seconds;
            if (!use.animated && use.strikeTimer >= kStrikeFallbackSeconds)
            {
                use.strikeTimer = 0.0f;
                blows = 1;
            }
            use.strikesLeft -= std::min(use.strikesLeft, blows);
            if (use.strikesLeft == 0)
            {
                finishRecipe(use);
            }
        }
        if (use.leaveRequested || (use.containerOpen && !m_inventoryOpen))
        {
            use.containerOpen = false;
            m_inventoryOpen = false;
            if (type != nullptr && !type->leave.empty())
            {
                startMobPhase(use, MobUse::Phase::Leave);
            }
            else
            {
                m.open = false;
                finishMobUse();
            }
        }
        break;
    case MobUse::Phase::Leave:
        if (!use.eventFired && moment)
        {
            use.eventFired = true;
            m.open = false;
        }
        if (use.eventFired && mobClipDone(use))
        {
            finishMobUse();
        }
        break;
    }
    m_mobEvents.clear();
}

void Engine::swingDoors(f32 seconds)
{
    for (auto& [id, m] : m_mobs)
    {
        if (m.doorTime < 0.0f)
        {
            continue;
        }
        m.doorTime = std::min(m.doorTime + seconds / kDoorSwingSeconds, 1.0f);
        const f32 t = m.doorTime;
        m.doorAngle = glm::mix(m.doorFrom, m.doorTo, t * t * (3.0f - 2.0f * t));
        if (m.doorTime >= 1.0f)
        {
            m.doorTime = -1.0f;
        }
        const entt::entity e = m_scene.findById(world::VobId{id});
        if (e == entt::null)
        {
            continue;
        }
        Transform local = *std::as_const(m_scene).get<Transform>(e);
        local.rotation = m.closedRotation * glm::angleAxis(m.doorAngle, Vec3(0.0f, 1.0f, 0.0f));
        m_scene.setTransform(e, local);
        m_scene.updateTransforms();
        const Mat4 world = m_scene.worldMatrix(e);
        for (SceneInstance& instance : m_instances)
        {
            if (instance.vob.value == id)
            {
                instance.transform = world;
                instance.bounds = boundsOf(instance.model->bounds, world);
                m_cullGridDirty = true;
            }
        }
        // The collision turns with it: the body is put in again at the new angle.
        if (const auto body = m_mobBodies.find(id); body != m_mobBodies.end() && m_physics.valid())
        {
            m_physics.remove(body->second.body);
            Vec3 position;
            Quat rotation;
            Vec3 scale;
            decomposeMatrix(world, position, rotation, scale);
            auto added =
                m_physics.addStatic(body->second.shape, position, rotation, scale, physics::Layer::World, id);
            if (added)
            {
                body->second.body = added.value();
            }
        }
    }
}

Result<void> Engine::takeFromMob(world::VobId vob, std::string_view item, u32 count)
{
    const auto it = m_mobs.find(vob.value);
    if (it == m_mobs.end() || !m_hero)
    {
        return Error{"no such mob"};
    }
    auto& contents = it->second.contents;
    const auto found = contents.find(item);
    if (found == contents.end() || found->second < count)
    {
        return Error{std::format("not {} x \"{}\" in it", count, item)};
    }
    found->second -= count;
    if (found->second == 0)
    {
        contents.erase(found);
    }
    m_hero->addItem(item, count);
    if (!it->second.owner.empty() && m_scripts)
    {
        const script::Value theft[] = {it->second.owner, std::string(item), static_cast<i64>(count)};
        m_scripts->emit("theft", theft); // somebody else's chest (M8 part D)
        witnessed("assess_theft", theft);
    }
    return {};
}

Result<void> Engine::putIntoMob(world::VobId vob, std::string_view item, u32 count)
{
    const auto it = m_mobs.find(vob.value);
    if (it == m_mobs.end() || !m_hero)
    {
        return Error{"no such mob"};
    }
    if (!m_hero->removeItem(item, count))
    {
        return Error{std::format("the hero has no {} x \"{}\"", count, item)};
    }
    it->second.contents[std::string(item)] += count;
    return {};
}

void Engine::lockpickUi()
{
    if (!m_mobUse || m_mobUse->phase != MobUse::Phase::Picklock || !m_mobUse->lockpick || !m_hero)
    {
        return;
    }
    MobUse& use = *m_mobUse;
    ui::LockpickPanel panel;
    panel.title = m_mobs.at(use.vob.value).name;
    panel.progress = use.lockpick->progress();
    panel.length = use.lockpick->length();
    panel.picks = m_hero->itemCount(lockpickItem());
    panel.hint = "links/rechts: drehen (Drehen- oder Seitwärts-Tasten), rückwärts: aufhören";
    panel.result = use.lockpickResult;
    m_debugUi.lockpickPanel(panel);
    if (panel.turn != 0)
    {
        mobCommand(panel.turn == 'L' ? MobCommand::TurnLeft : MobCommand::TurnRight);
    }
    if (panel.leave)
    {
        mobCommand(MobCommand::Leave);
    }
}

void Engine::mobInput()
{
    using platform::Action;
    if (!m_mobUse)
    {
        return;
    }
    if (m_actions.pressed(m_input, Action::MoveBack))
    {
        mobCommand(MobCommand::Leave);
    }
    if (m_mobUse && m_mobUse->phase == MobUse::Phase::Picklock)
    {
        if (m_actions.pressed(m_input, Action::TurnLeft) || m_actions.pressed(m_input, Action::StrafeLeft))
        {
            mobCommand(MobCommand::TurnLeft);
        }
        if (m_actions.pressed(m_input, Action::TurnRight) || m_actions.pressed(m_input, Action::StrafeRight))
        {
            mobCommand(MobCommand::TurnRight);
        }
    }
    if (m_mobUse && m_mobUse->phase == MobUse::Phase::Loop &&
        (m_actions.pressed(m_input, Action::Action) || m_actions.pressed(m_input, Action::Use)))
    {
        mobCommand(MobCommand::Leave);
    }
}
} // namespace g7

namespace g7
{
void Engine::bindMobFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    const auto mobByName = [this](std::span<const Value> a) -> Result<world::VobId>
    {
        if (a.empty() || !a[0].isString())
        {
            return Error{"argument 1 must be the mob vob's name"};
        }
        const auto vob = findMob(a[0].asString());
        if (!vob || !m_mobs.contains(vob->value))
        {
            return Error{std::format("no mob vob \"{}\" in this world", a[0].asString())};
        }
        return *vob;
    };
    vm.bind({"mob_state", "mob_state(vob: string) -> {definition, type, name, locked, open}",
             "Zustand eines Mob-Vobs dieser Welt (Name des Vobs, z. B. \"LAGER_TRUHE\").", "Mobs",
             [this, mobByName](std::span<const Value> a) -> Result<Value>
             {
                 auto vob = mobByName(a);
                 if (!vob)
                 {
                     return vob.error();
                 }
                 const MobRuntime& m = m_mobs.at(vob.value().value);
                 return script::makeTable({}, {{"definition", m.definition},
                                               {"type", m.type},
                                               {"name", m.name},
                                               {"locked", m.locked},
                                               {"open", m.open}});
             }});
    vm.bind({"unlock", "unlock(vob: string)",
             "Schließt ein Mob-Vob auf (Truhe, Tür), etwa wenn eine Quest es öffnet.", "Mobs",
             [this, mobByName](std::span<const Value> a) -> Result<Value>
             {
                 auto vob = mobByName(a);
                 if (!vob)
                 {
                     return vob.error();
                 }
                 m_mobs.at(vob.value().value).locked = false;
                 return Value();
             }});
    vm.bind({"mob_used",
             "on(\"mob_used\", fn(mob: string, type: string))",
             "Der Held hat ein Mob benutzt (Truhe offen, Tür bewegt); `mob` ist die Mob-Instanz.",
             "Ereignisse",
             {}});
    vm.bind({"mob_locked",
             "on(\"mob_locked\", fn(mob: string))",
             "Der Held wollte ein verschlossenes Mob benutzen, ohne Schlüssel und Dietrich.",
             "Ereignisse",
             {}});
    vm.bind({"lock_picked",
             "on(\"lock_picked\", fn(mob: string))",
             "Ein Schloss wurde mit dem Dietrich geknackt.",
             "Ereignisse",
             {}});
    vm.bind({"item_crafted",
             "on(\"item_crafted\", fn(recipe: string))",
             "Der Held hat an einem Mob etwas hergestellt (Amboss: nach seinen Schlägen).",
             "Ereignisse",
             {}});
    vm.bind(
        {"slept",
         "on(\"slept\", fn(hour: integer))",
         "Der Held hat im Bett bis zu dieser Stunde geschlafen (8, 12, 20 oder 0); LP und Mana sind voll.",
         "Ereignisse",
         {}});
    vm.bind({"lockpick_broken",
             "on(\"lockpick_broken\", fn(mob: string))",
             "Beim Knacken ist ein Dietrich abgebrochen.",
             "Ereignisse",
             {}});
}
} // namespace g7

namespace g7
{
std::vector<const script::Instance*> Engine::recipesFor(std::string_view type) const
{
    std::vector<const script::Instance*> out;
    if (!m_scripts)
    {
        return out;
    }
    for (const script::Instance* recipe : m_scripts->instancesOf("Recipe"))
    {
        if (recipe->fields["mob"].asString() == type)
        {
            out.push_back(recipe);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->name < b->name; });
    return out;
}

std::vector<std::string> Engine::mobChoices() const
{
    std::vector<std::string> out;
    if (!m_mobUse || m_mobUse->phase != MobUse::Phase::Loop || m_mobUse->leaveRequested)
    {
        return out;
    }
    if (m_mobUse->type == "bed")
    {
        for (const auto& [hour, label] : kSleepUntil)
        {
            out.emplace_back(label);
        }
    }
    else if (m_mobUse->strikesLeft == 0)
    {
        for (const script::Instance* recipe : recipesFor(m_mobUse->type))
        {
            out.emplace_back(recipe->fields["name"].asString());
        }
    }
    return out;
}

Result<void> Engine::chooseMobOption(usize index)
{
    if (!m_mobUse || !m_hero || index >= mobChoices().size())
    {
        return Error{"nothing to choose"};
    }
    MobUse& use = *m_mobUse;
    if (use.type == "bed")
    {
        const auto [hour, label] = kSleepUntil[index];
        m_gameTime.advanceTo(hour, 0);
        // Rested (Gothic 1): hit points and mana full.
        (void)m_hero->setAttribute("hp", m_hero->attribute("hp_max"));
        (void)m_hero->setAttribute("mana", m_hero->attribute("mana_max"));
        notice(std::format("Ausgeschlafen - Tag {}, {:02}:00.", m_gameTime.day(), hour));
        if (m_scripts)
        {
            const script::Value args[] = {static_cast<i64>(hour)};
            m_scripts->emit("slept", args);
        }
        use.leaveRequested = true; // gets up
        return {};
    }
    const script::Instance* recipe = recipesFor(use.type)[index];
    if (const script::Table* takes = recipe->fields["takes"].asTable())
    {
        for (const auto& [item, count] : takes->fields)
        {
            if (m_hero->itemCount(item) < static_cast<u32>(std::max<i64>(1, count.asInteger(1))))
            {
                const script::Instance* needed = m_scripts->findInstance("Item", item);
                use.choiceMessage = std::format("Dafür fehlt: {}.",
                                                needed != nullptr ? needed->fields["name"].asString() : item);
                return Error{use.choiceMessage};
            }
        }
    }
    use.recipe = recipe->name;
    use.strikesLeft = static_cast<u32>(recipe->fields["strikes"].asInteger(3));
    use.strikeTimer = 0.0f;
    use.choiceMessage.clear();
    return {};
}

void Engine::finishRecipe(MobUse& use)
{
    const script::Instance* recipe = m_scripts ? m_scripts->findInstance("Recipe", use.recipe) : nullptr;
    if (recipe == nullptr || !m_hero)
    {
        return;
    }
    // The material went into the work: taken now, the result given (all or nothing).
    const script::Table* takes = recipe->fields["takes"].asTable();
    const script::Table* gives = recipe->fields["gives"].asTable();
    if (takes != nullptr)
    {
        for (const auto& [item, count] : takes->fields)
        {
            if (m_hero->itemCount(item) < static_cast<u32>(std::max<i64>(1, count.asInteger(1))))
            {
                use.choiceMessage = "Das Material ist weg.";
                use.recipe.clear();
                return;
            }
        }
        for (const auto& [item, count] : takes->fields)
        {
            (void)m_hero->removeItem(item, static_cast<u32>(std::max<i64>(1, count.asInteger(1))));
        }
    }
    if (gives != nullptr)
    {
        for (const auto& [item, count] : gives->fields)
        {
            m_hero->addItem(item, static_cast<u32>(std::max<i64>(1, count.asInteger(1))));
        }
    }
    use.choiceMessage = std::format("{} fertig.", recipe->fields["name"].asString());
    notice(use.choiceMessage);
    if (m_scripts)
    {
        const script::Value args[] = {use.recipe};
        m_scripts->emit("item_crafted", args);
    }
    use.recipe.clear();
}

void Engine::choiceUi()
{
    if (!m_mobUse || m_mobUse->phase != MobUse::Phase::Loop || m_mobUse->containerOpen)
    {
        return;
    }
    MobUse& use = *m_mobUse;
    ui::ChoicePanel panel;
    panel.title = m_mobs.at(use.vob.value).name;
    panel.options = mobChoices();
    if (use.strikesLeft > 0)
    {
        panel.message = std::format("Schmiedet ... noch {} Schläge", use.strikesLeft);
    }
    else
    {
        panel.message = use.choiceMessage;
    }
    if (panel.options.empty() && use.type != "anvil")
    {
        return;
    }
    m_debugUi.choicePanel(panel);
    if (panel.chosen >= 0)
    {
        (void)chooseMobOption(static_cast<usize>(panel.chosen));
    }
    if (panel.cancel)
    {
        mobCommand(MobCommand::Leave);
    }
}
} // namespace g7
