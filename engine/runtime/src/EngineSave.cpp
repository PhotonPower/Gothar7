// The game state for save games (M15 parts B and C, docs/modules/save.md): what the engine captures from the
// running game and puts back - time, Story, named timers, the hero, and per visited world its delta against
// the file, the dropped items, mobs, NPCs and spent triggers. A world left through a level change keeps the
// same state in memory and comes back with it. NPCs come back where they were with their values and begin the
// state of their routine anew (plan point 2).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace g7
{
namespace
{
/// The Npc instance of a name for scripts ("mon_wolf#2" -> "mon_wolf").
std::string instanceOf(std::string_view name)
{
    return std::string(name.substr(0, name.find('#')));
}
} // namespace

Result<world::WorldFile> Engine::worldBase(const std::string& path)
{
    const std::string key = toLower(path);
    if (const auto it = m_worldBases.find(key); it != m_worldBases.end())
    {
        return *it->second;
    }
    auto file = world::loadWorldFile(m_vfs, path);
    if (!file)
    {
        return file.error();
    }
    auto shared = std::make_shared<const world::WorldFile>(std::move(file).value());
    m_worldBases.emplace(key, shared);
    return *shared;
}

Result<save::WorldState> Engine::captureCurrentWorld()
{
    save::WorldState state;
    state.path = m_worldPath;
    auto base = worldBase(m_worldPath);
    if (!base)
    {
        return Error{std::format("{}: {}", m_worldPath, base.error().message)};
    }
    world::WorldFile now = world::captureWorld(m_scene, m_worldFile.name);
    storeDoors(now);
    state.delta = save::diffWorld(base.value(), now);
    state.delta.nextRuntimeVobId = m_scene.nextRuntimeVobId();
    // Dropped and inserted items (runtime vobs): as item vobs of the file, with their runtime ids.
    for (const WorldItem& item : m_worldItems)
    {
        if (!item.vob.runtime())
        {
            continue;
        }
        const entt::entity e = m_scene.findById(item.vob);
        const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
        const Transform* t = e != entt::null ? std::as_const(m_scene).get<Transform>(e) : nullptr;
        if (ref == nullptr || t == nullptr)
        {
            continue;
        }
        world::WorldFileVob vob;
        vob.id = item.vob;
        vob.type = world::VobType::Item;
        vob.name = m_scene.get<world::Vob>(e)->nameText;
        vob.transform = *t;
        vob.item = *ref;
        state.delta.runtime.push_back(std::move(vob));
    }
    std::sort(state.delta.runtime.begin(), state.delta.runtime.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    state.spentTriggers.assign(m_triggers.spentTriggers().begin(), m_triggers.spentTriggers().end());
    for (const auto& [vob, m] : m_mobs)
    {
        state.mobs.push_back({vob, m.definition, m.locked, m.open, m.contents});
    }
    std::sort(state.mobs.begin(), state.mobs.end(),
              [](const auto& a, const auto& b) { return a.vob < b.vob; });
    for (const auto& c : m_creatures)
    {
        const std::string instance = instanceOf(c->species);
        if (!m_scripts || m_scripts->findInstance("Npc", instance) == nullptr)
        {
            continue; // a debug creature (Creatures window): not part of the game
        }
        save::NpcState npc;
        npc.name = c->species;
        npc.instance = instance;
        npc.position = c->position;
        npc.yaw = c->yaw;
        if (c->character)
        {
            npc.character = c->character->state();
        }
        npc.routine = c->routine;
        npc.life = c->vanished                                        ? save::Life::Vanished
                   : c->dead                                          ? save::Life::Dead
                   : c->fighter.state() == gameplay::FightState::Down ? save::Life::Down
                                                                      : save::Life::Alive;
        if (npc.life == save::Life::Down)
        {
            npc.downSeconds = std::max(0.5f, m_combat.knockoutSeconds - c->fighter.seconds());
        }
        npc.pickpocketed = c->pickpocketed;
        npc.summoned = c->summoned;
        npc.summonSeconds = c->summoned ? c->summonSeconds : 0.0f;
        state.npcs.push_back(std::move(npc));
    }
    return state;
}

Result<save::GameState> Engine::captureGame()
{
    if (!m_scripts)
    {
        return Error{"no scripts"};
    }
    if (m_worldPath.empty())
    {
        return Error{"no world loaded"};
    }
    save::GameState state;
    state.clock = m_gameTime.clock();
    state.secondsPerMinute = m_gameTime.secondsPerMinute();
    state.scriptClock = m_scripts->time();
    state.nextTimerId = m_scripts->nextTimerId();
    state.timers = m_scripts->namedTimers();
    auto story = m_scripts->storyForSave();
    if (!story)
    {
        return story.error(); // a function in Story: a content error
    }
    state.story = std::move(story).value();
    if (m_hero && m_player.valid())
    {
        save::HeroState hero;
        hero.character = m_hero->state();
        hero.position = m_player.feet();
        hero.yaw = m_movement.yaw();
        hero.torch = m_torch ? m_torch->item : std::string();
        state.hero = std::move(hero);
    }
    state.currentWorld = m_worldPath;
    auto current = captureCurrentWorld();
    if (!current)
    {
        return current.error();
    }
    // The worlds left earlier (in path order), then the current one.
    for (const auto& [path, left] : m_leftWorlds)
    {
        state.worlds.push_back(left);
    }
    state.worlds.push_back(std::move(current).value());
    return state;
}

Result<void> Engine::restoreWorldState(const save::WorldState& state)
{
    // Items dropped while playing, with the ids they had.
    for (const world::WorldFileVob& vob : state.delta.runtime)
    {
        auto spawned = spawnItemAt(vob.item.instance, vob.item.count, vob.transform, vob.id);
        if (!spawned)
        {
            G7_LOG_WARN("save", "{}: item {} not put back: {}", state.path, vob.item.instance,
                        spawned.error().message);
            continue;
        }
        if (const entt::entity e = m_scene.findById(spawned.value());
            e != entt::null && !vob.item.owner.empty())
        {
            world::ItemRef ref = *m_scene.get<world::ItemRef>(e);
            ref.owner = vob.item.owner;
            m_scene.set<world::ItemRef>(e, ref);
        }
    }
    if (auto raised = m_scene.setNextRuntimeVobId(state.delta.nextRuntimeVobId); !raised)
    {
        G7_LOG_WARN("save", "{}: {}", state.path, raised.error().message);
    }
    m_triggers.setSpentTriggers(std::set<u64>(state.spentTriggers.begin(), state.spentTriggers.end()));
    for (const save::MobState& saved : state.mobs)
    {
        const auto it = m_mobs.find(saved.vob);
        if (it == m_mobs.end() || it->second.definition != saved.definition)
        {
            G7_LOG_WARN("save", "{}: mob {} ({}) not in the world any more", state.path, saved.vob,
                        saved.definition);
            continue;
        }
        MobRuntime& m = it->second;
        m.locked = saved.locked;
        m.open = saved.open;
        m.contents.clear();
        for (const auto& [item, count] : saved.contents)
        {
            m.contents.emplace(item, count);
        }
    }
    // The NPCs: those the world's scripts put in are replaced by the saved ones.
    m_creatures.clear();
    for (const save::NpcState& saved : state.npcs)
    {
        auto spawned = spawnNpc(saved.instance, saved.position, saved.yaw);
        if (!spawned)
        {
            G7_LOG_WARN("save", "{}: {} not put back: {}", state.path, saved.name, spawned.error().message);
            continue;
        }
        Creature* c = creature(spawned.value());
        c->species = saved.name;
        c->position = c->positionBefore = saved.position;
        c->yaw = c->yawBefore = saved.yaw;
        if (saved.character && c->character)
        {
            c->character->restore(*saved.character, itemLookup());
        }
        c->routine = saved.routine;
        c->routineEntry = -1; // the state of the routine begins anew
        c->pickpocketed = saved.pickpocketed;
        c->summoned = saved.summoned;
        c->summonSeconds = saved.summonSeconds;
        if (saved.life == save::Life::Down)
        {
            knockOutRestored(*c, saved.downSeconds);
        }
        else if (saved.life == save::Life::Dead || saved.life == save::Life::Vanished)
        {
            killRestored(*c);
            c->vanished = saved.life == save::Life::Vanished;
            if (c->vanished)
            {
                c->body.reset();
            }
        }
    }
    return {};
}

Result<void> Engine::loadWorldState(const save::WorldState& state, std::string_view start)
{
    auto base = worldBase(state.path);
    if (!base)
    {
        return Error{std::format("{}: {}", state.path, base.error().message)};
    }
    world::WorldFile target = save::applyDelta(base.value(), state.delta);
    target.nextVobId = std::max(target.nextVobId, state.delta.nextVobId);
    m_restoringWorld = true;
    auto loaded = loadWorld(state.path, std::move(target), start);
    m_restoringWorld = false;
    if (!loaded)
    {
        return loaded;
    }
    return restoreWorldState(state);
}

Result<void> Engine::applyGame(const save::GameState& state)
{
    if (!m_scripts)
    {
        return Error{"no scripts"};
    }
    const auto current = std::find_if(state.worlds.begin(), state.worlds.end(), [&](const save::WorldState& w)
                                      { return equalsIgnoreCase(w.path, state.currentWorld); });
    if (current == state.worlds.end())
    {
        return Error{std::format("the game state has no world '{}'", state.currentWorld)};
    }
    if (auto base = worldBase(current->path); !base) // checked before anything is torn down
    {
        return Error{std::format("{}: {}", current->path, base.error().message)};
    }

    // Hero values first: the player figure is built from them when the world loads.
    if (state.hero && m_hero)
    {
        m_hero->restore(state.hero->character, itemLookup());
    }
    m_gameTime.setSecondsPerMinute(state.secondsPerMinute);
    m_gameTime.restore(state.clock);
    m_routineMinute = ~0ull; // routines put everybody where they belong now

    m_pendingWorldChange.reset();
    // The other visited worlds wait as they were (part C).
    m_leftWorlds.clear();
    for (const save::WorldState& w : state.worlds)
    {
        if (&w != &*current)
        {
            m_leftWorlds[toLower(w.path)] = w;
        }
    }
    if (m_torch)
    {
        putTorchAway();
    }
    unloadWorld();
    if (auto loaded = loadWorldState(*current, {}); !loaded)
    {
        return loaded;
    }
    // Story and timers after the world's handlers ran (they see `restored` and leave both alone).
    if (auto story = m_scripts->setStory(state.story); !story)
    {
        return story;
    }
    m_scripts->restoreTimers(state.scriptClock, state.timers, state.nextTimerId);
    if (state.hero && m_player.valid())
    {
        teleportPlayer(state.hero->position, state.hero->yaw);
        if (!state.hero->torch.empty())
        {
            lightTorch(state.hero->torch);
        }
    }
    if (m_scripts)
    {
        m_scripts->emit("game_loaded");
    }
    G7_LOG_INFO("save", "game state applied: {} ({} NPCs)", current->path, current->npcs.size());
    return {};
}

void Engine::bindSaveFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"game_loaded",
             "on(\"game_loaded\", fn())",
             "Ein Spielstand ist geladen: Welt, NPCs, Story und benannte Timer sind wieder da (M15).",
             "Ereignisse",
             {}});
}
} // namespace g7
