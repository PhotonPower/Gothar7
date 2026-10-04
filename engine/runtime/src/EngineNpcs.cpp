// NPC navigation (M9 part A): the world's waynet (ai::Waynet), human NPCs walking with their own capsule
// along routes over it (to a way point or freepoint by name, without regard to case), waiting and planning
// again when blocked, and the waynet and routes in the debug overlay (F2).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Mobs.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kArriveDistance = 0.35f; ///< metres to a route point that count as reached
constexpr f32 kTurnRate = 6.0f;        ///< radians per second an NPC turns while walking
/// A walkable line is checked with spheres at these heights above the feet: low obstacles (below the step
/// height) are walked over, fences with gaps between their rails still block.
constexpr f32 kWalkableHeights[] = {0.5f, 1.0f, 1.5f};
constexpr f32 kWalkableRadius = 0.3f; ///< the NPC capsule's
// Ground under a straight line (welt #171: shortcuts over steep slopes): probed every kGroundStep, no step
// may rise or fall more than tan 35 degrees (as welt's waynet check), and the ground must be there (no drop).
constexpr f32 kGroundStep = 0.5f;
constexpr f32 kMaxSlope = 0.7f;         ///< tan 35 degrees
constexpr f32 kMaxStraightLine = 50.0f; ///< longer lines are not checked (cost): they go over the waynet
constexpr f32 kProgressDistance = 0.3f; ///< the way to the next route point must shrink this much ...
constexpr f32 kStuckSeconds = 1.5f;     ///< ... within this long, else plan again (sliding is no progress)
constexpr u32 kMaxReplans = 3;          ///< then give up
constexpr f32 kCloseEnough = 1.2f;      ///< stuck this near the goal (something stands on it): arrived
constexpr f32 kNpcWalkSpeed = 1.6f;     ///< m/s without blend points in the graph
constexpr f32 kNpcRunSpeed = 4.5f;

f32 wrapAngle(f32 a)
{
    return std::remainder(a, 2.0f * glm::pi<f32>());
}
} // namespace

bool Engine::walkableLine(const Vec3& a, const Vec3& b) const
{
    if (!m_physics.valid())
    {
        return true;
    }
    const Vec3 delta = b - a;
    const f32 length = glm::length(delta);
    if (length < 1e-3f)
    {
        return true;
    }
    if (length > kMaxStraightLine)
    {
        return false;
    }
    for (const f32 height : kWalkableHeights)
    {
        if (m_physics.sphereCast(a + Vec3(0.0f, height, 0.0f), kWalkableRadius, delta / length, length,
                                 physics::layerBit(physics::Layer::World)))
        {
            return false;
        }
    }
    // The ground along it: no steep slope, no drop.
    const f32 flat = glm::length(Vec2(delta.x, delta.z));
    const u32 steps = static_cast<u32>(std::ceil(flat / kGroundStep));
    f32 ground = a.y;
    for (u32 i = 1; i <= steps; ++i)
    {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
        const Vec3 p = a + delta * t;
        const auto hit = m_physics.raycast(Vec3(p.x, ground + 1.5f, p.z), Vec3(0.0f, -1.0f, 0.0f), 3.5f,
                                           physics::layerBit(physics::Layer::World));
        if (!hit || std::abs(hit->position.y - ground) > kMaxSlope * flat / static_cast<f32>(steps) + 0.05f)
        {
            return false;
        }
        ground = hit->position.y;
    }
    return true;
}

std::optional<Vec3> Engine::navigationTarget(std::string_view name) const
{
    if (const auto wp = m_waynet.find(name))
    {
        return m_waynet.points()[*wp].position;
    }
    if (const auto fp = m_waynet.findFreepoint(name))
    {
        return m_waynet.freepoints()[*fp].position;
    }
    return std::nullopt;
}

Result<void> Engine::npcGoTo(u32 id, std::string_view target, bool run)
{
    Creature* c = creature(id);
    if (c == nullptr || !c->body)
    {
        return Error{std::format("creature {} cannot walk (no NPC)", id)};
    }
    const auto goal = navigationTarget(target);
    if (!goal)
    {
        return Error{std::format("no way point or freepoint \"{}\" in this world", target)};
    }
    return npcGoToPosition(id, *goal, target, run);
}

Result<void> Engine::npcGoToPosition(u32 id, const Vec3& goal, std::string_view target, bool run)
{
    Creature* c = creature(id);
    if (c == nullptr || !c->body)
    {
        return Error{std::format("creature {} cannot walk (no NPC)", id)};
    }
    auto route = m_waynet.route(c->position, goal,
                                [this](const Vec3& a, const Vec3& b) { return walkableLine(a, b); });
    if (!route)
    {
        return Error{std::format("no way from ({:.1f}, {:.1f}, {:.1f}) to \"{}\"", c->position.x,
                                 c->position.y, c->position.z, target)};
    }
    c->route = std::move(route);
    c->routeIndex = 0;
    c->routeGoal = std::string(target);
    c->running = run;
    c->stuckSeconds = 0.0f;
    c->progressIndex = ~usize(0);
    return {};
}

bool Engine::npcWalking(u32 id) const noexcept
{
    const Creature* c = creature(id);
    return c != nullptr && c->route.has_value();
}

std::optional<u32> Engine::npcByInstance(std::string_view instance) const noexcept
{
    for (const auto& c : m_creatures)
    {
        if (c->character && c->species == instance) // "mon_wolf#2" names the second one
        {
            return c->id;
        }
    }
    return std::nullopt;
}

void Engine::walkNpc(Creature& c, f32 seconds)
{
    if (!c.body)
    {
        return;
    }
    Vec3 velocity(0.0f);
    if (c.route)
    {
        // Next point; reached ones are skipped (a smoothing shortcut may pass close by).
        while (c.routeIndex < c.route->points.size())
        {
            const Vec3 to = c.route->points[c.routeIndex] - c.position;
            if (glm::length(Vec3(to.x, 0.0f, to.z)) > kArriveDistance)
            {
                break;
            }
            ++c.routeIndex;
        }
        if (c.routeIndex >= c.route->points.size())
        {
            c.route.reset();
            c.replans = 0;
            if (m_scripts)
            {
                const script::Value args[] = {c.species, c.routeGoal};
                m_scripts->emit("npc_arrived", args);
            }
        }
        else
        {
            const Vec3 to = c.route->points[c.routeIndex] - c.position;
            const f32 wanted = gameplay::yawOf(glm::normalize(Vec3(to.x, 0.0f, to.z)));
            const f32 turn = wrapAngle(wanted - c.yaw);
            c.yaw = wrapAngle(c.yaw + std::clamp(turn, -kTurnRate * seconds, kTurnRate * seconds));
            // Walk once facing roughly the right way; turn on the spot before.
            const f32 speed = c.running ? (c.runSpeed > 0.0f ? c.runSpeed : kNpcRunSpeed)
                                        : (c.walkSpeed > 0.0f ? c.walkSpeed : kNpcWalkSpeed);
            const f32 facing = std::abs(turn) < 1.0f ? std::cos(turn) : 0.0f;
            velocity = gameplay::forwardOf(c.yaw) * speed * facing;
            // Blocked (another NPC, a door, a crate, a slope it slides down): the way to the next route point
            // does not shrink. Plan again from here, at most a few times.
            c.stuckSeconds += seconds;
            const f32 left = glm::length(Vec2(to.x, to.z));
            if (c.routeIndex != c.progressIndex || left < c.progressDistance - kProgressDistance)
            {
                c.progressIndex = c.routeIndex;
                c.progressDistance = left;
                c.stuckSeconds = 0.0f;
            }
            if (c.stuckSeconds > kStuckSeconds && left < kCloseEnough)
            {
                // Near enough to a point it cannot reach: a crate or a stool on the goal, a way point too
                // close to a house corner (welt #171). On to the next one.
                ++c.routeIndex;
                c.stuckSeconds = 0.0f;
            }
            else if (c.stuckSeconds > kStuckSeconds)
            {
                const std::string goal = c.routeGoal;
                const u32 replans = c.replans + 1;
                if (replans > kMaxReplans || !npcGoTo(c.id, goal, c.running))
                {
                    G7_LOG_WARN("engine", "{} gives up walking to {}", c.species, goal);
                    c.route.reset();
                    c.replans = 0;
                    if (m_scripts)
                    {
                        const script::Value args[] = {c.species, goal};
                        m_scripts->emit("npc_blocked", args);
                    }
                }
                else
                {
                    c.replans = replans;
                }
            }
        }
    }
    c.body->update(seconds, velocity);
    c.position = c.body->feet();
    c.speed = glm::length(Vec3(c.body->velocity().x, 0.0f, c.body->velocity().z));
}

void Engine::drawWaynet()
{
    const render::DebugStyle edge{Vec4(0.3f, 0.8f, 1.0f, 0.8f)};
    const render::DebugStyle point{Vec4(0.3f, 0.8f, 1.0f, 1.0f)};
    const render::DebugStyle free{Vec4(1.0f, 0.85f, 0.3f, 1.0f)};
    const render::DebugStyle way{Vec4(0.4f, 1.0f, 0.4f, 1.0f)};
    const Vec3 lift(0.0f, 0.1f, 0.0f);
    const Vec3 eye = m_camera.transform.position;
    const auto& points = m_waynet.points();
    for (u32 i = 0; i < points.size(); ++i)
    {
        for (const u32 j : m_waynet.neighbours(i))
        {
            if (j > i)
            {
                m_debugDraw.line(points[i].position + lift, points[j].position + lift, edge);
            }
        }
        m_debugDraw.cross(points[i].position + lift, 0.3f, point);
        if (glm::length(points[i].position - eye) < 40.0f)
        {
            m_debugDraw.text(points[i].position + Vec3(0.0f, 0.5f, 0.0f), points[i].name, point);
        }
    }
    for (const ai::Waynet::Freepoint& fp : m_waynet.freepoints())
    {
        m_debugDraw.circle(fp.position + lift, Vec3(0.0f, 1.0f, 0.0f), 0.3f, free);
        if (fp.dir)
        {
            m_debugDraw.arrow(fp.position + lift, fp.position + lift + *fp.dir * 0.6f, free);
        }
        if (glm::length(fp.position - eye) < 25.0f)
        {
            m_debugDraw.text(fp.position + Vec3(0.0f, 0.4f, 0.0f), fp.name, free);
        }
    }
    for (const auto& c : m_creatures)
    {
        if (!c->route)
        {
            continue;
        }
        Vec3 at = c->position + lift;
        for (usize i = c->routeIndex; i < c->route->points.size(); ++i)
        {
            m_debugDraw.line(at, c->route->points[i] + lift, way);
            at = c->route->points[i] + lift;
        }
        m_debugDraw.text(c->position + Vec3(0.0f, 2.1f, 0.0f), std::format("-> {}", c->routeGoal), way);
    }
}

void Engine::bindNpcFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"route_length", "route_length(from: string, to: string) -> number | nil",
             "Länge des Weges (Meter) zwischen zwei Wegpunkten bzw. Freepoints, wie ein NPC ihn gehen würde; "
             "`nil`, "
             "wenn es keinen gibt (Prüfung der Routinen-Orte, Inhalte).",
             "NPCs", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 2 || !a[0].isString() || !a[1].isString())
                 {
                     return Error{"expects (from: string, to: string)"};
                 }
                 const auto from = navigationTarget(a[0].asString());
                 const auto to = navigationTarget(a[1].asString());
                 if (!from || !to)
                 {
                     return Error{std::format("no way point or freepoint \"{}\"",
                                              !from ? a[0].asString() : a[1].asString())};
                 }
                 const auto route = m_waynet.route(*from, *to, [this](const Vec3& p, const Vec3& q)
                                                   { return walkableLine(p, q); });
                 if (!route)
                 {
                     return Value();
                 }
                 f64 length = 0.0;
                 Vec3 previous = *from;
                 for (const Vec3& p : route->points)
                 {
                     length += static_cast<f64>(glm::length(p - previous));
                     previous = p;
                 }
                 return Value(length);
             }});
    vm.bind(
        {"npc_goto", "npc_goto(npc: string, target: string, run?: boolean) -> boolean",
         "Schickt den (ersten eingefügten) NPC dieser Instanz zu einem Wegpunkt oder Freepoint (Name ohne "
         "Rücksicht auf Groß- und Kleinschreibung) über das Wegnetz; `run` rennt. Fehler, wenn es keinen Weg "
         "gibt. Ankunft: Ereignis `npc_arrived`.",
         "NPCs", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.size() < 2 || !a[0].isString() || !a[1].isString())
             {
                 return Error{"expects (npc: string, target: string)"};
             }
             const auto id = npcByInstance(a[0].asString());
             if (!id)
             {
                 return Error{std::format("no NPC \"{}\" in this world", a[0].asString())};
             }
             if (auto sent = npcGoTo(*id, a[1].asString(), a.size() > 2 && a[2].asBool()); !sent)
             {
                 return sent.error();
             }
             return Value(true);
         }});
    vm.bind({"npc_arrived",
             "on(\"npc_arrived\", fn(npc: string, target: string))",
             "Ein NPC ist an seinem Ziel angekommen (npc_goto).",
             "Ereignisse",
             {}});
    vm.bind({"npc_blocked",
             "on(\"npc_blocked\", fn(npc: string, target: string))",
             "Ein NPC kommt nicht weiter und hat aufgegeben (nach mehrfachem Neuplanen).",
             "Ereignisse",
             {}});
}
} // namespace g7
