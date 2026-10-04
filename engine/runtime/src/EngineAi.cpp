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
constexpr f32 kSimulationDistance = 80.0f; ///< metres from the player: farther NPCs are not simulated
constexpr f32 kSimulationReturn = 75.0f;   ///< ... and come back nearer than this (no flicker at the edge)
constexpr f32 kLoopInterval = 0.5f;        ///< seconds between calls of a state's loop()
constexpr f32 kTurnRate = 5.0f;            ///< radians per second when turning on the spot
constexpr f32 kSayPerCharacter = 0.06f;    ///< seconds a line stays, per character (at least kSayMinimum)
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
} // namespace

Creature* Engine::npcNamed(std::string_view instance) noexcept
{
    const auto id = npcByInstance(instance);
    return id ? creature(*id) : nullptr;
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
    if (c.simulated && distance > kSimulationDistance)
    {
        c.simulated = false;
        c.route.reset();
        c.commands.clear();
        c.commandRunning = false;
    }
    else if (!c.simulated && distance < kSimulationReturn)
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
        if (a != nullptr && a->hasState("move"))
        {
            a->enter("move", 0.25f);
        }
        c.ambient.clear();
        c.ambientPhase = Creature::AmbientPhase::None;
        c.handItem = nullptr;
        return false;
    }
    case Kind::Wait:
        return cmd.value > 0.0f;
    case Kind::Follow:
        c.followPlayer = true;
        return cmd.value > 0.0f && m_player.valid();
    case Kind::Say:
        G7_LOG_INFO("engine", "{}: \"{}\"", c.species, cmd.text);
        if (m_scripts)
        {
            const script::Value args[] = {c.species, cmd.text};
            m_scripts->emit("npc_said", args);
        }
        if (m_player.valid() && glm::length(c.position - m_player.feet()) < 15.0f)
        {
            const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", c.species) : nullptr;
            notice(
                std::format("{}: {}", npc != nullptr ? npc->fields["name"].asString() : c.species, cmd.text));
        }
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
            c.commandRunning = true;
        }
        Creature::Command& cmd = c.commands.front();
        c.commandTime += seconds;
        bool done = false;
        switch (cmd.kind)
        {
        case Kind::GoTo:
        case Kind::GoToFreepoint:
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
            // Keep about `distance` from the player: a new way when he got away, face him when near.
            const Vec3 to = m_player.valid() ? m_player.feet() - c.position : Vec3(0.0f);
            const f32 distance = glm::length(Vec3(to.x, 0.0f, to.z));
            if (distance > cmd.distance + 1.0f && (!c.route || std::fmod(c.commandTime, 1.0f) < seconds))
            {
                const Vec3 goal = m_player.feet() - Vec3(to.x, 0.0f, to.z) / distance * cmd.distance;
                (void)npcGoToPosition(c.id, goal, "player", distance > 8.0f);
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
            done = c.commandTime >= cmd.value || !m_player.valid();
            if (done)
            {
                c.route.reset();
                c.followPlayer = false;
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
                cmd.run = a.size() > (kind == Kind::GoTo ? 2u : 3u) && a[kind == Kind::GoTo ? 2 : 3].asBool();
            }
            c.value()->commands.push_back(std::move(cmd));
            return Value();
        };
    };
    // npc_goto queues now (part A walked at once).
    vm.bind(
        {"npc_goto", "npc_goto(npc: string, target: string, run?: boolean)",
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
            }
            else if (kind == Kind::GoTo)
            {
                cmd.distance = static_cast<f32>(a.size() > 1 ? a[1].asNumber(1.5) : 1.5);
                cmd.run = a.size() > 2 && a[2].asBool();
            }
            c.value()->commands.push_back(std::move(cmd));
            return Value();
        };
    };
    vm.bind({"npc_goto_player", "npc_goto_player(npc: string, distance?: number, run?: boolean)",
             "Reiht ein: zum Spieler gehen (bzw. rennen), bis auf `distance` Meter (Vorgabe 1,5).", "NPCs",
             toPlayer(Kind::GoTo)});
    vm.bind({"npc_turn_to_player", "npc_turn_to_player(npc: string)", "Reiht ein: sich zum Spieler drehen.",
             "NPCs", toPlayer(Kind::Turn)});
    vm.bind(
        {"npc_follow_player", "npc_follow_player(npc: string, seconds?: number, distance?: number)",
         "Reiht ein: dem Spieler `seconds` Sekunden lang (Vorgabe 10) auf etwa `distance` Meter (Vorgabe 2) "
         "folgen und ihn ansehen (Drohen, Begleiten).",
         "NPCs", toPlayer(Kind::Follow)});
    vm.bind(
        {"npc_goto_freepoint",
         "npc_goto_freepoint(npc: string, type: string, radius?: number, run?: boolean)",
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
    vm.bind({"npc_state", "npc_state(npc: string) -> {state, routine, ambient, at, commands, animation}",
             "Zustand, Tagesablauf, Tagesablauf-Animation, Ort, Länge der Befehlsliste und Zustand des "
             "Animationsgraphen.",
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
                                                          : std::string()}});
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
    vm.bind({"insert_npc", "insert_npc(npc: string, at?: string) -> boolean",
             "Setzt ein Npc an einen Wegpunkt oder Freepoint (ohne `at`: an den Ort des passenden Eintrags "
             "seines "
             "Tagesablaufs, `routine` der Instanz) und startet den Tagesablauf.",
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
                 auto spawned = spawnNpc(a[0].asString(), *target, 0.0f);
                 if (!spawned)
                 {
                     return spawned.error();
                 }
                 return Value(true);
             }});
    vm.bind({"npc_said",
             "on(\"npc_said\", fn(npc: string, text: string))",
             "Ein NPC hat etwas gesagt (npc_say).",
             "Ereignisse",
             {}});
}
} // namespace g7
