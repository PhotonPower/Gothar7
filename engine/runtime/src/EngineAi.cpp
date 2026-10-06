// NPC behaviour (M9 part B, docs/modules/ai.md "Zustandsautomat", "Tagesabläufe"): each NPC follows its
// routine (Routine: from, to, state, at), runs the script state of the current entry (State: begin, loop,
// finish), and works off a command queue the state fills (walk, walk to a freepoint, turn, play an ambient
// animation, stop, wait, say). NPCs far from the player are not simulated (AI LOD): they jump to where their
// routine wants them.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Mobs.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
/// Ground probes start this far above a point: low enough to stay under the ceiling of a room (welt's houses,
/// 2.4 m and more), high enough for a way point in a hollow of the terrain model.
constexpr f32 kGroundProbeAbove = 0.5f;
// AI LOD: [ai] simulation_distance (80 m) from the player - farther NPCs are not simulated - and back 5 m
// nearer (no flicker at the edge); m_simulationDistance.
constexpr f32 kLoopInterval = 0.5f;     ///< seconds between calls of a state's loop()
constexpr f32 kTurnRate = 5.0f;         ///< radians per second when turning on the spot
constexpr f32 kSayPerCharacter = 0.06f; ///< seconds a line stays, per character (at least kSayMinimum)
constexpr f32 kSayMinimum = 1.5f;

/// "08:30" -> minute of the day; nullopt if malformed.
std::optional<u32> minuteOf(std::string_view text)
{
    if (text.size() != 5 || text[2] != ':')
    {
        return std::nullopt;
    }
    const auto digit = [&](usize i) { return static_cast<u32>(text[i] - '0'); };
    for (const usize i : {0u, 1u, 3u, 4u})
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return std::nullopt;
        }
    }
    const u32 hours = digit(0) * 10 + digit(1);
    const u32 minutes = digit(3) * 10 + digit(4);
    if (hours > 24 || minutes > 59 || (hours == 24 && minutes != 0))
    {
        return std::nullopt;
    }
    return hours * 60 + minutes;
}

/// Whether `minute` (of the day) lies in [from, to), across midnight when to <= from.
bool inWindow(u32 minute, u32 from, u32 to)
{
    return from < to ? (minute >= from && minute < to) : (minute >= from || minute < to);
}

f32 wrapAngle(f32 a)
{
    return std::remainder(a, 2.0f * glm::pi<f32>());
}

constexpr std::string_view kPlayerTarget = "@player"; ///< target of npc_goto_player, npc_turn_to_player
constexpr f32 kFleeStep = 10.0f; ///< metres a fleeing NPC runs straight away from the threat
constexpr f32 kFleeRadius =
    30.0f; ///< metres: a fleeing NPC runs to the way point within this farthest from the player
} // namespace

Creature* Engine::npcNamed(std::string_view instance) noexcept
{
    const auto id = npcByInstance(instance);
    return id ? creature(*id) : nullptr;
}

namespace
{
// The optional `run` argument of the NPC commands: true runs, "trot" trots (animals with a trot, else walks).
void setGait(Creature::Command& cmd, std::span<const script::Value> a, usize index)
{
    if (a.size() <= index)
    {
        return;
    }
    if (a[index].isString())
    {
        cmd.run = a[index].asString() == "run";
        cmd.trot = a[index].asString() == "trot";
        return;
    }
    cmd.run = a[index].asBool();
}
} // namespace

void Engine::npcSays(const Creature& c, std::string_view text)
{
    const std::string key = voiceKey({}, c.species, text); // shouts: svm_<voice>_<m|f>_<occasion>_NN
    G7_LOG_INFO("engine", "{}: \"{}\" [{}]", c.species, text, key);
    if (m_scripts)
    {
        const script::Value args[] = {c.species, std::string(text), key};
        m_scripts->emit("npc_said", args);
    }
    // Until the dialogues (M10): shown near the player.
    if (m_player.valid() && glm::length(c.position - m_player.feet()) < 15.0f)
    {
        const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", c.species) : nullptr;
        notice(std::format("{}: {}", npc != nullptr ? npc->fields["name"].asString() : c.species, text));
    }
}

std::optional<Vec3> Engine::targetPosition(std::string_view target) const
{
    if (target.empty() || target == kPlayerTarget)
    {
        return m_player.valid() ? std::optional<Vec3>(m_player.feet()) : std::nullopt;
    }
    const auto id = npcByInstance(target);
    return id ? std::optional<Vec3>(creature(*id)->position) : std::nullopt;
}

void Engine::releaseFreepoint(Creature& c)
{
    if (c.freepoint >= 0 && static_cast<usize>(c.freepoint) < m_freepointUsers.size() &&
        m_freepointUsers[static_cast<usize>(c.freepoint)] == c.id)
    {
        m_freepointUsers[static_cast<usize>(c.freepoint)] = 0;
    }
    c.freepoint = -1;
}

void Engine::handEvent(Creature& c, std::string_view event)
{
    if (event == "item_from_hand")
    {
        c.handItem = nullptr;
        return;
    }
    if (event != "item_to_hand" || c.handItemWanted.empty() || !c.figure)
    {
        return;
    }
    const i32 bone = c.figure->skeleton.find("socket_hand_r");
    if (bone < 0)
    {
        return;
    }
    c.handBone = static_cast<usize>(bone);
    c.handItem = itemModel(c.handItemWanted);
}

void Engine::finishState(Creature& c)
{
    if (c.state.empty())
    {
        return;
    }
    const std::string state = std::exchange(c.state, std::string());
    c.commands.clear();
    c.commandRunning = false;
    c.route.reset();
    releaseFreepoint(c);
    if (c.stateBegun && m_scripts)
    {
        if (const script::Instance* def = m_scripts->findInstance("State", state))
        {
            if (const script::FunctionRef f = def->fields["finish"].asFunction(); f.valid())
            {
                const script::Value args[] = {c.species};
                if (auto r = m_scripts->call(f, args); !r)
                {
                    G7_LOG_WARN("engine", "{}: {}.finish: {}", c.species, state, r.error().message);
                }
            }
        }
    }
    c.stateBegun = false;
}

void Engine::beginState(Creature& c, std::string_view state, std::string_view at)
{
    finishState(c);
    c.state = std::string(state);
    c.stateAt = std::string(at);
    c.stateLoopTime = 0.0f;
    c.loopTimer = 0.0f;
    c.stateBegun = false;
    if (!c.simulated || !m_scripts)
    {
        return; // begins when the player comes near
    }
    c.stateBegun = true;
    const script::Instance* def = m_scripts->findInstance("State", state);
    if (def == nullptr)
    {
        G7_LOG_WARN("engine", "{}: no State \"{}\"", c.species, state);
        return;
    }
    if (const script::FunctionRef f = def->fields["begin"].asFunction(); f.valid())
    {
        const script::Value args[] = {c.species, std::string(at)};
        if (auto r = m_scripts->call(f, args); !r)
        {
            G7_LOG_WARN("engine", "{}: {}.begin: {}", c.species, state, r.error().message);
        }
    }
}

void Engine::updateRoutine(Creature& c)
{
    if (c.routine.empty() || !m_scripts)
    {
        return;
    }
    const script::Instance* routine = m_scripts->findInstance("Routine", c.routine);
    const script::Table* entries = routine != nullptr ? routine->fields.asTable() : nullptr;
    if (entries == nullptr)
    {
        return;
    }
    const auto minute = static_cast<u32>(m_gameTime.minuteOfDay());
    for (usize i = 0; i < entries->array.size(); ++i)
    {
        const script::Value& e = entries->array[i];
        const auto from = minuteOf(e["from"].asString());
        const auto to = minuteOf(e["to"].asString());
        if (!from || !to || !inWindow(minute, *from, *to % 1440))
        {
            continue;
        }
        if (c.routineEntry == static_cast<i32>(i) && !c.state.empty())
        {
            return; // still this entry
        }
        c.routineEntry = static_cast<i32>(i);
        const std::string at(e["at"].asString());
        if (!c.simulated)
        {
            // Far away: straight to the place of the new entry, its state begins when the player comes near.
            finishState(c);
            c.ambient.clear(); // nobody sees the standing up
            c.ambientPhase = Creature::AmbientPhase::None;
            c.handItem = nullptr;
            if (c.figure && c.figure->animator.hasState("move"))
            {
                c.figure->animator.enter("move", 0.0f);
            }
            if (const auto target = navigationTarget(at); target && c.body)
            {
                c.body->teleport(*target);
                c.position = c.positionBefore = c.body->feet();
            }
        }
        beginState(c, e["state"].asString(), at);
        return;
    }
}

void Engine::fixedUpdateAi(Creature& c, f32 seconds)
{
    // AI LOD by the distance to the player (or the camera without one).
    const Vec3 eye = m_player.valid() ? m_player.feet() : m_camera.transform.position;
    const f32 distance = glm::length(c.position - eye);
    if (c.simulated && distance > m_simulationDistance)
    {
        c.simulated = false;
        if (c.route && !c.route->points.empty() && c.body)
        {
            // Out of the simulation while walking (welt #227): it does not stop silently - it is at its goal
            // at once, as unsimulated NPCs jump to their routine places, and says so.
            const std::string goal = c.routeGoal;
            c.body->teleport(c.route->points.back());
            c.position = c.positionBefore = c.body->feet();
            G7_LOG_DEBUG("engine", "{} leaves the simulation walking to {}: put there", c.species, goal);
            c.route.reset();
            c.replans = 0;
            if (m_scripts)
            {
                const script::Value args[] = {c.species, goal};
                m_scripts->emit("npc_arrived", args);
            }
        }
        c.route.reset();
        c.commands.clear();
        c.commandRunning = false;
    }
    else if (!c.simulated && distance < m_simulationDistance - 5.0f)
    {
        c.simulated = true;
        if (!c.state.empty() && !c.stateBegun)
        {
            beginState(c, std::string(c.state), std::string(c.stateAt));
        }
    }
    // Routines once per game minute (and right after a routine is set).
    if (m_gameTime.totalMinutes() != m_routineMinute || c.routineEntry < 0)
    {
        updateRoutine(c);
    }
    if (!c.simulated)
    {
        return;
    }
    perceive(c, seconds); // M9 part C
    if (c.talking)
    {
        // In a dialogue: stands and faces the player (M10).
        if (m_player.valid())
        {
            const f32 turn = wrapAngle(gameplay::yawOf(m_player.feet() - c.position) - c.yaw);
            c.yaw = wrapAngle(c.yaw + std::clamp(turn, -kTurnRate * seconds, kTurnRate * seconds));
        }
        c.route.reset();
        return;
    }
    if (c.approaching && c.commands.empty() && !c.commandRunning)
    {
        c.approaching = false; // walked up (or gave up): an important Info may start
    }
    runCommands(c, seconds);
    // The state's loop while nothing is queued.
    if (!c.state.empty() && c.stateBegun && c.commands.empty() && !c.commandRunning && m_scripts)
    {
        c.loopTimer += seconds;
        c.stateLoopTime += seconds;
        if (c.loopTimer >= kLoopInterval)
        {
            c.loopTimer = 0.0f;
            const script::Instance* def = m_scripts->findInstance("State", c.state);
            const script::FunctionRef f =
                def != nullptr ? def->fields["loop"].asFunction() : script::FunctionRef{};
            if (f.valid())
            {
                const script::Value args[] = {c.species, static_cast<f64>(c.stateLoopTime)};
                auto r = m_scripts->call(f, args);
                if (!r)
                {
                    G7_LOG_WARN("engine", "{}: {}.loop: {}", c.species, c.state, r.error().message);
                }
                else if (r.value().asString() == "done")
                {
                    finishState(c);
                    c.routineEntry = -1; // the routine starts the state of its entry again
                }
            }
        }
    }
}

bool Engine::startCommand(Creature& c)
{
    using Kind = Creature::Command::Kind;
    Creature::Command& cmd = c.commands.front();
    // Walking or another animation ends an ambient one first (its _out clip).
    const bool leavesAmbient = cmd.kind == Kind::GoTo || cmd.kind == Kind::GoToFreepoint ||
                               (cmd.kind == Kind::Play && cmd.text != c.ambient);
    if (leavesAmbient && c.ambientPhase != Creature::AmbientPhase::None)
    {
        c.commands.push_front({Kind::Stop});
        return startCommand(c);
    }
    animation::Animator* a = c.figure ? &c.figure->animator : nullptr;
    switch (cmd.kind)
    {
    case Kind::GoTo:
        if (cmd.text == kPlayerTarget)
        {
            if (!m_player.valid())
            {
                return false;
            }
            const Vec3 to = m_player.feet() - c.position;
            const f32 distance = glm::length(Vec3(to.x, 0.0f, to.z));
            if (distance <= cmd.distance)
            {
                return false; // near enough already
            }
            const Vec3 goal = m_player.feet() - Vec3(to.x, 0.0f, to.z) / distance * cmd.distance;
            if (auto sent = npcGoToPosition(c.id, goal, "player", cmd.run); !sent)
            {
                G7_LOG_WARN("engine", "{}: {}", c.species, sent.error().message);
                return false;
            }
            return true;
        }
        if (auto sent = npcGoTo(c.id, cmd.text, cmd.run); !sent)
        {
            G7_LOG_WARN("engine", "{}: {}", c.species, sent.error().message);
            return false;
        }
        return true;
    case Kind::GoToFreepoint:
    {
        // The nearest free freepoint of the type within the radius.
        releaseFreepoint(c);
        i32 best = -1;
        f32 bestDistance = cmd.value > 0.0f ? cmd.value : 1e9f;
        const auto& fps = m_waynet.freepoints();
        for (usize i = 0; i < fps.size(); ++i)
        {
            const f32 d = glm::length(fps[i].position - c.position);
            if (m_freepointUsers[i] == 0 && fps[i].type == cmd.text && d <= bestDistance)
            {
                best = static_cast<i32>(i);
                bestDistance = d;
            }
        }
        if (best < 0)
        {
            return false; // none free: stays where it is
        }
        m_freepointUsers[static_cast<usize>(best)] = c.id;
        c.freepoint = best;
        const auto& fp = fps[static_cast<usize>(best)];
        if (fp.dir)
        {
            // After arriving: face the way the freepoint says.
            c.commands.insert(c.commands.begin() + 1, Creature::Command{Kind::Turn, fp.name});
        }
        if (auto sent = npcGoTo(c.id, fp.name, cmd.run); !sent)
        {
            G7_LOG_WARN("engine", "{}: {}", c.species, sent.error().message);
            return false;
        }
        return true;
    }
    case Kind::Turn:
    {
        std::optional<Vec3> dir;
        if (cmd.text == kPlayerTarget && m_player.valid())
        {
            dir = m_player.feet() - c.position;
        }
        else if (const auto wp = m_waynet.find(cmd.text))
        {
            dir = m_waynet.points()[*wp].dir;
        }
        else if (const auto fp = m_waynet.findFreepoint(cmd.text))
        {
            dir = m_waynet.freepoints()[*fp].dir;
        }
        if (!dir)
        {
            return false; // nothing to face
        }
        c.turnTo = gameplay::yawOf(*dir);
        return true;
    }
    case Kind::Play:
    {
        if (c.ambient == cmd.text && c.ambientPhase == Creature::AmbientPhase::Loop)
        {
            return false; // already
        }
        c.ambient = cmd.text;
        c.handItemWanted = cmd.item;
        const std::string in = std::format("amb_{}_in", cmd.text);
        const std::string loop = cmd.text.starts_with("idle") || cmd.text.starts_with("react")
                                     ? cmd.text
                                     : std::format("amb_{}", cmd.text);
        // Animals (M9 part D): their graphs loop eat and sleep while the flag is set, threaten is an action.
        if (a != nullptr && !a->hasState(in) && !a->hasState(loop) && a->hasState(cmd.text))
        {
            if (cmd.text == "threaten")
            {
                c.action = 4;
                c.ambient.clear();
                c.ambientPhase = Creature::AmbientPhase::None;
                return false;
            }
            c.eat = cmd.text == "eat";
            c.sleep = cmd.text == "sleep";
            c.ambientPhase = Creature::AmbientPhase::Loop;
            return false;
        }
        if (a != nullptr && a->hasState(in))
        {
            a->enter(in, 0.2f);
            c.ambientPhase = Creature::AmbientPhase::In;
            return true;
        }
        if (a != nullptr && a->hasState(loop))
        {
            a->enter(loop, 0.2f);
        }
        handEvent(c, "item_to_hand"); // no _in clip to take it with
        c.ambientPhase = Creature::AmbientPhase::Loop;
        return false; // done at once
    }
    case Kind::Stop:
    {
        const std::string out = std::format("amb_{}_out", c.ambient);
        if (c.ambientPhase != Creature::AmbientPhase::None && a != nullptr && a->hasState(out))
        {
            a->enter(out, 0.2f);
            c.ambientPhase = Creature::AmbientPhase::Out;
            return true;
        }
        if (a != nullptr && a->hasState("move") && !c.eat && !c.sleep)
        {
            a->enter("move", 0.25f);
        }
        c.eat = false; // animals: their graph goes back to walking by itself
        c.sleep = false;
        c.ambient.clear();
        c.ambientPhase = Creature::AmbientPhase::None;
        c.handItem = nullptr;
        return false;
    }
    case Kind::Wait:
        return cmd.value > 0.0f;
    case Kind::Follow:
        c.followPlayer = cmd.text == kPlayerTarget;
        return cmd.value > 0.0f && targetPosition(cmd.text).has_value();
    case Kind::Flee:
        return cmd.value > 0.0f && targetPosition(cmd.text).has_value();
    case Kind::GoToPoint:
        if (auto sent = npcGoToPosition(c.id, cmd.point, "point", cmd.run); !sent)
        {
            G7_LOG_WARN("engine", "{}: {}", c.species, sent.error().message);
            return false;
        }
        return true;
    case Kind::Roam:
    {
        // A random point of the ground within the radius around the way point, reachable in a straight line
        // from it (no house, no slope in between); a few tries.
        const auto centre = navigationTarget(cmd.text);
        if (!centre || !m_physics.valid())
        {
            return false;
        }
        std::uniform_real_distribution<f32> unit(0.0f, 1.0f);
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const f32 angle = unit(m_rng) * 2.0f * glm::pi<f32>();
            const f32 r = std::sqrt(unit(m_rng)) * cmd.value;
            const Vec3 p = *centre + Vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
            const auto hit =
                m_physics.raycast(p + Vec3(0.0f, kGroundProbeAbove, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                  kGroundProbeAbove + 6.0f, physics::layerBit(physics::Layer::World));
            if (hit && walkableLine(*centre, hit->position) &&
                npcGoToPosition(c.id, hit->position, "roam", cmd.run).ok())
            {
                return true;
            }
        }
        return false;
    }
    case Kind::Say:
        npcSays(c, cmd.text);
        cmd.value = std::max(kSayMinimum, kSayPerCharacter * static_cast<f32>(cmd.text.size()));
        return true;
    }
    return false;
}

void Engine::runCommands(Creature& c, f32 seconds)
{
    using Kind = Creature::Command::Kind;
    animation::Animator* a = c.figure ? &c.figure->animator : nullptr;
    // A few commands may finish at once in one step (play without _in, stop without _out ...).
    for (int guard = 0; guard < 8; ++guard)
    {
        if (!c.commandRunning)
        {
            if (c.commands.empty())
            {
                return;
            }
            c.commandTime = 0.0f;
            if (!startCommand(c))
            {
                c.commands.pop_front();
                continue;
            }
            c.trotting = c.route && c.commands.front().trot; // a way started at the trot
            c.commandRunning = true;
        }
        Creature::Command& cmd = c.commands.front();
        c.commandTime += seconds;
        bool done = false;
        switch (cmd.kind)
        {
        case Kind::GoTo:
        case Kind::GoToFreepoint:
        case Kind::GoToPoint:
        case Kind::Roam:
            done = !c.route.has_value();
            break;
        case Kind::Turn:
        {
            if (cmd.text == kPlayerTarget && m_player.valid())
            {
                c.turnTo = gameplay::yawOf(m_player.feet() - c.position); // the player may move
            }
            const f32 turn = c.turnTo ? wrapAngle(*c.turnTo - c.yaw) : 0.0f;
            c.yaw = wrapAngle(c.yaw + std::clamp(turn, -kTurnRate * seconds, kTurnRate * seconds));
            done = std::abs(turn) < 0.05f;
            if (done)
            {
                c.turnTo.reset();
            }
            break;
        }
        case Kind::Play: // the _in clip: then the loop
            if (a == nullptr || a->state() != std::format("amb_{}_in", c.ambient) || a->stateEnded())
            {
                if (a != nullptr && a->hasState(std::format("amb_{}", c.ambient)))
                {
                    a->enter(std::format("amb_{}", c.ambient), 0.15f);
                }
                c.ambientPhase = Creature::AmbientPhase::Loop;
                done = true;
            }
            break;
        case Kind::Stop: // the _out clip: then walking again
            if (a == nullptr || a->state() != std::format("amb_{}_out", c.ambient) || a->stateEnded())
            {
                if (a != nullptr && a->hasState("move"))
                {
                    a->enter("move", 0.2f);
                }
                c.ambient.clear();
                c.ambientPhase = Creature::AmbientPhase::None;
                c.handItem = nullptr; // in case the _out clip has no item_from_hand
                done = true;
            }
            break;
        case Kind::Wait:
        case Kind::Say:
            done = c.commandTime >= cmd.value;
            break;
        case Kind::Follow:
        {
            // Keep about `distance` from the target (the player, the pack leader, prey): a new way when it
            // got away, face it when near.
            const auto target = targetPosition(cmd.text);
            const Vec3 to = target ? *target - c.position : Vec3(0.0f);
            const f32 distance = glm::length(Vec3(to.x, 0.0f, to.z));
            if (target && distance > cmd.distance + 1.0f &&
                (!c.route || std::fmod(c.commandTime, 1.0f) < seconds))
            {
                const Vec3 goal = *target - Vec3(to.x, 0.0f, to.z) / distance * cmd.distance;
                if (npcGoToPosition(c.id, goal, "follow", distance > 8.0f || cmd.run).ok())
                {
                    c.trotting = cmd.trot;
                }
            }
            else if (distance <= cmd.distance)
            {
                c.route.reset();
                if (distance > 0.1f)
                {
                    const f32 turn = wrapAngle(gameplay::yawOf(to) - c.yaw);
                    c.yaw = wrapAngle(c.yaw + std::clamp(turn, -kTurnRate * seconds, kTurnRate * seconds));
                }
            }
            done = c.commandTime >= cmd.value || !target;
            if (done)
            {
                c.route.reset();
                c.followPlayer = false;
            }
            break;
        }
        case Kind::Flee:
        {
            // Every 2 s (or when arrived): to the way point within kFleeRadius that is farthest from the
            // player.
            const auto from = targetPosition(cmd.text);
            done = c.commandTime >= cmd.value || !from;
            if (done)
            {
                c.route.reset();
                break;
            }
            if (!c.route || std::fmod(c.commandTime, 2.0f) < seconds)
            {
                const Vec3 player = *from;
                // Straight away from the threat first (or 45 degrees to either side), 10 m over walkable
                // ground.
                Vec3 away = c.position - player;
                away.y = 0.0f;
                away = glm::length(away) > 1e-3f ? glm::normalize(away) : gameplay::forwardOf(c.yaw);
                bool running = false;
                for (const f32 angle : {0.0f, 0.785f, -0.785f})
                {
                    const Vec3 dir(away.x * std::cos(angle) - away.z * std::sin(angle), 0.0f,
                                   away.x * std::sin(angle) + away.z * std::cos(angle));
                    const Vec3 p = c.position + dir * kFleeStep;
                    const auto ground =
                        m_physics.valid()
                            ? m_physics.raycast(p + Vec3(0.0f, kGroundProbeAbove, 0.0f),
                                                Vec3(0.0f, -1.0f, 0.0f), kGroundProbeAbove + 3.0f,
                                                physics::layerBit(physics::Layer::World))
                            : std::nullopt;
                    if (ground && walkableLine(c.position, ground->position) &&
                        npcGoToPosition(c.id, ground->position, "flee", true).ok())
                    {
                        running = true;
                        break;
                    }
                }
                if (running)
                {
                    break;
                }
                // Cornered: the way point within kFleeRadius farthest from the threat.
                const auto& points = m_waynet.points();
                const Vec3* best = nullptr;
                f32 bestDistance = glm::length(c.position - player);
                for (const auto& p : points)
                {
                    const f32 fromPlayer = glm::length(p.position - player);
                    if (glm::length(p.position - c.position) < kFleeRadius && fromPlayer > bestDistance)
                    {
                        best = &p.position;
                        bestDistance = fromPlayer;
                    }
                }
                if (best != nullptr)
                {
                    (void)npcGoToPosition(c.id, *best, "flee", true);
                }
            }
            break;
        }
        }
        if (!done)
        {
            return;
        }
        c.commands.pop_front();
        c.commandRunning = false;
    }
}

void Engine::bindAiFunctions()
{
    using script::Value;
    using Kind = Creature::Command::Kind;
    script::ScriptVm& vm = *m_scripts;
    const auto npc = [this](std::span<const Value> a) -> Result<Creature*>
    {
        if (a.empty() || !a[0].isString())
        {
            return Error{"argument 1 must be an NPC (its instance)"};
        }
        Creature* c = npcNamed(a[0].asString());
        if (c == nullptr)
        {
            return Error{std::format("no NPC \"{}\" in this world", a[0].asString())};
        }
        return c;
    };
    const auto queue = [npc](Kind kind, bool needsText)
    {
        return [npc, kind, needsText](std::span<const Value> a) -> Result<Value>
        {
            auto c = npc(a);
            if (!c)
            {
                return c.error();
            }
            Creature::Command cmd{kind};
            if (needsText)
            {
                if (a.size() < 2 || !a[1].isString())
                {
                    return Error{"argument 2 must be a string"};
                }
                cmd.text = std::string(a[1].asString());
            }
            if (kind == Kind::Play && a.size() > 2 && a[2].isString())
            {
                cmd.item = std::string(a[2].asString());
            }
            if (kind == Kind::Wait)
            {
                cmd.value = static_cast<f32>(a.size() > 1 ? a[1].asNumber(1.0) : 1.0);
            }
            if (kind == Kind::GoToFreepoint)
            {
                cmd.value = static_cast<f32>(a.size() > 2 ? a[2].asNumber(10.0) : 10.0);
            }
            if (kind == Kind::GoTo || kind == Kind::GoToFreepoint)
            {
                setGait(cmd, a, kind == Kind::GoTo ? 2u : 3u);
            }
            c.value()->commands.push_back(std::move(cmd));
            return Value();
        };
    };
    // npc_goto queues now (part A walked at once).
    vm.bind(
        {"npc_goto", "npc_goto(npc: string, target: string, run?: boolean|\"trot\")",
         "Reiht ein: Der NPC geht (oder rennt) über das Wegnetz zu einem Wegpunkt oder Freepoint (Name ohne "
         "Rücksicht auf Groß- und Kleinschreibung). Ankunft: Ereignis `npc_arrived`.",
         "NPCs", queue(Kind::GoTo, true)});
    const auto toPlayer = [npc](Kind kind)
    {
        return [npc, kind](std::span<const Value> a) -> Result<Value>
        {
            auto c = npc(a);
            if (!c)
            {
                return c.error();
            }
            Creature::Command cmd{kind, std::string(kPlayerTarget)};
            if (kind == Kind::Follow)
            {
                cmd.value = static_cast<f32>(a.size() > 1 ? a[1].asNumber(10.0) : 10.0);
                cmd.distance = static_cast<f32>(a.size() > 2 ? a[2].asNumber(2.0) : 2.0);
                setGait(cmd, a, 3);
            }
            else if (kind == Kind::GoTo)
            {
                cmd.distance = static_cast<f32>(a.size() > 1 ? a[1].asNumber(1.5) : 1.5);
                setGait(cmd, a, 2);
            }
            c.value()->commands.push_back(std::move(cmd));
            return Value();
        };
    };
    vm.bind({"npc_flee", "npc_flee(npc: string, seconds?: number, from?: string)",
             "Reiht ein: `seconds` Sekunden lang (Vorgabe 8) vor dem Spieler (bzw. dem NPC `from`) weglaufen "
             "– zum "
             "Wegpunkt im Umkreis von 30 m, der am weitesten von ihm weg ist, alle 2 s neu gewählt.",
             "NPCs", [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c)
                 {
                     return c.error();
                 }
                 Creature::Command cmd{Kind::Flee};
                 cmd.value = static_cast<f32>(a.size() > 1 ? a[1].asNumber(8.0) : 8.0);
                 cmd.text = a.size() > 2 && a[2].isString() ? std::string(a[2].asString()) : std::string();
                 c.value()->commands.push_back(std::move(cmd));
                 return Value();
             }});
    vm.bind({"npc_follow_npc",
             "npc_follow_npc(npc: string, target: string, distance?: number, seconds?: number, run?: "
             "boolean|\"trot\")",
             "Reiht ein: dem NPC `target` folgen (Rudel, Jagd) – auf etwa `distance` Meter (Vorgabe 2), "
             "`seconds` "
             "Sekunden lang (Vorgabe 10).",
             "NPCs", [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2 || !a[1].isString())
                 {
                     return !c ? c.error() : Error{"argument 2 must be an NPC"};
                 }
                 Creature::Command cmd{Kind::Follow, std::string(a[1].asString())};
                 cmd.distance = static_cast<f32>(a.size() > 2 ? a[2].asNumber(2.0) : 2.0);
                 cmd.value = static_cast<f32>(a.size() > 3 ? a[3].asNumber(10.0) : 10.0);
                 setGait(cmd, a, 4);
                 c.value()->commands.push_back(std::move(cmd));
                 return Value();
             }});
    vm.bind({"npc_goto_point",
             "npc_goto_point(npc: string, x: number, y: number, z: number, run?: boolean|\"trot\")",
             "Reiht ein: zu einem Punkt gehen (bzw. rennen), über das Wegnetz, wo nötig.", "NPCs",
             [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 4 || !a[1].isNumber() || !a[2].isNumber() || !a[3].isNumber())
                 {
                     return !c ? c.error() : Error{"expects (npc, x, y, z, run?)"};
                 }
                 Creature::Command cmd{Kind::GoToPoint};
                 cmd.point = Vec3(static_cast<f32>(a[1].asNumber()), static_cast<f32>(a[2].asNumber()),
                                  static_cast<f32>(a[3].asNumber()));
                 setGait(cmd, a, 4);
                 c.value()->commands.push_back(std::move(cmd));
                 return Value();
             }});
    vm.bind(
        {"npc_roam", "npc_roam(npc: string, centre: string, radius: number, run?: boolean|\"trot\")",
         "Reiht ein: zu einem zufälligen Punkt im Umkreis `radius` um den Wegpunkt `centre` gehen (Revier, "
         "Herumstreifen); gerade von dort erreichbar.",
         "NPCs", [npc](std::span<const Value> a) -> Result<Value>
         {
             auto c = npc(a);
             if (!c || a.size() < 3 || !a[1].isString() || !a[2].isNumber())
             {
                 return !c ? c.error() : Error{"expects (npc, centre: string, radius: number, run?)"};
             }
             Creature::Command cmd{Kind::Roam, std::string(a[1].asString())};
             cmd.value = static_cast<f32>(a[2].asNumber());
             setGait(cmd, a, 3);
             c.value()->commands.push_back(std::move(cmd));
             return Value();
         }});
    vm.bind({"npc_goto_player", "npc_goto_player(npc: string, distance?: number, run?: boolean|\"trot\")",
             "Reiht ein: zum Spieler gehen (bzw. rennen), bis auf `distance` Meter (Vorgabe 1,5).", "NPCs",
             toPlayer(Kind::GoTo)});
    vm.bind({"npc_turn_to_player", "npc_turn_to_player(npc: string)", "Reiht ein: sich zum Spieler drehen.",
             "NPCs", toPlayer(Kind::Turn)});
    vm.bind(
        {"npc_follow_player",
         "npc_follow_player(npc: string, seconds?: number, distance?: number, run?: boolean|\"trot\")",
         "Reiht ein: dem Spieler `seconds` Sekunden lang (Vorgabe 10) auf etwa `distance` Meter (Vorgabe 2) "
         "folgen und ihn ansehen (Drohen, Begleiten).",
         "NPCs", toPlayer(Kind::Follow)});
    vm.bind(
        {"npc_goto_freepoint",
         "npc_goto_freepoint(npc: string, type: string, radius?: number, run?: boolean|\"trot\")",
         "Reiht ein: zum nächsten freien Freepoint dieses Typs (`\"SIT\"`, `\"CAMPFIRE\"` ...) im Umkreis "
         "(Vorgabe 10 m), reserviert ihn und dreht sich in seine Richtung. Gibt es keinen, bleibt er stehen.",
         "NPCs", queue(Kind::GoToFreepoint, true)});
    vm.bind({"npc_turn", "npc_turn(npc: string, point: string)",
             "Reiht ein: in die Richtung (`dir`) eines Wegpunkts oder Freepoints drehen.", "NPCs",
             queue(Kind::Turn, true)});
    vm.bind(
        {"npc_play", "npc_play(npc: string, ambient: string, item?: string)",
         "Reiht ein: eine Tagesablauf-Animation (`\"sit_ground\"`, `\"guard\"` ... – Zustände amb_<x>_in, "
         "amb_<x>, "
         "amb_<x>_out des Menschen-Graphen; `\"idle_look\"` und `\"react_warn\"` usw. direkt), bis npc_stop "
         "oder Gehen sie beendet. `item` (`\"it_broom\"`) nimmt er bei `item_to_hand` in die rechte Hand und "
         "legt es bei `item_from_hand` bzw. am Ende weg.",
         "NPCs", queue(Kind::Play, true)});
    vm.bind({"npc_stop", "npc_stop(npc: string)",
             "Reiht ein: die laufende Tagesablauf-Animation beenden (_out).", "NPCs",
             queue(Kind::Stop, false)});
    vm.bind({"npc_wait", "npc_wait(npc: string, seconds: number)", "Reiht ein: warten.", "NPCs",
             queue(Kind::Wait, false)});
    vm.bind(
        {"npc_say", "npc_say(npc: string, text: string)",
         "Reiht ein: einen Satz sagen (bis zu den Dialogen in M10 eine Einblendung in der Nähe des Helden; "
         "Ereignis `npc_said`).",
         "NPCs", queue(Kind::Say, true)});
    vm.bind({"npc_shout", "npc_shout(npc: string, text: string)",
             "Ruft sofort (ohne Warteschlange, z. B. beim Weglaufen); sonst wie npc_say.", "NPCs",
             [this, npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2 || !a[1].isString())
                 {
                     return !c ? c.error() : Error{"argument 2 must be a string"};
                 }
                 npcSays(*c.value(), a[1].asString());
                 return Value();
             }});
    vm.bind({"npc_clear", "npc_clear(npc: string)", "Leert die Befehlsliste des NPCs (er bleibt, wo er ist).",
             "NPCs", [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c)
                 {
                     return c.error();
                 }
                 c.value()->commands.clear();
                 c.value()->commandRunning = false;
                 c.value()->route.reset();
                 return Value();
             }});
    vm.bind({"npc_start_state", "npc_start_state(npc: string, state: string, at?: string)",
             "Unterbricht: beendet den laufenden Zustand (finish) und startet einen anderen. Endet er "
             "(\"done\"), "
             "greift wieder der Tagesablauf.",
             "NPCs", [this, npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2 || !a[1].isString())
                 {
                     return !c ? c.error() : Error{"argument 2 must be a State"};
                 }
                 if (!m_scripts->findInstance("State", a[1].asString()))
                 {
                     return Error{std::format("no State \"{}\"", a[1].asString())};
                 }
                 beginState(*c.value(), a[1].asString(), a.size() > 2 ? a[2].asString() : c.value()->stateAt);
                 return Value();
             }});
    vm.bind({"npc_state",
             "npc_state(npc: string) -> {state, routine, ambient, at, commands, animation, walking, x, y, z}",
             "Zustand, Tagesablauf, Tagesablauf-Animation, Ort, Länge der Befehlsliste, Zustand des "
             "Animationsgraphen, ob er gerade geht, und seine Position.",
             "NPCs", [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c)
                 {
                     return c.error();
                 }
                 return script::makeTable(
                     {}, {{"state", c.value()->state},
                          {"routine", c.value()->routine},
                          {"ambient", c.value()->ambient},
                          {"at", c.value()->stateAt},
                          {"commands", static_cast<i64>(c.value()->commands.size())},
                          {"animation", c.value()->figure ? std::string(c.value()->figure->animator.state())
                                                          : std::string()},
                          {"walking", c.value()->route.has_value()},
                          {"x", static_cast<f64>(c.value()->position.x)},
                          {"y", static_cast<f64>(c.value()->position.y)},
                          {"z", static_cast<f64>(c.value()->position.z)}});
             }});
    vm.bind({"set_routine", "set_routine(npc: string, routine: string)",
             "Wechselt den Tagesablauf (Kapitelwechsel); der passende Eintrag beginnt sofort. `\"\"` "
             "schaltet ihn ab "
             "(der NPC tut dann nur, was Skripte ihm auftragen).",
             "NPCs", [this, npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2 || !a[1].isString())
                 {
                     return !c ? c.error() : Error{"argument 2 must be a Routine"};
                 }
                 if (!a[1].asString().empty() && !m_scripts->findInstance("Routine", a[1].asString()))
                 {
                     return Error{std::format("no Routine \"{}\"", a[1].asString())};
                 }
                 finishState(*c.value());
                 c.value()->routine = std::string(a[1].asString());
                 c.value()->routineEntry = -1;
                 return Value();
             }});
    vm.bind({"insert_npc", "insert_npc(npc: string, at?: string) -> string",
             "Setzt ein Npc an einen Wegpunkt oder Freepoint (ohne `at`: an den Ort des passenden Eintrags "
             "seines "
             "Tagesablaufs, `routine` der Instanz) und startet den Tagesablauf. Gibt seinen Namen zurück: "
             "die Instanz, ab dem zweiten NPC derselben Instanz `name#2` …",
             "NPCs", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be an Npc"};
                 }
                 const script::Instance* def = m_scripts->findInstance("Npc", a[0].asString());
                 if (def == nullptr)
                 {
                     return Error{std::format("no Npc \"{}\"", a[0].asString())};
                 }
                 std::string at = a.size() > 1 ? std::string(a[1].asString()) : std::string();
                 const std::string routine(def->fields["routine"].asString());
                 if (at.empty() && !routine.empty())
                 {
                     // The place of the entry for the current time.
                     const script::Instance* r = m_scripts->findInstance("Routine", routine);
                     const script::Table* entries = r != nullptr ? r->fields.asTable() : nullptr;
                     const auto minute = static_cast<u32>(m_gameTime.minuteOfDay());
                     for (usize i = 0; entries != nullptr && i < entries->array.size() && at.empty(); ++i)
                     {
                         const auto& e = entries->array[i];
                         const auto from = minuteOf(e["from"].asString());
                         const auto to = minuteOf(e["to"].asString());
                         if (from && to && inWindow(minute, *from, *to % 1440))
                         {
                             at = std::string(e["at"].asString());
                         }
                     }
                 }
                 const auto target = navigationTarget(at);
                 if (!target)
                 {
                     return Error{std::format("no way point or freepoint \"{}\"", at)};
                 }
                 // On the ground: a way point may lie a little below it (welt: hollows of the terrain model).
                 // The probe starts just above the point - from high up it would find the ceiling of a room
                 // (welt's walkable houses) first.
                 Vec3 at3 = *target;
                 if (m_physics.valid())
                 {
                     if (const auto hit = m_physics.raycast(at3 + Vec3(0.0f, kGroundProbeAbove, 0.0f),
                                                            Vec3(0.0f, -1.0f, 0.0f), kGroundProbeAbove + 3.0f,
                                                            physics::layerBit(physics::Layer::World)))
                     {
                         at3.y = std::max(at3.y, hit->position.y);
                     }
                 }
                 auto spawned = spawnNpc(a[0].asString(), at3, 0.0f);
                 if (!spawned)
                 {
                     return spawned.error();
                 }
                 return Value(creature(spawned.value())->species); // its name: "mon_wolf", "mon_wolf#2" ...
             }});
    vm.bind({"npc_said",
             "on(\"npc_said\", fn(npc: string, text: string, key: string))",
             "Ein NPC hat etwas gesagt (npc_say, npc_shout); `key` ist der Sprach-Schlüssel des Zurufs "
             "(`svm_<stimme>_<m|f>_<anlass>_NN`, leer, wenn die Sprach-Datenbank den Text nicht kennt).",
             "Ereignisse",
             {}});
}
} // namespace g7
