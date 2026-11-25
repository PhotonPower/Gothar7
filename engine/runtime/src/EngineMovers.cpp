// Moving parts (docs/modules/dungeon.md D2): gates, secret walls and trapdoors - vobs with components.mover.
// Events of the net move them; their drawing and collision move with them each fixed step. One closing on
// someone stops and opens again (a trap marked crush = "damage" hurts first). While they block the way
// (a gate not fully open, a trapdoor not fully shut), the waynet edges through them are closed.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/runtime/Engine.hpp>

#include <utility>
#include <vector>

namespace g7
{
namespace
{
constexpr u32 kHeroId = ~u32(0);
constexpr i32 kCrushDamage = 20;     ///< dungeon.md question 2: a gate marked as a trap
constexpr f32 kEdgeLift = 0.3f;      ///< waynet edges are tested this far above the way points
constexpr f32 kEdgeHeightPad = 0.5f; ///< ... against the closed box grown by this up and down

const char* kindName(world::Mover::Kind kind)
{
    switch (kind)
    {
    case world::Mover::Kind::Gate:
        return "gate";
    case world::Mover::Kind::SecretWall:
        return "secret_wall";
    case world::Mover::Kind::Trapdoor:
        return "trapdoor";
    }
    return "gate";
}

bool hasBounds(const AABB& box)
{
    return box.min.x <= box.max.x;
}

bool samePose(const Transform& a, const Transform& b)
{
    return glm::length(a.position - b.position) < 1e-4f &&
           std::abs(glm::dot(a.rotation, b.rotation)) > 0.99999f && glm::length(a.scale - b.scale) < 1e-4f;
}
} // namespace

void Engine::rebuildMovers()
{
    // The scene is set up anew: a world loaded (the list is empty then), or the editor rebuilt it while the
    // movers stood somewhere - those keep their closed pose and course.
    std::map<u64, MoverRuntime> before = std::exchange(m_movers, {});
    m_scene.each<world::Vob, world::Mover>(
        [&](entt::entity e, const world::Vob& vob, const world::Mover& mover)
        {
            MoverRuntime m;
            m.name = vob.nameText;
            m.mover = mover;
            m.closed = *std::as_const(m_scene).get<Transform>(e);
            m.modelBounds = AABB{Vec3(1.0f), Vec3(-1.0f)};
            for (const SceneInstance& instance : m_instances)
            {
                if (instance.vob == vob.id && instance.model != nullptr)
                {
                    m.modelBounds = instance.model->bounds;
                    break;
                }
            }
            if (!hasBounds(m.modelBounds))
            {
                G7_LOG_WARN("engine", "{}: mover {} has no model - it does not move", m_worldPath, m.name);
            }
            m.course.position = mover.open ? 1.0f : 0.0f;
            m.course.target = m.course.position;
            if (const auto old = before.find(vob.id.value); old != before.end())
            {
                const MoverRuntime& o = old->second;
                if (samePose(m.closed, world::moverPose(o.closed, o.motion, o.course.position)))
                {
                    m.closed = o.closed;
                    m.course = o.course;
                    m.crushes = o.crushes;
                }
            }
            m.motion =
                world::moverMotion(mover, hasBounds(m.modelBounds) ? m.modelBounds : AABB{}, m.closed.scale);
            m.blocks = world::moverBlocks(mover.kind, m.course);
            m_movers.emplace(vob.id.value, std::move(m));
        });
    for (const auto& [id, m] : m_movers)
    {
        if (m.course.position != 0.0f)
        {
            placeMover(id, m);
        }
    }
    updateMoverBlocks();
}

void Engine::moveMover(u64 id, MoverRuntime& m, bool open)
{
    const f32 target = open ? 1.0f : 0.0f;
    if (m.course.target == target)
    {
        return;
    }
    m.course.target = target;
    if (!m.mover.soundMove.empty())
    {
        if (const entt::entity e = m_scene.findById(world::VobId{id}); e != entt::null)
        {
            (void)playSound(m.mover.soundMove, Vec3(m_scene.worldMatrix(e)[3]));
        }
    }
}

void Engine::placeMover(u64 id, const MoverRuntime& m)
{
    if (const entt::entity e = m_scene.findById(world::VobId{id}); e != entt::null)
    {
        placeVob(id, e, world::moverPose(m.closed, m.motion, m.course.position));
    }
}

void Engine::fixedUpdateMovers(f32 seconds)
{
    if (m_movers.empty())
    {
        return;
    }
    G7_PROFILE_SCOPE("movers");
    bool blocksChanged = false;
    for (auto& [id, m] : m_movers)
    {
        if (m.course.moving() && hasBounds(m.modelBounds))
        {
            const world::MoverCourse was = m.course;
            const bool arrived = m.course.advance(seconds, m.mover.seconds);
            placeMover(id, m);
            const entt::entity e = m_scene.findById(world::VobId{id});
            std::vector<u32> inTheWay;
            if (was.closing() && e != entt::null)
            {
                const world::OrientedBox box = world::orientedBox(m.modelBounds, m_scene.worldMatrix(e));
                if (m_player.valid())
                {
                    const physics::CharacterDesc& d = m_player.desc();
                    if (world::capsuleInBox(box, m_player.feet(), d.radius, d.height))
                    {
                        inTheWay.push_back(kHeroId);
                    }
                }
                for (const auto& c : m_creatures)
                {
                    if (c->body && !c->dead && !c->vanished)
                    {
                        const physics::CharacterDesc& d = c->body->desc();
                        if (world::capsuleInBox(box, c->body->feet(), d.radius, d.height))
                        {
                            inTheWay.push_back(c->id);
                        }
                    }
                }
            }
            if (!inTheWay.empty())
            {
                // Questions 2 and 3: it stops where it was and opens again; a trap hurts first.
                m.course = was;
                ++m.crushes;
                placeMover(id, m);
                moveMover(id, m, true);
                G7_LOG_INFO("engine", "mover {} stopped on {} in its way - opens again", m.name,
                            inTheWay.size());
                if (m.mover.crush == world::Mover::Crush::Damage)
                {
                    for (const u32 victim : inTheWay)
                    {
                        trapHit(victim, kCrushDamage, m.name);
                    }
                }
            }
            else if (arrived && !m.mover.soundStop.empty() && e != entt::null)
            {
                (void)playSound(m.mover.soundStop, Vec3(m_scene.worldMatrix(e)[3]));
            }
        }
        if (const bool blocks = world::moverBlocks(m.mover.kind, m.course); blocks != m.blocks)
        {
            m.blocks = blocks;
            blocksChanged = true;
        }
    }
    if (blocksChanged)
    {
        updateMoverBlocks();
    }
}

void Engine::updateMoverBlocks()
{
    std::vector<std::pair<u32, u32>> blocked;
    for (auto& [id, m] : m_movers)
    {
        if (!m.blocks || !hasBounds(m.modelBounds))
        {
            continue;
        }
        if (!m.edges)
        {
            // The edges through its closed pose (a trapdoor's: over its shaft), worked out once per waynet.
            m.edges.emplace();
            const entt::entity e = m_scene.findById(world::VobId{id});
            if (e == entt::null)
            {
                continue;
            }
            const Transform now = world::moverPose(m.closed, m.motion, m.course.position);
            const Mat4 parent = m_scene.worldMatrix(e) * glm::inverse(now.toMatrix());
            world::OrientedBox box = world::orientedBox(m.modelBounds, parent * m.closed.toMatrix());
            box.half.y += kEdgeHeightPad;
            const auto& points = m_waynet.points();
            for (u32 p = 0; p < points.size(); ++p)
            {
                for (const u32 q : m_waynet.neighbours(p))
                {
                    const Vec3 lift(0.0f, kEdgeLift, 0.0f);
                    if (q > p &&
                        world::segmentCrossesBox(box, points[p].position + lift, points[q].position + lift))
                    {
                        m.edges->emplace_back(p, q);
                    }
                }
            }
            if (m.edges->empty())
            {
                G7_LOG_DEBUG("engine", "mover {} lies on no waynet edge", m.name);
            }
        }
        blocked.insert(blocked.end(), m.edges->begin(), m.edges->end());
    }
    m_waynet.setBlockedEdges(std::move(blocked));
}

bool Engine::moverEvent(const world::NetEvent& event)
{
    for (auto& [id, m] : m_movers)
    {
        if (m.name != event.target)
        {
            continue;
        }
        const std::string_view action = event.action;
        if (action == "open" || action == "on")
        {
            moveMover(id, m, true);
        }
        else if (action == "close" || action == "off")
        {
            moveMover(id, m, false);
        }
        else if (action == "toggle" || action == "trigger")
        {
            moveMover(id, m, m.course.target < 0.5f);
        }
        return true;
    }
    return false;
}

std::optional<MoverInfo> Engine::moverInfo(std::string_view name) const
{
    for (const auto& [id, m] : m_movers)
    {
        if (m.name == name)
        {
            return MoverInfo{
                m.name,   m.mover.kind, m.course.target > 0.5f, m.course.position, m.course.moving(),
                m.blocks, m.crushes};
        }
    }
    return std::nullopt;
}

bool Engine::previewMover(std::string_view name, std::optional<bool> open)
{
    for (auto& [id, m] : m_movers)
    {
        if (m.name == name)
        {
            const f32 at = open.value_or(m.course.target < 0.5f) ? 1.0f : 0.0f;
            m.course.position = at;
            m.course.target = at;
            placeMover(id, m);
            m.blocks = world::moverBlocks(m.mover.kind, m.course);
            updateMoverBlocks();
            return true;
        }
    }
    return false;
}

void Engine::resetMovers()
{
    bool changed = false;
    for (auto& [id, m] : m_movers)
    {
        const f32 at = m.mover.open ? 1.0f : 0.0f;
        if (m.course.position != at || m.course.target != at)
        {
            m.course.position = at;
            m.course.target = at;
            placeMover(id, m);
            m.blocks = world::moverBlocks(m.mover.kind, m.course);
            changed = true;
        }
    }
    if (changed)
    {
        updateMoverBlocks();
    }
}

void Engine::storeMovers(world::WorldFile& file, bool withState) const
{
    for (world::WorldFileVob& vob : file.vobs)
    {
        if (const auto it = m_movers.find(vob.id.value); it != m_movers.end() && vob.mover)
        {
            vob.transform = it->second.closed;
            vob.mover->open = withState ? it->second.course.target > 0.5f : it->second.mover.open;
        }
    }
}

void Engine::bindMoverFunctions()
{
    using script::Value;
    m_scripts->bind(
        {"mover_state", "mover_state(name: string) -> {kind, open, position, moving, blocks} | nil",
         "Der Zustand des bewegten Teils `name` (Dungeon D2): Art (gate, secret_wall, trapdoor), ob es offen "
         "ist "
         "bzw. dorthin fährt, die Stellung 0 (zu) bis 1 (offen), ob es sich bewegt und ob es gerade Wege des "
         "Wegnetzes sperrt; nil, wenn es kein bewegtes Teil dieses Namens gibt. Bewegt wird es mit `send`.",
         "Welt", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (name)"};
             }
             const auto info = moverInfo(a[0].asString());
             if (!info)
             {
                 return Value();
             }
             return script::makeTable({}, {{"kind", std::string(kindName(info->kind))},
                                           {"open", info->open},
                                           {"position", static_cast<f64>(info->position)},
                                           {"moving", info->moving},
                                           {"blocks", info->blocks}});
         }});
}
} // namespace g7
